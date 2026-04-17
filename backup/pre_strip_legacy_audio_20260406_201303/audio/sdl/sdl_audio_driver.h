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

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <queue>
#include <stack>

#include <rex/audio/audio_driver.h>
#include <rex/thread.h>

#include <SDL3/SDL.h>

namespace rex::audio {
class AudioRuntime;
}

namespace rex::audio::sdl {

class SDLAudioDriver : public AudioDriver {
 public:
  SDLAudioDriver(memory::Memory* memory, AudioRuntime* runtime, size_t client_index);
  ~SDLAudioDriver() override;

  bool Initialize() override;
  void SubmitFrame(uint32_t frame_ptr) override;
  void SubmitSilenceFrame() override;
  AudioDriverTelemetry GetTelemetry() const override;
  void Shutdown() override;
  const char* backend_name() const override { return "sdl"; }
  uint32_t queue_low_water_frames() const override;
  uint32_t queue_target_frames() const override;

 protected:
  static void SDLCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                          int total_amount);

  AudioRuntime* runtime_ = nullptr;
  size_t client_index_ = 0;

  SDL_AudioStream* sdl_stream_ = nullptr;
  bool sdl_initialized_ = false;
  uint8_t sdl_device_channels_ = 0;
  uint32_t sdl_device_sample_frames_ = kRenderDriverTicSamplesPerFrame;
  std::atomic<bool> shutting_down_ = false;

  static const uint32_t frame_frequency_ = 48000;
  static const uint32_t frame_channels_ = 6;
  static const uint32_t channel_samples_ = 256;
  static const uint32_t frame_samples_ = frame_channels_ * channel_samples_;
  static const uint32_t frame_size_ = sizeof(float) * frame_samples_;
  std::atomic<uint32_t> callback_count_ = 0;
  std::atomic<bool> refill_requested_ = false;
  std::atomic<uint32_t> submitted_frames_ = 0;
  std::atomic<uint32_t> consumed_frames_ = 0;
  std::atomic<uint32_t> underrun_count_ = 0;
  std::atomic<uint32_t> silence_injections_ = 0;
  std::atomic<uint32_t> queued_depth_ = 0;
  std::atomic<uint32_t> peak_queued_depth_ = 0;
  std::queue<float*> frames_queued_ = {};
  std::stack<float*> frames_unused_ = {};
  std::mutex frames_mutex_ = {};
  std::array<float, frame_samples_> pending_output_frame_ = {};
  size_t pending_output_float_count_ = 0;
  size_t pending_output_float_offset_ = 0;
};

}  // namespace rex::audio::sdl
