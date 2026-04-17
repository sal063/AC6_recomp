// Native runtime - dbg win
// Part of the AC6 Recompilation native foundation
#include "platform_win.h"

#include <native/dbg.h>
#include <native/string/buffer.h>

namespace rex::debug {

bool IsDebuggerAttached() {
  return IsDebuggerPresent() ? true : false;
}

void Break() {
  __debugbreak();
}

namespace detail {
void DebugPrint(const char* s) {
  OutputDebugStringA(s);
}
}  // namespace detail

}  // namespace rex::debug
