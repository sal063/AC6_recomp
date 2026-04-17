#include "native_renderer.h"

#include <string>

#include <rex/logging.h>

namespace ac6::renderer {
namespace {

RenderPassKind ToRenderPassKind(ObservedPassKind kind) {
  switch (kind) {
    case ObservedPassKind::kScene:
      return RenderPassKind::kScene;
    case ObservedPassKind::kPostProcess:
      return RenderPassKind::kPostProcess;
    case ObservedPassKind::kUiComposite:
      return RenderPassKind::kUiComposite;
    case ObservedPassKind::kUnknown:
    default:
      return RenderPassKind::kUnknown;
  }
}

std::string BuildObservedPassName(const ObservedPassDesc& pass,
                                  uint32_t pass_index) {
  return "ac6.observed." + std::string(ToString(pass.kind)) + "." +
         std::to_string(pass_index);
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
  stats_.transient_allocation_count = 0;
  return true;
}

void NativeRenderer::Shutdown() {
  device_.Shutdown();
  graph_.Reset();
  frontend_.Reset();
  frame_plan_ = {};
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

  // Phase-1: do not present. Build a minimal graph to prove deterministic
  // ownership without touching Rexglue emulation paths.
  graph_.AddPass(RenderPassDesc{
      .name = "ac6.native.bootstrap",
      .kind = RenderPassKind::kBootstrap,
      .async_compute = false,
  });
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

  uint32_t pass_index = 0;
  for (const ObservedPassDesc& pass : frontend_.passes()) {
    graph_.AddPass(RenderPassDesc{
        .name = BuildObservedPassName(pass, pass_index++),
        .kind = ToRenderPassKind(pass.kind),
        .async_compute = false,
        .draw_count = pass.draw_count,
        .clear_count = pass.clear_count,
        .resolve_count = pass.resolve_count,
        .render_target_0 = pass.render_target_0,
        .depth_stencil = pass.depth_stencil,
        .viewport_width = pass.viewport_width,
        .viewport_height = pass.viewport_height,
        .selected_for_present = pass.selected_for_present,
    });
  }
  if (frame_plan_.valid && frame_plan_.requires_present_pass) {
    graph_.AddPass(RenderPassDesc{
        .name = "ac6.plan.present",
        .kind = RenderPassKind::kPostProcess,
        .async_compute = false,
        .draw_count = 0,
        .clear_count = 0,
        .resolve_count = 0,
        .render_target_0 = frame_plan_.present_stage.render_target_0,
        .depth_stencil = frame_plan_.present_stage.depth_stencil,
        .viewport_width = frame_plan_.output_width,
        .viewport_height = frame_plan_.output_height,
        .selected_for_present = true,
    });
  }

  stats_.built_pass_count += graph_.pass_count();
  REXLOG_TRACE(
      "AC6 native renderer observed frame={} passes={} selected={} draws={} clears={} resolves={} plan_valid={} out={}x{}",
      summary.frame_index, summary.pass_count, summary.selected_pass_index,
      summary.total_draw_count, summary.total_clear_count,
      summary.total_resolve_count, frame_plan_.valid, frame_plan_.output_width,
      frame_plan_.output_height);
}

}  // namespace ac6::renderer
