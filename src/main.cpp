
// ac6recomp - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DECLARE(bool, ac6_render_capture);
REXCVAR_DECLARE(bool, ac6_timing_hooks_enabled);
REXCVAR_DECLARE(bool, ac6_unlock_fps);
REXCVAR_DECLARE(bool, ac6_native_graphics_enabled);
REXCVAR_DECLARE(bool, ac6_experimental_replay_present);
REXCVAR_DECLARE(std::string, ac6_graphics_mode);
REXCVAR_DECLARE(std::string, log_file);
REXCVAR_DECLARE(std::string, log_level);

#include "generated/ac6recomp_config.h"
#include "generated/ac6recomp_init.h"

#include <fstream>
#include <iostream>

#include "ac6recomp_app.h"

// Early boot log to catch crashes before the SDK logger is ready
std::ofstream g_boot_log;

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
    REXCVAR_SET(log_level, "info");
    REXCVAR_SET(ac6_unlock_fps, false);
    
    REXLOG_INFO("Ac6recompAppCreate: graphics mode={} replay_present={} capture={}",
                REXCVAR_GET(ac6_graphics_mode),
                REXCVAR_GET(ac6_experimental_replay_present) ? "true" : "false",
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
