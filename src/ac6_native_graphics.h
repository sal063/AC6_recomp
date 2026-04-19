#pragma once

#include <cstdint>
#include <string_view>

#include <rex/memory.h>

#include "ac6_backend_fixes/ac6_backend_hooks.h"
#include "ac6_native_renderer/ac6_render_frontend.h"
#include "ac6_native_renderer/execution_plan.h"
#include "ac6_native_renderer/frame_plan.h"
#include "ac6_native_renderer/replay_executor.h"
#include "ac6_native_renderer/replay_ir.h"
#include "ac6_native_renderer/types.h"
#include "d3d_state.h"

struct ID3D12Resource;

namespace ac6::graphics {

enum class GraphicsRuntimeMode : uint8_t {
  kDisabled,
  kAnalysisOnly,
  kHybridBackendFixes,
  kLegacyReplayExperimental,
};

std::string_view ToString(GraphicsRuntimeMode mode);

struct NativeGraphicsRuntimeStatus {
  bool enabled = false;
  GraphicsRuntimeMode mode = GraphicsRuntimeMode::kHybridBackendFixes;
  bool capture_enabled = false;
  bool authoritative_renderer_active = false;
  bool experimental_replay_present = false;
  bool initialized = false;
  bool had_init_failure = false;
  bool showing_latched_snapshot = false;
  uint64_t init_attempts = 0;
  uint64_t init_successes = 0;
  uint64_t analysis_frames_observed = 0;
  uint64_t replay_frames_built = 0;
  uint64_t latest_capture_frame_index = 0;
  uint64_t latest_renderer_frame_index = 0;
  uint64_t last_meaningful_capture_frame_index = 0;
  uint64_t last_meaningful_renderer_frame_index = 0;

  ac6::renderer::BackendType active_backend = ac6::renderer::BackendType::kUnknown;
  ac6::renderer::FeatureLevel feature_level = ac6::renderer::FeatureLevel::kBootstrap;
  ac6::renderer::NativeRendererStats renderer_stats{};
  ac6::renderer::FrontendFrameSummary frontend_summary{};
  ac6::renderer::ReplayFrameSummary replay_summary{};
  ac6::renderer::ExecutionFrameSummary execution_summary{};
  ac6::renderer::ReplayExecutorFrameSummary executor_summary{};
  ac6::renderer::BackendExecutorStatus backend_executor_status{};
  ac6::d3d::FrameCaptureSummary capture_summary{};
  ac6::backend::BackendDiagnosticsSnapshot backend_diagnostics{};
  ac6::renderer::NativeFramePlan frame_plan{};
};

void OnFrameBoundary(rex::memory::Memory* memory);
void Shutdown();

NativeGraphicsRuntimeStatus GetRuntimeStatus();
ID3D12Resource* GetNativeOutputTexture();

}  // namespace ac6::graphics
