/**
 * @file        render_hooks.cpp
 * @brief       Mid-asm hooks for vsync interval, frame timing, and 60fps unlock.
 */
#include "render_hooks.h"

#include <atomic>
#include <chrono>

#include <rex/cvar.h>
#include <rex/ppc/types.h>

REXCVAR_DEFINE_BOOL(ac6_unlock_fps, false, "AC6", "Unlock frame rate to 60fps");

using Clock = std::chrono::steady_clock;

namespace {

// Frame timing state (written on render thread, read from UI thread).
std::atomic<double> g_frame_time_ms{0.0};
std::atomic<double> g_fps{0.0};
std::atomic<uint64_t> g_frame_count{0};
Clock::time_point g_frame_start{};

}  // namespace

// ---------------------------------------------------------------------------
// Hook: VdSetDisplayMode swap interval skip (0x821F0758)
// ---------------------------------------------------------------------------
bool ac6VsyncIntervalHook() {
    return REXCVAR_GET(ac6_unlock_fps);
}

// ---------------------------------------------------------------------------
// Hook: Delta time divisor override (0x821EFD38)
// ---------------------------------------------------------------------------
void ac6DeltaDivisorHook(PPCRegister& r29) {
    if (REXCVAR_GET(ac6_unlock_fps)) {
        r29.u64 = 30;
    }
}

// ---------------------------------------------------------------------------
// Hook: Frame timing stats (0x821F0664, before VdSwap call)
//
// Records timestamp at each present call to measure render-thread frame time.
// ---------------------------------------------------------------------------
void ac6PresentTimingHook() {
    auto now = Clock::now();
    if (g_frame_start.time_since_epoch().count() != 0) {
        double ms =
            std::chrono::duration<double, std::milli>(now - g_frame_start)
                .count();
        g_frame_time_ms.store(ms, std::memory_order_relaxed);
        g_fps.store(ms > 0.0 ? 1000.0 / ms : 0.0, std::memory_order_relaxed);
        g_frame_count.fetch_add(1, std::memory_order_relaxed);
    }
    g_frame_start = now;
}

namespace ac6 {

FrameStats GetFrameStats() {
    return FrameStats{
        g_frame_time_ms.load(std::memory_order_relaxed),
        g_fps.load(std::memory_order_relaxed),
        g_frame_count.load(std::memory_order_relaxed),
    };
}

}  // namespace ac6
