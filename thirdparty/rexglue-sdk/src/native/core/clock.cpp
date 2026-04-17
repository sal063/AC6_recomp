#include <algorithm>
#include <limits>
#include <mutex>

#include <native/chrono/clock.h>
#include <rex/cvar.h>
#include <native/math.h>

REXCVAR_DEFINE_BOOL(clock_no_scaling, false, "Clock",
                    "Disable clock scaling (inverted: false = scaling enabled)");
REXCVAR_DEFINE_BOOL(clock_source_raw, false, "Clock", "Use raw clock source without scaling");

namespace rex::chrono {
namespace {

double g_guest_time_scalar = 1.0;
uint64_t g_guest_tick_frequency = Clock::host_tick_frequency_platform();
uint64_t g_guest_system_time_base = Clock::QueryHostSystemTime();
std::pair<uint64_t, uint64_t> g_guest_tick_ratio{1, 1};

uint64_t g_last_guest_tick_count = 0;
uint64_t g_last_host_tick_count = Clock::QueryHostTickCount();
std::mutex g_tick_mutex;

void recompute_guest_tick_ratio() {
  auto ratio = std::make_pair(g_guest_tick_frequency, Clock::QueryHostTickFrequency());
  if (g_guest_time_scalar > 1.0) {
    ratio.first *= static_cast<uint64_t>(g_guest_time_scalar * 10.0);
    ratio.second *= 10;
  } else {
    ratio.first *= 10;
    ratio.second *= static_cast<uint64_t>(10.0 / g_guest_time_scalar);
  }

  reduce_fraction(ratio);

  std::lock_guard<std::mutex> lock(g_tick_mutex);
  g_guest_tick_ratio = ratio;
}

uint64_t update_guest_clock() {
  const uint64_t host_tick_count = Clock::QueryHostTickCount();

  if (REXCVAR_GET(clock_no_scaling)) {
    return host_tick_count * g_guest_tick_ratio.first / g_guest_tick_ratio.second;
  }

  std::unique_lock<std::mutex> lock(g_tick_mutex, std::defer_lock);
  if (lock.try_lock()) {
    const uint64_t host_tick_delta =
        host_tick_count > g_last_host_tick_count ? host_tick_count - g_last_host_tick_count : 0;
    g_last_host_tick_count = host_tick_count;
    const uint64_t guest_tick_delta =
        host_tick_delta * g_guest_tick_ratio.first / g_guest_tick_ratio.second;
    g_last_guest_tick_count += guest_tick_delta;
    return g_last_guest_tick_count;
  }

  lock.lock();
  return g_last_guest_tick_count;
}

uint64_t query_guest_system_time_offset() {
  if (REXCVAR_GET(clock_no_scaling)) {
    return Clock::QueryHostSystemTime() - g_guest_system_time_base;
  }

  const uint64_t guest_tick_count = update_guest_clock();
  uint64_t numerator = 10000000;
  uint64_t denominator = g_guest_tick_frequency;
  reduce_fraction(numerator, denominator);
  return guest_tick_count * numerator / denominator;
}

}  // namespace

uint64_t Clock::QueryHostTickFrequency() {
#if REX_CLOCK_RAW_AVAILABLE
  if (REXCVAR_GET(clock_source_raw)) {
    return host_tick_frequency_raw();
  }
#endif
  return host_tick_frequency_platform();
}

uint64_t Clock::QueryHostTickCount() {
#if REX_CLOCK_RAW_AVAILABLE
  if (REXCVAR_GET(clock_source_raw)) {
    return host_tick_count_raw();
  }
#endif
  return host_tick_count_platform();
}

double Clock::guest_time_scalar() {
  return g_guest_time_scalar;
}

void Clock::set_guest_time_scalar(double scalar) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }
  g_guest_time_scalar = scalar;
  recompute_guest_tick_ratio();
}

std::pair<uint64_t, uint64_t> Clock::guest_tick_ratio() {
  std::lock_guard<std::mutex> lock(g_tick_mutex);
  return g_guest_tick_ratio;
}

uint64_t Clock::guest_tick_frequency() {
  return g_guest_tick_frequency;
}

void Clock::set_guest_tick_frequency(uint64_t frequency) {
  g_guest_tick_frequency = frequency;
  recompute_guest_tick_ratio();
}

uint64_t Clock::guest_system_time_base() {
  return g_guest_system_time_base;
}

void Clock::set_guest_system_time_base(uint64_t time_base) {
  g_guest_system_time_base = time_base;
}

uint64_t Clock::QueryGuestTickCount() {
  return update_guest_clock();
}

uint64_t Clock::QueryGuestSystemTime() {
  if (REXCVAR_GET(clock_no_scaling)) {
    return Clock::QueryHostSystemTime();
  }
  return g_guest_system_time_base + query_guest_system_time_offset();
}

uint32_t Clock::QueryGuestUptimeMillis() {
  return static_cast<uint32_t>(std::min<uint64_t>(
      query_guest_system_time_offset() / 10000, std::numeric_limits<uint32_t>::max()));
}

void Clock::SetGuestSystemTime(uint64_t system_time) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }
  g_guest_system_time_base = system_time - query_guest_system_time_offset();
}

uint32_t Clock::ScaleGuestDurationMillis(uint32_t guest_ms) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return guest_ms;
  }

  constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
  if (guest_ms >= kMax) {
    return static_cast<uint32_t>(kMax);
  }
  if (!guest_ms) {
    return 0;
  }

  const uint64_t scaled_ms = static_cast<uint64_t>(
      static_cast<double>(guest_ms) * g_guest_time_scalar);
  return static_cast<uint32_t>(std::min<uint64_t>(scaled_ms, kMax));
}

int64_t Clock::ScaleGuestDurationFileTime(int64_t guest_file_time) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return static_cast<uint64_t>(guest_file_time);
  }

  if (!guest_file_time) {
    return 0;
  }

  if (guest_file_time > 0) {
    const uint64_t guest_time = Clock::QueryGuestSystemTime();
    const int64_t relative_time = guest_file_time - static_cast<int64_t>(guest_time);
    const int64_t scaled_time = static_cast<int64_t>(relative_time * g_guest_time_scalar);
    return static_cast<int64_t>(guest_time) + scaled_time;
  }

  const uint64_t scaled_file_time =
      static_cast<uint64_t>(static_cast<double>(guest_file_time) * g_guest_time_scalar);
  return static_cast<int64_t>(scaled_file_time);
}

void Clock::ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }

  uint64_t scaled_sec = static_cast<uint64_t>(static_cast<double>(*tv_sec) * g_guest_time_scalar);
  uint64_t scaled_usec =
      static_cast<uint64_t>(static_cast<double>(*tv_usec) * g_guest_time_scalar);
  if (scaled_usec > std::numeric_limits<uint32_t>::max()) {
    const uint64_t overflow_sec = scaled_usec / 1000000;
    scaled_usec -= overflow_sec * 1000000;
    scaled_sec += overflow_sec;
  }
  *tv_sec = static_cast<int32_t>(scaled_sec);
  *tv_usec = static_cast<int32_t>(scaled_usec);
}

}  // namespace rex::chrono
