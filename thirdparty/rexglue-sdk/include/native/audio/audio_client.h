// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

#include <native/audio/audio_clock.h>

namespace rex::audio {

class AudioDriver;

constexpr size_t kMaximumAudioClientCount = 8;
constexpr uint32_t kRenderDriverTicSamplesPerFrame = 256;
constexpr uint32_t kAudioFrameChannelCount = 6;
constexpr uint32_t kAudioFrameSampleRate = 48000;
constexpr uint32_t kAudioFrameTotalSamples =
    kAudioFrameChannelCount * kRenderDriverTicSamplesPerFrame;

struct AudioDriverTelemetry {
  uint32_t submitted_frames{0};
  uint32_t consumed_frames{0};
  uint32_t underrun_count{0};
  uint32_t silence_injections{0};
  uint32_t queued_depth{0};
  uint32_t peak_queued_depth{0};
  uint32_t dropped_frames{0};
  uint32_t malformed_frames{0};
  uint32_t callback_dispatch_count{0};
  uint64_t last_submit_ticks{0};
  uint64_t last_consume_ticks{0};
  uint64_t last_callback_request_ticks{0};
  uint32_t callback_throttle_count{0};
  uint32_t callback_empty_count{0};
  uint32_t last_callback_produced_frames{0};
  uint32_t peak_callback_produced_frames{0};
  uint32_t last_callback_duration_us{0};
  uint32_t peak_callback_duration_us{0};
};

struct AudioFrame {
  uint32_t source_client_id{0};
  uint64_t sequence_number{0};
  uint32_t guest_submit_ptr{0};
  uint32_t sample_count{kRenderDriverTicSamplesPerFrame};
  uint32_t channel_count{kAudioFrameChannelCount};
  uint32_t sample_rate{kAudioFrameSampleRate};
  bool is_silence{false};
  bool is_discontinuity{false};
  bool is_malformed{false};
  std::array<uint32_t, kAudioFrameTotalSamples> guest_frame_words{};
};

struct AudioClientState {
  bool in_use{false};
  size_t client_index{0};
  uint32_t callback{0};
  uint32_t callback_arg{0};
  uint32_t wrapped_callback_arg{0};
  uint64_t next_sequence_number{1};
  uint64_t queued_played_frames{0};
  AudioDriverTelemetry telemetry{};
  AudioClock clock{};
  AudioDriver* driver{nullptr};
  std::deque<AudioFrame> queued_frames{};
  AudioClock::time_point first_callback_dispatch_time{};
  AudioClock::time_point last_callback_dispatch_time{};
};

}  // namespace rex::audio
