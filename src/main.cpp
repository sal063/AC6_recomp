
// ac6recomp - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.

#include <rex/cvar.h>
#include <rex/graphics/pipeline/texture/cache.h>
#include <rex/logging.h>

REXCVAR_DECLARE(bool, ac6_render_capture);
REXCVAR_DECLARE(bool, ac6_unlock_fps);
REXCVAR_DECLARE(bool, ac6_native_graphics_enabled);
REXCVAR_DECLARE(bool, ac6_force_safe_draw_resolution_scale);
REXCVAR_DECLARE(bool, ac6_force_safe_direct_host_resolve);
REXCVAR_DECLARE(std::string, ac6_graphics_mode);
REXCVAR_DECLARE(int32_t, draw_resolution_scale_x);
REXCVAR_DECLARE(int32_t, draw_resolution_scale_y);
REXCVAR_DECLARE(std::string, ac6_neutralize_deswizzle_hashes);
REXCVAR_DECLARE(std::string, ac6_snap_guest_texel_hashes);
REXCVAR_DECLARE(std::string, ac6_densify_x_fetch_hashes);
REXCVAR_DECLARE(std::string, ac6_densify_y_fetch_hashes);
REXCVAR_DECLARE(std::string, ac6_fix_hoisted_fetch_gradients_hashes);
REXCVAR_DECLARE(bool, param_gen_integer_guest_position);
REXCVAR_DECLARE(bool, param_gen_host_subpixel_restore);
REXCVAR_DECLARE(std::string, log_level);
REXCVAR_DECLARE(bool, ac6_d3d_trace);
REXCVAR_DECLARE(bool, ac6_backend_debug_swap);
REXCVAR_DECLARE(bool, ac6_backend_log_signatures);
REXCVAR_DECLARE(bool, ac6_backend_signature_diagnostics);
REXCVAR_DECLARE(bool, ac6_texture_swaps_dump_enabled);

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

// Correctness-fix switches. Each expands to its detailed cvars + AC6-specific
// shader hashes in ApplyAc6FixDefaults() unless the user overrode them. The
// payloads apply at shader translation, so a change needs a restart.
REXCVAR_DEFINE_BOOL(ac6_fix_scaling, true, "AC6/Fixes",
                    "Fix the upscaling mosaic on deferred rendering passes. Only "
                    "active at resolution scale above 1x.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(ac6_fix_deswizzle, true, "AC6/Fixes",
                    "Fix mosaic and streak artifacts from the game's manual texture "
                    "de-swizzle. Effective at all resolution scales.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(ac6_fix_dof, true, "AC6/Fixes",
                    "Fix cutscene depth-of-field striping and ghosting at resolution "
                    "scale above 1x.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(ac6_fix_water_line, true, "AC6/Fixes",
                    "Fix the thin bright lines across open water.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

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
        rex::cvar::SetSessionDefault("resolution_scale", "1",
                                     "ac6_force_safe_draw_resolution_scale");
        rex::cvar::SetSessionDefault("draw_resolution_scale_x", "1",
                                     "ac6_force_safe_draw_resolution_scale");
        rex::cvar::SetSessionDefault("draw_resolution_scale_y", "1",
                                     "ac6_force_safe_draw_resolution_scale");
    }
    // (The >1x param-gen mosaic fix is applied by ApplyAc6FixDefaults via
    // ac6_fix_scaling.)

    if (REXCVAR_GET(ac6_force_safe_direct_host_resolve)) {
        rex::cvar::SetSessionDefault("direct_host_resolve", "false",
                                     "ac6_force_safe_direct_host_resolve");
    }
}

void ApplyAc6DefaultSettings() {
    rex::cvar::SetSessionDefault("vsync", "true");
    rex::cvar::SetSessionDefault("guest_vblank_sync_to_refresh", "true");
    rex::cvar::SetSessionDefault("host_present_from_non_ui_thread", "true");
#if REX_HAS_D3D12
    rex::cvar::SetSessionDefault("d3d12_allow_variable_refresh_rate_and_tearing", "true");
    rex::cvar::SetSessionDefault("d3d12_low_latency_swap_chain", "true");
    rex::cvar::SetSessionDefault("d3d12_max_frame_latency", "1");
#endif
    rex::cvar::SetSessionDefault("vfetch_index_rounding_bias", "true");
    rex::cvar::SetSessionDefault("direct_host_resolve", "false");
    rex::cvar::SetSessionDefault("video_mode_width", "1920");
    rex::cvar::SetSessionDefault("video_mode_height", "1080");
    rex::cvar::SetSessionDefault("resolution", "1080p");
    rex::cvar::SetSessionDefault("window_width", "1920");
    rex::cvar::SetSessionDefault("window_height", "1080");
    rex::cvar::SetSessionDefault("show_build_stamp", "false");
}

void ApplyAc6PerformanceModeOverrides() {
    if (REXCVAR_GET(ac6_performance_mode)) {
        // Session-default RECORDING: gives the settings menu the "(set by
        // ac6_performance_mode)" annotation and the boot-time value. The
        // actual enforcement stays the pre-existing per-frame re-assert
        // (EnforceAc6PerformanceModeOverrides below) - these cvars predate
        // this rework and keep their upstream semantics: performance mode
        // forces them off while enabled.
        rex::cvar::SetSessionDefault("log_level", "error", "ac6_performance_mode");
        rex::cvar::SetSessionDefault("ac6_d3d_trace", "false", "ac6_performance_mode");
        rex::cvar::SetSessionDefault("ac6_render_capture", "false", "ac6_performance_mode");
        rex::cvar::SetSessionDefault("ac6_backend_debug_swap", "false", "ac6_performance_mode");
        rex::cvar::SetSessionDefault("ac6_backend_log_signatures", "false",
                                     "ac6_performance_mode");
        rex::cvar::SetSessionDefault("ac6_backend_signature_diagnostics", "false",
                                     "ac6_performance_mode");
        rex::cvar::SetSessionDefault("ac6_texture_swaps_dump_enabled", "false",
                                     "ac6_performance_mode");
    } else {
        rex::cvar::SetSessionDefault("log_level", "debug");
    }
}

// The pre-existing enforcement, unchanged from before this rework: while
// performance mode is on, the diagnostics are re-forced off every frame from
// the graphics overlay. These writes go through REXCVAR_SET (raw storage), so
// they no longer leak into a settings-menu save - the save writes the user's
// own values (FlagEntry::user_value).
void EnforceAc6PerformanceModeOverrides() {
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

void ApplyAc6FixDefaults() {
    // Post-config, this reads the USER's draw scale - with the old app-create
    // timing this gate always saw the default 1x, so the scaling fix payload
    // never engaged for anyone who set their scale in the toml.
    //
    // Ask the texture cache for the EFFECTIVE scale rather than reading
    // draw_resolution_scale_x/y: the combined resolution_scale cvar - what the
    // settings menu writes - leaves those two at 1, so reading them alone left
    // the fixes off for everyone who scaled that way.
    uint32_t effective_scale_x = 1;
    uint32_t effective_scale_y = 1;
    rex::graphics::TextureCache::GetConfigDrawResolutionScale(effective_scale_x,
                                                              effective_scale_y);
    const bool scaled = effective_scale_x > 1 || effective_scale_y > 1;

    // param_gen floor + host sub-pixel restore only bites at >1x --
    // inert at 1x, so gate it on the draw scale.
    if (REXCVAR_GET(ac6_fix_scaling) && scaled) {
        rex::cvar::SetSessionDefault("param_gen_integer_guest_position", "true",
                                     "ac6_fix_scaling");
        rex::cvar::SetSessionDefault("param_gen_host_subpixel_restore", "true",
                                     "ac6_fix_scaling");
    }
    // de-swizzle identity override is a texture-layout mismatch present
    // at ALL draw scales, so it is NOT gated on scale.
    if (REXCVAR_GET(ac6_fix_deswizzle)) {
        rex::cvar::SetSessionDefault("ac6_neutralize_deswizzle_hashes",
                                     "7d22894002d16018, 17e5e4ac3e713245:4",
                                     "ac6_fix_deswizzle");
    }
    // Cutscene DoF chain (CoC pre-blur pair snapped to the guest grid; the
    // two 13-tap gathers densified along their pass axes). The translator
    // only emits these at draw scale > 1, so no scale gate is needed here.
    if (REXCVAR_GET(ac6_fix_dof)) {
        rex::cvar::SetSessionDefault("ac6_snap_guest_texel_hashes",
                                     "85d733927e3bba9c, d050dfa6f58567f6", "ac6_fix_dof");
        rex::cvar::SetSessionDefault("ac6_densify_x_fetch_hashes", "6328f9c40913c82c",
                                     "ac6_fix_dof");
        rex::cvar::SetSessionDefault("ac6_densify_y_fetch_hashes", "5bd20f9d0d911687",
                                     "ac6_fix_dof");
    }
    // Water pixel shader, interpolator 4 - the wave-normal UVs. Its fetches sit
    // inside translated guest control flow, where DXBC derivatives are undefined,
    // so the gradient collapses at the ocean mesh's seams and the lookup clamps
    // to the finest mip. Hoisting the gradients to the prologue restores what the
    // console computes. Not gated on draw scale: the seams are in the game's own
    // vertex data, so this bites at 1x too.
    if (REXCVAR_GET(ac6_fix_water_line)) {
        rex::cvar::SetSessionDefault("ac6_fix_hoisted_fetch_gradients_hashes",
                                     "35a80cb48a481624:4", "ac6_fix_water_line");
    }
}

}  // namespace

// The whole preset suite. Called from Ac6recompApp::OnConfigLoaded - AFTER
// rex::cvar::LoadConfig parsed the user's toml (so the master switches and
// gates read the user's real values, fixing the "ac6_fix_* dead from the
// toml" bug) and BEFORE anything consumes cvars (logging, window, graphics).
// Everything goes through SetSessionDefault: a user-set value always wins,
// and none of these values leak into the user's config on a settings save.
// The game-specific fix constants stay here in code, not in the user's toml.
void ApplyAc6ConfigPresets() {
    rex::cvar::SetSessionDefault("log_file", "ac6recomp.log");
    ApplyAc6DefaultSettings();
    ApplyAc6HybridStartupSafetyOverrides();
    ApplyAc6FixDefaults();
    ApplyAc6PerformanceModeOverrides();
    // Runtime enforcement of performance mode stays the pre-existing
    // per-frame re-assert from the graphics overlay (see
    // ApplyAc6PerformanceModeOverridesPublic).
}

void ApplyAc6PerformanceModeOverridesPublic() {
    EnforceAc6PerformanceModeOverrides();
}

// One greppable summary of what the presets decided, logged from OnPreSetup
// (the presets themselves run before logging is initialized). Logged at
// error level so it survives performance mode's log_level=error session
// default - the same reason the widescreen activation line logs at error.
// This is the support line: it shows whether a master switch genuinely
// disengaged its payload.
void LogAc6ConfigPresetSummary() {
    uint32_t effective_scale_x = 1;
    uint32_t effective_scale_y = 1;
    rex::graphics::TextureCache::GetConfigDrawResolutionScale(effective_scale_x,
                                                              effective_scale_y);
    REXLOG_ERROR("AC6 config: graphics mode={} capture={} draw scale={}x{}",
                 REXCVAR_GET(ac6_graphics_mode),
                 REXCVAR_GET(ac6_render_capture) ? "true" : "false", effective_scale_x,
                 effective_scale_y);
    REXLOG_ERROR("AC6 fixes: scaling={} deswizzle={} dof={} water_line={} perf_mode={} "
                 "unlock_fps={}",
                 REXCVAR_GET(ac6_fix_scaling), REXCVAR_GET(ac6_fix_deswizzle),
                 REXCVAR_GET(ac6_fix_dof), REXCVAR_GET(ac6_fix_water_line),
                 REXCVAR_GET(ac6_performance_mode), REXCVAR_GET(ac6_unlock_fps));
    REXLOG_ERROR("AC6 fix payloads: neutralize='{}' snap='{}' densify_x='{}' densify_y='{}' "
                 "hoisted_gradients='{}' param_gen={}/{}",
                 REXCVAR_GET(ac6_neutralize_deswizzle_hashes),
                 REXCVAR_GET(ac6_snap_guest_texel_hashes),
                 REXCVAR_GET(ac6_densify_x_fetch_hashes),
                 REXCVAR_GET(ac6_densify_y_fetch_hashes),
                 REXCVAR_GET(ac6_fix_hoisted_fetch_gradients_hashes),
                 REXCVAR_GET(param_gen_integer_guest_position),
                 REXCVAR_GET(param_gen_host_subpixel_restore));
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

    // No cvar presets here: this runs BEFORE rex::cvar::LoadConfig parses
    // the toml, so anything applied here reads defaults, not user values.
    // The preset suite lives in ApplyAc6ConfigPresets, called from
    // Ac6recompApp::OnConfigLoaded.

    return Ac6recompApp::Create(ctx);
}

REX_DEFINE_APP(ac6recomp, Ac6recompAppCreate)

// Hook into static initialization to start log early
struct EarlyBoot {
    EarlyBoot() {
        InitEarlyLog();
    }
} g_early_boot;
