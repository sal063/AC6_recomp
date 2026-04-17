#pragma once

#include <cstdint>

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, ac6_unlock_fps);
REXCVAR_DECLARE(bool, ac6_audio_deep_trace);

namespace ac6 {

struct FrameStats {
    double frame_time_ms;
    double fps;
    uint64_t frame_count;
    bool movie_audio_active{false};
    uint32_t movie_audio_client_count{0};
    uint64_t movie_audio_register_count{0};
    uint64_t movie_audio_submit_count{0};
    uint32_t movie_audio_owner{0};
    uint32_t movie_audio_driver{0};
};

FrameStats GetFrameStats();
bool IsMovieAudioActive();
void OnMovieAudioClientRegistered(uint32_t owner_ptr, uint32_t callback_ptr,
                                  uint32_t callback_arg, uint32_t driver_ptr);
void OnMovieAudioClientUnregistered(uint32_t owner_ptr, uint32_t driver_ptr);
void OnMovieAudioFrameSubmitted(uint32_t owner_ptr, uint32_t driver_ptr,
                                uint32_t samples_ptr);

}  // namespace ac6
