
// ac6recomp - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.

#include "generated/ac6recomp_config.h"
#include "generated/ac6recomp_init.h"

#include <rex/cvar.h>

#include "ac6recomp_app.h"

REXCVAR_DEFINE_BOOL(audio_deep_trace, false, "Audio",
                    "Enable verbose runtime audio tracing");

REX_DEFINE_APP(ac6recomp, Ac6recompApp::Create)
