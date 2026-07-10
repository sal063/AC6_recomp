// Framerate-independent flight dynamics for the FPS unlock.
//
// The game's flight model mixes two timing styles:
//   - Kinematics are delta-scaled and framerate-correct: position integration,
//     the persistent-velocity wind relaxation, and the control-surface ramps
//     all multiply by the adaptive frame delta (see ac6DeltaDivisorHook).
//   - The longitudinal force/speed-command dynamics accumulate FIXED PER-FRAME
//     steps sized for the native 30fps cadence (constants assume a 33.3ms
//     frame, no delta anywhere in the chain).
//
// With ac6_unlock_fps the dynamics tick 2x+ as often, so throttle accel/decel,
// gravity response, and high-G speed bleed all scale with framerate. Cruise
// speed stays correct at any rate because it is the accumulator's equilibrium
// (per-step gain and decay cancel), which is call-rate independent.
//
// The two per-frame-stepped accumulators, located by static analysis of the
// flight-model class (ctor rex_sub_82328F48/82329160, embedded in the aircraft
// at this+10672, base class 0x82303xxx):
//
//   - rex_sub_823046A0 (core force step, generated recomp.22): [this+1320] is
//     the persistent longitudinal force/speed command. Thrust ([this+972] and
//     the rex_sub_82282BC8 term), overspeed drag (fractions of the reference
//     speeds [this+1264]/[this+1268]), min-speed correction ([this+1280]) and
//     the high-G/AoA bleed all add constant-sized steps to it each call.
//     Everything downstream (velocity blend, position += velocity * dt) is
//     delta-scaled.
//   - rex_sub_82329B40 (input shaping, generated recomp.23): [this+1456] is
//     the accel/brake trigger command in [-1, 1], ramped toward +/-1 and
//     decayed toward 0 by per-frame constants. (The stick lags at +360/+364,
//     hold timers +368/+372 and the +304/+312/+1364 terms in the same class
//     are already delta-scaled and are left untouched.)
//
// Fix: wrap both guest functions and rescale each accumulator's NET per-call
// change by frame_ms / 33.333 (clamped to <= 1):
//
//     x_new = x_pre + (x_post - x_pre) * ratio
//
// Scaling the per-step delta of a feedback accumulator by the step-frequency
// ratio reproduces the same continuous-time dynamics the game has at 30fps,
// without touching the delta-scaled math inside. At native cadence the ratio
// is 1 and the wrapper is a bit-exact pass-through. The wrapper applies to
// every aircraft that runs this flight model (player and AI), keeping the
// whole sim consistent.
//
// Both symbols are weak in the generated code and registered by address in
// ac6recomp_init, so these strong overrides capture direct calls and vtable
// dispatch alike.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc.h>

#include "../render_hooks.h"

REXCVAR_DEFINE_BOOL(ac6_fps_physics_fix, true, "AC6",
                    "Make flight-model accel/decel framerate-independent when the FPS unlock "
                    "is active (rescales the game's fixed per-frame force steps by "
                    "frame time / 33.3ms)");

namespace {

// Longitudinal force/speed-command accumulator in the flight-model object
// (stepped per frame by rex_sub_823046A0 without delta scaling).
constexpr uint32_t kForceCommandOffset = 1320;
// Accel/brake trigger command in [-1, 1] (ramped/decayed per frame by
// rex_sub_82329B40 without delta scaling).
constexpr uint32_t kTriggerCommandOffset = 1456;

// The game's native simulation cadence the per-frame constants were tuned for.
constexpr double kNativeFrameMs = 1000.0 / 30.0;

// Everything below runs only on the guest sim thread (the flight-model
// wrappers are the sole callers), so plain statics are safe throughout.

// Per-frame-step scale for this frame: 1.0 at the native 30fps cadence,
// shrinking as the frame rate rises (never above 1.0 - the game's own delta
// clamp already holds sim speed at/below 30fps). Gated on the SAME signal as
// the frame-delta hooks (ac6::TimingHooksActive) so the force step and the
// kinematics delta always revert to vanilla together (cutscene clamp, menus,
// unlock off). Returns exactly 1.0 for a pass-through. Inputs change at most
// once per guest frame, so the result is cached per frame.
double StepRatio() {
  static uint64_t s_cached_frame = ~uint64_t(0);
  static double s_cached_ratio = 1.0;
  const ac6::FrameStats stats = ac6::GetFrameStats();
  if (stats.frame_count == s_cached_frame) {
    return s_cached_ratio;
  }
  s_cached_frame = stats.frame_count;
  s_cached_ratio = 1.0;
  if (!REXCVAR_GET(ac6_fps_physics_fix) || !ac6::TimingHooksActive()) {
    return s_cached_ratio;
  }
  if (stats.frame_time_ms <= 0.0) {
    return s_cached_ratio;  // no frame measured yet
  }
  double ratio = stats.frame_time_ms / kNativeFrameMs;
  if (ratio > 1.0) {
    ratio = 1.0;  // never blend past the native 30fps step
  } else if (ratio < 0.02) {
    ratio = 0.02;  // sanity floor against a bogus frame-time sample
  }
  s_cached_ratio = ratio;
  return s_cached_ratio;
}

// Rescales the net change the wrapped call made to one guest float:
// field = pre + (post - pre) * ratio. Logs the first activation per field so
// each wrapper gets its own confirmation line in the log.
void BlendFieldDelta(uint8_t* base, uint32_t ea, float pre, double ratio, const char* what,
                     bool& logged) {
  const float post = rex::memory::load_and_swap<float>(base + ea);
  if (post == pre) {
    return;
  }
  rex::memory::store_and_swap<float>(base + ea, static_cast<float>(pre + (post - pre) * ratio));
  if (!logged) {
    logged = true;
    REXLOG_INFO("[AC6-PHYS-FIX] active: {} delta {:+.5f} scaled by {:.3f}", what, post - pre,
                ratio);
  }
}

}  // namespace

PPC_EXTERN_FUNC(__imp__rex_sub_823046A0);  // flight-model force step
PPC_EXTERN_FUNC(__imp__rex_sub_82329B40);  // flight-model input shaping

// Flight-model core force step (0x823046A0): rescale the per-frame stepped
// longitudinal force/speed command at [this+1320].
PPC_FUNC_IMPL(rex_sub_823046A0) {
  PPC_FUNC_PROLOGUE();

  const uint32_t self = ctx.r3.u32;
  const double ratio = StepRatio();
  // 1.0 is the exact pass-through value (native cadence, or the fix disabled).
  if (ratio == 1.0 || self == 0) {
    __imp__rex_sub_823046A0(ctx, base);
    return;
  }

  const float pre = rex::memory::load_and_swap<float>(base + self + kForceCommandOffset);
  __imp__rex_sub_823046A0(ctx, base);
  static bool s_logged = false;
  BlendFieldDelta(base, self + kForceCommandOffset, pre, ratio, "force-command(+1320)", s_logged);
}

// Flight-model input shaping (0x82329B40): rescale the per-frame ramped
// accel/brake trigger command at [this+1456].
PPC_FUNC_IMPL(rex_sub_82329B40) {
  PPC_FUNC_PROLOGUE();

  const uint32_t self = ctx.r3.u32;
  const double ratio = StepRatio();
  if (ratio == 1.0 || self == 0) {
    __imp__rex_sub_82329B40(ctx, base);
    return;
  }

  const float pre = rex::memory::load_and_swap<float>(base + self + kTriggerCommandOffset);
  __imp__rex_sub_82329B40(ctx, base);
  static bool s_logged = false;
  BlendFieldDelta(base, self + kTriggerCommandOffset, pre, ratio, "trigger-command(+1456)",
                  s_logged);
}
