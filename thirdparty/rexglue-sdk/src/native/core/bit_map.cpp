#include <cstring>

#include <native/assert.h>
#include <native/bit.h>
#include <native/thread/atomic.h>

namespace rex::bit {

BitMap::BitMap() = default;

BitMap::BitMap(size_t size_bits) {
  Resize(size_bits);
}

BitMap::BitMap(uint64_t* data, size_t size_bits) {
  assert_true(size_bits % kDataSizeBits == 0);

  data_.resize(size_bits / kDataSizeBits);
  std::memcpy(data_.data(), data, size_bits / kDataSizeBits);
}

size_t BitMap::Acquire() {
  for (size_t i = 0; i < data_.size(); ++i) {
    uint64_t entry = 0;
    uint64_t new_entry = 0;
    int64_t acquired_idx = -1;

    do {
      entry = data_[i];
      const uint8_t index = lzcnt(entry);
      if (index == kDataSizeBits) {
        acquired_idx = -1;
        break;
      }

      const uint64_t bit = 1ull << (kDataSizeBits - index - 1);
      new_entry = entry & ~bit;
      assert_not_zero(entry & bit);
      acquired_idx = index;
    } while (!thread::atomic_cas(entry, new_entry, &data_[i]));

    if (acquired_idx != -1) {
      return (i * kDataSizeBits) + acquired_idx;
    }
  }

  return static_cast<size_t>(-1);
}

void BitMap::Release(size_t index) {
  const auto slot = index / kDataSizeBits;
  index -= slot * kDataSizeBits;

  const uint64_t bit = 1ull << (kDataSizeBits - index - 1);
  uint64_t entry = 0;
  uint64_t new_entry = 0;
  do {
    entry = data_[slot];
    assert_zero(entry & bit);
    new_entry = entry | bit;
  } while (!thread::atomic_cas(entry, new_entry, &data_[slot]));
}

void BitMap::Resize(size_t new_size_bits) {
  const auto old_size = data_.size();
  assert_true(new_size_bits % kDataSizeBits == 0);
  data_.resize(new_size_bits / kDataSizeBits);

  if (data_.size() > old_size) {
    for (size_t i = old_size; i < data_.size(); ++i) {
      data_[i] = static_cast<uint64_t>(-1);
    }
  }
}

void BitMap::Reset() {
  for (size_t i = 0; i < data_.size(); ++i) {
    data_[i] = static_cast<uint64_t>(-1);
  }
}

}  // namespace rex::bit
