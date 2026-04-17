#pragma once
/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <queue>

#include <rex/system/xobject.h>
#include <rex/system/xtypes.h>
#include <rex/thread.h>

namespace rex::system {

class XIOCompletion : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::IOCompletion;

  explicit XIOCompletion(KernelState* kernel_state);
  ~XIOCompletion() override;

  struct IONotification {
    uint32_t key_context;
    uint32_t apc_context;
    uint32_t status;
    uint32_t num_bytes;
  };

  void QueueNotification(IONotification& notification);

  // Returns true if the wait ended because a notification was received.
  bool WaitForNotification(uint64_t wait_ticks, IONotification* notify);

 private:
  static const uint32_t kMaxNotifications = 1024;

  std::mutex notification_lock_;
  std::queue<IONotification> notifications_;
  std::unique_ptr<rex::thread::Semaphore> notification_semaphore_ = nullptr;
};

}  // namespace rex::system
