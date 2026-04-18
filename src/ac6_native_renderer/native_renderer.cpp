#include "native_renderer.h"

#include <string>

#include <rex/logging.h>

namespace ac6::renderer {
namespace {

RenderPassKind ToRenderPassKind(ReplayPassRole role) {
  switch (role) {
    case ReplayPassRole::kScene:
      return RenderPassKind::kScene;
    case ReplayPassRole::kPostProcess:
    case ReplayPassRole::kPresent:
      return RenderPassKind::kPostProcess;
    case ReplayPassRole::kUiComposite:
      return RenderPassKind::kUiComposite;
    case ReplayPassRole::kBootstrap:
      return RenderPassKind::kBootstrap;
    case ReplayPassRole::kUnknown:
    default:
      return RenderPassKind::kUnknown;
  }
}

RenderPassDesc BuildRenderPassDesc(const ReplayExecutorPassPacket& pass) {
  return RenderPassDesc{
      .name = pass.name,
      .kind = ToRenderPassKind(pass.role),
      .async_compute = false,
      .draw_count = pass.draw_count,
      .clear_count = pass.clear_count,
      .resolve_count = pass.resolve_count,
      .render_target_0 = pass.render_target_0,
      .depth_stencil = pass.depth_stencil,
      .viewport_width = pass.output_width,
      .viewport_height = pass.output_height,
      .selected_for_present = pass.selected_for_present,
  };
}

}  // namespace

NativeRenderer::NativeRenderer() = default;

NativeRenderer::~NativeRenderer() {
  Shutdown();
}

bool NativeRenderer::Initialize(const NativeRendererConfig& config) {
  Shutdown();

  config_ = config;
  scheduler_.Configure(config_.max_frames_in_flight);

  if (!device_.Initialize(config_)) {
    return false;
  }

  stats_.initialized = true;
  stats_.active_backend = device_.active_backend();
  stats_.frame_count = 0;
  stats_.built_pass_count = 0;
  stats_.backend_submit_count = 0;
  stats_.transient_allocation_count = 0;
  return true;
}

void NativeRenderer::Shutdown() {
  device_.Shutdown();
  graph_.Reset();
  frontend_.Reset();
  frame_plan_ = {};
  replay_frame_ = {};
  execution_plan_ = {};
  executor_frame_ = {};
  stats_ = {};
}

void NativeRenderer::BeginFrame() {
  if (!stats_.initialized) {
    return;
  }
  scheduler_.BeginFrame();
  ++stats_.frame_count;
  graph_.Reset();
}

void NativeRenderer::BuildBootstrapFrame() {
  if (!stats_.initialized) {
    return;
  }

  frame_plan_ = {};
  replay_frame_ = replay_builder_.BuildBootstrapFrame(scheduler_.frame_index());
  execution_plan_ = execution_builder_.BuildBootstrapPlan(scheduler_.frame_index());
  executor_frame_ = executor_builder_.BuildBootstrapFrame(scheduler_.frame_index());
  if (device_.SubmitExecutorFrame(executor_frame_)) {
    ++stats_.backend_submit_count;
  }

  // Phase-1: do not present. Build a minimal graph to prove deterministic
  // ownership without touching Rexglue emulation paths.
  for (const ReplayExecutorPassPacket& pass : executor_frame_.passes) {
    graph_.AddPass(BuildRenderPassDesc(pass));
  }
  stats_.built_pass_count += graph_.pass_count();

  REXLOG_TRACE("AC6 native renderer bootstrap frame built passes={}",
               graph_.pass_count());
}

void NativeRenderer::BuildCapturedFrame(
    const ac6::d3d::FrameCaptureSnapshot& frame_capture) {
  if (!stats_.initialized) {
    return;
  }

  const FrontendFrameSummary summary = frontend_.BuildFromCapture(frame_capture);
  if (!summary.capture_valid) {
    BuildBootstrapFrame();
    return;
  }

  frame_plan_ = planner_.Build(summary, frontend_.passes());
  replay_frame_ = replay_builder_.Build(summary, frontend_.passes(), frame_plan_);
  execution_plan_ = execution_builder_.Build(replay_frame_, frame_plan_);
  executor_frame_ = executor_builder_.Build(execution_plan_);
  if (device_.SubmitExecutorFrame(executor_frame_)) {
    ++stats_.backend_submit_count;
  }

  for (const ReplayExecutorPassPacket& pass : executor_frame_.passes) {
    graph_.AddPass(BuildRenderPassDesc(pass));
  }

  stats_.built_pass_count += graph_.pass_count();
  REXLOG_TRACE(
      "AC6 native renderer observed frame={} frontend_passes={} replay_passes={} replay_commands={} execution_passes={} execution_commands={} executor_passes={} executor_commands={} backend_submits={} selected={} draws={} clears={} resolves={} plan_valid={} out={}x{}",
      summary.frame_index, summary.pass_count, replay_frame_.summary.pass_count,
      replay_frame_.summary.command_count, execution_plan_.summary.pass_count,
      execution_plan_.summary.command_count, executor_frame_.summary.pass_count,
      executor_frame_.summary.command_count, stats_.backend_submit_count,
      summary.selected_pass_index,
      summary.total_draw_count, summary.total_clear_count,
      summary.total_resolve_count, frame_plan_.valid, frame_plan_.output_width,
      frame_plan_.output_height);
}

}  // namespace ac6::renderer
