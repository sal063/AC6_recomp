#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <native/math.h>

namespace rex::bit {

class BitMap {
 public:
  BitMap();
  BitMap(size_t size_bits);
  BitMap(uint64_t* data, size_t size_bits);

  size_t Acquire();
  void Release(size_t index);
  void Resize(size_t new_size_bits);
  void Reset();

  const std::vector<uint64_t> data() const { return data_; }
  std::vector<uint64_t>& data() { return data_; }

 private:
  static constexpr size_t kDataSize = 8;
  static constexpr size_t kDataSizeBits = kDataSize * 8;
  std::vector<uint64_t> data_;
};

template <typename Block>
std::pair<size_t, size_t> GetNextRangeUnset(const Block* bits, size_t first, size_t length) {
  if (!length) {
    return std::make_pair(size_t(first), size_t(0));
  }

  const size_t last = first + length - 1;
  const size_t block_bits = sizeof(Block) * CHAR_BIT;
  const size_t block_first = first / block_bits;
  const size_t block_last = last / block_bits;
  size_t range_start = SIZE_MAX;

  for (size_t i = block_first; i <= block_last; ++i) {
    Block block = bits[i];
    if (i == block_first) {
      block |= (Block(1) << (first & (block_bits - 1))) - 1;
    }
    if (i == block_last && (last & (block_bits - 1)) != block_bits - 1) {
      block |= ~((Block(1) << ((last & (block_bits - 1)) + 1)) - 1);
    }

    while (true) {
      uint32_t block_bit;
      if (range_start == SIZE_MAX) {
        if (!rex::bit_scan_forward(~block, &block_bit)) {
          break;
        }
        range_start = i * block_bits + block_bit;
      } else {
        Block block_bits_set_from_start = block;
        if (i == range_start / block_bits) {
          block_bits_set_from_start &= ~((Block(1) << (range_start & (block_bits - 1))) - 1);
        }
        if (!rex::bit_scan_forward(block_bits_set_from_start, &block_bit)) {
          break;
        }
        return std::make_pair(range_start, (i * block_bits) + block_bit - range_start);
      }
    }
  }

  if (range_start != SIZE_MAX) {
    return std::make_pair(range_start, last + size_t(1) - range_start);
  }

  return std::make_pair(first + length, size_t(0));
}

template <typename Block>
void SetRange(Block* bits, size_t first, size_t length) {
  if (!length) {
    return;
  }

  const size_t last = first + length - 1;
  const size_t block_bits = sizeof(Block) * CHAR_BIT;
  const size_t block_first = first / block_bits;
  const size_t block_last = last / block_bits;
  const Block set_first = ~((Block(1) << (first & (block_bits - 1))) - 1);
  Block set_last = ~Block(0);
  if ((last & (block_bits - 1)) != (block_bits - 1)) {
    set_last &= (Block(1) << ((last & (block_bits - 1)) + 1)) - 1;
  }

  if (block_first == block_last) {
    bits[block_first] |= set_first & set_last;
    return;
  }

  bits[block_first] |= set_first;
  if (block_first + 1 < block_last) {
    std::memset(bits + block_first + 1, CHAR_MAX, (block_last - (block_first + 1)) * sizeof(Block));
  }
  bits[block_last] |= set_last;
}

}  // namespace rex::bit
