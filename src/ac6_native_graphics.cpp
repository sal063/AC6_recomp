#include "ac6_native_graphics.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string_view>

#include <native/audio/audio_system.h>
#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/ui/d3d12/d3d12_provider.h>

#include "ac6_native_renderer/backends/d3d12_backend.h"
#include "ac6_native_renderer/native_renderer.h"
#include "d3d_hooks.h"
#include "render_hooks.h"

REXCVAR_DEFINE_BOOL(ac6_native_graphics_enabled, true, "AC6/NativeGraphics",
                    "Enable AC6 graphics capture analysis, overlay reporting, and backend fixes");
REXCVAR_DEFINE_BOOL(ac6_native_graphics_require_capture, false, "AC6/NativeGraphics",
                    "Keep render capture enabled while AC6 graphics analysis is active");
REXCVAR_DEFINE_BOOL(ac6_force_safe_render_capture, true, "AC6/NativeGraphics",
                    "Force AC6 hybrid backend fixes mode to keep per-draw render capture disabled until the capture path is stabilized");
REXCVAR_DEFINE_STRING(ac6_graphics_mode, "hybrid_backend_fixes", "AC6/NativeGraphics",
                      "AC6 graphics runtime mode: disabled, analysis_only, hybrid_backend_fixes, legacy_replay_experimental")
    .allowed({"disabled", "analysis_only", "hybrid_backend_fixes", "legacy_replay_experimental"});
REXCVAR_DEFINE_BOOL(ac6_force_safe_draw_resolution_scale, true, "AC6/NativeGraphics",
                    "Force AC6 hybrid backend fixes mode to use 1x draw resolution scaling until the scaled path is fixed");
REXCVAR_DEFINE_BOOL(ac6_force_safe_direct_host_resolve, true, "AC6/NativeGraphics",
                    "Force AC6 hybrid backend fixes mode to keep direct_host_resolve disabled until the AC6 crash is fixed");
REXCVAR_DEFINE_BOOL(ac6_experimental_replay_present, false, "AC6/NativeGraphics",
                    "Allow the legacy AC6 replay renderer to override the RexGlue swap source");
REXCVAR_DEFINE_STRING(ac6_native_graphics_backend, "auto", "AC6/NativeGraphics",
                      "Legacy experimental replay backend preference");
REXCVAR_DEFINE_STRING(ac6_native_graphics_feature_level, "scene_submission", "AC6/NativeGraphics",
                      "Legacy experimental replay feature level (shipping is a legacy scaffold label)")
    .allowed({"bootstrap", "scene_submission", "parity_validation", "shipping"});
REXCVAR_DEFINE_INT32(ac6_native_graphics_frames_in_flight, 2, "AC6/NativeGraphics",
                     "Legacy experimental replay max frames in flight")
    .range(1, 4);

namespace ac6::graphics {
namespace {

ac6::renderer::NativeRenderer g_native_renderer;
ac6::renderer::Ac6RenderFrontend g_capture_frontend;
ac6::renderer::FramePlanner g_capture_frame_planner;
NativeGraphicsRuntimeStatus g_runtime_status{};
ac6::renderer::D3D12Backend* g_d3d12_backend = nullptr;
std::atomic<rex::memory::Memory*> g_captured_memory{nullptr};
std::atomic_flag g_frame_boundary_active = ATOMIC_FLAG_INIT;
std::atomic<uint32_t> g_frame_boundary_reentry_count{0};

GraphicsRuntimeMode ParseGraphicsMode(std::string_view value) {
  if (value == "disabled") {
    return GraphicsRuntimeMode::kDisabled;
  }
  if (value == "analysis_only") {
    return GraphicsRuntimeMode::kAnalysisOnly;
  }
  if (value == "legacy_replay_experimental") {
    return GraphicsRuntimeMode::kLegacyReplayExperimental;
  }
  return GraphicsRuntimeMode::kHybridBackendFixes;
}

ac6::renderer::FeatureLevel ParseFeatureLevel(std::string_view value) {
  using ac6::renderer::FeatureLevel;
  if (value == "scene_submission") {
    return FeatureLevel::kSceneSubmission;
  }
  if (value == "parity_validation") {
    return FeatureLevel::kParityValidation;
  }
  if (value == "shipping") {
    return FeatureLevel::kShipping;
  }
  return FeatureLevel::kBootstrap;
}

bool IsReplayMode(const GraphicsRuntimeMode mode) {
  return mode == GraphicsRuntimeMode::kLegacyReplayExperimental;
}

void ResetReplayStatus() {
  g_runtime_status.initialized = false;
  g_runtime_status.replay_frames_built = 0;
  g_runtime_status.active_backend = ac6::renderer::BackendType::kUnknown;
  g_runtime_status.renderer_stats = {};
  g_runtime_status.replay_summary = {};
  g_runtime_status.execution_summary = {};
  g_runtime_status.executor_summary = {};
  g_runtime_status.backend_executor_status = {};
  g_runtime_status.latest_renderer_frame_index = 0;
  g_runtime_status.last_meaningful_renderer_frame_index = 0;
  g_runtime_status.showing_latched_snapshot = false;
}

void SyncRuntimeFlags() {
  g_runtime_status.enabled = REXCVAR_GET(ac6_native_graphics_enabled);
  g_runtime_status.mode = ParseGraphicsMode(REXCVAR_GET(ac6_graphics_mode));
  g_runtime_status.capture_enabled = REXCVAR_GET(ac6_render_capture);
  const uint32_t shared_scale =
      static_cast<uint32_t>(std::max(INT32_C(1), REXCVAR_GET(resolution_scale)));
  const bool use_shared_scale = rex::cvar::HasNonDefaultValue("resolution_scale");
  const int32_t configured_scale_x =
      use_shared_scale && !rex::cvar::HasNonDefaultValue("draw_resolution_scale_x")
          ? static_cast<int32_t>(shared_scale)
          : REXCVAR_GET(draw_resolution_scale_x);
  const int32_t configured_scale_y =
      use_shared_scale && !rex::cvar::HasNonDefaultValue("draw_resolution_scale_y")
          ? static_cast<int32_t>(shared_scale)
          : REXCVAR_GET(draw_resolution_scale_y);
  g_runtime_status.draw_resolution_scale_x =
      static_cast<uint32_t>(std::max(INT32_C(1), configured_scale_x));
  g_runtime_status.draw_resolution_scale_y =
      static_cast<uint32_t>(std::max(INT32_C(1), configured_scale_y));
  g_runtime_status.direct_host_resolve = REXCVAR_GET(direct_host_resolve);
  g_runtime_status.draw_resolution_scaled_texture_offsets =
      REXCVAR_GET(draw_resolution_scaled_texture_offsets);
  g_runtime_status.authoritative_renderer_active =
      g_runtime_status.enabled &&
      g_runtime_status.mode != GraphicsRuntimeMode::kDisabled;
  g_runtime_status.experimental_replay_present =
      g_runtime_status.enabled &&
      IsReplayMode(g_runtime_status.mode) &&
      REXCVAR_GET(ac6_experimental_replay_present);
}

void RefreshRuntimeStatusFromRenderer() {
  g_runtime_status.active_backend = g_native_renderer.GetStats().active_backend;
  g_runtime_status.feature_level = g_native_renderer.feature_level();
  g_runtime_status.renderer_stats = g_native_renderer.GetStats();
  g_runtime_status.frontend_summary = g_native_renderer.frontend_summary();
  g_runtime_status.replay_summary = g_native_renderer.replay_summary();
  g_runtime_status.execution_summary = g_native_renderer.execution_summary();
  g_runtime_status.executor_summary = g_native_renderer.executor_summary();
  g_runtime_status.backend_executor_status = g_native_renderer.backend_executor_status();
  g_runtime_status.frame_plan = g_native_renderer.frame_plan();
}

bool IsMeaningfulRendererSnapshot(
    const ac6::d3d::FrameCaptureSummary& capture_summary,
    const ac6::renderer::FrontendFrameSummary& frontend_summary,
    const ac6::renderer::ReplayFrameSummary& replay_summary,
    const ac6::renderer::ExecutionFrameSummary& execution_summary,
    const ac6::renderer::ReplayExecutorFrameSummary& executor_summary,
    const ac6::renderer::BackendExecutorStatus& backend_status) {
  return capture_summary.draw_count != 0 || capture_summary.clear_count != 0 ||
         capture_summary.resolve_count != 0 ||
         frontend_summary.total_command_count != 0 ||
         replay_summary.command_count != 0 ||
         execution_summary.command_count != 0 ||
         executor_summary.command_count != 0 ||
         backend_status.draw_attempt_count != 0 ||
         backend_status.clear_command_count != 0 ||
         backend_status.resolve_command_count != 0;
}

uint32_t ScoreObservedPassForDiagnostics(const ac6::renderer::ObservedPassDesc& pass) {
  const uint64_t viewport_area =
      uint64_t(pass.viewport_width) * uint64_t(pass.viewport_height);
  uint32_t score = 0;
  score += pass.selected_for_present ? 160u : 0u;
  score += pass.matches_frame_end_viewport ? 120u : 0u;
  score += pass.resolve_count * 80u;
  score += pass.draw_count * 4u;
  score += pass.clear_count * 6u;
  score += pass.max_texture_count * 3u;
  score += pass.max_stream_count * 2u;
  score += pass.max_sampler_count * 2u;
  score += static_cast<uint32_t>(std::min<uint64_t>(viewport_area / 32768u, 120u));
  switch (pass.kind) {
    case ac6::renderer::ObservedPassKind::kScene:
      score += 40u;
      break;
    case ac6::renderer::ObservedPassKind::kPostProcess:
      score += 50u;
      break;
    case ac6::renderer::ObservedPassKind::kUiComposite:
      score += 20u;
      break;
    case ac6::renderer::ObservedPassKind::kUnknown:
    default:
      break;
  }
  return score;
}

void RefreshPassDiagnostics(const std::vector<ac6::renderer::ObservedPassDesc>& passes) {
  g_runtime_status.pass_diagnostics_count = 0;
  g_runtime_status.pass_diagnostics = {};
  if (passes.empty()) {
    return;
  }

  struct RankedPass {
    uint32_t pass_index = 0;
    uint32_t score = 0;
  };

  std::vector<RankedPass> ranked;
  ranked.reserve(passes.size());
  for (uint32_t i = 0; i < passes.size(); ++i) {
    const auto& pass = passes[i];
    ranked.push_back({i, ScoreObservedPassForDiagnostics(pass)});
  }

  std::sort(ranked.begin(), ranked.end(), [&](const RankedPass& left, const RankedPass& right) {
    if (left.score != right.score) {
      return left.score > right.score;
    }
    return left.pass_index < right.pass_index;
  });

  const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(ranked.size()),
                                            static_cast<uint32_t>(g_runtime_status.pass_diagnostics.size()));
  g_runtime_status.pass_diagnostics_count = count;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& ranked_pass = ranked[i];
    const auto& pass = passes[ranked_pass.pass_index];
    g_runtime_status.pass_diagnostics[i] = {
        .valid = true,
        .pass_index = ranked_pass.pass_index,
        .kind = pass.kind,
        .score = ranked_pass.score,
        .render_target_0 = pass.render_target_0,
        .depth_stencil = pass.depth_stencil,
        .viewport_x = pass.viewport_x,
        .viewport_y = pass.viewport_y,
        .viewport_width = pass.viewport_width,
        .viewport_height = pass.viewport_height,
        .draw_count = pass.draw_count,
        .clear_count = pass.clear_count,
        .resolve_count = pass.resolve_count,
        .max_texture_count = pass.max_texture_count,
        .max_stream_count = pass.max_stream_count,
        .max_sampler_count = pass.max_sampler_count,
        .max_fetch_constant_count = pass.max_fetch_constant_count,
        .max_shader_gpr_alloc = pass.max_shader_gpr_alloc,
        .pass_signature = pass.pass_signature,
        .first_texture_fetch_layout_signature =
            pass.first_texture_fetch_layout_signature,
        .last_texture_fetch_layout_signature =
            pass.last_texture_fetch_layout_signature,
        .first_resource_binding_signature =
            pass.first_resource_binding_signature,
        .last_resource_binding_signature =
            pass.last_resource_binding_signature,
        .first_textures = {.count = pass.first_textures.count,
                           .slots = pass.first_textures.slots,
                           .values = pass.first_textures.values},
        .last_textures = {.count = pass.last_textures.count,
                          .slots = pass.last_textures.slots,
                          .values = pass.last_textures.values},
        .first_fetch_constants = {.count = pass.first_fetch_constants.count,
                                  .slots = pass.first_fetch_constants.slots,
                                  .values = pass.first_fetch_constants.values},
        .last_fetch_constants = {.count = pass.last_fetch_constants.count,
                                 .slots = pass.last_fetch_constants.slots,
                                 .values = pass.last_fetch_constants.values},
        .selected_for_present = pass.selected_for_present,
        .matches_frame_end_viewport = pass.matches_frame_end_viewport,
    };
  }
}

void RefreshResolveDiagnostics(const ac6::d3d::FrameCaptureSnapshot& frame_capture) {
  g_runtime_status.resolve_diagnostics_count = 0;
  g_runtime_status.resolve_diagnostics = {};
  if (frame_capture.resolves.empty()) {
    return;
  }

  const uint32_t total_resolves = static_cast<uint32_t>(frame_capture.resolves.size());
  const uint32_t count =
      std::min<uint32_t>(total_resolves,
                         static_cast<uint32_t>(g_runtime_status.resolve_diagnostics.size()));
  const uint32_t start_index = total_resolves - count;
  g_runtime_status.resolve_diagnostics_count = count;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& resolve = frame_capture.resolves[start_index + i];
    g_runtime_status.resolve_diagnostics[i] = {
        .valid = true,
        .sequence = resolve.sequence,
        .render_target_0 = resolve.shadow_state.render_targets[0],
        .depth_stencil = resolve.shadow_state.depth_stencil,
        .viewport_width = resolve.shadow_state.viewport.width,
        .viewport_height = resolve.shadow_state.viewport.height,
        .args = resolve.args,
        .depth_or_scale = resolve.depth_or_scale,
    };
  }
}

void ShutdownReplayRenderer() {
  g_d3d12_backend = nullptr;
  if (!g_runtime_status.initialized) {
    return;
  }
  g_native_renderer.Shutdown();
  ResetReplayStatus();
}

bool EnsureExperimentalReplayInitialized(rex::memory::Memory* memory) {
  if (!g_runtime_status.enabled || !IsReplayMode(g_runtime_status.mode)) {
    return false;
  }
  if (g_runtime_status.initialized) {
    return true;
  }

  ++g_runtime_status.init_attempts;
  auto* ts = rex::runtime::ThreadState::Get();
  if (!ts || !ts->context() || !ts->context()->kernel_state) {
    return false;
  }

  auto* graphics_system = ts->context()->kernel_state->graphics_system();
  if (!graphics_system || !graphics_system->provider()) {
    return false;
  }

  auto* d3d_provider =
      dynamic_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system->provider());
  if (!d3d_provider) {
    g_runtime_status.had_init_failure = true;
    return false;
  }

  ID3D12Device* device = d3d_provider->GetDevice();
  ID3D12CommandQueue* queue = d3d_provider->GetDirectQueue();
  if (!device || !queue) {
    return false;
  }

  ac6::renderer::NativeRendererConfig config;
  config.preferred_backend = ac6::renderer::BackendType::kD3D12;
  config.feature_level =
      ParseFeatureLevel(REXCVAR_GET(ac6_native_graphics_feature_level));
  config.max_frames_in_flight = static_cast<uint32_t>(
      std::clamp(REXCVAR_GET(ac6_native_graphics_frames_in_flight), 1, 4));
  config.enable_debug_markers = true;
  config.enable_validation = false;

  if (!g_native_renderer.InitializeShared(config, memory, device, queue)) {
    g_runtime_status.had_init_failure = true;
    return false;
  }

  g_d3d12_backend = g_native_renderer.GetD3D12Backend();
  ++g_runtime_status.init_successes;
  g_runtime_status.initialized = true;
  RefreshRuntimeStatusFromRenderer();
  REXLOG_INFO("AC6 graphics: legacy experimental replay renderer initialized");
  return true;
}

}  // namespace

std::string_view ToString(const GraphicsRuntimeMode mode) {
  switch (mode) {
    case GraphicsRuntimeMode::kDisabled:
      return "disabled";
    case GraphicsRuntimeMode::kAnalysisOnly:
      return "analysis_only";
    case GraphicsRuntimeMode::kHybridBackendFixes:
      return "hybrid_backend_fixes";
    case GraphicsRuntimeMode::kLegacyReplayExperimental:
      return "legacy_replay_experimental";
    default:
      return "unknown";
  }
}

void OnFrameBoundary(rex::memory::Memory* memory) {
  if (g_frame_boundary_active.test_and_set(std::memory_order_acquire)) {
    const uint32_t reentry_count =
        g_frame_boundary_reentry_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (reentry_count == 1 || (reentry_count % 64) == 0) {
      REXLOG_WARN("AC6 graphics: dropping re-entrant frame boundary callback (count={})",
                  reentry_count);
    }
    return;
  }

  struct FrameBoundaryScope {
    ~FrameBoundaryScope() {
      g_frame_boundary_active.clear(std::memory_order_release);
    }
  } frame_boundary_scope;

  SyncRuntimeFlags();
  g_captured_memory.store(memory, std::memory_order_release);

  if (!g_runtime_status.enabled || g_runtime_status.mode == GraphicsRuntimeMode::kDisabled) {
    ShutdownReplayRenderer();
    ac6::backend::ShutdownDiagnostics();
    return;
  }

  if (REXCVAR_GET(ac6_native_graphics_require_capture)) {
    REXCVAR_SET(ac6_render_capture, true);
    g_runtime_status.capture_enabled = true;
  }

  if (!IsReplayMode(g_runtime_status.mode)) {
    ShutdownReplayRenderer();
  }

  ac6::d3d::OnFrameBoundary();

  ac6::d3d::FrameCaptureSummary capture_summary;
  const ac6::d3d::FrameCaptureSnapshot frame_capture =
      ac6::d3d::TakeFrameCapture(&capture_summary);
  const ac6::d3d::ShadowState shadow_state = ac6::d3d::GetShadowState();

  ++g_runtime_status.analysis_frames_observed;
  g_runtime_status.capture_summary = capture_summary;
  g_runtime_status.latest_capture_frame_index = capture_summary.frame_index;
  g_runtime_status.frontend_summary = g_capture_frontend.BuildFromCapture(frame_capture);
  if (g_runtime_status.frontend_summary.capture_valid) {
    RefreshPassDiagnostics(g_capture_frontend.passes());
    RefreshResolveDiagnostics(frame_capture);
    g_runtime_status.frame_plan = g_capture_frame_planner.Build(
        g_runtime_status.frontend_summary, g_capture_frontend.passes());
  } else {
    g_runtime_status.pass_diagnostics_count = 0;
    g_runtime_status.pass_diagnostics = {};
    g_runtime_status.resolve_diagnostics_count = 0;
    g_runtime_status.resolve_diagnostics = {};
    g_runtime_status.frame_plan = {};
  }
  if (capture_summary.draw_count || capture_summary.clear_count ||
      capture_summary.resolve_count) {
    g_runtime_status.last_meaningful_capture_frame_index = capture_summary.frame_index;
  }

  rex::system::GraphicsSwapSubmission swap_submission{};
  uint64_t swap_sequence = 0;
  uint64_t guest_vblank_interval_ticks = 0;
  uint64_t last_guest_vblank_tick = 0;
  rex::audio::AudioTelemetrySnapshot audio_telemetry{};
  rex::audio::AudioClientTimingSnapshot audio_timing{};
  const rex::audio::AudioTelemetrySnapshot* audio_telemetry_ptr = nullptr;
  const rex::audio::AudioClientTimingSnapshot* audio_timing_ptr = nullptr;

  auto* ts = rex::runtime::ThreadState::Get();
  if (ts && ts->context() && ts->context()->kernel_state) {
    auto* kernel_state = ts->context()->kernel_state;
    if (auto* concrete_graphics =
            dynamic_cast<rex::graphics::GraphicsSystem*>(kernel_state->graphics_system())) {
      concrete_graphics->GetLastSwapSubmission(&swap_submission, &swap_sequence);
      guest_vblank_interval_ticks = concrete_graphics->guest_vblank_interval_ticks();
      last_guest_vblank_tick = concrete_graphics->last_vblank_interrupt_guest_tick();
    }
    if (auto* native_audio = kernel_state->native_audio_system()) {
      audio_telemetry = native_audio->GetTelemetrySnapshot();
      audio_telemetry_ptr = &audio_telemetry;
      if (audio_telemetry.active_clients != 0) {
        audio_timing = native_audio->GetClientTimingSnapshot(0);
        audio_timing_ptr = &audio_timing;
      }
    }
  }

  ac6::backend::AnalyzeFrameBoundary(
      frame_capture, capture_summary, shadow_state,
      swap_sequence ? &swap_submission : nullptr, swap_sequence,
      guest_vblank_interval_ticks, last_guest_vblank_tick, ac6::GetFrameStats(),
      audio_telemetry_ptr, audio_timing_ptr);
  g_runtime_status.backend_diagnostics = ac6::backend::GetDiagnosticsSnapshot();

  if (!IsReplayMode(g_runtime_status.mode)) {
    return;
  }
  if (!EnsureExperimentalReplayInitialized(memory)) {
    return;
  }

  g_native_renderer.BeginFrame();
  g_native_renderer.BuildCapturedFrame(frame_capture);
  g_runtime_status.replay_frames_built = g_native_renderer.GetStats().frame_count;
  g_runtime_status.latest_renderer_frame_index =
      g_native_renderer.GetStats().frame_count;

  const ac6::renderer::FrontendFrameSummary frontend_summary =
      g_native_renderer.frontend_summary();
  const ac6::renderer::ReplayFrameSummary replay_summary =
      g_native_renderer.replay_summary();
  const ac6::renderer::ExecutionFrameSummary execution_summary =
      g_native_renderer.execution_summary();
  const ac6::renderer::ReplayExecutorFrameSummary executor_summary =
      g_native_renderer.executor_summary();
  const ac6::renderer::BackendExecutorStatus backend_status =
      g_native_renderer.backend_executor_status();

  if (IsMeaningfulRendererSnapshot(capture_summary, frontend_summary, replay_summary,
                                   execution_summary, executor_summary, backend_status) ||
      g_runtime_status.last_meaningful_renderer_frame_index == 0) {
    RefreshRuntimeStatusFromRenderer();
    g_runtime_status.showing_latched_snapshot = false;
    g_runtime_status.last_meaningful_renderer_frame_index =
        g_runtime_status.latest_renderer_frame_index;
  } else {
    g_runtime_status.active_backend = g_native_renderer.GetStats().active_backend;
    g_runtime_status.feature_level = g_native_renderer.feature_level();
    g_runtime_status.renderer_stats = g_native_renderer.GetStats();
    g_runtime_status.showing_latched_snapshot = true;
  }
}

void Shutdown() {
  g_captured_memory.store(nullptr, std::memory_order_release);
  ShutdownReplayRenderer();
}

NativeGraphicsRuntimeStatus GetRuntimeStatus() {
  SyncRuntimeFlags();
  g_runtime_status.backend_diagnostics = ac6::backend::GetDiagnosticsSnapshot();
  return g_runtime_status;
}

ID3D12Resource* GetNativeOutputTexture() {
  SyncRuntimeFlags();
  if (!g_runtime_status.enabled || !g_runtime_status.initialized ||
      !IsReplayMode(g_runtime_status.mode) ||
      !REXCVAR_GET(ac6_experimental_replay_present)) {
    return nullptr;
  }
  return g_d3d12_backend ? g_d3d12_backend->GetOutputTexture() : nullptr;
}

rex::memory::Memory* GetCapturedMemory() {
  return g_captured_memory.load(std::memory_order_acquire);
}

}  // namespace ac6::graphics
