#pragma once

#include <cstdint>

#include <rex/cvar.h>

#include "d3d_state.h"

REXCVAR_DECLARE(bool, ac6_audio_deep_trace);
REXCVAR_DECLARE(bool, ac6_unlock_fps_video_safe);
REXCVAR_DECLARE(bool, ac6_movie_audio_trace);
REXCVAR_DECLARE(bool, ac6_movie_audio_trace_verbose);

namespace ac6::audio_policy {

struct MovieAudioSnapshot {
    bool movie_audio_active{false};
    uint32_t active_client_count{0};
    uint64_t register_count{0};
    uint64_t submit_count{0};
    uint32_t primary_owner{0};
    uint32_t primary_driver{0};
};

bool IsDeepTraceEnabled();
bool IsMovieAudioActive();
bool ShouldKeepStockTimingForMovieAudio();

MovieAudioSnapshot GetMovieAudioSnapshot();

void OnMovieAudioClientRegistered(uint32_t owner_ptr, uint32_t callback_ptr,
                                  uint32_t callback_arg, uint32_t driver_ptr);
void OnMovieAudioClientUnregistered(uint32_t owner_ptr, uint32_t driver_ptr);
void OnMovieGateDriverEntered(uint32_t caller_lr, uint32_t movie_state_ptr);
uint32_t ConsumeMovieAudioStartupSilenceFrames(uint32_t owner_ptr, uint32_t driver_ptr);
void OnMovieAudioFrameSubmitted(uint32_t owner_ptr, uint32_t driver_ptr,
                                uint32_t samples_ptr);

void OnPresentFrame(double frame_time_ms, double fps, uint64_t frame_count,
                    const ac6::d3d::DrawStatsSnapshot& draw_stats);

}  // namespace ac6::audio_policy
