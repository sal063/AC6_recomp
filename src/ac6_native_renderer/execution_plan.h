#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "frame_plan.h"
#include "replay_ir.h"

namespace ac6::renderer {

enum class ExecutionCommandCategory : uint8_t {
  kNone = 0,
  kDraw = 1,
  kClear = 2,
  kResolve = 3,
};

struct ExecutionCommandPacket {
  ExecutionCommandCategory category = ExecutionCommandCategory::kNone;
  ObservedCommandType source_type = ObservedCommandType::kDraw;
  uint32_t replay_pass_index = 0;
  uint32_t replay_command_index = 0;
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
  ac6::d3d::ShadowState shadow_state{};
};

struct ExecutionResourceRequirements {
  bool needs_render_target = false;
  bool needs_depth_stencil = false;
  bool needs_vertex_streams = false;
  bool needs_index_buffer = false;
  bool needs_textures = false;
  bool needs_samplers = false;
  bool needs_fetch_constants = false;
  uint32_t max_texture_count = 0;
  uint32_t max_stream_count = 0;
  uint32_t max_sampler_count = 0;
  uint32_t max_fetch_constant_count = 0;
  uint32_t max_viewport_width = 0;
  uint32_t max_viewport_height = 0;
};

struct ExecutionPassPacket {
  std::string name;
  ReplayPassRole role = ReplayPassRole::kUnknown;
  bool replay_pass_valid = false;
  uint32_t replay_pass_index = 0;
  bool source_pass_valid = false;
  uint32_t source_pass_index = 0;
  uint32_t draw_count = 0;
  uint32_t clear_count = 0;
  uint32_t resolve_count = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  bool selected_for_present = false;
  ExecutionResourceRequirements resources{};
  std::vector<ExecutionCommandPacket> commands;
};

struct ExecutionFrameSummary {
  bool valid = false;
  uint64_t frame_index = 0;
  uint32_t pass_count = 0;
  uint32_t command_count = 0;
  uint32_t draw_packet_count = 0;
  uint32_t clear_packet_count = 0;
  uint32_t resolve_packet_count = 0;
  uint32_t present_pass_count = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  bool has_present_pass = false;
};

struct ExecutionFramePlan {
  ExecutionFrameSummary summary{};
  std::vector<ExecutionPassPacket> passes;
};

class ExecutionPlanBuilder {
 public:
  ExecutionFramePlan BuildBootstrapPlan(uint64_t frame_index) const;
  ExecutionFramePlan Build(const ReplayFrame& replay_frame,
                           const NativeFramePlan& frame_plan) const;
};

const char* ToString(ExecutionCommandCategory category);

}  // namespace ac6::renderer
