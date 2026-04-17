/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <chrono>
#include <cstring>

#include <rex/assert.h>
#include <native/audio/audio_system.h>
#include <native/audio/xma/decoder.h>
#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/ppc/function.h>
#include <rex/ppc/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

namespace rex::kernel::xboxkrnl {
using namespace rex::system;

using rex::audio::XMA_CONTEXT_DATA;

namespace {

audio::XmaDecoder* GetXmaDecoder() {
  return REX_KERNEL_STATE()->native_xma_decoder();
}

struct XMA_LOOP_DATA {
  rex::be<uint32_t> loop_start;
  rex::be<uint32_t> loop_end;
  uint8_t loop_count;
  uint8_t loop_subframe_end;
  uint8_t loop_subframe_skip;
};
static_assert_size(XMA_LOOP_DATA, 12);

struct XMA_CONTEXT_INIT {
  rex::be<uint32_t> input_buffer_0_ptr;
  rex::be<uint32_t> input_buffer_0_packet_count;
  rex::be<uint32_t> input_buffer_1_ptr;
  rex::be<uint32_t> input_buffer_1_packet_count;
  rex::be<uint32_t> input_buffer_read_offset;
  rex::be<uint32_t> output_buffer_ptr;
  rex::be<uint32_t> output_buffer_block_count;
  rex::be<uint32_t> work_buffer;
  rex::be<uint32_t> subframe_decode_count;
  rex::be<uint32_t> channel_count;
  rex::be<uint32_t> sample_rate;
  XMA_LOOP_DATA loop_data;
};
static_assert_size(XMA_CONTEXT_INIT, 56);

}  // namespace

ppc_u32_result_t XMACreateContext_entry(ppc_pu32_t context_out_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  if (!xma_decoder || !context_out_ptr) {
    return X_E_INVALIDARG;
  }

  uint32_t context_ptr = xma_decoder->AllocateContext();
  *context_out_ptr = context_ptr;
  if (!context_ptr) {
    return X_STATUS_NO_MEMORY;
  }
  return X_STATUS_SUCCESS;
}

ppc_u32_result_t XMAReleaseContext_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  if (!xma_decoder || !xma_decoder->IsManagedContextPointer(context_ptr.guest_address())) {
    return X_E_INVALIDARG;
  }
  xma_decoder->ReleaseContext(context_ptr.guest_address());
  return 0;
}

ppc_u32_result_t XMAInitializeContext_entry(ppc_pvoid_t context_ptr,
                                            ppc_ptr_t<XMA_CONTEXT_INIT> context_init) {
  auto* xma_decoder = GetXmaDecoder();
  if (!xma_decoder || !context_init) {
    return X_E_INVALIDARG;
  }
  audio::XmaContextInit init{};
  init.input_buffer_0_ptr = context_init->input_buffer_0_ptr;
  init.input_buffer_0_packet_count = context_init->input_buffer_0_packet_count;
  init.input_buffer_1_ptr = context_init->input_buffer_1_ptr;
  init.input_buffer_1_packet_count = context_init->input_buffer_1_packet_count;
  init.input_buffer_read_offset = context_init->input_buffer_read_offset;
  init.output_buffer_ptr = context_init->output_buffer_ptr;
  init.output_buffer_block_count = context_init->output_buffer_block_count;
  init.work_buffer = context_init->work_buffer;
  init.subframe_decode_count = context_init->subframe_decode_count;
  init.channel_count = context_init->channel_count;
  init.sample_rate = context_init->sample_rate;
  init.loop_data.loop_start = context_init->loop_data.loop_start;
  init.loop_data.loop_end = context_init->loop_data.loop_end;
  init.loop_data.loop_count = context_init->loop_data.loop_count;
  init.loop_data.loop_subframe_end = context_init->loop_data.loop_subframe_end;
  init.loop_data.loop_subframe_skip = context_init->loop_data.loop_subframe_skip;
  return xma_decoder->InitializeContext(context_ptr.guest_address(), init);
}

ppc_u32_result_t XMASetLoopData_entry(ppc_pvoid_t context_ptr,
                                      ppc_ptr_t<XMA_CONTEXT_DATA> loop_data) {
  auto* xma_decoder = GetXmaDecoder();
  if (!xma_decoder || !loop_data) {
    return X_E_INVALIDARG;
  }
  audio::XmaLoopData native_loop_data{};
  native_loop_data.loop_start = loop_data->loop_start;
  native_loop_data.loop_end = loop_data->loop_end;
  native_loop_data.loop_count = loop_data->loop_count;
  native_loop_data.loop_subframe_end = loop_data->loop_subframe_end;
  native_loop_data.loop_subframe_skip = loop_data->loop_subframe_skip;
  return xma_decoder->SetLoopData(context_ptr.guest_address(), native_loop_data);
}

ppc_u32_result_t XMAGetInputBufferReadOffset_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->GetInputBufferReadOffset(context_ptr.guest_address()) : 0;
}

ppc_u32_result_t XMASetInputBufferReadOffset_entry(ppc_pvoid_t context_ptr, ppc_u32_t value) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->SetInputBufferReadOffset(context_ptr.guest_address(), value)
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMASetInputBuffer0_entry(ppc_pvoid_t context_ptr, ppc_pvoid_t buffer,
                                          ppc_u32_t packet_count) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->SetInputBuffer(context_ptr.guest_address(),
                                                   buffer.guest_address(), packet_count, 0)
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMAIsInputBuffer0Valid_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->IsInputBufferValid(context_ptr.guest_address(), 0) : 0;
}

ppc_u32_result_t XMASetInputBuffer0Valid_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->SetInputBufferValid(context_ptr.guest_address(), 0)
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMASetInputBuffer1_entry(ppc_pvoid_t context_ptr, ppc_pvoid_t buffer,
                                          ppc_u32_t packet_count) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->SetInputBuffer(context_ptr.guest_address(),
                                                   buffer.guest_address(), packet_count, 1)
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMAIsInputBuffer1Valid_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->IsInputBufferValid(context_ptr.guest_address(), 1) : 0;
}

ppc_u32_result_t XMASetInputBuffer1Valid_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->SetInputBufferValid(context_ptr.guest_address(), 1)
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMAIsOutputBufferValid_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->IsOutputBufferValid(context_ptr.guest_address()) : 0;
}

ppc_u32_result_t XMASetOutputBufferValid_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->SetOutputBufferValid(context_ptr.guest_address())
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMAGetOutputBufferReadOffset_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->GetOutputBufferReadOffset(context_ptr.guest_address()) : 0;
}

ppc_u32_result_t XMASetOutputBufferReadOffset_entry(ppc_pvoid_t context_ptr, ppc_u32_t value) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->SetOutputBufferReadOffset(context_ptr.guest_address(), value)
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMAGetOutputBufferWriteOffset_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->GetOutputBufferWriteOffset(context_ptr.guest_address()) : 0;
}

ppc_u32_result_t XMAGetPacketMetadata_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->GetPacketMetadata(context_ptr.guest_address()) : 0;
}

ppc_u32_result_t XMAEnableContext_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->EnableContext(context_ptr.guest_address()) : X_E_INVALIDARG;
}

ppc_u32_result_t XMADisableContext_entry(ppc_pvoid_t context_ptr, ppc_u32_t wait) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->DisableContext(context_ptr.guest_address(), wait != 0)
                     : X_E_INVALIDARG;
}

ppc_u32_result_t XMABlockWhileInUse_entry(ppc_pvoid_t context_ptr) {
  auto* xma_decoder = GetXmaDecoder();
  return xma_decoder ? xma_decoder->BlockWhileInUse(context_ptr.guest_address())
                     : X_E_INVALIDARG;
}

}  // namespace rex::kernel::xboxkrnl

XBOXKRNL_EXPORT(__imp__XMACreateContext, rex::kernel::xboxkrnl::XMACreateContext_entry)
XBOXKRNL_EXPORT(__imp__XMAReleaseContext, rex::kernel::xboxkrnl::XMAReleaseContext_entry)
XBOXKRNL_EXPORT(__imp__XMAInitializeContext, rex::kernel::xboxkrnl::XMAInitializeContext_entry)
XBOXKRNL_EXPORT(__imp__XMASetLoopData, rex::kernel::xboxkrnl::XMASetLoopData_entry)
XBOXKRNL_EXPORT(__imp__XMAGetInputBufferReadOffset,
                rex::kernel::xboxkrnl::XMAGetInputBufferReadOffset_entry)
XBOXKRNL_EXPORT(__imp__XMASetInputBufferReadOffset,
                rex::kernel::xboxkrnl::XMASetInputBufferReadOffset_entry)
XBOXKRNL_EXPORT(__imp__XMASetInputBuffer0, rex::kernel::xboxkrnl::XMASetInputBuffer0_entry)
XBOXKRNL_EXPORT(__imp__XMAIsInputBuffer0Valid, rex::kernel::xboxkrnl::XMAIsInputBuffer0Valid_entry)
XBOXKRNL_EXPORT(__imp__XMASetInputBuffer0Valid,
                rex::kernel::xboxkrnl::XMASetInputBuffer0Valid_entry)
XBOXKRNL_EXPORT(__imp__XMASetInputBuffer1, rex::kernel::xboxkrnl::XMASetInputBuffer1_entry)
XBOXKRNL_EXPORT(__imp__XMAIsInputBuffer1Valid, rex::kernel::xboxkrnl::XMAIsInputBuffer1Valid_entry)
XBOXKRNL_EXPORT(__imp__XMASetInputBuffer1Valid,
                rex::kernel::xboxkrnl::XMASetInputBuffer1Valid_entry)
XBOXKRNL_EXPORT(__imp__XMAIsOutputBufferValid, rex::kernel::xboxkrnl::XMAIsOutputBufferValid_entry)
XBOXKRNL_EXPORT(__imp__XMASetOutputBufferValid,
                rex::kernel::xboxkrnl::XMASetOutputBufferValid_entry)
XBOXKRNL_EXPORT(__imp__XMAGetOutputBufferReadOffset,
                rex::kernel::xboxkrnl::XMAGetOutputBufferReadOffset_entry)
XBOXKRNL_EXPORT(__imp__XMASetOutputBufferReadOffset,
                rex::kernel::xboxkrnl::XMASetOutputBufferReadOffset_entry)
XBOXKRNL_EXPORT(__imp__XMAGetOutputBufferWriteOffset,
                rex::kernel::xboxkrnl::XMAGetOutputBufferWriteOffset_entry)
XBOXKRNL_EXPORT(__imp__XMAGetPacketMetadata, rex::kernel::xboxkrnl::XMAGetPacketMetadata_entry)
XBOXKRNL_EXPORT(__imp__XMAEnableContext, rex::kernel::xboxkrnl::XMAEnableContext_entry)
XBOXKRNL_EXPORT(__imp__XMADisableContext, rex::kernel::xboxkrnl::XMADisableContext_entry)
XBOXKRNL_EXPORT(__imp__XMABlockWhileInUse, rex::kernel::xboxkrnl::XMABlockWhileInUse_entry)
