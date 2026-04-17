/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <string>
#include <string_view>

#include <rex/logging.h>
#include <rex/ppc/function.h>
#include <rex/ppc/types.h>
#include <rex/system/kernel_state.h>

namespace rex::kernel::xam {

void OutputDebugStringA_entry(ppc_pchar_t string) {
  if (string) {
    REXKRNL_INFO("OutputDebugStringA: {}", string.value());
  }
}

void OutputDebugStringW_entry(ppc_pchar16_t string) {
  if (string) {
    std::u16string_view sv = string.value();
    std::string utf8;
    utf8.reserve(sv.size());
    for (char16_t c : sv) {
      if (c < 0x80) {
        utf8.push_back(static_cast<char>(c));
      } else {
        utf8.push_back('?');
      }
    }
    REXKRNL_INFO("OutputDebugStringW: {}", utf8);
  }
}

void RtlOutputDebugString_entry(ppc_pchar_t string) {
  if (string) {
    REXKRNL_INFO("RtlOutputDebugString: {}", string.value());
  }
}

void RtlDebugTrace_entry(ppc_pchar_t string) {
  if (string) {
    REXKRNL_INFO("RtlDebugTrace: {}", string.value());
  }
}

}  // namespace rex::kernel::xam

XAM_EXPORT(__imp__OutputDebugStringA, rex::kernel::xam::OutputDebugStringA_entry)
XAM_EXPORT(__imp__OutputDebugStringW, rex::kernel::xam::OutputDebugStringW_entry)
XAM_EXPORT(__imp__RtlOutputDebugString, rex::kernel::xam::RtlOutputDebugString_entry)
XAM_EXPORT(__imp__RtlDebugTrace, rex::kernel::xam::RtlDebugTrace_entry)
