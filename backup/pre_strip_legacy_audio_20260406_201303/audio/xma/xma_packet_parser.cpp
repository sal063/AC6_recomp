/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <rex/audio/xma/xma_packet_parser.h>

namespace rex::audio::xma {

XmaPacketParseResult XmaPacketParser::Parse(const std::span<const uint8_t> packet_data) const {
  XmaPacketParseResult result;
  result.valid = !packet_data.empty();
  result.packet_count = result.valid ? 1 : 0;
  result.frame_count = 0;
  result.total_payload_bytes = static_cast<uint32_t>(packet_data.size());
  return result;
}

}  // namespace rex::audio::xma
