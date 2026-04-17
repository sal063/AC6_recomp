#pragma once
/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <rex/system/xobject.h>
#include <rex/system/xtypes.h>
#include <rex/thread.h>

namespace rex::system {

struct X_KSEMAPHORE {
  X_DISPATCH_HEADER header;
  rex::be<uint32_t> limit;
};
static_assert_size(X_KSEMAPHORE, 0x14);

class XSemaphore : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Semaphore;

  explicit XSemaphore(KernelState* kernel_state);
  ~XSemaphore() override;

  [[nodiscard]] bool Initialize(int32_t initial_count, int32_t maximum_count);
  [[nodiscard]] bool InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header);

  [[nodiscard]] bool ReleaseSemaphore(int32_t release_count, int32_t* out_previous_count);

 protected:
  rex::thread::WaitHandle* GetWaitHandle() override { return semaphore_.get(); }

 private:
  std::unique_ptr<rex::thread::Semaphore> semaphore_;
  uint32_t maximum_count_ = 0;
};

}  // namespace rex::system
