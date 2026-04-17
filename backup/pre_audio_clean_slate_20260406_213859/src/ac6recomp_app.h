#pragma once

#include <rex/cvar.h>
#include <rex/audio/audio_system.h>
#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/overlay/debug_overlay.h>

#include "ac6_audio_policy.h"
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
    if (REXCVAR_GET(ac6_unlock_fps)) {
      REXCVAR_SET(vsync, false);
      REXCVAR_SET(guest_vblank_sync_to_refresh, false);
    }
    if (REXCVAR_GET(ac6_audio_deep_trace)) {
      if (REXCVAR_GET(log_level) == "info") {
        REXCVAR_SET(log_level, "debug");
      }
      if (REXCVAR_GET(log_file).empty()) {
        REXCVAR_SET(log_file, "ac6_audio_trace.log");
      }
    }
  }

  void OnPostSetup() override {
    auto* audio_system =
        static_cast<rex::audio::AudioSystem*>(runtime()->audio_system());
    if (!audio_system) {
      REXLOG_INFO("AC6 audio path: runtime audio system unavailable");
      return;
    }

    const auto snapshot = audio_system->GetTelemetrySnapshot();
    const auto movie_audio = ac6::audio_policy::GetMovieAudioSnapshot();
    REXLOG_INFO(
        "AC6 audio path: runtime=AudioRuntime backend={} active_clients={} queued_frames={} "
        "trace_events={}",
        audio_system->GetBackendName(), snapshot.active_clients, snapshot.queued_frames,
        snapshot.trace_event_count);
    REXLOG_INFO(
        "AC6 audio policy: unlock_fps={} video_safe={} movie_audio_active={}",
        REXCVAR_GET(ac6_unlock_fps), REXCVAR_GET(ac6_unlock_fps_video_safe),
        movie_audio.movie_audio_active);
    REXLOG_INFO("AC6 presentation policy: vsync={} guest_vblank_sync_to_refresh={}",
        REXCVAR_GET(vsync), REXCVAR_GET(guest_vblank_sync_to_refresh));
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    debug_overlay()->SetStatsProvider([] {
      auto gs = ac6::GetFrameStats();
      return rex::ui::FrameStats{gs.frame_time_ms, gs.fps, gs.frame_count};
    });
  }

};
