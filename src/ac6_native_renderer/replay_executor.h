#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "execution_plan.h"

namespace ac6::renderer {

enum class SubmissionQueueType : uint8_t {
  kUnknown = 0,
  kGraphics = 1,
  kAsyncCompute = 2,
  kCopy = 3,
};

struct ReplayExecutorCommandPacket {
  ExecutionCommandCategory category = ExecutionCommandCategory::kNone;
  uint32_t execution_pass_index = 0;
  uint32_t execution_command_index = 0;
  uint32_t sequence = 0;
  bool requires_resource_translation = false;
  bool requires_pipeline_state = false;
  bool requires_descriptor_setup = false;
  bool touches_render_target = false;
  bool touches_depth_stencil = false;
  // Draw call dispatch fields (forwarded from ExecutionCommandPacket)
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
  ac6::d3d::ShadowState shadow_state{};
};

struct ReplayExecutorPassPacket {
  std::string name;
  ReplayPassRole role = ReplayPassRole::kUnknown;
  SubmissionQueueType queue = SubmissionQueueType::kUnknown;
  bool execution_pass_valid = false;
  uint32_t execution_pass_index = 0;
  uint32_t draw_count = 0;
  uint32_t clear_count = 0;
  uint32_t resolve_count = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  bool selected_for_present = false;
  bool requires_present = false;
  bool requires_resource_translation = false;
  bool requires_pipeline_state = false;
  bool requires_descriptor_setup = false;
  ExecutionResourceRequirements resources{};
  std::vector<ReplayExecutorCommandPacket> commands;
};

struct ReplayExecutorFrameSummary {
  bool valid = false;
  uint64_t frame_index = 0;
  uint32_t pass_count = 0;
  uint32_t command_count = 0;
  uint32_t graphics_pass_count = 0;
  uint32_t async_compute_pass_count = 0;
  uint32_t copy_pass_count = 0;
  uint32_t present_pass_count = 0;
  uint32_t resource_translation_pass_count = 0;
  uint32_t pipeline_state_pass_count = 0;
  uint32_t descriptor_setup_pass_count = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  bool has_present_pass = false;
};

struct ReplayExecutorFrame {
  ReplayExecutorFrameSummary summary{};
  std::vector<ReplayExecutorPassPacket> passes;
};

class ReplayExecutorPlanBuilder {
 public:
  ReplayExecutorFrame BuildBootstrapFrame(uint64_t frame_index) const;
  ReplayExecutorFrame Build(const ExecutionFramePlan& execution_plan) const;
};

const char* ToString(SubmissionQueueType queue);

}  // namespace ac6::renderer
