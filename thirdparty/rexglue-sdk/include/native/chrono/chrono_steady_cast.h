#pragma once

#include <atomic>
#include <chrono>

#include <native/chrono/chrono.h>

namespace std::chrono {

template <>
struct clock_time_conversion<::rex::chrono::WinSystemClock, std::chrono::steady_clock> {
  using win_clock = ::rex::chrono::WinSystemClock;
  using steady_clock_type = std::chrono::steady_clock;

  template <typename Duration>
  typename win_clock::time_point operator()(
      const std::chrono::time_point<steady_clock_type, Duration>& time) const {
    std::atomic_thread_fence(std::memory_order_acq_rel);
    const auto steady_now = steady_clock_type::now();
    const auto win_now = win_clock::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    const auto delta = std::chrono::floor<win_clock::duration>(time - steady_now);
    return win_now + delta;
  }
};

template <>
struct clock_time_conversion<std::chrono::steady_clock, ::rex::chrono::WinSystemClock> {
  using win_clock = ::rex::chrono::WinSystemClock;
  using steady_clock_type = std::chrono::steady_clock;

  template <typename Duration>
  steady_clock_type::time_point operator()(
      const std::chrono::time_point<win_clock, Duration>& time) const {
    std::atomic_thread_fence(std::memory_order_acq_rel);
    const auto steady_now = steady_clock_type::now();
    const auto win_now = win_clock::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    return steady_now + (time - win_now);
  }
};

}  // namespace std::chrono
