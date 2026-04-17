#include "frame_plan.h"

namespace ac6::renderer {
namespace {

PlannedPassReference MakeReference(const ObservedPassDesc& pass, uint32_t pass_index) {
  return PlannedPassReference{
      .valid = true,
      .pass_index = pass_index,
      .kind = pass.kind,
      .render_target_0 = pass.render_target_0,
      .depth_stencil = pass.depth_stencil,
      .viewport_x = pass.viewport_x,
      .viewport_y = pass.viewport_y,
      .viewport_width = pass.viewport_width,
      .viewport_height = pass.viewport_height,
      .draw_count = pass.draw_count,
      .clear_count = pass.clear_count,
      .resolve_count = pass.resolve_count,
      .indexed_draw_count = pass.indexed_draw_count,
      .indexed_shared_draw_count = pass.indexed_shared_draw_count,
      .primitive_draw_count = pass.primitive_draw_count,
      .max_texture_count = pass.max_texture_count,
      .max_stream_count = pass.max_stream_count,
      .max_sampler_count = pass.max_sampler_count,
      .max_fetch_constant_count = pass.max_fetch_constant_count,
  };
}

uint32_t ScoreScenePass(const ObservedPassDesc& pass) {
  uint32_t score = 0;
  score += pass.draw_count * 8;
  score += pass.indexed_draw_count * 6;
  score += pass.indexed_shared_draw_count * 7;
  score += pass.primitive_draw_count * 2;
  score += pass.clear_count * 3;
  score += pass.max_texture_count * 6;
  score += pass.max_stream_count * 8;
  score += pass.max_sampler_count * 4;
  score += pass.max_fetch_constant_count * 4;
  score += pass.matches_frame_end_viewport ? 12u : 0u;
  if (pass.resolve_count == 0) {
    score += 16;
  }
  return score;
}

uint32_t ScorePostProcessPass(const ObservedPassDesc& pass, uint32_t pass_index,
                              uint32_t selected_index) {
  uint32_t score = 0;
  score += pass.resolve_count * 48;
  score += pass.clear_count * 4;
  score += std::min<uint32_t>(pass.draw_count, 24u) * 2;
  score += pass.max_texture_count * 10;
  score += pass.max_sampler_count * 5;
  score += pass.matches_frame_end_viewport ? 18u : 0u;
  score += pass.max_stream_count <= 2 ? 10u : 0u;
  score += pass_index <= selected_index ? 6u : 0u;
  return score;
}

uint32_t ScoreUiPass(const ObservedPassDesc& pass, uint32_t pass_index, uint32_t selected_index) {
  uint32_t score = 10;
  if (pass_index >= selected_index) {
    score += 15;
  }
  score += (pass.draw_count <= 16 ? 10u : 0u);
  score += (pass.matches_frame_end_viewport ? 5u : 0u);
  score += (pass.clear_count == 0 ? 8u : 0u);
  score += (pass.max_stream_count <= 2 ? 12u : 0u);
  score += (pass.max_texture_count <= 8 ? 8u : 0u);
  score += (pass.primitive_draw_count == 0 ? 6u : 0u);
  return score;
}

}  // namespace

NativeFramePlan FramePlanner::Build(const FrontendFrameSummary& summary,
                                    const std::vector<ObservedPassDesc>& passes) {
  NativeFramePlan plan;
  plan.frame_index = summary.frame_index;
  plan.observed_pass_count = summary.pass_count;
  if (!summary.capture_valid || passes.empty()) {
    return plan;
  }

  const uint32_t selected_index =
      summary.selected_pass_index < passes.size() ? summary.selected_pass_index
                                                  : uint32_t(passes.size() - 1);

  uint32_t best_scene_score = 0;
  bool have_scene = false;
  uint32_t best_ui_score = 0;
  bool have_ui = false;

  for (uint32_t i = 0; i < passes.size(); ++i) {
    const ObservedPassDesc& pass = passes[i];
    if (pass.kind == ObservedPassKind::kScene) {
      const uint32_t score = ScoreScenePass(pass);
      if (!have_scene || score > best_scene_score) {
        plan.scene_stage = MakeReference(pass, i);
        plan.scene_stage_score = score;
        best_scene_score = score;
        have_scene = true;
      }
    }
    if (pass.kind == ObservedPassKind::kPostProcess) {
      const uint32_t score = ScorePostProcessPass(pass, i, selected_index);
      if (!plan.post_process_stage.valid || score >= plan.post_process_stage_score) {
        plan.post_process_stage = MakeReference(pass, i);
        plan.post_process_stage_score = score;
      }
    }
    if (pass.kind == ObservedPassKind::kUiComposite) {
      const uint32_t score = ScoreUiPass(pass, i, selected_index);
      if (!have_ui || score >= best_ui_score) {
        plan.ui_stage = MakeReference(pass, i);
        plan.ui_stage_score = score;
        best_ui_score = score;
        have_ui = true;
      }
    }
  }

  plan.present_stage = MakeReference(passes[selected_index], selected_index);
  plan.present_stage_score =
      ScorePostProcessPass(passes[selected_index], selected_index, selected_index) +
      (passes[selected_index].selected_for_present ? 12u : 0u);
  plan.present_from_selected_pass = true;
  plan.has_scene_stage = plan.scene_stage.valid;
  plan.has_post_process_stage = plan.post_process_stage.valid;
  plan.has_ui_stage = plan.ui_stage.valid;
  plan.output_width = plan.present_stage.viewport_width;
  plan.output_height = plan.present_stage.viewport_height;
  plan.requires_present_pass = plan.present_stage.valid &&
                               (plan.has_post_process_stage || plan.has_ui_stage ||
                                plan.has_scene_stage);
  plan.valid = plan.present_stage.valid && plan.output_width != 0 && plan.output_height != 0;
  return plan;
}

}  // namespace ac6::renderer
