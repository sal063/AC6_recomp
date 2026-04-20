// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <native/audio/audio_client.h>
#include <native/audio/audio_trace.h>
#include <rex/memory.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xthread.h>
#include <native/thread.h>

namespace rex::stream {
class ByteStream;
}

namespace rex::system {
class KernelState;
}

namespace rex::audio {

struct AudioClientSnapshot {
  bool in_use{false};
  uint32_t callback{0};
  uint32_t callback_arg{0};
  uint32_t render_driver_tic{0};
  AudioDriverTelemetry telemetry{};
};

struct AudioTelemetrySnapshot {
  uint32_t active_clients{0};
  uint32_t queued_frames{0};
  uint32_t peak_queued_frames{0};
  uint32_t dropped_frames{0};
  uint32_t underruns{0};
  uint32_t silence_injections{0};
  uint64_t trace_event_count{0};
  std::string backend_name;
  std::array<AudioClientSnapshot, kMaximumAudioClientCount> clients{};
};

struct AudioClientTimingSnapshot {
  uint64_t consumed_samples{0};
  uint64_t consumed_frames{0};
  uint64_t queued_played_frames{0};
  uint64_t submitted_tic{0};
  uint64_t startup_cap_tic{0};
  uint64_t synthetic_startup_tic{0};
  uint64_t callback_floor_tic{0};
  uint64_t host_elapsed_tic{0};
  uint32_t startup_inflight_frames{0};
  uint32_t render_driver_tic{0};
  uint32_t callback_dispatch_count{0};
  uint32_t callback_throttle_count{0};
  uint32_t callback_empty_count{0};
  uint32_t last_callback_produced_frames{0};
  uint32_t last_callback_duration_us{0};
};

class AudioRuntime {
 public:
  AudioRuntime(memory::Memory* memory, runtime::FunctionDispatcher* function_dispatcher);
  ~AudioRuntime();

  X_STATUS Setup(system::KernelState* kernel_state);
  void Shutdown();

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg, size_t* out_index);
  bool UnregisterClient(size_t index);
  bool SubmitFrame(size_t index, uint32_t samples_ptr);
  bool SubmitSilenceFrame(size_t index);
  bool ConsumeNextFrameForClient(size_t index, AudioFrame* out_frame);
  void ReportSamplesConsumedForClient(size_t index, uint32_t sample_count);
  bool ShouldRequestCallbackForClient(size_t index) const;

  AudioDriverTelemetry GetClientTelemetry(size_t index) const;
  uint32_t GetClientRenderDriverTic(size_t index) const;
  AudioClientTimingSnapshot GetClientTimingSnapshot(size_t index) const;
  AudioTelemetrySnapshot GetTelemetrySnapshot() const;

  bool Save(stream::ByteStream* stream);
  bool Restore(stream::ByteStream* stream);

  void Pause();
  void Resume();
  bool is_paused() const;

  size_t ConsumeQueuedFramesForClient(size_t index, size_t max_frames);
  size_t ConsumeAllAvailableFrames();

  AudioTraceBuffer& trace_buffer() { return trace_buffer_; }
  const AudioTraceBuffer& trace_buffer() const { return trace_buffer_; }
  std::string backend_name() const;

  /// Wake the audio worker thread to re-evaluate callback demand.
  /// Called by the host backend when it consumes frames or detects underruns.
  void WakeWorker();

 private:
  void WorkerThreadMain();
  uint64_t NextTickLocked();

  mutable std::mutex mutex_;
  memory::Memory* memory_{nullptr};
  runtime::FunctionDispatcher* function_dispatcher_{nullptr};
  system::KernelState* kernel_state_{nullptr};
  std::array<AudioClientState, kMaximumAudioClientCount> clients_{};
  AudioTraceBuffer trace_buffer_;
  bool paused_{false};
  std::atomic<bool> worker_running_{false};
  std::unique_ptr<rex::thread::Thread> worker_thread_;
  system::object_ref<system::XThread> worker_thread_context_;
  std::unique_ptr<rex::thread::Event> worker_wake_event_;
  std::unique_ptr<rex::thread::Event> shutdown_event_;
  uint64_t tick_counter_{0};
  uint32_t peak_queued_frames_{0};
  uint64_t worker_iteration_count_{0};
  std::string backend_name_{"none"};
};

}  // namespace rex::audio
