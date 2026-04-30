#pragma once

#include <cstdint>
#include <string_view>

#include <rex/memory.h>

#include "ac6_backend_fixes/ac6_backend_hooks.h"
#include "d3d_state.h"

namespace ac6::graphics {

enum class GraphicsRuntimeMode : uint8_t {
  kDisabled,
  kAnalysisOnly,
  kHybridBackendFixes,
};

std::string_view ToString(GraphicsRuntimeMode mode);

struct NativeGraphicsRuntimeStatus {
  bool enabled = false;
  GraphicsRuntimeMode mode = GraphicsRuntimeMode::kHybridBackendFixes;
  bool capture_enabled = false;
  bool authoritative_renderer_active = false;
  uint32_t draw_resolution_scale_x = 1;
  uint32_t draw_resolution_scale_y = 1;
  bool direct_host_resolve = true;
  bool draw_resolution_scaled_texture_offsets = true;
  uint64_t analysis_frames_observed = 0;
  uint64_t latest_capture_frame_index = 0;
  uint64_t last_meaningful_capture_frame_index = 0;
  ac6::d3d::FrameCaptureSummary capture_summary{};
  ac6::backend::BackendDiagnosticsSnapshot backend_diagnostics{};
};

void OnFrameBoundary(rex::memory::Memory* memory);
void Shutdown();

NativeGraphicsRuntimeStatus GetRuntimeStatus();
rex::memory::Memory* GetCapturedMemory();

}  // namespace ac6::graphics
