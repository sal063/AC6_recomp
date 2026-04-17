#pragma once

#include <cstddef>

namespace rex::literals {

constexpr size_t operator""_KiB(unsigned long long value) {
  return 1024ULL * value;
}

constexpr size_t operator""_MiB(unsigned long long value) {
  return 1024_KiB * value;
}

constexpr size_t operator""_GiB(unsigned long long value) {
  return 1024_MiB * value;
}

constexpr size_t operator""_TiB(unsigned long long value) {
  return 1024_GiB * value;
}

constexpr size_t operator""_PiB(unsigned long long value) {
  return 1024_TiB * value;
}

}  // namespace rex::literals
