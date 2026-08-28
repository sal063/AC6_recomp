/**
 * @file        tests/unit/core/sync_semantics_test.cpp
 * @brief       Windows-dispatcher semantics for the host wait primitives
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

// These tests encode behaviour that the Win32 dispatcher guarantees. The Win32
// backend delegates to the OS and so inherits it for free, which makes Windows
// the oracle: every test here must pass there unchanged. They exist because the
// POSIX backend reimplements the primitives by hand, and a guest race that is
// benign under Windows semantics turns into a permanent hang under different
// ones.

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <native/thread.h>

using namespace std::chrono_literals;
using rex::thread::Event;
using rex::thread::WaitResult;

namespace {

// Long enough that a correct implementation never trips it, short enough that a
// broken one fails the suite instead of hanging it.
constexpr auto kGenerous = 2000ms;

// Give a spawned thread time to actually reach its Wait before we signal.
void SettleUntilWaiting() { std::this_thread::sleep_for(50ms); }

}  // namespace

// ---------------------------------------------------------------------------
// Auto-reset events count releases, they are not a boolean
// ---------------------------------------------------------------------------
//
// This is the AC6 hang in miniature. Two threads are parked on one auto-reset
// event and it is signalled twice. On Windows each SetEvent releases one
// waiter, so both run. An implementation that models the event as a single
// bool collapses the two signals into one and strands a waiter forever.
TEST_CASE("auto-reset event releases one waiter per Set", "[sync]") {
  for (int trial = 0; trial < 20; ++trial) {
    auto event = Event::CreateAutoResetEvent(false);
    std::atomic<int> released{0};

    std::vector<std::thread> waiters;
    for (int i = 0; i < 2; ++i) {
      waiters.emplace_back([&] {
        if (rex::thread::Wait(event.get(), false, kGenerous) == WaitResult::kSuccess) {
          released.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    SettleUntilWaiting();

    // Back-to-back, so both land before either waiter can consume one.
    event->Set();
    event->Set();

    for (auto& t : waiters) {
      t.join();
    }
    INFO("trial " << trial);
    REQUIRE(released.load() == 2);
  }
}

// ---------------------------------------------------------------------------
// Waiters are released in arrival order
// ---------------------------------------------------------------------------
//
// Windows dispatcher objects release waiters FIFO (within a priority). Without
// an ordering guarantee a thread can lose every race indefinitely, which is
// starvation rather than a deadlock but is just as fatal to a guest that is
// waiting on a condition another thread already satisfied.
TEST_CASE("auto-reset event releases waiters in FIFO order", "[sync]") {
  constexpr int kTrials = 20;
  int first_waiter_won = 0;

  for (int trial = 0; trial < kTrials; ++trial) {
    auto event = Event::CreateAutoResetEvent(false);
    std::atomic<int> winner{-1};

    std::thread a([&] {
      if (rex::thread::Wait(event.get(), false, kGenerous) == WaitResult::kSuccess) {
        int expected = -1;
        winner.compare_exchange_strong(expected, 0);
      }
    });
    SettleUntilWaiting();  // a is queued first

    std::thread b([&] {
      if (rex::thread::Wait(event.get(), false, kGenerous) == WaitResult::kSuccess) {
        int expected = -1;
        winner.compare_exchange_strong(expected, 1);
      }
    });
    SettleUntilWaiting();

    event->Set();   // must release 'a'
    SettleUntilWaiting();
    event->Set();   // release the other so the trial can finish

    a.join();
    b.join();
    if (winner.load() == 0) {
      ++first_waiter_won;
    }
  }

  INFO("first-arrived waiter won " << first_waiter_won << " of " << kTrials << " trials");
  REQUIRE(first_waiter_won == kTrials);
}

// ---------------------------------------------------------------------------
// SignalAndWait must not lose a signal delivered in the release/wait window
// ---------------------------------------------------------------------------
//
// SignalObjectAndWait exists so a thread can release a lock and enqueue on a
// condition atomically - the release-and-wait at the heart of every hand-rolled
// condition variable, including the one AC6 uses. Implemented as a separate
// Signal() then Wait(), another thread can take the lock and signal the
// condition in the gap. With more than one waiter that signal can be consumed
// by somebody else, and the caller sleeps forever on a condition that is
// already true.
TEST_CASE("SignalAndWait does not lose a wakeup to the release window", "[sync]") {
  for (int trial = 0; trial < 20; ++trial) {
    auto lock_event = Event::CreateAutoResetEvent(true);    // mutex: signalled == available
    auto cond_event = Event::CreateAutoResetEvent(false);   // condition
    std::atomic<int> released{0};

    std::vector<std::thread> waiters;
    for (int i = 0; i < 2; ++i) {
      waiters.emplace_back([&] {
        // Acquire the "mutex", then atomically release it and wait.
        REQUIRE(rex::thread::Wait(lock_event.get(), false, kGenerous) == WaitResult::kSuccess);
        if (rex::thread::SignalAndWait(lock_event.get(), cond_event.get(), false, kGenerous) ==
            WaitResult::kSuccess) {
          released.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    SettleUntilWaiting();

    // One notification per waiter, from a thread that also takes the lock -
    // exactly the setter side of the guest's condition variable.
    for (int i = 0; i < 2; ++i) {
      REQUIRE(rex::thread::Wait(lock_event.get(), false, kGenerous) == WaitResult::kSuccess);
      lock_event->Set();
      cond_event->Set();
      std::this_thread::sleep_for(10ms);
    }

    for (auto& t : waiters) {
      t.join();
    }
    INFO("trial " << trial);
    REQUIRE(released.load() == 2);
  }
}

// ---------------------------------------------------------------------------
// Regression guards for the rewrite - these should pass before and after
// ---------------------------------------------------------------------------

TEST_CASE("manual-reset event releases every waiter", "[sync]") {
  auto event = Event::CreateManualResetEvent(false);
  std::atomic<int> released{0};

  std::vector<std::thread> waiters;
  for (int i = 0; i < 4; ++i) {
    waiters.emplace_back([&] {
      if (rex::thread::Wait(event.get(), false, kGenerous) == WaitResult::kSuccess) {
        released.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  SettleUntilWaiting();

  event->Set();  // one Set, all four go

  for (auto& t : waiters) {
    t.join();
  }
  REQUIRE(released.load() == 4);
}

TEST_CASE("auto-reset event retains a signal with no waiter present", "[sync]") {
  auto event = Event::CreateAutoResetEvent(false);
  event->Set();
  // Already signalled: this must return immediately and consume the signal.
  REQUIRE(rex::thread::Wait(event.get(), false, kGenerous) == WaitResult::kSuccess);
  // Consumed: the next wait must time out rather than succeed.
  REQUIRE(rex::thread::Wait(event.get(), false, 100ms) == WaitResult::kTimeout);
}

TEST_CASE("wait times out when never signalled", "[sync]") {
  auto event = Event::CreateAutoResetEvent(false);
  REQUIRE(rex::thread::Wait(event.get(), false, 100ms) == WaitResult::kTimeout);
}

TEST_CASE("WaitMultiple wait-any returns the signalled index", "[sync]") {
  auto a = Event::CreateAutoResetEvent(false);
  auto b = Event::CreateAutoResetEvent(false);
  rex::thread::WaitHandle* handles[] = {a.get(), b.get()};

  b->Set();
  auto result = rex::thread::WaitAny(handles, 2, false, kGenerous);
  REQUIRE(result.first == WaitResult::kSuccess);
  REQUIRE(result.second == 1);
}

TEST_CASE("WaitMultiple wait-all requires every object", "[sync]") {
  auto a = Event::CreateAutoResetEvent(false);
  auto b = Event::CreateAutoResetEvent(false);
  rex::thread::WaitHandle* handles[] = {a.get(), b.get()};

  a->Set();
  REQUIRE(rex::thread::WaitAll(handles, 2, false, 100ms) == WaitResult::kTimeout);

  a->Set();
  b->Set();
  REQUIRE(rex::thread::WaitAll(handles, 2, false, kGenerous) == WaitResult::kSuccess);
}
