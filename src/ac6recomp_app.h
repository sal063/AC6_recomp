// ac6recomp - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/ui/overlay/debug_overlay.h>

#include "render_hooks.h"

REXCVAR_DECLARE(bool, vfetch_index_rounding_bias);

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
    // Fix black triangles caused by vertex fetch index precision (RCP).
    REXCVAR_SET(vfetch_index_rounding_bias, true);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    debug_overlay()->SetStatsProvider([] {
      auto gs = ac6::GetFrameStats();
      return rex::ui::FrameStats{gs.frame_time_ms, gs.fps, gs.frame_count};
    });
  }

  // void OnPostSetup() override {}
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
