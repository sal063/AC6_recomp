#pragma once

#include <cstdint>

#include "ac6_native_renderer/frame_plan.h"
#include "ac6_native_renderer/types.h"
#include "d3d_state.h"

namespace ac6::graphics {

struct NativeGraphicsRuntimeStatus {
  bool enabled = false;
  bool initialized = false;
  bool had_init_failure = false;
  uint64_t init_attempts = 0;
  uint64_t init_successes = 0;
  uint64_t frames_built = 0;

  ac6::renderer::BackendType active_backend = ac6::renderer::BackendType::kUnknown;
  ac6::renderer::FeatureLevel feature_level = ac6::renderer::FeatureLevel::kBootstrap;
  ac6::renderer::NativeRendererStats renderer_stats{};
  ac6::d3d::FrameCaptureSummary capture_summary{};
  ac6::renderer::NativeFramePlan frame_plan{};
};

void OnFrameBoundary();
void Shutdown();

NativeGraphicsRuntimeStatus GetRuntimeStatus();

}  // namespace ac6::graphics

