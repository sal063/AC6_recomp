// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <cstdint>

#include <native/audio/audio_client.h>
#include <rex/kernel.h>
#include <rex/memory.h>

namespace rex::audio {

// Guest render-driver frames needed to cover one host device period. A device
// period that is not a whole number of 256-sample guest frames still has to be
// satisfied from whole frames, so this rounds up; without the round-up the host
// asks for more audio than the runtime is ever allowed to queue and the
// shortfall is filled with silence on every callback.
constexpr uint32_t RequiredQueueFramesForDevice(const uint32_t device_buffer_frames,
                                                const uint32_t headroom_frames = 0) {
  const uint32_t buffer_frames = device_buffer_frames > 0 ? device_buffer_frames : 1u;
  const uint32_t periods =
      (buffer_frames + kRenderDriverTicSamplesPerFrame - 1) / kRenderDriverTicSamplesPerFrame;
  return (periods > 3u ? periods : 3u) + headroom_frames;
}

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
