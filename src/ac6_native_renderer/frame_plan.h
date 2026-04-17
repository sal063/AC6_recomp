#pragma once

#include <cstdint>
#include <vector>

#include "ac6_render_frontend.h"

namespace ac6::renderer {

struct PlannedPassReference {
  bool valid = false;
  uint32_t pass_index = 0;
  ObservedPassKind kind = ObservedPassKind::kUnknown;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t viewport_x = 0;
  uint32_t viewport_y = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
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
};

struct NativeFramePlan {
  bool valid = false;
  uint64_t frame_index = 0;
  uint32_t observed_pass_count = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  bool has_scene_stage = false;
  bool has_post_process_stage = false;
  bool has_ui_stage = false;
  bool requires_present_pass = false;
  bool present_from_selected_pass = false;
  uint32_t scene_stage_score = 0;
  uint32_t post_process_stage_score = 0;
  uint32_t ui_stage_score = 0;
  uint32_t present_stage_score = 0;
  PlannedPassReference scene_stage{};
  PlannedPassReference post_process_stage{};
  PlannedPassReference ui_stage{};
  PlannedPassReference present_stage{};
};

class FramePlanner {
 public:
  NativeFramePlan Build(const FrontendFrameSummary& summary,
                        const std::vector<ObservedPassDesc>& passes);
};

}  // namespace ac6::renderer
