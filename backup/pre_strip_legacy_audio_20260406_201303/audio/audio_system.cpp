/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/audio/audio_system.h>

#include <rex/audio/xma/decoder.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_BOOL(audio_mute, false, "Audio", "Mute host audio output");
REXCVAR_DEFINE_BOOL(ffmpeg_verbose, false, "Audio",
                    "Legacy placeholder while the audio stack is stubbed");
REXCVAR_DEFINE_BOOL(audio_trace_telemetry, false, "Audio",
                    "Trace audio runtime telemetry");
REXCVAR_DEFINE_BOOL(audio_trace_render_driver_verbose, false, "Audio",
                    "Trace render-driver activity");

namespace rex::audio {

std::unique_ptr<AudioSystem> AudioSystem::Create(
    runtime::FunctionDispatcher* function_dispatcher) {
  return std::unique_ptr<AudioSystem>(new AudioSystem(function_dispatcher));
}

AudioSystem::AudioSystem(runtime::FunctionDispatcher* function_dispatcher)
    : function_dispatcher_(function_dispatcher),
      xma_decoder_(std::make_unique<XmaDecoder>(function_dispatcher)) {}

AudioSystem::~AudioSystem() {
  Shutdown();
}

X_STATUS AudioSystem::Setup(system::KernelState* kernel_state) {
  memory_ = kernel_state ? kernel_state->memory() : nullptr;
  if (xma_decoder_) {
    const X_STATUS xma_status = xma_decoder_->Setup(kernel_state);
    if (XFAILED(xma_status)) {
      return xma_status;
    }
  }
  runtime_ = std::make_unique<AudioRuntime>(memory_, function_dispatcher_);
  const X_STATUS runtime_status = runtime_->Setup(kernel_state);
  if (XFAILED(runtime_status) && xma_decoder_) {
    xma_decoder_->Shutdown();
    return runtime_status;
  }
  if (!XFAILED(runtime_status)) {
    REXAPU_INFO(
        "AudioSystem setup complete: runtime=AudioRuntime backend={} trace_telemetry={} "
        "trace_verbose={} mute={}",
        runtime_->backend_name(), REXCVAR_GET(audio_trace_telemetry),
        REXCVAR_GET(audio_trace_render_driver_verbose), REXCVAR_GET(audio_mute));
  }
  return runtime_status;
}

void AudioSystem::Shutdown() {
  if (runtime_) {
    runtime_->Shutdown();
    runtime_.reset();
  }
  if (xma_decoder_) {
    xma_decoder_->Shutdown();
  }
}

X_STATUS AudioSystem::RegisterClient(const uint32_t callback, const uint32_t callback_arg,
                                     size_t* out_index) {
  return runtime_ ? runtime_->RegisterClient(callback, callback_arg, out_index) : X_E_FAIL;
}

void AudioSystem::UnregisterClient(const size_t index) {
  if (runtime_) {
    runtime_->UnregisterClient(index);
  }
}

void AudioSystem::SubmitFrame(const size_t index, const uint32_t samples_ptr) {
  if (runtime_) {
    runtime_->SubmitFrame(index, samples_ptr);
  }
}

void AudioSystem::SubmitSilenceFrame(const size_t index) {
  if (runtime_) {
    runtime_->SubmitSilenceFrame(index);
  }
}

AudioDriverTelemetry AudioSystem::GetClientTelemetry(const size_t index) {
  return runtime_ ? runtime_->GetClientTelemetry(index) : AudioDriverTelemetry{};
}

uint32_t AudioSystem::GetClientRenderDriverTic(const size_t index) {
  return runtime_ ? runtime_->GetClientRenderDriverTic(index) : 0;
}

AudioClientTimingSnapshot AudioSystem::GetClientTimingSnapshot(const size_t index) {
  return runtime_ ? runtime_->GetClientTimingSnapshot(index) : AudioClientTimingSnapshot{};
}

AudioTelemetrySnapshot AudioSystem::GetTelemetrySnapshot() const {
  return runtime_ ? runtime_->GetTelemetrySnapshot() : AudioTelemetrySnapshot{};
}

const AudioTraceBuffer& AudioSystem::trace_buffer() const {
  static const AudioTraceBuffer empty_trace;
  return runtime_ ? runtime_->trace_buffer() : empty_trace;
}

std::string AudioSystem::GetBackendName() const {
  return runtime_ ? runtime_->backend_name() : "none";
}

bool AudioSystem::Save(stream::ByteStream* stream) {
  return runtime_ ? runtime_->Save(stream) : false;
}

bool AudioSystem::Restore(stream::ByteStream* stream) {
  return runtime_ ? runtime_->Restore(stream) : false;
}

void AudioSystem::Pause() {
  if (runtime_) {
    runtime_->Pause();
  }
  if (xma_decoder_) {
    xma_decoder_->Pause();
  }
}

void AudioSystem::Resume() {
  if (runtime_) {
    runtime_->Resume();
  }
  if (xma_decoder_) {
    xma_decoder_->Resume();
  }
}

bool AudioSystem::is_paused() const {
  return runtime_ ? runtime_->is_paused() : false;
}

}  // namespace rex::audio
