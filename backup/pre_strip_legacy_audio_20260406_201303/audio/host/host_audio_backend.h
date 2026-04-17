/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#pragma once

#include <memory>
#include <string_view>

#include <rex/system/xtypes.h>

namespace rex::audio {
class AudioRuntime;
}

namespace rex::audio::host {

class IHostAudioBackend {
 public:
  virtual ~IHostAudioBackend() = default;

  virtual X_STATUS Setup() = 0;
  virtual void Shutdown() = 0;
  virtual std::string_view backend_name() const = 0;
  virtual void Pump(AudioRuntime& runtime) = 0;
};

std::unique_ptr<IHostAudioBackend> CreateNullAudioBackend();

}  // namespace rex::audio::host
