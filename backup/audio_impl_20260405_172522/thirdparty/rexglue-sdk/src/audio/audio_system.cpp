/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <chrono>

#include <rex/assert.h>
#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>
#include <rex/audio/flags.h>
#include <rex/audio/xma/decoder.h>
#include <rex/dbg.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory/ring_buffer.h>
#include <rex/stream.h>
#include <rex/string/buffer.h>
#include <rex/system/thread_state.h>
#include <rex/thread.h>
#include <rex/cvar.h>

REXCVAR_DECLARE(bool, audio_trace_telemetry);
REXCVAR_DECLARE(bool, ac6_audio_deep_trace);
REXCVAR_DEFINE_INT32(
    audio_maxqframes, 8, "Audio",
    "Max buffered audio frames (range 4-64). Lower reduces latency but may cause stuttering.");
REXCVAR_DEFINE_BOOL(audio_trace_worker_cadence, false, "Audio",
                    "Trace render-driver callback cadence, callback duration, and worker wakeups");
REXCVAR_DEFINE_BOOL(audio_trace_render_driver_verbose, false, "Audio",
                    "Trace render-driver client lifecycle and queue state on every important "
                    "transition");

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// For ease of implementation, most audio related processing is handled in
// AudioSystem, and the functions here call off to it.
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal AudioSystem handling take it, to prevent duplicate
// implementations. They can be found in xboxkrnl_audio_xma.cc

namespace rex::audio {

using Clock = std::chrono::steady_clock;

namespace {

bool IsDeepTraceEnabled() {
  return REXCVAR_GET(ac6_audio_deep_trace);
}

}

AudioSystem::AudioSystem(runtime::FunctionDispatcher* function_dispatcher)
    : memory_(function_dispatcher->memory()),
      function_dispatcher_(function_dispatcher),
      worker_running_(false) {
  std::memset(clients_, 0, sizeof(clients_));

  queued_frames_ = std::min(
      static_cast<uint32_t>(kMaximumQueuedFrames),
      std::max(static_cast<uint32_t>(REXCVAR_GET(audio_maxqframes)), static_cast<uint32_t>(4)));

  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    client_semaphores_[i] = rex::thread::Semaphore::Create(0, queued_frames_);
    assert_not_null(client_semaphores_[i]);
    wait_handles_[i] = client_semaphores_[i].get();
  }
  shutdown_event_ = rex::thread::Event::CreateAutoResetEvent(false);
  assert_not_null(shutdown_event_);
  wait_handles_[kMaximumClientCount] = shutdown_event_.get();

  xma_decoder_ = std::make_unique<rex::audio::XmaDecoder>(function_dispatcher_);

  resume_event_ = rex::thread::Event::CreateAutoResetEvent(false);
  assert_not_null(resume_event_);
}

AudioSystem::~AudioSystem() {
  if (xma_decoder_) {
    xma_decoder_->Shutdown();
  }
}

X_STATUS AudioSystem::Setup(system::KernelState* kernel_state) {
  X_STATUS result = xma_decoder_->Setup(kernel_state);
  if (result) {
    return result;
  }

  worker_running_ = true;
  worker_thread_ = system::object_ref<system::XHostThread>(
      new system::XHostThread(kernel_state, 128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));

  worker_thread_->set_name("Audio Worker");
  worker_thread_->Create();

  return X_STATUS_SUCCESS;
}

void AudioSystem::WorkerThreadMain() {
  // Initialize driver and ringbuffer.
  Initialize();

  // Main run loop.
  uint32_t diag_pump_count = 0;
  std::array<Clock::time_point, kMaximumClientCount> last_callback_times{};
  std::array<uint64_t, kMaximumClientCount> callback_counts{};
  uint64_t timeout_count = 0;
  while (worker_running_) {
    // These handles signify the number of submitted samples. Once we reach
    // 64 samples, we wait until our audio backend releases a semaphore
    // (signaling a sample has finished playing)
    auto result = rex::thread::WaitAny(wait_handles_, rex::countof(wait_handles_), true,
                                       std::chrono::milliseconds(500));
    if (result.first == rex::thread::WaitResult::kFailed) {
      REXAPU_WARN("AudioWorker: WaitAny failed");
      continue;
    }

    if (result.first == rex::thread::WaitResult::kTimeout) {
      ++timeout_count;
      if (diag_pump_count < 5) {
        REXAPU_DEBUG("AudioWorker: WaitAny timed out (no semaphore signals)");
      }
      if (REXCVAR_GET(audio_trace_worker_cadence) || IsDeepTraceEnabled()) {
        REXAPU_WARN("AudioWorker: WaitAny timeout count={}", timeout_count);
      }
    }

    if (result.first == thread::WaitResult::kSuccess && result.second == kMaximumClientCount) {
      // Shutdown event signaled.
      if (paused_) {
        pause_fence_.Signal();
        thread::Wait(resume_event_.get(), false);
      }

      continue;
    }

    // Number of clients pumped
    bool pumped = false;
    if (result.first == rex::thread::WaitResult::kSuccess) {
      auto index = result.second;

      auto global_lock = global_critical_region_.Acquire();
      uint32_t client_callback = clients_[index].callback;
      uint32_t client_callback_arg = clients_[index].wrapped_callback_arg;
      AudioDriver* driver = clients_[index].driver;
      global_lock.unlock();

      if (client_callback) {
        const auto before_callback = Clock::now();
        const double since_last_callback_ms =
            last_callback_times[index].time_since_epoch().count() == 0
                ? -1.0
                : std::chrono::duration<double, std::milli>(
                      before_callback - last_callback_times[index])
                      .count();
        ++callback_counts[index];
        if (REXCVAR_GET(audio_trace_worker_cadence) || IsDeepTraceEnabled()) {
          const auto telemetry = driver ? driver->GetTelemetry() : AudioDriverTelemetry{};
          REXAPU_DEBUG(
              "AudioWorker: callback-dispatch client={} callback={:08X} arg={:08X} count={} "
              "since_last_ms={:.3f} submitted={} consumed={} queued_depth={} underruns={} "
              "silence_injections={}",
              index, client_callback, client_callback_arg, callback_counts[index],
              since_last_callback_ms, telemetry.submitted_frames, telemetry.consumed_frames,
              telemetry.queued_depth, telemetry.underrun_count, telemetry.silence_injections);
        }
        if (diag_pump_count < 10) {
          REXAPU_DEBUG("AudioWorker: dispatching callback {:08X} with arg {:08X} for client {}",
                       client_callback, client_callback_arg, index);
        }
        SCOPE_profile_cpu_i("apu", "rex::audio::AudioSystem->client_callback");
        uint64_t args[] = {client_callback_arg};
        function_dispatcher_->Execute(worker_thread_->thread_state(), client_callback, args,
                                      rex::countof(args));
        if (diag_pump_count < 10) {
          REXAPU_DEBUG("AudioWorker: callback returned for client {}", index);
        }
        if (REXCVAR_GET(audio_trace_worker_cadence) || IsDeepTraceEnabled()) {
          const auto after_callback = Clock::now();
          const auto telemetry = driver ? driver->GetTelemetry() : AudioDriverTelemetry{};
          REXAPU_DEBUG(
              "AudioWorker: callback-return client={} duration_ms={:.3f} submitted={} "
              "consumed={} queued_depth={} peak={} underruns={} silence_injections={}",
              index,
              std::chrono::duration<double, std::milli>(after_callback - before_callback).count(),
              telemetry.submitted_frames, telemetry.consumed_frames, telemetry.queued_depth,
              telemetry.peak_queued_depth, telemetry.underrun_count,
              telemetry.silence_injections);
          last_callback_times[index] = after_callback;
        } else {
          last_callback_times[index] = before_callback;
        }
        diag_pump_count++;
      } else {
        const auto telemetry = driver ? driver->GetTelemetry() : AudioDriverTelemetry{};
        REXAPU_DEBUG(
            "AudioWorker: semaphore signaled for client {} but callback is 0 "
            "submitted={} consumed={} queued_depth={} underruns={}",
            index, telemetry.submitted_frames, telemetry.consumed_frames, telemetry.queued_depth,
            telemetry.underrun_count);
      }

      pumped = true;
    }

    if (!worker_running_) {
      break;
    }

    if (!pumped) {
      SCOPE_profile_cpu_i("apu", "Sleep");
      rex::thread::Sleep(std::chrono::milliseconds(500));
    }
  }
  worker_running_ = false;

  // TODO(benvanik): call module API to kill?
}

int AudioSystem::FindFreeClient() {
  for (size_t i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      return i;
    }
  }

  return -1;
}

void AudioSystem::Initialize() {}

void AudioSystem::Shutdown() {
  if (!worker_running_) {
    return;
  }

  // Shut down XMA decoder first - its worker can stall in FFmpeg
  if (xma_decoder_) {
    xma_decoder_->Shutdown();
  }

  worker_running_ = false;
  shutdown_event_->Set();
  if (worker_thread_) {
    // The worker may be stuck inside a guest callback that is itself blocked
    // on guest objects (e.g. KeWaitForMultipleObjects).
    // Terminate the thread to break the deadlock.
    worker_thread_->Terminate(0);
    worker_thread_.reset();
  }

  // Destroy all active client drivers (closes SDL audio devices, stopping
  // callback threads) before the semaphores they reference are destroyed.
  for (size_t i = 0; i < kMaximumClientCount; i++) {
    if (clients_[i].in_use) {
      DestroyDriver(clients_[i].driver);
      if (clients_[i].wrapped_callback_arg) {
        memory()->SystemHeapFree(clients_[i].wrapped_callback_arg);
      }
      clients_[i] = {nullptr, 0, 0, 0, false};
    }
  }
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg, size_t* out_index) {
  REXAPU_DEBUG("AudioSystem::RegisterClient: callback={:08X} callback_arg={:08X}", callback,
               callback_arg);
  auto global_lock = global_critical_region_.Acquire();

  auto index = FindFreeClient();
  assert_true(index >= 0);
  REXAPU_DEBUG("AudioSystem::RegisterClient: using client index={} queued_frames={}", index,
               queued_frames_);

  auto client_semaphore = client_semaphores_[index].get();
  auto ret = client_semaphore->Release(queued_frames_, nullptr);
  assert_true(ret);

  AudioDriver* driver;
  auto result = CreateDriver(index, client_semaphore, &driver);
  if (XFAILED(result)) {
    return result;
  }
  assert_not_null(driver);

  uint32_t ptr = memory()->SystemHeapAlloc(0x4);
  memory::store_and_swap<uint32_t>(memory()->TranslateVirtual(ptr), callback_arg);

  clients_[index] = {driver, callback, callback_arg, ptr, true};

  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    const auto telemetry = driver->GetTelemetry();
    REXAPU_DEBUG(
        "AudioSystem::RegisterClient created client index={} callback={:08X} callback_arg={:08X} "
        "wrapped_arg={:08X} queued_frames={} submitted={} consumed={} queued_depth={}",
        index, callback, callback_arg, ptr, queued_frames_, telemetry.submitted_frames,
        telemetry.consumed_frames, telemetry.queued_depth);
  }

  if (out_index) {
    *out_index = index;
  }

  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, uint32_t samples_ptr) {
  SCOPE_profile_cpu_f("apu");

  static uint32_t submit_count = 0;
  if (submit_count < 10) {
    REXAPU_DEBUG("AudioSystem::SubmitFrame called: index={} samples_ptr={:08X}", index,
                 samples_ptr);
    submit_count++;
  }

  auto global_lock = global_critical_region_.Acquire();
  assert_true(index < kMaximumClientCount);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    const auto telemetry = clients_[index].driver->GetTelemetry();
    if (telemetry.submitted_frames <= 24 || (telemetry.submitted_frames % 60) == 0 ||
        telemetry.queued_depth <= 1 || telemetry.underrun_count != 0) {
      REXAPU_DEBUG(
          "AudioSystem::SubmitFrame telemetry: index={} samples_ptr={:08X} submitted={} "
          "consumed={} queued_depth={} peak={} underruns={} silence_injections={}",
          index, samples_ptr, telemetry.submitted_frames, telemetry.consumed_frames,
          telemetry.queued_depth, telemetry.peak_queued_depth, telemetry.underrun_count,
          telemetry.silence_injections);
    }
  }
}

AudioDriverTelemetry AudioSystem::GetClientTelemetry(size_t index) {
  auto global_lock = global_critical_region_.Acquire();
  if (index >= kMaximumClientCount || !clients_[index].in_use || !clients_[index].driver) {
    return {};
  }
  return clients_[index].driver->GetTelemetry();
}

uint32_t AudioSystem::GetClientRenderDriverTic(size_t index) {
  const auto telemetry = GetClientTelemetry(index);
  return telemetry.consumed_frames * kRenderDriverTicSamplesPerFrame;
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  auto global_lock = global_critical_region_.Acquire();
  assert_true(index < kMaximumClientCount);
  if ((REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) &&
      clients_[index].driver) {
    const auto telemetry = clients_[index].driver->GetTelemetry();
    REXAPU_DEBUG(
        "AudioSystem::UnregisterClient before-destroy index={} callback={:08X} callback_arg={:08X} "
        "wrapped_arg={:08X} submitted={} consumed={} queued_depth={} peak={} underruns={} "
        "silence_injections={}",
        index, clients_[index].callback, clients_[index].callback_arg,
        clients_[index].wrapped_callback_arg, telemetry.submitted_frames,
        telemetry.consumed_frames, telemetry.queued_depth, telemetry.peak_queued_depth,
        telemetry.underrun_count, telemetry.silence_injections);
  }
  DestroyDriver(clients_[index].driver);
  memory()->SystemHeapFree(clients_[index].wrapped_callback_arg);
  clients_[index] = {nullptr, 0, 0, 0, false};

  // Drain the semaphore of its count.
  auto client_semaphore = client_semaphores_[index].get();
  rex::thread::WaitResult wait_result;
  do {
    wait_result = rex::thread::Wait(client_semaphore, false, std::chrono::milliseconds(0));
  } while (wait_result == rex::thread::WaitResult::kSuccess);
  assert_true(wait_result == rex::thread::WaitResult::kTimeout);
}

bool AudioSystem::Save(stream::ByteStream* stream) {
  stream->Write(kAudioSaveSignature);

  // Count the number of used clients first.
  // Any gaps should be handled gracefully.
  uint32_t used_clients = 0;
  for (size_t i = 0; i < kMaximumClientCount; i++) {
    if (clients_[i].in_use) {
      used_clients++;
    }
  }

  stream->Write(used_clients);
  for (uint32_t i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      continue;
    }

    stream->Write(i);
    stream->Write(client.callback);
    stream->Write(client.callback_arg);
    stream->Write(client.wrapped_callback_arg);
  }

  return true;
}

bool AudioSystem::Restore(stream::ByteStream* stream) {
  if (stream->Read<uint32_t>() != kAudioSaveSignature) {
    REXAPU_ERROR("AudioSystem::Restore - Invalid magic value!");
    return false;
  }

  uint32_t num_clients = stream->Read<uint32_t>();
  for (uint32_t i = 0; i < num_clients; i++) {
    auto id = stream->Read<uint32_t>();
    assert_true(id < kMaximumClientCount);

    auto& client = clients_[id];

    // Reset the semaphore and recreate the driver ourselves.
    if (client.driver) {
      UnregisterClient(id);
    }

    client.callback = stream->Read<uint32_t>();
    client.callback_arg = stream->Read<uint32_t>();
    client.wrapped_callback_arg = stream->Read<uint32_t>();

    client.in_use = true;

    auto client_semaphore = client_semaphores_[id].get();
    auto ret = client_semaphore->Release(queued_frames_, nullptr);
    assert_true(ret);

    AudioDriver* driver = nullptr;
    auto status = CreateDriver(id, client_semaphore, &driver);
    if (XFAILED(status)) {
      REXAPU_ERROR(
          "AudioSystem::Restore - Call to CreateDriver failed with status "
          "{:08X}",
          status);
      return false;
    }

    assert_not_null(driver);
    client.driver = driver;
  }

  return true;
}

void AudioSystem::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  // Kind of a hack, but it works.
  shutdown_event_->Set();
  pause_fence_.Wait();

  xma_decoder_->Pause();
}

void AudioSystem::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  resume_event_->Set();

  xma_decoder_->Resume();
}

}  // namespace rex::audio
