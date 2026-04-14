#pragma once

#include <string>

#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/overlay/debug_overlay.h>
#include <rex/version.h>

#include "ac6_native_graphics.h"
#include "ac6_native_graphics_overlay.h"
#include "render_hooks.h"

REXCVAR_DECLARE(bool, vfetch_index_rounding_bias);
REXCVAR_DECLARE(bool, guest_vblank_sync_to_refresh);

class Ac6recompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Ac6recompApp>(new Ac6recompApp(ctx, "ac6recomp",
        PPCImageConfig));
  }

 protected:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    REXCVAR_SET(vfetch_index_rounding_bias, true);
    if (REXCVAR_GET(trace_gpu_stream) && !REXCVAR_GET(ac6_allow_gpu_trace_stream)) {
      gpu_trace_stream_blocked_ = true;
      REXLOG_WARN(
          "AC6 safety guard disabled trace_gpu_stream to avoid unstable GPU "
          "trace compression during native graphics experiments. Set "
          "ac6_allow_gpu_trace_stream=true to re-enable it explicitly.");
      REXCVAR_SET(trace_gpu_stream, false);
    }
    ac6::graphics::ConfigureGraphicsBackend(config);
    if (REXCVAR_GET(ac6_unlock_fps)) {
      REXCVAR_SET(vsync, false);
      REXCVAR_SET(guest_vblank_sync_to_refresh, false);
    }
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    debug_overlay()->SetStatsProvider([] {
      auto gs = ac6::GetFrameStats();
      return rex::ui::FrameStats{gs.frame_time_ms, gs.fps, gs.frame_count};
    });

    native_graphics_status_dialog_ =
        std::make_unique<ac6::graphics::NativeGraphicsStatusDialog>(drawer);

    if (window()) {
      std::string title = std::string(GetName()) + " " + REXGLUE_BUILD_TITLE;
      auto native_status = ac6::graphics::GetNativeGraphicsStatus();
      if (native_status.bootstrap_enabled) {
        title += native_status.placeholder_present_enabled ? " | native swap takeover"
                                                           : " | native swap observe";
      }
      if (gpu_trace_stream_blocked_) {
        title += " | trace stream blocked";
      }
      window()->SetTitle(title);
    }
  }

 private:
  bool gpu_trace_stream_blocked_ = false;
  std::unique_ptr<ac6::graphics::NativeGraphicsStatusDialog> native_graphics_status_dialog_;
};
