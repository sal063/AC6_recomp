/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Tom Clay. All rights reserved.                              *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include <rex/audio/xma/xma_context_pool.h>

#include <rex/kernel.h>
#include <rex/stream.h>

namespace rex::audio::xma {

XmaContextPool::XmaContextPool() = default;
XmaContextPool::~XmaContextPool() = default;

void XmaContextPool::Setup(memory::Memory* memory, AudioTraceBuffer* trace_buffer) {
  std::lock_guard<std::mutex> lock(mutex_);
  memory_ = memory;
  trace_buffer_ = trace_buffer;
  records_.clear();
  tick_counter_ = 0;
}

void XmaContextPool::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (memory_) {
    for (const auto& [guest_context_ptr, record] : records_) {
      memory_->SystemHeapFree(guest_context_ptr);
      if (trace_buffer_) {
        trace_buffer_->Record(AudioTraceSubsystem::kXma, AudioTraceEventType::kXmaReleased,
                              guest_context_ptr);
      }
    }
  }
  records_.clear();
}

uint32_t XmaContextPool::AllocateContext() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!memory_) {
    return 0;
  }

  const uint32_t guest_context_ptr =
      memory_->SystemHeapAlloc(sizeof(XMA_CONTEXT_DATA), 256, memory::kSystemHeapPhysical);
  if (guest_context_ptr == 0) {
    return 0;
  }

  std::memset(memory_->TranslateVirtual(guest_context_ptr), 0, sizeof(XMA_CONTEXT_DATA));

  XmaContextRecord record;
  record.guest_context_ptr = guest_context_ptr;
  record.last_update_ticks = NextTickLocked();
  records_[guest_context_ptr] = record;

  if (trace_buffer_) {
    trace_buffer_->Record(AudioTraceSubsystem::kXma, AudioTraceEventType::kXmaAllocated,
                          guest_context_ptr);
  }
  return guest_context_ptr;
}

bool XmaContextPool::ReleaseContext(const uint32_t guest_context_ptr) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* record = LookupRecordLocked(guest_context_ptr);
  if (!record || !memory_) {
    return false;
  }

  memory_->SystemHeapFree(guest_context_ptr);
  records_.erase(guest_context_ptr);
  if (trace_buffer_) {
    trace_buffer_->Record(AudioTraceSubsystem::kXma, AudioTraceEventType::kXmaReleased,
                          guest_context_ptr);
  }
  return true;
}

bool XmaContextPool::InitializeContext(const uint32_t guest_context_ptr,
                                       const XMA_CONTEXT_DATA& context_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* record = LookupRecordLocked(guest_context_ptr);
  if (!record || !memory_) {
    return false;
  }

  std::memset(memory_->TranslateVirtual(guest_context_ptr), 0, sizeof(XMA_CONTEXT_DATA));
  record->initialized = true;
  record->enabled = false;
  record->blocked = false;
  record->input_buffer_0_physical_address = context_data.input_buffer_0_ptr;
  record->input_buffer_1_physical_address = context_data.input_buffer_1_ptr;
  record->output_buffer_physical_address = context_data.output_buffer_ptr;
  record->input_buffer_0_packet_count = context_data.input_buffer_0_packet_count;
  record->input_buffer_1_packet_count = context_data.input_buffer_1_packet_count;
  record->output_buffer_block_count = context_data.output_buffer_block_count;
  return WriteContextDataLocked(guest_context_ptr, context_data, context_data.input_buffer_0_ptr,
                                context_data.output_buffer_ptr);
}

bool XmaContextPool::SetLoopData(const uint32_t guest_context_ptr, const XMA_CONTEXT_DATA& loop_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.loop_start = loop_data.loop_start;
  data.loop_end = loop_data.loop_end;
  data.loop_count = loop_data.loop_count;
  data.loop_subframe_end = loop_data.loop_subframe_end;
  data.loop_subframe_skip = loop_data.loop_subframe_skip;
  return WriteContextDataLocked(guest_context_ptr, data, data.loop_start, data.loop_end);
}

uint32_t XmaContextPool::GetInputBufferReadOffset(const uint32_t guest_context_ptr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return 0;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  return data.input_buffer_read_offset;
}

bool XmaContextPool::SetInputBufferReadOffset(const uint32_t guest_context_ptr,
                                              const uint32_t value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.input_buffer_read_offset = value;
  return WriteContextDataLocked(guest_context_ptr, data, value, 0);
}

bool XmaContextPool::SetInputBuffer0(const uint32_t guest_context_ptr,
                                     const uint32_t buffer_physical_address,
                                     const uint32_t packet_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* record = LookupRecordLocked(guest_context_ptr);
  if (!record || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.input_buffer_0_ptr = buffer_physical_address;
  data.input_buffer_0_packet_count = packet_count;
  record->input_buffer_0_physical_address = buffer_physical_address;
  record->input_buffer_0_packet_count = packet_count;
  return WriteContextDataLocked(guest_context_ptr, data, buffer_physical_address, packet_count);
}

bool XmaContextPool::IsInputBuffer0Valid(const uint32_t guest_context_ptr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  return XMA_CONTEXT_DATA(memory_->TranslateVirtual(guest_context_ptr)).input_buffer_0_valid != 0;
}

bool XmaContextPool::SetInputBuffer0Valid(const uint32_t guest_context_ptr, const bool valid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.input_buffer_0_valid = valid ? 1 : 0;
  return WriteContextDataLocked(guest_context_ptr, data, valid ? 1u : 0u, 0);
}

bool XmaContextPool::SetInputBuffer1(const uint32_t guest_context_ptr,
                                     const uint32_t buffer_physical_address,
                                     const uint32_t packet_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* record = LookupRecordLocked(guest_context_ptr);
  if (!record || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.input_buffer_1_ptr = buffer_physical_address;
  data.input_buffer_1_packet_count = packet_count;
  record->input_buffer_1_physical_address = buffer_physical_address;
  record->input_buffer_1_packet_count = packet_count;
  return WriteContextDataLocked(guest_context_ptr, data, buffer_physical_address, packet_count);
}

bool XmaContextPool::IsInputBuffer1Valid(const uint32_t guest_context_ptr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  return XMA_CONTEXT_DATA(memory_->TranslateVirtual(guest_context_ptr)).input_buffer_1_valid != 0;
}

bool XmaContextPool::SetInputBuffer1Valid(const uint32_t guest_context_ptr, const bool valid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.input_buffer_1_valid = valid ? 1 : 0;
  return WriteContextDataLocked(guest_context_ptr, data, valid ? 1u : 0u, 1);
}

bool XmaContextPool::IsOutputBufferValid(const uint32_t guest_context_ptr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  return XMA_CONTEXT_DATA(memory_->TranslateVirtual(guest_context_ptr)).output_buffer_valid != 0;
}

bool XmaContextPool::SetOutputBufferValid(const uint32_t guest_context_ptr, const bool valid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.output_buffer_valid = valid ? 1 : 0;
  return WriteContextDataLocked(guest_context_ptr, data, valid ? 1u : 0u, 2);
}

uint32_t XmaContextPool::GetOutputBufferReadOffset(const uint32_t guest_context_ptr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return 0;
  }
  return XMA_CONTEXT_DATA(memory_->TranslateVirtual(guest_context_ptr)).output_buffer_read_offset;
}

bool XmaContextPool::SetOutputBufferReadOffset(const uint32_t guest_context_ptr,
                                               const uint32_t value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  XMA_CONTEXT_DATA data(memory_->TranslateVirtual(guest_context_ptr));
  data.output_buffer_read_offset = value;
  return WriteContextDataLocked(guest_context_ptr, data, value, 3);
}

uint32_t XmaContextPool::GetOutputBufferWriteOffset(const uint32_t guest_context_ptr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return 0;
  }
  return XMA_CONTEXT_DATA(memory_->TranslateVirtual(guest_context_ptr)).output_buffer_write_offset;
}

uint32_t XmaContextPool::GetPacketMetadata(const uint32_t guest_context_ptr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!LookupRecordLocked(guest_context_ptr) || !memory_) {
    return 0;
  }
  return XMA_CONTEXT_DATA(memory_->TranslateVirtual(guest_context_ptr)).packet_metadata;
}

bool XmaContextPool::SetEnabled(const uint32_t guest_context_ptr, const bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* record = LookupRecordLocked(guest_context_ptr);
  if (!record) {
    return false;
  }
  record->enabled = enabled;
  record->last_update_ticks = NextTickLocked();
  if (trace_buffer_) {
    trace_buffer_->Record(AudioTraceSubsystem::kXma, AudioTraceEventType::kXmaStateUpdated,
                          guest_context_ptr, enabled ? 1u : 0u, 4);
  }
  return true;
}

bool XmaContextPool::BlockWhileInUse(const uint32_t guest_context_ptr, const bool wait) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* record = LookupRecordLocked(guest_context_ptr);
  if (!record) {
    return false;
  }
  record->blocked = wait;
  record->last_update_ticks = NextTickLocked();
  if (trace_buffer_) {
    trace_buffer_->Record(AudioTraceSubsystem::kXma, AudioTraceEventType::kXmaStateUpdated,
                          guest_context_ptr, wait ? 1u : 0u, 5);
  }
  return true;
}

bool XmaContextPool::Save(stream::ByteStream* stream) const {
  if (!stream) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  stream->Write(static_cast<uint32_t>(records_.size()));
  for (const auto& [guest_context_ptr, record] : records_) {
    stream->Write(guest_context_ptr);
    stream->Write(record.input_buffer_0_physical_address);
    stream->Write(record.input_buffer_1_physical_address);
    stream->Write(record.output_buffer_physical_address);
    stream->Write(record.input_buffer_0_packet_count);
    stream->Write(record.input_buffer_1_packet_count);
    stream->Write(record.output_buffer_block_count);
    stream->Write(static_cast<uint32_t>(record.initialized ? 1 : 0));
    stream->Write(static_cast<uint32_t>(record.enabled ? 1 : 0));
    stream->Write(static_cast<uint32_t>(record.blocked ? 1 : 0));
    stream->Write(record.last_update_ticks);
  }
  return true;
}

bool XmaContextPool::Restore(stream::ByteStream* stream) {
  if (!stream) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  records_.clear();
  const uint32_t record_count = stream->Read<uint32_t>();
  for (uint32_t i = 0; i < record_count; ++i) {
    XmaContextRecord record;
    record.guest_context_ptr = stream->Read<uint32_t>();
    record.input_buffer_0_physical_address = stream->Read<uint32_t>();
    record.input_buffer_1_physical_address = stream->Read<uint32_t>();
    record.output_buffer_physical_address = stream->Read<uint32_t>();
    record.input_buffer_0_packet_count = stream->Read<uint32_t>();
    record.input_buffer_1_packet_count = stream->Read<uint32_t>();
    record.output_buffer_block_count = stream->Read<uint32_t>();
    record.initialized = stream->Read<uint32_t>() != 0;
    record.enabled = stream->Read<uint32_t>() != 0;
    record.blocked = stream->Read<uint32_t>() != 0;
    record.last_update_ticks = stream->Read<uint64_t>();
    records_[record.guest_context_ptr] = record;
  }
  return true;
}

bool XmaContextPool::ReadContextDataLocked(const uint32_t guest_context_ptr,
                                           XMA_CONTEXT_DATA* out_data) const {
  if (!out_data || !LookupRecordLocked(guest_context_ptr) || !memory_) {
    return false;
  }
  *out_data = XMA_CONTEXT_DATA(memory_->TranslateVirtual(guest_context_ptr));
  return true;
}

bool XmaContextPool::WriteContextDataLocked(const uint32_t guest_context_ptr,
                                            XMA_CONTEXT_DATA data,
                                            const uint32_t trace_value_0,
                                            const uint32_t trace_value_1) {
  auto* record = LookupRecordLocked(guest_context_ptr);
  if (!record || !memory_) {
    return false;
  }

  data.Store(memory_->TranslateVirtual(guest_context_ptr));
  record->last_update_ticks = NextTickLocked();
  if (trace_buffer_) {
    trace_buffer_->Record(AudioTraceSubsystem::kXma, AudioTraceEventType::kXmaStateUpdated,
                          guest_context_ptr, trace_value_0, trace_value_1);
  }
  return true;
}

XmaContextRecord* XmaContextPool::LookupRecordLocked(const uint32_t guest_context_ptr) {
  const auto it = records_.find(guest_context_ptr);
  return it == records_.end() ? nullptr : &it->second;
}

const XmaContextRecord* XmaContextPool::LookupRecordLocked(const uint32_t guest_context_ptr) const {
  const auto it = records_.find(guest_context_ptr);
  return it == records_.end() ? nullptr : &it->second;
}

uint64_t XmaContextPool::NextTickLocked() {
  return ++tick_counter_;
}

}  // namespace rex::audio::xma
