#pragma once
/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <rex/system/xobject.h>
#include <rex/system/xtypes.h>
#include <rex/thread.h>

namespace rex::system {

class XThread;

class XMutant : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Mutant;

  explicit XMutant(KernelState* kernel_state);
  ~XMutant() override;

  void Initialize(bool initial_owner);
  void InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header);

  X_STATUS ReleaseMutant(uint32_t priority_increment, bool abandon, bool wait);

 protected:
  rex::thread::WaitHandle* GetWaitHandle() override { return mutant_.get(); }
  void WaitCallback() override;

 private:
  XMutant();

  std::unique_ptr<rex::thread::Mutant> mutant_;
  XThread* owning_thread_ = nullptr;
};

}  // namespace rex::system
