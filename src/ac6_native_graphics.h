#pragma once

#include <cstdint>

#include <rex/cvar.h>
#include <rex/runtime.h>

#include "d3d_state.h"

REXCVAR_DECLARE(bool, ac6_allow_gpu_trace_stream);

namespace ac6::graphics {

struct SelectedPassPreviewSummary {
  bool valid{false};
  uint32_t draw_count{0};
  uint32_t clear_count{0};
  uint32_t resolve_count{0};
  uint32_t sampled_clear_color_count{0};
  uint32_t sampled_clear_rect_count{0};
  uint32_t first_clear_color{0};
  uint32_t last_clear_color{0};
  bool using_clear_fill{false};
  bool using_raster_replay{false};
};

struct NativeReplayPlanSummary {
  bool valid{false};
  uint64_t frame_index{0};
  uint32_t pass_count{0};
  uint32_t swap_size_match_pass_count{0};
  uint32_t draw_event_count{0};
  uint32_t clear_event_count{0};
  uint32_t resolve_event_count{0};
  uint32_t largest_pass_draw_count{0};
  uint32_t first_pass_draw_count{0};
  uint32_t last_pass_draw_count{0};
  uint32_t selected_pass_index{0};
  uint32_t selected_pass_score{0};
  uint32_t selected_pass_start_sequence{0};
  uint32_t selected_pass_end_sequence{0};
  uint32_t selected_pass_draw_count{0};
  uint32_t selected_pass_clear_count{0};
  uint32_t selected_pass_resolve_count{0};
  uint32_t selected_pass_streak{0};
  bool selected_pass_is_last{false};
  bool selected_pass_has_resolve{false};
  bool selected_pass_is_stable{false};
  uint32_t present_candidate_rt0{0};
  uint32_t present_candidate_depth_stencil{0};
  uint32_t present_candidate_viewport_x{0};
  uint32_t present_candidate_viewport_y{0};
  uint32_t present_candidate_viewport_width{0};
  uint32_t present_candidate_viewport_height{0};
  bool present_candidate_matches_swap_size{false};
};

struct NativeGraphicsStatusSnapshot {
  bool bootstrap_enabled{false};
  bool backend_active{false};
  bool placeholder_present_enabled{false};
  bool provider_ready{false};
  bool presenter_ready{false};
  bool placeholder_resources_initialized{false};
  bool last_swap_intercepted{false};
  bool last_swap_fell_back{false};
  uint64_t total_swap_count{0};
  uint64_t intercepted_swap_count{0};
  uint64_t fallback_swap_count{0};
  uint32_t last_frontbuffer_virtual_address{0};
  uint32_t last_frontbuffer_physical_address{0};
  uint32_t last_frontbuffer_width{0};
  uint32_t last_frontbuffer_height{0};
  uint32_t last_texture_format{0};
  uint32_t last_color_space{0};
  d3d::FrameCaptureSummary capture_summary{};
  NativeReplayPlanSummary replay_plan{};
  SelectedPassPreviewSummary selected_pass_preview{};
};

void ConfigureGraphicsBackend(rex::RuntimeConfig& config);
NativeGraphicsStatusSnapshot GetNativeGraphicsStatus();

}  // namespace ac6::graphics
