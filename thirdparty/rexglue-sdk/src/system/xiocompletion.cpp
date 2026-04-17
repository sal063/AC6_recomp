/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <rex/system/xiocompletion.h>

namespace rex::system {

XIOCompletion::XIOCompletion(KernelState* kernel_state) : XObject(kernel_state, kObjectType) {
  notification_semaphore_ = rex::thread::Semaphore::Create(0, kMaxNotifications);
  assert_not_null(notification_semaphore_);
}

XIOCompletion::~XIOCompletion() = default;

void XIOCompletion::QueueNotification(IONotification& notification) {
  std::unique_lock<std::mutex> lock(notification_lock_);

  notifications_.push(notification);
  notification_semaphore_->Release(1, nullptr);
}

bool XIOCompletion::WaitForNotification(uint64_t wait_ticks, IONotification* notify) {
  auto ms = std::chrono::milliseconds(TimeoutTicksToMs(wait_ticks));
  auto res = rex::thread::Wait(notification_semaphore_.get(), false, ms);
  if (res == rex::thread::WaitResult::kSuccess) {
    std::unique_lock<std::mutex> lock(notification_lock_);
    assert_false(notifications_.empty());

    std::memcpy(notify, &notifications_.front(), sizeof(IONotification));
    notifications_.pop();

    return true;
  }

  return false;
}

}  // namespace rex::system
