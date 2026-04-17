#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <type_traits>
#include <utility>

#include <native/platform.h>

#if REX_ARCH_AMD64
#include <xmmintrin.h>
#endif

namespace rex {

template <typename T, size_t N>
constexpr size_t countof(T (&)[N]) {
  return N;
}

template <typename T>
constexpr bool is_pow2(T value) {
  return (value & (value - 1)) == 0;
}

template <typename T>
constexpr T align(T value, T alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T, typename V>
constexpr T round_up(T value, V multiple, bool force_non_zero = true) {
  if (force_non_zero && !value) {
    return static_cast<T>(multiple);
  }
  return static_cast<T>((value + multiple - 1) / multiple * multiple);
}

template <typename T>
T clamp_float(T value, T min_value, T max_value) {
  const T clamped_to_min = std::isgreater(value, min_value) ? value : min_value;
  return std::isless(clamped_to_min, max_value) ? clamped_to_min : max_value;
}

template <typename T>
T saturate(T value) {
  return clamp_float(value, static_cast<T>(0.0f), static_cast<T>(1.0f));
}

template <typename T>
T next_pow2(T value) {
  value--;
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  value++;
  return value;
}

template <typename T>
constexpr T greatest_common_divisor(T a, T b) {
  if constexpr (__cpp_lib_gcd_lcm) {
    return std::gcd(a, b);
  } else {
    while (b) {
      a = std::exchange(b, a % b);
    }
    return a;
  }
}

template <typename T>
constexpr void reduce_fraction(T& numerator, T& denominator) {
  const auto gcd = greatest_common_divisor(numerator, denominator);
  numerator /= gcd;
  denominator /= gcd;
}

template <typename T>
constexpr void reduce_fraction(std::pair<T, T>& fraction) {
  reduce_fraction(fraction.first, fraction.second);
}

constexpr uint32_t make_bitmask(uint32_t first, uint32_t last) {
  return (static_cast<uint32_t>(-1) >> (31 - last)) & ~((1u << first) - 1);
}

constexpr uint32_t select_bits(uint32_t value, uint32_t first, uint32_t last) {
  return (value & make_bitmask(first, last)) >> first;
}

template <class T>
constexpr uint32_t bit_count(T value) {
  return static_cast<uint32_t>(std::popcount(value));
}

uint8_t lzcnt(uint8_t value);
uint8_t lzcnt(uint16_t value);
uint8_t lzcnt(uint32_t value);
uint8_t lzcnt(uint64_t value);

uint8_t tzcnt(uint8_t value);
uint8_t tzcnt(uint16_t value);
uint8_t tzcnt(uint32_t value);
uint8_t tzcnt(uint64_t value);

inline uint8_t lzcnt(int8_t value) { return lzcnt(static_cast<uint8_t>(value)); }
inline uint8_t lzcnt(int16_t value) { return lzcnt(static_cast<uint16_t>(value)); }
inline uint8_t lzcnt(int32_t value) { return lzcnt(static_cast<uint32_t>(value)); }
inline uint8_t lzcnt(int64_t value) { return lzcnt(static_cast<uint64_t>(value)); }

inline uint8_t tzcnt(int8_t value) { return tzcnt(static_cast<uint8_t>(value)); }
inline uint8_t tzcnt(int16_t value) { return tzcnt(static_cast<uint16_t>(value)); }
inline uint8_t tzcnt(int32_t value) { return tzcnt(static_cast<uint32_t>(value)); }
inline uint8_t tzcnt(int64_t value) { return tzcnt(static_cast<uint64_t>(value)); }

bool bit_scan_forward(uint32_t value, uint32_t* out_first_set_index);
bool bit_scan_forward(uint64_t value, uint32_t* out_first_set_index);

inline bool bit_scan_forward(int32_t value, uint32_t* out_first_set_index) {
  return bit_scan_forward(static_cast<uint32_t>(value), out_first_set_index);
}

inline bool bit_scan_forward(int64_t value, uint32_t* out_first_set_index) {
  return bit_scan_forward(static_cast<uint64_t>(value), out_first_set_index);
}

template <typename T>
inline T log2_floor(T value) {
  return static_cast<T>(sizeof(T) * 8 - 1 - lzcnt(value));
}

template <typename T>
inline T log2_ceil(T value) {
  return static_cast<T>(sizeof(T) * 8 - lzcnt(static_cast<T>(value - 1)));
}

template <typename T>
inline T rotate_left(T value, uint8_t shift) {
  return static_cast<T>((T(value) << shift) | (T(value) >> ((sizeof(T) * 8) - shift)));
}

#if REX_ARCH_AMD64
template <int N>
float m128_f32(const __m128& value) {
  float result;
  _mm_store_ss(&result, _mm_shuffle_ps(value, value, _MM_SHUFFLE(N, N, N, N)));
  return result;
}

template <int N>
int32_t m128_i32(const __m128& value) {
  float result;
  _mm_store_ss(&result, _mm_shuffle_ps(value, value, _MM_SHUFFLE(N, N, N, N)));
  return std::bit_cast<int32_t>(result);
}

template <int N>
double m128_f64(const __m128d& value) {
  double result;
  _mm_store_sd(&result, _mm_shuffle_pd(value, value, _MM_SHUFFLE2(N, N)));
  return result;
}

template <int N>
double m128_f64(const __m128& value) {
  return m128_f64<N>(_mm_castps_pd(value));
}

template <int N>
int64_t m128_i64(const __m128d& value) {
  double result;
  _mm_store_sd(&result, _mm_shuffle_pd(value, value, _MM_SHUFFLE2(N, N)));
  return std::bit_cast<int64_t>(result);
}

template <int N>
int64_t m128_i64(const __m128& value) {
  return m128_i64<N>(_mm_castps_pd(value));
}
#endif

inline uint16_t float_to_xenos_half(
    float value, bool preserve_denormal = false, bool round_to_nearest_even = false) {
  const uint32_t integer_value = std::bit_cast<uint32_t>(value);
  const uint32_t abs_value = integer_value & 0x7FFFFFFFu;
  uint32_t result;
  if (abs_value >= 0x47FFE000u) {
    result = 0x7FFFu;
  } else {
    if (abs_value < 0x38800000u) {
      if (preserve_denormal) {
        const uint32_t shift = std::min(uint32_t(113u - (abs_value >> 23u)), uint32_t(24u));
        result = (0x800000u | (abs_value & 0x7FFFFFu)) >> shift;
      } else {
        result = 0u;
      }
    } else {
      result = abs_value + 0xC8000000u;
    }
    if (round_to_nearest_even) {
      result += 0xFFFu + ((result >> 13u) & 1u);
    }
    result = (result >> 13u) & 0x7FFFu;
  }
  return static_cast<uint16_t>(result | ((integer_value & 0x80000000u) >> 16u));
}

inline float xenos_half_to_float(uint16_t value, bool preserve_denormal = false) {
  uint32_t mantissa = value & 0x3FFu;
  uint32_t exponent = (value >> 10u) & 0x1Fu;
  if (!exponent) {
    if (!preserve_denormal) {
      mantissa = 0;
    } else if (mantissa) {
      const uint32_t mantissa_lzcnt = rex::lzcnt(mantissa) - (32u - 11u);
      exponent = uint32_t(1 - int32_t(mantissa_lzcnt));
      mantissa = (mantissa << mantissa_lzcnt) & 0x3FFu;
    }
    if (!mantissa) {
      exponent = uint32_t(-112);
    }
  }
  const uint32_t result =
      (uint32_t(value & 0x8000u) << 16u) | ((exponent + 112u) << 23u) | (mantissa << 13u);
  return std::bit_cast<float>(result);
}

template <typename T>
inline T sat_add(T a, T b) {
  using unsigned_type = typename std::make_unsigned<T>::type;
  unsigned_type result = unsigned_type(a) + unsigned_type(b);
  if (std::is_unsigned<T>::value) {
    result |= unsigned_type(-static_cast<typename std::make_signed<T>::type>(result < unsigned_type(a)));
  } else {
    const unsigned_type overflowed =
        (unsigned_type(a) >> (sizeof(T) * 8 - 1)) + std::numeric_limits<T>::max();
    if (T((overflowed ^ unsigned_type(b)) | ~(unsigned_type(b) ^ result)) >= 0) {
      result = overflowed;
    }
  }
  return T(result);
}

template <typename T>
inline T sat_sub(T a, T b) {
  using unsigned_type = typename std::make_unsigned<T>::type;
  unsigned_type result = unsigned_type(a) - unsigned_type(b);
  if (std::is_unsigned<T>::value) {
    result &= unsigned_type(-static_cast<typename std::make_signed<T>::type>(result <= unsigned_type(a)));
  } else {
    const unsigned_type overflowed =
        (unsigned_type(a) >> (sizeof(T) * 8 - 1)) + std::numeric_limits<T>::max();
    if (T((overflowed ^ unsigned_type(b)) & (overflowed ^ result)) < 0) {
      result = overflowed;
    }
  }
  return T(result);
}

}  // namespace rex
