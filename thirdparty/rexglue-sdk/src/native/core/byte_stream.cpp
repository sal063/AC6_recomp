#include <cstring>
#include <span>

#include <native/assert.h>
#include <native/stream.h>

namespace rex::stream {

ByteStream::ByteStream(uint8_t* data, size_t data_length, size_t offset)
    : data_(data), data_length_(data_length), offset_(offset) {}

ByteStream::~ByteStream() = default;

void ByteStream::Advance(size_t num_bytes) {
  assert_true(offset_ + num_bytes <= data_length_);
  offset_ += num_bytes;
}

void ByteStream::Read(std::span<uint8_t> buffer) {
  assert_true(offset_ + buffer.size() <= data_length_);
  std::memcpy(buffer.data(), data_ + offset_, buffer.size());
  Advance(buffer.size());
}

void ByteStream::Write(std::span<const uint8_t> buffer) {
  assert_true(offset_ + buffer.size() <= data_length_);
  std::memcpy(data_ + offset_, buffer.data(), buffer.size());
  Advance(buffer.size());
}

template <>
std::string ByteStream::Read() {
  std::string value;
  const uint32_t length = Read<uint32_t>();
  value.resize(length);
  Read(std::span<uint8_t>(reinterpret_cast<uint8_t*>(value.data()), length));
  return value;
}

template <>
std::u16string ByteStream::Read() {
  std::u16string value;
  const size_t length = Read<uint32_t>();
  value.resize(length);
  Read(std::span<uint8_t>(reinterpret_cast<uint8_t*>(value.data()), length * sizeof(char16_t)));
  return value;
}

}  // namespace rex::stream
