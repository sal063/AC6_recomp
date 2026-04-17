// Native runtime - Global critical region mutex
// Part of the AC6 Recompilation native foundation

#pragma once

#include <mutex>

namespace rex::thread {

// The global critical region mutex singleton.
// This must guard any operation that may suspend threads or be sensitive to
// being suspended such as global table locks and such.
class global_critical_region {
 public:
  static std::recursive_mutex& mutex();

  // Acquires a lock on the global critical section.
  static std::unique_lock<std::recursive_mutex> AcquireDirect() {
    return std::unique_lock<std::recursive_mutex>(mutex());
  }

  // Acquires a lock on the global critical section.
  inline std::unique_lock<std::recursive_mutex> Acquire() {
    return std::unique_lock<std::recursive_mutex>(mutex());
  }

  // Acquires a deferred lock on the global critical section.
  inline std::unique_lock<std::recursive_mutex> AcquireDeferred() {
    return std::unique_lock<std::recursive_mutex>(mutex(), std::defer_lock);
  }

  // Tries to acquire a lock on the global critical section.
  inline std::unique_lock<std::recursive_mutex> TryAcquire() {
    return std::unique_lock<std::recursive_mutex>(mutex(), std::try_to_lock);
  }
};

}  // namespace rex::thread
