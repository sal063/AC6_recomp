#include "render_hooks.h"
#include "d3d_hooks.h"
#include "ac6_native_graphics.h"

#include <atomic>
#include <chrono>
#include <cmath>

#include <rex/cvar.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_BOOL(ac6_unlock_fps, false, "AC6", "Unlock frame rate to 60fps");
REXCVAR_DEFINE_BOOL(ac6_timing_hooks_enabled, true, "AC6",
                    "Enable AC6 timing hooks that alter the game's presentation cadence");
REXCVAR_DEFINE_BOOL(ac6_cutscene_clamp, true, "AC6",
                    "Suspend the 60fps unlock during in-engine cutscenes so they "
                    "play at native ~30fps instead of double speed");
REXCVAR_DEFINE_BOOL(ac6_dynamic_vblank, true, "AC6",
                    "With the FPS unlock active, pace frame-locked content (menus, "
                    "cutscenes, pause) at the native 60Hz guest vblank while gameplay "
                    "free-runs at the configured rate. Gameplay is detected via the "
                    "world-compositor draw heartbeat; cutscenes via the cinematic "
                    "hooks.");
REXCVAR_DEFINE_BOOL(ac6_delta_precision, true, "AC6",
                    "With the FPS unlock active, carry the fractional remainder of the "
                    "game's integer frame delta across frames so its floor(x)+1 "
                    "truncation guard stops inflating game speed at high framerates "
                    "(+2% at 60fps, +10% at 300fps)");

using Clock = std::chrono::steady_clock;

namespace {

std::atomic<double> g_frame_time_ms{0.0};
std::atomic<double> g_fps{0.0};
std::atomic<uint64_t> g_frame_count{0};
Clock::time_point g_frame_start{};

// Wall-clock (steady) ms of the last in-engine cutscene tick. The cutscene
// hook (ac6CinematicTickHook) runs on the game thread; the timing hooks read
// this on the present/GPU path, hence the atomic. INT64_MIN = never ticked.
std::atomic<int64_t> g_last_cinematic_tick_ms{INT64_MIN};

// The demo-manager Exec runs every game frame while a cutscene plays (~16-33ms
// apart). A few-frame decay keeps the clamp asserted across the cutscene and
// auto-releases shortly after it ends.
constexpr int64_t kCinematicDecayMs = 100;

// Wall-clock ms of the last draw using the world/effects compositor pixel
// shader (stamped by the GPU command processor via NotifyWorldCompositorDraw).
// The compositor runs every frame the 3D world renders and never in the 2D
// front-end, so its freshness distinguishes free-runnable gameplay from
// frame-locked menus/hangar. (The delta-time hook was tried first as this
// signal, but it lives in the frame layer and ticks in menus too.)
// INT64_MIN = never drawn.
std::atomic<int64_t> g_last_world_draw_ms{INT64_MIN};
constexpr int64_t kWorldDrawDecayMs = 300;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch())
        .count();
}

bool IsWorldRenderActive() {
  const int64_t last = g_last_world_draw_ms.load(std::memory_order_relaxed);
  if (last == INT64_MIN) {
    return false;
  }
  return (NowMs() - last) <= kWorldDrawDecayMs;
}

bool AreTimingHooksActive() {
    // The world-render gate only applies under dynamic vblank pacing: it exists
    // to keep menus/hangar at native cadence, and it fails closed (a game
    // build/render path whose compositor shader hashes differently would never
    // stamp it). ac6_dynamic_vblank=false restores the plain always-on unlock.
    return REXCVAR_GET(ac6_timing_hooks_enabled) && REXCVAR_GET(ac6_unlock_fps) &&
           !ac6::IsCinematicActive() &&
           (!REXCVAR_GET(ac6_dynamic_vblank) || IsWorldRenderActive());
}

}  // namespace

namespace ac6 {

// True while the FPS-unlock timing hooks are remapping the game's cadence
// (unlock cvars on and no cutscene clamp). The physics force-step rescale keys
// off this so it stays in lockstep with the frame-delta hooks: whenever the
// delta reverts to vanilla, the force step must too.
bool TimingHooksActive() {
  return AreTimingHooksActive();
}

bool WorldRenderActiveRecently() {
  return IsWorldRenderActive();
}

bool IsCinematicActive() {
    if (!REXCVAR_GET(ac6_cutscene_clamp)) {
        return false;
    }
    const int64_t last = g_last_cinematic_tick_ms.load(std::memory_order_relaxed);
    if (last == INT64_MIN) {
        return false;
    }
    return (NowMs() - last) <= kCinematicDecayMs;
}

}  // namespace ac6

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

// Fires right after the game turns the measured frame time into its integer
// delta: r8 = min(floor(elapsed*100 / (freq/divisor)) + 1, 100). Under the
// unlock the floor plus the +1 guard bias the delta high by (1 - frac) every
// frame - a systematic speed-up at unlocked framerates (+2% @60fps, +10% @300).
// Carry the fractional remainder across frames so the SUM of integer deltas
// tracks real time exactly. Only active while the divisor remap is (r29 == 30);
// otherwise the vanilla value passes through and the remainder resets so no
// stale correction leaks across mode changes.
void ac6DeltaPrecisionHook(PPCRegister& r8, PPCRegister& r10, PPCRegister& r29, PPCRegister& r30) {
  static double s_remainder = 0.0;

  // Cheapest gates first: the register compares are free, the cvar/clock work
  // only runs on the frames that can actually take the rewrite path.
  if (r29.u32 != 30 || r10.u32 == 0 || !AreTimingHooksActive()) {
    s_remainder = 0.0;
    return;
  }
  if (!REXCVAR_GET(ac6_delta_precision)) {
    s_remainder = 0.0;
    return;
  }
  const double max_delta = 100.0;  // the game's stock 30fps delta clamp

  // Exact delta this frame in the game's own scale (elapsed * 3000), using the
  // same ticks-per-frame divisor (r10 = freq / 30) the game divided by.
  double exact = double(r30.u32) * 100.0 / double(r10.u32);
  if (exact > max_delta) {
    exact = max_delta;
  }

  // Carry the remainder (no +1) so the summed integer deltas track real time.
  const double base = s_remainder + exact;
  double delta = std::floor(base);
  if (delta < 1.0) {
    delta = 1.0;  // the game guarantees progress every frame; the overshoot is
                  // repaid through a negative remainder next frame
  } else if (delta > max_delta) {
    delta = max_delta;
  }
  s_remainder = base - delta;
  r8.u64 = uint64_t(delta);
}

void ac6PresentTimingHook(PPCRegister& /*r31*/) {
    // ac6::d3d::OnFrameBoundary(); // MOVED TO GPU THREAD

  // Dynamic vblank pacing: free-run only while the 3D world is rendering; pace
  // frame-locked content (menus, hangar, cutscenes) at the native 60Hz. Only
  // engages when the FPS unlock is on, so default configurations keep the plain
  // cvar-driven vblank behavior.
  const bool unlock = REXCVAR_GET(ac6_timing_hooks_enabled) && REXCVAR_GET(ac6_unlock_fps);
  const bool dynamic_pacing = REXCVAR_GET(ac6_dynamic_vblank) && unlock;
  // Single source of truth for "the unlock is remapping the cadence right now" -
  // the same signal that gates the interval/delta hooks and the physics rescale.
  const bool free_running = dynamic_pacing && AreTimingHooksActive();

  // Guest-vblank Hz override for this frame. 0 = no override (free-run at the
  // vsync/tearing rate); dynamic pacing forces frame-locked content to 60Hz.
  double override_hz = 0.0;
  if (dynamic_pacing && !free_running) {
    override_hz = 60.0;
  }
  rex::graphics::GraphicsSystem::SetGuestVblankHzOverride(override_hz);

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

  // Log the first handful of pacing transitions.
  static double last_log_key = -0.5;
  static uint32_t transition_logs = 0;
  if (unlock && override_hz != last_log_key && transition_logs < 32) {
    ++transition_logs;
    if (override_hz == 0.0) {
      REXLOG_INFO("[AC6-VBLANK] pacing -> free-run (uncapped)");
    } else {
      REXLOG_INFO("[AC6-VBLANK] pacing -> {:.0f}Hz guest vblank", override_hz);
    }
  }
  last_log_key = override_hz;
}

void ac6CinematicTickHook(PPCRegister& /*r3*/) {
    // A demo-manager Exec (DD: sub_82184460 / EM: sub_821856F8) ran this frame ->
    // an in-engine cutscene is playing. Stamp the time so AreTimingHooksActive()
    // suspends the 60fps unlock until the cutscene ends (the stamp goes stale a
    // few frames after the last tick), letting the frame-locked cinematic
    // Sequencer play at native ~30fps instead of double speed.
    g_last_cinematic_tick_ms.store(NowMs(), std::memory_order_relaxed);
}

namespace ac6 {

FrameStats GetFrameStats() {
    return FrameStats{g_frame_time_ms.load(std::memory_order_relaxed),
                      g_fps.load(std::memory_order_relaxed),
                      g_frame_count.load(std::memory_order_relaxed)};
}

void NotifyWorldCompositorDraw() {
  g_last_world_draw_ms.store(NowMs(), std::memory_order_relaxed);
}

}  // namespace ac6
