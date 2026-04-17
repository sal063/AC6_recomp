// Native runtime - Threading primitives
// Part of the AC6 Recompilation native foundation

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <native/assert.h>
#include <native/chrono/chrono.h>
#include <native/literals.h>
#include <native/platform.h>
#include <native/thread/timer_queue.h>

namespace rex::thread {

using namespace rex::literals;

#if REX_PLATFORM_ANDROID
void AndroidInitialize();
void AndroidShutdown();
#endif

// This is more like an Event with self-reset when returning from Wait()
class Fence {
 public:
  Fence() : signal_state_(0) {}

  void Signal() {
    std::unique_lock<std::mutex> lock(mutex_);
    signal_state_.store(signal_state_.load() | SIGMASK_, std::memory_order_release);
    cond_.notify_all();
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    assert_true((signal_state_.load() & ~SIGMASK_) < (SIGMASK_ - 1) && "Too many threads?");

    auto signal_state = signal_state_.fetch_add(1) + 1;
    for (; !(signal_state & SIGMASK_); signal_state = signal_state_.load()) {
      cond_.wait(lock);
    }

    assert_true((signal_state & ~SIGMASK_) > 0);
    if (signal_state == (1 | SIGMASK_)) {
      signal_state_.store(0, std::memory_order_release);
    } else {
      signal_state_.store(--signal_state, std::memory_order_release);
    }
  }

 private:
  using state_t_ = uint_fast32_t;
  static constexpr state_t_ SIGMASK_ = state_t_(1) << (sizeof(state_t_) * 8 - 1);

  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<state_t_> signal_state_;
};

// Returns the total number of logical processors in the host system.
uint32_t logical_processor_count();

// Enables the current process to set thread affinity.
void EnableAffinityConfiguration();

uint32_t current_thread_system_id();

uint32_t current_thread_id();
void set_current_thread_id(uint32_t id);

void set_current_thread_name(const std::string_view name);

void MaybeYield();

void SyncMemory();

void Sleep(std::chrono::microseconds duration);
template <typename Rep, typename Period>
void Sleep(std::chrono::duration<Rep, Period> duration) {
  Sleep(std::chrono::duration_cast<std::chrono::microseconds>(duration));
}

enum class SleepResult {
  kSuccess,
  kAlerted,
};
SleepResult AlertableSleep(std::chrono::microseconds duration);
template <typename Rep, typename Period>
SleepResult AlertableSleep(std::chrono::duration<Rep, Period> duration) {
  return AlertableSleep(std::chrono::duration_cast<std::chrono::microseconds>(duration));
}

typedef uint32_t TlsHandle;
constexpr TlsHandle kInvalidTlsHandle = UINT_MAX;

TlsHandle AllocateTlsHandle();
bool FreeTlsHandle(TlsHandle handle);
uintptr_t GetTlsValue(TlsHandle handle);
bool SetTlsValue(TlsHandle handle, uintptr_t value);

class HighResolutionTimer {
  HighResolutionTimer(std::chrono::milliseconds interval, std::function<void()> callback) {
    assert_not_null(callback);
    wait_item_ = QueueTimerRecurring([callback = std::move(callback)](void*) { callback(); },
                                     nullptr, TimerQueueWaitItem::clock::now(), interval);
  }

 public:
  ~HighResolutionTimer() {
    if (auto wait_item = wait_item_.lock()) {
      wait_item->Disarm();
    }
  }

  static std::unique_ptr<HighResolutionTimer> CreateRepeating(std::chrono::milliseconds period,
                                                              std::function<void()> callback) {
    return std::unique_ptr<HighResolutionTimer>(
        new HighResolutionTimer(period, std::move(callback)));
  }

 private:
  std::weak_ptr<TimerQueueWaitItem> wait_item_;
};

enum class WaitResult {
  kSuccess,
  kUserCallback,
  kTimeout,
  kAbandoned,
  kFailed,
};

class WaitHandle {
 public:
  virtual ~WaitHandle() = default;
  virtual void* native_handle() const = 0;

 protected:
  WaitHandle() = default;
};

WaitResult Wait(WaitHandle* wait_handle, bool is_alertable,
                std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

WaitResult SignalAndWait(WaitHandle* wait_handle_to_signal, WaitHandle* wait_handle_to_wait_on,
                         bool is_alertable,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

std::pair<WaitResult, size_t> WaitMultiple(
    WaitHandle* wait_handles[], size_t wait_handle_count, bool wait_all, bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

inline WaitResult WaitAll(WaitHandle* wait_handles[], size_t wait_handle_count, bool is_alertable,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  return WaitMultiple(wait_handles, wait_handle_count, true, is_alertable, timeout).first;
}
inline WaitResult WaitAll(std::vector<WaitHandle*> wait_handles, bool is_alertable,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  return WaitAll(wait_handles.data(), wait_handles.size(), is_alertable, timeout);
}

inline std::pair<WaitResult, size_t> WaitAny(
    WaitHandle* wait_handles[], size_t wait_handle_count, bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  return WaitMultiple(wait_handles, wait_handle_count, false, is_alertable, timeout);
}
inline std::pair<WaitResult, size_t> WaitAny(
    std::vector<WaitHandle*> wait_handles, bool is_alertable,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  return WaitAny(wait_handles.data(), wait_handles.size(), is_alertable, timeout);
}

class Event : public WaitHandle {
 public:
  static std::unique_ptr<Event> CreateManualResetEvent(bool initial_state);
  static std::unique_ptr<Event> CreateAutoResetEvent(bool initial_state);
  virtual void Set() = 0;
  virtual void Reset() = 0;
  virtual void Pulse() = 0;
};

class Semaphore : public WaitHandle {
 public:
  static std::unique_ptr<Semaphore> Create(int initial_count, int maximum_count);
  virtual bool Release(int release_count, int* out_previous_count) = 0;
};

class Mutant : public WaitHandle {
 public:
  static std::unique_ptr<Mutant> Create(bool initial_owner);
  virtual bool Release() = 0;
};

class Timer : public WaitHandle {
 public:
  using WClock_ = rex::chrono::WinSystemClock;
  using GClock_ = std::chrono::steady_clock;

  static std::unique_ptr<Timer> CreateManualResetTimer();
  static std::unique_ptr<Timer> CreateSynchronizationTimer();

  virtual bool SetOnceAfter(rex::chrono::hundrednanoseconds rel_time,
                            std::function<void()> opt_callback = nullptr) = 0;
  virtual bool SetOnceAt(WClock_::time_point due_time,
                         std::function<void()> opt_callback = nullptr) = 0;
  virtual bool SetOnceAt(GClock_::time_point due_time,
                         std::function<void()> opt_callback = nullptr) = 0;

  virtual bool SetRepeatingAfter(rex::chrono::hundrednanoseconds rel_time,
                                 std::chrono::milliseconds period,
                                 std::function<void()> opt_callback = nullptr) = 0;
  virtual bool SetRepeatingAt(WClock_::time_point due_time, std::chrono::milliseconds period,
                              std::function<void()> opt_callback = nullptr) = 0;
  virtual bool SetRepeatingAt(GClock_::time_point due_time, std::chrono::milliseconds period,
                              std::function<void()> opt_callback = nullptr) = 0;

  virtual bool Cancel() = 0;
};

#if REX_PLATFORM_WIN32
struct ThreadPriority {
  static const int32_t kLowest = -2;
  static const int32_t kBelowNormal = -1;
  static const int32_t kNormal = 0;
  static const int32_t kAboveNormal = 1;
  static const int32_t kHighest = 2;
};
#else
struct ThreadPriority {
  static const int32_t kLowest = 1;
  static const int32_t kBelowNormal = 8;
  static const int32_t kNormal = 16;
  static const int32_t kAboveNormal = 24;
  static const int32_t kHighest = 32;
};
#endif

class Thread : public WaitHandle {
 public:
  struct CreationParameters {
    size_t stack_size = 4_MiB;
    bool create_suspended = false;
    int32_t initial_priority = 0;
  };

  static std::unique_ptr<Thread> Create(CreationParameters params,
                                        std::function<void()> start_routine);
  static Thread* GetCurrentThread();

  static void Exit(int exit_code);

  virtual uint32_t system_id() const = 0;

  const std::string& name() const { return name_; }
  virtual void set_name(std::string name) { name_ = std::move(name); }

  virtual int32_t priority() = 0;
  virtual void set_priority(int32_t new_priority) = 0;

  virtual uint64_t affinity_mask() = 0;
  virtual void set_affinity_mask(uint64_t new_affinity_mask) = 0;

  virtual void QueueUserCallback(std::function<void()> callback) = 0;

  virtual bool Resume(uint32_t* out_previous_suspend_count = nullptr) = 0;
  virtual bool Suspend(uint32_t* out_previous_suspend_count = nullptr) = 0;

  virtual void Terminate(int exit_code) = 0;

 protected:
  std::string name_;
};

}  // namespace rex::thread
