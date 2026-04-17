// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <chrono>
#include <cstdint>

namespace rex::audio {

class AudioClock {
 public:
  using clock_type = std::chrono::steady_clock;
  using time_point = clock_type::time_point;
  using duration = clock_type::duration;

  void Reset();
  void AdvanceFrames(uint32_t frame_count);
  void AdvanceSamples(uint64_t sample_count);

  void SetPaused(bool paused);
  bool is_paused() const { return paused_; }

  uint64_t consumed_samples() const { return consumed_samples_; }
  uint64_t consumed_frames() const { return consumed_frames_; }
  uint32_t render_driver_tic() const;

  /// Returns approximate drift between host clock and sample-derived time.
  /// Positive = host clock ahead of audio clock.
  double drift_ms() const;

 private:
  uint64_t consumed_samples_{0};
  uint64_t consumed_frames_{0};
  bool paused_{false};
  time_point first_advance_time_{};
  time_point last_advance_time_{};
  time_point pause_started_time_{};
  duration paused_duration_{duration::zero()};
};

}  // namespace rex::audio
