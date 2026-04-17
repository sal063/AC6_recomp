/**
 * ReXGlue native audio runtime
 * Part of the AC6 Recompilation project
 */

#include <chrono>

#include <native/audio/xma/context.h>
#include <native/audio/xma/decoder.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <native/memory/ring_buffer.h>
#include <native/stream.h>
#include <native/string/buffer.h>
#include <rex/system/function_dispatcher.h>

extern "C" {
#include "libavutil/log.h"
}  // extern "C"

REXCVAR_DECLARE(bool, ffmpeg_verbose);

namespace rex::audio {

namespace {

constexpr uint32_t kXmaDecoderSaveVersion = 1;

}  // namespace

XmaDecoder::XmaDecoder(runtime::FunctionDispatcher* function_dispatcher)
    : memory_(function_dispatcher->memory()), function_dispatcher_(function_dispatcher) {}

XmaDecoder::~XmaDecoder() = default;

void av_log_callback(void* avcl, int level, const char* fmt, va_list va) {
  if (!REXCVAR_GET(ffmpeg_verbose) && level > AV_LOG_WARNING) {
    return;
  }

  string::StringBuffer buff;
  buff.AppendVarargs(fmt, va);
  auto msg = buff.to_string_view();

  switch (level) {
    case AV_LOG_ERROR:
      REXAPU_ERROR("ffmpeg: {}", msg);
      break;
    case AV_LOG_WARNING:
      REXAPU_WARN("ffmpeg: {}", msg);
      break;
    case AV_LOG_INFO:
      REXAPU_INFO("ffmpeg: {}", msg);
      break;
    case AV_LOG_VERBOSE:
    case AV_LOG_DEBUG:
    default:
      REXAPU_DEBUG("ffmpeg: {}", msg);
      break;
  }
}

X_STATUS XmaDecoder::Setup(system::KernelState* kernel_state) {
  (void)kernel_state;
  av_log_set_callback(av_log_callback);

  memory()->AddVirtualMappedRange(
      0x7FEA0000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<runtime::MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<runtime::MMIOWriteCallback>(MMIOWriteRegisterThunk));
  REXAPU_DEBUG("XMA: Registered MMIO handlers at 0x7FEA0000-0x7FEAFFFF");

  context_data_first_ptr_ =
      memory()->SystemHeapAlloc(sizeof(XMA_CONTEXT_DATA) * kContextCount, 256,
                                memory::kSystemHeapPhysical);
  context_data_last_ptr_ =
      context_data_first_ptr_ + (sizeof(XMA_CONTEXT_DATA) * kContextCount - 1);
  register_file_[XmaRegister::ContextArrayAddress] =
      memory()->GetPhysicalAddress(context_data_first_ptr_);

  for (size_t i = 0; i < kContextCount; ++i) {
    uint32_t guest_ptr = context_data_first_ptr_ + i * sizeof(XMA_CONTEXT_DATA);
    XmaContext& context = contexts_[i];
    if (context.Setup(i, memory(), guest_ptr)) {
      assert_always();
    }
  }
  register_file_[XmaRegister::NextContextIndex] = 1;
  context_bitmap_.Resize(kContextCount);

  worker_running_ = true;
  work_event_ = rex::thread::Event::CreateAutoResetEvent(false);
  assert_not_null(work_event_);
  rex::thread::Thread::CreationParameters thread_params;
  thread_params.stack_size = 128 * 1024;
  worker_thread_ = rex::thread::Thread::Create(thread_params, [this]() { WorkerThreadMain(); });
  assert_not_null(worker_thread_);
  worker_thread_->set_name("XMA Decoder");

  return X_STATUS_SUCCESS;
}

void XmaDecoder::WorkerThreadMain() {
  while (worker_running_) {
    bool did_work = false;
    for (uint32_t n = 0; n < kContextCount && worker_running_; n++) {
      XmaContext& context = contexts_[n];
      bool worked = context.Work();
      if (worked) {
        context.SignalWorkDone();
      }
      did_work = did_work || worked;
    }

    if (paused_) {
      pause_fence_.Signal();
      resume_fence_.Wait();
    }

    if (did_work) {
      continue;
    }
    rex::thread::Wait(work_event_.get(), false);
  }
}

void XmaDecoder::Shutdown() {
  if (!worker_thread_) {
    return;
  }

  worker_running_ = false;

  if (work_event_) {
    work_event_->Set();
  }

  if (paused_) {
    Resume();
  }

  const auto result =
      rex::thread::Wait(worker_thread_.get(), false, std::chrono::milliseconds(2000));
  if (result == rex::thread::WaitResult::kTimeout) {
    REXAPU_WARN("XMA: Worker thread did not exit within 2s, terminating");
    worker_thread_->Terminate(0);
  }
  worker_thread_.reset();

  if (context_data_first_ptr_) {
    memory()->SystemHeapFree(context_data_first_ptr_);
  }

  context_data_first_ptr_ = 0;
  context_data_last_ptr_ = 0;
}

int XmaDecoder::GetContextId(uint32_t guest_ptr) {
  static_assert_size(XMA_CONTEXT_DATA, 64);
  if (guest_ptr < context_data_first_ptr_ || guest_ptr > context_data_last_ptr_) {
    return -1;
  }
  assert_zero(guest_ptr & 0x3F);
  return (guest_ptr - context_data_first_ptr_) >> 6;
}

bool XmaDecoder::IsManagedContextPointer(const uint32_t guest_ptr) const {
  static_assert_size(XMA_CONTEXT_DATA, 64);
  if (guest_ptr < context_data_first_ptr_ || guest_ptr > context_data_last_ptr_) {
    return false;
  }
  if ((guest_ptr & 0x3F) != 0) {
    return false;
  }

  const uint32_t context_id = (guest_ptr - context_data_first_ptr_) >> 6;
  return context_id < kContextCount && contexts_[context_id].is_allocated();
}

XMA_CONTEXT_DATA* XmaDecoder::TranslateManagedContext(const uint32_t guest_ptr,
                                                      const char* caller) const {
  if (!IsManagedContextPointer(guest_ptr)) {
    REXAPU_WARN("{}: unmanaged XMA context {:08X}", caller, guest_ptr);
    return nullptr;
  }

  if (!memory_) {
    return nullptr;
  }

  auto* context_ptr = memory_->TranslateVirtual(guest_ptr);
  if (!context_ptr) {
    REXAPU_ERROR("{}: failed to translate XMA context {:08X}", caller, guest_ptr);
    return nullptr;
  }
  return reinterpret_cast<XMA_CONTEXT_DATA*>(context_ptr);
}

uint32_t XmaDecoder::ResolvePhysicalAddress(const uint32_t guest_ptr, const char* caller) const {
  const uint32_t physical_address = memory_ ? memory_->GetPhysicalAddress(guest_ptr) : UINT32_MAX;
  assert_true(physical_address != UINT32_MAX);
  if (physical_address == UINT32_MAX) {
    REXAPU_ERROR("{}: Invalid virtual address {:08X}", caller, guest_ptr);
    return UINT32_MAX;
  }
  return physical_address;
}

void XmaDecoder::StoreContextIndexedRegister(const uint32_t base_reg, const uint32_t context_ptr) {
  if (!IsManagedContextPointer(context_ptr)) {
    return;
  }

  const uint32_t context_physical_address = memory_->GetPhysicalAddress(context_ptr);
  assert_true(context_physical_address != UINT32_MAX);
  const uint32_t hw_index =
      (context_physical_address - context_array_ptr()) / sizeof(XMA_CONTEXT_DATA);
  const uint32_t reg_num = base_reg + (hw_index >> 5) * 4;
  const uint32_t reg_value = 1 << (hw_index & 0x1F);
  WriteRegister(reg_num, rex::byte_swap(reg_value));
}

uint32_t XmaDecoder::AllocateContext() {
  size_t index = context_bitmap_.Acquire();
  if (index == -1) {
    return 0;
  }

  XmaContext& context = contexts_[index];
  assert_false(context.is_allocated());
  context.set_is_allocated(true);
  return context.guest_ptr();
}

void XmaDecoder::ReleaseContext(uint32_t guest_ptr) {
  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = contexts_[context_id];
  assert_true(context.is_allocated());
  context.Release();
  context_bitmap_.Release(context_id);
}

bool XmaDecoder::BlockOnContext(uint32_t guest_ptr, bool poll) {
  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = contexts_[context_id];
  return context.Block(poll);
}

bool XmaDecoder::WaitUntilIdle(uint32_t guest_ptr) {
  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = contexts_[context_id];
  while (worker_running_.load(std::memory_order_acquire)) {
    if (context.IsIdle()) {
      return true;
    }

    const auto result = context.WaitForWorkDone(std::chrono::milliseconds(10));
    if (result == rex::thread::WaitResult::kFailed) {
      return false;
    }
  }

  return context.IsIdle();
}

X_STATUS XmaDecoder::InitializeContext(const uint32_t guest_ptr, const XmaContextInit& init) {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMAInitializeContext");
  if (!context_ptr) {
    return X_E_INVALIDARG;
  }

  const uint32_t input_buffer_0_physical_address = init.input_buffer_0_ptr
                                                       ? ResolvePhysicalAddress(
                                                             init.input_buffer_0_ptr,
                                                             "XMAInitializeContext input buffer 0")
                                                       : 0;
  if (init.input_buffer_0_ptr && input_buffer_0_physical_address == UINT32_MAX) {
    return X_E_FALSE;
  }
  const uint32_t input_buffer_1_physical_address = init.input_buffer_1_ptr
                                                       ? ResolvePhysicalAddress(
                                                             init.input_buffer_1_ptr,
                                                             "XMAInitializeContext input buffer 1")
                                                       : 0;
  if (init.input_buffer_1_ptr && input_buffer_1_physical_address == UINT32_MAX) {
    return X_E_FALSE;
  }
  if (!init.output_buffer_ptr) {
    REXAPU_ERROR("XMAInitializeContext output buffer: Invalid virtual address 00000000");
    return X_E_FALSE;
  }
  const uint32_t output_buffer_physical_address =
      ResolvePhysicalAddress(init.output_buffer_ptr, "XMAInitializeContext output buffer");
  if (output_buffer_physical_address == UINT32_MAX) {
    return X_E_FALSE;
  }

  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_0_ptr = input_buffer_0_physical_address;
  context.input_buffer_0_packet_count = init.input_buffer_0_packet_count;
  context.input_buffer_1_ptr = input_buffer_1_physical_address;
  context.input_buffer_1_packet_count = init.input_buffer_1_packet_count;
  context.input_buffer_read_offset = init.input_buffer_read_offset;
  context.output_buffer_ptr = output_buffer_physical_address;
  context.output_buffer_block_count = init.output_buffer_block_count;
  context.work_buffer_ptr = init.work_buffer;
  context.subframe_decode_count = init.subframe_decode_count;
  context.is_stereo = init.channel_count >= 1;
  context.sample_rate = init.sample_rate;
  context.loop_start = init.loop_data.loop_start;
  context.loop_end = init.loop_data.loop_end;
  context.loop_count = init.loop_data.loop_count;
  context.loop_subframe_end = init.loop_data.loop_subframe_end;
  context.loop_subframe_skip = init.loop_data.loop_subframe_skip;
  context.Store(context_ptr);

  StoreContextIndexedRegister(0x1A80, guest_ptr);
  return X_STATUS_SUCCESS;
}

X_STATUS XmaDecoder::SetLoopData(const uint32_t guest_ptr, const XmaLoopData& loop_data) {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMASetLoopData");
  if (!context_ptr) {
    return X_E_INVALIDARG;
  }

  XMA_CONTEXT_DATA context(context_ptr);
  context.loop_start = loop_data.loop_start;
  context.loop_end = loop_data.loop_end;
  context.loop_count = loop_data.loop_count;
  context.loop_subframe_end = loop_data.loop_subframe_end;
  context.loop_subframe_skip = loop_data.loop_subframe_skip;
  context.Store(context_ptr);
  return X_STATUS_SUCCESS;
}

uint32_t XmaDecoder::GetInputBufferReadOffset(const uint32_t guest_ptr) const {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMAGetInputBufferReadOffset");
  if (!context_ptr) {
    return 0;
  }
  return XMA_CONTEXT_DATA(context_ptr).input_buffer_read_offset;
}

X_STATUS XmaDecoder::SetInputBufferReadOffset(const uint32_t guest_ptr, const uint32_t value) {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMASetInputBufferReadOffset");
  if (!context_ptr) {
    return X_E_INVALIDARG;
  }
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_read_offset = value;
  context.Store(context_ptr);
  return X_STATUS_SUCCESS;
}

X_STATUS XmaDecoder::SetInputBuffer(const uint32_t guest_ptr, const uint32_t buffer_guest_ptr,
                                    const uint32_t packet_count, const uint8_t buffer_index) {
  const char* caller = buffer_index == 0 ? "XMASetInputBuffer0" : "XMASetInputBuffer1";
  auto* context_ptr = TranslateManagedContext(guest_ptr, caller);
  if (!context_ptr) {
    return X_E_INVALIDARG;
  }

  const uint32_t buffer_physical_address = ResolvePhysicalAddress(buffer_guest_ptr, caller);
  if (buffer_physical_address == UINT32_MAX) {
    return X_E_FALSE;
  }

  XMA_CONTEXT_DATA context(context_ptr);
  if (buffer_index == 0) {
    context.input_buffer_0_ptr = buffer_physical_address;
    context.input_buffer_0_packet_count = packet_count;
  } else {
    context.input_buffer_1_ptr = buffer_physical_address;
    context.input_buffer_1_packet_count = packet_count;
  }
  context.Store(context_ptr);
  return X_STATUS_SUCCESS;
}

uint32_t XmaDecoder::IsInputBufferValid(const uint32_t guest_ptr, const uint8_t buffer_index) const {
  const char* caller = buffer_index == 0 ? "XMAIsInputBuffer0Valid" : "XMAIsInputBuffer1Valid";
  auto* context_ptr = TranslateManagedContext(guest_ptr, caller);
  if (!context_ptr) {
    return 0;
  }
  return XMA_CONTEXT_DATA(context_ptr).IsInputBufferValid(buffer_index);
}

X_STATUS XmaDecoder::SetInputBufferValid(const uint32_t guest_ptr, const uint8_t buffer_index) {
  const char* caller = buffer_index == 0 ? "XMASetInputBuffer0Valid" : "XMASetInputBuffer1Valid";
  auto* context_ptr = TranslateManagedContext(guest_ptr, caller);
  if (!context_ptr) {
    return X_E_INVALIDARG;
  }

  XMA_CONTEXT_DATA context(context_ptr);
  if (buffer_index == 0) {
    context.input_buffer_0_valid = 1;
  } else {
    context.input_buffer_1_valid = 1;
  }
  context.Store(context_ptr);
  return X_STATUS_SUCCESS;
}

uint32_t XmaDecoder::IsOutputBufferValid(const uint32_t guest_ptr) const {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMAIsOutputBufferValid");
  if (!context_ptr) {
    return 0;
  }
  return XMA_CONTEXT_DATA(context_ptr).output_buffer_valid;
}

X_STATUS XmaDecoder::SetOutputBufferValid(const uint32_t guest_ptr) {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMASetOutputBufferValid");
  if (!context_ptr) {
    return X_E_INVALIDARG;
  }

  XMA_CONTEXT_DATA context(context_ptr);
  context.output_buffer_valid = 1;
  context.Store(context_ptr);
  return X_STATUS_SUCCESS;
}

uint32_t XmaDecoder::GetOutputBufferReadOffset(const uint32_t guest_ptr) const {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMAGetOutputBufferReadOffset");
  if (!context_ptr) {
    return 0;
  }
  return XMA_CONTEXT_DATA(context_ptr).output_buffer_read_offset;
}

X_STATUS XmaDecoder::SetOutputBufferReadOffset(const uint32_t guest_ptr, const uint32_t value) {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMASetOutputBufferReadOffset");
  if (!context_ptr) {
    return X_E_INVALIDARG;
  }

  XMA_CONTEXT_DATA context(context_ptr);
  context.output_buffer_read_offset = value;
  context.Store(context_ptr);
  return X_STATUS_SUCCESS;
}

uint32_t XmaDecoder::GetOutputBufferWriteOffset(const uint32_t guest_ptr) const {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMAGetOutputBufferWriteOffset");
  if (!context_ptr) {
    return 0;
  }
  return XMA_CONTEXT_DATA(context_ptr).output_buffer_write_offset;
}

uint32_t XmaDecoder::GetPacketMetadata(const uint32_t guest_ptr) const {
  auto* context_ptr = TranslateManagedContext(guest_ptr, "XMAGetPacketMetadata");
  if (!context_ptr) {
    return 0;
  }
  return XMA_CONTEXT_DATA(context_ptr).packet_metadata;
}

X_STATUS XmaDecoder::EnableContext(const uint32_t guest_ptr) {
  if (!TranslateManagedContext(guest_ptr, "XMAEnableContext")) {
    return X_E_INVALIDARG;
  }
  StoreContextIndexedRegister(0x1940, guest_ptr);
  return X_STATUS_SUCCESS;
}

X_STATUS XmaDecoder::DisableContext(const uint32_t guest_ptr, const bool wait) {
  if (!TranslateManagedContext(guest_ptr, "XMADisableContext")) {
    return X_E_INVALIDARG;
  }

  StoreContextIndexedRegister(0x1A40, guest_ptr);
  return BlockOnContext(guest_ptr, !wait) ? X_E_SUCCESS : X_E_FALSE;
}

X_STATUS XmaDecoder::BlockWhileInUse(const uint32_t guest_ptr) {
  if (!TranslateManagedContext(guest_ptr, "XMABlockWhileInUse")) {
    return X_E_INVALIDARG;
  }
  return WaitUntilIdle(guest_ptr) ? X_STATUS_SUCCESS : X_E_FALSE;
}

uint32_t XmaDecoder::ReadRegister(uint32_t addr) {
  auto r = (addr & 0xFFFF) / 4;

  assert_true(r < XmaRegisterFile::kRegisterCount);

  switch (r) {
    case XmaRegister::ContextArrayAddress:
      break;
    case XmaRegister::CurrentContextIndex: {
      uint32_t& current_context_index = register_file_[XmaRegister::CurrentContextIndex];
      uint32_t& next_context_index = register_file_[XmaRegister::NextContextIndex];
      current_context_index = next_context_index;
      next_context_index = (next_context_index + 1) % kContextCount;
      break;
    }
    default:
      const auto register_info = register_file_.GetRegisterInfo(r);
      if (register_info) {
        REXAPU_DEBUG("XMA: Read from unhandled register ({:04X}, {})", r, register_info->name);
      } else {
        REXAPU_DEBUG("XMA: Read from unknown register ({:04X})", r);
      }
      break;
  }

  return rex::byte_swap(register_file_[r]);
}

void XmaDecoder::WriteRegister(uint32_t addr, uint32_t value) {
  SCOPE_profile_cpu_f("apu");

  uint32_t r = (addr & 0xFFFF) / 4;
  value = rex::byte_swap(value);

  assert_true(r < XmaRegisterFile::kRegisterCount);
  register_file_[r] = value;

  if (r >= XmaRegister::Context0Kick && r <= XmaRegister::Context9Kick) {
    uint32_t base_context_id = (r - XmaRegister::Context0Kick) * 32;
    uint32_t kicked_value = value;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        auto& context = contexts_[context_id];
        context.Enable();
      }
    }
    work_event_->Set();
    for (int i = 0; kicked_value && i < 32; ++i, kicked_value >>= 1) {
      if (kicked_value & 1) {
        uint32_t context_id = base_context_id + i;
        contexts_[context_id].WaitForWorkDone();
      }
    }
  } else if (r >= XmaRegister::Context0Lock && r <= XmaRegister::Context9Lock) {
    uint32_t base_context_id = (r - XmaRegister::Context0Lock) * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        auto& context = contexts_[context_id];
        context.Disable();
        // Wait until the worker is out of the context before guest-side mutation resumes.
        context.Block(false);
      }
    }
  } else if (r >= XmaRegister::Context0Clear && r <= XmaRegister::Context9Clear) {
    uint32_t base_context_id = (r - XmaRegister::Context0Clear) * 32;
    for (int i = 0; value && i < 32; ++i, value >>= 1) {
      if (value & 1) {
        uint32_t context_id = base_context_id + i;
        XmaContext& context = contexts_[context_id];
        context.Clear();
      }
    }
  } else {
    switch (r) {
      default: {
        const auto register_info = register_file_.GetRegisterInfo(r);
        if (register_info) {
          REXAPU_DEBUG("XMA: Write to unhandled register ({:04X}, {}): {:08X}", r,
                       register_info->name, value);
        } else {
          REXAPU_DEBUG("XMA: Write to unknown register ({:04X}): {:08X}", r, value);
        }
        break;
      }
#pragma warning(suppress : 4065)
    }
  }
}

bool XmaDecoder::Save(stream::ByteStream* stream) {
  if (!stream) {
    return false;
  }

  const bool was_paused = paused_.load(std::memory_order_acquire);
  const bool needs_temporary_pause =
      worker_running_.load(std::memory_order_acquire) && !was_paused;
  if (needs_temporary_pause) {
    Pause();
  }

  stream->Write(kXmaDecoderSaveVersion);
  stream->Write(context_data_first_ptr_);
  stream->Write(context_data_last_ptr_);
  stream->Write(context_array_ptr());
  stream->Write(static_cast<uint32_t>(was_paused ? 1 : 0));
  stream->Write(register_file_.values, sizeof(register_file_.values));
  for (uint32_t i = 0; i < kContextCount; ++i) {
    if (!contexts_[i].Save(stream)) {
      if (needs_temporary_pause) {
        Resume();
      }
      return false;
    }
  }

  if (needs_temporary_pause) {
    Resume();
  }
  return true;
}

bool XmaDecoder::Restore(stream::ByteStream* stream) {
  if (!stream) {
    return false;
  }

  const bool needs_temporary_pause =
      worker_running_.load(std::memory_order_acquire) && !paused_.load(std::memory_order_acquire);
  if (needs_temporary_pause) {
    Pause();
  }

  if (stream->Read<uint32_t>() != kXmaDecoderSaveVersion) {
    REXAPU_ERROR("XmaDecoder::Restore - Invalid version");
    if (needs_temporary_pause) {
      Resume();
    }
    return false;
  }

  const uint32_t saved_context_data_first_ptr = stream->Read<uint32_t>();
  const uint32_t saved_context_data_last_ptr = stream->Read<uint32_t>();
  const uint32_t saved_context_array_ptr = stream->Read<uint32_t>();
  const bool saved_paused = stream->Read<uint32_t>() != 0;
  if (saved_context_data_first_ptr != context_data_first_ptr_ ||
      saved_context_data_last_ptr != context_data_last_ptr_ ||
      saved_context_array_ptr != context_array_ptr()) {
    REXAPU_ERROR(
        "XmaDecoder::Restore - Context array mismatch saved_first={:08X} live_first={:08X} "
        "saved_last={:08X} live_last={:08X} saved_array={:08X} live_array={:08X}",
        saved_context_data_first_ptr, context_data_first_ptr_, saved_context_data_last_ptr,
        context_data_last_ptr_, saved_context_array_ptr, context_array_ptr());
    if (needs_temporary_pause && !saved_paused) {
      Resume();
    }
    return false;
  }

  stream->Read(register_file_.values, sizeof(register_file_.values));
  context_bitmap_.Reset();
  for (uint32_t i = 0; i < kContextCount; ++i) {
    if (!contexts_[i].Restore(stream)) {
      if (needs_temporary_pause && !saved_paused) {
        Resume();
      }
      return false;
    }
    if (contexts_[i].is_allocated()) {
      context_bitmap_.data()[i >> 6] |= (uint64_t(1) << (i & 63));
    }
  }

  if (saved_paused) {
    paused_.store(true, std::memory_order_release);
  } else if (paused_.load(std::memory_order_acquire)) {
    Resume();
  }
  return true;
}

void XmaDecoder::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  if (work_event_) {
    work_event_->Set();
  }
  pause_fence_.Wait();
}

void XmaDecoder::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  resume_fence_.Signal();
}

}  // namespace rex::audio
