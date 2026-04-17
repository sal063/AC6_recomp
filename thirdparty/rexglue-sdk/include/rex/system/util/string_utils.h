/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#pragma once

#include <string>
#include <string_view>

#include <rex/memory.h>
#include <rex/string.h>
#include <rex/system/xio.h>
#include <rex/system/xtypes.h>

namespace rex::system::util {

inline std::string_view TranslateAnsiString(const memory::Memory* memory,
                                            const X_ANSI_STRING* ansi_string) {
  if (!ansi_string || !ansi_string->length) {
    return "";
  }
  return std::string_view(memory->TranslateVirtual<const char*>(ansi_string->pointer),
                          ansi_string->length);
}

inline std::string TranslateAnsiPath(const memory::Memory* memory,
                                     const X_ANSI_STRING* ansi_string) {
  return rex::string::trim_string(std::string(TranslateAnsiString(memory, ansi_string)));
}

inline std::string_view TranslateAnsiStringAddress(const memory::Memory* memory,
                                                   uint32_t guest_address) {
  if (!guest_address) {
    return "";
  }
  return TranslateAnsiString(memory, memory->TranslateVirtual<const X_ANSI_STRING*>(guest_address));
}

inline std::u16string TranslateUnicodeString(const memory::Memory* memory,
                                             const X_UNICODE_STRING* unicode_string) {
  if (!unicode_string) {
    return u"";
  }
  uint16_t length = unicode_string->length;
  if (!length) {
    return u"";
  }

  auto src = memory->TranslateVirtual<const uint16_t*>(unicode_string->pointer);
  std::u16string result;
  result.reserve(length);
  for (uint16_t i = 0; i < length / 2; ++i) {
    result.push_back(rex::byte_swap(src[i]));
  }
  return result;
}

}  // namespace rex::system::util
