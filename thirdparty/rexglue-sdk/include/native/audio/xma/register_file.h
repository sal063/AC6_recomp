// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <cstdint>
#include <cstdlib>

namespace rex::audio {

struct XmaRegister {
#define XE_XMA_REGISTER(index, name) static const uint32_t name = index;
#include <native/audio/xma/register_table.inc>
#undef XE_XMA_REGISTER
};

struct XmaRegisterInfo {
  const char* name;
};

class XmaRegisterFile {
 public:
  XmaRegisterFile();

  static const XmaRegisterInfo* GetRegisterInfo(uint32_t index);

  static const size_t kRegisterCount = (0xFFFF + 1) / 4;
  uint32_t values[kRegisterCount];

  uint32_t operator[](uint32_t reg) const { return values[reg]; }
  uint32_t& operator[](uint32_t reg) { return values[reg]; }
};

}  // namespace rex::audio
