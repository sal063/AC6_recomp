/**
 ******************************************************************************
 * ReXGlue native SDL audio driver                                             *
 ******************************************************************************
 */

#include <native/audio/sdl/sdl_audio_driver.h>

#include <algorithm>
#include <cstring>

#include <SDL3/SDL_init.h>

#include <native/audio/audio_runtime.h>
#include <native/audio/conversion.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DECLARE(bool, audio_mute);
REXCVAR_DECLARE(bool, audio_trace_render_driver_verbose);

namespace rex::audio::sdl {

namespace {

constexpr int kOutputChannels = 2;
constexpr int kOutputFrameBytes =
    static_cast<int>(kRenderDriverTicSamplesPerFrame * kOutputChannels * sizeof(float));

}  // namespace

SdlAudioDriver::SdlAudioDriver(memory::Memory* memory, AudioRuntime* runtime,
                               const size_t client_index)
    : AudioDriver(memory), runtime_(runtime), client_index_(client_index) {}

SdlAudioDriver::~SdlAudioDriver() {
  Shutdown();
}

bool SdlAudioDriver::Initialize() {
  shutting_down_.store(false, std::memory_order_release);

  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXAPU_ERROR("SdlAudioDriver init failed for client {}: SDL_InitSubSystem: {}",
                 client_index_, SDL_GetError());
    return false;
  }

  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = kOutputChannels;
  spec.freq = kAudioFrameSampleRate;

  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      &SdlAudioDriver::StreamCallback, this);
  if (!stream_) {
    REXAPU_ERROR("SdlAudioDriver init failed for client {}: SDL_OpenAudioDeviceStream: {}",
                 client_index_, SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }

  if (!SDL_ResumeAudioStreamDevice(stream_)) {
    REXAPU_ERROR("SdlAudioDriver init failed for client {}: SDL_ResumeAudioStreamDevice: {}",
                 client_index_, SDL_GetError());
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }

  REXAPU_INFO("SdlAudioDriver initialized: client={} channels={} freq={}", client_index_,
              kOutputChannels, kAudioFrameSampleRate);
  return true;
}

void SdlAudioDriver::Shutdown() {
  const bool was_shutting_down = shutting_down_.exchange(true, std::memory_order_acq_rel);
  if (was_shutting_down) {
    return;
  }

  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }

  std::lock_guard<std::mutex> guard(frames_mutex_);
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

  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SdlAudioDriver::SubmitFrame(const uint32_t frame_ptr) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  if (!input_frame) {
    return;
  }

  float* output_frame = nullptr;
  {
    std::lock_guard<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[kAudioFrameTotalSamples];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  std::memcpy(output_frame, input_frame, sizeof(float) * kAudioFrameTotalSamples);
  {
    std::lock_guard<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }

  const uint32_t submitted = submitted_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t queued_depth = queued_depth_.fetch_add(1, std::memory_order_relaxed) + 1;
  uint32_t previous_peak = peak_queued_depth_.load(std::memory_order_relaxed);
  while (queued_depth > previous_peak &&
         !peak_queued_depth_.compare_exchange_weak(previous_peak, queued_depth,
                                                   std::memory_order_relaxed)) {
  }

  if (REXCVAR_GET(audio_trace_render_driver_verbose) &&
      (submitted <= 24 || (submitted % 60) == 0 || queued_depth <= 1)) {
    REXAPU_DEBUG(
        "SdlAudioDriver::SubmitFrame frame_ptr={:08X} submitted={} consumed={} queued_depth={} peak={} underruns={}",
        frame_ptr, submitted, consumed_frames_.load(std::memory_order_relaxed), queued_depth,
        peak_queued_depth_.load(std::memory_order_relaxed),
        underrun_count_.load(std::memory_order_relaxed));
  }
}

void SdlAudioDriver::SubmitSilenceFrame() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  float* output_frame = nullptr;
  {
    std::lock_guard<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[kAudioFrameTotalSamples];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  std::fill_n(output_frame, kAudioFrameTotalSamples, 0.0f);
  {
    std::lock_guard<std::mutex> guard(frames_mutex_);
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
        "SdlAudioDriver::SubmitSilenceFrame submitted={} consumed={} queued_depth={} peak={} underruns={} silence_injections={}",
        submitted, consumed_frames_.load(std::memory_order_relaxed), queued_depth,
        peak_queued_depth_.load(std::memory_order_relaxed),
        underrun_count_.load(std::memory_order_relaxed),
        silence_injections_.load(std::memory_order_relaxed));
  }
}

AudioDriverTelemetry SdlAudioDriver::GetTelemetry() const {
  return AudioDriverTelemetry{
      submitted_frames_.load(std::memory_order_relaxed),
      consumed_frames_.load(std::memory_order_relaxed),
      underrun_count_.load(std::memory_order_relaxed),
      silence_injections_.load(std::memory_order_relaxed),
      queued_depth_.load(std::memory_order_relaxed),
      peak_queued_depth_.load(std::memory_order_relaxed),
  };
}

void SDLCALL SdlAudioDriver::StreamCallback(void* userdata, SDL_AudioStream* stream,
                                            const int additional_amount, const int total_amount) {
  (void)total_amount;
  auto* driver = static_cast<SdlAudioDriver*>(userdata);
  if (!driver || driver->shutting_down_.load(std::memory_order_acquire) || additional_amount <= 0) {
    return;
  }
  driver->FillStream(stream, additional_amount);
}

void SdlAudioDriver::FillStream(SDL_AudioStream* stream, int bytes_needed) {
  while (bytes_needed > 0 && !shutting_down_.load(std::memory_order_acquire)) {
    if (pending_output_float_offset_ == pending_output_float_count_) {
      pending_output_float_count_ = 0;
      pending_output_float_offset_ = 0;

      float* buffer = nullptr;
      {
        std::lock_guard<std::mutex> guard(frames_mutex_);
        if (!frames_queued_.empty()) {
          buffer = frames_queued_.front();
          frames_queued_.pop();
          queued_depth_.fetch_sub(1, std::memory_order_relaxed);
        }
      }

      if (buffer) {
        if (!REXCVAR_GET(audio_mute)) {
          conversion::render_driver_6_BE_to_interleaved_2_LE(
              pending_output_frame_.data(), buffer, kRenderDriverTicSamplesPerFrame);
        } else {
          std::memset(pending_output_frame_.data(), 0, sizeof(float) * pending_output_frame_.size());
        }
        pending_output_float_count_ = pending_output_frame_.size();
        {
          std::lock_guard<std::mutex> guard(frames_mutex_);
          frames_unused_.push(buffer);
        }
      }
    }

    if (pending_output_float_count_ == 0) {
      std::array<float, kRenderDriverTicSamplesPerFrame * kOutputChannels> silence{};
      if (!SDL_PutAudioStreamData(stream, silence.data(), kOutputFrameBytes)) {
        return;
      }
      ++underrun_count_;
      ++silence_injections_;
      bytes_needed -= std::min(bytes_needed, kOutputFrameBytes);
      
      consumed_frames_.fetch_add(1, std::memory_order_relaxed);
      if (runtime_) {
        runtime_->ReportSamplesConsumedForClient(client_index_, kRenderDriverTicSamplesPerFrame);
        runtime_->WakeWorker();
      }
      continue;
    }

    const size_t pending_bytes =
        (pending_output_float_count_ - pending_output_float_offset_) * sizeof(float);
    const int write_bytes = static_cast<int>(
        std::min<size_t>(pending_bytes, static_cast<size_t>(std::max(bytes_needed, 0))));
    if (write_bytes <= 0) {
      break;
    }

    const auto* write_ptr = reinterpret_cast<const uint8_t*>(pending_output_frame_.data()) +
                            (pending_output_float_offset_ * sizeof(float));
    if (!SDL_PutAudioStreamData(stream, write_ptr, write_bytes)) {
      return;
    }

    pending_output_float_offset_ += static_cast<size_t>(write_bytes) / sizeof(float);
    bytes_needed -= write_bytes;

    if (pending_output_float_offset_ == pending_output_float_count_) {
      pending_output_float_count_ = 0;
      pending_output_float_offset_ = 0;
      consumed_frames_.fetch_add(1, std::memory_order_relaxed);
      if (runtime_) {
        runtime_->ReportSamplesConsumedForClient(client_index_, kRenderDriverTicSamplesPerFrame);
        runtime_->ConsumeQueuedFramesForClient(client_index_, 1);
        runtime_->WakeWorker();
      }
    }
  }
}

}  // namespace rex::audio::sdl
