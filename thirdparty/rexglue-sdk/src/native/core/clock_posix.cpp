#include <native/assert.h>
#include <native/chrono/clock.h>
#include <native/platform.h>

static_assert(REX_PLATFORM_LINUX || REX_PLATFORM_MAC, "This file is POSIX-only");

#include <sys/time.h>

namespace rex::chrono {

uint64_t Clock::host_tick_frequency_platform() {
  timespec resolution;
  const int error = clock_getres(CLOCK_MONOTONIC_RAW, &resolution);
  assert_zero(error);
  assert_zero(resolution.tv_sec);
  return 1000000000ull / static_cast<uint64_t>(resolution.tv_nsec);
}

uint64_t Clock::host_tick_count_platform() {
  timespec now;
  const int error = clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  assert_zero(error);
  return static_cast<uint64_t>(now.tv_nsec) + static_cast<uint64_t>(now.tv_sec) * 1000000000ull;
}

uint64_t Clock::QueryHostSystemTime() {
  constexpr uint64_t kSecondsPerDay = 3600 * 24;
  constexpr uint64_t kSeconds1601To1970 = ((369 * 365 + 89) * kSecondsPerDay);

  timeval now;
  const int error = gettimeofday(&now, nullptr);
  assert_zero(error);

  return static_cast<uint64_t>(
      (static_cast<int64_t>(now.tv_sec) + kSeconds1601To1970) * 10000000ull + now.tv_usec * 10);
}

uint64_t Clock::QueryHostUptimeMillis() {
  return host_tick_count_platform() * 1000 / host_tick_frequency_platform();
}

}  // namespace rex::chrono
