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
REXCVAR_DEFINE_INT32(ac6_movie_audio_startup_silence_frames, 96, "AC6",
                     "Extra silence frames to inject before the first AC6 movie-audio submit")
    .range(0, 1024);
REXCVAR_DEFINE_INT32(ac6_movie_audio_gate_rearm_ms, 1000, "AC6",
                     "Minimum gap between movie-gate epochs before a new cutscene-start silence budget is armed")
    .range(0, 10000);
REXCVAR_DEFINE_INT32(ac6_movie_audio_gate_min_client_age_ms, 5000, "AC6",
                     "Minimum age of the active movie-audio client before movie-gate silence can arm")
    .range(0, 30000);
REXCVAR_DEFINE_INT32(ac6_movie_audio_visual_arm_consecutive_frames, 2, "AC6",
                     "Number of consecutive cinematic-looking presents required before movie-audio silence arms")
    .range(1, 8);
REXCVAR_DEFINE_INT32(ac6_movie_audio_visual_max_draw_calls, 8, "AC6",
                     "Maximum draw calls for a present to count as cinematic-looking")
    .range(0, 256);
REXCVAR_DEFINE_INT32(ac6_movie_audio_visual_max_texture_sets, 8, "AC6",
                     "Maximum texture binds for a present to count as cinematic-looking")
    .range(0, 256);
REXCVAR_DEFINE_INT32(ac6_movie_audio_visual_max_resolves, 0, "AC6",
                     "Maximum resolves for a present to count as cinematic-looking")
    .range(0, 16);

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
uint32_t g_movie_gate_silence_frames_pending{0};
Clock::time_point g_last_movie_gate_entry{};
uint64_t g_movie_gate_epoch_count{0};
bool g_movie_gate_visual_candidate_pending{false};
uint32_t g_movie_gate_visual_candidate_owner{0};
uint32_t g_movie_gate_visual_candidate_driver{0};
uint32_t g_movie_gate_visual_present_streak{0};

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
    const bool had_no_clients = g_movie_audio_clients.empty();
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

    if (had_no_clients) {
        g_movie_gate_silence_frames_pending = 0;
        g_movie_gate_epoch_count = 0;
        g_last_movie_gate_entry = Clock::time_point{};
        g_movie_gate_visual_candidate_pending = false;
        g_movie_gate_visual_candidate_owner = 0;
        g_movie_gate_visual_candidate_driver = 0;
        g_movie_gate_visual_present_streak = 0;
    }

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
    if (g_movie_audio_clients.empty()) {
        g_movie_gate_silence_frames_pending = 0;
        g_movie_gate_epoch_count = 0;
        g_last_movie_gate_entry = Clock::time_point{};
        g_movie_gate_visual_candidate_pending = false;
        g_movie_gate_visual_candidate_owner = 0;
        g_movie_gate_visual_candidate_driver = 0;
        g_movie_gate_visual_present_streak = 0;
    }
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

void OnMovieGateDriverEntered(const uint32_t caller_lr, const uint32_t /*movie_state_ptr*/) {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    if (g_movie_audio_clients.empty()) {
        g_movie_gate_epoch_count = 0;
        g_movie_gate_silence_frames_pending = 0;
        g_last_movie_gate_entry = now;
        g_movie_gate_visual_candidate_pending = false;
        g_movie_gate_visual_candidate_owner = 0;
        g_movie_gate_visual_candidate_driver = 0;
        g_movie_gate_visual_present_streak = 0;
        return;
    }

    if (caller_lr != 0x823B9810) {
        return;
    }

    const double since_prev_gate_ms = MillisecondsBetween(now, g_last_movie_gate_entry);
    g_last_movie_gate_entry = now;

    if (g_movie_gate_silence_frames_pending != 0) {
        return;
    }

    const double required_gap_ms =
        static_cast<double>(REXCVAR_GET(ac6_movie_audio_gate_rearm_ms));
    if (since_prev_gate_ms >= 0.0 && since_prev_gate_ms < required_gap_ms) {
        return;
    }

    ++g_movie_gate_epoch_count;
    const MovieAudioState* primary = SelectPrimaryMovieAudioClientLocked();
    const double since_register_ms =
        primary ? MillisecondsBetween(now, primary->last_register) : -1.0;
    const double required_client_age_ms =
        static_cast<double>(REXCVAR_GET(ac6_movie_audio_gate_min_client_age_ms));
    if (g_movie_gate_epoch_count == 1 ||
        (since_register_ms >= 0.0 && since_register_ms < required_client_age_ms)) {
        if (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) {
            REXAPU_DEBUG(
                "AC6 movie-audio gate epoch ignored owner={:08X} driver={:08X} epoch={} "
                "since_prev_gate_ms={:.3f} since_register_ms={:.3f} caller_lr={:08X}",
                primary ? primary->owner_ptr : 0u,
                primary ? primary->driver_ptr : 0u,
                g_movie_gate_epoch_count,
                since_prev_gate_ms,
                since_register_ms,
                caller_lr);
        }
        return;
    }

    if (REXCVAR_GET(ac6_movie_audio_startup_silence_frames) == 0) {
        return;
    }

    g_movie_gate_visual_candidate_pending = true;
    g_movie_gate_visual_candidate_owner = primary ? primary->owner_ptr : 0u;
    g_movie_gate_visual_candidate_driver = primary ? primary->driver_ptr : 0u;
    g_movie_gate_visual_present_streak = 0;
    if (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) {
        REXAPU_DEBUG(
            "AC6 movie-audio visual candidate armed owner={:08X} driver={:08X} "
            "epoch={} since_prev_gate_ms={:.3f} since_register_ms={:.3f} caller_lr={:08X}",
            primary ? primary->owner_ptr : 0u,
            primary ? primary->driver_ptr : 0u,
            g_movie_gate_epoch_count,
            since_prev_gate_ms,
            since_register_ms,
            caller_lr);
    }
}

uint32_t ConsumeMovieAudioStartupSilenceFrames(const uint32_t owner_ptr,
                                               const uint32_t driver_ptr) {
    std::lock_guard<std::mutex> lock(g_movie_audio_mutex);
    MovieAudioState* client = FindMovieAudioClientLocked(owner_ptr, driver_ptr);
    if (!client) {
        return 0;
    }

    if (g_movie_gate_silence_frames_pending == 0) {
        return 0;
    }

    const uint32_t silence_frames = g_movie_gate_silence_frames_pending;
    g_movie_gate_silence_frames_pending = 0;
    if (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) {
        REXAPU_DEBUG(
            "AC6 movie-audio gate silence consume owner={:08X} driver={:08X} frames={}",
            owner_ptr,
            driver_ptr,
            silence_frames);
    }
    return silence_frames;
}

void OnPresentFrame(const double frame_time_ms, const double fps,
                    const uint64_t frame_count,
                    const ac6::d3d::DrawStatsSnapshot& draw_stats) {
    const auto now = Clock::now();
    uint32_t active_clients = 0;
    uint32_t primary_owner = 0;
    uint32_t primary_driver = 0;
    uint64_t primary_submits = 0;
    double since_primary_submit_ms = -1.0;
    bool armed_from_visual_present = false;
    uint32_t visual_streak = 0;
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

            if (g_movie_gate_visual_candidate_pending &&
                g_movie_gate_silence_frames_pending == 0 &&
                primary_owner == g_movie_gate_visual_candidate_owner &&
                primary_driver == g_movie_gate_visual_candidate_driver) {
                const bool cinematic_present =
                    draw_stats.draw_calls <=
                        static_cast<uint32_t>(REXCVAR_GET(ac6_movie_audio_visual_max_draw_calls)) &&
                    draw_stats.set_texture_calls <=
                        static_cast<uint32_t>(REXCVAR_GET(ac6_movie_audio_visual_max_texture_sets)) &&
                    draw_stats.resolve_calls <=
                        static_cast<uint32_t>(REXCVAR_GET(ac6_movie_audio_visual_max_resolves));

                if (cinematic_present) {
                    ++g_movie_gate_visual_present_streak;
                    const uint32_t required_frames = static_cast<uint32_t>(
                        REXCVAR_GET(ac6_movie_audio_visual_arm_consecutive_frames));
                    if (g_movie_gate_visual_present_streak >= required_frames) {
                        g_movie_gate_silence_frames_pending = static_cast<uint32_t>(
                            REXCVAR_GET(ac6_movie_audio_startup_silence_frames));
                        g_movie_gate_visual_candidate_pending = false;
                        g_movie_gate_visual_present_streak = 0;
                        armed_from_visual_present = g_movie_gate_silence_frames_pending != 0;
                    }
                } else {
                    g_movie_gate_visual_present_streak = 0;
                }
                visual_streak = g_movie_gate_visual_present_streak;
            } else if (!g_movie_gate_visual_candidate_pending) {
                g_movie_gate_visual_present_streak = 0;
            }
        }
    }
    if (active_clients == 0) {
        return;
    }

    if (armed_from_visual_present &&
        (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled())) {
        REXAPU_DEBUG(
            "AC6 movie-audio visual silence armed owner={:08X} driver={:08X} frames={} "
            "frame={} draws={} textures={} resolves={}",
            primary_owner,
            primary_driver,
            static_cast<uint32_t>(REXCVAR_GET(ac6_movie_audio_startup_silence_frames)),
            frame_count,
            draw_stats.draw_calls,
            draw_stats.set_texture_calls,
            draw_stats.resolve_calls);
    }

    if ((REXCVAR_GET(ac6_movie_audio_trace_verbose) || IsDeepTraceEnabled()) &&
        (g_movie_audio_active_frame_trace_count <= 180 ||
        (g_movie_audio_active_frame_trace_count % 60) == 0 ||
        frame_time_ms >= 40.0 || since_primary_submit_ms >= 40.0 || visual_streak != 0)) {
        REXAPU_DEBUG(
            "AC6 movie-audio frame frame={} active_clients={} primary_owner={:08X} "
            "primary_driver={:08X} primary_submits={} frame_ms={:.3f} fps={:.3f} "
            "since_primary_submit_ms={:.3f} draws={} prim={} idx={} idx_shared={} "
            "textures={} resolves={} visual_streak={}",
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
            draw_stats.draw_calls_indexed_shared,
            draw_stats.set_texture_calls,
            draw_stats.resolve_calls,
            visual_streak);
    }
}

}  // namespace ac6::audio_policy
