/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <span>

namespace rex::audio::xma {

struct XmaPacketParseResult {
  bool valid{false};
  uint32_t packet_count{0};
  uint32_t frame_count{0};
  uint32_t total_payload_bytes{0};
};

class XmaPacketParser {
 public:
  XmaPacketParseResult Parse(std::span<const uint8_t> packet_data) const;
};

}  // namespace rex::audio::xma
