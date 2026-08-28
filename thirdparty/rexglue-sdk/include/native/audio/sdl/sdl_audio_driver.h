// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <stack>
#include <vector>

#include <SDL3/SDL_audio.h>

#include <native/audio/audio_driver.h>

namespace rex::audio {
class AudioRuntime;
}

namespace rex::audio::sdl {

class SdlAudioDriver : public AudioDriver {
 public:
  SdlAudioDriver(memory::Memory* memory, AudioRuntime* runtime, size_t client_index);
  ~SdlAudioDriver() override;

  bool Initialize() override;
  void Shutdown() override;
  void SubmitFrame(uint32_t frame_ptr) override;
  void SubmitSilenceFrame() override;
  AudioDriverTelemetry GetTelemetry() const override;
  const char* backend_name() const override { return "sdl"; }
  uint32_t queue_low_water_frames() const override;
  uint32_t queue_target_frames() const override;

 private:
  // Slack above the device period, absorbing worker wake jitter. The POSIX
  // multi-handle wait is a 1ms poll rather than a real blocking wait, so the
  // refill can lag a consumed frame by more than a scheduler tick.
  static constexpr uint32_t kQueueHeadroomFrames = 2;

  static void SDLCALL StreamCallback(void* userdata, SDL_AudioStream* stream,
                                     int additional_amount, int total_amount);
  void FillStream(SDL_AudioStream* stream, int bytes_needed);

  AudioRuntime* runtime_{nullptr};
  size_t client_index_{0};
  SDL_AudioStream* stream_{nullptr};

  float* AcquireFrameBufferLocked();

  // SDL's device period, queried from the device rather than assumed. Defaults
  // to SDL's own fallback for 48kHz so a failed query still sizes the queue
  // sanely. Atomic because queue_target_frames() is called from the audio
  // worker while Initialize() runs on another thread.
  std::atomic<uint32_t> device_buffer_frames_{1024};

  std::mutex frames_mutex_{};
  std::queue<float*> frames_queued_{};
  std::stack<float*> frames_unused_{};
  // Backing store for the whole pool, so the realtime callback never contends
  // with a thread sitting inside the allocator.
  std::vector<float> frame_pool_storage_{};
  std::array<float, kRenderDriverTicSamplesPerFrame * 2> pending_output_frame_{};
  size_t pending_output_float_count_{0};
  size_t pending_output_float_offset_{0};

  std::atomic<bool> shutting_down_{false};
  std::atomic<uint32_t> submitted_frames_{0};
  std::atomic<uint32_t> consumed_frames_{0};
  std::atomic<uint32_t> underrun_count_{0};
  std::atomic<uint32_t> silence_injections_{0};
  std::atomic<uint32_t> queued_depth_{0};
  std::atomic<uint32_t> peak_queued_depth_{0};
  std::atomic<uint32_t> dropped_frames_{0};
};

}  // namespace rex::audio::sdl
