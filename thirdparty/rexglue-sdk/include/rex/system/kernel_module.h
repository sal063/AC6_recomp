#pragma once
/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <unordered_map>

#include <rex/system/export_resolver.h>
#include <rex/system/xmodule.h>

namespace rex::system {

class KernelState;

class KernelModule : public XModule {
 public:
  KernelModule(KernelState* kernel_state, const std::string_view path);
  ~KernelModule() override;

  const std::string& path() const override { return path_; }
  const std::string& name() const override { return name_; }

 uint32_t GetProcAddressByOrdinal(uint16_t ordinal) override;
  uint32_t GetProcAddressByName(const std::string_view name) override;

 protected:
  rex::runtime::ExportResolver* export_resolver_;

  std::string name_;
  std::string path_;

  rex::thread::global_critical_region global_critical_region_;

  // Cache of ordinal -> thunk guest address (for XexGetProcedureAddress)
  std::unordered_map<uint16_t, uint32_t> thunk_cache_;
};

}  // namespace rex::system
