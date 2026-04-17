/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <rex/audio/audio_trace.h>
#include <rex/audio/xma/context.h>
#include <rex/memory.h>

namespace rex::stream {
class ByteStream;
}

namespace rex::audio::xma {

struct XmaContextRecord {
  uint32_t guest_context_ptr{0};
  uint32_t input_buffer_0_physical_address{0};
  uint32_t input_buffer_1_physical_address{0};
  uint32_t output_buffer_physical_address{0};
  uint32_t input_buffer_0_packet_count{0};
  uint32_t input_buffer_1_packet_count{0};
  uint32_t output_buffer_block_count{0};
  bool initialized{false};
  bool enabled{false};
  bool blocked{false};
  uint64_t last_update_ticks{0};
};

class XmaContextPool {
 public:
  XmaContextPool();
  ~XmaContextPool();

  void Setup(memory::Memory* memory, AudioTraceBuffer* trace_buffer);
  void Shutdown();

  uint32_t AllocateContext();
  bool ReleaseContext(uint32_t guest_context_ptr);

  bool InitializeContext(uint32_t guest_context_ptr, const XMA_CONTEXT_DATA& context_data);
  bool SetLoopData(uint32_t guest_context_ptr, const XMA_CONTEXT_DATA& loop_data);

  uint32_t GetInputBufferReadOffset(uint32_t guest_context_ptr) const;
  bool SetInputBufferReadOffset(uint32_t guest_context_ptr, uint32_t value);

  bool SetInputBuffer0(uint32_t guest_context_ptr, uint32_t buffer_physical_address,
                       uint32_t packet_count);
  bool IsInputBuffer0Valid(uint32_t guest_context_ptr) const;
  bool SetInputBuffer0Valid(uint32_t guest_context_ptr, bool valid);

  bool SetInputBuffer1(uint32_t guest_context_ptr, uint32_t buffer_physical_address,
                       uint32_t packet_count);
  bool IsInputBuffer1Valid(uint32_t guest_context_ptr) const;
  bool SetInputBuffer1Valid(uint32_t guest_context_ptr, bool valid);

  bool IsOutputBufferValid(uint32_t guest_context_ptr) const;
  bool SetOutputBufferValid(uint32_t guest_context_ptr, bool valid);

  uint32_t GetOutputBufferReadOffset(uint32_t guest_context_ptr) const;
  bool SetOutputBufferReadOffset(uint32_t guest_context_ptr, uint32_t value);
  uint32_t GetOutputBufferWriteOffset(uint32_t guest_context_ptr) const;
  uint32_t GetPacketMetadata(uint32_t guest_context_ptr) const;

  bool SetEnabled(uint32_t guest_context_ptr, bool enabled);
  bool BlockWhileInUse(uint32_t guest_context_ptr, bool wait);

  bool Save(stream::ByteStream* stream) const;
  bool Restore(stream::ByteStream* stream);

 private:
  bool ReadContextDataLocked(uint32_t guest_context_ptr, XMA_CONTEXT_DATA* out_data) const;
  bool WriteContextDataLocked(uint32_t guest_context_ptr, XMA_CONTEXT_DATA data,
                              uint32_t trace_value_0, uint32_t trace_value_1);
  XmaContextRecord* LookupRecordLocked(uint32_t guest_context_ptr);
  const XmaContextRecord* LookupRecordLocked(uint32_t guest_context_ptr) const;
  uint64_t NextTickLocked();

  memory::Memory* memory_{nullptr};
  AudioTraceBuffer* trace_buffer_{nullptr};
  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, XmaContextRecord> records_;
  uint64_t tick_counter_{0};
};

}  // namespace rex::audio::xma
