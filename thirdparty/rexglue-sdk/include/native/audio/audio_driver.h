// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <cstdint>

#include <native/audio/audio_client.h>
#include <rex/kernel.h>
#include <rex/memory.h>

namespace rex::audio {

class AudioDriver {
 public:
  explicit AudioDriver(memory::Memory* memory);
  virtual ~AudioDriver();

  virtual bool Initialize() = 0;
  virtual void Shutdown() = 0;
  virtual void SubmitFrame(uint32_t samples_ptr) = 0;
  virtual void SubmitSilenceFrame() = 0;
  virtual AudioDriverTelemetry GetTelemetry() const;
  virtual const char* backend_name() const = 0;
  virtual uint32_t queue_low_water_frames() const { return 1; }
  virtual uint32_t queue_target_frames() const { return 2; }

 protected:
  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return memory_->TranslatePhysical(guest_address);
  }

  memory::Memory* memory_ = nullptr;
};

}  // namespace rex::audio
