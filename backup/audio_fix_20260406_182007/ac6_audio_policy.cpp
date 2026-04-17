#include "ac6_audio_policy.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(ac6_audio_deep_trace, false, "AC6",
                    "Enable high-volume AC6 audio diagnostics across AC6 hooks, kernel audio, "
                    "worker cadence, and host queue telemetry");
REXCVAR_DEFINE_BOOL(ac6_unlock_fps_video_safe, true, "AC6",
                    "Keep stock timing while AC6 movie-audio clients are active");
REXCVAR_DEFINE_BOOL(ac6_movie_audio_trace, false, "AC6",
                    "Trace AC6 movie-audio registration, submission, and timing transitions");
REXCVAR_DEFINE_BOOL(ac6_movie_audio_trace_verbose, false, "AC6",
                    "Trace AC6 movie-audio cadence, draw stats, and per-frame timing while "
                    "movie audio is active");
REXCVAR_DEFINE_BOOL(ac6_movie_audio_force_sync_dispatch, true, "AC6",
                    "Force AC6 movie-audio callbacks to run their update path synchronously "
                    "instead of handing work to cutscene worker threads");

REXCVAR_DECLARE(bool, ac6_timing_hooks_enabled);

using Clock = std::chrono::steady_clock;

namespace {

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

void ReportMovieAudioStateTransitionLocked() {
    const bool active = ComputeMovieAudioActiveLocked();
    if (active == g_movie_audio_last_reported_active) {
        return;
    }

    g_movie_audio_last_reported_active = active;
    if (!active) {
        g_movie_audio_active_frame_trace_count = 0;
    }
    if (!(REXCVAR_GET(ac6_movie_audio_trace) ||
          ac6::audio_policy::IsDeepTraceEnabled())) {
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
        !(REXCVAR_GET(ac6_movie_audio_trace) ||
          ac6::audio_policy::IsDeepTraceEnabled())) {
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

}  // namespace

namespace ac6::audio_policy {

bool IsDeepTraceEnabled() {
    return REXCVAR_GET(ac6_audio_deep_trace);
}

bool IsMovieAudioActive() {
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    ReportMovieAudioStateTransitionLocked();
    return ComputeMovieAudioActiveLocked();
}

bool ShouldKeepStockTimingForMovieAudio() {
    return REXCVAR_GET(ac6_timing_hooks_enabled) &&
           REXCVAR_GET(ac6_unlock_fps_video_safe) && IsMovieAudioActive();
}

MovieAudioSnapshot GetMovieAudioSnapshot() {
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    ReportMovieAudioStateTransitionLocked();

    uint64_t register_count = 0;
    uint64_t submit_count = 0;
    for (const auto& client : g_movie_audio_clients) {
        register_count += client.register_count;
        submit_count += client.submit_count;
    }

    const MovieAudioState* primary = SelectPrimaryMovieAudioClientLocked();
    return MovieAudioSnapshot{
        ComputeMovieAudioActiveLocked(),
        static_cast<uint32_t>(g_movie_audio_clients.size()),
        register_count,
        submit_count,
        primary ? primary->owner_ptr : 0u,
        primary ? primary->driver_ptr : 0u,
    };
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

    if (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) {
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

    if (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) {
        for (auto iter = it; iter != g_movie_audio_clients.end(); ++iter) {
            const double since_submit_ms =
                MillisecondsBetween(Clock::now(), iter->last_submit);
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
    const double since_prev_submit_ms =
        MillisecondsBetween(now, client.last_submit);
    const double since_prev_register_ms =
        MillisecondsBetween(now, client.last_register);
    client.owner_ptr = owner_ptr;
    client.driver_ptr = driver_ptr;
    client.last_samples_ptr = samples_ptr;
    client.submit_count++;
    client.last_submit = now;

    if ((REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) &&
        (client.submit_count <= 24 || (client.submit_count % 60) == 0 ||
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

void OnPresentFrame(const double frame_time_ms, const double fps,
                    const uint64_t frame_count,
                    const ac6::d3d::DrawStatsSnapshot& draw_stats) {
    if (!(REXCVAR_GET(ac6_movie_audio_trace_verbose) || IsDeepTraceEnabled())) {
        return;
    }

    const auto now = Clock::now();
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
                since_primary_submit_ms =
                    MillisecondsBetween(now, primary->last_submit);
            }
        }
    }
    if (active_clients == 0) {
        return;
    }

    if (g_movie_audio_active_frame_trace_count <= 180 ||
        (g_movie_audio_active_frame_trace_count % 60) == 0 ||
        frame_time_ms >= 40.0 || since_primary_submit_ms >= 40.0) {
        REXAPU_DEBUG(
            "AC6 movie-audio frame frame={} active_clients={} primary_owner={:08X} "
            "primary_driver={:08X} primary_submits={} frame_ms={:.3f} fps={:.3f} "
            "since_primary_submit_ms={:.3f} draws={} prim={} idx={} idx_shared={}",
            frame_count,
            active_clients,
            primary_owner,
            primary_driver,
            primary_submits,
            frame_time_ms,
            fps,
            since_primary_submit_ms,
            draw_stats.draw_calls,
            draw_stats.draw_calls_primitive,
            draw_stats.draw_calls_indexed,
            draw_stats.draw_calls_indexed_shared);
    }
}

}  // namespace ac6::audio_policy
