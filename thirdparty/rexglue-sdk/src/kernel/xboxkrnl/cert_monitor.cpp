/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <vector>

#include <rex/chrono/clock.h>
#include <rex/dbg.h>
#include <rex/kernel/xboxkrnl/cert_monitor.h>
#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xthread.h>

namespace rex::kernel::xboxkrnl {

void KeCertMonitorCallback(PPCContext* ppc_context, rex::system::KernelState* kernel_state) {
  auto id = ppc_context->r[3];
  auto arg = ppc_context->r[4];
  REXKRNL_DEBUG("KeCertMonitorCallback({}, {:08X})", id, arg);
  // TODO: Implement cert monitor callback if needed
}

}  // namespace rex::kernel::xboxkrnl
