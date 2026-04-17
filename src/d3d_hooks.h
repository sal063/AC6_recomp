#pragma once

#include <rex/cvar.h>

#include "d3d_state.h"

REXCVAR_DECLARE(bool, ac6_render_capture);

namespace ac6::d3d {

void OnFrameBoundary();

DrawStatsSnapshot GetDrawStats();
FrameCaptureSnapshot GetFrameCapture();
FrameCaptureSummary GetFrameCaptureSummary();
ShadowState GetShadowState();

}  // namespace ac6::d3d

