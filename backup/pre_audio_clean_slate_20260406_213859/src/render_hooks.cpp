#include "render_hooks.h"
#include "ac6_audio_policy.h"
#include "d3d_hooks.h"

#include <chrono>
#include <mutex>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(ac6_unlock_fps, false, "AC6", "Unlock frame rate to 60fps");
REXCVAR_DEFINE_BOOL(ac6_timing_hooks_enabled, true, "AC6",
                    "Enable AC6 timing hooks that alter the game's presentation cadence");

using Clock = std::chrono::steady_clock;

namespace {

std::mutex g_frame_mutex;
double g_frame_time_ms{0.0};
double g_fps{0.0};
uint64_t g_frame_count{0};
Clock::time_point g_frame_start{};

bool AreTimingHooksActive() {
    return REXCVAR_GET(ac6_timing_hooks_enabled) && REXCVAR_GET(ac6_unlock_fps);
}

}  // namespace

bool ac6FlipIntervalHook() {
    return AreTimingHooksActive() &&
           !ac6::audio_policy::ShouldKeepStockTimingForMovieAudio();
}

bool ac6PresentIntervalHook(PPCRegister& r10) {
    if (!AreTimingHooksActive() ||
        ac6::audio_policy::ShouldKeepStockTimingForMovieAudio()) {
        return false;
    }
    r10.u64 = 1;
    return true;
}

void ac6DeltaDivisorHook(PPCRegister& r29) {
    if (!AreTimingHooksActive() ||
        ac6::audio_policy::ShouldKeepStockTimingForMovieAudio()) {
        return;
    }
    r29.u64 = 30;
}

void ac6PresentTimingHook(PPCRegister& /*r31*/) {
    ac6::d3d::OnFrameBoundary();

    const auto now = Clock::now();
    double frame_time_ms = 0.0;
    double fps = 0.0;
    uint64_t frame_count = 0;
    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        if (g_frame_start.time_since_epoch().count() != 0) {
            g_frame_time_ms =
                std::chrono::duration<double, std::milli>(now - g_frame_start).count();
            g_fps = g_frame_time_ms > 0.0001 ? (1000.0 / g_frame_time_ms) : 0.0;
            ++g_frame_count;
        }
        g_frame_start = now;
        frame_time_ms = g_frame_time_ms;
        fps = g_fps;
        frame_count = g_frame_count;
    }

    ac6::audio_policy::OnPresentFrame(frame_time_ms, fps, frame_count,
                                      ac6::d3d::GetDrawStats());
}

namespace ac6 {

FrameStats GetFrameStats() {
    std::lock_guard<std::mutex> lock(g_frame_mutex);
    const auto movie_audio = audio_policy::GetMovieAudioSnapshot();
    return FrameStats{g_frame_time_ms,
                      g_fps,
                      g_frame_count,
                      movie_audio.movie_audio_active,
                      movie_audio.active_client_count,
                      movie_audio.register_count,
                      movie_audio.submit_count,
                      movie_audio.primary_owner,
                      movie_audio.primary_driver};
}

}  // namespace ac6
