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

  if (!ImGui::Begin("AC6 Native Graphics##status", &visible_, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  const NativeGraphicsRuntimeStatus status = GetRuntimeStatus();
  ImGui::Text("enabled: %s", status.enabled ? "true" : "false");
  ImGui::Text("initialized: %s", status.initialized ? "true" : "false");
  ImGui::Text("init failures seen: %s", status.had_init_failure ? "true" : "false");
  ImGui::Text("init attempts/successes: %llu / %llu",
              static_cast<unsigned long long>(status.init_attempts),
              static_cast<unsigned long long>(status.init_successes));
  ImGui::Text("frames built: %llu", static_cast<unsigned long long>(status.frames_built));
  ImGui::Separator();
  ImGui::Text("backend: %s", ac6::renderer::ToString(status.active_backend).data());
  ImGui::Text("feature level: %s", ac6::renderer::ToString(status.feature_level).data());
  ImGui::Text("renderer frames: %llu",
              static_cast<unsigned long long>(status.renderer_stats.frame_count));
  ImGui::Text("render passes built: %llu",
              static_cast<unsigned long long>(status.renderer_stats.built_pass_count));
  ImGui::Separator();
  ImGui::Text("capture frame: %llu",
              static_cast<unsigned long long>(status.capture_summary.frame_index));
  ImGui::Text("capture draws/clears/resolves: %u / %u / %u",
              status.capture_summary.draw_count, status.capture_summary.clear_count,
              status.capture_summary.resolve_count);
  ImGui::Text("planned output: %ux%u", status.frame_plan.output_width,
              status.frame_plan.output_height);
  ImGui::Text("stages scene/post/ui: %s / %s / %s",
              status.frame_plan.has_scene_stage ? "yes" : "no",
              status.frame_plan.has_post_process_stage ? "yes" : "no",
              status.frame_plan.has_ui_stage ? "yes" : "no");

  ImGui::End();
}

}  // namespace ac6::graphics

