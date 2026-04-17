/**
 * ReXGlue native audio runtime
 * Part of the AC6 Recompilation project
 */

#include <chrono>

#include <native/audio/audio_trace.h>

namespace rex::audio {

namespace {

uint64_t CurrentTimestampUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}  // namespace

void AudioTraceBuffer::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  events_.clear();
}

void AudioTraceBuffer::Record(const AudioTraceSubsystem subsystem,
                              const AudioTraceEventType event_type, const uint32_t client_id,
                              const uint32_t value_0, const uint32_t value_1,
                              const uint32_t value_2) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (events_.size() >= kMaximumTraceEventCount) {
    events_.pop_front();
  }
  events_.push_back(
      {CurrentTimestampUs(), subsystem, event_type, client_id, value_0, value_1, value_2});
}

size_t AudioTraceBuffer::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return events_.size();
}

std::vector<AudioTraceEvent> AudioTraceBuffer::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {events_.begin(), events_.end()};
}

}  // namespace rex::audio
