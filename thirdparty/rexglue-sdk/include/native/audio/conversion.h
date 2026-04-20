// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <cmath>
#include <cstdint>

#include <native/audio/render_driver_frame_layout.h>
#include <rex/platform.h>
#include <rex/types.h>

namespace rex::audio::conversion {

inline constexpr float kStereoDownmixCenterGain = 0.70710678f;
inline constexpr float kStereoDownmixSurroundGain = 0.5f;
inline constexpr float kStereoDownmixLfeGain = 0.0f;
inline constexpr float kStereoDownmixPeakHeadroom = 0.92f;
inline constexpr float kStereoDownmixNormalize =
    1.0f / (1.0f + kStereoDownmixCenterGain + kStereoDownmixSurroundGain +
            kStereoDownmixLfeGain);

inline float SanitizeGuestAudioSample(float sample) {
  if (!std::isfinite(sample)) {
    return 0.0f;
  }
  if (sample > 1.0f) {
    return 1.0f;
  }
  if (sample < -1.0f) {
    return -1.0f;
  }
  return sample;
}
#if REX_ARCH_AMD64
inline __m128 SanitizeGuestAudioSamples(__m128 samples) {
  const __m128 ordered_mask = _mm_cmpord_ps(samples, samples);
  const __m128 min_sample = _mm_set1_ps(-1.0f);
  const __m128 max_sample = _mm_set1_ps(1.0f);
  samples = _mm_and_ps(samples, ordered_mask);
  return _mm_min_ps(max_sample, _mm_max_ps(min_sample, samples));
}
#endif

#if REX_ARCH_AMD64
inline void sequential_6_BE_to_interleaved_6_LE(float* output, const float* input,
                                                size_t ch_sample_count) {
  const uint32_t* in = reinterpret_cast<const uint32_t*>(input);
  uint32_t* out = reinterpret_cast<uint32_t*>(output);
  const __m128i byte_swap_shuffle =
      _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    __m128i sample0 =
        _mm_set_epi32(in[3 * ch_sample_count + sample], in[2 * ch_sample_count + sample],
                      in[1 * ch_sample_count + sample], in[0 * ch_sample_count + sample]);
    uint32_t sample1 = in[4 * ch_sample_count + sample];
    uint32_t sample2 = in[5 * ch_sample_count + sample];
    sample0 = _mm_shuffle_epi8(sample0, byte_swap_shuffle);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[sample * 6]), sample0);
    sample1 = rex::byte_swap(sample1);
    out[sample * 6 + 4] = sample1;
    sample2 = rex::byte_swap(sample2);
    out[sample * 6 + 5] = sample2;
  }
}

inline void sequential_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                size_t ch_sample_count) {
  assert_true(ch_sample_count % 4 == 0);

  const __m128i byte_swap_shuffle =
      _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
  const __m128 center_gain = _mm_set1_ps(kStereoDownmixCenterGain);
  const __m128 surround_gain = _mm_set1_ps(kStereoDownmixSurroundGain);
  const __m128 lfe_gain = _mm_set1_ps(kStereoDownmixLfeGain);
  const __m128 normalize = _mm_set1_ps(kStereoDownmixNormalize);
  const __m128 peak_headroom = _mm_set1_ps(kStereoDownmixPeakHeadroom);
  const __m128 sign_mask = _mm_set1_ps(-0.0f);

  // Use a dialogue-forward stereo fold-down. The old mapping mixed rears too
  // heavily for cutscenes and could sound smeared on stereo playback.
  for (size_t sample = 0; sample < ch_sample_count; sample += 4) {
    __m128 fl = _mm_loadu_ps(&input[0 * ch_sample_count + sample]);
    __m128 fr = _mm_loadu_ps(&input[1 * ch_sample_count + sample]);
    __m128 fc = _mm_loadu_ps(&input[2 * ch_sample_count + sample]);
    __m128 lf = _mm_loadu_ps(&input[3 * ch_sample_count + sample]);
    __m128 bl = _mm_loadu_ps(&input[4 * ch_sample_count + sample]);
    __m128 br = _mm_loadu_ps(&input[5 * ch_sample_count + sample]);
    fl = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(fl), byte_swap_shuffle));
    fr = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(fr), byte_swap_shuffle));
    fc = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(fc), byte_swap_shuffle));
    lf = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(lf), byte_swap_shuffle));
    bl = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(bl), byte_swap_shuffle));
    br = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(br), byte_swap_shuffle));
    fl = SanitizeGuestAudioSamples(fl);
    fr = SanitizeGuestAudioSamples(fr);
    fc = SanitizeGuestAudioSamples(fc);
    lf = SanitizeGuestAudioSamples(lf);
    bl = SanitizeGuestAudioSamples(bl);
    br = SanitizeGuestAudioSamples(br);

    __m128 left = _mm_add_ps(
        _mm_add_ps(fl, _mm_mul_ps(fc, center_gain)),
        _mm_add_ps(_mm_mul_ps(bl, surround_gain), _mm_mul_ps(lf, lfe_gain)));
    __m128 right = _mm_add_ps(
        _mm_add_ps(fr, _mm_mul_ps(fc, center_gain)),
        _mm_add_ps(_mm_mul_ps(br, surround_gain), _mm_mul_ps(lf, lfe_gain)));
    left = _mm_mul_ps(left, normalize);
    right = _mm_mul_ps(right, normalize);

    // Apply a lightweight linked limiter instead of hard clipping. Mission
    // mixes can stack enough combat layers to hit repeated peaks, which sounds
    // like constant crackling when clipped.
    const __m128 left_abs = _mm_andnot_ps(sign_mask, left);
    const __m128 right_abs = _mm_andnot_ps(sign_mask, right);
    const __m128 max_abs = _mm_max_ps(left_abs, right_abs);
    const __m128 limiter_denominator = _mm_max_ps(max_abs, peak_headroom);
    const __m128 limiter_scale = _mm_div_ps(peak_headroom, limiter_denominator);
    left = _mm_mul_ps(left, limiter_scale);
    right = _mm_mul_ps(right, limiter_scale);

    _mm_storeu_ps(&output[sample * 2], _mm_unpacklo_ps(left, right));
    _mm_storeu_ps(&output[(sample + 2) * 2], _mm_unpackhi_ps(left, right));
  }
}

inline void interleaved_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                 size_t ch_sample_count) {
  for (size_t sample = 0; sample < ch_sample_count; ++sample) {
    float fl = rex::byte_swap(input[sample * 6 + 0]);
    float fr = rex::byte_swap(input[sample * 6 + 1]);
    float fc = rex::byte_swap(input[sample * 6 + 2]);
    float lf = rex::byte_swap(input[sample * 6 + 3]);
    float bl = rex::byte_swap(input[sample * 6 + 4]);
    float br = rex::byte_swap(input[sample * 6 + 5]);
    fl = SanitizeGuestAudioSample(fl);
    fr = SanitizeGuestAudioSample(fr);
    fc = SanitizeGuestAudioSample(fc);
    lf = SanitizeGuestAudioSample(lf);
    bl = SanitizeGuestAudioSample(bl);
    br = SanitizeGuestAudioSample(br);
    float left = (fl + (fc * kStereoDownmixCenterGain) + (bl * kStereoDownmixSurroundGain) +
                  (lf * kStereoDownmixLfeGain)) *
                 kStereoDownmixNormalize;
    float right = (fr + (fc * kStereoDownmixCenterGain) + (br * kStereoDownmixSurroundGain) +
                   (lf * kStereoDownmixLfeGain)) *
                  kStereoDownmixNormalize;
    float max_abs = left >= 0.0f ? left : -left;
    float right_abs = right >= 0.0f ? right : -right;
    if (right_abs > max_abs) {
      max_abs = right_abs;
    }
    if (max_abs > kStereoDownmixPeakHeadroom) {
      const float limiter_scale = kStereoDownmixPeakHeadroom / max_abs;
      left *= limiter_scale;
      right *= limiter_scale;
    }
    output[sample * 2] = left;
    output[sample * 2 + 1] = right;
  }
}

inline void render_driver_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                    size_t ch_sample_count) {
  switch (ResolveRenderDriverFrameLayout(input, ch_sample_count)) {
    case RenderDriverFrameLayout::kInterleaved:
      interleaved_6_BE_to_interleaved_2_LE(output, input, ch_sample_count);
      return;
    case RenderDriverFrameLayout::kPlanar:
    default:
      sequential_6_BE_to_interleaved_2_LE(output, input, ch_sample_count);
      return;
  }
}
#else
inline void sequential_6_BE_to_interleaved_6_LE(float* output, const float* input,
                                                size_t ch_sample_count) {
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    for (size_t channel = 0; channel < 6; channel++) {
      output[sample * 6 + channel] = rex::byte_swap(input[channel * ch_sample_count + sample]);
    }
  }
}
inline void sequential_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                size_t ch_sample_count) {
  // Default 5.1 channel mapping is fl, fr, fc, lf, bl, br
  // https://docs.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-default-channel-mapping
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    float fl = rex::byte_swap(input[0 * ch_sample_count + sample]);
    float fr = rex::byte_swap(input[1 * ch_sample_count + sample]);
    float fc = rex::byte_swap(input[2 * ch_sample_count + sample]);
    float lf = rex::byte_swap(input[3 * ch_sample_count + sample]);
    float bl = rex::byte_swap(input[4 * ch_sample_count + sample]);
    float br = rex::byte_swap(input[5 * ch_sample_count + sample]);
    fl = SanitizeGuestAudioSample(fl);
    fr = SanitizeGuestAudioSample(fr);
    fc = SanitizeGuestAudioSample(fc);
    lf = SanitizeGuestAudioSample(lf);
    bl = SanitizeGuestAudioSample(bl);
    br = SanitizeGuestAudioSample(br);
    float left = (fl + (fc * kStereoDownmixCenterGain) + (bl * kStereoDownmixSurroundGain) +
                  (lf * kStereoDownmixLfeGain)) *
                 kStereoDownmixNormalize;
    float right = (fr + (fc * kStereoDownmixCenterGain) + (br * kStereoDownmixSurroundGain) +
                   (lf * kStereoDownmixLfeGain)) *
                  kStereoDownmixNormalize;
    float max_abs = left >= 0.0f ? left : -left;
    float right_abs = right >= 0.0f ? right : -right;
    if (right_abs > max_abs) {
      max_abs = right_abs;
    }
    if (max_abs > kStereoDownmixPeakHeadroom) {
      const float limiter_scale = kStereoDownmixPeakHeadroom / max_abs;
      left *= limiter_scale;
      right *= limiter_scale;
    }
    output[sample * 2] = left;
    output[sample * 2 + 1] = right;
  }
}

inline void interleaved_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                 size_t ch_sample_count) {
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    float fl = rex::byte_swap(input[sample * 6 + 0]);
    float fr = rex::byte_swap(input[sample * 6 + 1]);
    float fc = rex::byte_swap(input[sample * 6 + 2]);
    float lf = rex::byte_swap(input[sample * 6 + 3]);
    float bl = rex::byte_swap(input[sample * 6 + 4]);
    float br = rex::byte_swap(input[sample * 6 + 5]);
    fl = SanitizeGuestAudioSample(fl);
    fr = SanitizeGuestAudioSample(fr);
    fc = SanitizeGuestAudioSample(fc);
    lf = SanitizeGuestAudioSample(lf);
    bl = SanitizeGuestAudioSample(bl);
    br = SanitizeGuestAudioSample(br);
    float left = (fl + (fc * kStereoDownmixCenterGain) + (bl * kStereoDownmixSurroundGain) +
                  (lf * kStereoDownmixLfeGain)) *
                 kStereoDownmixNormalize;
    float right = (fr + (fc * kStereoDownmixCenterGain) + (br * kStereoDownmixSurroundGain) +
                   (lf * kStereoDownmixLfeGain)) *
                  kStereoDownmixNormalize;
    float max_abs = left >= 0.0f ? left : -left;
    float right_abs = right >= 0.0f ? right : -right;
    if (right_abs > max_abs) {
      max_abs = right_abs;
    }
    if (max_abs > kStereoDownmixPeakHeadroom) {
      const float limiter_scale = kStereoDownmixPeakHeadroom / max_abs;
      left *= limiter_scale;
      right *= limiter_scale;
    }
    output[sample * 2] = left;
    output[sample * 2 + 1] = right;
  }
}

inline void render_driver_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                    size_t ch_sample_count) {
  switch (ResolveRenderDriverFrameLayout(input, ch_sample_count)) {
    case RenderDriverFrameLayout::kInterleaved:
      interleaved_6_BE_to_interleaved_2_LE(output, input, ch_sample_count);
      return;
    case RenderDriverFrameLayout::kPlanar:
    default:
      sequential_6_BE_to_interleaved_2_LE(output, input, ch_sample_count);
      return;
  }
}
#endif

}  // namespace rex::audio::conversion
