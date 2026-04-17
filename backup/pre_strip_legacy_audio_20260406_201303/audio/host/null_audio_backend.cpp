/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <rex/audio/audio_runtime.h>
#include <rex/audio/host/host_audio_backend.h>

namespace rex::audio::host {

namespace {

class NullAudioBackend final : public IHostAudioBackend {
 public:
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void Shutdown() override {}
  std::string_view backend_name() const override { return "null"; }
  void Pump(AudioRuntime& runtime) override { runtime.ConsumeAllAvailableFrames(); }
};

}  // namespace

std::unique_ptr<IHostAudioBackend> CreateNullAudioBackend() {
  return std::make_unique<NullAudioBackend>();
}

}  // namespace rex::audio::host
