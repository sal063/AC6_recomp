#pragma once
/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <string>

#include <rex/ppc/function.h>
#include <rex/system/export_resolver.h>
#include <rex/system/kernel_module.h>
#include <rex/system/kernel_state.h>

namespace rex {
namespace kernel {
namespace xam {

bool xeXamIsUIActive();

class XamModule : public system::KernelModule {
 public:
  explicit XamModule(system::KernelState* kernel_state);
  virtual ~XamModule();

  static void RegisterExportTable(rex::runtime::ExportResolver* export_resolver);

  struct LoaderData {
    bool launch_data_present = false;
    std::vector<uint8_t> launch_data;
    uint32_t launch_flags = 0;
    std::string launch_path;  // Full path to next xex
  };

  const LoaderData& loader_data() const { return loader_data_; }
  LoaderData& loader_data() { return loader_data_; }

 private:
  LoaderData loader_data_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace rex
