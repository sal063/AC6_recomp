#include "replay_ir.h"

#include <string>

namespace ac6::renderer {
namespace {

ReplayPassRole ToReplayPassRole(ObservedPassKind kind) {
  switch (kind) {
    case ObservedPassKind::kScene:
      return ReplayPassRole::kScene;
    case ObservedPassKind::kPostProcess:
      return ReplayPassRole::kPostProcess;
    case ObservedPassKind::kUiComposite:
      return ReplayPassRole::kUiComposite;
    case ObservedPassKind::kUnknown:
    default:
      return ReplayPassRole::kUnknown;
  }
}

std::string BuildObservedPassName(const ObservedPassDesc& pass,
                                  uint32_t pass_index) {
  return "ac6.replay." + std::string(ToString(pass.kind)) + "." +
         std::to_string(pass_index);
}

ReplayCommandDesc BuildReplayCommand(const ObservedCommandDesc& command) {
  return ReplayCommandDesc{
      .type = command.type,
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

ReplayPassDesc BuildReplayPass(const ObservedPassDesc& pass, uint32_t pass_index) {
  ReplayPassDesc replay_pass;
  replay_pass.name = BuildObservedPassName(pass, pass_index);
  replay_pass.role = ToReplayPassRole(pass.kind);
  replay_pass.source_pass_valid = true;
  replay_pass.source_pass_index = pass_index;
  replay_pass.draw_count = pass.draw_count;
  replay_pass.clear_count = pass.clear_count;
  replay_pass.resolve_count = pass.resolve_count;
  replay_pass.render_target_0 = pass.render_target_0;
  replay_pass.depth_stencil = pass.depth_stencil;
  replay_pass.viewport_width = pass.viewport_width;
  replay_pass.viewport_height = pass.viewport_height;
  replay_pass.selected_for_present = pass.selected_for_present;
  replay_pass.commands.reserve(pass.commands.size());
  for (const ObservedCommandDesc& command : pass.commands) {
    replay_pass.commands.push_back(BuildReplayCommand(command));
  }
  return replay_pass;
}

void AccumulateSummary(ReplayFrameSummary& summary, const ReplayPassDesc& pass) {
  ++summary.pass_count;
  summary.command_count += static_cast<uint32_t>(pass.commands.size());
  summary.draw_count += pass.draw_count;
  summary.clear_count += pass.clear_count;
  summary.resolve_count += pass.resolve_count;
  summary.valid = summary.pass_count != 0;
}

}  // namespace

const char* ToString(ReplayPassRole role) {
  switch (role) {
    case ReplayPassRole::kBootstrap:
      return "bootstrap";
    case ReplayPassRole::kScene:
      return "scene";
    case ReplayPassRole::kPostProcess:
      return "post_process";
    case ReplayPassRole::kUiComposite:
      return "ui_composite";
    case ReplayPassRole::kPresent:
      return "present";
    case ReplayPassRole::kUnknown:
    default:
      return "unknown";
  }
}

ReplayFrame ReplayIrBuilder::BuildBootstrapFrame(uint64_t frame_index) const {
  ReplayFrame replay_frame;
  replay_frame.summary.frame_index = frame_index;

  ReplayPassDesc bootstrap_pass;
  bootstrap_pass.name = "ac6.replay.bootstrap";
  bootstrap_pass.role = ReplayPassRole::kBootstrap;
  bootstrap_pass.viewport_width = 0;
  bootstrap_pass.viewport_height = 0;

  AccumulateSummary(replay_frame.summary, bootstrap_pass);
  replay_frame.passes.push_back(std::move(bootstrap_pass));
  return replay_frame;
}

ReplayFrame ReplayIrBuilder::Build(
    const FrontendFrameSummary& summary,
    const std::vector<ObservedPassDesc>& passes,
    const NativeFramePlan& frame_plan) const {
  ReplayFrame replay_frame;
  replay_frame.summary.frame_index = summary.frame_index;
  replay_frame.summary.output_width = frame_plan.output_width;
  replay_frame.summary.output_height = frame_plan.output_height;

  if (!summary.capture_valid || passes.empty()) {
    return replay_frame;
  }

  replay_frame.passes.reserve(passes.size() + (frame_plan.requires_present_pass ? 1u : 0u));
  for (uint32_t i = 0; i < passes.size(); ++i) {
    ReplayPassDesc replay_pass = BuildReplayPass(passes[i], i);
    AccumulateSummary(replay_frame.summary, replay_pass);
    replay_frame.passes.push_back(std::move(replay_pass));
  }

  if (frame_plan.valid && frame_plan.requires_present_pass) {
    ReplayPassDesc present_pass;
    present_pass.name = "ac6.replay.present";
    present_pass.role = ReplayPassRole::kPresent;
    present_pass.source_pass_valid = frame_plan.present_stage.valid;
    present_pass.source_pass_index = frame_plan.present_stage.pass_index;
    present_pass.render_target_0 = frame_plan.present_stage.render_target_0;
    present_pass.depth_stencil = frame_plan.present_stage.depth_stencil;
    present_pass.viewport_width = frame_plan.output_width;
    present_pass.viewport_height = frame_plan.output_height;
    present_pass.selected_for_present = true;
    replay_frame.summary.has_present_pass = true;
    AccumulateSummary(replay_frame.summary, present_pass);
    replay_frame.passes.push_back(std::move(present_pass));
  }

  replay_frame.summary.valid =
      replay_frame.summary.pass_count != 0 &&
      (!frame_plan.valid ||
       (replay_frame.summary.output_width != 0 &&
        replay_frame.summary.output_height != 0));
  return replay_frame;
}

}  // namespace ac6::renderer
