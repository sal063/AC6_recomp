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
#include <string>

#include <rex/assert.h>
#include <rex/audio/conversion.h>
#include <rex/audio/flags.h>
#include <rex/audio/audio_runtime.h>
#include <rex/audio/sdl/sdl_audio_driver.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/logging.h>

#include <SDL3/SDL.h>

REXCVAR_DECLARE(bool, audio_mute);
REXCVAR_DECLARE(bool, audio_trace_telemetry);
REXCVAR_DECLARE(bool, audio_trace_render_driver_verbose);
REXCVAR_DECLARE(bool, ac6_audio_deep_trace);
REXCVAR_DEFINE_INT32(audio_sdl_device_sample_frames, 256, "Audio",
                     "Requested SDL device buffer size in sample frames for low-latency playback");

namespace rex::audio::sdl {

using Clock = std::chrono::steady_clock;

namespace {

uint32_t RequiredQueueFramesForDevice(const uint32_t device_sample_frames) {
  const uint32_t device_frames = std::max(device_sample_frames, 1u);
  return std::max(2u, (device_frames + kRenderDriverTicSamplesPerFrame - 1) /
                           kRenderDriverTicSamplesPerFrame + 1);
}

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

}  // namespace

SDLAudioDriver::SDLAudioDriver(memory::Memory* memory, AudioRuntime* runtime,
                               size_t client_index)
    : AudioDriver(memory),
      runtime_(runtime),
      client_index_(client_index) {}

SDLAudioDriver::~SDLAudioDriver() = default;

bool SDLAudioDriver::Initialize() {
  SDL_SetHintWithPriority(SDL_HINT_TIMER_RESOLUTION, "0", SDL_HINT_OVERRIDE);
  SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
  const int32_t requested_sample_frames =
      std::max(REXCVAR_GET(audio_sdl_device_sample_frames), 1);
  const std::string requested_sample_frames_string =
      std::to_string(requested_sample_frames);
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,
              requested_sample_frames_string.c_str());
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "rexglue");

  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXAPU_ERROR("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: {}", SDL_GetError());
    return false;
  }
  sdl_initialized_ = true;

  SDL_AudioSpec desired_spec = {};
  SDL_AudioSpec obtained_spec;
  int obtained_sample_frames = 0;
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
  SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sdl_stream_), &obtained_spec,
                           &obtained_sample_frames);
  sdl_device_sample_frames_ = static_cast<uint32_t>(std::max(obtained_sample_frames, 1));
  if (obtained_spec.channels == 2) {
    SDL_DestroyAudioStream(sdl_stream_);
    desired_spec.channels = 2;
    sdl_device_channels_ = 2;
    sdl_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec,
                                            SDLCallback, this);
    if (!sdl_stream_) {
      REXAPU_ERROR("SDL_OpenAudioDevice() stereo reopen failed: {}", SDL_GetError());
      return false;
    }
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sdl_stream_), &obtained_spec,
                             &obtained_sample_frames);
    sdl_device_sample_frames_ = static_cast<uint32_t>(std::max(obtained_sample_frames, 1));
  }
  SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(sdl_stream_));
  REXAPU_INFO("SDLAudioDriver initialized: client={} freq={} channels={} device_channels={} "
              "format={:#x} device_id={} sample_frames_requested={} sample_frames_obtained={}",
              client_index_, frame_frequency_, frame_channels_, sdl_device_channels_,
              static_cast<uint32_t>(obtained_spec.format),
              SDL_GetAudioStreamDevice(sdl_stream_), requested_sample_frames,
              obtained_sample_frames);
  return true;
}

void SDLAudioDriver::SubmitFrame(uint32_t frame_ptr) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  refill_requested_.store(false, std::memory_order_release);
  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  const auto guest_frame_stats = AnalyzeGuestFrame(input_frame, frame_samples_);
  float* output_frame = nullptr;
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
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }

  const uint32_t submitted = submitted_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t queued_depth = queued_depth_.fetch_add(1, std::memory_order_relaxed) + 1;
  uint32_t previous_peak = peak_queued_depth_.load(std::memory_order_relaxed);
  while (queued_depth > previous_peak &&
         !peak_queued_depth_.compare_exchange_weak(previous_peak, queued_depth,
                                                   std::memory_order_relaxed)) {
  }

  if (((REXCVAR_GET(audio_trace_render_driver_verbose) &&
        (submitted <= 24 || (submitted % 60) == 0 || queued_depth <= 1)) ||
       (IsDeepTraceEnabled() &&
        (submitted <= 200 || guest_frame_stats.zeroish_samples == guest_frame_stats.sample_count)))) {
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
        "SDLAudioDriver::SubmitFrame frame_ptr={:08X} submitted={} consumed={} queued_depth={} "
        "peak={} underruns={} silence_injections={} guest_min={:.6f} guest_max={:.6f} "
        "guest_rms={:.6f} guest_zeroish_pct={:.2f} guest_nonfinite={} deep_trace={}",
        frame_ptr, submitted, consumed_frames_.load(std::memory_order_relaxed), queued_depth,
        peak_queued_depth_.load(std::memory_order_relaxed),
        underrun_count_.load(std::memory_order_relaxed),
        silence_injections_.load(std::memory_order_relaxed), guest_frame_stats.min_sample,
        guest_frame_stats.max_sample, guest_rms, guest_zeroish_pct,
        guest_frame_stats.has_nonfinite, IsDeepTraceEnabled());
  }
}

void SDLAudioDriver::SubmitSilenceFrame() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  refill_requested_.store(false, std::memory_order_release);
  float* output_frame = nullptr;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[frame_samples_];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  std::fill_n(output_frame, frame_samples_, 0.0f);
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }

  const uint32_t submitted = submitted_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t queued_depth = queued_depth_.fetch_add(1, std::memory_order_relaxed) + 1;
  silence_injections_.fetch_add(1, std::memory_order_relaxed);
  uint32_t previous_peak = peak_queued_depth_.load(std::memory_order_relaxed);
  while (queued_depth > previous_peak &&
         !peak_queued_depth_.compare_exchange_weak(previous_peak, queued_depth,
                                                   std::memory_order_relaxed)) {
  }

  if (REXCVAR_GET(audio_trace_render_driver_verbose) &&
      (submitted <= 24 || (submitted % 60) == 0 || queued_depth <= 1)) {
    REXAPU_DEBUG(
        "SDLAudioDriver::SubmitSilenceFrame submitted={} consumed={} queued_depth={} peak={} underruns={} silence_injections={}",
        submitted, consumed_frames_.load(std::memory_order_relaxed), queued_depth,
        peak_queued_depth_.load(std::memory_order_relaxed),
        underrun_count_.load(std::memory_order_relaxed),
        silence_injections_.load(std::memory_order_relaxed));
  }
}

void SDLAudioDriver::Shutdown() {
  if (sdl_stream_) {
    shutting_down_.store(true, std::memory_order_release);
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

uint32_t SDLAudioDriver::queue_low_water_frames() const {
  return std::max(1u, queue_target_frames() - 1);
}

uint32_t SDLAudioDriver::queue_target_frames() const {
  return RequiredQueueFramesForDevice(sdl_device_sample_frames_);
}

void SDLAudioDriver::SDLCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                                 int total_amount) {
  SCOPE_profile_cpu_f("apu");
  if (!userdata || !stream) {
    return;
  }
  const auto driver = static_cast<SDLAudioDriver*>(userdata);
  if (driver->shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  const int len = static_cast<int>(sizeof(float) * channel_samples_ * driver->sdl_device_channels_);
  float* data = SDL_stack_alloc(float, len / static_cast<int>(sizeof(float)));
  const size_t output_frame_float_count =
      channel_samples_ * static_cast<size_t>(driver->sdl_device_channels_);
  OutputChunkStats aggregate_stats;
  const uint32_t callback_count =
      driver->callback_count_.fetch_add(1, std::memory_order_relaxed) + 1;

  while (additional_amount > 0) {
    if (driver->pending_output_float_offset_ == driver->pending_output_float_count_) {
      driver->pending_output_float_count_ = 0;
      driver->pending_output_float_offset_ = 0;

      if (driver->shutting_down_.load(std::memory_order_acquire)) {
        break;
      }

      float* buffer = nullptr;
      {
        std::unique_lock<std::mutex> guard(driver->frames_mutex_);
        if (!driver->frames_queued_.empty()) {
          buffer = driver->frames_queued_.front();
          driver->frames_queued_.pop();
          driver->queued_depth_.fetch_sub(1, std::memory_order_relaxed);
        }
      }

      if (buffer) {
        const float* input_frame = buffer;
        if (!REXCVAR_GET(audio_mute)) {
          switch (driver->sdl_device_channels_) {
            case 2:
              conversion::sequential_6_BE_to_interleaved_2_LE(
                  driver->pending_output_frame_.data(), input_frame, channel_samples_);
              break;
            case 6:
              conversion::sequential_6_BE_to_interleaved_6_LE(
                  driver->pending_output_frame_.data(), input_frame, channel_samples_);
              break;
            default:
              assert_unhandled_case(driver->sdl_device_channels_);
              break;
          }
        } else {
          std::memset(driver->pending_output_frame_.data(), 0,
                      output_frame_float_count * sizeof(float));
        }
        driver->pending_output_float_count_ = output_frame_float_count;
        {
          std::unique_lock<std::mutex> guard(driver->frames_mutex_);
          driver->frames_unused_.push(buffer);
        }

        if (REXCVAR_GET(audio_trace_render_driver_verbose) &&
            (callback_count <= 24 || (callback_count % 60) == 0)) {
          const auto output_stats = AnalyzeOutputChunk(driver->pending_output_frame_.data(),
                                                       output_frame_float_count);
          const double output_rms =
              output_stats.sample_count == 0
                  ? 0.0
                  : std::sqrt(output_stats.sum_squares /
                              static_cast<double>(output_stats.sample_count));
          REXAPU_DEBUG(
              "SDLAudioDriver callback client={} callback_count={} samples={} output_min={:.6f} "
              "output_max={:.6f} output_rms={:.6f} output_nonfinite={}",
              driver->client_index_, callback_count, 0u,
              output_stats.min_sample, output_stats.max_sample, output_rms,
              output_stats.has_nonfinite);
        }
      }
    }

    if (driver->pending_output_float_count_ == 0) {
      const int chunk_bytes = std::min(additional_amount, len);
      driver->underrun_count_.fetch_add(1, std::memory_order_relaxed);
      driver->silence_injections_.fetch_add(1, std::memory_order_relaxed);
      std::memset(data, 0, chunk_bytes);
      MergeOutputChunkStats(
          aggregate_stats,
          AnalyzeOutputChunk(data, chunk_bytes / static_cast<int>(sizeof(float))));
      if (!SDL_PutAudioStreamData(stream, data, chunk_bytes)) {
        break;
      }
      if (driver->runtime_) {
        driver->runtime_->WakeWorker();
      }
      additional_amount -= chunk_bytes;
      continue;
    }

    const size_t pending_float_count =
        driver->pending_output_float_count_ - driver->pending_output_float_offset_;
    const int chunk_bytes =
        std::min(additional_amount, static_cast<int>(pending_float_count * sizeof(float)));
    const size_t chunk_float_count =
        static_cast<size_t>(chunk_bytes / static_cast<int>(sizeof(float)));
    const float* chunk_ptr =
        driver->pending_output_frame_.data() + driver->pending_output_float_offset_;

    MergeOutputChunkStats(aggregate_stats, AnalyzeOutputChunk(chunk_ptr, chunk_float_count));
    if (!SDL_PutAudioStreamData(stream, chunk_ptr, chunk_bytes)) {
      break;
    }

    driver->pending_output_float_offset_ += chunk_float_count;
    if (driver->runtime_) {
      driver->runtime_->ReportSamplesConsumedForClient(
          driver->client_index_,
          static_cast<uint32_t>(chunk_float_count / driver->sdl_device_channels_));
    }
    if (driver->pending_output_float_offset_ == driver->pending_output_float_count_) {
      driver->pending_output_float_count_ = 0;
      driver->pending_output_float_offset_ = 0;
      driver->consumed_frames_.fetch_add(1, std::memory_order_relaxed);
      if (driver->runtime_) {
        driver->runtime_->ConsumeQueuedFramesForClient(driver->client_index_, 1);
        driver->runtime_->WakeWorker();
      }
    }
    additional_amount -= chunk_bytes;
  }

  SDL_stack_free(data);
}

}  // namespace rex::audio::sdl
