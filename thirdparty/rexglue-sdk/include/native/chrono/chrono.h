#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include <native/chrono/clock.h>

namespace rex {

using hundrednano = std::ratio<1, 10000000>;

namespace chrono {

using hundrednanoseconds = std::chrono::duration<int64_t, hundrednano>;

namespace detail {

enum class Domain {
  Host,
  Guest,
};

template <Domain domain>
struct NtSystemClock {
  using rep = int64_t;
  using period = hundrednano;
  using duration = hundrednanoseconds;
  using time_point = std::chrono::time_point<NtSystemClock<domain>>;

  static constexpr std::chrono::seconds unix_epoch_delta() {
    const auto filetime_epoch = std::chrono::year{1601} / std::chrono::month{1} / std::chrono::day{1};
    const auto system_epoch = std::chrono::year{1970} / std::chrono::month{1} / std::chrono::day{1};
    const auto filetime_point =
        static_cast<std::chrono::sys_seconds>(static_cast<std::chrono::sys_days>(filetime_epoch));
    const auto system_point =
        static_cast<std::chrono::sys_seconds>(static_cast<std::chrono::sys_days>(system_epoch));
    return filetime_point.time_since_epoch() - system_point.time_since_epoch();
  }

  static constexpr uint64_t to_file_time(const time_point& time) noexcept {
    return static_cast<uint64_t>(time.time_since_epoch().count());
  }

  static constexpr time_point from_file_time(uint64_t file_time) noexcept {
    return time_point{duration{file_time}};
  }

  static constexpr std::chrono::system_clock::time_point to_sys(const time_point& time)
    requires(domain == Domain::Host)
  {
    using sys_duration = std::chrono::system_clock::duration;
    using sys_time = std::chrono::system_clock::time_point;

    auto adjusted = time;
    adjusted += unix_epoch_delta();
    const auto casted = std::chrono::time_point_cast<sys_duration>(adjusted);
    return sys_time{casted.time_since_epoch()};
  }

  static constexpr time_point from_sys(const std::chrono::system_clock::time_point& time)
    requires(domain == Domain::Host)
  {
    const auto casted = std::chrono::time_point_cast<duration>(time);
    auto adjusted = time_point{casted.time_since_epoch()};
    adjusted -= unix_epoch_delta();
    return adjusted;
  }

  [[nodiscard]] static time_point now() noexcept {
    if constexpr (domain == Domain::Host) {
      return from_file_time(Clock::QueryHostSystemTime());
    } else {
      return from_file_time(Clock::QueryGuestSystemTime());
    }
  }
};

}  // namespace detail

using WinSystemClock = detail::NtSystemClock<detail::Domain::Host>;
using XSystemClock = detail::NtSystemClock<detail::Domain::Guest>;

}  // namespace chrono
}  // namespace rex

namespace std::chrono {

template <>
struct clock_time_conversion<::rex::chrono::WinSystemClock, ::rex::chrono::XSystemClock> {
  using win_clock = ::rex::chrono::WinSystemClock;
  using guest_clock = ::rex::chrono::XSystemClock;

  template <typename Duration>
  typename win_clock::time_point operator()(const std::chrono::time_point<guest_clock, Duration>& time) const {
    std::atomic_thread_fence(std::memory_order_acq_rel);
    const auto win_now = win_clock::now();
    const auto guest_now = guest_clock::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    auto delta = time - guest_now;
    if (!REXCVAR_GET(clock_no_scaling)) {
      delta = std::chrono::floor<win_clock::duration>(
          delta * ::rex::chrono::Clock::guest_time_scalar());
    }
    return win_now + delta;
  }
};

template <>
struct clock_time_conversion<::rex::chrono::XSystemClock, ::rex::chrono::WinSystemClock> {
  using win_clock = ::rex::chrono::WinSystemClock;
  using guest_clock = ::rex::chrono::XSystemClock;

  template <typename Duration>
  typename guest_clock::time_point operator()(const std::chrono::time_point<win_clock, Duration>& time) const {
    std::atomic_thread_fence(std::memory_order_acq_rel);
    const auto win_now = win_clock::now();
    const auto guest_now = guest_clock::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    ::rex::chrono::hundrednanoseconds delta = time - win_now;
    if (!REXCVAR_GET(clock_no_scaling)) {
      delta = std::chrono::floor<win_clock::duration>(
          delta / ::rex::chrono::Clock::guest_time_scalar());
    }
    return guest_now + delta;
  }
};

}  // namespace std::chrono
