#include <algorithm>
#include <cstring>

#include <native/assert.h>
#include <native/stream.h>
#include <rex/types.h>

namespace rex::stream {

BitStream::BitStream(uint8_t* buffer, size_t size_in_bits)
    : buffer_(buffer), size_bits_(size_in_bits) {}

BitStream::~BitStream() = default;

void BitStream::SetOffset(size_t offset_bits) {
  assert_false(offset_bits > size_bits_);
  offset_bits_ = std::min(offset_bits, size_bits_);
}

size_t BitStream::BitsRemaining() {
  return size_bits_ - offset_bits_;
}

uint64_t BitStream::Peek(size_t num_bits) {
  assert_false(num_bits > 57);
  assert_false(offset_bits_ + num_bits > size_bits_);

  const size_t offset_bytes = offset_bits_ >> 3;
  const size_t rel_offset_bits = offset_bits_ - (offset_bytes << 3);

  uint64_t bits = *reinterpret_cast<uint64_t*>(buffer_ + offset_bytes);
  bits = rex::byte_swap(bits);
  bits >>= 64 - (rel_offset_bits + num_bits);
  bits &= (1ULL << num_bits) - 1;
  return bits;
}

uint64_t BitStream::Read(size_t num_bits) {
  const uint64_t value = Peek(num_bits);
  Advance(num_bits);
  return value;
}

bool BitStream::Write(uint64_t value, size_t num_bits) {
  assert_false(num_bits > 57);
  assert_false(offset_bits_ + num_bits >= size_bits_);

  const size_t offset_bytes = offset_bits_ >> 3;
  const size_t rel_offset_bits = offset_bits_ - (offset_bytes << 3);

  uint64_t mask = (1ULL << num_bits) - 1;
  mask <<= 64 - (rel_offset_bits + num_bits);
  mask = ~mask;

  value <<= 64 - (rel_offset_bits + num_bits);

  uint64_t bits = *reinterpret_cast<uint64_t*>(buffer_ + offset_bytes);
  bits &= mask;
  bits |= value;
  *reinterpret_cast<uint64_t*>(buffer_ + offset_bytes) = bits;

  Advance(num_bits);
  return true;
}

size_t BitStream::Copy(uint8_t* dest_buffer, size_t num_bits) {
  const size_t offset_bytes = offset_bits_ >> 3;
  const size_t rel_offset_bits = offset_bits_ - (offset_bytes << 3);
  size_t bits_left = num_bits;
  size_t out_offset_bytes = 0;

  if (rel_offset_bits) {
    const uint64_t bits = Peek(8 - rel_offset_bits);
    const uint8_t clear_mask = ~((uint8_t(1) << rel_offset_bits) - 1);
    dest_buffer[out_offset_bytes] &= clear_mask;
    dest_buffer[out_offset_bytes] |= static_cast<uint8_t>(bits);

    bits_left -= 8 - rel_offset_bits;
    Advance(8 - rel_offset_bits);
    ++out_offset_bytes;
  }

  if (bits_left >= 8) {
    std::memcpy(dest_buffer + out_offset_bytes, buffer_ + offset_bytes + out_offset_bytes, bits_left / 8);
    out_offset_bytes += (bits_left / 8);
    Advance((bits_left / 8) * 8);
    bits_left -= (bits_left / 8) * 8;
  }

  if (bits_left) {
    uint64_t bits = Peek(bits_left);
    bits <<= 8 - bits_left;

    const uint8_t clear_mask = (uint8_t(1) << bits_left) - 1;
    dest_buffer[out_offset_bytes] &= clear_mask;
    dest_buffer[out_offset_bytes] |= static_cast<uint8_t>(bits);
    Advance(bits_left);
  }

  return rel_offset_bits;
}

void BitStream::Advance(size_t num_bits) {
  SetOffset(offset_bits_ + num_bits);
}

}  // namespace rex::stream
