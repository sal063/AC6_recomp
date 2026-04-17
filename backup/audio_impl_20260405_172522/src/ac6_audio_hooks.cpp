#include "generated/ac6recomp_config.h"
#include "render_hooks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(ac6_movie_audio_zero_frame_guard, true, "AC6",
                    "When AC6 movie audio submits a malformed frame, briefly reuse the last "
                    "good frame to mask cutouts.");
REXCVAR_DEFINE_INT32(ac6_movie_audio_zero_frame_guard_frames, 480, "AC6",
                     "Maximum consecutive malformed movie-audio frames to replace with the last "
                     "good frame.");
REXCVAR_DEFINE_DOUBLE(ac6_movie_audio_zero_frame_guard_rms_threshold, 1.0e-4, "AC6",
                      "Frames at or below this RMS are considered malformed for the movie-audio "
                      "guard.");
REXCVAR_DEFINE_DOUBLE(ac6_movie_audio_zero_frame_guard_zeroish_pct, 95.0, "AC6",
                      "Frames at or above this near-zero sample percentage are considered "
                      "malformed for the movie-audio guard.");
REXCVAR_DEFINE_DOUBLE(ac6_movie_audio_zero_frame_guard_cache_rms_threshold, 1.0e-3, "AC6",
                      "Frames below this RMS are not allowed to replace the cached movie-audio "
                      "guard frame.");
REXCVAR_DEFINE_BOOL(ac6_movie_audio_force_sync_dispatch, true, "AC6",
                    "Force AC6 movie-audio callbacks to run their update path synchronously "
                    "instead of handing work to the cutscene worker threads.");
REXCVAR_DECLARE(bool, ac6_movie_audio_trace);
REXCVAR_DECLARE(bool, ac6_movie_audio_trace_verbose);
REXCVAR_DECLARE(bool, ac6_audio_deep_trace);

PPC_EXTERN_IMPORT(__savegprlr_28);
PPC_EXTERN_IMPORT(__savegprlr_29);
PPC_EXTERN_IMPORT(__restgprlr_28);
PPC_EXTERN_IMPORT(__restgprlr_29);
PPC_EXTERN_IMPORT(__imp__XAudioRegisterRenderDriverClient);
PPC_EXTERN_IMPORT(__imp__XAudioSubmitRenderDriverFrame);
PPC_EXTERN_IMPORT(__imp__XAudioUnregisterRenderDriverClient);
PPC_EXTERN_FUNC(rex_sub_823A8648);
PPC_EXTERN_FUNC(__imp__rex_sub_823A2C30);
PPC_EXTERN_FUNC(__imp__rex_sub_823AD0D8);
PPC_EXTERN_FUNC(__imp__rex_sub_823AD1C0);
PPC_EXTERN_FUNC(__imp__rex_sub_823AD9C0);
PPC_EXTERN_FUNC(__imp__rex_sub_823B0DB8);
PPC_EXTERN_FUNC(__imp__rex_sub_823B0DF0);
PPC_EXTERN_IMPORT(__imp__KeSetEvent);
PPC_EXTERN_IMPORT(__imp__KeWaitForMultipleObjects);
PPC_EXTERN_IMPORT(__imp__RtlEnterCriticalSection);
PPC_EXTERN_IMPORT(__imp__RtlLeaveCriticalSection);

namespace {

constexpr size_t kMovieAudioFrameWordCount = 6 * 256;
struct MovieAudioFrameGuardState {
    uint32_t owner_ptr{0};
    uint32_t driver_ptr{0};
    std::array<uint32_t, kMovieAudioFrameWordCount> last_good_frame_words{};
    bool has_last_good_frame{false};
    bool warned_limit_exhausted{false};
    uint32_t consecutive_zero_frames{0};
    uint64_t substituted_zero_frames{0};
};

std::mutex g_movie_audio_frame_guard_mutex;
std::vector<MovieAudioFrameGuardState> g_movie_audio_frame_guards;

struct MovieAudioFrameStats {
    float min_sample{std::numeric_limits<float>::infinity()};
    float max_sample{-std::numeric_limits<float>::infinity()};
    double sum_squares{0.0};
    uint32_t sample_count{0};
    uint32_t zeroish_samples{0};
    uint32_t nonzero_word_count{0};
    bool has_nonfinite{false};
};

struct MovieAudioFrameDerivedStats {
    double rms{0.0};
    double zeroish_pct{0.0};
};

bool IsDeepTraceEnabled() {
    return REXCVAR_GET(ac6_audio_deep_trace);
}

float ByteSwapFloatWord(const uint32_t value) {
    const uint32_t swapped = __builtin_bswap32(value);
    float result = 0.0f;
    std::memcpy(&result, &swapped, sizeof(result));
    return result;
}

MovieAudioFrameStats AnalyzeMovieAudioFrame(const uint32_t* frame_words) {
    MovieAudioFrameStats stats;
    for (size_t i = 0; i < kMovieAudioFrameWordCount; ++i) {
        if (frame_words[i] != 0) {
            ++stats.nonzero_word_count;
        }
        const float sample = ByteSwapFloatWord(frame_words[i]);
        if (!std::isfinite(sample)) {
            stats.has_nonfinite = true;
            continue;
        }
        stats.min_sample = std::min(stats.min_sample, sample);
        stats.max_sample = std::max(stats.max_sample, sample);
        stats.sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
        ++stats.sample_count;
        if (std::fabs(sample) <= 1.0e-6f) {
            ++stats.zeroish_samples;
        }
    }
    if (stats.sample_count == 0) {
        stats.min_sample = 0.0f;
        stats.max_sample = 0.0f;
    }
    return stats;
}

MovieAudioFrameDerivedStats SummarizeMovieAudioFrame(const MovieAudioFrameStats& stats) {
    MovieAudioFrameDerivedStats derived;
    if (stats.sample_count != 0) {
        derived.rms = std::sqrt(stats.sum_squares / static_cast<double>(stats.sample_count));
        derived.zeroish_pct =
            (static_cast<double>(stats.zeroish_samples) * 100.0) /
            static_cast<double>(stats.sample_count);
    }
    return derived;
}

bool IsMalformedMovieAudioFrame(const MovieAudioFrameStats& stats,
                                const MovieAudioFrameDerivedStats& derived) {
    if (stats.nonzero_word_count == 0 || stats.has_nonfinite) {
        return true;
    }

    const double rms_threshold = REXCVAR_GET(ac6_movie_audio_zero_frame_guard_rms_threshold);
    const double zeroish_pct_threshold =
        REXCVAR_GET(ac6_movie_audio_zero_frame_guard_zeroish_pct);
    return derived.rms <= rms_threshold || derived.zeroish_pct >= zeroish_pct_threshold;
}

MovieAudioFrameGuardState* FindMovieAudioFrameGuardLocked(const uint32_t owner_ptr,
                                                         const uint32_t driver_ptr) {
    if (driver_ptr != 0) {
        for (auto& state : g_movie_audio_frame_guards) {
            if (state.driver_ptr == driver_ptr) {
                return &state;
            }
        }
    }
    if (owner_ptr != 0) {
        for (auto& state : g_movie_audio_frame_guards) {
            if (state.owner_ptr == owner_ptr) {
                return &state;
            }
        }
    }
    return nullptr;
}

MovieAudioFrameGuardState& UpsertMovieAudioFrameGuardLocked(const uint32_t owner_ptr,
                                                            const uint32_t driver_ptr) {
    if (auto* existing = FindMovieAudioFrameGuardLocked(owner_ptr, driver_ptr)) {
        if (owner_ptr != 0) {
            existing->owner_ptr = owner_ptr;
        }
        if (driver_ptr != 0) {
            existing->driver_ptr = driver_ptr;
        }
        return *existing;
    }
    g_movie_audio_frame_guards.emplace_back();
    auto& state = g_movie_audio_frame_guards.back();
    state.owner_ptr = owner_ptr;
    state.driver_ptr = driver_ptr;
    return state;
}

void RemoveMovieAudioFrameGuard(const uint32_t owner_ptr, const uint32_t driver_ptr) {
    std::lock_guard<std::mutex> lock(g_movie_audio_frame_guard_mutex);
    g_movie_audio_frame_guards.erase(
        std::remove_if(g_movie_audio_frame_guards.begin(), g_movie_audio_frame_guards.end(),
                       [owner_ptr, driver_ptr](const MovieAudioFrameGuardState& state) {
                           const bool matches_owner =
                               owner_ptr != 0 && state.owner_ptr == owner_ptr;
                           const bool matches_driver =
                               driver_ptr != 0 && state.driver_ptr == driver_ptr;
                           return matches_owner || matches_driver;
                       }),
        g_movie_audio_frame_guards.end());
}

bool ApplyMovieAudioZeroFrameGuard(const uint32_t owner_ptr,
                                   const uint32_t driver_ptr,
                                   const uint32_t samples_ptr,
                                   uint8_t* base) {
    if (!REXCVAR_GET(ac6_movie_audio_zero_frame_guard) || samples_ptr == 0) {
        return false;
    }

    auto* frame_words = reinterpret_cast<uint32_t*>(PPC_RAW_ADDR(samples_ptr));
    const auto frame_stats = AnalyzeMovieAudioFrame(frame_words);
    const auto frame_derived = SummarizeMovieAudioFrame(frame_stats);
    const bool malformed_frame = IsMalformedMovieAudioFrame(frame_stats, frame_derived);
    const double cache_rms_threshold =
        REXCVAR_GET(ac6_movie_audio_zero_frame_guard_cache_rms_threshold);

    const int32_t configured_limit = REXCVAR_GET(ac6_movie_audio_zero_frame_guard_frames);
    const uint32_t substitution_limit =
        configured_limit <= 0 ? 0u : static_cast<uint32_t>(configured_limit);

    std::lock_guard<std::mutex> lock(g_movie_audio_frame_guard_mutex);
    auto& state = UpsertMovieAudioFrameGuardLocked(owner_ptr, driver_ptr);
    if (!malformed_frame) {
        if (frame_derived.rms >= cache_rms_threshold) {
            std::memcpy(state.last_good_frame_words.data(), frame_words,
                        sizeof(uint32_t) * kMovieAudioFrameWordCount);
            state.has_last_good_frame = true;
        }
        state.warned_limit_exhausted = false;
        state.consecutive_zero_frames = 0;
        return false;
    }

    ++state.consecutive_zero_frames;
    if (!state.has_last_good_frame || state.consecutive_zero_frames > substitution_limit) {
        if (state.has_last_good_frame && substitution_limit != 0 &&
            state.consecutive_zero_frames == substitution_limit + 1 &&
            !state.warned_limit_exhausted &&
            (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled())) {
            REXAPU_WARN(
                "AC6 malformed-frame guard hit substitution limit owner={:08X} driver={:08X} "
                "samples={:08X} limit={} substitutions={} rms={:.6f} zeroish_pct={:.2f}",
                owner_ptr, driver_ptr, samples_ptr, substitution_limit,
                state.substituted_zero_frames, frame_derived.rms, frame_derived.zeroish_pct);
            state.warned_limit_exhausted = true;
        }
        return false;
    }

    std::memcpy(frame_words, state.last_good_frame_words.data(),
                sizeof(uint32_t) * kMovieAudioFrameWordCount);
    ++state.substituted_zero_frames;
    if (REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) {
        REXAPU_WARN(
            "AC6 malformed-frame guard substituted owner={:08X} driver={:08X} samples={:08X} "
            "bad_run={} substitutions={} limit={} nonzero_words={} rms={:.6f} zeroish_pct={:.2f} "
            "nonfinite={}",
            owner_ptr, driver_ptr, samples_ptr, state.consecutive_zero_frames,
            state.substituted_zero_frames, substitution_limit, frame_stats.nonzero_word_count,
            frame_derived.rms, frame_derived.zeroish_pct, frame_stats.has_nonfinite);
    }
    return true;
}

}  // namespace

PPC_FUNC_IMPL(rex_sub_823A6620) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    ctx.r12.u64 = ctx.lr;
    __savegprlr_29(ctx, base);
    ea = -128 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r31.u64 = ctx.r3.u64;
    ctx.r29.u64 = ctx.r4.u64;
    ctx.r3.s64 = 0;
    ctx.r30.s64 = ctx.r31.s64 + 24;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 24);
    ctx.cr6.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
    if (ctx.cr6.eq) {
        goto loc_823A6660;
    }

    const uint32_t old_driver_ptr = ctx.r11.u32;
    if (REXCVAR_GET(ac6_movie_audio_trace_verbose) ||
        REXCVAR_GET(ac6_audio_deep_trace)) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6620 unregister-existing owner={:08X} old_driver={:08X}",
            ctx.r31.u32,
            old_driver_ptr);
    }
    ctx.r3.u64 = ctx.r11.u64;
    ctx.lr = 0x823A6650;
    __imp__XAudioUnregisterRenderDriverClient(ctx, base);
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823A6680;
    }

    ac6::OnMovieAudioClientUnregistered(ctx.r31.u32, old_driver_ptr);
    RemoveMovieAudioFrameGuard(ctx.r31.u32, old_driver_ptr);
    ctx.r11.s64 = 0;
    PPC_STORE_U32(ctx.r30.u32 + 0, ctx.r11.u32);

loc_823A6660:
    ctx.cr6.compare<uint32_t>(ctx.r29.u32, 0, ctx.xer);
    if (ctx.cr6.eq) {
        goto loc_823A6680;
    }

    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 12);
    ctx.r4.u64 = ctx.r30.u64;
    ctx.r3.s64 = ctx.r1.s64 + 80;
    PPC_STORE_U32(ctx.r1.u32 + 80, ctx.r29.u32);
    PPC_STORE_U32(ctx.r1.u32 + 84, ctx.r11.u32);
    if (REXCVAR_GET(ac6_movie_audio_trace_verbose) ||
        REXCVAR_GET(ac6_audio_deep_trace)) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6620 register owner={:08X} callback={:08X} callback_arg={:08X}",
            ctx.r31.u32,
            ctx.r29.u32,
            ctx.r11.u32);
    }
    ctx.lr = 0x823A6680;
    __imp__XAudioRegisterRenderDriverClient(ctx, base);
    if (ctx.r3.s32 >= 0) {
        if (REXCVAR_GET(ac6_movie_audio_trace) ||
            REXCVAR_GET(ac6_audio_deep_trace)) {
            REXAPU_DEBUG(
                "AC6 hook rex_sub_823A6620 register-result owner={:08X} callback={:08X} "
                "driver={:08X} status={:08X}",
                ctx.r31.u32,
                ctx.r29.u32,
                PPC_LOAD_U32(ctx.r30.u32 + 0),
                static_cast<uint32_t>(ctx.r3.u32));
        }
        ac6::OnMovieAudioClientRegistered(ctx.r31.u32, ctx.r29.u32,
                                          PPC_LOAD_U32(ctx.r31.u32 + 12),
                                          PPC_LOAD_U32(ctx.r30.u32 + 0));
    }

loc_823A6680:
    ctx.r1.s64 = ctx.r1.s64 + 128;
    __restgprlr_29(ctx, base);
    return;
}

PPC_FUNC_IMPL(rex_sub_823A6778) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    ctx.r12.u64 = ctx.lr;
    PPC_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
    PPC_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
    PPC_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
    ea = -112 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r11.s64 = -2113601536;
    ctx.r10.s64 = -2113601536;
    ctx.r31.u64 = ctx.r3.u64;
    ctx.r11.s64 = ctx.r11.s64 + -13968;
    ctx.r10.s64 = ctx.r10.s64 + -13996;
    ctx.r9.s64 = -2106654720;
    ctx.r30.s64 = 0;
    ctx.r9.s64 = ctx.r9.s64 + -18624;
    PPC_STORE_U32(ctx.r31.u32 + 0, ctx.r11.u32);
    ctx.r11.s64 = 6144;
    PPC_STORE_U32(ctx.r31.u32 + 4, ctx.r10.u32);
    ctx.r10.s64 = -2103050240;
    ctx.r6.u64 = ctx.r11.u64;
    PPC_STORE_U32(ctx.r10.u32 + -3580, ctx.r11.u32);

loc_823A67C4:
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.r7.u64 = PPC_CHECK_GLOBAL_LOCK();
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr = (ctx.r13.u32 & 0x8020) | (ctx.msr & ~0x8020);
    PPC_ENTER_GLOBAL_LOCK();
    ea = ctx.r9.u32;
    ctx.reserved.u32 = *(uint32_t*)PPC_RAW_ADDR(ea);
    ctx.r8.u64 = __builtin_bswap32(ctx.reserved.u32);
    ctx.cr6.compare<int32_t>(ctx.r8.s32, ctx.r30.s32, ctx.xer);
    if (!ctx.cr6.eq) {
        goto loc_823A67E8;
    }

    ea = ctx.r9.u32;
    ctx.cr0.lt = 0;
    ctx.cr0.gt = 0;
    ctx.cr0.eq = __sync_bool_compare_and_swap(
        reinterpret_cast<uint32_t*>(PPC_RAW_ADDR(ea)), ctx.reserved.s32,
        __builtin_bswap32(ctx.r6.s32));
    ctx.cr0.so = ctx.xer.so;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr = (ctx.r7.u32 & 0x8020) | (ctx.msr & ~0x8020);
    PPC_LEAVE_GLOBAL_LOCK();
    if (!ctx.cr0.eq) {
        goto loc_823A67C4;
    }
    goto loc_823A67F0;

loc_823A67E8:
    ea = ctx.r9.u32;
    ctx.cr0.lt = 0;
    ctx.cr0.gt = 0;
    ctx.cr0.eq = __sync_bool_compare_and_swap(
        reinterpret_cast<uint32_t*>(PPC_RAW_ADDR(ea)), ctx.reserved.s32,
        __builtin_bswap32(ctx.r8.s32));
    ctx.cr0.so = ctx.xer.so;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr = (ctx.r7.u32 & 0x8020) | (ctx.msr & ~0x8020);
    PPC_LEAVE_GLOBAL_LOCK();

loc_823A67F0:
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 24);
    const uint32_t old_driver_ptr = ctx.r3.u32;
    ctx.cr6.compare<uint32_t>(ctx.r3.u32, 0, ctx.xer);
    if (ctx.cr6.eq) {
        goto loc_823A680C;
    }

    ctx.lr = 0x823A6800;
    __imp__XAudioUnregisterRenderDriverClient(ctx, base);
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823A680C;
    }

    if (REXCVAR_GET(ac6_movie_audio_trace) ||
        REXCVAR_GET(ac6_audio_deep_trace)) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6778 destructor-unregister owner={:08X} old_driver={:08X} "
            "status={:08X}",
            ctx.r31.u32,
            old_driver_ptr,
            static_cast<uint32_t>(ctx.r3.u32));
    }
    ac6::OnMovieAudioClientUnregistered(ctx.r31.u32, old_driver_ptr);
    RemoveMovieAudioFrameGuard(ctx.r31.u32, old_driver_ptr);
    PPC_STORE_U32(ctx.r31.u32 + 24, ctx.r30.u32);

loc_823A680C:
    ctx.r11.s64 = -2113601536;
    ctx.r11.s64 = ctx.r11.s64 + -14044;
    PPC_STORE_U32(ctx.r31.u32 + 4, ctx.r11.u32);
    ctx.r1.s64 = ctx.r1.s64 + 112;
    ctx.r12.u64 = PPC_LOAD_U32(ctx.r1.u32 + -8);
    ctx.lr = ctx.r12.u64;
    ctx.r30.u64 = PPC_LOAD_U64(ctx.r1.u32 + -24);
    ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 + -16);
    return;
}

PPC_FUNC_IMPL(rex_sub_823A6878) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    ctx.r12.u64 = ctx.lr;
    PPC_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
    PPC_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
    ea = -112 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r31.u64 = ctx.r3.u64;
    const uint32_t packet_provider_ptr = ctx.r4.u32;
    const uint32_t packet_provider_base_ptr =
        packet_provider_ptr == 0 ? 0 : packet_provider_ptr - 8;
    ctx.r3.u64 = ctx.r4.u64;
    ctx.r4.s64 = ctx.r1.s64 + 80;
    ctx.lr = 0x823A6898;
    rex_sub_823A8648(ctx, base);
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823A68C4;
    }

    const uint32_t pre_samples_ptr = PPC_LOAD_U32(ctx.r1.u32 + 88);
    MovieAudioFrameStats pre_frame_stats{};
    MovieAudioFrameDerivedStats pre_frame_derived{};
    bool has_pre_frame_stats = false;
    if (pre_samples_ptr != 0) {
        const auto* pre_frame_words =
            reinterpret_cast<const uint32_t*>(PPC_RAW_ADDR(pre_samples_ptr));
        pre_frame_stats = AnalyzeMovieAudioFrame(pre_frame_words);
        pre_frame_derived = SummarizeMovieAudioFrame(pre_frame_stats);
        has_pre_frame_stats = true;
    }

    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);
    ctx.r4.s64 = ctx.r1.s64 + 80;
    ctx.r3.u64 = ctx.r31.u64;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 32);
    const uint32_t producer_callback_ptr = ctx.r11.u32;
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x823A68B8;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    ctx.r4.u64 = PPC_LOAD_U32(ctx.r1.u32 + 88);
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 24);
    const uint32_t driver_ptr = ctx.r3.u32;
    const uint32_t samples_ptr = ctx.r4.u32;
    const uint32_t packet_word_0 = PPC_LOAD_U32(ctx.r1.u32 + 80);
    const uint32_t packet_word_1 = PPC_LOAD_U32(ctx.r1.u32 + 84);
    const uint32_t packet_word_2 = PPC_LOAD_U32(ctx.r1.u32 + 88);
    if (samples_ptr != 0) {
        const auto* frame_words = reinterpret_cast<const uint32_t*>(PPC_RAW_ADDR(samples_ptr));
        const auto frame_stats = AnalyzeMovieAudioFrame(frame_words);
        const auto frame_derived = SummarizeMovieAudioFrame(frame_stats);
        if (frame_stats.nonzero_word_count == 0 || frame_derived.rms <= 1.0e-4 ||
            frame_stats.has_nonfinite) {
            REXAPU_WARN(
                "AC6 producer frame anomaly owner={:08X} driver={:08X} producer={:08X} "
                "provider={:08X}/{:08X} packet=[{:08X} {:08X} {:08X}] pre_samples={:08X} "
                "post_samples={:08X} pre_nonzero_words={} pre_rms={:.6f} pre_zeroish_pct={:.2f} "
                "post_nonzero_words={} min={:.6f} max={:.6f} post_rms={:.6f} "
                "post_zeroish_pct={:.2f} nonfinite={}",
                ctx.r31.u32, driver_ptr, producer_callback_ptr, packet_provider_ptr,
                packet_provider_base_ptr, packet_word_0, packet_word_1, packet_word_2,
                pre_samples_ptr, samples_ptr,
                has_pre_frame_stats ? pre_frame_stats.nonzero_word_count : 0u,
                has_pre_frame_stats ? pre_frame_derived.rms : 0.0,
                has_pre_frame_stats ? pre_frame_derived.zeroish_pct : 0.0,
                frame_stats.nonzero_word_count, frame_stats.min_sample, frame_stats.max_sample,
                frame_derived.rms, frame_derived.zeroish_pct, frame_stats.has_nonfinite);
        }
    } else {
        REXAPU_WARN(
            "AC6 producer returned null samples owner={:08X} driver={:08X} producer={:08X} "
            "provider={:08X}/{:08X} packet=[{:08X} {:08X} {:08X}] pre_samples={:08X}",
            ctx.r31.u32, driver_ptr, producer_callback_ptr, packet_provider_ptr,
            packet_provider_base_ptr, packet_word_0, packet_word_1, packet_word_2,
            pre_samples_ptr);
    }
    if (REXCVAR_GET(ac6_movie_audio_trace_verbose) ||
        REXCVAR_GET(ac6_audio_deep_trace)) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6878 submit owner={:08X} driver={:08X} samples={:08X}",
            ctx.r31.u32,
            driver_ptr,
            samples_ptr);
    }
    ApplyMovieAudioZeroFrameGuard(ctx.r31.u32, driver_ptr, samples_ptr, base);
    ctx.lr = 0x823A68C4;
    __imp__XAudioSubmitRenderDriverFrame(ctx, base);
    ac6::OnMovieAudioFrameSubmitted(ctx.r31.u32, driver_ptr, samples_ptr);

loc_823A68C4:
    ctx.r1.s64 = ctx.r1.s64 + 112;
    ctx.r12.u64 = PPC_LOAD_U32(ctx.r1.u32 + -8);
    ctx.lr = ctx.r12.u64;
    ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 + -16);
    return;
}

PPC_FUNC_IMPL(rex_sub_823AD9C0) {
    PPC_FUNC_PROLOGUE();
    if (!REXCVAR_GET(ac6_movie_audio_force_sync_dispatch)) {
        __imp__rex_sub_823AD9C0(ctx, base);
        return;
    }

    uint32_t ea{};
    ctx.r12.u64 = ctx.lr;
    __savegprlr_28(ctx, base);
    ea = -192 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r30.s64 = 0;
    ctx.r31.u64 = ctx.r3.u64;
    ctx.r28.u64 = ctx.r30.u64;
    ctx.lr = 0x823AD9DC;
    __imp__rex_sub_823A2C30(ctx, base);
    ctx.r11.s64 = -2107047936;
    ctx.r29.s64 = ctx.r11.s64 + -6288;
    ctx.r3.s64 = ctx.r29.s64 + 4;
    ctx.lr = 0x823AD9EC;
    __imp__RtlEnterCriticalSection(ctx, base);
    const uint32_t worker_count = PPC_LOAD_U32(ctx.r31.u32 + 304);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r13.u32 + 256);
    PPC_STORE_U32(ctx.r31.u32 + 300, ctx.r11.u32);

    if ((REXCVAR_GET(ac6_movie_audio_trace) || IsDeepTraceEnabled()) && worker_count != 0) {
        REXAPU_DEBUG(
            "AC6 cutscene audio forcing synchronous dispatch singleton={:08X} worker_count={} "
            "current_thread={:08X}",
            ctx.r31.u32, worker_count, PPC_LOAD_U32(ctx.r31.u32 + 300));
    }

    ctx.r3.u64 = ctx.r31.u64;
    ctx.lr = 0x823ADA7C;
    __imp__rex_sub_823AD0D8(ctx, base);
    ctx.r4.s64 = 1;
    ctx.r3.u64 = ctx.r31.u64;
    ctx.lr = 0x823ADA88;
    __imp__rex_sub_823AD1C0(ctx, base);

    ctx.r11.u64 = ctx.r28.u32 & 0xFF;
    ctx.cr6.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
    if (!ctx.cr6.eq) {
        goto loc_823ADABC_sync;
    }

    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 64);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 68);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x823ADAA8;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823ADABC_sync;
    }

    ctx.r4.s64 = 1;
    ctx.r3.s64 = 0;
    ctx.lr = 0x823ADABC;
    __imp__rex_sub_823B0DB8(ctx, base);

loc_823ADABC_sync:
    PPC_STORE_U32(ctx.r31.u32 + 300, ctx.r30.u32);
    ctx.lr = 0x823ADAC4;
    __imp__rex_sub_823B0DF0(ctx, base);
    ctx.r3.s64 = ctx.r29.s64 + 4;
    ctx.lr = 0x823ADACC;
    __imp__RtlLeaveCriticalSection(ctx, base);
    ctx.r3.s64 = 0;
    ctx.r1.s64 = ctx.r1.s64 + 192;
    __restgprlr_28(ctx, base);
    return;
}
