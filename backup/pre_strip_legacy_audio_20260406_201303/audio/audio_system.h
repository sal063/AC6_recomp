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

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_runtime.h>
#include <rex/kernel.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/interfaces/audio.h>

namespace rex::stream {
class ByteStream;
}  // namespace rex::stream

namespace rex::audio {

constexpr memory::fourcc_t kAudioSaveSignature = memory::make_fourcc("XAUD");

class XmaDecoder;

class AudioSystem : public system::IAudioSystem {
 public:
  static std::unique_ptr<AudioSystem> Create(runtime::FunctionDispatcher* function_dispatcher);

  virtual ~AudioSystem();

  memory::Memory* memory() const { return memory_; }
  runtime::FunctionDispatcher* function_dispatcher() const { return function_dispatcher_; }
  AudioRuntime* runtime() const { return runtime_.get(); }
  XmaDecoder* xma_decoder() const { return xma_decoder_.get(); }

  X_STATUS Setup(system::KernelState* kernel_state) override;
  void Shutdown() override;

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg, size_t* out_index);
  void UnregisterClient(size_t index);
  void SubmitFrame(size_t index, uint32_t samples_ptr);
  void SubmitSilenceFrame(size_t index);
  AudioDriverTelemetry GetClientTelemetry(size_t index);
  uint32_t GetClientRenderDriverTic(size_t index);
  AudioClientTimingSnapshot GetClientTimingSnapshot(size_t index);
  AudioTelemetrySnapshot GetTelemetrySnapshot() const;
  const AudioTraceBuffer& trace_buffer() const;
  std::string GetBackendName() const;

  bool Save(stream::ByteStream* stream);
  bool Restore(stream::ByteStream* stream);

  bool is_paused() const;
  void Pause();
  void Resume();

 protected:
  explicit AudioSystem(runtime::FunctionDispatcher* function_dispatcher);

  memory::Memory* memory_ = nullptr;
  runtime::FunctionDispatcher* function_dispatcher_ = nullptr;
  std::unique_ptr<AudioRuntime> runtime_;
  std::unique_ptr<XmaDecoder> xma_decoder_;
};

}  // namespace rex::audio
