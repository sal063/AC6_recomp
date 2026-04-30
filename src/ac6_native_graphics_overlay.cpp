#include "ac6_native_graphics_overlay.h"

#include <imgui.h>
#include <rex/cvar.h>

#include "ac6_native_graphics.h"

REXCVAR_DECLARE(bool, ac6_performance_mode);

extern void ApplyAc6PerformanceModeOverridesPublic();

namespace ac6::graphics {

NativeGraphicsStatusDialog::NativeGraphicsStatusDialog(rex::ui::ImGuiDrawer* imgui_drawer)
    : ImGuiDialog(imgui_drawer) {}

NativeGraphicsStatusDialog::~NativeGraphicsStatusDialog() = default;

void NativeGraphicsStatusDialog::OnDraw(ImGuiIO& io) {
  (void)io;

  ApplyAc6PerformanceModeOverridesPublic();

  if (REXCVAR_GET(ac6_performance_mode) || !visible_) {
    return;
  }

  if (!ImGui::Begin("AC6 Graphics Diagnostics##status", &visible_,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  const NativeGraphicsRuntimeStatus status = GetRuntimeStatus();
  const auto& diagnostics = status.backend_diagnostics;

  ImGui::Text("module: %s", status.enabled ? "enabled" : "disabled");
  ImGui::Text("mode: %.*s", static_cast<int>(ToString(status.mode).size()),
              ToString(status.mode).data());
  ImGui::Text("authoritative renderer: %s",
              status.authoritative_renderer_active ? "RexGlue/Xenia D3D12 backend"
                                                   : "disabled");
  ImGui::Text("capture active: %s", status.capture_enabled ? "yes" : "no");
  ImGui::Text("draw resolution scale: %ux%u", status.draw_resolution_scale_x,
              status.draw_resolution_scale_y);
  ImGui::Text("scaled tex offsets / direct host resolve: %s / %s",
              status.draw_resolution_scaled_texture_offsets ? "on" : "off",
              status.direct_host_resolve ? "on" : "off");
  ImGui::Text("analysis frames: %llu",
              static_cast<unsigned long long>(status.analysis_frames_observed));

  ImGui::Separator();
  ImGui::Text("capture frame: %llu",
              static_cast<unsigned long long>(status.capture_summary.frame_index));
  ImGui::Text("capture draws / clears / resolves: %u / %u / %u",
              status.capture_summary.draw_count, status.capture_summary.clear_count,
              status.capture_summary.resolve_count);
  ImGui::Text("capture indexed / shared / primitive: %u / %u / %u",
              status.capture_summary.indexed_draw_count,
              status.capture_summary.indexed_shared_draw_count,
              status.capture_summary.primitive_draw_count);
  ImGui::Text("capture rt0 switches / unique rt0: %u / %u",
              status.capture_summary.rt0_switch_count,
              status.capture_summary.unique_rt0_count);
  ImGui::Text("frame-end viewport: %ux%u",
              status.capture_summary.frame_end_viewport_width,
              status.capture_summary.frame_end_viewport_height);

  ImGui::Separator();
  ImGui::Text("swap source: %s", ac6::backend::ToString(diagnostics.swap_source));
  ImGui::Text("frontbuffer / guest output: %ux%u / %ux%u",
              diagnostics.frontbuffer_width, diagnostics.frontbuffer_height,
              diagnostics.guest_output_width, diagnostics.guest_output_height);
  ImGui::Text("swap source extent: %ux%u (%s)",
              diagnostics.source_width, diagnostics.source_height,
              diagnostics.swap_source_scaled ? "scaled" : "unscaled");
  ImGui::Text("present classification: %s",
              ac6::backend::ToString(diagnostics.latest_signature.classification));
  ImGui::Text("signature: %016llX hits=%u",
              static_cast<unsigned long long>(diagnostics.latest_signature.stable_id),
              diagnostics.repeated_signature_count);
  const uint32_t signature_viewport_width = diagnostics.latest_signature.viewport_width;
  const uint32_t signature_viewport_height = diagnostics.latest_signature.viewport_height;
  const uint32_t viewport_scale_x = diagnostics.frontbuffer_width
                                        ? (signature_viewport_width * 100) /
                                              diagnostics.frontbuffer_width
                                        : 0;
  const uint32_t viewport_scale_y = diagnostics.frontbuffer_height
                                        ? (signature_viewport_height * 100) /
                                              diagnostics.frontbuffer_height
                                        : 0;
  ImGui::Text("signature viewport: %ux%u (%u%% x %u%% of frontbuffer)",
              signature_viewport_width, signature_viewport_height,
              viewport_scale_x, viewport_scale_y);
  ImGui::Text("signature point-list / primitive draws: %u / %u",
              diagnostics.latest_signature.topology_pointlist_count,
              diagnostics.latest_signature.primitive_draw_count);
  ImGui::Text("effect hints: half_res=%s quarter_res=%s point_sprites=%s additive=%s",
              diagnostics.latest_signature.half_res_like ? "yes" : "no",
              diagnostics.latest_signature.quarter_res_like ? "yes" : "no",
              diagnostics.latest_signature.point_sprite_like ? "yes" : "no",
              diagnostics.latest_signature.additive_like ? "yes" : "no");
  ImGui::Text("sampler hints: point min/mip=%u/%u linear min/mip=%u/%u mip clamp=%u max mip=%u",
              diagnostics.latest_signature.point_min_sampler_count,
              diagnostics.latest_signature.point_mip_sampler_count,
              diagnostics.latest_signature.linear_min_sampler_count,
              diagnostics.latest_signature.linear_mip_sampler_count,
              diagnostics.latest_signature.mip_clamp_sampler_count,
              diagnostics.latest_signature.max_sampler_mip_level);
  ImGui::TextWrapped("signature tags: %s",
                     diagnostics.latest_signature_tags.empty()
                         ? "none"
                         : diagnostics.latest_signature_tags.c_str());
  const bool draw_scale_is_native =
      status.draw_resolution_scale_x <= 1 && status.draw_resolution_scale_y <= 1;
  const bool likely_point_filtered =
      diagnostics.latest_signature.point_min_sampler_count != 0 ||
      diagnostics.latest_signature.point_mip_sampler_count != 0;
  if (draw_scale_is_native) {
    ImGui::TextWrapped(
        "likely cause: guest output is still native 720p/1x, so particles and low-res effect "
        "passes will look blocky on higher-resolution displays.");
  } else if (!diagnostics.swap_source_scaled) {
    ImGui::TextWrapped(
        "likely cause: draw scaling is enabled, but the presented swap source is still "
        "unscaled. This points to an unscaled resolve / swap-texture path.");
  } else if (status.direct_host_resolve) {
    ImGui::TextWrapped(
        "likely cause: draw scaling is active and the swap source is scaled, so the failure is "
        "likely in the scaled resolve path rather than scaling being ignored.");
  } else if (diagnostics.latest_signature.point_sprite_like && likely_point_filtered) {
    ImGui::TextWrapped(
        "likely cause: the active effect pass looks like point-sprite rendering with point "
        "sampling, so close sprites can stay pixelated even when they are near the camera.");
  }
  ImGui::Text("authoritative VS / PS: %016llX / %016llX",
              static_cast<unsigned long long>(diagnostics.active_vertex_shader_hash),
              static_cast<unsigned long long>(diagnostics.active_pixel_shader_hash));
  ImGui::Text("vblank interval / last tick: %llu / %llu",
              static_cast<unsigned long long>(diagnostics.guest_vblank_interval_ticks),
              static_cast<unsigned long long>(diagnostics.last_guest_vblank_tick));
  ImGui::Text("host frame time / fps: %.2f ms / %.2f",
              diagnostics.host_frame_time_ms, diagnostics.host_fps);

  ImGui::Separator();
  ImGui::Text("audio backend: %s",
              diagnostics.audio_backend_name.empty()
                  ? "unavailable"
                  : diagnostics.audio_backend_name.c_str());
  ImGui::Text("audio clients / queued / peak: %u / %u / %u",
              diagnostics.audio_active_clients, diagnostics.audio_queued_frames,
              diagnostics.audio_peak_queued_frames);
  ImGui::Text("audio underruns / dropped / silence inject: %u / %u / %u",
              diagnostics.audio_underruns, diagnostics.audio_dropped_frames,
              diagnostics.audio_silence_injections);
  ImGui::Text("audio consumed / queued-played / submitted tic: %llu / %llu / %llu",
              static_cast<unsigned long long>(diagnostics.audio_consumed_frames),
              static_cast<unsigned long long>(diagnostics.audio_queued_played_frames),
              static_cast<unsigned long long>(diagnostics.audio_submitted_tic));
  ImGui::Text("audio host tic: %llu",
              static_cast<unsigned long long>(diagnostics.audio_host_elapsed_tic));
  ImGui::Text("audio startup inflight / callback dispatch / throttle: %u / %u / %u",
              diagnostics.audio_startup_inflight_frames,
              diagnostics.audio_callback_dispatch_count,
              diagnostics.audio_callback_throttle_count);

  ImGui::End();
}

}  // namespace ac6::graphics
