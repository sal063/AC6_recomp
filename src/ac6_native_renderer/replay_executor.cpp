#include "replay_executor.h"

#include <utility>

namespace ac6::renderer {
namespace {

SubmissionQueueType SelectQueue(const ExecutionPassPacket& pass) {
  (void)pass;
  // Current scaffold keeps all work on the graphics queue until backend
  // implementations can prove safe async-compute or copy splits.
  return SubmissionQueueType::kGraphics;
}

ReplayExecutorCommandPacket BuildExecutorCommandPacket(
    const ExecutionCommandPacket& command, uint32_t execution_pass_index,
    uint32_t execution_command_index) {
  const bool is_draw = command.category == ExecutionCommandCategory::kDraw;
  return ReplayExecutorCommandPacket{
      .category = command.category,
      .execution_pass_index = execution_pass_index,
      .execution_command_index = execution_command_index,
      .sequence = command.sequence,
      .requires_resource_translation =
          command.render_target_0 != 0 || command.depth_stencil != 0 ||
          command.texture_count != 0 || command.stream_count != 0 ||
          command.fetch_constant_count != 0,
      .requires_pipeline_state = is_draw,
      .requires_descriptor_setup =
          is_draw &&
          (command.texture_count != 0 || command.sampler_count != 0 ||
           command.fetch_constant_count != 0),
      .touches_render_target = command.render_target_0 != 0,
      .touches_depth_stencil = command.depth_stencil != 0,
  };
}

ReplayExecutorPassPacket BuildExecutorPassPacket(const ExecutionPassPacket& pass,
                                                 uint32_t execution_pass_index) {
  ReplayExecutorPassPacket executor_pass;
  executor_pass.name = pass.name;
  executor_pass.role = pass.role;
  executor_pass.queue = SelectQueue(pass);
  executor_pass.execution_pass_valid = true;
  executor_pass.execution_pass_index = execution_pass_index;
  executor_pass.draw_count = pass.draw_count;
  executor_pass.clear_count = pass.clear_count;
  executor_pass.resolve_count = pass.resolve_count;
  executor_pass.render_target_0 = pass.render_target_0;
  executor_pass.depth_stencil = pass.depth_stencil;
  executor_pass.output_width = pass.output_width;
  executor_pass.output_height = pass.output_height;
  executor_pass.selected_for_present = pass.selected_for_present;
  executor_pass.requires_present = pass.selected_for_present;
  executor_pass.resources = pass.resources;
  executor_pass.commands.reserve(pass.commands.size());

  for (uint32_t i = 0; i < pass.commands.size(); ++i) {
    ReplayExecutorCommandPacket command_packet =
        BuildExecutorCommandPacket(pass.commands[i], execution_pass_index, i);
    executor_pass.requires_resource_translation |=
        command_packet.requires_resource_translation;
    executor_pass.requires_pipeline_state |= command_packet.requires_pipeline_state;
    executor_pass.requires_descriptor_setup |=
        command_packet.requires_descriptor_setup;
    executor_pass.commands.push_back(std::move(command_packet));
  }

  executor_pass.requires_resource_translation |=
      pass.resources.needs_render_target || pass.resources.needs_depth_stencil ||
      pass.resources.needs_vertex_streams || pass.resources.needs_index_buffer ||
      pass.resources.needs_textures || pass.resources.needs_fetch_constants;
  executor_pass.requires_pipeline_state |= pass.draw_count != 0;
  executor_pass.requires_descriptor_setup |=
      pass.resources.needs_textures || pass.resources.needs_samplers ||
      pass.resources.needs_fetch_constants;
  executor_pass.requires_present |= pass.role == ReplayPassRole::kPresent;
  return executor_pass;
}

void AccumulateSummary(ReplayExecutorFrameSummary& summary,
                       const ReplayExecutorPassPacket& pass) {
  ++summary.pass_count;
  summary.command_count += static_cast<uint32_t>(pass.commands.size());

  switch (pass.queue) {
    case SubmissionQueueType::kGraphics:
      ++summary.graphics_pass_count;
      break;
    case SubmissionQueueType::kAsyncCompute:
      ++summary.async_compute_pass_count;
      break;
    case SubmissionQueueType::kCopy:
      ++summary.copy_pass_count;
      break;
    case SubmissionQueueType::kUnknown:
    default:
      break;
  }

  if (pass.requires_present) {
    ++summary.present_pass_count;
    summary.has_present_pass = true;
  }
  if (pass.requires_resource_translation) {
    ++summary.resource_translation_pass_count;
  }
  if (pass.requires_pipeline_state) {
    ++summary.pipeline_state_pass_count;
  }
  if (pass.requires_descriptor_setup) {
    ++summary.descriptor_setup_pass_count;
  }
  summary.valid = summary.pass_count != 0;
}

}  // namespace

const char* ToString(SubmissionQueueType queue) {
  switch (queue) {
    case SubmissionQueueType::kGraphics:
      return "graphics";
    case SubmissionQueueType::kAsyncCompute:
      return "async_compute";
    case SubmissionQueueType::kCopy:
      return "copy";
    case SubmissionQueueType::kUnknown:
    default:
      return "unknown";
  }
}

ReplayExecutorFrame ReplayExecutorPlanBuilder::BuildBootstrapFrame(
    uint64_t frame_index) const {
  ReplayExecutorFrame frame;
  frame.summary.frame_index = frame_index;

  ReplayExecutorPassPacket bootstrap_pass;
  bootstrap_pass.name = "ac6.executor.bootstrap";
  bootstrap_pass.role = ReplayPassRole::kBootstrap;
  bootstrap_pass.queue = SubmissionQueueType::kGraphics;

  AccumulateSummary(frame.summary, bootstrap_pass);
  frame.passes.push_back(std::move(bootstrap_pass));
  return frame;
}

ReplayExecutorFrame ReplayExecutorPlanBuilder::Build(
    const ExecutionFramePlan& execution_plan) const {
  ReplayExecutorFrame frame;
  frame.summary.frame_index = execution_plan.summary.frame_index;
  frame.summary.output_width = execution_plan.summary.output_width;
  frame.summary.output_height = execution_plan.summary.output_height;

  if (!execution_plan.summary.valid || execution_plan.passes.empty()) {
    return frame;
  }

  frame.passes.reserve(execution_plan.passes.size());
  for (uint32_t i = 0; i < execution_plan.passes.size(); ++i) {
    ReplayExecutorPassPacket pass =
        BuildExecutorPassPacket(execution_plan.passes[i], i);
    AccumulateSummary(frame.summary, pass);
    frame.passes.push_back(std::move(pass));
  }

  frame.summary.valid =
      frame.summary.pass_count != 0 &&
      (frame.summary.output_width != 0 || frame.summary.output_height != 0 ||
       execution_plan.summary.frame_index != 0 ||
       !frame.passes.empty());
  return frame;
}

}  // namespace ac6::renderer
