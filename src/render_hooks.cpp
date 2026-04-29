#include "render_hooks.h"
#include "d3d_hooks.h"
#include "ac6_native_graphics.h"

#include <atomic>
#include <chrono>

#include <rex/cvar.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_BOOL(ac6_unlock_fps, false, "AC6", "Unlock frame rate to 60fps");
REXCVAR_DEFINE_BOOL(ac6_timing_hooks_enabled, true, "AC6",
                    "Enable AC6 timing hooks that alter the game's presentation cadence");

using Clock = std::chrono::steady_clock;

namespace {

std::atomic<double> g_frame_time_ms{0.0};
std::atomic<double> g_fps{0.0};
std::atomic<uint64_t> g_frame_count{0};
Clock::time_point g_frame_start{};

bool AreTimingHooksActive() {
    return REXCVAR_GET(ac6_timing_hooks_enabled) && REXCVAR_GET(ac6_unlock_fps);
}

}  // namespace

bool ac6FlipIntervalHook() {
    return AreTimingHooksActive();
}

bool ac6PresentIntervalHook(PPCRegister& r10) {
    if (!AreTimingHooksActive()) {
        return false;
    }
    r10.u64 = 1;
    return true;
}

void ac6DeltaDivisorHook(PPCRegister& r29) {
    if (!AreTimingHooksActive()) {
        return;
    }
    r29.u64 = 30;
}

void ac6PresentTimingHook(PPCRegister& /*r31*/) {
    // ac6::d3d::OnFrameBoundary(); // MOVED TO GPU THREAD

    const auto now = Clock::now();
    if (g_frame_start.time_since_epoch().count() != 0) {
        const double frame_time_ms =
            std::chrono::duration<double, std::milli>(now - g_frame_start).count();
        g_frame_time_ms.store(frame_time_ms, std::memory_order_relaxed);
        g_fps.store(frame_time_ms > 0.0001 ? (1000.0 / frame_time_ms) : 0.0,
                    std::memory_order_relaxed);
        g_frame_count.fetch_add(1, std::memory_order_relaxed);
    }
    g_frame_start = now;
}

namespace ac6 {

FrameStats GetFrameStats() {
    return FrameStats{g_frame_time_ms.load(std::memory_order_relaxed),
                      g_fps.load(std::memory_order_relaxed),
                      g_frame_count.load(std::memory_order_relaxed)};
}

}  // namespace ac6
