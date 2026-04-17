#include "render_hooks.h"
#include "d3d_hooks.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/types.h>

REXCVAR_DEFINE_BOOL(ac6_unlock_fps, false, "AC6", "Unlock frame rate to 60fps");
REXCVAR_DEFINE_BOOL(ac6_audio_deep_trace, false, "AC6",
                    "Enable high-volume AC6 audio diagnostics across AC6 hooks, kernel audio, "
                    "worker cadence, and SDL queue telemetry");
REXCVAR_DEFINE_BOOL(ac6_timing_hooks_enabled, true, "AC6",
                    "Enable AC6 timing hooks that alter the game's presentation cadence");
REXCVAR_DEFINE_BOOL(ac6_unlock_fps_video_safe, true, "AC6",
                    "Keep stock timing while AC6 movie-audio clients are active");
REXCVAR_DEFINE_BOOL(ac6_movie_audio_trace, false, "AC6",
                    "Trace AC6 movie-audio registration, submission, and timing transitions");
REXCVAR_DEFINE_BOOL(ac6_movie_audio_trace_verbose, false, "AC6",
                    "Trace AC6 movie-audio cadence, draw stats, and per-frame timing while "
                    "movie audio is active");

using Clock = std::chrono::steady_clock;

namespace {

std::mutex g_frame_mutex;
double g_frame_time_ms{0.0};
double g_fps{0.0};
uint64_t g_frame_count{0};
Clock::time_point g_frame_start{};

struct MovieAudioState {
    uint32_t owner_ptr{0};
    uint32_t callback_ptr{0};
    uint32_t callback_arg{0};
    uint32_t driver_ptr{0};
    uint32_t last_samples_ptr{0};
    uint64_t register_count{0};
    uint64_t unregister_count{0};
    uint64_t submit_count{0};
    Clock::time_point last_register{};
    Clock::time_point last_submit{};
};

std::mutex g_movie_audio_mutex;
std::vector<MovieAudioState> g_movie_audio_clients{};
bool g_movie_audio_last_reported_active{false};
uint64_t g_movie_audio_duplicate_events{0};
uint64_t g_movie_audio_active_frame_trace_count{0};

double MillisecondsBetween(const Clock::time_point newer,
                           const Clock::time_point older) {
    if (older.time_since_epoch().count() == 0 ||
        newer.time_since_epoch().count() == 0) {
        return -1.0;
    }
    return std::chrono::duration<double, std::milli>(newer - older).count();
}

MovieAudioState* FindMovieAudioClientLocked(const uint32_t owner_ptr,
                                            const uint32_t driver_ptr) {
    if (driver_ptr != 0) {
        for (auto& client : g_movie_audio_clients) {
            if (client.driver_ptr == driver_ptr) {
                return &client;
            }
        }
    }
    if (owner_ptr != 0) {
        for (auto& client : g_movie_audio_clients) {
            if (client.owner_ptr == owner_ptr) {
                return &client;
            }
        }
    }
    return nullptr;
}

MovieAudioState& UpsertMovieAudioClientLocked(const uint32_t owner_ptr,
                                              const uint32_t driver_ptr) {
    if (auto* existing = FindMovieAudioClientLocked(owner_ptr, driver_ptr)) {
        return *existing;
    }
    g_movie_audio_clients.emplace_back();
    return g_movie_audio_clients.back();
}

const MovieAudioState* SelectPrimaryMovieAudioClientLocked() {
    if (g_movie_audio_clients.empty()) {
        return nullptr;
    }

    return &*std::max_element(
        g_movie_audio_clients.begin(), g_movie_audio_clients.end(),
        [](const MovieAudioState& lhs, const MovieAudioState& rhs) {
            const auto lhs_activity =
                lhs.last_submit.time_since_epoch().count() != 0 ? lhs.last_submit
                                                                : lhs.last_register;
            const auto rhs_activity =
                rhs.last_submit.time_since_epoch().count() != 0 ? rhs.last_submit
                                                                : rhs.last_register;
            return lhs_activity < rhs_activity;
        });
}

bool ComputeMovieAudioActiveLocked() {
    return !g_movie_audio_clients.empty();
}

bool IsAc6DeepTraceEnabled() {
    return REXCVAR_GET(ac6_audio_deep_trace);
}

void ReportMovieAudioStateTransitionLocked() {
    const bool active = ComputeMovieAudioActiveLocked();
    if (active == g_movie_audio_last_reported_active) {
        return;
    }

    g_movie_audio_last_reported_active = active;
    if (!active) {
        g_movie_audio_active_frame_trace_count = 0;
    }
    if (!(REXCVAR_GET(ac6_movie_audio_trace) || IsAc6DeepTraceEnabled())) {
        return;
    }

    const MovieAudioState* primary = SelectPrimaryMovieAudioClientLocked();
    double since_submit_ms = -1.0;
    double since_register_ms = -1.0;
    if (primary) {
        const auto now = Clock::now();
        since_submit_ms = MillisecondsBetween(now, primary->last_submit);
        since_register_ms = MillisecondsBetween(now, primary->last_register);
    }
    REXAPU_DEBUG(
        "AC6 movie-audio timing {} active_clients={} primary_owner={:08X} primary_driver={:08X} "
        "since_submit_ms={:.3f} since_register_ms={:.3f}",
        active ? "enabled" : "restored",
        g_movie_audio_clients.size(),
        primary ? primary->owner_ptr : 0u,
        primary ? primary->driver_ptr : 0u,
        since_submit_ms,
        since_register_ms);
}

void MaybeReportDuplicateMovieAudioClientsLocked(const char* reason) {
    if (g_movie_audio_clients.size() <= 1 ||
        !(REXCVAR_GET(ac6_movie_audio_trace) || IsAc6DeepTraceEnabled())) {
        return;
    }

    ++g_movie_audio_duplicate_events;
    const MovieAudioState* primary = SelectPrimaryMovieAudioClientLocked();
    REXAPU_WARN(
        "AC6 movie-audio duplicate clients after {}: active_clients={} duplicate_events={} "
        "primary_owner={:08X} primary_driver={:08X}",
        reason,
        g_movie_audio_clients.size(),
        g_movie_audio_duplicate_events,
        primary ? primary->owner_ptr : 0u,
        primary ? primary->driver_ptr : 0u);
}

bool IsMovieAudioActiveInternal() {
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    ReportMovieAudioStateTransitionLocked();
    return ComputeMovieAudioActiveLocked();
}

bool ShouldKeepStockTimingForMovieAudio() {
    return REXCVAR_GET(ac6_timing_hooks_enabled) &&
           REXCVAR_GET(ac6_unlock_fps_video_safe) &&
           IsMovieAudioActiveInternal();
}

}  // namespace

bool ac6FlipIntervalHook() {
    return REXCVAR_GET(ac6_timing_hooks_enabled) &&
           REXCVAR_GET(ac6_unlock_fps) &&
           !ShouldKeepStockTimingForMovieAudio();
}

bool ac6PresentIntervalHook(PPCRegister& r10) {
    if (REXCVAR_GET(ac6_timing_hooks_enabled) &&
        REXCVAR_GET(ac6_unlock_fps) &&
        !ShouldKeepStockTimingForMovieAudio()) {
        r10.u64 = 1;
        return true;
    }
    return false;
}

void ac6DeltaDivisorHook(PPCRegister& r29) {
    if (!REXCVAR_GET(ac6_timing_hooks_enabled) ||
        !REXCVAR_GET(ac6_unlock_fps) ||
        ShouldKeepStockTimingForMovieAudio()) {
        return;
    }
    r29.u64 = 30;
}

void ac6PresentTimingHook(PPCRegister& /*r31*/) {
    ac6::d3d::OnFrameBoundary();

    auto now = Clock::now();
    double ms = 0.0;
    float fps_val = 0.0f;
    uint64_t frame_count = 0;
    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        if (g_frame_start.time_since_epoch().count() != 0) {
            ms =
                std::chrono::duration<double, std::milli>(now - g_frame_start)
                    .count();
            fps_val = ms > 0.0001 ? static_cast<float>(1000.0 / ms) : 0.0f;

            g_frame_time_ms = ms;
            g_fps = static_cast<double>(fps_val);
            g_frame_count++;
        }
        g_frame_start = now;
        frame_count = g_frame_count;
    }

    if (!(REXCVAR_GET(ac6_movie_audio_trace_verbose) || IsAc6DeepTraceEnabled())) {
        return;
    }

    uint32_t active_clients = 0;
    uint32_t primary_owner = 0;
    uint32_t primary_driver = 0;
    uint64_t primary_submits = 0;
    double since_primary_submit_ms = -1.0;
    {
        std::lock_guard<std::mutex> movie_lock(g_movie_audio_mutex);
        if (!g_movie_audio_clients.empty()) {
            ++g_movie_audio_active_frame_trace_count;
            active_clients = static_cast<uint32_t>(g_movie_audio_clients.size());
            const MovieAudioState* primary = SelectPrimaryMovieAudioClientLocked();
            if (primary) {
                primary_owner = primary->owner_ptr;
                primary_driver = primary->driver_ptr;
                primary_submits = primary->submit_count;
                since_primary_submit_ms = MillisecondsBetween(now, primary->last_submit);
            }
        }
    }
    if (active_clients == 0) {
        return;
    }

    const ac6::d3d::DrawStatsSnapshot draw_stats = ac6::d3d::GetDrawStats();
    if (g_movie_audio_active_frame_trace_count <= 180 ||
        (g_movie_audio_active_frame_trace_count % 60) == 0 ||
        ms >= 40.0 || since_primary_submit_ms >= 40.0) {
        REXAPU_DEBUG(
            "AC6 movie-audio frame frame={} active_clients={} primary_owner={:08X} "
            "primary_driver={:08X} primary_submits={} frame_ms={:.3f} fps={:.3f} "
            "since_primary_submit_ms={:.3f} draws={} prim={} idx={} idx_shared={}",
            frame_count,
            active_clients,
            primary_owner,
            primary_driver,
            primary_submits,
            ms,
            static_cast<double>(fps_val),
            since_primary_submit_ms,
            draw_stats.draw_calls,
            draw_stats.draw_calls_primitive,
            draw_stats.draw_calls_indexed,
            draw_stats.draw_calls_indexed_shared);
    }
}

namespace ac6 {

FrameStats GetFrameStats() {
    std::lock_guard<std::mutex> frame_lock(g_frame_mutex);
    std::lock_guard<std::mutex> movie_lock(g_movie_audio_mutex);

    uint64_t register_count = 0;
    uint64_t submit_count = 0;
    for (const auto& client : g_movie_audio_clients) {
        register_count += client.register_count;
        submit_count += client.submit_count;
    }

    const MovieAudioState* primary = SelectPrimaryMovieAudioClientLocked();
    return FrameStats{
        g_frame_time_ms,
        g_fps,
        g_frame_count,
        ComputeMovieAudioActiveLocked(),
        static_cast<uint32_t>(g_movie_audio_clients.size()),
        register_count,
        submit_count,
        primary ? primary->owner_ptr : 0u,
        primary ? primary->driver_ptr : 0u,
    };
}

bool IsMovieAudioActive() {
    return IsMovieAudioActiveInternal();
}

void OnMovieAudioClientRegistered(const uint32_t owner_ptr,
                                  const uint32_t callback_ptr,
                                  const uint32_t callback_arg,
                                  const uint32_t driver_ptr) {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    MovieAudioState& client = UpsertMovieAudioClientLocked(owner_ptr, driver_ptr);
    const double since_prev_register_ms =
        MillisecondsBetween(now, client.last_register);
    const double since_prev_submit_ms =
        MillisecondsBetween(now, client.last_submit);
    client.owner_ptr = owner_ptr;
    client.callback_ptr = callback_ptr;
    client.callback_arg = callback_arg;
    client.driver_ptr = driver_ptr;
    client.register_count++;
    client.last_register = now;

    if (REXCVAR_GET(ac6_movie_audio_trace) || IsAc6DeepTraceEnabled()) {
        REXAPU_DEBUG(
            "AC6 movie-audio register owner={:08X} callback={:08X} arg={:08X} driver={:08X} "
            "registers={} submits={} active_clients={} since_prev_register_ms={:.3f} "
            "since_prev_submit_ms={:.3f}",
            owner_ptr,
            callback_ptr,
            callback_arg,
            driver_ptr,
            client.register_count,
            client.submit_count,
            g_movie_audio_clients.size(),
            since_prev_register_ms,
            since_prev_submit_ms);
    }

    MaybeReportDuplicateMovieAudioClientsLocked("register");
    ReportMovieAudioStateTransitionLocked();
}

void OnMovieAudioClientUnregistered(const uint32_t owner_ptr,
                                    const uint32_t driver_ptr) {
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    auto it = std::remove_if(
        g_movie_audio_clients.begin(), g_movie_audio_clients.end(),
        [owner_ptr, driver_ptr](const MovieAudioState& client) {
            const bool matches_owner =
                owner_ptr != 0 && owner_ptr == client.owner_ptr;
            const bool matches_driver =
                driver_ptr != 0 && driver_ptr == client.driver_ptr;
            return matches_owner || matches_driver;
        });
    if (it == g_movie_audio_clients.end()) {
        return;
    }

    if (REXCVAR_GET(ac6_movie_audio_trace) || IsAc6DeepTraceEnabled()) {
        for (auto iter = it; iter != g_movie_audio_clients.end(); ++iter) {
            const double since_submit_ms = MillisecondsBetween(Clock::now(), iter->last_submit);
            REXAPU_DEBUG(
                "AC6 movie-audio unregister owner={:08X} driver={:08X} submits={} registers={} "
                "unregisters={} since_submit_ms={:.3f}",
                iter->owner_ptr,
                iter->driver_ptr,
                iter->submit_count,
                iter->register_count,
                iter->unregister_count + 1,
                since_submit_ms);
        }
    }

    for (auto iter = it; iter != g_movie_audio_clients.end(); ++iter) {
        iter->unregister_count++;
    }
    g_movie_audio_clients.erase(it, g_movie_audio_clients.end());
    ReportMovieAudioStateTransitionLocked();
}

void OnMovieAudioFrameSubmitted(const uint32_t owner_ptr,
                                const uint32_t driver_ptr,
                                const uint32_t samples_ptr) {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    MovieAudioState& client = UpsertMovieAudioClientLocked(owner_ptr, driver_ptr);
    const double since_prev_submit_ms = MillisecondsBetween(now, client.last_submit);
    const double since_prev_register_ms = MillisecondsBetween(now, client.last_register);
    client.owner_ptr = owner_ptr;
    client.driver_ptr = driver_ptr;
    client.last_samples_ptr = samples_ptr;
    client.submit_count++;
    client.last_submit = now;

    if ((REXCVAR_GET(ac6_movie_audio_trace) || IsAc6DeepTraceEnabled()) &&
        (client.submit_count <= 24 ||
         (client.submit_count % 60) == 0 ||
         since_prev_submit_ms >= 40.0)) {
        REXAPU_DEBUG(
            "AC6 movie-audio submit owner={:08X} driver={:08X} samples={:08X} submits={} "
            "active_clients={} since_prev_submit_ms={:.3f} since_prev_register_ms={:.3f}",
            owner_ptr,
            driver_ptr,
            samples_ptr,
            client.submit_count,
            g_movie_audio_clients.size(),
            since_prev_submit_ms,
            since_prev_register_ms);
    }

    MaybeReportDuplicateMovieAudioClientsLocked("submit");
    ReportMovieAudioStateTransitionLocked();
}

}  // namespace ac6
