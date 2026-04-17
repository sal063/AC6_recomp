/**
 * @file        d3d_hooks.h
 * @brief       Public interface for the D3D state shadowing layer.
 *
 * Provides per-frame draw statistics, a frame capture snapshot for renderer
 * analysis, and a read-only view of the current D3D device shadow state.
 * Call OnFrameBoundary() once per frame (typically from the present/swap hook)
 * to snapshot stats and reset counters.
 */
#pragma once

#include <rex/cvar.h>

#include "d3d_state.h"

namespace ac6::d3d {

void OnFrameBoundary();

DrawStatsSnapshot GetDrawStats();

FrameCaptureSnapshot GetFrameCapture();
FrameCaptureSummary GetFrameCaptureSummary();

ShadowState GetShadowState();

}  // namespace ac6::d3d

REXCVAR_DECLARE(bool, ac6_render_capture);
