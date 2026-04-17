/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#pragma once

#include <span>
#include <vector>

namespace rex::audio::xma {

class XmaDecoderBackend {
 public:
  virtual ~XmaDecoderBackend() = default;

  virtual bool IsAvailable() const = 0;
  virtual bool DecodePacket(std::span<const uint8_t> packet_data,
                            std::vector<float>* out_pcm) = 0;
};

class NullXmaDecoderBackend final : public XmaDecoderBackend {
 public:
  bool IsAvailable() const override;
  bool DecodePacket(std::span<const uint8_t> packet_data,
                    std::vector<float>* out_pcm) override;
};

}  // namespace rex::audio::xma
