
// ac6recomp - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DECLARE(bool, ac6_render_capture);
REXCVAR_DECLARE(bool, ac6_timing_hooks_enabled);
REXCVAR_DECLARE(bool, ac6_unlock_fps);
REXCVAR_DECLARE(bool, ac6_native_graphics_enabled);
REXCVAR_DECLARE(bool, ac6_force_safe_draw_resolution_scale);
REXCVAR_DECLARE(bool, ac6_force_safe_direct_host_resolve);
REXCVAR_DECLARE(std::string, ac6_graphics_mode);
REXCVAR_DECLARE(bool, direct_host_resolve);
REXCVAR_DECLARE(int32_t, resolution_scale);
REXCVAR_DECLARE(int32_t, draw_resolution_scale_x);
REXCVAR_DECLARE(int32_t, draw_resolution_scale_y);
REXCVAR_DECLARE(bool, param_gen_integer_guest_position);
REXCVAR_DECLARE(bool, param_gen_host_subpixel_restore);
REXCVAR_DECLARE(std::string, ac6_neutralize_deswizzle_hashes);
REXCVAR_DECLARE(std::string, log_file);
REXCVAR_DECLARE(std::string, log_level);
REXCVAR_DECLARE(bool, ac6_d3d_trace);
REXCVAR_DECLARE(bool, ac6_backend_debug_swap);
REXCVAR_DECLARE(bool, ac6_backend_log_signatures);
REXCVAR_DECLARE(bool, ac6_backend_signature_diagnostics);
REXCVAR_DECLARE(bool, ac6_texture_swaps_dump_enabled);
REXCVAR_DECLARE(bool, vsync);
REXCVAR_DECLARE(bool, guest_vblank_sync_to_refresh);
REXCVAR_DECLARE(bool, host_present_from_non_ui_thread);
#if REX_HAS_D3D12
REXCVAR_DECLARE(bool, d3d12_allow_variable_refresh_rate_and_tearing);
REXCVAR_DECLARE(bool, d3d12_low_latency_swap_chain);
REXCVAR_DECLARE(int32_t, d3d12_max_frame_latency);
#endif
REXCVAR_DECLARE(bool, vfetch_index_rounding_bias);
REXCVAR_DECLARE(int32_t, video_mode_width);
REXCVAR_DECLARE(int32_t, video_mode_height);
REXCVAR_DECLARE(std::string, resolution);
REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);

#if REX_HAS_VULKAN
#define AC6_DEFAULT_GRAPHICS_BACKEND "vulkan"
#elif REX_HAS_D3D12
#define AC6_DEFAULT_GRAPHICS_BACKEND "d3d12"
#else
#define AC6_DEFAULT_GRAPHICS_BACKEND "auto"
#endif

REXCVAR_DEFINE_STRING(ac6_graphics_backend, AC6_DEFAULT_GRAPHICS_BACKEND,
                      "AC6/NativeGraphics",
                      "Host graphics backend: vulkan, d3d12, or auto")
    .allowed({"vulkan", "d3d12", "auto"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(ac6_performance_mode, true, "AC6/Performance",
                    "Disable all diagnostics, logging, and development overlays for maximum runtime performance");

// Correctness-fix switches. Each expands
// to its detailed cvars + AC6-specific shader hashes in ApplyAc6FixDefaults()
// unless the user overrode them.
REXCVAR_DEFINE_BOOL(ac6_fix_scaling, true, "AC6",
                    "Fix the upscaling mosaic on AC6's deferred EDRAM restore passes by "
                    "flooring the PsParamGen pixel position (+ host sub-pixel restore). "
                    "Auto-applies only at draw resolution scale > 1; inert at 1x. On by default.");
REXCVAR_DEFINE_BOOL(ac6_fix_deswizzle, true, "AC6",
                    "Fix the de-swizzle mosaic/streaks: identity-override AC6's manual EDRAM "
                    "sub-tile de-swizzle, which is always a wrong texel permutation once the "
                    "emulator detiles to linear. On by default; effective at all draw scales "
                    "(the swizzle is present at 1x too).");

#include "generated/ac6recomp_config.h"
#include "generated/ac6recomp_init.h"

#include <fstream>
#include <iostream>

#include "ac6recomp_app.h"

// Early boot log to catch crashes before the SDK logger is ready
std::ofstream g_boot_log;

namespace {

bool ShouldApplyAc6HybridStartupSafetyOverrides() {
    return REXCVAR_GET(ac6_native_graphics_enabled) &&
           REXCVAR_GET(ac6_graphics_mode) == "hybrid_backend_fixes";
}

void ApplyAc6HybridStartupSafetyOverrides() {
    if (!ShouldApplyAc6HybridStartupSafetyOverrides()) {
        return;
    }

    if (REXCVAR_GET(ac6_force_safe_draw_resolution_scale)) {
        REXCVAR_SET(resolution_scale, 1);
        REXCVAR_SET(draw_resolution_scale_x, 1);
        REXCVAR_SET(draw_resolution_scale_y, 1);
    }
    // (The >1x param-gen mosaic fix is applied by ApplyAc6FixDefaults via
    // ac6_fix_scaling.)

    if (REXCVAR_GET(ac6_force_safe_direct_host_resolve)) {
        REXCVAR_SET(direct_host_resolve, false);
    }
}

void ApplyAc6DefaultSettings() {
    if (!rex::cvar::HasNonDefaultValue("vsync")) {
        REXCVAR_SET(vsync, true);
    }
    if (!rex::cvar::HasNonDefaultValue("guest_vblank_sync_to_refresh")) {
        REXCVAR_SET(guest_vblank_sync_to_refresh, true);
    }
    if (!rex::cvar::HasNonDefaultValue("host_present_from_non_ui_thread")) {
        REXCVAR_SET(host_present_from_non_ui_thread, true);
    }
#if REX_HAS_D3D12
    if (!rex::cvar::HasNonDefaultValue("d3d12_allow_variable_refresh_rate_and_tearing")) {
        REXCVAR_SET(d3d12_allow_variable_refresh_rate_and_tearing, true);
    }
    if (!rex::cvar::HasNonDefaultValue("d3d12_low_latency_swap_chain")) {
        REXCVAR_SET(d3d12_low_latency_swap_chain, true);
    }
    if (!rex::cvar::HasNonDefaultValue("d3d12_max_frame_latency")) {
        REXCVAR_SET(d3d12_max_frame_latency, 1);
    }
#endif
    if (!rex::cvar::HasNonDefaultValue("vfetch_index_rounding_bias")) {
        REXCVAR_SET(vfetch_index_rounding_bias, true);
    }
    if (!rex::cvar::HasNonDefaultValue("direct_host_resolve")) {
        REXCVAR_SET(direct_host_resolve, false);
    }
    if (!rex::cvar::HasNonDefaultValue("video_mode_width")) {
        REXCVAR_SET(video_mode_width, 1920);
    }
    if (!rex::cvar::HasNonDefaultValue("video_mode_height")) {
        REXCVAR_SET(video_mode_height, 1080);
    }
    if (!rex::cvar::HasNonDefaultValue("resolution")) {
        REXCVAR_SET(resolution, "1080p");
    }
    if (!rex::cvar::HasNonDefaultValue("window_width")) {
        REXCVAR_SET(window_width, 1920);
    }
    if (!rex::cvar::HasNonDefaultValue("window_height")) {
        REXCVAR_SET(window_height, 1080);
    }
}

void ApplyAc6PerformanceModeOverrides() {
    if (!REXCVAR_GET(ac6_performance_mode)) {
        return;
    }
    REXCVAR_SET(log_level, "error");
    REXCVAR_SET(ac6_d3d_trace, false);
    REXCVAR_SET(ac6_render_capture, false);
    REXCVAR_SET(ac6_backend_debug_swap, false);
    REXCVAR_SET(ac6_backend_log_signatures, false);
    REXCVAR_SET(ac6_backend_signature_diagnostics, false);
    REXCVAR_SET(ac6_texture_swaps_dump_enabled, false);
}

// Each fix switch -> its detailed cvars + AC6-specific shader hashes. The
// game-specific constants live here in code, not in the user's toml; each
// value is filled in only if the user did NOT set it explicitly, so any toml
// override still wins (the dev/debug path).
#define AC6_SET_IF_UNSET(cv, value) \
    do { if (!rex::cvar::HasNonDefaultValue(#cv)) REXCVAR_SET(cv, value); } while (0)

void ApplyAc6FixDefaults() {
    const bool scaled = REXCVAR_GET(draw_resolution_scale_x) > 1 ||
                        REXCVAR_GET(draw_resolution_scale_y) > 1;

    // param_gen floor + host sub-pixel restore only bites at >1x --
    // inert at 1x, so gate it on the draw scale.
    if (REXCVAR_GET(ac6_fix_scaling) && scaled) {
        AC6_SET_IF_UNSET(param_gen_integer_guest_position, true);
        AC6_SET_IF_UNSET(param_gen_host_subpixel_restore, true);
    }
    // de-swizzle identity override is a texture-layout mismatch present
    // at ALL draw scales, so it is NOT gated on scale.
    if (REXCVAR_GET(ac6_fix_deswizzle)) {
        AC6_SET_IF_UNSET(ac6_neutralize_deswizzle_hashes,
                         "7d22894002d16018, 17e5e4ac3e713245:4");
    }
}
#undef AC6_SET_IF_UNSET

}  // namespace

void ApplyAc6PerformanceModeOverridesPublic() {
    ApplyAc6PerformanceModeOverrides();
}

void InitEarlyLog() {
    g_boot_log.open("boot.log", std::ios::out | std::ios::trunc);
    if (g_boot_log.is_open()) {
        g_boot_log << "AC6 Recompiled Early Boot Log" << std::endl;
        g_boot_log << "-----------------------------" << std::endl;
        g_boot_log.flush();
    }
    std::cout << "Early boot logging initialized." << std::endl;
}

std::unique_ptr<rex::ui::WindowedApp> Ac6recompAppCreate(rex::ui::WindowedAppContext& ctx) {
    if (g_boot_log.is_open()) {
        g_boot_log << "Ac6recompApp::Create called" << std::endl;
        g_boot_log.flush();
    }
    
    // Force SDK logging to a file as well
    REXCVAR_SET(log_file, "ac6recomp.log");
    if (!rex::cvar::HasNonDefaultValue("log_level")) {
        REXCVAR_SET(log_level, "debug");
    }
    REXCVAR_SET(ac6_unlock_fps, false);
    ApplyAc6DefaultSettings();
    ApplyAc6HybridStartupSafetyOverrides();
    ApplyAc6FixDefaults();
    ApplyAc6PerformanceModeOverrides();
    
    REXLOG_INFO("Ac6recompAppCreate: graphics mode={} capture={}",
                REXCVAR_GET(ac6_graphics_mode),
                REXCVAR_GET(ac6_render_capture) ? "true" : "false");
    
    return Ac6recompApp::Create(ctx);
}

REX_DEFINE_APP(ac6recomp, Ac6recompAppCreate)

// Hook into static initialization to start log early
struct EarlyBoot {
    EarlyBoot() {
        InitEarlyLog();
    }
} g_early_boot;
