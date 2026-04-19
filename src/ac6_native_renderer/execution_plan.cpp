#include "execution_plan.h"

#include <algorithm>
#include <utility>

namespace ac6::renderer {
namespace {

ExecutionCommandCategory ToExecutionCommandCategory(ObservedCommandType type) {
  switch (type) {
    case ObservedCommandType::kDraw:
      return ExecutionCommandCategory::kDraw;
    case ObservedCommandType::kClear:
      return ExecutionCommandCategory::kClear;
    case ObservedCommandType::kResolve:
      return ExecutionCommandCategory::kResolve;
    default:
      return ExecutionCommandCategory::kNone;
  }
}

ExecutionCommandPacket BuildExecutionCommandPacket(const ReplayCommandDesc& command,
                                                  uint32_t replay_pass_index,
                                                  uint32_t replay_command_index) {
  return ExecutionCommandPacket{
      .category = ToExecutionCommandCategory(command.type),
      .source_type = command.type,
      .replay_pass_index = replay_pass_index,
      .replay_command_index = replay_command_index,
      .sequence = command.sequence,
      .draw_kind = command.draw_kind,
      .primitive_type = command.primitive_type,
      .start = command.start,
      .count = command.count,
      .flags = command.flags,
      .rect_count = command.rect_count,
      .captured_rect_count = command.captured_rect_count,
      .color = command.color,
      .stencil = command.stencil,
      .depth = command.depth,
      .texture_count = command.texture_count,
      .stream_count = command.stream_count,
      .sampler_count = command.sampler_count,
      .fetch_constant_count = command.fetch_constant_count,
      .render_target_0 = command.render_target_0,
      .depth_stencil = command.depth_stencil,
      .viewport_x = command.viewport_x,
      .viewport_y = command.viewport_y,
      .viewport_width = command.viewport_width,
      .viewport_height = command.viewport_height,
      .shadow_state = command.shadow_state,
  };
}

void AccumulateResourceRequirements(ExecutionResourceRequirements& resources,
                                    const ExecutionCommandPacket& command) {
  resources.needs_render_target |= command.render_target_0 != 0;
  resources.needs_depth_stencil |= command.depth_stencil != 0;
  resources.max_texture_count =
      std::max(resources.max_texture_count, command.texture_count);
  resources.max_stream_count =
      std::max(resources.max_stream_count, command.stream_count);
  resources.max_sampler_count =
      std::max(resources.max_sampler_count, command.sampler_count);
  resources.max_fetch_constant_count =
      std::max(resources.max_fetch_constant_count, command.fetch_constant_count);
  resources.max_viewport_width =
      std::max(resources.max_viewport_width, command.viewport_width);
  resources.max_viewport_height =
      std::max(resources.max_viewport_height, command.viewport_height);

  if (command.category != ExecutionCommandCategory::kDraw) {
    return;
  }

  resources.needs_vertex_streams |= command.stream_count != 0;
  resources.needs_index_buffer |= command.draw_kind != ac6::d3d::DrawCallKind::kPrimitive;
  resources.needs_textures |= command.texture_count != 0;
  resources.needs_samplers |= command.sampler_count != 0;
  resources.needs_fetch_constants |= command.fetch_constant_count != 0;
}

ExecutionPassPacket BuildExecutionPassPacket(const ReplayPassDesc& replay_pass,
                                             uint32_t replay_pass_index,
                                             const NativeFramePlan& frame_plan) {
  ExecutionPassPacket pass_packet;
  pass_packet.name = replay_pass.name;
  pass_packet.role = replay_pass.role;
  pass_packet.replay_pass_valid = true;
  pass_packet.replay_pass_index = replay_pass_index;
  pass_packet.source_pass_valid = replay_pass.source_pass_valid;
  pass_packet.source_pass_index = replay_pass.source_pass_index;
  pass_packet.draw_count = replay_pass.draw_count;
  pass_packet.clear_count = replay_pass.clear_count;
  pass_packet.resolve_count = replay_pass.resolve_count;
  pass_packet.render_target_0 = replay_pass.render_target_0;
  pass_packet.depth_stencil = replay_pass.depth_stencil;
  pass_packet.output_width = replay_pass.viewport_width;
  pass_packet.output_height = replay_pass.viewport_height;
  pass_packet.selected_for_present = replay_pass.selected_for_present;
  pass_packet.commands.reserve(replay_pass.commands.size());

  if (pass_packet.role == ReplayPassRole::kPresent) {
    pass_packet.output_width = frame_plan.output_width;
    pass_packet.output_height = frame_plan.output_height;
  }

  for (uint32_t i = 0; i < replay_pass.commands.size(); ++i) {
    ExecutionCommandPacket command_packet =
        BuildExecutionCommandPacket(replay_pass.commands[i], replay_pass_index, i);
    AccumulateResourceRequirements(pass_packet.resources, command_packet);
    pass_packet.commands.push_back(std::move(command_packet));
  }

  pass_packet.resources.needs_render_target |= pass_packet.render_target_0 != 0;
  pass_packet.resources.needs_depth_stencil |= pass_packet.depth_stencil != 0;
  pass_packet.resources.max_viewport_width =
      std::max(pass_packet.resources.max_viewport_width, pass_packet.output_width);
  pass_packet.resources.max_viewport_height =
      std::max(pass_packet.resources.max_viewport_height, pass_packet.output_height);
  return pass_packet;
}

void AccumulateSummary(ExecutionFrameSummary& summary,
                       const ExecutionPassPacket& pass_packet) {
  ++summary.pass_count;
  summary.command_count += static_cast<uint32_t>(pass_packet.commands.size());
  summary.draw_packet_count += pass_packet.draw_count;
  summary.clear_packet_count += pass_packet.clear_count;
  summary.resolve_packet_count += pass_packet.resolve_count;
  if (pass_packet.role == ReplayPassRole::kPresent) {
    ++summary.present_pass_count;
    summary.has_present_pass = true;
  }
  summary.valid = summary.pass_count != 0;
}

}  // namespace

const char* ToString(ExecutionCommandCategory category) {
  switch (category) {
    case ExecutionCommandCategory::kDraw:
      return "draw";
    case ExecutionCommandCategory::kClear:
      return "clear";
    case ExecutionCommandCategory::kResolve:
      return "resolve";
    case ExecutionCommandCategory::kNone:
    default:
      return "none";
  }
}

ExecutionFramePlan ExecutionPlanBuilder::BuildBootstrapPlan(uint64_t frame_index) const {
  ExecutionFramePlan plan;
  plan.summary.frame_index = frame_index;

  ExecutionPassPacket bootstrap_pass;
  bootstrap_pass.name = "ac6.execution.bootstrap";
  bootstrap_pass.role = ReplayPassRole::kBootstrap;

  AccumulateSummary(plan.summary, bootstrap_pass);
  plan.passes.push_back(std::move(bootstrap_pass));
  return plan;
}

ExecutionFramePlan ExecutionPlanBuilder::Build(
    const ReplayFrame& replay_frame, const NativeFramePlan& frame_plan) const {
  ExecutionFramePlan plan;
  plan.summary.frame_index = replay_frame.summary.frame_index;
  plan.summary.output_width = replay_frame.summary.output_width;
  plan.summary.output_height = replay_frame.summary.output_height;

  if (!replay_frame.summary.valid || replay_frame.passes.empty()) {
    return plan;
  }

  plan.passes.reserve(replay_frame.passes.size());
  for (uint32_t i = 0; i < replay_frame.passes.size(); ++i) {
    ExecutionPassPacket pass_packet =
        BuildExecutionPassPacket(replay_frame.passes[i], i, frame_plan);
    AccumulateSummary(plan.summary, pass_packet);
    plan.passes.push_back(std::move(pass_packet));
  }

  plan.summary.valid =
      plan.summary.pass_count != 0 &&
      (!frame_plan.valid ||
       (plan.summary.output_width != 0 && plan.summary.output_height != 0));
  return plan;
}

}  // namespace ac6::renderer
