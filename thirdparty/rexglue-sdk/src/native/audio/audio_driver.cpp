/**
 * ReXGlue native audio runtime
 * Part of the AC6 Recompilation project
 */

#include <native/audio/audio_driver.h>

namespace rex::audio {

AudioDriver::AudioDriver(memory::Memory* memory) : memory_(memory) {}

AudioDriver::~AudioDriver() = default;

AudioDriverTelemetry AudioDriver::GetTelemetry() const {
  return {};
}

}  // namespace rex::audio
