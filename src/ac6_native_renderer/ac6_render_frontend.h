#pragma once

#include <cstdint>
#include <vector>

#include "d3d_state.h"

namespace ac6::renderer {

enum class ObservedPassKind : uint8_t {
  kUnknown = 0,
  kScene = 1,
  kPostProcess = 2,
  kUiComposite = 3,
};

enum class ObservedCommandType : uint8_t {
  kDraw = 0,
  kClear = 1,
  kResolve = 2,
};

struct ObservedCommandDesc {
  ObservedCommandType type = ObservedCommandType::kDraw;
  uint32_t sequence = 0;
  ac6::d3d::DrawCallKind draw_kind = ac6::d3d::DrawCallKind::kIndexed;
  uint32_t primitive_type = 0;
  uint32_t start = 0;
  uint32_t count = 0;
  uint32_t flags = 0;
  uint32_t rect_count = 0;
  uint32_t captured_rect_count = 0;
  uint32_t color = 0;
  uint32_t stencil = 0;
  float depth = 1.0f;
  uint32_t texture_count = 0;
  uint32_t stream_count = 0;
  uint32_t sampler_count = 0;
  uint32_t fetch_constant_count = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t viewport_x = 0;
  uint32_t viewport_y = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
};

struct ObservedPassDesc {
  ObservedPassKind kind = ObservedPassKind::kUnknown;
  uint32_t start_sequence = 0;
  uint32_t end_sequence = 0;
  uint32_t draw_count = 0;
  uint32_t clear_count = 0;
  uint32_t resolve_count = 0;
  uint32_t indexed_draw_count = 0;
  uint32_t indexed_shared_draw_count = 0;
  uint32_t primitive_draw_count = 0;
  uint32_t max_texture_count = 0;
  uint32_t max_stream_count = 0;
  uint32_t max_sampler_count = 0;
  uint32_t max_fetch_constant_count = 0;
  uint32_t max_draw_call_count = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t viewport_x = 0;
  uint32_t viewport_y = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
  bool selected_for_present = false;
  bool matches_frame_end_viewport = false;
  std::vector<ObservedCommandDesc> commands;
};

struct FrontendFrameSummary {
  bool capture_valid = false;
  uint64_t frame_index = 0;
  uint32_t pass_count = 0;
  uint32_t total_command_count = 0;
  uint32_t scene_pass_count = 0;
  uint32_t post_process_pass_count = 0;
  uint32_t ui_pass_count = 0;
  uint32_t selected_pass_index = 0;
  uint32_t total_draw_count = 0;
  uint32_t total_clear_count = 0;
  uint32_t total_resolve_count = 0;
};

class Ac6RenderFrontend {
 public:
  void Reset();
  FrontendFrameSummary BuildFromCapture(const ac6::d3d::FrameCaptureSnapshot& frame_capture);

  const FrontendFrameSummary& summary() const { return summary_; }
  const std::vector<ObservedPassDesc>& passes() const { return passes_; }

 private:
  FrontendFrameSummary summary_{};
  std::vector<ObservedPassDesc> passes_;
};

const char* ToString(ObservedPassKind kind);
const char* ToString(ObservedCommandType type);

}  // namespace ac6::renderer
