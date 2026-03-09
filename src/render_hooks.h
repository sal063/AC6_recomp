/**
 * @file        render_hooks.h
 * @brief       Mid-asm hooks for vsync interval, frame timing, and 60fps unlock.
 */
#pragma once

#include <cstdint>

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, ac6_unlock_fps);

namespace ac6 {

struct FrameStats {
    double frame_time_ms;
    double fps;
    uint64_t frame_count;
};

FrameStats GetFrameStats();

}  // namespace ac6
