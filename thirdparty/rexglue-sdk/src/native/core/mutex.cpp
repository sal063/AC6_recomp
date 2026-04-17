#include <native/thread/mutex.h>

namespace rex::thread {

std::recursive_mutex& global_critical_region::mutex() {
  static std::recursive_mutex global_mutex;
  return global_mutex;
}

}  // namespace rex::thread
