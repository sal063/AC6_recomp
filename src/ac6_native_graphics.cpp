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

#include "d3d_hooks.h"
#include "render_hooks.h"

REXCVAR_DEFINE_BOOL(ac6_native_graphics_enabled, true, "AC6/NativeGraphics",
                    "Enable AC6 graphics capture analysis, overlay reporting, and backend fixes");
REXCVAR_DEFINE_STRING(ac6_graphics_mode, "hybrid_backend_fixes", "AC6/NativeGraphics",
                      "AC6 graphics runtime mode: disabled, analysis_only, hybrid_backend_fixes")
    .allowed({"disabled", "analysis_only", "hybrid_backend_fixes"});
REXCVAR_DEFINE_BOOL(ac6_force_safe_draw_resolution_scale, true, "AC6/NativeGraphics",
                    "Force AC6 hybrid backend fixes mode to use 1x draw resolution scaling until the scaled path is fixed");
REXCVAR_DEFINE_BOOL(ac6_force_safe_direct_host_resolve, true, "AC6/NativeGraphics",
                    "Force AC6 hybrid backend fixes mode to keep direct_host_resolve disabled until the AC6 crash is fixed");

namespace ac6::graphics {
namespace {

NativeGraphicsRuntimeStatus g_runtime_status{};
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
  return GraphicsRuntimeMode::kHybridBackendFixes;
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
    ac6::backend::ShutdownDiagnostics();
    return;
  }

  ac6::d3d::OnFrameBoundary();

  ac6::d3d::FrameCaptureSummary capture_summary;
  const ac6::d3d::FrameCaptureSnapshot frame_capture =
      ac6::d3d::TakeFrameCapture(&capture_summary);
  const ac6::d3d::ShadowState shadow_state = ac6::d3d::GetShadowState();

  ++g_runtime_status.analysis_frames_observed;
  g_runtime_status.capture_summary = capture_summary;
  g_runtime_status.latest_capture_frame_index = capture_summary.frame_index;
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
}

void Shutdown() {
  g_captured_memory.store(nullptr, std::memory_order_release);
}

NativeGraphicsRuntimeStatus GetRuntimeStatus() {
  SyncRuntimeFlags();
  g_runtime_status.backend_diagnostics = ac6::backend::GetDiagnosticsSnapshot();
  return g_runtime_status;
}

rex::memory::Memory* GetCapturedMemory() {
  return g_captured_memory.load(std::memory_order_acquire);
}

}  // namespace ac6::graphics
