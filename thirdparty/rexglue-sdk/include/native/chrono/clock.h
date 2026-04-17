#pragma once

#include <chrono>
#include <cstdint>
#include <utility>

#include <native/platform.h>
#include <rex/cvar.h>

REXCVAR_DECLARE(bool, clock_no_scaling);
REXCVAR_DECLARE(bool, clock_source_raw);

#ifndef REX_CLOCK_RAW_AVAILABLE
#define REX_CLOCK_RAW_AVAILABLE 0
#endif

#if REX_ARCH_AMD64
#undef REX_CLOCK_RAW_AVAILABLE
#define REX_CLOCK_RAW_AVAILABLE 0
#endif

namespace rex::chrono {

class Clock {
 public:
  static uint64_t host_tick_frequency_platform();
#if REX_CLOCK_RAW_AVAILABLE
  static uint64_t host_tick_frequency_raw();
#endif

  static uint64_t host_tick_count_platform();
#if REX_CLOCK_RAW_AVAILABLE
  static uint64_t host_tick_count_raw();
#endif

  static uint64_t QueryHostTickFrequency();
  static uint64_t QueryHostTickCount();
  static uint64_t QueryHostSystemTime();
  static uint64_t QueryHostUptimeMillis();

  static double guest_time_scalar();
  static void set_guest_time_scalar(double scalar);
  static std::pair<uint64_t, uint64_t> guest_tick_ratio();
  static uint64_t guest_tick_frequency();
  static void set_guest_tick_frequency(uint64_t frequency);
  static uint64_t guest_system_time_base();
  static void set_guest_system_time_base(uint64_t time_base);

  static uint64_t QueryGuestTickCount();
  static uint64_t QueryGuestSystemTime();
  static uint32_t QueryGuestUptimeMillis();

  static void SetGuestSystemTime(uint64_t system_time);

  static uint32_t ScaleGuestDurationMillis(uint32_t guest_ms);
  static int64_t ScaleGuestDurationFileTime(int64_t guest_file_time);
  static void ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec);
};

}  // namespace rex::chrono
