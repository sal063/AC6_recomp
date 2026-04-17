/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#pragma once

#include <memory>

#include <rex/system/kernel_module.h>
#include <rex/system/kernel_state.h>
#include <rex/thread.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {

struct X_KECERTMONITORDATA {
  rex::be<uint32_t> callback_fn;
};

void KeCertMonitorCallback(::PPCContext* ppc_context, rex::system::KernelState* kernel_state);

}  // namespace rex::kernel::xboxkrnl
