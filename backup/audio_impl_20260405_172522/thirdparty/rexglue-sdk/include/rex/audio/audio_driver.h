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

#include <cstdint>

#include <rex/kernel.h>
#include <rex/memory.h>

namespace rex::audio {

struct AudioDriverTelemetry {
  uint32_t submitted_frames{0};
  uint32_t consumed_frames{0};
  uint32_t underrun_count{0};
  uint32_t silence_injections{0};
  uint32_t queued_depth{0};
  uint32_t peak_queued_depth{0};
};

class AudioDriver {
 public:
  explicit AudioDriver(memory::Memory* memory);
  virtual ~AudioDriver();

  virtual void SubmitFrame(uint32_t samples_ptr) = 0;
  virtual AudioDriverTelemetry GetTelemetry() const;

 protected:
  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return memory_->TranslatePhysical(guest_address);
  }

  memory::Memory* memory_ = nullptr;
};

}  // namespace rex::audio
