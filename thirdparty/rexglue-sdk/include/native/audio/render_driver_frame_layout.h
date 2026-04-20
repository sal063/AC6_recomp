// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <cstddef>

namespace rex::audio::conversion {

enum class RenderDriverFrameLayout : unsigned char {
  kPlanar,
  kInterleaved,
};

RenderDriverFrameLayout ResolveRenderDriverFrameLayout(const float* input, size_t ch_sample_count);
const char* ToString(RenderDriverFrameLayout layout);

}  // namespace rex::audio::conversion
