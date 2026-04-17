/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#include <rex/assert.h>
#include <rex/audio/conversion.h>
#include <rex/audio/flags.h>
#include <rex/audio/sdl/sdl_audio_driver.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/logging.h>
#include <SDL3/SDL.h>

REXCVAR_DEFINE_BOOL(audio_mute, false, "Audio", "Mute audio output");
REXCVAR_DEFINE_BOOL(audio_trace_telemetry, false, "Audio",
                    "Trace SDL audio queue depth, underruns, and playback telemetry");
REXCVAR_DECLARE(bool, audio_trace_render_driver_verbose);
REXCVAR_DECLARE(bool, ac6_audio_deep_trace);

namespace rex::audio::sdl {

using Clock = std::chrono::steady_clock;

namespace {

bool IsDeepTraceEnabled() {
  return REXCVAR_GET(ac6_audio_deep_trace);
}

struct OutputChunkStats {
  float min_sample = std::numeric_limits<float>::infinity();
  float max_sample = -std::numeric_limits<float>::infinity();
  double sum_squares = 0.0;
  uint32_t sample_count = 0;
  uint32_t zeroish_samples = 0;
  bool has_nonfinite = false;
};

float ByteSwapFloatWord(uint32_t value) {
  value = rex::byte_swap(value);
  float result = 0.0f;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

OutputChunkStats AnalyzeOutputChunk(const float* data, size_t sample_count) {
  OutputChunkStats stats;
  for (size_t i = 0; i < sample_count; ++i) {
    const float sample = data[i];
    if (!std::isfinite(sample)) {
      stats.has_nonfinite = true;
      continue;
    }
    stats.min_sample = std::min(stats.min_sample, sample);
    stats.max_sample = std::max(stats.max_sample, sample);
    stats.sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
    ++stats.sample_count;
    if (std::fabs(sample) <= 1.0e-6f) {
      ++stats.zeroish_samples;
    }
  }
  if (stats.sample_count == 0) {
    stats.min_sample = 0.0f;
    stats.max_sample = 0.0f;
  }
  return stats;
}

OutputChunkStats AnalyzeGuestFrame(const float* data, size_t sample_count) {
  OutputChunkStats stats;
  const auto* words = reinterpret_cast<const uint32_t*>(data);
  for (size_t i = 0; i < sample_count; ++i) {
    const float sample = ByteSwapFloatWord(words[i]);
    if (!std::isfinite(sample)) {
      stats.has_nonfinite = true;
      continue;
    }
    stats.min_sample = std::min(stats.min_sample, sample);
    stats.max_sample = std::max(stats.max_sample, sample);
    stats.sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
    ++stats.sample_count;
    if (std::fabs(sample) <= 1.0e-6f) {
      ++stats.zeroish_samples;
    }
  }
  if (stats.sample_count == 0) {
    stats.min_sample = 0.0f;
    stats.max_sample = 0.0f;
  }
  return stats;
}

void MergeOutputChunkStats(OutputChunkStats& dst, const OutputChunkStats& src) {
  dst.min_sample = std::min(dst.min_sample, src.min_sample);
  dst.max_sample = std::max(dst.max_sample, src.max_sample);
  dst.sum_squares += src.sum_squares;
  dst.sample_count += src.sample_count;
  dst.zeroish_samples += src.zeroish_samples;
  dst.has_nonfinite = dst.has_nonfinite || src.has_nonfinite;
  if (dst.sample_count == 0) {
    dst.min_sample = 0.0f;
    dst.max_sample = 0.0f;
  }
}

}

SDLAudioDriver::SDLAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {}

SDLAudioDriver::~SDLAudioDriver() {
  assert_true(frames_queued_.empty());
  assert_true(frames_unused_.empty());
  assert_true(pending_output_float_count_ == 0);
  assert_true(pending_output_float_offset_ == 0);
}

bool SDLAudioDriver::Initialize() {
  // Prevent SDL from interfering with timer resolution (causes FPS drops)
  SDL_SetHintWithPriority(SDL_HINT_TIMER_RESOLUTION, "0", SDL_HINT_OVERRIDE);

  // Set audio category for proper OS audio handling
  SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");

  // Set app name for audio device identification
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "rexglue");

  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXAPU_ERROR("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: {}", SDL_GetError());
    return false;
  }
  sdl_initialized_ = true;

  SDL_AudioSpec desired_spec = {};
  SDL_AudioSpec obtained_spec;
  desired_spec.freq = frame_frequency_;
  desired_spec.format = SDL_AUDIO_F32LE;
  desired_spec.channels = frame_channels_;
  sdl_device_channels_ = frame_channels_;
  sdl_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec,
                                          SDLCallback, this);
  if (!sdl_stream_) {
    REXAPU_ERROR("SDL_OpenAudioDevice() failed: {}", SDL_GetError());
    return false;
  }
  SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sdl_stream_), &obtained_spec, NULL);
  if (obtained_spec.channels == 2) {
    SDL_DestroyAudioStream(sdl_stream_);
    desired_spec.channels = 2;
    sdl_device_channels_ = 2;
    sdl_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec,
                                            SDLCallback, this);
  }
  SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(sdl_stream_));

  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    REXAPU_DEBUG(
        "SDLAudioDriver::Initialize stream={:p} requested_freq={} requested_channels={} "
        "obtained_freq={} obtained_channels={} active_device_channels={}",
        static_cast<void*>(sdl_stream_), frame_frequency_, frame_channels_, obtained_spec.freq,
        obtained_spec.channels, sdl_device_channels_);
  }

  return true;
}

void SDLAudioDriver::SubmitFrame(uint32_t frame_ptr) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  const auto guest_frame_stats = AnalyzeGuestFrame(input_frame, frame_samples_);
  float* output_frame;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[frame_samples_];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  std::memcpy(output_frame, input_frame, frame_samples_ * sizeof(float));

  static uint32_t sdl_submit_count = 0;
  if (sdl_submit_count < 10) {
    REXAPU_DEBUG("SDLAudioDriver::SubmitFrame: frame_ptr={:08X} queued_count={}", frame_ptr,
                 frames_queued_.size() + 1);
    sdl_submit_count++;
  }

  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }

  const uint32_t submitted = submitted_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t queued_depth =
      queued_depth_.fetch_add(1, std::memory_order_relaxed) + 1;
  uint32_t previous_peak = peak_queued_depth_.load(std::memory_order_relaxed);
  while (queued_depth > previous_peak &&
         !peak_queued_depth_.compare_exchange_weak(previous_peak, queued_depth,
                                                   std::memory_order_relaxed)) {
  }

  if (REXCVAR_GET(audio_trace_telemetry) &&
      (submitted <= 12 || (submitted % 120) == 0)) {
    REXAPU_DEBUG(
        "SDLAudioDriver::SubmitFrame: frame_ptr={:08X} submitted={} queued_depth={} peak={}",
        frame_ptr, submitted, queued_depth, peak_queued_depth_.load(std::memory_order_relaxed));
  }
  if ((REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) &&
      (submitted <= 24 || (submitted % 60) == 0 || queued_depth <= 1)) {
    const double guest_rms =
        guest_frame_stats.sample_count == 0
            ? 0.0
            : std::sqrt(guest_frame_stats.sum_squares /
                        static_cast<double>(guest_frame_stats.sample_count));
    const double guest_zeroish_pct =
        guest_frame_stats.sample_count == 0
            ? 0.0
            : (static_cast<double>(guest_frame_stats.zeroish_samples) * 100.0) /
                  static_cast<double>(guest_frame_stats.sample_count);
    REXAPU_DEBUG(
        "SDLAudioDriver::SubmitFrame verbose: frame_ptr={:08X} submitted={} consumed={} "
        "queued_depth={} peak={} underruns={} silence_injections={} guest_min={:.6f} "
        "guest_max={:.6f} guest_rms={:.6f} guest_zeroish_pct={:.2f} guest_nonfinite={}",
        frame_ptr, submitted, consumed_frames_.load(std::memory_order_relaxed), queued_depth,
        peak_queued_depth_.load(std::memory_order_relaxed),
        underrun_count_.load(std::memory_order_relaxed),
        silence_injections_.load(std::memory_order_relaxed), guest_frame_stats.min_sample,
        guest_frame_stats.max_sample, guest_rms, guest_zeroish_pct,
        guest_frame_stats.has_nonfinite);
  }
}

void SDLAudioDriver::Shutdown() {
  if (sdl_stream_) {
    if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
      const auto telemetry = GetTelemetry();
      REXAPU_DEBUG(
          "SDLAudioDriver::Shutdown stream={:p} submitted={} consumed={} underruns={} "
          "silence_injections={} queued_depth={} peak={}",
          static_cast<void*>(sdl_stream_), telemetry.submitted_frames, telemetry.consumed_frames,
          telemetry.underrun_count, telemetry.silence_injections, telemetry.queued_depth,
          telemetry.peak_queued_depth);
    }
    shutting_down_.store(true, std::memory_order_release);

    // Disable device processing first, then unregister the callback. SDL
    // blocks until any callback already in flight has returned.
    if (!SDL_PauseAudioStreamDevice(sdl_stream_)) {
      REXAPU_WARN("SDL_PauseAudioStreamDevice failed during shutdown: {}", SDL_GetError());
    }
    if (!SDL_SetAudioStreamGetCallback(sdl_stream_, nullptr, nullptr)) {
      REXAPU_WARN("SDL_SetAudioStreamGetCallback(nullptr) failed during shutdown: {}",
                  SDL_GetError());
    }
    if (SDL_LockAudioStream(sdl_stream_)) {
      if (!SDL_ClearAudioStream(sdl_stream_)) {
        REXAPU_WARN("SDL_ClearAudioStream failed during shutdown: {}", SDL_GetError());
      }
      if (!SDL_UnlockAudioStream(sdl_stream_)) {
        REXAPU_WARN("SDL_UnlockAudioStream failed during shutdown: {}", SDL_GetError());
      }
    } else {
      REXAPU_WARN("SDL_LockAudioStream failed during shutdown: {}", SDL_GetError());
    }

    SDL_DestroyAudioStream(sdl_stream_);
    sdl_stream_ = nullptr;
  }
  if (sdl_initialized_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdl_initialized_ = false;
  }
  std::unique_lock<std::mutex> guard(frames_mutex_);
  while (!frames_unused_.empty()) {
    delete[] frames_unused_.top();
    frames_unused_.pop();
  }
  while (!frames_queued_.empty()) {
    delete[] frames_queued_.front();
    frames_queued_.pop();
  }
  pending_output_float_count_ = 0;
  pending_output_float_offset_ = 0;
}

AudioDriverTelemetry SDLAudioDriver::GetTelemetry() const {
  return AudioDriverTelemetry{
      submitted_frames_.load(std::memory_order_relaxed),
      consumed_frames_.load(std::memory_order_relaxed),
      underrun_count_.load(std::memory_order_relaxed),
      silence_injections_.load(std::memory_order_relaxed),
      queued_depth_.load(std::memory_order_relaxed),
      peak_queued_depth_.load(std::memory_order_relaxed),
  };
}

void SDLAudioDriver::SDLCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                                 int total_amount) {
  SCOPE_profile_cpu_f("apu");
  if (!userdata || !stream) {
    REXAPU_ERROR("SDLAudioDriver::SDLCallback called with nullptr.");
    return;
  }
  const auto driver = static_cast<SDLAudioDriver*>(userdata);
  if (driver->shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  const int len = static_cast<int>(sizeof(float) * channel_samples_ * driver->sdl_device_channels_);
  static Clock::time_point last_callback_time{};
  const auto callback_start = Clock::now();
  const int requested_amount = additional_amount;
  const int stream_queued_before = SDL_GetAudioStreamQueued(stream);
  const double since_last_callback_ms =
      last_callback_time.time_since_epoch().count() == 0
          ? -1.0
          : std::chrono::duration<double, std::milli>(callback_start - last_callback_time).count();
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    REXAPU_DEBUG(
        "SDLCallback begin stream={:p} additional_amount={} total_amount={} len={} "
        "since_last_callback_ms={:.3f} queued_depth={} submitted={} consumed={} underruns={} "
        "silence_injections={} stream_queued_bytes={}",
        static_cast<void*>(stream), additional_amount, total_amount, len, since_last_callback_ms,
        driver->queued_depth_.load(std::memory_order_relaxed),
        driver->submitted_frames_.load(std::memory_order_relaxed),
        driver->consumed_frames_.load(std::memory_order_relaxed),
        driver->underrun_count_.load(std::memory_order_relaxed),
        driver->silence_injections_.load(std::memory_order_relaxed), stream_queued_before);
  }
  float* data = SDL_stack_alloc(float, len / static_cast<int>(sizeof(float)));
  const size_t output_frame_float_count =
      channel_samples_ * static_cast<size_t>(driver->sdl_device_channels_);
  int bytes_written = 0;
  int silence_chunks = 0;
  int audio_chunks = 0;
  int stream_queued_peak = stream_queued_before;
  OutputChunkStats aggregate_stats;
  while (additional_amount > 0) {
    static uint32_t sdl_callback_count = 0;
    if (driver->pending_output_float_offset_ == driver->pending_output_float_count_) {
      driver->pending_output_float_count_ = 0;
      driver->pending_output_float_offset_ = 0;

      float* buffer = nullptr;
      {
        std::unique_lock<std::mutex> guard(driver->frames_mutex_);
        if (!driver->frames_queued_.empty()) {
          buffer = driver->frames_queued_.front();
          driver->frames_queued_.pop();
          driver->queued_depth_.fetch_sub(1, std::memory_order_relaxed);
        }
      }

      if (driver->shutting_down_.load(std::memory_order_acquire)) {
        if (buffer) {
          std::unique_lock<std::mutex> guard(driver->frames_mutex_);
          driver->frames_unused_.push(buffer);
        }
        break;
      }

      if (buffer) {
        if (!REXCVAR_GET(audio_mute)) {
          switch (driver->sdl_device_channels_) {
            case 2:
              conversion::sequential_6_BE_to_interleaved_2_LE(
                  driver->pending_output_frame_.data(), buffer, channel_samples_);
              break;
            case 6:
              conversion::sequential_6_BE_to_interleaved_6_LE(
                  driver->pending_output_frame_.data(), buffer, channel_samples_);
              break;
            default:
              assert_unhandled_case(driver->sdl_device_channels_);
              break;
          }
        } else {
          std::memset(driver->pending_output_frame_.data(), 0,
                      output_frame_float_count * sizeof(float));
        }

        {
          std::unique_lock<std::mutex> guard(driver->frames_mutex_);
          driver->frames_unused_.push(buffer);
        }
        driver->pending_output_float_count_ = output_frame_float_count;
      }
    }

    if (driver->pending_output_float_count_ == 0) {
      const uint32_t underruns =
          driver->underrun_count_.fetch_add(1, std::memory_order_relaxed) + 1;
      const uint32_t silences =
          driver->silence_injections_.fetch_add(1, std::memory_order_relaxed) + 1;
      if (sdl_callback_count < 10) {
        REXAPU_DEBUG("SDLCallback: no frames queued, emitting silence");
        sdl_callback_count++;
      }
      if (REXCVAR_GET(audio_trace_telemetry) || IsDeepTraceEnabled()) {
        REXAPU_WARN(
            "SDLCallback underrun: count={} silence_injections={} queued_depth={} total_amount={} "
            "additional_amount={}",
            underruns, silences, driver->queued_depth_.load(std::memory_order_relaxed),
            total_amount, additional_amount);
      }
      const int chunk_bytes = std::min(additional_amount, len);
      std::memset(data, 0, chunk_bytes);
      MergeOutputChunkStats(
          aggregate_stats,
          AnalyzeOutputChunk(data, chunk_bytes / static_cast<int>(sizeof(float))));
      if (!SDL_PutAudioStreamData(stream, data, chunk_bytes)) {
        REXAPU_ERROR("SDLCallback: SDL_PutAudioStreamData(silence) failed: {}", SDL_GetError());
        break;
      }
      bytes_written += chunk_bytes;
      ++silence_chunks;
      stream_queued_peak = std::max(stream_queued_peak, SDL_GetAudioStreamQueued(stream));
      additional_amount -= chunk_bytes;
      continue;
    }

    const size_t pending_float_count =
        driver->pending_output_float_count_ - driver->pending_output_float_offset_;
    const int chunk_bytes =
        std::min(additional_amount,
                 static_cast<int>(pending_float_count * sizeof(float)));
    const size_t chunk_float_count =
        static_cast<size_t>(chunk_bytes / static_cast<int>(sizeof(float)));
    const float* chunk_ptr =
        driver->pending_output_frame_.data() + driver->pending_output_float_offset_;
    const auto chunk_stats = AnalyzeOutputChunk(chunk_ptr, chunk_float_count);

    if (!SDL_PutAudioStreamData(stream, chunk_ptr, chunk_bytes)) {
      REXAPU_ERROR("SDLCallback: SDL_PutAudioStreamData(audio) failed: {}", SDL_GetError());
      break;
    }
    bytes_written += chunk_bytes;
    ++audio_chunks;
    MergeOutputChunkStats(aggregate_stats, chunk_stats);
    stream_queued_peak = std::max(stream_queued_peak, SDL_GetAudioStreamQueued(stream));
    driver->pending_output_float_offset_ += chunk_float_count;
    if (driver->pending_output_float_offset_ == driver->pending_output_float_count_) {
      driver->pending_output_float_count_ = 0;
      driver->pending_output_float_offset_ = 0;
      const uint32_t consumed =
          driver->consumed_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
      if ((REXCVAR_GET(audio_trace_telemetry) || IsDeepTraceEnabled()) &&
          (consumed <= 12 || (consumed % 120) == 0)) {
        REXAPU_DEBUG(
            "SDLCallback: consumed={} queued_depth={} peak={} underruns={} silence_injections={}",
            consumed, driver->queued_depth_.load(std::memory_order_relaxed),
            driver->peak_queued_depth_.load(std::memory_order_relaxed),
            driver->underrun_count_.load(std::memory_order_relaxed),
            driver->silence_injections_.load(std::memory_order_relaxed));
      }

      auto ret = driver->semaphore_->Release(1, nullptr);
      assert_true(ret);
    }
    additional_amount -= chunk_bytes;
  }
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    const auto callback_end = Clock::now();
    const int stream_queued_after = SDL_GetAudioStreamQueued(stream);
    const int oversupply_bytes = bytes_written - requested_amount;
    const double output_rms =
        aggregate_stats.sample_count == 0
            ? 0.0
            : std::sqrt(aggregate_stats.sum_squares /
                        static_cast<double>(aggregate_stats.sample_count));
    const double zeroish_pct =
        aggregate_stats.sample_count == 0
            ? 0.0
            : (static_cast<double>(aggregate_stats.zeroish_samples) * 100.0) /
                  static_cast<double>(aggregate_stats.sample_count);
    REXAPU_DEBUG(
        "SDLCallback end stream={:p} duration_ms={:.3f} queued_depth={} submitted={} consumed={} "
        "underruns={} silence_injections={} requested_bytes={} written_bytes={} "
        "oversupply_bytes={} stream_queued_before={} stream_queued_after={} "
        "stream_queued_peak={} audio_chunks={} silence_chunks={} output_min={:.6f} "
        "output_max={:.6f} output_rms={:.6f} zeroish_pct={:.2f} nonfinite={}",
        static_cast<void*>(stream),
        std::chrono::duration<double, std::milli>(callback_end - callback_start).count(),
        driver->queued_depth_.load(std::memory_order_relaxed),
        driver->submitted_frames_.load(std::memory_order_relaxed),
        driver->consumed_frames_.load(std::memory_order_relaxed),
        driver->underrun_count_.load(std::memory_order_relaxed),
        driver->silence_injections_.load(std::memory_order_relaxed), requested_amount,
        bytes_written, oversupply_bytes, stream_queued_before, stream_queued_after,
        stream_queued_peak, audio_chunks, silence_chunks, aggregate_stats.min_sample,
        aggregate_stats.max_sample, output_rms, zeroish_pct, aggregate_stats.has_nonfinite);
  }
  last_callback_time = callback_start;
  SDL_stack_free(data);
}

}  // namespace rex::audio::sdl
