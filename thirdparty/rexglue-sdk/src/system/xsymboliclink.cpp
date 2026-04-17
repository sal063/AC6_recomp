/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <rex/system/kernel_state.h>
#include <rex/system/xsymboliclink.h>

namespace rex::system {

XSymbolicLink::XSymbolicLink(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType), path_(), target_() {}

XSymbolicLink::XSymbolicLink() : XObject(kObjectType), path_(), target_() {}

XSymbolicLink::~XSymbolicLink() {}

void XSymbolicLink::Initialize(const std::string_view path, const std::string_view target) {
  path_ = std::string(path);
  target_ = std::string(target);
  // TODO(gibbed): kernel_state_->RegisterSymbolicLink(this);
}

}  // namespace rex::system
