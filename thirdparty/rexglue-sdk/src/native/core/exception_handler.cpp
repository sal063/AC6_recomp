#include <native/exception_handler.h>

namespace rex::arch {

// Based on VIXL Instruction::IsLoad and IsStore.
// https://github.com/Linaro/vixl/blob/d48909dd0ac62197edb75d26ed50927e4384a199/src/aarch64/instructions-aarch64.cc#L484
//
// Copyright 2015, VIXL authors - All rights reserved.
// See VIXL BSD-style license for redistribution terms.
bool IsArm64LoadPrefetchStore(uint32_t instruction, bool& is_store_out) {
  if ((instruction & kArm64LoadLiteralFMask) == kArm64LoadLiteralFixed) {
    return true;
  }
  if ((instruction & kArm64LoadStoreAnyFMask) != kArm64LoadStoreAnyFixed) {
    return false;
  }
  if ((instruction & kArm64LoadStorePairAnyFMask) == kArm64LoadStorePairAnyFixed) {
    is_store_out = !(instruction & kArm64LoadStorePairLoadBit);
    return true;
  }
  switch (Arm64LoadStoreOp(instruction & kArm64LoadStoreMask)) {
    case Arm64LoadStoreOp::kLDRB_w:
    case Arm64LoadStoreOp::kLDRH_w:
    case Arm64LoadStoreOp::kLDR_w:
    case Arm64LoadStoreOp::kLDR_x:
    case Arm64LoadStoreOp::kLDRSB_x:
    case Arm64LoadStoreOp::kLDRSH_x:
    case Arm64LoadStoreOp::kLDRSW_x:
    case Arm64LoadStoreOp::kLDRSB_w:
    case Arm64LoadStoreOp::kLDRSH_w:
    case Arm64LoadStoreOp::kLDR_b:
    case Arm64LoadStoreOp::kLDR_h:
    case Arm64LoadStoreOp::kLDR_s:
    case Arm64LoadStoreOp::kLDR_d:
    case Arm64LoadStoreOp::kLDR_q:
    case Arm64LoadStoreOp::kPRFM:
      is_store_out = false;
      return true;
    case Arm64LoadStoreOp::kSTRB_w:
    case Arm64LoadStoreOp::kSTRH_w:
    case Arm64LoadStoreOp::kSTR_w:
    case Arm64LoadStoreOp::kSTR_x:
    case Arm64LoadStoreOp::kSTR_b:
    case Arm64LoadStoreOp::kSTR_h:
    case Arm64LoadStoreOp::kSTR_s:
    case Arm64LoadStoreOp::kSTR_d:
    case Arm64LoadStoreOp::kSTR_q:
      is_store_out = true;
      return true;
    default:
      return false;
  }
}

}  // namespace rex::arch
