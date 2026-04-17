/**
 ******************************************************************************
 * ReXGlue native WASAPI audio driver                                          *
 ******************************************************************************
 */

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <stack>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <rex/audio/audio_driver.h>

namespace rex::audio {
class AudioRuntime;
}

namespace rex::audio::wasapi {

class WasapiAudioDriver : public AudioDriver {
 public:
  WasapiAudioDriver(memory::Memory* memory, AudioRuntime* runtime, size_t client_index);
  ~WasapiAudioDriver() override;

  bool Initialize() override;
  void Shutdown() override;
  void SubmitFrame(uint32_t frame_ptr) override;
  void SubmitSilenceFrame() override;
  AudioDriverTelemetry GetTelemetry() const override;
  const char* backend_name() const override { return "wasapi"; }
  uint32_t queue_low_water_frames() const override;
  uint32_t queue_target_frames() const override;

 private:
  void RenderThreadMain();
  void SignalInitResult(bool success, std::string error_message = {});

  AudioRuntime* runtime_{nullptr};
  size_t client_index_{0};

  std::thread render_thread_;
  std::mutex init_mutex_;
  std::condition_variable init_cv_;
  bool init_done_{false};
  bool init_success_{false};
  std::string init_error_;

  HANDLE render_event_{nullptr};
  uint32_t device_buffer_frames_{256};
  std::atomic<bool> shutting_down_{false};
  std::atomic<uint32_t> submitted_frames_{0};
  std::atomic<uint32_t> consumed_frames_{0};
  std::atomic<uint32_t> underrun_count_{0};
  std::atomic<uint32_t> silence_injections_{0};
  std::atomic<uint32_t> queued_depth_{0};
  std::atomic<uint32_t> peak_queued_depth_{0};

  std::queue<float*> frames_queued_{};
  std::stack<float*> frames_unused_{};
  std::mutex frames_mutex_{};
  std::array<float, 256 * 2> pending_output_frame_{};
  size_t pending_output_float_count_{0};
  size_t pending_output_float_offset_{0};
};

}  // namespace rex::audio::wasapi
