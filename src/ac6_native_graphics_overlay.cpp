#include "ac6_native_graphics_overlay.h"

#include <rex/graphics/flags.h>
#include <rex/ui/keybinds.h>

#include <imgui.h>

#include "ac6_native_graphics.h"
#include "d3d_hooks.h"
#include "render_hooks.h"

namespace ac6::graphics {
namespace {

const char* DescribeNativeMode(const NativeGraphicsStatusSnapshot& status) {
  if (!status.bootstrap_enabled) {
    return "Disabled";
  }
  if (!status.backend_active) {
    return "Configured, backend inactive";
  }
  if (status.routing_mode == SwapRoutingMode::kEmergencyHold) {
    return "Emergency hold";
  }
  if (status.native_present_authoritative) {
    return "Native present authoritative";
  }
  if (status.native_present_enabled) {
    return "Native present gated (fallback active)";
  }
  if (status.placeholder_present_enabled) {
    return "Diagnostic takeover";
  }
  return "Observe only";
}

const char* DescribeRoutingMode(SwapRoutingMode mode) {
  switch (mode) {
    case SwapRoutingMode::kNativeAuthoritative:
      return "native";
    case SwapRoutingMode::kEmergencyHold:
      return "emergency";
    case SwapRoutingMode::kCompatPm4:
    default:
      return "compat";
  }
}

const char* DescribeExecutionPath(SwapExecutionPath path) {
  switch (path) {
    case SwapExecutionPath::kDirectPresentSwap:
      return "direct-present-swap";
    case SwapExecutionPath::kNativeRasterReplay:
      return "native-raster-replay";
    case SwapExecutionPath::kNativeMinimalUav:
      return "native-minimal-uav";
    case SwapExecutionPath::kPm4Fallback:
      return "pm4-fallback";
    case SwapExecutionPath::kEmergencyHoldNoPresent:
      return "emergency-hold";
    default:
      return "unknown";
  }
}

const char* DescribeCadencePolicy(CadencePolicy policy) {
  switch (policy) {
    case CadencePolicy::kPresentCompletion:
      return "present";
    case CadencePolicy::kHybridDebug:
      return "hybrid";
    case CadencePolicy::kTimerVblank:
    default:
      return "timer";
  }
}

const char* DescribeParallelRendererBackend(ac6::renderer::BackendType backend) {
  switch (backend) {
    case ac6::renderer::BackendType::kD3D12:
      return "d3d12";
    case ac6::renderer::BackendType::kVulkan:
      return "vulkan";
    case ac6::renderer::BackendType::kMetal:
      return "metal";
    case ac6::renderer::BackendType::kUnknown:
    default:
      return "unknown";
  }
}

const char* DescribeParallelRendererFeatureLevel(ac6::renderer::FeatureLevel level) {
  switch (level) {
    case ac6::renderer::FeatureLevel::kSceneSubmission:
      return "scene_submission";
    case ac6::renderer::FeatureLevel::kParityValidation:
      return "parity_validation";
    case ac6::renderer::FeatureLevel::kShipping:
      return "shipping";
    case ac6::renderer::FeatureLevel::kBootstrap:
    default:
      return "bootstrap";
  }
}

const char* DescribeFallbackReason(uint32_t reason) {
  switch (reason) {
    case 1:
      return "native present disabled";
    case 2:
      return "forced PM4 fallback";
    case 3:
      return "native resources unavailable";
    case 4:
      return "native composer failed";
    case 5:
      return "low parity confidence";
    default:
      return "none";
  }
}

const char* DescribeLastSwapOutcome(const NativeGraphicsStatusSnapshot& status) {
  if (!status.total_swap_count) {
    return "No direct swaps yet";
  }
  if (status.last_swap_intercepted) {
    return "Presented through native diagnostic path";
  }
  if (status.last_swap_fell_back) {
    return "Handled by legacy PM4 presentation";
  }
  return "Observed";
}

}  // namespace

NativeGraphicsStatusDialog::NativeGraphicsStatusDialog(rex::ui::ImGuiDrawer* imgui_drawer)
    : ImGuiDialog(imgui_drawer) {
  rex::ui::RegisterBind("bind_ac6_native_graphics_overlay", "F4",
                        "Toggle AC6 native graphics status overlay",
                        [this] { ToggleVisible(); });
}

NativeGraphicsStatusDialog::~NativeGraphicsStatusDialog() {
  rex::ui::UnregisterBind("bind_ac6_native_graphics_overlay");
}

void NativeGraphicsStatusDialog::OnDraw(ImGuiIO& io) {
  if (!visible_) {
    return;
  }

  NativeGraphicsStatusSnapshot native_status = GetNativeGraphicsStatus();
  ac6::FrameStats frame_stats = ac6::GetFrameStats();
  ac6::d3d::DrawStatsSnapshot draw_stats = ac6::d3d::GetDrawStats();

  ImGui::SetNextWindowPos(ImVec2(10, 84), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(430, 486), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.72f);
  if (!ImGui::Begin("AC6 Native Graphics##status", &visible_, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  ImGui::Text("Mode: %s", DescribeNativeMode(native_status));
  ImGui::Text("Backend: %s | Provider: %s | Presenter: %s",
              native_status.backend_active ? "active" : "inactive",
              native_status.provider_ready ? "ready" : "missing",
              native_status.presenter_ready ? "ready" : "missing");
  ImGui::Text("Parallel renderer: %s | backend=%s | level=%s",
              native_status.parallel_renderer_initialized ? "initialized" : "inactive",
              DescribeParallelRendererBackend(native_status.parallel_renderer_backend),
              DescribeParallelRendererFeatureLevel(
                  native_status.parallel_renderer_feature_level));
  ImGui::Text("Parallel renderer frames: %llu | passes=%llu | slot=%u/%u",
              static_cast<unsigned long long>(native_status.parallel_renderer_frame_count),
              static_cast<unsigned long long>(native_status.parallel_renderer_built_pass_count),
              native_status.parallel_renderer_frame_slot,
              native_status.parallel_renderer_max_frames_in_flight);
  ImGui::Text("Parallel capture: %s | selected pass=%u",
              native_status.parallel_renderer_capture_valid ? "observed frame" : "bootstrap only",
              native_status.parallel_renderer_selected_pass_index);
  ImGui::Text("Parallel pass classes: scene=%u postfx=%u ui=%u",
              native_status.parallel_renderer_scene_pass_count,
              native_status.parallel_renderer_post_process_pass_count,
              native_status.parallel_renderer_ui_pass_count);
  ImGui::Text("Parallel workload: draws=%u clears=%u resolves=%u",
              native_status.parallel_renderer_total_draw_count,
              native_status.parallel_renderer_total_clear_count,
              native_status.parallel_renderer_total_resolve_count);
  ImGui::Text("Parallel frame plan: %s | output=%ux%u | selected-present=%s",
              native_status.parallel_frame_plan_valid ? "valid" : "not ready",
              native_status.parallel_frame_plan_output_width,
              native_status.parallel_frame_plan_output_height,
              native_status.parallel_frame_plan_present_from_selected_pass ? "yes" : "no");
  ImGui::Text("Planned stages: scene=%s postfx=%s ui=%s present=%u",
              native_status.parallel_frame_plan_has_scene_stage ? "yes" : "no",
              native_status.parallel_frame_plan_has_post_process_stage ? "yes" : "no",
              native_status.parallel_frame_plan_has_ui_stage ? "yes" : "no",
              native_status.parallel_frame_plan_present_pass_index);
  ImGui::Text("Planned pass indices: scene=%u postfx=%u ui=%u",
              native_status.parallel_frame_plan_scene_pass_index,
              native_status.parallel_frame_plan_post_process_pass_index,
              native_status.parallel_frame_plan_ui_pass_index);
  ImGui::Text("Planned stage scores: scene=%u postfx=%u ui=%u present=%u",
              native_status.parallel_frame_plan_scene_stage_score,
              native_status.parallel_frame_plan_post_process_stage_score,
              native_status.parallel_frame_plan_ui_stage_score,
              native_status.parallel_frame_plan_present_stage_score);
  ImGui::Text("Stage draw counts: scene=%u postfx=%u ui=%u",
              native_status.parallel_frame_plan_scene_stage_draw_count,
              native_status.parallel_frame_plan_post_process_stage_draw_count,
              native_status.parallel_frame_plan_ui_stage_draw_count);
  ImGui::Text("Stage state peaks: tex %u/%u/%u | streams %u/%u/%u",
              native_status.parallel_frame_plan_scene_stage_texture_peak,
              native_status.parallel_frame_plan_post_process_stage_texture_peak,
              native_status.parallel_frame_plan_ui_stage_texture_peak,
              native_status.parallel_frame_plan_scene_stage_stream_peak,
              native_status.parallel_frame_plan_post_process_stage_stream_peak,
              native_status.parallel_frame_plan_ui_stage_stream_peak);
  ImGui::Text("Native present: %s | authoritative=%s | PM4 force fallback=%s",
              native_status.native_present_enabled ? "enabled" : "disabled",
              native_status.native_present_authoritative ? "yes" : "no",
              REXCVAR_GET(ac6_native_present_force_pm4_fallback) ? "yes" : "no");
  ImGui::Text("Routing: %s | Exec: %s | Cadence: %s",
              DescribeRoutingMode(native_status.routing_mode),
              DescribeExecutionPath(native_status.execution_path),
              DescribeCadencePolicy(native_status.cadence_policy));
  ImGui::Text("Runtime controls: postfx=%s ui_compose=%s allow_unstable=%s",
              REXCVAR_GET(ac6_native_present_enable_postfx) ? "on" : "off",
              REXCVAR_GET(ac6_native_present_enable_ui_compose) ? "on" : "off",
              REXCVAR_GET(ac6_native_present_allow_unstable) ? "on" : "off");
  ImGui::Text("Placeholder resources: %s",
              native_status.placeholder_resources_initialized ? "initialized" : "inactive");
  ImGui::Text("GPU trace stream: %s | allow=%s", REXCVAR_GET(trace_gpu_stream) ? "on" : "off",
              REXCVAR_GET(ac6_allow_gpu_trace_stream) ? "true" : "false");

  ImGui::Separator();
  ImGui::Text("Direct swaps: %llu total | %llu native | %llu legacy",
              static_cast<unsigned long long>(native_status.total_swap_count),
              static_cast<unsigned long long>(native_status.intercepted_swap_count),
              static_cast<unsigned long long>(native_status.fallback_swap_count));
  ImGui::Text("Last outcome: %s", DescribeLastSwapOutcome(native_status));
  ImGui::Text("Fallback reason: %s", DescribeFallbackReason(native_status.last_fallback_reason));
  ImGui::Text("Output scale: %u%% -> %ux%u", native_status.configured_output_scale_percent,
              native_status.effective_output_width, native_status.effective_output_height);
  ImGui::Text("Last frontbuffer: %ux%u fmt %08X cs %u", native_status.last_frontbuffer_width,
              native_status.last_frontbuffer_height, native_status.last_texture_format,
              native_status.last_color_space);
  ImGui::Text("Last FB VA/PA: %08X / %08X", native_status.last_frontbuffer_virtual_address,
              native_status.last_frontbuffer_physical_address);

  ImGui::Separator();
  ImGui::Text("Capture: %s frame=%llu draws=%u clears=%u resolves=%u",
              native_status.capture_summary.capture_enabled ? "enabled" : "disabled",
              static_cast<unsigned long long>(native_status.capture_summary.frame_index),
              native_status.capture_summary.draw_count, native_status.capture_summary.clear_count,
              native_status.capture_summary.resolve_count);
  if (native_status.capture_summary.record_signature_valid) {
    ImGui::Text("Record signature: %016llX",
                static_cast<unsigned long long>(native_status.capture_summary.record_signature));
    ImGui::Text("Recorded draw mix: indexed=%u shared=%u primitive=%u",
                native_status.capture_summary.indexed_draw_count,
                native_status.capture_summary.indexed_shared_draw_count,
                native_status.capture_summary.primitive_draw_count);
    ImGui::Text("Recorded RT0s: unique=%u switches=%u",
                native_status.capture_summary.unique_rt0_count,
                native_status.capture_summary.rt0_switch_count);
    ImGui::Text("Recorded RT0 first/last: %08X / %08X",
                native_status.capture_summary.first_draw_render_target_0,
                native_status.capture_summary.last_draw_render_target_0);
  } else {
    ImGui::TextUnformatted("Record signature: unavailable (enable ac6_render_capture)");
  }
  ImGui::Text("Frame-end RT0/DS: %08X / %08X",
              native_status.capture_summary.frame_end_render_target_0,
              native_status.capture_summary.frame_end_depth_stencil);
  ImGui::Text("Frame-end viewport: %ux%u | RTs=%u tex=%u fetch=%u",
              native_status.capture_summary.frame_end_viewport_width,
              native_status.capture_summary.frame_end_viewport_height,
              native_status.capture_summary.frame_end_render_target_count,
              native_status.capture_summary.frame_end_texture_count,
              native_status.capture_summary.frame_end_texture_fetch_count);
  ImGui::Text("Frame-end streams=%u samplers=%u | last draw prim=%u count=%u flags=%u",
              native_status.capture_summary.frame_end_stream_count,
              native_status.capture_summary.frame_end_sampler_count,
              native_status.capture_summary.last_draw_primitive_type,
              native_status.capture_summary.last_draw_count,
              native_status.capture_summary.last_draw_flags);

  ImGui::Separator();
  if (native_status.replay_plan.valid) {
    ImGui::Text("Replay plan: frame=%llu passes=%u draws=%u clears=%u resolves=%u",
                static_cast<unsigned long long>(native_status.replay_plan.frame_index),
                native_status.replay_plan.pass_count,
                native_status.replay_plan.draw_event_count,
                native_status.replay_plan.clear_event_count,
                native_status.replay_plan.resolve_event_count);
    ImGui::Text("Swap-size matching passes: %u | selected=%u score=%u",
                native_status.replay_plan.swap_size_match_pass_count,
                native_status.replay_plan.selected_pass_index,
                native_status.replay_plan.selected_pass_score);
    ImGui::Text("Pass draw counts: first=%u last=%u largest=%u",
                native_status.replay_plan.first_pass_draw_count,
                native_status.replay_plan.last_pass_draw_count,
                native_status.replay_plan.largest_pass_draw_count);
    ImGui::Text("Selected pass seq: %u..%u | draws=%u clears=%u resolves=%u",
                native_status.replay_plan.selected_pass_start_sequence,
                native_status.replay_plan.selected_pass_end_sequence,
                native_status.replay_plan.selected_pass_draw_count,
                native_status.replay_plan.selected_pass_clear_count,
                native_status.replay_plan.selected_pass_resolve_count);
    ImGui::Text("Selected pass flags: last=%s resolve=%s stable=%s streak=%u",
                native_status.replay_plan.selected_pass_is_last ? "yes" : "no",
                native_status.replay_plan.selected_pass_has_resolve ? "yes" : "no",
                native_status.replay_plan.selected_pass_is_stable ? "yes" : "no",
                native_status.replay_plan.selected_pass_streak);
    ImGui::Text("Present candidate RT0/DS: %08X / %08X",
                native_status.replay_plan.present_candidate_rt0,
                native_status.replay_plan.present_candidate_depth_stencil);
    ImGui::Text("Present candidate viewport: %u,%u %ux%u | swap match=%s",
                native_status.replay_plan.present_candidate_viewport_x,
                native_status.replay_plan.present_candidate_viewport_y,
                native_status.replay_plan.present_candidate_viewport_width,
                native_status.replay_plan.present_candidate_viewport_height,
                native_status.replay_plan.present_candidate_matches_swap_size ? "yes" : "no");
    if (native_status.selected_pass_preview.valid) {
      ImGui::Text("Selected-pass preview: source=%s sampled clears=%u captured rects=%u",
                  native_status.selected_pass_preview.using_clear_fill
                      ? "captured clear colors"
                      : "fallback candidate tint",
                  native_status.selected_pass_preview.sampled_clear_color_count,
                  native_status.selected_pass_preview.sampled_clear_rect_count);
      ImGui::Text("Preview events: draws=%u clears=%u resolves=%u",
                  native_status.selected_pass_preview.draw_count,
                  native_status.selected_pass_preview.clear_count,
                  native_status.selected_pass_preview.resolve_count);
      ImGui::Text("Preview renderer: %s",
                  native_status.selected_pass_preview.using_raster_replay
                      ? "offscreen raster replay"
                      : "legacy diagnostic fill");
      if (native_status.selected_pass_preview.clear_count) {
        ImGui::Text("Preview clear colors: first=%08X last=%08X",
                    native_status.selected_pass_preview.first_clear_color,
                    native_status.selected_pass_preview.last_clear_color);
      }
    }
  } else {
    ImGui::TextUnformatted("Replay plan: unavailable (no captured frame events yet)");
  }

  ImGui::Separator();
  ImGui::Text("Parity confidence: %u (%s)", native_status.parity_confidence_score,
              native_status.parity_confidence_good ? "good" : "low");
  ImGui::Text("Effects classification: %s | capture=%s",
              native_status.effects_pass_classification_valid ? "valid" : "pending",
              native_status.effects_capture_available ? "available" : "none");
  ImGui::Text("Pass classes: scene=%u postfx=%u ui=%u", native_status.scene_pass_count,
              native_status.post_process_pass_count, native_status.ui_pass_count);
  ImGui::Text("Visual issue counters: missing_fx=%u pixelation=%u",
              native_status.missing_effects_counter, native_status.pixelation_counter);

  ImGui::Separator();
  ImGui::Text("Guest frame: %.1f FPS (%.2f ms) frame=%llu", frame_stats.fps,
              frame_stats.frame_time_ms, static_cast<unsigned long long>(frame_stats.frame_count));
  ImGui::Text("D3D stats: draws=%u clears=%u resolves=%u", draw_stats.draw_calls,
              draw_stats.clear_calls, draw_stats.resolve_calls);
  ImGui::Text("Indexed=%u shared=%u primitive=%u", draw_stats.draw_calls_indexed,
              draw_stats.draw_calls_indexed_shared, draw_stats.draw_calls_primitive);

  ImGui::Separator();
  ImGui::TextUnformatted("F4 toggles this panel. F3 toggles the base debug overlay.");

  ImGui::End();
}

}  // namespace ac6::graphics
