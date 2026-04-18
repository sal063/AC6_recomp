#include "ac6_native_graphics.h"

#include <mutex>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "ac6_native_renderer/native_renderer.h"
#include "d3d_hooks.h"

REXCVAR_DEFINE_BOOL(ac6_native_graphics_enabled, true, "AC6/NativeGraphics",
                    "Enable AC6 native renderer frame-plan execution from captured D3D state");
REXCVAR_DEFINE_BOOL(ac6_native_graphics_require_capture, true, "AC6/NativeGraphics",
                    "Force render-capture on while native graphics execution is enabled");
REXCVAR_DEFINE_STRING(ac6_native_graphics_backend, "auto", "AC6/NativeGraphics",
                      "Preferred native backend: auto, d3d12, vulkan, metal")
    .allowed({"auto", "d3d12", "vulkan", "metal"});
REXCVAR_DEFINE_STRING(ac6_native_graphics_feature_level, "scene_submission", "AC6/NativeGraphics",
                      "Native renderer feature level: bootstrap, scene_submission, parity_validation, shipping")
    .allowed({"bootstrap", "scene_submission", "parity_validation", "shipping"});
REXCVAR_DEFINE_INT32(ac6_native_graphics_frames_in_flight, 2, "AC6/NativeGraphics",
                     "Native renderer max frames in flight")
    .range(1, 4);

namespace ac6::graphics {
namespace {

std::mutex g_native_graphics_mutex;
ac6::renderer::NativeRenderer g_native_renderer;
NativeGraphicsRuntimeStatus g_runtime_status{};

ac6::renderer::BackendType ParseBackend(std::string_view value) {
  if (value == "d3d12") {
    return ac6::renderer::BackendType::kD3D12;
  }
  if (value == "vulkan") {
    return ac6::renderer::BackendType::kVulkan;
  }
  if (value == "metal") {
    return ac6::renderer::BackendType::kMetal;
  }
  return ac6::renderer::BackendType::kUnknown;
}

ac6::renderer::FeatureLevel ParseFeatureLevel(std::string_view value) {
  if (value == "bootstrap") {
    return ac6::renderer::FeatureLevel::kBootstrap;
  }
  if (value == "parity_validation") {
    return ac6::renderer::FeatureLevel::kParityValidation;
  }
  if (value == "shipping") {
    return ac6::renderer::FeatureLevel::kShipping;
  }
  return ac6::renderer::FeatureLevel::kSceneSubmission;
}

ac6::renderer::NativeRendererConfig BuildRendererConfig() {
  ac6::renderer::NativeRendererConfig config;
  config.preferred_backend = ParseBackend(REXCVAR_GET(ac6_native_graphics_backend));
  config.feature_level = ParseFeatureLevel(REXCVAR_GET(ac6_native_graphics_feature_level));
  config.max_frames_in_flight = static_cast<uint32_t>(REXCVAR_GET(ac6_native_graphics_frames_in_flight));
  config.enable_debug_markers = true;
  config.enable_validation = true;
  return config;
}

bool EnsureInitialized() {
  if (g_runtime_status.initialized) {
    return true;
  }

  ++g_runtime_status.init_attempts;
  const ac6::renderer::NativeRendererConfig config = BuildRendererConfig();
  if (!g_native_renderer.Initialize(config)) {
    g_runtime_status.had_init_failure = true;
    REXLOG_ERROR("AC6 native graphics failed to initialize backend={}",
                 ac6::renderer::ToString(ac6::renderer::ResolveBackend(config.preferred_backend)));
    return false;
  }

  g_runtime_status.initialized = true;
  g_runtime_status.had_init_failure = false;
  ++g_runtime_status.init_successes;
  g_runtime_status.feature_level = config.feature_level;
  return true;
}

void UpdateStatusFromRendererUnlocked() {
  g_runtime_status.renderer_stats = g_native_renderer.GetStats();
  g_runtime_status.active_backend = g_runtime_status.renderer_stats.active_backend;
  g_runtime_status.frontend_summary = g_native_renderer.frontend_summary();
  g_runtime_status.replay_summary = g_native_renderer.replay_summary();
  g_runtime_status.execution_summary = g_native_renderer.execution_summary();
  g_runtime_status.executor_summary = g_native_renderer.executor_summary();
  g_runtime_status.backend_executor_status =
      g_native_renderer.backend_executor_status();
  g_runtime_status.frame_plan = g_native_renderer.frame_plan();
}

}  // namespace

void OnFrameBoundary() {
  std::scoped_lock<std::mutex> lock(g_native_graphics_mutex);

  g_runtime_status.enabled = REXCVAR_GET(ac6_native_graphics_enabled);
  if (!g_runtime_status.enabled) {
    if (g_runtime_status.initialized) {
      g_native_renderer.Shutdown();
      g_runtime_status.initialized = false;
    }
    return;
  }

  if (REXCVAR_GET(ac6_native_graphics_require_capture) && !REXCVAR_GET(ac6_render_capture)) {
    REXCVAR_SET(ac6_render_capture, true);
  }

  if (!EnsureInitialized()) {
    return;
  }

  const ac6::d3d::FrameCaptureSnapshot frame_capture = ac6::d3d::GetFrameCapture();
  g_runtime_status.capture_summary = ac6::d3d::GetFrameCaptureSummary();

  g_native_renderer.BeginFrame();
  g_native_renderer.BuildCapturedFrame(frame_capture);
  ++g_runtime_status.frames_built;
  UpdateStatusFromRendererUnlocked();
}

void Shutdown() {
  std::scoped_lock<std::mutex> lock(g_native_graphics_mutex);
  if (!g_runtime_status.initialized) {
    return;
  }
  g_native_renderer.Shutdown();
  g_runtime_status.initialized = false;
}

NativeGraphicsRuntimeStatus GetRuntimeStatus() {
  std::scoped_lock<std::mutex> lock(g_native_graphics_mutex);
  return g_runtime_status;
}

}  // namespace ac6::graphics

