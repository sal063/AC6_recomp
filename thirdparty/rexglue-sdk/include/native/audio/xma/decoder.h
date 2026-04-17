// Native audio runtime
// Part of the AC6 Recompilation native foundation

#pragma once

#include <atomic>
#include <mutex>
#include <queue>

#include <native/audio/xma/context.h>
#include <native/audio/xma/register_file.h>
#include <native/thread.h>
#include <rex/bit.h>
#include <rex/kernel.h>

namespace rex::runtime {
class FunctionDispatcher;
}  // namespace rex::runtime

namespace rex::stream {
class ByteStream;
}  // namespace rex::stream

namespace rex::audio {

struct XMA_CONTEXT_DATA;

struct XmaLoopData {
  uint32_t loop_start{0};
  uint32_t loop_end{0};
  uint8_t loop_count{0};
  uint8_t loop_subframe_end{0};
  uint8_t loop_subframe_skip{0};
};

struct XmaContextInit {
  uint32_t input_buffer_0_ptr{0};
  uint32_t input_buffer_0_packet_count{0};
  uint32_t input_buffer_1_ptr{0};
  uint32_t input_buffer_1_packet_count{0};
  uint32_t input_buffer_read_offset{0};
  uint32_t output_buffer_ptr{0};
  uint32_t output_buffer_block_count{0};
  uint32_t work_buffer{0};
  uint32_t subframe_decode_count{0};
  uint32_t channel_count{0};
  uint32_t sample_rate{0};
  XmaLoopData loop_data{};
};

class XmaDecoder {
 public:
  explicit XmaDecoder(runtime::FunctionDispatcher* function_dispatcher);
  ~XmaDecoder();

  memory::Memory* memory() const { return memory_; }
  runtime::FunctionDispatcher* function_dispatcher() const { return function_dispatcher_; }

  X_STATUS Setup(system::KernelState* kernel_state);
  void Shutdown();

  uint32_t context_array_ptr() const { return register_file_[XmaRegister::ContextArrayAddress]; }
  bool IsManagedContextPointer(uint32_t guest_ptr) const;

  uint32_t AllocateContext();
  void ReleaseContext(uint32_t guest_ptr);
  bool BlockOnContext(uint32_t guest_ptr, bool poll);
  bool WaitUntilIdle(uint32_t guest_ptr);
  X_STATUS InitializeContext(uint32_t guest_ptr, const XmaContextInit& init);
  X_STATUS SetLoopData(uint32_t guest_ptr, const XmaLoopData& loop_data);
  uint32_t GetInputBufferReadOffset(uint32_t guest_ptr) const;
  X_STATUS SetInputBufferReadOffset(uint32_t guest_ptr, uint32_t value);
  X_STATUS SetInputBuffer(uint32_t guest_ptr, uint32_t buffer_guest_ptr, uint32_t packet_count,
                          uint8_t buffer_index);
  uint32_t IsInputBufferValid(uint32_t guest_ptr, uint8_t buffer_index) const;
  X_STATUS SetInputBufferValid(uint32_t guest_ptr, uint8_t buffer_index);
  uint32_t IsOutputBufferValid(uint32_t guest_ptr) const;
  X_STATUS SetOutputBufferValid(uint32_t guest_ptr);
  uint32_t GetOutputBufferReadOffset(uint32_t guest_ptr) const;
  X_STATUS SetOutputBufferReadOffset(uint32_t guest_ptr, uint32_t value);
  uint32_t GetOutputBufferWriteOffset(uint32_t guest_ptr) const;
  uint32_t GetPacketMetadata(uint32_t guest_ptr) const;
  X_STATUS EnableContext(uint32_t guest_ptr);
  X_STATUS DisableContext(uint32_t guest_ptr, bool wait);
  X_STATUS BlockWhileInUse(uint32_t guest_ptr);

  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);
  bool Save(stream::ByteStream* stream);
  bool Restore(stream::ByteStream* stream);

  bool is_paused() const { return paused_; }
  void Pause();
  void Resume();

 protected:
  int GetContextId(uint32_t guest_ptr);

 private:
  void WorkerThreadMain();
  XMA_CONTEXT_DATA* TranslateManagedContext(uint32_t guest_ptr, const char* caller) const;
  uint32_t ResolvePhysicalAddress(uint32_t guest_ptr, const char* caller) const;
  void StoreContextIndexedRegister(uint32_t base_reg, uint32_t context_ptr);

  static uint32_t MMIOReadRegisterThunk(void* ppc_context, XmaDecoder* as, uint32_t addr) {
    return as->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(void* ppc_context, XmaDecoder* as, uint32_t addr,
                                     uint32_t value) {
    as->WriteRegister(addr, value);
  }

 protected:
  memory::Memory* memory_ = nullptr;
  runtime::FunctionDispatcher* function_dispatcher_ = nullptr;

  std::atomic<bool> worker_running_ = {false};
  std::unique_ptr<rex::thread::Thread> worker_thread_;
  std::unique_ptr<rex::thread::Event> work_event_ = nullptr;

  std::atomic<bool> paused_ = false;
  rex::thread::Fence pause_fence_;   // Signaled when worker paused.
  rex::thread::Fence resume_fence_;  // Signaled when resume requested.

  XmaRegisterFile register_file_;

  static const uint32_t kContextCount = 320;
  XmaContext contexts_[kContextCount];
  bit::BitMap context_bitmap_;

  uint32_t context_data_first_ptr_ = 0;
  uint32_t context_data_last_ptr_ = 0;
};

}  // namespace rex::audio
