/**
 * ReXGlue native audio runtime
 * Part of the AC6 Recompilation project
 */

#include <native/audio/xma/xma_decoder_backend.h>

#include <native/audio/xma/context.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/platform.h>

extern "C" {
#if REX_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4101 4244 5033)
#endif
#include "libavcodec/avcodec.h"
#include "libavutil/error.h"
#if REX_COMPILER_MSVC
#pragma warning(pop)
#endif
}  // extern "C"

namespace rex::audio::xma {

namespace {

void ConvertFrameToRawBuffer(const uint8_t** samples, const bool is_two_channel,
                             std::span<uint8_t> output_buffer) {
  constexpr float scale = (1 << 15) - 1;
  auto* out = reinterpret_cast<int16_t*>(output_buffer.data());

#if REX_ARCH_AMD64
  static_assert(XmaContext::kSamplesPerFrame % 8 == 0);
  const auto* in_channel_0 = reinterpret_cast<const float*>(samples[0]);
  const __m128 scale_mm = _mm_set1_ps(scale);
  if (is_two_channel) {
    const auto* in_channel_1 = reinterpret_cast<const float*>(samples[1]);
    const __m128i shufmask = _mm_set_epi8(14, 15, 6, 7, 12, 13, 4, 5, 10, 11, 2, 3, 8, 9, 0, 1);
    for (uint32_t i = 0; i < XmaContext::kSamplesPerFrame; i += 4) {
      __m128 in_mm0 = _mm_loadu_ps(&in_channel_0[i]);
      __m128 in_mm1 = _mm_loadu_ps(&in_channel_1[i]);
      in_mm0 = _mm_mul_ps(in_mm0, scale_mm);
      in_mm1 = _mm_mul_ps(in_mm1, scale_mm);
      __m128i out_mm0 = _mm_cvtps_epi32(in_mm0);
      __m128i out_mm1 = _mm_cvtps_epi32(in_mm1);
      __m128i out_mm = _mm_packs_epi32(out_mm0, out_mm1);
      out_mm = _mm_shuffle_epi8(out_mm, shufmask);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i * 2]), out_mm);
    }
  } else {
    const __m128i shufmask = _mm_set_epi8(14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1);
    for (uint32_t i = 0; i < XmaContext::kSamplesPerFrame; i += 8) {
      __m128 in_mm0 = _mm_loadu_ps(&in_channel_0[i]);
      __m128 in_mm1 = _mm_loadu_ps(&in_channel_0[i + 4]);
      in_mm0 = _mm_mul_ps(in_mm0, scale_mm);
      in_mm1 = _mm_mul_ps(in_mm1, scale_mm);
      __m128i out_mm0 = _mm_cvtps_epi32(in_mm0);
      __m128i out_mm1 = _mm_cvtps_epi32(in_mm1);
      __m128i out_mm = _mm_packs_epi32(out_mm0, out_mm1);
      out_mm = _mm_shuffle_epi8(out_mm, shufmask);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i]), out_mm);
    }
  }
#else
  uint32_t o = 0;
  for (uint32_t i = 0; i < XmaContext::kSamplesPerFrame; i++) {
    for (uint32_t j = 0; j <= uint32_t(is_two_channel); j++) {
      auto in = reinterpret_cast<const float*>(samples[j]);
      float scaled_sample = rex::clamp_float(in[i], -1.0f, 1.0f) * scale;
      auto sample = static_cast<int16_t>(scaled_sample);
      out[o++] = rex::byte_swap(sample);
    }
  }
#endif
}

class FfmpegXmaDecoderBackend final : public XmaDecoderBackend {
 public:
  FfmpegXmaDecoderBackend() {
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    codec_ = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
    if (!packet_ || !frame_ || !codec_) {
      REXAPU_ERROR("XMA: FFmpeg decoder backend initialization failed");
    }
  }

  ~FfmpegXmaDecoderBackend() override {
    if (context_) {
      avcodec_free_context(&context_);
    }
    if (frame_) {
      av_frame_free(&frame_);
    }
    if (packet_) {
      av_packet_free(&packet_);
    }
  }

  bool IsAvailable() const override {
    return packet_ != nullptr && frame_ != nullptr && codec_ != nullptr;
  }

  bool DecodePacket(const XmaDecodeRequest& request,
                    std::span<uint8_t> output_frame) override {
    if (!IsAvailable() || request.packet_data.empty() || request.sample_rate == 0) {
      return false;
    }

    const size_t required_output_bytes =
        XmaContext::kBytesPerFrameChannel * (request.is_two_channel ? 2u : 1u);
    if (output_frame.size() < required_output_bytes) {
      REXAPU_ERROR("XMA: output frame buffer too small ({} < {})", output_frame.size(),
                   required_output_bytes);
      return false;
    }

    if (!EnsureConfigured(request.sample_rate, request.is_two_channel)) {
      return false;
    }

    av_packet_unref(packet_);
    packet_->data = const_cast<uint8_t*>(request.packet_data.data());
    packet_->size = static_cast<int>(request.packet_data.size());

    int ret = avcodec_send_packet(context_, packet_);
    if (ret < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      REXAPU_ERROR("XMA: Error sending packet for decoding: {} ({})", errbuf, ret);
      return false;
    }

    ret = avcodec_receive_frame(context_, frame_);
    if (ret == AVERROR(EAGAIN)) {
      return false;
    }
    if (ret < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      REXAPU_ERROR("XMA: Error during decoding: {} ({})", errbuf, ret);
      return false;
    }

    ConvertFrameToRawBuffer(reinterpret_cast<const uint8_t**>(&frame_->data),
                            request.is_two_channel, output_frame.first(required_output_bytes));
    return true;
  }

 private:
  bool EnsureConfigured(const uint32_t sample_rate, const bool is_two_channel) {
    const int channels = is_two_channel ? 2 : 1;
    if (context_ && avcodec_is_open(context_) && configured_sample_rate_ == sample_rate &&
        configured_channels_ == channels) {
      return true;
    }

    if (context_) {
      avcodec_free_context(&context_);
    }

    if (configured_sample_rate_ != 0 && configured_channels_ != 0) {
      REXAPU_DEBUG("XMA: reconfiguring decoder rate {} -> {}, channels {} -> {}",
                   configured_sample_rate_, sample_rate, configured_channels_, channels);
    }

    context_ = avcodec_alloc_context3(codec_);
    if (!context_) {
      REXAPU_ERROR("XMA: Couldn't allocate FFmpeg context");
      return false;
    }

    context_->sample_rate = static_cast<int>(sample_rate);
    context_->channels = channels;
    context_->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
    if (avcodec_open2(context_, codec_, nullptr) < 0) {
      REXAPU_ERROR("XMA: Failed to open FFmpeg decoder context");
      avcodec_free_context(&context_);
      return false;
    }

    configured_sample_rate_ = sample_rate;
    configured_channels_ = channels;
    return true;
  }

  AVPacket* packet_{nullptr};
  const AVCodec* codec_{nullptr};
  AVCodecContext* context_{nullptr};
  AVFrame* frame_{nullptr};
  uint32_t configured_sample_rate_{0};
  int configured_channels_{0};
};

}  // namespace

bool NullXmaDecoderBackend::IsAvailable() const {
  return false;
}

bool NullXmaDecoderBackend::DecodePacket([[maybe_unused]] const XmaDecodeRequest& request,
                                         [[maybe_unused]] std::span<uint8_t> output_frame) {
  return false;
}

std::unique_ptr<XmaDecoderBackend> CreateXmaDecoderBackend() {
  auto backend = std::make_unique<FfmpegXmaDecoderBackend>();
  if (backend->IsAvailable()) {
    return backend;
  }
  return std::make_unique<NullXmaDecoderBackend>();
}

}  // namespace rex::audio::xma
