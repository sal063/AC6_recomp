#pragma once
/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <memory>
#include <unordered_map>

#include <rex/system/xobject.h>
#include <rex/system/xtypes.h>
#include <rex/thread.h>
#include <rex/thread/mutex.h>

namespace rex::system {

class XSymbolicLink : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::SymbolicLink;

  explicit XSymbolicLink(KernelState* kernel_state);
  ~XSymbolicLink() override;

  void Initialize(const std::string_view path, const std::string_view target);

  const std::string& path() const { return path_; }
  const std::string& target() const { return target_; }

 private:
  XSymbolicLink();

  std::string path_;
  std::string target_;
};

}  // namespace rex::system
