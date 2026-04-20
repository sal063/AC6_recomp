// Native audio runtime
// Part of the AC6 Recompilation project

#include <native/audio/render_driver_frame_layout.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#include <native/math.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/types.h>

REXCVAR_DEFINE_STRING(audio_render_driver_layout, "auto", "Audio",
                      "Layout for XAudio render-driver frames: auto, planar, or interleaved")
    .allowed({"auto", "planar", "interleaved"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::audio::conversion {
namespace {

std::atomic<int> g_detected_layout{0};

float DecodeSanitizedSample(const float* input, const size_t ch_sample_count, const size_t sample,
                            const size_t channel, const RenderDriverFrameLayout layout) {
  const size_t index = layout == RenderDriverFrameLayout::kInterleaved
                           ? (sample * 6) + channel
                           : (channel * ch_sample_count) + sample;
  float value = rex::byte_swap(input[index]);
  if (!std::isfinite(value)) {
    return 0.0f;
  }
  return rex::clamp_float(value, -1.0f, 1.0f);
}

struct LayoutScore {
  double continuity = 0.0;
  double energy = 0.0;
};

struct LayoutDetectionResult {
  RenderDriverFrameLayout layout = RenderDriverFrameLayout::kPlanar;
  bool cacheable = false;
};

LayoutScore ScoreLayout(const float* input, const size_t ch_sample_count,
                        const RenderDriverFrameLayout layout) {
  const size_t inspect_samples = std::min<size_t>(ch_sample_count, 64);
  LayoutScore score{};
  if (!input || inspect_samples < 2) {
    return score;
  }

  for (size_t channel = 0; channel < 6; ++channel) {
    float previous = DecodeSanitizedSample(input, ch_sample_count, 0, channel, layout);
    score.energy += std::fabs(previous);
    for (size_t sample = 1; sample < inspect_samples; ++sample) {
      const float current =
          DecodeSanitizedSample(input, ch_sample_count, sample, channel, layout);
      score.continuity += std::fabs(current - previous);
      score.energy += std::fabs(current);
      previous = current;
    }
  }
  return score;
}

LayoutDetectionResult DetectFrameLayout(const float* input, const size_t ch_sample_count) {
  const LayoutScore planar = ScoreLayout(input, ch_sample_count, RenderDriverFrameLayout::kPlanar);
  const LayoutScore interleaved =
      ScoreLayout(input, ch_sample_count, RenderDriverFrameLayout::kInterleaved);

  if (std::max(planar.energy, interleaved.energy) < 0.01) {
    return {};
  }

  constexpr double kDecisionRatio = 0.75;
  if (planar.continuity * kDecisionRatio < interleaved.continuity) {
    return {RenderDriverFrameLayout::kPlanar, true};
  }
  if (interleaved.continuity * kDecisionRatio < planar.continuity) {
    return {RenderDriverFrameLayout::kInterleaved, true};
  }

  return {planar.continuity <= interleaved.continuity ? RenderDriverFrameLayout::kPlanar
                                                      : RenderDriverFrameLayout::kInterleaved,
          false};
}

}  // namespace

RenderDriverFrameLayout ResolveRenderDriverFrameLayout(const float* input,
                                                       const size_t ch_sample_count) {
  const auto& configured_layout = REXCVAR_GET(audio_render_driver_layout);
  if (configured_layout == "planar") {
    return RenderDriverFrameLayout::kPlanar;
  }
  if (configured_layout == "interleaved") {
    return RenderDriverFrameLayout::kInterleaved;
  }

  const int cached_layout = g_detected_layout.load(std::memory_order_acquire);
  if (cached_layout == 1) {
    return RenderDriverFrameLayout::kPlanar;
  }
  if (cached_layout == 2) {
    return RenderDriverFrameLayout::kInterleaved;
  }

  const LayoutDetectionResult detection = DetectFrameLayout(input, ch_sample_count);
  if (!detection.cacheable) {
    return detection.layout;
  }

  const RenderDriverFrameLayout detected_layout = detection.layout;
  const int detected_value =
      detected_layout == RenderDriverFrameLayout::kInterleaved ? 2 : 1;
  const int previous =
      g_detected_layout.exchange(detected_value, std::memory_order_acq_rel);
  if (previous == 0) {
    REXAPU_INFO("Audio render-driver layout auto-detected: {}", ToString(detected_layout));
  }
  return detected_layout;
}

const char* ToString(const RenderDriverFrameLayout layout) {
  switch (layout) {
    case RenderDriverFrameLayout::kPlanar:
      return "planar";
    case RenderDriverFrameLayout::kInterleaved:
      return "interleaved";
    default:
      return "unknown";
  }
}

}  // namespace rex::audio::conversion
