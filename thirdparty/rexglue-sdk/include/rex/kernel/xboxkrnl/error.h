/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#pragma once

#include <rex/system/xtypes.h>

namespace rex::kernel::xboxkrnl {

uint32_t xeRtlNtStatusToDosError(uint32_t source_status);

}  // namespace rex::kernel::xboxkrnl
