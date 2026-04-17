/**
 * ReXGlue native audio runtime
 * Part of the AC6 Recompilation project
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>

#include <native/audio/audio_runtime.h>

#include <native/audio/wasapi/wasapi_audio_driver.h>
#include <rex/cvar.h>
#include <rex/memory.h>
#include <rex/ppc/exceptions.h>
#include <native/stream.h>

REXCVAR_DEFINE_STRING(audio_backend, "wasapi", "Audio", "Audio backend: wasapi")
    .allowed({"wasapi"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(audio_max_queue_depth, 8, "Audio",
                     "Maximum queued render-driver frames per client");
REXCVAR_DEFINE_INT32(audio_callback_low_water_frames, 1, "Audio",
                     "Request a new guest callback when the runtime queue falls to this depth");
REXCVAR_DEFINE_INT32(audio_callback_target_queue_depth, 2, "Audio",
                     "Initial and refill queue target for runtime-owned render-driver frames");
REXCVAR_DEFINE_INT32(
    audio_startup_callback_min_spacing_ms, 4, "Audio",
    "Minimum spacing between render-driver callbacks before the first real playback consumption")
    .range(0, 100);
REXCVAR_DEFINE_INT32(audio_startup_max_callback_lead_frames, 2, "Audio",
                     "Maximum queued/submitted callback lead before first real playback consumption")
    .range(1, 8);

namespace rex::audio {

namespace {

std::unique_ptr<AudioDriver> CreateConfiguredDriver(memory::Memory* memory, AudioRuntime* runtime,
                                                    const size_t client_index) {
  return std::make_unique<wasapi::WasapiAudioDriver>(memory, runtime, client_index);
}

uint32_t QueueDepthLimit() {
  return std::max(REXCVAR_GET(audio_max_queue_depth), 1);
}

uint32_t CallbackLowWaterFrames() {
  const int32_t queue_limit = static_cast<int32_t>(QueueDepthLimit());
  return static_cast<uint32_t>(
      std::clamp(REXCVAR_GET(audio_callback_low_water_frames), 0, queue_limit));
}

uint32_t CallbackTargetQueueDepth() {
  const int32_t queue_limit = static_cast<int32_t>(QueueDepthLimit());
  const int32_t low_water = static_cast<int32_t>(CallbackLowWaterFrames());
  return static_cast<uint32_t>(std::clamp(REXCVAR_GET(audio_callback_target_queue_depth),
                                          std::max(low_water, 1), queue_limit));
}

uint32_t StartupCallbackMinSpacingMs() {
  return static_cast<uint32_t>(
      std::clamp(REXCVAR_GET(audio_startup_callback_min_spacing_ms), 0, 100));
}

uint32_t StartupMaxCallbackLeadFrames() {
  const int32_t queue_limit = static_cast<int32_t>(QueueDepthLimit());
  return static_cast<uint32_t>(std::clamp(REXCVAR_GET(audio_startup_max_callback_lead_frames), 1,
                                          std::max(queue_limit, 1)));
}

uint32_t EffectiveCallbackTargetQueueDepth(const AudioClientState& client) {
  const uint32_t requested_target = CallbackTargetQueueDepth();
  const uint32_t driver_target = client.driver ? client.driver->queue_target_frames() : 1;
  return std::clamp(std::max(requested_target, driver_target), 1u, QueueDepthLimit());
}

uint32_t EffectiveCallbackLowWaterFrames(const AudioClientState& client) {
  const uint32_t target = EffectiveCallbackTargetQueueDepth(client);
  const uint32_t requested_low_water = CallbackLowWaterFrames();
  const uint32_t driver_low_water = client.driver ? client.driver->queue_low_water_frames() : 0;
  return std::clamp(std::max(requested_low_water, driver_low_water), 0u, target - 1);
}

uint64_t ElapsedSamplesSince(const AudioClock::time_point start_time,
                             const AudioClock::time_point end_time) {
  if (start_time.time_since_epoch().count() == 0 ||
      end_time.time_since_epoch().count() == 0 || end_time <= start_time) {
    return 0;
  }

  const auto elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
  return (static_cast<uint64_t>(elapsed_us) * kAudioFrameSampleRate) / 1000000ull;
}

AudioDriverTelemetry MergeDriverTelemetry(const AudioClientState& client) {
  AudioDriverTelemetry merged = client.telemetry;
  if (!client.driver) {
    return merged;
  }

  const auto driver_telemetry = client.driver->GetTelemetry();
  merged.submitted_frames =
      std::max(merged.submitted_frames, driver_telemetry.submitted_frames);
  merged.consumed_frames = std::max(merged.consumed_frames, driver_telemetry.consumed_frames);
  merged.underrun_count = std::max(merged.underrun_count, driver_telemetry.underrun_count);
  merged.silence_injections =
      std::max(merged.silence_injections, driver_telemetry.silence_injections);
  merged.queued_depth = std::max(merged.queued_depth, driver_telemetry.queued_depth);
  merged.peak_queued_depth =
      std::max(merged.peak_queued_depth, driver_telemetry.peak_queued_depth);
  merged.dropped_frames = std::max(merged.dropped_frames, driver_telemetry.dropped_frames);
  merged.malformed_frames =
      std::max(merged.malformed_frames, driver_telemetry.malformed_frames);
  merged.callback_dispatch_count =
      std::max(merged.callback_dispatch_count, driver_telemetry.callback_dispatch_count);
  merged.callback_throttle_count =
      std::max(merged.callback_throttle_count, driver_telemetry.callback_throttle_count);
  return merged;
}

bool StartupConsumptionObserved(const AudioClientState& client) {
  return client.clock.consumed_samples() != 0;
}

uint32_t StartupInflightFrames(const AudioClientState& client) {
  const uint32_t submitted_not_consumed =
      client.telemetry.submitted_frames >= client.telemetry.consumed_frames
          ? client.telemetry.submitted_frames - client.telemetry.consumed_frames
          : 0;
  return std::max<uint32_t>(static_cast<uint32_t>(client.queued_frames.size()),
                            submitted_not_consumed);
}

uint32_t StartupCallbackSpacingRemainingMs(const AudioClientState& client,
                                           const AudioClock::time_point now) {
  const uint32_t min_spacing_ms = StartupCallbackMinSpacingMs();
  if (min_spacing_ms == 0 ||
      client.last_callback_dispatch_time.time_since_epoch().count() == 0 || now <=
          client.last_callback_dispatch_time) {
    return 0;
  }

  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                            client.last_callback_dispatch_time)
          .count();
  if (elapsed_ms >= static_cast<int64_t>(min_spacing_ms)) {
    return 0;
  }
  return static_cast<uint32_t>(min_spacing_ms - elapsed_ms);
}

bool ShouldThrottleStartupCallback(const AudioClientState& client,
                                   const AudioClock::time_point now,
                                   uint32_t* out_inflight_frames,
                                   uint32_t* out_spacing_remaining_ms) {
  if (out_inflight_frames) {
    *out_inflight_frames = 0;
  }
  if (out_spacing_remaining_ms) {
    *out_spacing_remaining_ms = 0;
  }

  if (StartupConsumptionObserved(client)) {
    return false;
  }

  const uint32_t inflight_frames = StartupInflightFrames(client);
  if (out_inflight_frames) {
    *out_inflight_frames = inflight_frames;
  }
  if (inflight_frames >= StartupMaxCallbackLeadFrames()) {
    return true;
  }

  const uint32_t spacing_remaining_ms = StartupCallbackSpacingRemainingMs(client, now);
  if (out_spacing_remaining_ms) {
    *out_spacing_remaining_ms = spacing_remaining_ms;
  }
  return spacing_remaining_ms != 0;
}

uint64_t SubmittedTicSamples(const AudioClientState& client) {
  return static_cast<uint64_t>(client.telemetry.submitted_frames) *
         kRenderDriverTicSamplesPerFrame;
}

uint64_t StartupTicCapSamples(const AudioClientState& client) {
  const uint32_t startup_cap_frames = std::max(
      1u, client.driver ? client.driver->queue_target_frames() : CallbackTargetQueueDepth());
  return static_cast<uint64_t>(startup_cap_frames) * kRenderDriverTicSamplesPerFrame;
}

uint64_t ComputeStartupSyntheticTic(const AudioClientState& client) {
  if (client.telemetry.callback_dispatch_count == 0) {
    return 0;
  }

  const uint64_t submitted_samples = SubmittedTicSamples(client);
  if (submitted_samples != 0) {
    return submitted_samples;
  }

  // When no frames have been submitted yet, grow the synthetic tic
  // proportionally to dispatched callbacks so the game gets a realistic
  // sense of elapsed audio-time during startup instead of being stuck at
  // a single-frame cap that can cause dialogue timing desyncs.
  return static_cast<uint64_t>(client.telemetry.callback_dispatch_count) *
         kRenderDriverTicSamplesPerFrame;
}

uint32_t ComputeRenderDriverTic(const AudioClientState& client) {
  uint64_t tic = client.clock.consumed_samples();

  // Keep startup liveness bounded to actual queued/submitted audio instead of
  // deriving progress from wall clock time alone.
  if (tic == 0) {
    tic = ComputeStartupSyntheticTic(client);
  }

  return tic > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                    : static_cast<uint32_t>(tic);
}

AudioClientTimingSnapshot BuildTimingSnapshot(const AudioClientState& client) {
  AudioClientTimingSnapshot snapshot;
  snapshot.consumed_samples = client.clock.consumed_samples();
  snapshot.consumed_frames = client.clock.consumed_frames();
  snapshot.submitted_tic = SubmittedTicSamples(client);
  snapshot.startup_cap_tic = StartupTicCapSamples(client);
  snapshot.synthetic_startup_tic = ComputeStartupSyntheticTic(client);
  snapshot.callback_dispatch_count = client.telemetry.callback_dispatch_count;
  snapshot.callback_floor_tic =
      static_cast<uint64_t>(client.telemetry.callback_dispatch_count) *
      kRenderDriverTicSamplesPerFrame;
  if (client.first_callback_dispatch_time.time_since_epoch().count() != 0) {
    snapshot.host_elapsed_tic = ElapsedSamplesSince(client.first_callback_dispatch_time,
                                                    AudioClock::clock_type::now());
  }
  snapshot.startup_inflight_frames = StartupInflightFrames(client);
  snapshot.render_driver_tic = ComputeRenderDriverTic(client);
  snapshot.callback_throttle_count = client.telemetry.callback_throttle_count;
  snapshot.callback_empty_count = client.telemetry.callback_empty_count;
  snapshot.last_callback_produced_frames = client.telemetry.last_callback_produced_frames;
  snapshot.last_callback_duration_us = client.telemetry.last_callback_duration_us;
  return snapshot;
}

void ResetClientState(AudioClientState& client, const size_t client_index) {
  client = AudioClientState{};
  client.client_index = client_index;
  client.clock.Reset();
}

}  // namespace

AudioRuntime::AudioRuntime(memory::Memory* memory, runtime::FunctionDispatcher* function_dispatcher)
    : memory_(memory), function_dispatcher_(function_dispatcher) {}

AudioRuntime::~AudioRuntime() {
  Shutdown();
}

X_STATUS AudioRuntime::Setup(system::KernelState* kernel_state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (worker_running_.load(std::memory_order_acquire)) {
    return X_STATUS_SUCCESS;
  }

  kernel_state_ = kernel_state;
  peak_queued_frames_ = 0;
  paused_ = false;
  tick_counter_ = 0;
  worker_iteration_count_ = 0;
  backend_name_ = REXCVAR_GET(audio_backend);
  trace_buffer_.Reset();
  for (size_t i = 0; i < clients_.size(); ++i) {
    ResetClientState(clients_[i], i);
  }

  shutdown_event_ = rex::thread::Event::CreateAutoResetEvent(false);
  worker_wake_event_ = rex::thread::Event::CreateAutoResetEvent(false);

  worker_running_.store(true, std::memory_order_release);
  worker_thread_context_ = system::object_ref<system::XThread>(
      new system::XThread(kernel_state_, 128 * 1024, 0, 0, 0, 0, false));
  const X_STATUS worker_context_status = worker_thread_context_->PrepareHostContext();
  if (XFAILED(worker_context_status)) {
    worker_running_.store(false, std::memory_order_release);
    worker_thread_context_.reset();
    worker_wake_event_.reset();
    shutdown_event_.reset();
    kernel_state_ = nullptr;
    return worker_context_status;
  }
  worker_thread_context_->set_name("Audio Worker");

  rex::thread::Thread::CreationParameters params;
  params.stack_size = 16 * 1024 * 1024;
  auto* worker_thread_context = worker_thread_context_.get();
  worker_thread_ = rex::thread::Thread::Create(params, [this, worker_thread_context]() {
    rex::initialize_seh_thread();
    worker_thread_context->BindToCurrentHostThread();
    kernel_state_->OnThreadExecute(worker_thread_context);
    WorkerThreadMain();
    kernel_state_->OnThreadExit(worker_thread_context);
    worker_thread_context->UnbindFromCurrentHostThread();
  });
  if (!worker_thread_) {
    worker_running_.store(false, std::memory_order_release);
    worker_thread_context_.reset();
    worker_wake_event_.reset();
    shutdown_event_.reset();
    kernel_state_ = nullptr;
    return X_STATUS_NO_MEMORY;
  }
  worker_thread_->set_name("Audio Worker");

  return X_STATUS_SUCCESS;
}

void AudioRuntime::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_running_.load(std::memory_order_acquire)) {
      worker_running_.store(false, std::memory_order_release);
      if (shutdown_event_) {
        shutdown_event_->Set();
      }
      if (worker_wake_event_) {
        worker_wake_event_->Set();
      }
    }
  }

  if (worker_thread_) {
    const auto result =
        rex::thread::Wait(worker_thread_.get(), false, std::chrono::milliseconds(2000));
    if (result == rex::thread::WaitResult::kTimeout) {
      REXAPU_WARN("AudioRuntime: worker thread did not exit within 2s, terminating");
      worker_thread_->Terminate(0);
    }
    worker_thread_.reset();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < clients_.size(); ++i) {
    if (clients_[i].driver) {
      clients_[i].driver->Shutdown();
      delete clients_[i].driver;
      clients_[i].driver = nullptr;
    }
    if (clients_[i].wrapped_callback_arg) {
      memory_->SystemHeapFree(clients_[i].wrapped_callback_arg);
    }
    ResetClientState(clients_[i], i);
  }

  worker_wake_event_.reset();
  shutdown_event_.reset();
  worker_thread_context_.reset();
  paused_ = false;
  kernel_state_ = nullptr;
}

X_STATUS AudioRuntime::RegisterClient(const uint32_t callback, const uint32_t callback_arg,
                                      size_t* out_index) {
  if (!out_index || callback == 0) {
    return X_E_INVALIDARG;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < clients_.size(); ++i) {
    auto& client = clients_[i];
    if (client.in_use) {
      continue;
    }

    auto driver = CreateConfiguredDriver(memory_, this, i);
    if (!driver || !driver->Initialize()) {
      if (driver) {
        driver->Shutdown();
      }
      return X_STATUS_UNSUCCESSFUL;
    }

    const uint32_t wrapped_callback_arg = memory_->SystemHeapAlloc(0x4);
    memory::store_and_swap<uint32_t>(memory_->TranslateVirtual(wrapped_callback_arg), callback_arg);

    client = AudioClientState{};
    client.in_use = true;
    client.client_index = i;
    client.callback = callback;
    client.callback_arg = callback_arg;
    client.wrapped_callback_arg = wrapped_callback_arg;
    client.driver = driver.release();
    backend_name_ = client.driver->backend_name();
    client.clock.Reset();
    client.clock.SetPaused(paused_);
    client.telemetry = {};

    trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kClientRegistered,
                         static_cast<uint32_t>(i), callback, callback_arg);

    *out_index = i;

    // Wake the worker so it can start dispatching callbacks for this client
    if (worker_wake_event_) {
      worker_wake_event_->Set();
    }

    return X_STATUS_SUCCESS;
  }

  return X_STATUS_NO_MEMORY;
}

bool AudioRuntime::UnregisterClient(const size_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return false;
  }

  if (clients_[index].driver) {
    clients_[index].driver->Shutdown();
    delete clients_[index].driver;
  }
  if (clients_[index].wrapped_callback_arg) {
    memory_->SystemHeapFree(clients_[index].wrapped_callback_arg);
  }

  trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kClientUnregistered,
                       static_cast<uint32_t>(index));

  ResetClientState(clients_[index], index);
  return true;
}

bool AudioRuntime::SubmitFrame(const size_t index, const uint32_t samples_ptr) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use || !clients_[index].driver) {
    return false;
  }

  auto& client = clients_[index];
  if (!samples_ptr) {
    ++client.telemetry.malformed_frames;
    trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kMalformedFrame,
                         static_cast<uint32_t>(index));
    return false;
  }

  AudioFrame frame;
  frame.source_client_id = static_cast<uint32_t>(index);
  frame.sequence_number = client.next_sequence_number++;
  frame.guest_submit_ptr = samples_ptr;

  const auto* guest_words = memory_->TranslateVirtual<uint32_t*>(samples_ptr);
  if (!guest_words) {
    ++client.telemetry.malformed_frames;
    trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kMalformedFrame,
                         static_cast<uint32_t>(index), samples_ptr);
    return false;
  }
  std::memcpy(frame.guest_frame_words.data(), guest_words,
              sizeof(frame.guest_frame_words));

  // Keep the runtime queue as bounded shadow state for telemetry/tracing only.
  // The active host driver still owns actual buffering and must not be blocked
  // by runtime metadata overflow.
  if (client.queued_frames.size() >= QueueDepthLimit()) {
    ++client.telemetry.dropped_frames;
    trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kFrameDropped,
                         static_cast<uint32_t>(index), samples_ptr,
                         static_cast<uint32_t>(client.queued_frames.size()));
    client.queued_frames.pop_front();
  }

  client.queued_frames.push_back(frame);
  ++client.telemetry.submitted_frames;
  client.telemetry.queued_depth = static_cast<uint32_t>(client.queued_frames.size());
  client.telemetry.peak_queued_depth =
      std::max(client.telemetry.peak_queued_depth, client.telemetry.queued_depth);
  client.telemetry.last_submit_ticks = static_cast<uint64_t>(NextTickLocked());
  peak_queued_frames_ = std::max(peak_queued_frames_, client.telemetry.queued_depth);

  trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kFrameSubmitted,
                       static_cast<uint32_t>(index), samples_ptr, client.telemetry.queued_depth,
                       static_cast<uint32_t>(frame.sequence_number));
  client.driver->SubmitFrame(samples_ptr);
  return true;
}

bool AudioRuntime::SubmitSilenceFrame(const size_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use || !clients_[index].driver) {
    return false;
  }

  auto& client = clients_[index];
  AudioFrame frame;
  frame.source_client_id = static_cast<uint32_t>(index);
  frame.sequence_number = client.next_sequence_number++;
  frame.guest_submit_ptr = 0;
  frame.is_silence = true;

  if (client.queued_frames.size() >= QueueDepthLimit()) {
    ++client.telemetry.dropped_frames;
    trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kFrameDropped,
                         static_cast<uint32_t>(index), 0,
                         static_cast<uint32_t>(client.queued_frames.size()));
    client.queued_frames.pop_front();
  }

  client.queued_frames.push_back(frame);
  ++client.telemetry.submitted_frames;
  ++client.telemetry.silence_injections;
  client.telemetry.queued_depth = static_cast<uint32_t>(client.queued_frames.size());
  client.telemetry.peak_queued_depth =
      std::max(client.telemetry.peak_queued_depth, client.telemetry.queued_depth);
  client.telemetry.last_submit_ticks = static_cast<uint64_t>(NextTickLocked());
  peak_queued_frames_ = std::max(peak_queued_frames_, client.telemetry.queued_depth);

  trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kFrameSubmitted,
                       static_cast<uint32_t>(index), 0, client.telemetry.queued_depth,
                       static_cast<uint32_t>(frame.sequence_number));
  client.driver->SubmitSilenceFrame();
  return true;
}

bool AudioRuntime::ConsumeNextFrameForClient(const size_t index, AudioFrame* out_frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return false;
  }

  auto& client = clients_[index];
  if (client.queued_frames.empty()) {
    client.telemetry.queued_depth = 0;
    ++client.telemetry.underrun_count;
    ++client.telemetry.silence_injections;
    return false;
  }

  AudioFrame frame = client.queued_frames.front();
  client.queued_frames.pop_front();
  client.telemetry.queued_depth = static_cast<uint32_t>(client.queued_frames.size());
  trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kFrameConsumed,
                       static_cast<uint32_t>(index), frame.guest_submit_ptr,
                       client.telemetry.queued_depth,
                       static_cast<uint32_t>(frame.sequence_number));
  if (out_frame) {
    *out_frame = std::move(frame);
  }
  return true;
}

void AudioRuntime::ReportSamplesConsumedForClient(const size_t index,
                                                  const uint32_t sample_count) {
  if (!sample_count) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return;
  }

  auto& client = clients_[index];
  client.telemetry.last_consume_ticks = static_cast<uint64_t>(NextTickLocked());
  client.clock.AdvanceSamples(sample_count);
  client.telemetry.consumed_frames =
      static_cast<uint32_t>(std::min<uint64_t>(client.clock.consumed_frames(),
                                              std::numeric_limits<uint32_t>::max()));
}

bool AudioRuntime::ShouldRequestCallbackForClient(const size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return false;
  }
  return clients_[index].queued_frames.size() <= EffectiveCallbackLowWaterFrames(clients_[index]);
}

AudioDriverTelemetry AudioRuntime::GetClientTelemetry(const size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return {};
  }
  return MergeDriverTelemetry(clients_[index]);
}

uint32_t AudioRuntime::GetClientRenderDriverTic(const size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return 0;
  }
  return ComputeRenderDriverTic(clients_[index]);
}

AudioClientTimingSnapshot AudioRuntime::GetClientTimingSnapshot(const size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return {};
  }
  return BuildTimingSnapshot(clients_[index]);
}

AudioTelemetrySnapshot AudioRuntime::GetTelemetrySnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);

  AudioTelemetrySnapshot snapshot;
  snapshot.backend_name = backend_name();
  for (size_t i = 0; i < clients_.size(); ++i) {
    const auto& client = clients_[i];
    auto& client_snapshot = snapshot.clients[i];
    client_snapshot.in_use = client.in_use;
    client_snapshot.callback = client.callback;
    client_snapshot.callback_arg = client.callback_arg;
    client_snapshot.telemetry = MergeDriverTelemetry(client);
    client_snapshot.render_driver_tic = ComputeRenderDriverTic(client);

    if (!client.in_use) {
      continue;
    }

    ++snapshot.active_clients;
    snapshot.queued_frames += client_snapshot.telemetry.queued_depth;
    snapshot.peak_queued_frames =
        std::max(snapshot.peak_queued_frames, client_snapshot.telemetry.peak_queued_depth);
    snapshot.dropped_frames += client_snapshot.telemetry.dropped_frames;
    snapshot.underruns += client_snapshot.telemetry.underrun_count;
    snapshot.silence_injections += client_snapshot.telemetry.silence_injections;
  }

  snapshot.peak_queued_frames =
      std::max(snapshot.peak_queued_frames, peak_queued_frames_);
  snapshot.trace_event_count = trace_buffer_.size();
  return snapshot;
}

bool AudioRuntime::Save(stream::ByteStream* stream) {
  if (!stream) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  stream->Write(static_cast<uint32_t>(paused_ ? 1 : 0));
  for (size_t i = 0; i < clients_.size(); ++i) {
    const auto& client = clients_[i];
    stream->Write(static_cast<uint32_t>(client.in_use ? 1 : 0));
    if (!client.in_use) {
      continue;
    }
    stream->Write(static_cast<uint32_t>(client.client_index));
    stream->Write(client.callback);
    stream->Write(client.callback_arg);
    stream->Write(client.wrapped_callback_arg);
  }

  // XMA state is no longer shadow-owned by AudioRuntime. The live XMA path
  // is owned by XmaDecoder / kernel-facing XMA services.
  return true;
}

bool AudioRuntime::Restore(stream::ByteStream* stream) {
  if (!stream) {
    return false;
  }

  paused_ = stream->Read<uint32_t>() != 0;
  for (size_t i = 0; i < clients_.size(); ++i) {
    const bool in_use = stream->Read<uint32_t>() != 0;
    if (!in_use) {
      continue;
    }

    size_t index = stream->Read<uint32_t>();
    uint32_t callback = stream->Read<uint32_t>();
    uint32_t callback_arg = stream->Read<uint32_t>();
    uint32_t wrapped_callback_arg = stream->Read<uint32_t>();

    auto driver = CreateConfiguredDriver(memory_, this, index);
    if (!driver || !driver->Initialize()) {
      if (driver) {
        driver->Shutdown();
      }
      return false;
    }

    auto& client = clients_[index];
    client.in_use = true;
    client.client_index = index;
    client.callback = callback;
    client.callback_arg = callback_arg;
    client.wrapped_callback_arg = wrapped_callback_arg;
    client.driver = driver.release();
    backend_name_ = client.driver->backend_name();
    client.clock.Reset();
    client.clock.SetPaused(paused_);
    client.telemetry = {};
  }

  // XMA state restore is intentionally not handled here because the live XMA
  // path no longer routes through an AudioRuntime-owned context pool.
  return true;
}

void AudioRuntime::Pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  paused_ = true;
  for (auto& client : clients_) {
    if (client.in_use) {
      client.clock.SetPaused(true);
    }
  }
}

void AudioRuntime::Resume() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!paused_) {
    return;
  }
  paused_ = false;
  for (auto& client : clients_) {
    if (client.in_use) {
      client.clock.SetPaused(false);
    }
  }
  if (worker_wake_event_) {
    worker_wake_event_->Set();
  }
}

bool AudioRuntime::is_paused() const {
  return paused_;
}

size_t AudioRuntime::ConsumeQueuedFramesForClient(const size_t index, const size_t max_frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= clients_.size() || !clients_[index].in_use) {
    return 0;
  }

  auto& client = clients_[index];
  size_t consumed = 0;
  while (consumed < max_frames && !client.queued_frames.empty()) {
    const AudioFrame frame = client.queued_frames.front();
    client.queued_frames.pop_front();
    client.telemetry.last_consume_ticks = static_cast<uint64_t>(NextTickLocked());
    client.telemetry.queued_depth = static_cast<uint32_t>(client.queued_frames.size());
    client.telemetry.consumed_frames =
        static_cast<uint32_t>(std::min<uint64_t>(client.clock.consumed_frames(),
                                                std::numeric_limits<uint32_t>::max()));
    trace_buffer_.Record(AudioTraceSubsystem::kCore, AudioTraceEventType::kFrameConsumed,
                         static_cast<uint32_t>(index), frame.guest_submit_ptr,
                         client.telemetry.queued_depth,
                         static_cast<uint32_t>(frame.sequence_number));
    ++consumed;
  }
  return consumed;
}

size_t AudioRuntime::ConsumeAllAvailableFrames() {
  size_t consumed = 0;
  for (size_t i = 0; i < clients_.size(); ++i) {
    consumed += ConsumeQueuedFramesForClient(i, std::numeric_limits<size_t>::max());
  }
  return consumed;
}

std::string AudioRuntime::backend_name() const {
  return backend_name_;
}

void AudioRuntime::WorkerThreadMain() {
  REXAPU_INFO("AudioRuntime worker started: backend={} scheduling=queue-depth-driven "
              "low_water={} target={} max={} startup_spacing_ms={} startup_max_lead_frames={}",
              backend_name_, CallbackLowWaterFrames(), CallbackTargetQueueDepth(),
              QueueDepthLimit(), StartupCallbackMinSpacingMs(),
              StartupMaxCallbackLeadFrames());

  rex::thread::WaitHandle* wait_handles[2]{};
  wait_handles[0] = worker_wake_event_.get();
  wait_handles[1] = shutdown_event_.get();

  while (worker_running_.load(std::memory_order_acquire)) {
    // Wait for: host driver wake, shutdown, or 5ms timeout
    rex::thread::WaitAny(wait_handles, 2, true, std::chrono::milliseconds(5));

    if (!worker_running_.load(std::memory_order_acquire)) {
      break;
    }

    ++worker_iteration_count_;
    const bool startup_trace = worker_iteration_count_ <= 60;

    // Skip dispatch while paused
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (paused_) {
        continue;
      }
    }

    // Check each active client and dispatch callbacks to fill queue to target
    for (size_t i = 0; i < clients_.size(); ++i) {
      // Before the first real playback consumption, keep callback lead tightly
      // serialized so guest-side movie/cutscene workers cannot sprint ahead of
      // the render driver on timeout wakes alone.
      uint32_t dispatch = 0;
      while (true) {
        uint32_t client_callback = 0;
        uint32_t client_callback_arg = 0;
        bool needs_callback = false;
        bool throttle_startup = false;
        bool startup_mode = false;
        size_t current_depth = 0;
        uint32_t target = 0;
        uint32_t callback_limit = 0;
        uint32_t startup_inflight_frames = 0;
        uint32_t startup_spacing_remaining_ms = 0;
        uint32_t submitted_before = 0;

        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (!clients_[i].in_use) {
            break;
          }
          target = EffectiveCallbackTargetQueueDepth(clients_[i]);
          startup_mode = clients_[i].telemetry.callback_dispatch_count == 0;
          callback_limit = startup_mode ? 1u : target;
          if (dispatch >= callback_limit) {
            break;
          }
          current_depth = clients_[i].queued_frames.size();
          needs_callback = current_depth <= EffectiveCallbackLowWaterFrames(clients_[i]);
          if (needs_callback && startup_mode) {
            throttle_startup =
                ShouldThrottleStartupCallback(clients_[i], AudioClock::clock_type::now(),
                                              &startup_inflight_frames,
                                              &startup_spacing_remaining_ms);
            if (throttle_startup) {
              ++clients_[i].telemetry.callback_throttle_count;
              trace_buffer_.Record(AudioTraceSubsystem::kCore,
                                   AudioTraceEventType::kCallbackThrottled,
                                   static_cast<uint32_t>(i), startup_inflight_frames,
                                   startup_spacing_remaining_ms, current_depth);
            }
          }
          if (needs_callback) {
            if (throttle_startup) {
              break;
            }
            const auto now = AudioClock::clock_type::now();
            client_callback = clients_[i].callback;
            client_callback_arg = clients_[i].wrapped_callback_arg;
            submitted_before = clients_[i].telemetry.submitted_frames;
            if (clients_[i].first_callback_dispatch_time.time_since_epoch().count() == 0) {
              clients_[i].first_callback_dispatch_time = now;
            }
            clients_[i].last_callback_dispatch_time = now;
            ++clients_[i].telemetry.callback_dispatch_count;
            clients_[i].telemetry.last_callback_request_ticks =
                static_cast<uint64_t>(NextTickLocked());
            trace_buffer_.Record(AudioTraceSubsystem::kCore,
                                 AudioTraceEventType::kCallbackDispatched,
                                 static_cast<uint32_t>(i),
                                 static_cast<uint32_t>(current_depth),
                                 startup_inflight_frames, target);
          }
        }

        if (!needs_callback) {
          break;
        }
        if (throttle_startup) {
          if (startup_trace) {
            REXAPU_INFO(
                "AudioRuntime startup throttle: iter={} client={} queued={} inflight={} "
                "spacing_remaining_ms={} throttles={}",
                worker_iteration_count_, i, current_depth, startup_inflight_frames,
                startup_spacing_remaining_ms, GetClientTelemetry(i).callback_throttle_count);
          }
          break;
        }

        if (!client_callback || !function_dispatcher_ || !worker_thread_context_) {
          break;
        }

        if (startup_trace) {
          REXAPU_INFO(
              "AudioRuntime dispatch: iter={} client={} depth={} inflight={} "
              "dispatch_round={} dispatch_limit={} callback_count={}",
              worker_iteration_count_, i, current_depth, startup_inflight_frames, dispatch,
              callback_limit, GetClientTelemetry(i).callback_dispatch_count);
        }

        const auto callback_start_time = AudioClock::clock_type::now();
        uint64_t args[] = {client_callback_arg};
        function_dispatcher_->Execute(worker_thread_context_->thread_state(), client_callback, args,
                                      rex::countof(args));
        const auto callback_end_time = AudioClock::clock_type::now();
        const auto callback_duration_us_64 = std::chrono::duration_cast<std::chrono::microseconds>(
                                                 callback_end_time - callback_start_time)
                                                 .count();
        const uint32_t callback_duration_us =
            callback_duration_us_64 > std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(callback_duration_us_64);
        ++dispatch;

        uint32_t produced_frames = 0;
        uint32_t depth_after = 0;
        uint32_t empty_count = 0;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (clients_[i].in_use) {
            auto& client = clients_[i];
            const uint32_t submitted_after = client.telemetry.submitted_frames;
            produced_frames = submitted_after >= submitted_before ? submitted_after - submitted_before
                                                                  : 0;
            depth_after = static_cast<uint32_t>(client.queued_frames.size());
            client.telemetry.last_callback_produced_frames = produced_frames;
            client.telemetry.peak_callback_produced_frames =
                std::max(client.telemetry.peak_callback_produced_frames, produced_frames);
            client.telemetry.last_callback_duration_us = callback_duration_us;
            client.telemetry.peak_callback_duration_us =
                std::max(client.telemetry.peak_callback_duration_us, callback_duration_us);
            if (produced_frames == 0) {
              ++client.telemetry.callback_empty_count;
            } else {
              client.telemetry.callback_empty_count = 0;
            }
            empty_count = client.telemetry.callback_empty_count;
            trace_buffer_.Record(AudioTraceSubsystem::kCore,
                                 AudioTraceEventType::kCallbackCompleted,
                                 static_cast<uint32_t>(i), produced_frames, depth_after,
                                 callback_duration_us);
          }
        }

        if (startup_trace) {
          REXAPU_INFO(
              "AudioRuntime callback result: iter={} client={} produced={} depth_after={} "
              "empty_count={} duration_us={}",
              worker_iteration_count_, i, produced_frames, depth_after, empty_count,
              callback_duration_us);
        }

        if (produced_frames == 0) {
          break;
        }
      }
    }

    // Startup telemetry - first 60 iterations at INFO level
    if (startup_trace && (worker_iteration_count_ % 10) == 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      for (size_t i = 0; i < clients_.size(); ++i) {
        if (!clients_[i].in_use) continue;
        const auto& c = clients_[i];
        REXAPU_INFO(
            "AudioRuntime startup: iter={} client={} queued={} target={} low_water={} "
            "submitted={} consumed={} underruns={} callbacks={} tic={} synthetic_tic={} "
            "submitted_tic={} startup_cap_tic={} startup_inflight={} callback_throttles={} "
            "callback_empty={} last_callback_frames={} last_callback_us={} peak={} drift_ms={:.1f}",
            worker_iteration_count_, i, c.queued_frames.size(),
            EffectiveCallbackTargetQueueDepth(c), EffectiveCallbackLowWaterFrames(c),
            c.telemetry.submitted_frames, c.telemetry.consumed_frames, c.telemetry.underrun_count,
            c.telemetry.callback_dispatch_count, ComputeRenderDriverTic(c),
            ComputeStartupSyntheticTic(c), SubmittedTicSamples(c), StartupTicCapSamples(c),
            StartupInflightFrames(c), c.telemetry.callback_throttle_count,
            c.telemetry.callback_empty_count, c.telemetry.last_callback_produced_frames,
            c.telemetry.last_callback_duration_us,
            c.telemetry.peak_queued_depth,
            c.clock.drift_ms());
      }
    }

    // Periodic telemetry - every ~3 seconds at DEBUG level
    if (!startup_trace && (worker_iteration_count_ % 600) == 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      for (size_t i = 0; i < clients_.size(); ++i) {
        if (!clients_[i].in_use) continue;
        const auto& c = clients_[i];
        REXAPU_DEBUG(
            "AudioRuntime periodic: client={} queued={} target={} low_water={} submitted={} "
            "consumed={} underruns={} callbacks={} tic={} synthetic_tic={} submitted_tic={} "
            "startup_cap_tic={} startup_inflight={} callback_throttles={} callback_empty={} "
            "last_callback_frames={} last_callback_us={} peak={} drift_ms={:.1f}",
            i, c.queued_frames.size(), EffectiveCallbackTargetQueueDepth(c),
            EffectiveCallbackLowWaterFrames(c), c.telemetry.submitted_frames,
            c.telemetry.consumed_frames, c.telemetry.underrun_count,
            c.telemetry.callback_dispatch_count, ComputeRenderDriverTic(c),
            ComputeStartupSyntheticTic(c), SubmittedTicSamples(c), StartupTicCapSamples(c),
            StartupInflightFrames(c), c.telemetry.callback_throttle_count,
            c.telemetry.callback_empty_count, c.telemetry.last_callback_produced_frames,
            c.telemetry.last_callback_duration_us,
            c.telemetry.peak_queued_depth,
            c.clock.drift_ms());
      }
    }
  }

  REXAPU_INFO("AudioRuntime worker stopped after {} iterations", worker_iteration_count_);
}

uint64_t AudioRuntime::NextTickLocked() {
  return ++tick_counter_;
}

void AudioRuntime::WakeWorker() {
  if (worker_wake_event_) {
    worker_wake_event_->Set();
  }
}

}  // namespace rex::audio
