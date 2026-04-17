/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <rex/audio/xma/xma_decoder_backend.h>

namespace rex::audio::xma {

bool NullXmaDecoderBackend::IsAvailable() const {
  return false;
}

bool NullXmaDecoderBackend::DecodePacket([[maybe_unused]] std::span<const uint8_t> packet_data,
                                         [[maybe_unused]] std::vector<float>* out_pcm) {
  return false;
}

}  // namespace rex::audio::xma
