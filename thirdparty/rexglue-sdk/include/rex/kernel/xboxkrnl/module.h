/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#pragma once

#include <memory>

#include <rex/ppc/function.h>
#include <rex/system/export_resolver.h>
#include <rex/system/kernel_module.h>
#include <rex/system/kernel_state.h>
#include <rex/thread.h>

// All of the exported functions:
#include <rex/kernel/xboxkrnl/rtl.h>

namespace rex::kernel::xboxkrnl {

class XboxkrnlModule : public system::KernelModule {
 public:
  explicit XboxkrnlModule(system::KernelState* kernel_state);
  virtual ~XboxkrnlModule();

  static void RegisterExportTable(rex::runtime::ExportResolver* export_resolver);

  bool SendPIXCommand(const char* cmd);

  void set_pix_function(uint32_t addr) { pix_function_ = addr; }

 protected:
  uint32_t pix_function_ = 0;

 private:
  std::unique_ptr<rex::thread::HighResolutionTimer> timestamp_timer_;
};

}  // namespace rex::kernel::xboxkrnl
