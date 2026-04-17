#pragma once

#include <cstdint>

#include <rex/cvar.h>
#include <rex/runtime.h>

#include "ac6_native_renderer/types.h"
#include "d3d_state.h"

REXCVAR_DECLARE(bool, ac6_allow_gpu_trace_stream);
REXCVAR_DECLARE(bool, ac6_native_present_enabled);
REXCVAR_DECLARE(bool, ac6_native_present_force_pm4_fallback);
REXCVAR_DECLARE(bool, ac6_native_present_enable_postfx);
REXCVAR_DECLARE(bool, ac6_native_present_enable_ui_compose);
REXCVAR_DECLARE(bool, ac6_native_present_allow_unstable);

namespace ac6::graphics {

enum class SwapRoutingMode : uint32_t {
  kCompatPm4 = 0,
  kNativeAuthoritative = 1,
  kEmergencyHold = 2,
};

enum class SwapIngressPath : uint32_t {
  kUnknown = 0,
  kKernelDirectSwap = 1,
  kPm4XeSwap = 2,
};

enum class SwapExecutionPath : uint32_t {
  kUnknown = 0,
  kDirectPresentSwap = 1,
  kNativeRasterReplay = 2,
  kNativeMinimalUav = 3,
  kPm4Fallback = 4,
  kEmergencyHoldNoPresent = 5,
};

enum class CadencePolicy : uint32_t {
  kTimerVblank = 0,
  kPresentCompletion = 1,
  kHybridDebug = 2,
};

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
  bool using_scene_stage_intermediate{false};
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
  uint64_t decision_frame_index{0};
  bool bootstrap_enabled{false};
  bool parallel_renderer_enabled{false};
  bool parallel_renderer_initialized{false};
  bool backend_active{false};
  bool placeholder_present_enabled{false};
  bool native_present_enabled{false};
  bool native_present_authoritative{false};
  bool provider_ready{false};
  bool presenter_ready{false};
  bool placeholder_resources_initialized{false};
  bool last_swap_intercepted{false};
  bool last_swap_fell_back{false};
  bool effects_capture_available{false};
  bool effects_pass_classification_valid{false};
  bool parity_confidence_good{false};
  uint64_t total_swap_count{0};
  uint64_t intercepted_swap_count{0};
  uint64_t fallback_swap_count{0};
  uint32_t last_frontbuffer_virtual_address{0};
  uint32_t last_frontbuffer_physical_address{0};
  uint32_t last_frontbuffer_width{0};
  uint32_t last_frontbuffer_height{0};
  uint32_t last_texture_format{0};
  uint32_t last_color_space{0};
  uint32_t last_present_mode{0};
  uint32_t last_fallback_reason{0};
  uint32_t configured_output_scale_percent{100};
  uint32_t effective_output_width{0};
  uint32_t effective_output_height{0};
  uint32_t post_process_pass_count{0};
  uint32_t ui_pass_count{0};
  uint32_t scene_pass_count{0};
  uint32_t missing_effects_counter{0};
  uint32_t pixelation_counter{0};
  uint32_t parity_confidence_score{0};
  uint64_t parallel_renderer_frame_count{0};
  uint64_t parallel_renderer_built_pass_count{0};
  uint64_t parallel_renderer_transient_allocation_count{0};
  uint32_t parallel_renderer_frame_slot{0};
  uint32_t parallel_renderer_max_frames_in_flight{0};
  uint32_t parallel_renderer_scene_pass_count{0};
  uint32_t parallel_renderer_post_process_pass_count{0};
  uint32_t parallel_renderer_ui_pass_count{0};
  uint32_t parallel_renderer_selected_pass_index{0};
  uint32_t parallel_renderer_total_draw_count{0};
  uint32_t parallel_renderer_total_clear_count{0};
  uint32_t parallel_renderer_total_resolve_count{0};
  bool parallel_renderer_capture_valid{false};
  bool parallel_frame_plan_valid{false};
  bool parallel_frame_plan_has_scene_stage{false};
  bool parallel_frame_plan_has_post_process_stage{false};
  bool parallel_frame_plan_has_ui_stage{false};
  bool parallel_frame_plan_present_from_selected_pass{false};
  uint32_t parallel_frame_plan_scene_pass_index{0};
  uint32_t parallel_frame_plan_post_process_pass_index{0};
  uint32_t parallel_frame_plan_ui_pass_index{0};
  uint32_t parallel_frame_plan_present_pass_index{0};
  uint32_t parallel_frame_plan_scene_stage_score{0};
  uint32_t parallel_frame_plan_post_process_stage_score{0};
  uint32_t parallel_frame_plan_ui_stage_score{0};
  uint32_t parallel_frame_plan_present_stage_score{0};
  uint32_t parallel_frame_plan_scene_stage_draw_count{0};
  uint32_t parallel_frame_plan_post_process_stage_draw_count{0};
  uint32_t parallel_frame_plan_ui_stage_draw_count{0};
  uint32_t parallel_frame_plan_scene_stage_texture_peak{0};
  uint32_t parallel_frame_plan_post_process_stage_texture_peak{0};
  uint32_t parallel_frame_plan_ui_stage_texture_peak{0};
  uint32_t parallel_frame_plan_scene_stage_stream_peak{0};
  uint32_t parallel_frame_plan_post_process_stage_stream_peak{0};
  uint32_t parallel_frame_plan_ui_stage_stream_peak{0};
  uint32_t parallel_frame_plan_output_width{0};
  uint32_t parallel_frame_plan_output_height{0};
  SwapRoutingMode routing_mode{SwapRoutingMode::kCompatPm4};
  SwapIngressPath ingress_path{SwapIngressPath::kUnknown};
  SwapExecutionPath execution_path{SwapExecutionPath::kUnknown};
  CadencePolicy cadence_policy{CadencePolicy::kTimerVblank};
  renderer::BackendType parallel_renderer_backend{renderer::BackendType::kUnknown};
  renderer::FeatureLevel parallel_renderer_feature_level{renderer::FeatureLevel::kBootstrap};
  d3d::FrameCaptureSummary capture_summary{};
  NativeReplayPlanSummary replay_plan{};
  SelectedPassPreviewSummary selected_pass_preview{};
};

void ConfigureGraphicsBackend(rex::RuntimeConfig& config);
NativeGraphicsStatusSnapshot GetNativeGraphicsStatus();

}  // namespace ac6::graphics
