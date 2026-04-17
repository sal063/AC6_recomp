#include <native/chrono/clock.h>
#include <native/platform.h>

static_assert(REX_PLATFORM_WIN32, "This file is Windows-only");

#include "../../core/platform_win.h"

namespace rex::chrono {

uint64_t Clock::host_tick_frequency_platform() {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  return static_cast<uint64_t>(frequency.QuadPart);
}

uint64_t Clock::host_tick_count_platform() {
  LARGE_INTEGER counter;
  if (!QueryPerformanceCounter(&counter)) {
    return 0;
  }
  return static_cast<uint64_t>(counter.QuadPart);
}

uint64_t Clock::QueryHostSystemTime() {
  FILETIME time;
  GetSystemTimeAsFileTime(&time);
  return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

uint64_t Clock::QueryHostUptimeMillis() {
  return host_tick_count_platform() * 1000 / host_tick_frequency_platform();
}

}  // namespace rex::chrono
