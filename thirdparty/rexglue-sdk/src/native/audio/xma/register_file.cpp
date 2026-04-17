/**
 * ReXGlue native audio runtime
 * Part of the AC6 Recompilation project
 */

#include <cstring>

#include <native/audio/xma/register_file.h>
#include <rex/math.h>

namespace rex::audio {

XmaRegisterFile::XmaRegisterFile() {
  std::memset(values, 0, sizeof(values));
}

const XmaRegisterInfo* XmaRegisterFile::GetRegisterInfo(uint32_t index) {
  switch (index) {
#define XE_XMA_REGISTER(index, name)          \
  case index: {                               \
    static const XmaRegisterInfo reg_info = { \
        #name,                                \
    };                                        \
    return &reg_info;                         \
  }
#include <native/audio/xma/register_table.inc>
#undef XE_XMA_REGISTER
    default:
      return nullptr;
  }
}

}  //  namespace rex::audio
