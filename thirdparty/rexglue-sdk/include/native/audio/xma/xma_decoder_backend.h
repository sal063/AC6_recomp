// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace rex::audio::xma {

struct XmaDecodeRequest {
  std::span<const uint8_t> packet_data{};
  uint32_t sample_rate{0};
  bool is_two_channel{false};
};

class XmaDecoderBackend {
 public:
  virtual ~XmaDecoderBackend() = default;

  virtual bool IsAvailable() const = 0;
  virtual bool DecodePacket(const XmaDecodeRequest& request,
                            std::span<uint8_t> output_frame) = 0;
};

class NullXmaDecoderBackend final : public XmaDecoderBackend {
 public:
  bool IsAvailable() const override;
  bool DecodePacket(const XmaDecodeRequest& request,
                    std::span<uint8_t> output_frame) override;
};

std::unique_ptr<XmaDecoderBackend> CreateXmaDecoderBackend();

}  // namespace rex::audio::xma
