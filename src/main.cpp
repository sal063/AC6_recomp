
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
REXCVAR_DECLARE(bool, d3d12_allow_variable_refresh_rate_and_tearing);
REXCVAR_DECLARE(bool, vfetch_index_rounding_bias);
REXCVAR_DECLARE(int32_t, video_mode_width);
REXCVAR_DECLARE(int32_t, video_mode_height);
REXCVAR_DECLARE(std::string, resolution);
REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);

REXCVAR_DEFINE_BOOL(ac6_performance_mode, true, "AC6/Performance",
                    "Disable all diagnostics, logging, and development overlays for maximum runtime performance");

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
        REXCVAR_SET(host_present_from_non_ui_thread, false);
    }
    if (!rex::cvar::HasNonDefaultValue("d3d12_allow_variable_refresh_rate_and_tearing")) {
        REXCVAR_SET(d3d12_allow_variable_refresh_rate_and_tearing, false);
    }
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
