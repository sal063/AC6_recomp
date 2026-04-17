#pragma once
/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <rex/system/xobject.h>
#include <rex/system/xtypes.h>
#include <rex/thread.h>

namespace rex::system {

// https://www.nirsoft.net/kernel_struct/vista/KEVENT.html
struct X_KEVENT {
  X_DISPATCH_HEADER header;
};
static_assert_size(X_KEVENT, 0x10);

class XEvent : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Event;

  explicit XEvent(KernelState* kernel_state);
  ~XEvent() override;

  void Initialize(bool manual_reset, bool initial_state);
  void InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header);

  void Query(uint32_t* out_type, uint32_t* out_state);

  int32_t Set(uint32_t priority_increment, bool wait);
  int32_t Pulse(uint32_t priority_increment, bool wait);
  int32_t Reset();
  void Clear();

 protected:
  rex::thread::WaitHandle* GetWaitHandle() override { return event_.get(); }

 private:
  bool manual_reset_ = false;
  std::unique_ptr<rex::thread::Event> event_;
};

}  // namespace rex::system
