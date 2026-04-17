// Native runtime - Timer queue
// Part of the AC6 Recompilation native foundation

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

// A platform independent implementation of a timer queue similar to
// Windows CreateTimerQueueTimer with WT_EXECUTEINTIMERTHREAD.

namespace rex::thread {

class TimerQueue;

struct TimerQueueWaitItem {
  using clock = std::chrono::steady_clock;

  TimerQueueWaitItem(std::move_only_function<void(void*)> callback, void* userdata,
                     TimerQueue* parent_queue, clock::time_point due, clock::duration interval)
      : callback_(std::move(callback)),
        userdata_(userdata),
        parent_queue_(parent_queue),
        due_(due),
        interval_(interval),
        state_(State::kIdle) {}

  // Cancel the pending wait item. No callbacks will be running after this call.
  void Disarm();

  friend TimerQueue;

 private:
  enum class State : uint_least8_t {
    kIdle = 0,                // Waiting for the due time
    kInCallback,              // Callback is being executed
    kInCallbackSelfDisarmed,  // Callback is being executed and disarmed itself
    kDisarmed                 // Disarmed, waiting for destruction
  };
  static_assert(std::atomic<State>::is_always_lock_free);

  std::move_only_function<void(void*)> callback_;
  void* userdata_;
  TimerQueue* parent_queue_;
  clock::time_point due_;
  clock::duration interval_;  // zero if not recurring
  std::atomic<State> state_;
};

std::weak_ptr<TimerQueueWaitItem> QueueTimerOnce(std::move_only_function<void(void*)> callback,
                                                 void* userdata,
                                                 TimerQueueWaitItem::clock::time_point due);

// Callback is first executed at due, then again repeatedly after interval
// passes (unless interval == 0).
std::weak_ptr<TimerQueueWaitItem> QueueTimerRecurring(std::move_only_function<void(void*)> callback,
                                                      void* userdata,
                                                      TimerQueueWaitItem::clock::time_point due,
                                                      TimerQueueWaitItem::clock::duration interval);

}  // namespace rex::thread
