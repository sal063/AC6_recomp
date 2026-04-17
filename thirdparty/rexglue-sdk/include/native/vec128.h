#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include <native/math.h>
#include <native/platform.h>

namespace rex {

typedef struct alignas(16) vec128_s {
  union {
    struct {
      float x;
      float y;
      float z;
      float w;
    };
    struct {
      int32_t ix;
      int32_t iy;
      int32_t iz;
      int32_t iw;
    };
    struct {
      uint32_t ux;
      uint32_t uy;
      uint32_t uz;
      uint32_t uw;
    };
    float f32[4];
    double f64[2];
    int8_t i8[16];
    uint8_t u8[16];
    int16_t i16[8];
    uint16_t u16[8];
    int32_t i32[4];
    uint32_t u32[4];
    int64_t i64[2];
    uint64_t u64[2];
    struct {
      uint64_t low;
      uint64_t high;
    };
  };

  bool operator==(const vec128_s& other) const { return low == other.low && high == other.high; }
  bool operator!=(const vec128_s& other) const { return !(*this == other); }

  vec128_s operator^(const vec128_s& other) const {
    vec128_s value = *this;
    value.high ^= other.high;
    value.low ^= other.low;
    return value;
  }

  vec128_s& operator^=(const vec128_s& other) {
    *this = *this ^ other;
    return *this;
  }

  vec128_s operator&(const vec128_s& other) const {
    vec128_s value = *this;
    value.high &= other.high;
    value.low &= other.low;
    return value;
  }

  vec128_s& operator&=(const vec128_s& other) {
    *this = *this & other;
    return *this;
  }

  vec128_s operator|(const vec128_s& other) const {
    vec128_s value = *this;
    value.high |= other.high;
    value.low |= other.low;
    return value;
  }

  vec128_s& operator|=(const vec128_s& other) {
    *this = *this | other;
    return *this;
  }
} vec128_t;

static inline vec128_t vec128i(uint32_t src) {
  vec128_t value;
  for (int i = 0; i < 4; ++i) {
    value.u32[i] = src;
  }
  return value;
}

static inline vec128_t vec128i(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
  vec128_t value;
  value.u32[0] = x;
  value.u32[1] = y;
  value.u32[2] = z;
  value.u32[3] = w;
  return value;
}

static inline vec128_t vec128q(uint64_t src) {
  vec128_t value;
  for (int i = 0; i < 2; ++i) {
    value.i64[i] = static_cast<int64_t>(src);
  }
  return value;
}

static inline vec128_t vec128q(uint64_t x, uint64_t y) {
  vec128_t value;
  value.i64[0] = static_cast<int64_t>(x);
  value.i64[1] = static_cast<int64_t>(y);
  return value;
}

static inline vec128_t vec128d(double src) {
  vec128_t value;
  for (int i = 0; i < 2; ++i) {
    value.f64[i] = src;
  }
  return value;
}

static inline vec128_t vec128d(double x, double y) {
  vec128_t value;
  value.f64[0] = x;
  value.f64[1] = y;
  return value;
}

static inline vec128_t vec128f(float src) {
  vec128_t value;
  for (int i = 0; i < 4; ++i) {
    value.f32[i] = src;
  }
  return value;
}

static inline vec128_t vec128f(float x, float y, float z, float w) {
  vec128_t value;
  value.f32[0] = x;
  value.f32[1] = y;
  value.f32[2] = z;
  value.f32[3] = w;
  return value;
}

static inline vec128_t vec128s(uint16_t src) {
  vec128_t value;
  for (int i = 0; i < 8; ++i) {
    value.u16[i] = src;
  }
  return value;
}

static inline vec128_t vec128s(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint16_t z0,
                               uint16_t z1, uint16_t w0, uint16_t w1) {
  vec128_t value;
  value.u16[0] = x1;
  value.u16[1] = x0;
  value.u16[2] = y1;
  value.u16[3] = y0;
  value.u16[4] = z1;
  value.u16[5] = z0;
  value.u16[6] = w1;
  value.u16[7] = w0;
  return value;
}

static inline vec128_t vec128b(uint8_t src) {
  vec128_t value;
  for (int i = 0; i < 16; ++i) {
    value.u8[i] = src;
  }
  return value;
}

static inline vec128_t vec128b(uint8_t x0, uint8_t x1, uint8_t x2, uint8_t x3, uint8_t y0,
                               uint8_t y1, uint8_t y2, uint8_t y3, uint8_t z0, uint8_t z1,
                               uint8_t z2, uint8_t z3, uint8_t w0, uint8_t w1, uint8_t w2,
                               uint8_t w3) {
  vec128_t value;
  value.u8[0] = x3;
  value.u8[1] = x2;
  value.u8[2] = x1;
  value.u8[3] = x0;
  value.u8[4] = y3;
  value.u8[5] = y2;
  value.u8[6] = y1;
  value.u8[7] = y0;
  value.u8[8] = z3;
  value.u8[9] = z2;
  value.u8[10] = z1;
  value.u8[11] = z0;
  value.u8[12] = w3;
  value.u8[13] = w2;
  value.u8[14] = w1;
  value.u8[15] = w0;
  return value;
}

std::string to_string(const vec128_t& value);
std::ostream& operator<<(std::ostream& os, const vec128_t& value);

}  // namespace rex
