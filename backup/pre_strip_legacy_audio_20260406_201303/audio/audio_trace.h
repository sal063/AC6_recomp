/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace rex::audio {

enum class AudioTraceSubsystem : uint8_t {
  kCore = 0,
  kKernel = 1,
  kHost = 2,
  kXma = 3,
};

enum class AudioTraceEventType : uint8_t {
  kClientRegistered = 0,
  kClientUnregistered = 1,
  kFrameSubmitted = 2,
  kFrameConsumed = 3,
  kFrameDropped = 4,
  kMalformedFrame = 5,
  kXmaAllocated = 6,
  kXmaReleased = 7,
  kXmaStateUpdated = 8,
};

struct AudioTraceEvent {
  uint64_t timestamp_us{0};
  AudioTraceSubsystem subsystem{AudioTraceSubsystem::kCore};
  AudioTraceEventType event_type{AudioTraceEventType::kClientRegistered};
  uint32_t client_id{0};
  uint32_t value_0{0};
  uint32_t value_1{0};
  uint32_t value_2{0};
};

class AudioTraceBuffer {
 public:
  void Reset();
  void Record(AudioTraceSubsystem subsystem, AudioTraceEventType event_type, uint32_t client_id,
              uint32_t value_0 = 0, uint32_t value_1 = 0, uint32_t value_2 = 0);

  size_t size() const;
  std::vector<AudioTraceEvent> Snapshot() const;

 private:
  static constexpr size_t kMaximumTraceEventCount = 2048;

  mutable std::mutex mutex_;
  std::deque<AudioTraceEvent> events_;
};

}  // namespace rex::audio
