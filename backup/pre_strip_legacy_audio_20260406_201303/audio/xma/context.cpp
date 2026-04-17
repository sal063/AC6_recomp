/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2021 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*
* @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
*/

#include <algorithm>
#include <cstring>

#include <rex/audio/xma/context.h>
#include <rex/audio/xma/decoder.h>
#include <rex/audio/xma/helpers.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/logging.h>
#include <rex/memory/ring_buffer.h>
#include <rex/platform.h>
#include <rex/stream.h>

extern "C" {
#if REX_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4101 4244 5033)
#endif
#include "libavcodec/avcodec.h"
#if REX_COMPILER_MSVC
#pragma warning(pop)
#endif
}  // extern "C"

REXCVAR_DECLARE(bool, ac6_audio_deep_trace);

namespace rex::audio {

using stream::BitStream;

namespace {

bool IsDeepTraceEnabled() {
  return REXCVAR_GET(ac6_audio_deep_trace);
}

}  // namespace

XmaContext::XmaContext()
    : work_completion_event_(rex::thread::Event::CreateAutoResetEvent(false)) {}

XmaContext::~XmaContext() {
  if (av_context_) {
    if (avcodec_is_open(av_context_)) {
      avcodec_close(av_context_);
    }
    av_free(av_context_);
  }
  if (av_frame_) {
    av_frame_free(&av_frame_);
  }
}

int XmaContext::Setup(uint32_t id, memory::Memory* memory, uint32_t guest_ptr) {
  id_ = id;
  memory_ = memory;
  guest_ptr_ = guest_ptr;

  av_packet_ = av_packet_alloc();
  assert_not_null(av_packet_);

  av_codec_ = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
  if (!av_codec_) {
    REXAPU_ERROR("XmaContext {}: Codec not found", id);
    return 1;
  }

  av_context_ = avcodec_alloc_context3(av_codec_);
  if (!av_context_) {
    REXAPU_ERROR("XmaContext {}: Couldn't allocate context", id);
    return 1;
  }

  av_context_->channels = 0;
  av_context_->sample_rate = 0;

  av_frame_ = av_frame_alloc();
  if (!av_frame_) {
    REXAPU_ERROR("XmaContext {}: Couldn't allocate frame", id);
    return 1;
  }

  return 0;
}

bool XmaContext::Work() {
  if (!is_allocated() || !is_enabled()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(lock_);
  set_is_enabled(false);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);
  const XMA_CONTEXT_DATA initial_data = data;

  if (!data.output_buffer_valid) {
    return true;
  }

  memory::RingBuffer output_rb = PrepareOutputRingBuffer(&data);

  if (data.IsConsumeOnlyContext()) {
    if (current_frame_remaining_subframes_ == 0) {
      return true;
    }
    Consume(&output_rb, &data);
    data.output_buffer_write_offset = output_rb.write_offset() / kOutputBytesPerBlock;
    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }

  const uint32_t effective_sdc = std::max(static_cast<uint32_t>(1), data.subframe_decode_count);
  const int32_t minimum_subframe_decode_count =
      static_cast<int32_t>(effective_sdc) + data.output_buffer_padding;

  if (minimum_subframe_decode_count > remaining_subframe_blocks_in_output_buffer_) {
    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }

  while (remaining_subframe_blocks_in_output_buffer_ >= minimum_subframe_decode_count) {
    Decode(&data);
    Consume(&output_rb, &data);

    if (!data.IsAnyInputBufferValid() || data.error_status == 4) {
      if (data.error_status == 4) {
        REXAPU_WARN(
            "XmaContext {}: decode aborted with error_status=4 packet_index={} next_packet={} "
            "read_before={} read_after={} frame_bits={} bits_to_copy={} skip={} "
            "cross_packet={} swapped={}",
            id(), last_packet_index_, last_next_packet_index_, last_input_read_offset_before_,
            last_input_read_offset_after_, last_frame_size_bits_, last_bits_to_copy_,
            last_skip_count_, last_cross_packet_copy_, last_swapped_input_buffer_);
      }
      break;
    }
  }

  data.output_buffer_write_offset = output_rb.write_offset() / kOutputBytesPerBlock;

  if (output_rb.empty()) {
    data.output_buffer_valid = 0;
  }

  StoreContextMerged(data, initial_data, context_ptr);
  return true;
}

void XmaContext::Enable() {
  std::lock_guard<std::mutex> lock(lock_);
  set_is_enabled(true);
}

bool XmaContext::Block(bool poll) {
  if (!lock_.try_lock()) {
    if (poll) {
      return false;
    }
    lock_.lock();
  }
  lock_.unlock();
  return true;
}

void XmaContext::Clear() {
  std::lock_guard<std::mutex> lock(lock_);
  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);
  REXAPU_DEBUG(
      "XmaContext {} guest-state before reset: current_buffer={} valid0={} valid1={} "
      "output_valid={} input_read_offset={} output_read_offset={} output_write_offset={} "
      "error_status={}",
      id(), static_cast<uint32_t>(data.current_buffer),
      static_cast<uint32_t>(data.input_buffer_0_valid),
      static_cast<uint32_t>(data.input_buffer_1_valid),
      static_cast<uint32_t>(data.output_buffer_valid),
      static_cast<uint32_t>(data.input_buffer_read_offset),
      static_cast<uint32_t>(data.output_buffer_read_offset),
      static_cast<uint32_t>(data.output_buffer_write_offset),
      static_cast<uint32_t>(data.error_status));
  REXAPU_DEBUG(
      "XmaContext {} last decode snapshot before reset: current_buffer={} read_before={} "
      "read_after={} packet_count={} decode_attempt={} last_packet={} next_packet={} "
      "last_frame_bits={} bits_to_copy={} skip={} cross_packet={} swapped={} "
      "decode_ok={} last_error_status={}",
      id(), last_current_buffer_, last_input_read_offset_before_, last_input_read_offset_after_,
      last_current_input_packet_count_, decode_attempt_count_, last_packet_index_,
      last_next_packet_index_, last_frame_size_bits_, last_bits_to_copy_, last_skip_count_,
      last_cross_packet_copy_, last_swapped_input_buffer_, last_decode_succeeded_,
      last_error_status_);
  ClearLocked(&data);
  data.Store(context_ptr);
}

void XmaContext::ClearLocked(XMA_CONTEXT_DATA* data) {
  data->input_buffer_0_valid = 0;
  data->input_buffer_1_valid = 0;
  data->output_buffer_valid = 0;

  data->input_buffer_read_offset = kBitsPerPacketHeader;
  data->output_buffer_read_offset = 0;
  data->output_buffer_write_offset = 0;

  current_frame_remaining_subframes_ = 0;
  loop_frame_output_limit_ = 0;
  loop_start_skip_pending_ = false;
}

void XmaContext::Disable() {
  std::lock_guard<std::mutex> lock(lock_);
  set_is_enabled(false);
}

void XmaContext::Release() {
  std::lock_guard<std::mutex> lock(lock_);
  assert_true(is_allocated());

  set_is_allocated(false);
  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));
}

void XmaContext::SwapInputBuffer(XMA_CONTEXT_DATA* data) {
  if (data->current_buffer == 0) {
    data->input_buffer_0_valid = 0;
  } else {
    data->input_buffer_1_valid = 0;
  }
  data->current_buffer ^= 1;
  data->input_buffer_read_offset = kBitsPerPacketHeader;
}

void XmaContext::UpdateLoopStatus(XMA_CONTEXT_DATA* data) {
  if (data->loop_count == 0) {
    return;
  }

  const uint32_t loop_start = std::max(kBitsPerPacketHeader, data->loop_start);
  const uint32_t loop_end = std::max(kBitsPerPacketHeader, data->loop_end);

  if (data->input_buffer_read_offset != loop_end) {
    return;
  }

  data->input_buffer_read_offset = loop_start;
  loop_start_skip_pending_ = true;

  if (data->loop_count < 255) {
    data->loop_count--;
  }
}

int XmaContext::GetSampleRate(int id) {
  return kIdToSampleRate[std::min(id, 3)];
}

int16_t XmaContext::GetPacketNumber(size_t size, size_t bit_offset) {
  if (bit_offset < kBitsPerPacketHeader) {
    assert_always();
    return -1;
  }
  if (bit_offset >= (size << 3)) {
    assert_always();
    return -1;
  }
  size_t byte_offset = bit_offset >> 3;
  size_t packet_number = byte_offset / kBytesPerPacket;
  return static_cast<int16_t>(packet_number);
}

uint32_t XmaContext::GetCurrentInputBufferSize(XMA_CONTEXT_DATA* data) {
  return data->GetCurrentInputBufferPacketCount() * kBytesPerPacket;
}

uint8_t* XmaContext::GetCurrentInputBuffer(XMA_CONTEXT_DATA* data) {
  return memory()->TranslatePhysical(data->GetCurrentInputBufferAddress());
}

uint32_t XmaContext::GetAmountOfBitsToRead(uint32_t remaining_stream_bits, uint32_t frame_size) {
  return std::min(remaining_stream_bits, frame_size);
}

const uint8_t* XmaContext::GetNextPacket(XMA_CONTEXT_DATA* data, uint32_t next_packet_index,
                                         uint32_t current_input_packet_count) {
  if (next_packet_index < current_input_packet_count) {
    return memory()->TranslatePhysical(data->GetCurrentInputBufferAddress()) +
           next_packet_index * kBytesPerPacket;
  }

  const uint8_t next_buffer_index = data->current_buffer ^ 1;
  if (!data->IsInputBufferValid(next_buffer_index)) {
    return nullptr;
  }

   const uint32_t next_buffer_packet_count = data->GetInputBufferPacketCount(next_buffer_index);
   const uint32_t next_buffer_packet_index = next_packet_index - current_input_packet_count;
   if (next_buffer_packet_index >= next_buffer_packet_count) {
     return nullptr;
   }

  const uint32_t next_buffer_address = data->GetInputBufferAddress(next_buffer_index);
  if (!next_buffer_address) {
    REXAPU_ERROR("XmaContext {}: Buffer marked valid but has null pointer!", id());
    return nullptr;
  }

  return memory()->TranslatePhysical(next_buffer_address) +
         next_buffer_packet_index * kBytesPerPacket;
}

uint32_t XmaContext::GetNextPacketReadOffset(XMA_CONTEXT_DATA* data, uint32_t next_packet_index,
                                             uint32_t current_input_packet_count) {
  if (next_packet_index < current_input_packet_count) {
    uint8_t* buffer = memory()->TranslatePhysical(data->GetCurrentInputBufferAddress());
    while (next_packet_index < current_input_packet_count) {
      uint8_t* next_packet = buffer + (next_packet_index * kBytesPerPacket);
      const uint32_t packet_frame_offset = xma::GetPacketFrameOffset(next_packet);

      if (packet_frame_offset <= kMaxFrameSizeinBits) {
        return (next_packet_index * kBitsPerPacket) + packet_frame_offset;
      }
      next_packet_index++;
    }
    return kBitsPerPacketHeader;
  }

  const uint8_t next_buffer_index = data->current_buffer ^ 1;
  if (!data->IsInputBufferValid(next_buffer_index)) {
    return kBitsPerPacketHeader;
  }

  const uint32_t next_buffer_address = data->GetInputBufferAddress(next_buffer_index);
  if (!next_buffer_address) {
    REXAPU_ERROR("XmaContext {}: Buffer marked valid but has null pointer!", id());
    return kBitsPerPacketHeader;
  }

  uint32_t next_buffer_packet_index = next_packet_index - current_input_packet_count;
  const uint32_t next_buffer_packet_count = data->GetInputBufferPacketCount(next_buffer_index);
  uint8_t* next_buffer = memory()->TranslatePhysical(next_buffer_address);
  while (next_buffer_packet_index < next_buffer_packet_count) {
    uint8_t* next_packet = next_buffer + (next_buffer_packet_index * kBytesPerPacket);
    const uint32_t packet_frame_offset = xma::GetPacketFrameOffset(next_packet);

    if (packet_frame_offset <= kMaxFrameSizeinBits) {
      return (next_buffer_packet_index * kBitsPerPacket) + packet_frame_offset;
    }
    next_buffer_packet_index++;
  }

  return kBitsPerPacketHeader;
}

memory::RingBuffer XmaContext::PrepareOutputRingBuffer(XMA_CONTEXT_DATA* data) {
  const uint32_t output_capacity = data->output_buffer_block_count * kOutputBytesPerBlock;
  const uint32_t output_read_offset = data->output_buffer_read_offset * kOutputBytesPerBlock;
  const uint32_t output_write_offset = data->output_buffer_write_offset * kOutputBytesPerBlock;

  if (output_capacity > kOutputMaxSizeBytes) {
    REXAPU_WARN(
        "XmaContext {}: Output buffer exceeds expected size! "
        "(Actual: {} Max: {})",
        id(), output_capacity, kOutputMaxSizeBytes);
  }

  uint8_t* output_buffer = memory()->TranslatePhysical(data->output_buffer_ptr);

  memory::RingBuffer output_rb(output_buffer, output_capacity);
  output_rb.set_read_offset(output_read_offset);
  output_rb.set_write_offset(output_write_offset);
  remaining_subframe_blocks_in_output_buffer_ =
      static_cast<int32_t>(output_rb.write_count()) / kOutputBytesPerBlock;

  return output_rb;
}

kPacketInfo XmaContext::GetPacketInfo(uint8_t* packet, uint32_t frame_offset) {
  kPacketInfo packet_info = {};

  const uint32_t first_frame_offset = xma::GetPacketFrameOffset(packet);
  BitStream stream(packet, kBitsPerPacket);
  stream.SetOffset(first_frame_offset);

  if (frame_offset < first_frame_offset) {
    packet_info.current_frame_ = 0;
    packet_info.current_frame_size_ = first_frame_offset - frame_offset;
  }

  while (true) {
    if (stream.BitsRemaining() < kBitsPerFrameHeader) {
      break;
    }

    const uint64_t frame_size = stream.Peek(kBitsPerFrameHeader);
    if (frame_size == 0 || frame_size == xma::kMaxFrameLength) {
      break;
    }

    if (stream.offset_bits() == frame_offset) {
      packet_info.current_frame_ = packet_info.frame_count_;
      packet_info.current_frame_size_ = static_cast<uint32_t>(frame_size);
    }

    packet_info.frame_count_++;

    if (frame_size > stream.BitsRemaining()) {
      break;
    }

    stream.Advance(frame_size - 1);

    if (stream.Read(1) == 0) {
      break;
    }
  }

  if (xma::IsPacketXma2Type(packet)) {
    const uint8_t xma2_frame_count = xma::GetPacketFrameCount(packet);
    if (xma2_frame_count > packet_info.frame_count_) {
      if (packet_info.current_frame_size_ == 0) {
        packet_info.current_frame_ = packet_info.frame_count_;
      }
      packet_info.frame_count_ = xma2_frame_count;
    }
  }
  return packet_info;
}

void XmaContext::StoreContextMerged(const XMA_CONTEXT_DATA& data,
                                    const XMA_CONTEXT_DATA& initial_data, uint8_t* context_ptr) {
  XMA_CONTEXT_DATA fresh(context_ptr);

  fresh.loop_count = data.loop_count;
  fresh.output_buffer_write_offset = data.output_buffer_write_offset;
  if (initial_data.input_buffer_0_valid && !data.input_buffer_0_valid) {
    fresh.input_buffer_0_valid = 0;
  }
  if (initial_data.input_buffer_1_valid && !data.input_buffer_1_valid) {
    fresh.input_buffer_1_valid = 0;
  }

  if (initial_data.output_buffer_valid && !data.output_buffer_valid) {
    fresh.output_buffer_valid = 0;
  }

  fresh.input_buffer_read_offset = data.input_buffer_read_offset;
  fresh.error_status = data.error_status;
  fresh.current_buffer = data.current_buffer;
  fresh.output_buffer_read_offset = data.output_buffer_read_offset;

  fresh.Store(context_ptr);
}

void XmaContext::Consume(memory::RingBuffer* output_rb, const XMA_CONTEXT_DATA* data) {
  if (!current_frame_remaining_subframes_) {
    return;
  }

  if (loop_frame_output_limit_ > 0) {
    const uint8_t total_subframes =
        (kBytesPerFrameChannel / kOutputBytesPerBlock) << data->is_stereo;
    const uint8_t consumed = total_subframes - current_frame_remaining_subframes_;
    if (consumed >= loop_frame_output_limit_) {
      remaining_subframe_blocks_in_output_buffer_ -= data->output_buffer_padding;
      current_frame_remaining_subframes_ = 0;
      loop_frame_output_limit_ = 0;
      return;
    }
  }

  const uint8_t effective_sdc = std::max(static_cast<uint32_t>(1), data->subframe_decode_count);
  int8_t subframes_to_write = std::min(static_cast<int8_t>(current_frame_remaining_subframes_),
                                       static_cast<int8_t>(effective_sdc));

  if (loop_frame_output_limit_ > 0) {
    const uint8_t total_subframes =
        (kBytesPerFrameChannel / kOutputBytesPerBlock) << data->is_stereo;
    const uint8_t consumed = total_subframes - current_frame_remaining_subframes_;
    const int8_t remaining_until_limit = static_cast<int8_t>(loop_frame_output_limit_ - consumed);
    if (subframes_to_write > remaining_until_limit) {
      subframes_to_write = remaining_until_limit;
    }
  }

  const int8_t raw_frame_read_offset =
      ((kBytesPerFrameChannel / kOutputBytesPerBlock) << data->is_stereo) -
      current_frame_remaining_subframes_;

  output_rb->Write(raw_frame_.data() + (kOutputBytesPerBlock * raw_frame_read_offset),
                   subframes_to_write * kOutputBytesPerBlock);

  const int8_t headroom = (current_frame_remaining_subframes_ - subframes_to_write == 0)
                              ? data->output_buffer_padding
                              : 0;

  remaining_subframe_blocks_in_output_buffer_ -= subframes_to_write + headroom;
  current_frame_remaining_subframes_ -= subframes_to_write;
}

int XmaContext::PrepareDecoder(int sample_rate, bool is_two_channel) {
  sample_rate = GetSampleRate(sample_rate);

  uint32_t channels = is_two_channel ? 2 : 1;
  if (av_context_->sample_rate != sample_rate ||
      av_context_->channels != static_cast<int>(channels)) {
    avcodec_close(av_context_);
    av_free(av_context_);
    av_context_ = avcodec_alloc_context3(av_codec_);

    av_context_->sample_rate = sample_rate;
    av_context_->channels = channels;

    if (avcodec_open2(av_context_, av_codec_, nullptr) < 0) {
      REXAPU_ERROR("XmaContext: Failed to reopen FFmpeg context");
      return -1;
    }
    return 1;
  }
  return 0;
}

void XmaContext::PreparePacket(uint32_t frame_size, uint32_t frame_padding) {
  av_packet_->data = xma_frame_.data();
  av_packet_->size = static_cast<int>(1 + ((frame_padding + frame_size) / 8) +
                                      (((frame_padding + frame_size) % 8) ? 1 : 0));

  auto padding_end = av_packet_->size * 8 - (8 + frame_padding + frame_size);
  assert_true(padding_end < 8);
  xma_frame_[0] = ((frame_padding & 7) << 5) | ((padding_end & 7) << 2);
}

bool XmaContext::DecodePacket(AVCodecContext* av_context, const AVPacket* av_packet,
                              AVFrame* av_frame) {
  auto ret = avcodec_send_packet(av_context, av_packet);
  if (ret < 0) {
    REXAPU_ERROR(
        "XmaContext {}: Error sending packet for decoding ({}) packet_index={} next_packet={} "
        "read_before={} read_after={} frame_bits={} bits_to_copy={} skip={} "
        "cross_packet={} swapped={} decode_attempt={}",
        id(), ret, last_packet_index_, last_next_packet_index_, last_input_read_offset_before_,
        last_input_read_offset_after_, last_frame_size_bits_, last_bits_to_copy_, last_skip_count_,
        last_cross_packet_copy_, last_swapped_input_buffer_, decode_attempt_count_);
    return false;
  }
  ret = avcodec_receive_frame(av_context, av_frame);

  if (ret == AVERROR(EAGAIN)) {
    return false;
  }
  if (ret < 0) {
    REXAPU_ERROR(
        "XmaContext {}: Error during decoding ({}) packet_index={} next_packet={} "
        "read_before={} read_after={} frame_bits={} bits_to_copy={} skip={} "
        "cross_packet={} swapped={} decode_attempt={}",
        id(), ret, last_packet_index_, last_next_packet_index_, last_input_read_offset_before_,
        last_input_read_offset_after_, last_frame_size_bits_, last_bits_to_copy_, last_skip_count_,
        last_cross_packet_copy_, last_swapped_input_buffer_, decode_attempt_count_);
    return false;
  }
  return true;
}

void XmaContext::Decode(XMA_CONTEXT_DATA* data) {
  SCOPE_profile_cpu_f("apu");

  ++decode_attempt_count_;
  last_input_read_offset_before_ = static_cast<uint32_t>(data->input_buffer_read_offset);
  last_input_read_offset_after_ = static_cast<uint32_t>(data->input_buffer_read_offset);
  last_current_input_packet_count_ = 0;
  last_frame_size_bits_ = 0;
  last_bits_to_copy_ = 0;
  last_next_packet_index_ = 0;
  last_current_buffer_ = data->current_buffer;
  last_skip_count_ = 0;
  last_packet_index_ = -1;
  last_cross_packet_copy_ = false;
  last_swapped_input_buffer_ = false;
  last_decode_succeeded_ = false;
  last_error_status_ = static_cast<uint32_t>(data->error_status);

  auto log_decode_state = [&](const char* reason) {
    REXAPU_WARN(
        "XmaContext {}: {} current_buffer={} valid0={} valid1={} output_valid={} "
        "read_before={} read_after={} packet_index={} next_packet={} packet_count={} "
        "skip={} frame_bits={} bits_to_copy={} loop_count={} err={} cross_packet={} "
        "swapped={} decode_attempt={}",
        id(), reason, static_cast<uint32_t>(data->current_buffer),
        static_cast<uint32_t>(data->input_buffer_0_valid),
        static_cast<uint32_t>(data->input_buffer_1_valid),
        static_cast<uint32_t>(data->output_buffer_valid), last_input_read_offset_before_,
        last_input_read_offset_after_,
        last_packet_index_, last_next_packet_index_, last_current_input_packet_count_,
        last_skip_count_, last_frame_size_bits_, last_bits_to_copy_,
        static_cast<uint32_t>(data->loop_count), static_cast<uint32_t>(data->error_status),
        last_cross_packet_copy_, last_swapped_input_buffer_, decode_attempt_count_);
  };

  if (!data->IsAnyInputBufferValid()) {
    return;
  }

  if (current_frame_remaining_subframes_ > 0) {
    return;
  }

  if (!data->IsCurrentInputBufferValid()) {
    last_swapped_input_buffer_ = true;
    SwapInputBuffer(data);
    if (!data->IsCurrentInputBufferValid()) {
      last_input_read_offset_after_ = static_cast<uint32_t>(data->input_buffer_read_offset);
      return;
    }
  }

  uint8_t* current_input_buffer = GetCurrentInputBuffer(data);

  input_buffer_.fill(0);

  bool is_loop_end_frame = false;
  if (data->loop_count > 0) {
    const uint32_t loop_end = std::max(kBitsPerPacketHeader, data->loop_end);
    is_loop_end_frame = (data->input_buffer_read_offset == loop_end);
  }

  UpdateLoopStatus(data);

  if (!data->output_buffer_block_count) {
    REXAPU_ERROR("XmaContext {}: Error - Received 0 for output_buffer_block_count!", id());
    return;
  }

  if (data->input_buffer_read_offset < kBitsPerPacketHeader) {
    data->input_buffer_read_offset = kBitsPerPacketHeader;
  }

  const uint32_t current_input_size = GetCurrentInputBufferSize(data);
  const uint32_t current_input_packet_count = current_input_size / kBytesPerPacket;
  last_current_input_packet_count_ = current_input_packet_count;

  const int16_t packet_index = GetPacketNumber(current_input_size, data->input_buffer_read_offset);
  last_packet_index_ = packet_index;

  if (packet_index == -1) {
    REXAPU_ERROR("XmaContext {}: Invalid packet index. Input read offset: {}", id(),
                 static_cast<uint32_t>(data->input_buffer_read_offset));
    log_decode_state("invalid-packet-index");
    return;
  }

  uint8_t* packet = current_input_buffer + (packet_index * kBytesPerPacket);
  const uint32_t packet_first_frame_offset = xma::GetPacketFrameOffset(packet);
  uint32_t relative_offset = data->input_buffer_read_offset % kBitsPerPacket;

  if (relative_offset < packet_first_frame_offset) {
    data->input_buffer_read_offset = (packet_index * kBitsPerPacket) + packet_first_frame_offset;
    relative_offset = packet_first_frame_offset;
  }

  const uint8_t skip_count = xma::GetPacketSkipCount(packet);
  last_skip_count_ = skip_count;

  if (skip_count == 0xFF) {
    const uint32_t next_packet_index = packet_index + 1;
    const bool next_packet_in_next_buffer = next_packet_index >= current_input_packet_count;
    uint32_t next_input_offset =
        GetNextPacketReadOffset(data, next_packet_index, current_input_packet_count);
    if (next_packet_in_next_buffer || next_input_offset == kBitsPerPacketHeader) {
      last_swapped_input_buffer_ = true;
      SwapInputBuffer(data);
    }
    data->input_buffer_read_offset = next_input_offset;
    last_input_read_offset_after_ = next_input_offset;
    if (IsDeepTraceEnabled()) {
      REXAPU_DEBUG(
          "XmaContext {}: skip packet packet_index={} next_input_offset={} packet_count={}",
          id(), packet_index, next_input_offset, current_input_packet_count);
    }
    return;
  }

  kPacketInfo packet_info = GetPacketInfo(packet, relative_offset);
  const uint32_t packet_to_skip = skip_count + 1;
  const uint32_t next_packet_index = packet_index + packet_to_skip;
  last_next_packet_index_ = next_packet_index;

  if (packet_info.current_frame_size_ == 0) {
    const uint8_t* next_packet = GetNextPacket(data, next_packet_index, current_input_packet_count);
    if (!next_packet) {
      last_swapped_input_buffer_ = true;
      SwapInputBuffer(data);
      last_input_read_offset_after_ = static_cast<uint32_t>(data->input_buffer_read_offset);
      log_decode_state("missing-next-packet-for-split-frame");
      return;
    }
    last_cross_packet_copy_ = true;
    std::memcpy(input_buffer_.data(), packet + kBytesPerPacketHeader, kBytesPerPacketData);
    std::memcpy(input_buffer_.data() + kBytesPerPacketData, next_packet + kBytesPerPacketHeader,
                kBytesPerPacketData);

    BitStream combined(input_buffer_.data(), (kBitsPerPacket - kBitsPerPacketHeader) * 2);
    combined.SetOffset(relative_offset - kBitsPerPacketHeader);

    uint64_t frame_size = combined.Peek(kBitsPerFrameHeader);
    if (frame_size == xma::kMaxFrameLength) {
      data->error_status = 4;
      last_error_status_ = static_cast<uint32_t>(data->error_status);
      log_decode_state("split-frame-size-invalid");
      return;
    }
    packet_info.current_frame_size_ = static_cast<uint32_t>(frame_size);
  }
  last_frame_size_bits_ = packet_info.current_frame_size_;

  BitStream stream(current_input_buffer, (packet_index + 1) * kBitsPerPacket);
  stream.SetOffset(data->input_buffer_read_offset);

  const uint64_t bits_to_copy =
      GetAmountOfBitsToRead(static_cast<uint32_t>(stream.BitsRemaining()),
                            packet_info.current_frame_size_);
  last_bits_to_copy_ = static_cast<uint32_t>(bits_to_copy);

  if (bits_to_copy == 0) {
    REXAPU_ERROR("XmaContext {}: There are no bits to copy!", id());
    last_swapped_input_buffer_ = true;
    SwapInputBuffer(data);
    last_input_read_offset_after_ = static_cast<uint32_t>(data->input_buffer_read_offset);
    log_decode_state("zero-bits-to-copy");
    return;
  }

  if (packet_info.isLastFrameInPacket()) {
    if (stream.BitsRemaining() < packet_info.current_frame_size_) {
      const uint8_t* next_packet =
          GetNextPacket(data, next_packet_index, current_input_packet_count);
      if (!next_packet) {
        data->error_status = 4;
        last_error_status_ = static_cast<uint32_t>(data->error_status);
        log_decode_state("missing-next-packet-last-frame");
        return;
      }
      last_cross_packet_copy_ = true;
      std::memcpy(input_buffer_.data() + kBytesPerPacketData, next_packet + kBytesPerPacketHeader,
                  kBytesPerPacketData);
    }
  }

  std::memcpy(input_buffer_.data(), packet + kBytesPerPacketHeader, kBytesPerPacketData);

  stream = BitStream(input_buffer_.data(), (kBitsPerPacket - kBitsPerPacketHeader) * 2);
  stream.SetOffset(relative_offset - kBitsPerPacketHeader);

  xma_frame_.fill(0);

  const uint32_t padding_start =
      static_cast<uint8_t>(stream.Copy(xma_frame_.data() + 1, packet_info.current_frame_size_));

  raw_frame_.fill(0);

  PrepareDecoder(data->sample_rate, bool(data->is_stereo));
  if (IsDeepTraceEnabled() &&
      (last_cross_packet_copy_ || (decode_attempt_count_ % 512) == 0)) {
    REXAPU_DEBUG(
        "XmaContext {}: decode candidate packet_index={} next_packet={} frame_bits={} "
        "bits_to_copy={} current_buffer={} packet_count={} skip={} cross_packet={} "
        "sample_rate={} stereo={}",
        id(), packet_index, next_packet_index, packet_info.current_frame_size_,
        static_cast<uint32_t>(bits_to_copy), static_cast<uint32_t>(data->current_buffer),
        current_input_packet_count,
        skip_count, last_cross_packet_copy_, GetSampleRate(data->sample_rate), bool(data->is_stereo));
  }
  PreparePacket(packet_info.current_frame_size_, padding_start);
  if (DecodePacket(av_context_, av_packet_, av_frame_)) {
    ConvertFrame(reinterpret_cast<const uint8_t**>(&av_frame_->data), bool(data->is_stereo),
                 raw_frame_.data());
    current_frame_remaining_subframes_ = 4 << data->is_stereo;
    last_decode_succeeded_ = true;

    if (is_loop_end_frame) {
      loop_frame_output_limit_ = (data->loop_subframe_end + 1) << data->is_stereo;
    } else {
      loop_frame_output_limit_ = 0;
    }

    if (loop_start_skip_pending_) {
      const uint8_t skip = data->loop_subframe_skip << data->is_stereo;
      if (skip < current_frame_remaining_subframes_) {
        current_frame_remaining_subframes_ -= skip;
      }
      loop_start_skip_pending_ = false;
    }
  }

  if (!packet_info.isLastFrameInPacket()) {
    const uint32_t next_frame_offset =
        (data->input_buffer_read_offset + bits_to_copy) % kBitsPerPacket;
    data->input_buffer_read_offset = (packet_index * kBitsPerPacket) + next_frame_offset;
    last_input_read_offset_after_ = static_cast<uint32_t>(data->input_buffer_read_offset);
    return;
  }

  const bool next_packet_in_next_buffer = next_packet_index >= current_input_packet_count;
  uint32_t next_input_offset =
      GetNextPacketReadOffset(data, next_packet_index, current_input_packet_count);

  if (next_packet_in_next_buffer) {
    last_swapped_input_buffer_ = true;
    SwapInputBuffer(data);
  } else if (next_input_offset == kBitsPerPacketHeader) {
    last_swapped_input_buffer_ = true;
    SwapInputBuffer(data);
    if (data->IsAnyInputBufferValid()) {
      next_input_offset =
          xma::GetPacketFrameOffset(memory()->TranslatePhysical(data->GetCurrentInputBufferAddress()));

      if (next_input_offset > kMaxFrameSizeinBits) {
        log_decode_state("next-packet-frame-offset-invalid");
        last_swapped_input_buffer_ = true;
        SwapInputBuffer(data);
        return;
      }
    }
  }
  data->input_buffer_read_offset = next_input_offset;
  last_input_read_offset_after_ = static_cast<uint32_t>(data->input_buffer_read_offset);
}

void XmaContext::ConvertFrame(const uint8_t** samples, bool is_two_channel,
                              uint8_t* output_buffer) {
  constexpr float scale = (1 << 15) - 1;
  auto out = reinterpret_cast<int16_t*>(output_buffer);

#if REX_ARCH_AMD64
  static_assert(kSamplesPerFrame % 8 == 0);
  const auto in_channel_0 = reinterpret_cast<const float*>(samples[0]);
  const __m128 scale_mm = _mm_set1_ps(scale);
  if (is_two_channel) {
    const auto in_channel_1 = reinterpret_cast<const float*>(samples[1]);
    const __m128i shufmask = _mm_set_epi8(14, 15, 6, 7, 12, 13, 4, 5, 10, 11, 2, 3, 8, 9, 0, 1);
    for (uint32_t i = 0; i < kSamplesPerFrame; i += 4) {
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
    for (uint32_t i = 0; i < kSamplesPerFrame; i += 8) {
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
  for (uint32_t i = 0; i < kSamplesPerFrame; i++) {
    for (uint32_t j = 0; j <= uint32_t(is_two_channel); j++) {
      auto in = reinterpret_cast<const float*>(samples[j]);
      float scaled_sample = rex::clamp_float(in[i], -1.0f, 1.0f) * scale;
      auto sample = static_cast<int16_t>(scaled_sample);
      out[o++] = rex::byte_swap(sample);
    }
  }
#endif
}

}  // namespace rex::audio
