/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <limits>

#include <rex/audio/audio_client.h>
#include <rex/audio/audio_clock.h>

namespace rex::audio {

void AudioClock::Reset() {
  consumed_samples_ = 0;
  consumed_frames_ = 0;
  paused_ = false;
  first_advance_time_ = {};
  last_advance_time_ = {};
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

  const double host_elapsed_ms =
      std::chrono::duration<double, std::milli>(last_advance_time_ - first_advance_time_).count();
  const double audio_elapsed_ms =
      (static_cast<double>(consumed_samples_) / static_cast<double>(kAudioFrameSampleRate)) * 1000.0;

  return host_elapsed_ms - audio_elapsed_ms;
}

}  // namespace rex::audio

