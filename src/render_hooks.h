#pragma once

#include <cstdint>

#include <rex/cvar.h>
#include <rex/ppc/types.h>

REXCVAR_DECLARE(bool, ac6_unlock_fps);
REXCVAR_DECLARE(bool, ac6_timing_hooks_enabled);

namespace ac6 {

struct FrameStats {
    double frame_time_ms{0.0};
    double fps{0.0};
    uint64_t frame_count{0};
};

FrameStats GetFrameStats();

}  // namespace ac6

bool ac6FlipIntervalHook();
bool ac6PresentIntervalHook(PPCRegister& r10);
void ac6DeltaDivisorHook(PPCRegister& r29);
void ac6PresentTimingHook(PPCRegister& r31);
