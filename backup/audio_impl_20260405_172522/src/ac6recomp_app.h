#pragma once

#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/logging/api.h>
#include <rex/rex_app.h>
#include <rex/ui/overlay/debug_overlay.h>

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
    // Preserve the previous project startup behavior unless the user overrides
    // it explicitly in config.
    REXCVAR_SET(ac6_unlock_fps, true);
    REXCVAR_SET(vsync, false);
    REXCVAR_SET(guest_vblank_sync_to_refresh, false);
    if (REXCVAR_GET(ac6_audio_deep_trace)) {
      if (REXCVAR_GET(log_level) == "info") {
        REXCVAR_SET(log_level, "debug");
      }
      if (REXCVAR_GET(log_file).empty()) {
        REXCVAR_SET(log_file, "ac6_audio_trace.log");
      }
    }
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    debug_overlay()->SetStatsProvider([] {
      auto gs = ac6::GetFrameStats();
      return rex::ui::FrameStats{gs.frame_time_ms, gs.fps, gs.frame_count};
    });
  }

};
