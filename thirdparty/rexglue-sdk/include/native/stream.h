#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rex::stream {

class BitStream {
 public:
  BitStream(uint8_t* buffer, size_t size_in_bits);
  ~BitStream();

  const uint8_t* buffer() const { return buffer_; }
  uint8_t* buffer() { return buffer_; }
  size_t offset_bits() const { return offset_bits_; }
  size_t size_bits() const { return size_bits_; }

  void Advance(size_t num_bits);
  void SetOffset(size_t offset_bits);
  size_t BitsRemaining();
  uint64_t Peek(size_t num_bits);
  uint64_t Read(size_t num_bits);
  bool Write(uint64_t value, size_t num_bits);
  size_t Copy(uint8_t* dest_buffer, size_t num_bits);

 private:
  uint8_t* buffer_ = nullptr;
  size_t offset_bits_ = 0;
  size_t size_bits_ = 0;
};

class ByteStream {
 public:
  ByteStream(uint8_t* data, size_t data_length, size_t offset = 0);
  ~ByteStream();

  void Advance(size_t num_bytes);
  void Read(std::span<uint8_t> buffer);
  void Write(std::span<const uint8_t> buffer);

  void Read(void* buffer, size_t length) {
    Read(std::span<uint8_t>(reinterpret_cast<uint8_t*>(buffer), length));
  }

  void Write(const void* buffer, size_t length) {
    Write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buffer), length));
  }

  const uint8_t* data() const { return data_; }
  uint8_t* data() { return data_; }
  size_t data_length() const { return data_length_; }

  size_t offset() const { return offset_; }
  void set_offset(size_t offset) { offset_ = offset; }

  template <typename T>
  T Read() {
    T data;
    Read(std::span<uint8_t>(reinterpret_cast<uint8_t*>(&data), sizeof(T)));
    return data;
  }

  template <typename T>
  void Write(T data) {
    Write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data), sizeof(T)));
  }

  void Write(std::string_view value) {
    Write(uint32_t(value.length()));
    Write(value.data(), value.length() * sizeof(char));
  }

  void Write(std::u16string_view value) {
    Write(uint32_t(value.length()));
    Write(value.data(), value.length() * sizeof(char16_t));
  }

 private:
  uint8_t* data_ = nullptr;
  size_t data_length_ = 0;
  size_t offset_ = 0;
};

template <>
std::string ByteStream::Read();

template <>
std::u16string ByteStream::Read();

}  // namespace rex::stream
