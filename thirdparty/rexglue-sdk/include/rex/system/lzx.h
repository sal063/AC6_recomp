/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace rex {
struct xex2_delta_patch;
}

int lzx_decompress(const void* lzx_data, size_t lzx_len, void* dest, size_t dest_len,
                   uint32_t window_size, void* window_data, size_t window_data_len);

int lzxdelta_apply_patch(rex::xex2_delta_patch* patch, size_t patch_len, uint32_t window_size,
                         void* dest);
