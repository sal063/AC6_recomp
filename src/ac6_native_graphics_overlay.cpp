#include "ac6_native_graphics_overlay.h"

#include <imgui.h>

#include "ac6_native_graphics.h"

namespace ac6::graphics {

NativeGraphicsStatusDialog::NativeGraphicsStatusDialog(rex::ui::ImGuiDrawer* imgui_drawer)
    : ImGuiDialog(imgui_drawer) {}

NativeGraphicsStatusDialog::~NativeGraphicsStatusDialog() = default;

void NativeGraphicsStatusDialog::OnDraw(ImGuiIO& io) {
  (void)io;
  if (!visible_) {
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
  ImGui::Text("experimental replay present override: %s",
              status.experimental_replay_present ? "enabled" : "disabled");
  ImGui::Text("analysis frames / replay frames: %llu / %llu",
              static_cast<unsigned long long>(status.analysis_frames_observed),
              static_cast<unsigned long long>(status.replay_frames_built));

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
  ImGui::TextWrapped("signature tags: %s",
                     diagnostics.latest_signature_tags.empty()
                         ? "none"
                         : diagnostics.latest_signature_tags.c_str());
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

  if (status.mode == GraphicsRuntimeMode::kLegacyReplayExperimental) {
    ImGui::Separator();
    ImGui::TextUnformatted("legacy replay diagnostics (experimental):");
    ImGui::Text("initialized: %s", status.initialized ? "true" : "false");
    ImGui::Text("init failures seen: %s", status.had_init_failure ? "true" : "false");
    ImGui::Text("replay backend: %s",
                ac6::renderer::ToString(status.active_backend).data());
    ImGui::Text("replay feature level: %s",
                ac6::renderer::ToString(status.feature_level).data());
    ImGui::Text("frontend / replay / execution commands: %u / %u / %u",
                status.frontend_summary.total_command_count,
                status.replay_summary.command_count,
                status.execution_summary.command_count);
    ImGui::Text("backend draw attempts / success: %u / %u",
                status.backend_executor_status.draw_attempt_count,
                status.backend_executor_status.draw_success_count);
    ImGui::Text("planned output: %ux%u", status.frame_plan.output_width,
                status.frame_plan.output_height);
  }

  ImGui::End();
}

}  // namespace ac6::graphics
