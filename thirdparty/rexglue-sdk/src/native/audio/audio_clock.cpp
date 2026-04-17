/**
 * ReXGlue native audio runtime
 * Part of the AC6 Recompilation project
 */

#include <limits>

#include <native/audio/audio_client.h>
#include <native/audio/audio_clock.h>

namespace rex::audio {

void AudioClock::Reset() {
  consumed_samples_ = 0;
  consumed_frames_ = 0;
  paused_ = false;
  first_advance_time_ = {};
  last_advance_time_ = {};
  pause_started_time_ = {};
  paused_duration_ = duration::zero();
}

void AudioClock::AdvanceFrames(const uint32_t frame_count) {
  AdvanceSamples(static_cast<uint64_t>(frame_count) * kRenderDriverTicSamplesPerFrame);
}

void AudioClock::AdvanceSamples(const uint64_t sample_count) {
  consumed_samples_ += sample_count;
  consumed_frames_ = consumed_samples_ / kRenderDriverTicSamplesPerFrame;

  const auto now = clock_type::now();
  if (first_advance_time_.time_since_epoch().count() == 0) {
    first_advance_time_ = now;
  }
  last_advance_time_ = now;
}

void AudioClock::SetPaused(const bool paused) {
  if (paused_ == paused) {
    return;
  }

  const auto now = clock_type::now();
  if (paused) {
    pause_started_time_ = now;
  } else if (pause_started_time_.time_since_epoch().count() != 0 && now > pause_started_time_) {
    paused_duration_ += now - pause_started_time_;
    pause_started_time_ = {};
  } else {
    pause_started_time_ = {};
  }
  paused_ = paused;
}

uint32_t AudioClock::render_driver_tic() const {
  const auto tic = consumed_samples_;
  return tic > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                    : static_cast<uint32_t>(tic);
}

double AudioClock::drift_ms() const {
  if (first_advance_time_.time_since_epoch().count() == 0 ||
      last_advance_time_.time_since_epoch().count() == 0 ||
      consumed_samples_ == 0) {
    return 0.0;
  }

  const auto host_end_time =
      paused_ && pause_started_time_.time_since_epoch().count() != 0 ? pause_started_time_
                                                                      : last_advance_time_;
  if (host_end_time <= first_advance_time_) {
    return 0.0;
  }

  const auto active_host_elapsed =
      (host_end_time - first_advance_time_) > paused_duration_
          ? (host_end_time - first_advance_time_) - paused_duration_
          : duration::zero();
  const double host_elapsed_ms =
      std::chrono::duration<double, std::milli>(active_host_elapsed).count();
  const double audio_elapsed_ms =
      (static_cast<double>(consumed_samples_) / static_cast<double>(kAudioFrameSampleRate)) * 1000.0;

  return host_elapsed_ms - audio_elapsed_ms;
}

}  // namespace rex::audio
