#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "d3d12_backend.h"

#include <algorithm>

#include <rex/logging.h>
#include <rex/system/xmemory.h>

#if defined(_WIN32)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace ac6::renderer {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#if defined(_WIN32)

// Compute a safe index-buffer upload size from draw count + index type.
// Xenos always uses 16-bit indices unless count > 65535.
static uint32_t SafeIndexBufferSize(uint32_t index_count) {
  // Xbox 360 indices are always big-endian uint16.  Cap at 4 MB.
  const uint64_t byte_size = uint64_t(index_count) * sizeof(uint16_t);
  return byte_size <= 4u * 1024u * 1024u ? static_cast<uint32_t>(byte_size) : 0u;
}

// Compute a safe vertex-buffer upload size from vertex count + stride.
static uint32_t SafeVertexBufferSize(uint32_t vertex_count, uint32_t stride) {
  if (stride == 0 || vertex_count == 0) return 0;
  const uint64_t byte_size = uint64_t(vertex_count) * uint64_t(stride);
  // Cap at 8 MB per stream.
  return byte_size <= 8u * 1024u * 1024u ? static_cast<uint32_t>(byte_size) : 0u;
}

// Validate a guest address is non-zero before calling TranslateVirtual.
static bool ValidGuestAddress(uint32_t addr) {
  return addr != 0 && addr < 0xFF000000u;
}

// Hash a byte span with FNV-1a.
static uint64_t FnvHash64(const void* data, size_t size) {
  constexpr uint64_t kBasis = 14695981039346656037ull;
  constexpr uint64_t kPrime = 1099511628211ull;
  uint64_t h = kBasis;
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= kPrime; }
  return h;
}

static uint16_t ByteSwap16(uint16_t value) {
  return static_cast<uint16_t>((value << 8) | (value >> 8));
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTypeToTopologyType(uint32_t primitive_type) {
  switch (primitive_type) {
    case 1:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case 2:
    case 3:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case 4:
    case 8:
    case 13:
    case 5:
    case 6:
    default:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
}

static D3D12_PRIMITIVE_TOPOLOGY PrimitiveTypeToTopology(uint32_t primitive_type) {
  switch (primitive_type) {
    case 1:
      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case 2:
      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case 3:
      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case 4:
    case 8:
    case 13:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case 5:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case 6:
      // Triangle fans need index expansion. Use strip as the least-bad fallback
      // until guest primitive conversion is implemented.
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }
}

static bool IsQuadListPrimitive(uint32_t primitive_type) {
  // Xenos kQuadList.
  return primitive_type == 13;
}

static bool IsRectangleListPrimitive(uint32_t primitive_type) {
  // Xenos kRectangleList.
  return primitive_type == 8;
}

static bool NeedsSyntheticIndices(uint32_t primitive_type) {
  return IsQuadListPrimitive(primitive_type) || IsRectangleListPrimitive(primitive_type);
}

static uint32_t GuessColorOffset(uint32_t vertex_stride) {
  if (vertex_stride == 20 || vertex_stride == 24) {
    return 12;
  }
  if (vertex_stride >= 28) {
    return 16;
  }
  return 0xFFFFFFFFu;
}

struct DrawRootConstants {
  uint32_t vertex_base_offset = 0;
  uint32_t vertex_stride = 0;
  uint32_t vertex_buffer_size = 0;
  uint32_t viewport_x = 0;
  uint32_t viewport_y = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
  uint32_t color_offset = 0xFFFFFFFFu;
  uint32_t flags = 0;
};

static void CreateRawBufferSRV(ID3D12Device* device,
                               ID3D12Resource* resource,
                               uint32_t size_bytes,
                               D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.FirstElement = 0;
  desc.Buffer.NumElements = std::max(1u, (size_bytes + 3u) / 4u);
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(resource, &desc, handle);
}

#endif  // _WIN32

// ---------------------------------------------------------------------------
// D3D12Backend public interface
// ---------------------------------------------------------------------------

bool D3D12Backend::IsSupported() const {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

// ---------------------------------------------------------------------------
// InitializeShared — reuse the emulator's already-created device/queue
// ---------------------------------------------------------------------------
bool D3D12Backend::InitializeShared(const NativeRendererConfig& config,
                                     rex::memory::Memory* memory,
                                     ID3D12Device* device,
                                     ID3D12CommandQueue* queue) {
  if (initialized_) return true;
  memory_  = memory;
  device_  = device;
  graphics_queue_ = queue;

  REXLOG_INFO("D3D12Backend::InitializeShared device=0x{:016X}", (uint64_t)device_.Get());

  if (!CreateCommandObjects(config.max_frames_in_flight)) return false;
  if (!resource_manager_.Initialize(device_.Get(), config.max_frames_in_flight)) return false;
  if (!shader_manager_.Initialize(device_.Get())) return false;
  if (!CreateRootSignature()) return false;

  frame_scheduler_.Configure(config.max_frames_in_flight);
  initialized_ = true;
  REXLOG_INFO("D3D12Backend::InitializeShared succeeded max_frames={}", config.max_frames_in_flight);
  return true;
}

// ---------------------------------------------------------------------------
// Initialize — create our own device (standalone mode)
// ---------------------------------------------------------------------------
bool D3D12Backend::Initialize(const NativeRendererConfig& config,
                               rex::memory::Memory* memory) {
  if (initialized_) return true;
  memory_ = memory;
  REXLOG_INFO("D3D12Backend::Initialize starting");

#if defined(_WIN32)
  if (!CreateDevice())                                                  return false;
  if (!CreateCommandObjects(config.max_frames_in_flight))               return false;
  if (!resource_manager_.Initialize(device_.Get(), config.max_frames_in_flight)) return false;
  if (!shader_manager_.Initialize(device_.Get()))                       return false;
  if (!CreateRootSignature())                                           return false;
  frame_scheduler_.Configure(config.max_frames_in_flight);
#endif

  initialized_ = true;
  REXLOG_INFO("D3D12Backend::Initialize succeeded max_frames={}", config.max_frames_in_flight);
  return true;
}

// ---------------------------------------------------------------------------
// Phase 4 helper — ensure output texture exists at correct size
// ---------------------------------------------------------------------------
#if defined(_WIN32)
bool D3D12Backend::EnsureOutputTexture(uint32_t width, uint32_t height) {
  if (!device_) return false;
  // Clamp / default
  if (width  == 0) width  = 1280;
  if (height == 0) height = 720;

  // Texture exists and is the correct size — reuse it.
  if (output_texture_ && output_width_ == width && output_height_ == height)
    return true;

  // If we already have a texture at a different size: keep the old one rather
  // than destroying it mid-flight. Resize only when safe (i.e., on first creation).
  if (output_texture_) {
    // Already created at a different size — just return success and keep
    // rendering into the old size to avoid a WaitForGpu on the shared queue.
    return true;
  }

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width              = width;
  desc.Height             = height;
  desc.DepthOrArraySize   = 1;
  desc.MipLevels          = 1;
  desc.Format             = kOutputFormat;
  desc.SampleDesc.Count   = 1;
  desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear_val = {};
  clear_val.Format   = kOutputFormat;
  clear_val.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  HRESULT hr = device_->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_val,
      IID_PPV_ARGS(&output_texture_));
  if (FAILED(hr)) {
    REXLOG_ERROR("D3D12Backend: CreateCommittedResource output texture failed 0x{:08X}", (uint32_t)hr);
    return false;
  }
  output_texture_->SetName(L"ac6.native.output");

  // Create/recreate the RTV heap + descriptor
  if (!output_rtv_heap_) {
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
    rtv_desc.NumDescriptors = 1;
    rtv_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&output_rtv_heap_));
    if (FAILED(hr)) return false;
  }
  output_rtv_ = output_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  device_->CreateRenderTargetView(output_texture_.Get(), nullptr, output_rtv_);

  // Track the new resource
  resource_tracker_.TrackResource(output_texture_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

  output_width_  = width;
  output_height_ = height;
  REXLOG_INFO("D3D12Backend: output texture {}x{} created", width, height);
  return true;
}
#endif

// ---------------------------------------------------------------------------
// SubmitExecutorFrame — Phases 1, 3, 4
// ---------------------------------------------------------------------------
bool D3D12Backend::SubmitExecutorFrame(const ReplayExecutorFrame& frame) {
  if (!initialized_ || !device_) return false;

  // Guard: device removed?
  if (FAILED(device_->GetDeviceRemovedReason())) {
    REXLOG_ERROR("D3D12Backend: device removed, disabling backend");
    initialized_ = false;
    return false;
  }

#if defined(_WIN32)
  submission_debug_stats_ = {};

  // Determine output dimensions from the frame summary
  uint32_t out_w = frame.summary.output_width;
  uint32_t out_h = frame.summary.output_height;
  if (!EnsureOutputTexture(out_w, out_h)) return false;

  // Pick the frame slot
  uint32_t slot = static_cast<uint32_t>(frame.summary.frame_index % frame_contexts_.size());
  FrameContext& ctx = frame_contexts_[slot];

  // Wait for this slot to finish on GPU
  if (fence_->GetCompletedValue() < ctx.fence_value) {
    fence_->SetEventOnCompletion(ctx.fence_value, (HANDLE)fence_event_);
    WaitForSingleObject((HANDLE)fence_event_, INFINITE);
  }

  // Reset for this frame
  HRESULT hr = ctx.command_allocator->Reset();
  if (FAILED(hr)) { REXLOG_ERROR("D3D12Backend: allocator Reset failed"); return false; }

  hr = ctx.command_list->Reset(ctx.command_allocator.Get(), nullptr);
  if (FAILED(hr)) { REXLOG_ERROR("D3D12Backend: cmdlist Reset failed"); return false; }

  resource_manager_.BeginFrame(slot);

  // -------------------------------------------------------------------
  // Phase 4: Transition output texture → RTV, clear it
  // -------------------------------------------------------------------
  resource_tracker_.TransitionBarrier(ctx.command_list.Get(),
                                      output_texture_.Get(),
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
  float clear_color[4] = {0.05f, 0.05f, 0.08f, 1.0f};
  ctx.command_list->ClearRenderTargetView(output_rtv_, clear_color, 0, nullptr);
  ctx.command_list->OMSetRenderTargets(1, &output_rtv_, FALSE, nullptr);

  D3D12_VIEWPORT vp = {0.f, 0.f, (float)output_width_, (float)output_height_, 0.f, 1.f};
  D3D12_RECT     scissor = {0, 0, (LONG)output_width_, (LONG)output_height_};
  ctx.command_list->RSSetViewports(1, &vp);
  ctx.command_list->RSSetScissorRects(1, &scissor);
  ctx.command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  // -------------------------------------------------------------------
  // Phase 3: Dispatch per-pass commands
  // -------------------------------------------------------------------
  for (const ReplayExecutorPassPacket& pass : frame.passes) {
    DispatchPassCommands(ctx.command_list.Get(), pass, slot);
  }

  // -------------------------------------------------------------------
  // Phase 4: Transition output → PIXEL_SHADER_RESOURCE for blit consumer
  // -------------------------------------------------------------------
  resource_tracker_.TransitionBarrier(ctx.command_list.Get(),
                                      output_texture_.Get(),
                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  hr = ctx.command_list->Close();
  if (FAILED(hr)) { REXLOG_ERROR("D3D12Backend: cmdlist Close failed"); return false; }

  ID3D12CommandList* lists[] = { ctx.command_list.Get() };
  graphics_queue_->ExecuteCommandLists(1, lists);

  ++current_fence_value_;
  hr = graphics_queue_->Signal(fence_.Get(), current_fence_value_);
  if (FAILED(hr)) { REXLOG_ERROR("D3D12Backend: Signal failed"); return false; }
  ctx.fence_value = current_fence_value_;
#endif

  executor_status_ = {
    .initialized              = true,
    .frame_valid              = frame.summary.valid,
    .frame_index              = frame.summary.frame_index,
    .submitted_pass_count     = frame.summary.pass_count,
    .submitted_command_count  = frame.summary.command_count,
    .graphics_pass_count      = frame.summary.graphics_pass_count,
    .async_compute_pass_count = frame.summary.async_compute_pass_count,
    .copy_pass_count          = frame.summary.copy_pass_count,
    .present_pass_count       = frame.summary.present_pass_count,
    .resource_translation_pass_count = frame.summary.resource_translation_pass_count,
    .pipeline_state_pass_count       = frame.summary.pipeline_state_pass_count,
    .descriptor_setup_pass_count     = frame.summary.descriptor_setup_pass_count,
    .draw_attempt_count              = submission_debug_stats_.draw_attempt_count,
    .draw_success_count              = submission_debug_stats_.draw_success_count,
    .draw_prepare_failure_count      = submission_debug_stats_.draw_prepare_failure_count,
    .draw_pso_failure_count          = submission_debug_stats_.draw_pso_failure_count,
    .indexed_draw_count              = submission_debug_stats_.indexed_draw_count,
    .non_indexed_draw_count          = submission_debug_stats_.non_indexed_draw_count,
    .clear_command_count             = submission_debug_stats_.clear_command_count,
    .resolve_command_count           = submission_debug_stats_.resolve_command_count,
    .invalid_stream_binding_count    = submission_debug_stats_.invalid_stream_binding_count,
    .invalid_index_buffer_count      = submission_debug_stats_.invalid_index_buffer_count,
    .index_count_overflow_count      = submission_debug_stats_.index_count_overflow_count,
    .index_data_unavailable_count    = submission_debug_stats_.index_data_unavailable_count,
    .index_buffer_create_failure_count =
        submission_debug_stats_.index_buffer_create_failure_count,
    .index_upload_failure_count = submission_debug_stats_.index_upload_failure_count,
    .zero_vertex_count = submission_debug_stats_.zero_vertex_count,
    .invalid_vertex_range_count = submission_debug_stats_.invalid_vertex_range_count,
    .vertex_buffer_size_invalid_count =
        submission_debug_stats_.vertex_buffer_size_invalid_count,
    .vertex_buffer_create_failure_count =
        submission_debug_stats_.vertex_buffer_create_failure_count,
    .vertex_data_unavailable_count =
        submission_debug_stats_.vertex_data_unavailable_count,
    .vertex_upload_failure_count = submission_debug_stats_.vertex_upload_failure_count,
  };
  return true;
}

// ---------------------------------------------------------------------------
// Phase 3: per-pass command dispatch
// ---------------------------------------------------------------------------
#if defined(_WIN32)
void D3D12Backend::DispatchPassCommands(ID3D12GraphicsCommandList* cmd,
                                        const ReplayExecutorPassPacket& pass,
                                        uint32_t slot) {
  ID3D12DescriptorHeap* descriptor_heaps[] = {resource_manager_.GetSrvHeap()};
  const DXGI_FORMAT rt_fmt = kOutputFormat;
  const DXGI_FORMAT ds_fmt = DXGI_FORMAT_UNKNOWN;
  const bool soft_part = (pass.role == ReplayPassRole::kPostProcess ||
                          pass.role == ReplayPassRole::kPresent);

  for (const ReplayExecutorCommandPacket& command : pass.commands) {
    switch (command.category) {
      case ExecutionCommandCategory::kClear: {
        ++submission_debug_stats_.clear_command_count;
        // Use the captured clear color rather than deriving nonsense from the
        // render-target handle stored in shadow state.
        float cc[4] = {
          static_cast<float>((command.color >> 16) & 0xFF) / 255.f,
          static_cast<float>((command.color >> 8) & 0xFF) / 255.f,
          static_cast<float>((command.color >> 0) & 0xFF) / 255.f,
          static_cast<float>((command.color >> 24) & 0xFF) / 255.f,
        };
        cmd->ClearRenderTargetView(output_rtv_, cc, 0, nullptr);
        break;
      }

      case ExecutionCommandCategory::kDraw: {
        ++submission_debug_stats_.draw_attempt_count;
        // Draw a full-screen triangle via SV_VertexID — the passthrough VS
        // does not need any vertex buffer. Real geometry binding (Phase 1.4)
        // requires a CPU-safe readback copy, not a live TranslateVirtual read.
        DrawResources draw_resources;
        if (!PrepareDrawResources(cmd, command, slot, draw_resources)) {
          ++submission_debug_stats_.draw_prepare_failure_count;
          break;
        }

        const uint64_t pso_hash = MakePSOHash(rt_fmt, ds_fmt,
                                              draw_resources.topology_type,
                                              soft_part);
        ID3D12PipelineState* pso = shader_manager_.GetOrCreatePSO(
            pso_hash, root_signature_.Get(), rt_fmt, ds_fmt,
            draw_resources.topology_type, soft_part);
        if (!pso) {
          ++submission_debug_stats_.draw_pso_failure_count;
          break;
        }

        DrawRootConstants constants;
        constants.vertex_base_offset = draw_resources.vertex_base_offset;
        constants.vertex_stride = draw_resources.vertex_stride;
        constants.vertex_buffer_size = draw_resources.vertex_buffer_size;
        constants.viewport_x = command.shadow_state.viewport.x;
        constants.viewport_y = command.shadow_state.viewport.y;
        constants.viewport_width =
            command.shadow_state.viewport.width ? command.shadow_state.viewport.width : pass.output_width;
        constants.viewport_height =
            command.shadow_state.viewport.height ? command.shadow_state.viewport.height : pass.output_height;
        constants.color_offset = draw_resources.color_offset;

        D3D12_VIEWPORT vp = {
            static_cast<float>(constants.viewport_x),
            static_cast<float>(constants.viewport_y),
            static_cast<float>(std::max(constants.viewport_width, 1u)),
            static_cast<float>(std::max(constants.viewport_height, 1u)),
            0.0f,
            1.0f,
        };
        D3D12_RECT scissor = {
            static_cast<LONG>(constants.viewport_x),
            static_cast<LONG>(constants.viewport_y),
            static_cast<LONG>(constants.viewport_x + std::max(constants.viewport_width, 1u)),
            static_cast<LONG>(constants.viewport_y + std::max(constants.viewport_height, 1u)),
        };

        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootSignature(root_signature_.Get());
        if (descriptor_heaps[0]) {
          cmd->SetDescriptorHeaps(1, descriptor_heaps);
          cmd->SetGraphicsRootDescriptorTable(1, draw_resources.vertex_buffer_gpu);
        }
        cmd->SetGraphicsRoot32BitConstants(
            0, sizeof(constants) / sizeof(uint32_t), &constants, 0);
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &scissor);
        cmd->IASetPrimitiveTopology(draw_resources.topology);

        if (draw_resources.indexed) {
          ++submission_debug_stats_.indexed_draw_count;
          cmd->DrawIndexedInstanced(draw_resources.draw_count, 1,
                                    draw_resources.draw_start, 0, 0);
        } else {
          ++submission_debug_stats_.non_indexed_draw_count;
          cmd->DrawInstanced(draw_resources.draw_count, 1,
                             draw_resources.draw_start, 0);
        }
        ++submission_debug_stats_.draw_success_count;
        break;
      }

      case ExecutionCommandCategory::kResolve:
        ++submission_debug_stats_.resolve_command_count;
        // Resolve is a no-op at this stage — handled by the output RT clear
        break;

      default:
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Phase 1: Safe buffer upload
// ---------------------------------------------------------------------------
bool D3D12Backend::PrepareDrawResources(ID3D12GraphicsCommandList* cmd,
                                        const ReplayExecutorCommandPacket& command,
                                        uint32_t slot,
                                        DrawResources& out_resources) {
  (void)slot;
  if (!memory_ || !cmd || command.count == 0) return false;
  out_resources.draw_count = command.count;
  out_resources.draw_start = 0;
  const auto& ss = command.shadow_state;
  const auto& stream = ss.streams[0];
  if (!ValidGuestAddress(stream.buffer) || stream.stride == 0) {
    ++submission_debug_stats_.invalid_stream_binding_count;
    return false;
  }

  uint32_t vertex_count = 0;
  uint32_t vertex_first = command.start;
  if (command.draw_kind != d3d::DrawCallKind::kPrimitive) {
    if (!ValidGuestAddress(ss.index_buffer)) {
      if (!NeedsSyntheticIndices(command.primitive_type)) {
        ++submission_debug_stats_.invalid_index_buffer_count;
        return false;
      }

      if (IsRectangleListPrimitive(command.primitive_type)) {
        // D3D9 resolve-style rectangle lists are frequently emitted as 3-vertex
        // draws. Our replay path doesn't implement rectangle expansion yet, so
        // prefer a non-indexed triangle fallback over dropping the draw.
        out_resources.indexed = false;
        out_resources.draw_count = command.count;
        out_resources.draw_start = 0;
        vertex_count = command.count;
        vertex_first = command.start;
      } else {
        const uint32_t quad_count = command.count / 4;
        if (quad_count == 0) {
          ++submission_debug_stats_.invalid_index_buffer_count;
          return false;
        }
        const uint32_t expanded_index_count = quad_count * 6;
        const uint32_t ib_size = SafeIndexBufferSize(expanded_index_count);
        if (ib_size == 0) {
          ++submission_debug_stats_.index_count_overflow_count;
          return false;
        }

        std::vector<uint16_t> host_indices;
        host_indices.reserve(expanded_index_count);
        for (uint32_t quad = 0; quad < quad_count; ++quad) {
          const uint32_t base_vertex = quad * 4;
          if (base_vertex + 3 > UINT16_MAX) {
            ++submission_debug_stats_.index_count_overflow_count;
            return false;
          }
          const uint16_t i0 = static_cast<uint16_t>(base_vertex + 0);
          const uint16_t i1 = static_cast<uint16_t>(base_vertex + 1);
          const uint16_t i2 = static_cast<uint16_t>(base_vertex + 2);
          const uint16_t i3 = static_cast<uint16_t>(base_vertex + 3);
          host_indices.push_back(i0);
          host_indices.push_back(i1);
          host_indices.push_back(i2);
          host_indices.push_back(i0);
          host_indices.push_back(i2);
          host_indices.push_back(i3);
        }

        ID3D12Resource* host_ib = resource_manager_.GetOrCreateBuffer(
            stream.buffer ^ 0x80000000u ^ command.start, ib_size);
        if (!host_ib) {
          ++submission_debug_stats_.index_buffer_create_failure_count;
          return false;
        }

        resource_tracker_.TransitionBarrier(cmd, host_ib, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!resource_manager_.UploadData(cmd, host_ib, host_indices.data(), ib_size)) {
          ++submission_debug_stats_.index_upload_failure_count;
          return false;
        }
        resource_tracker_.TransitionBarrier(cmd, host_ib, D3D12_RESOURCE_STATE_INDEX_BUFFER);

        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = host_ib->GetGPUVirtualAddress();
        ibv.SizeInBytes = ib_size;
        ibv.Format = DXGI_FORMAT_R16_UINT;
        cmd->IASetIndexBuffer(&ibv);

        out_resources.indexed = true;
        out_resources.draw_count = expanded_index_count;
        out_resources.draw_start = 0;
        vertex_count = command.count;
        vertex_first = command.start;
      }
    } else {
      const uint64_t index_end_u64 =
          uint64_t(command.start) + uint64_t(command.count);
      if (index_end_u64 > UINT32_MAX) {
        ++submission_debug_stats_.index_count_overflow_count;
        return false;
      }
      const uint32_t upload_index_count = command.count;
      const uint32_t ib_size = SafeIndexBufferSize(upload_index_count);
      const auto* guest_indices = memory_->TranslateVirtual<const uint16_t*>(ss.index_buffer);
      if (!guest_indices || ib_size == 0) {
        ++submission_debug_stats_.index_data_unavailable_count;
        return false;
      }

      std::vector<uint16_t> host_indices(upload_index_count);
      uint32_t min_index = UINT32_MAX;
      uint32_t max_index = 0;
      for (uint32_t i = 0; i < upload_index_count; ++i) {
        const uint16_t value = ByteSwap16(guest_indices[command.start + i]);
        host_indices[i] = value;
        min_index = std::min<uint32_t>(min_index, value);
        max_index = std::max<uint32_t>(max_index, value);
      }
      vertex_first = min_index == UINT32_MAX ? 0u : min_index;
      vertex_count = max_index >= vertex_first ? (max_index - vertex_first + 1u) : 0u;

      if (IsQuadListPrimitive(command.primitive_type)) {
        const uint32_t quad_count = command.count / 4;
        if (quad_count == 0) {
          ++submission_debug_stats_.invalid_index_buffer_count;
          return false;
        }

        std::vector<uint16_t> quad_indices;
        quad_indices.reserve(static_cast<size_t>(quad_count) * 6);
        for (uint32_t quad = 0; quad < quad_count; ++quad) {
          const uint32_t base = command.start + quad * 4;
          if (base + 3 >= host_indices.size()) {
            break;
          }
          const uint16_t i0 = host_indices[base + 0];
          const uint16_t i1 = host_indices[base + 1];
          const uint16_t i2 = host_indices[base + 2];
          const uint16_t i3 = host_indices[base + 3];
          quad_indices.push_back(i0);
          quad_indices.push_back(i1);
          quad_indices.push_back(i2);
          quad_indices.push_back(i0);
          quad_indices.push_back(i2);
          quad_indices.push_back(i3);
        }

        if (quad_indices.empty()) {
          ++submission_debug_stats_.invalid_index_buffer_count;
          return false;
        }

        host_indices = std::move(quad_indices);
        out_resources.draw_count = static_cast<uint32_t>(host_indices.size());
        out_resources.draw_start = 0;

        min_index = UINT32_MAX;
        max_index = 0;
        for (const uint16_t value : host_indices) {
          min_index = std::min<uint32_t>(min_index, value);
          max_index = std::max<uint32_t>(max_index, value);
        }
        vertex_first = min_index == UINT32_MAX ? 0u : min_index;
        vertex_count = max_index >= vertex_first ? (max_index - vertex_first + 1u) : 0u;
      }

      if (vertex_count == 0) {
        ++submission_debug_stats_.zero_vertex_count;
        return false;
      }

      if (vertex_first != 0) {
        for (uint16_t& value : host_indices) {
          value = static_cast<uint16_t>(value - vertex_first);
        }
      }

      const uint32_t upload_ib_size =
          static_cast<uint32_t>(host_indices.size() * sizeof(uint16_t));
      ID3D12Resource* host_ib =
          resource_manager_.GetOrCreateBuffer(ss.index_buffer, upload_ib_size);
      if (!host_ib) {
        ++submission_debug_stats_.index_buffer_create_failure_count;
        return false;
      }

      resource_tracker_.TransitionBarrier(cmd, host_ib, D3D12_RESOURCE_STATE_COPY_DEST);
      if (!resource_manager_.UploadData(cmd, host_ib, host_indices.data(), upload_ib_size)) {
        ++submission_debug_stats_.index_upload_failure_count;
        return false;
      }
      resource_tracker_.TransitionBarrier(cmd, host_ib, D3D12_RESOURCE_STATE_INDEX_BUFFER);

      D3D12_INDEX_BUFFER_VIEW ibv = {};
      ibv.BufferLocation = host_ib->GetGPUVirtualAddress();
      ibv.SizeInBytes = upload_ib_size;
      ibv.Format = DXGI_FORMAT_R16_UINT;
      cmd->IASetIndexBuffer(&ibv);
      out_resources.indexed = true;
    }
  } else {
    if (IsQuadListPrimitive(command.primitive_type)) {
      const uint32_t quad_count = command.count / 4;
      if (quad_count == 0) {
        ++submission_debug_stats_.invalid_vertex_range_count;
        return false;
      }
      const uint32_t expanded_index_count = quad_count * 6;
      const uint32_t ib_size = SafeIndexBufferSize(expanded_index_count);
      if (ib_size == 0) {
        ++submission_debug_stats_.index_count_overflow_count;
        return false;
      }

      std::vector<uint16_t> host_indices;
      host_indices.reserve(expanded_index_count);
      for (uint32_t quad = 0; quad < quad_count; ++quad) {
        const uint32_t base_vertex = command.start + quad * 4;
        if (base_vertex + 3 > UINT16_MAX) {
          ++submission_debug_stats_.index_count_overflow_count;
          return false;
        }
        const uint16_t i0 = static_cast<uint16_t>(base_vertex + 0);
        const uint16_t i1 = static_cast<uint16_t>(base_vertex + 1);
        const uint16_t i2 = static_cast<uint16_t>(base_vertex + 2);
        const uint16_t i3 = static_cast<uint16_t>(base_vertex + 3);
        host_indices.push_back(i0);
        host_indices.push_back(i1);
        host_indices.push_back(i2);
        host_indices.push_back(i0);
        host_indices.push_back(i2);
        host_indices.push_back(i3);
      }

      ID3D12Resource* host_ib = resource_manager_.GetOrCreateBuffer(
          stream.buffer ^ 0x80000000u ^ command.start, ib_size);
      if (!host_ib) {
        ++submission_debug_stats_.index_buffer_create_failure_count;
        return false;
      }

      resource_tracker_.TransitionBarrier(cmd, host_ib, D3D12_RESOURCE_STATE_COPY_DEST);
      if (!resource_manager_.UploadData(cmd, host_ib, host_indices.data(), ib_size)) {
        ++submission_debug_stats_.index_upload_failure_count;
        return false;
      }
      resource_tracker_.TransitionBarrier(cmd, host_ib, D3D12_RESOURCE_STATE_INDEX_BUFFER);

      D3D12_INDEX_BUFFER_VIEW ibv = {};
      ibv.BufferLocation = host_ib->GetGPUVirtualAddress();
      ibv.SizeInBytes = ib_size;
      ibv.Format = DXGI_FORMAT_R16_UINT;
      cmd->IASetIndexBuffer(&ibv);
      out_resources.indexed = true;
      out_resources.draw_count = expanded_index_count;
      out_resources.draw_start = 0;
    }

    const uint64_t vertex_count_u64 = uint64_t(command.count);
    if (vertex_count_u64 > UINT32_MAX) {
      return false;
    }
    vertex_count = static_cast<uint32_t>(vertex_count_u64);
    vertex_first = command.start;
  }

  // --- Vertex streams ---
  // We only bind stream 0 (the primary stream) for now — a complete implementation
  // would iterate all kMaxStreams, but that is expensive and risks upload overflow.
  if (vertex_count == 0) {
    ++submission_debug_stats_.zero_vertex_count;
    return false;
  }

  const uint64_t guest_start_u64 =
      uint64_t(stream.buffer) + uint64_t(stream.offset) +
      uint64_t(vertex_first) * uint64_t(stream.stride);
  if (guest_start_u64 > UINT32_MAX) {
    ++submission_debug_stats_.invalid_vertex_range_count;
    return false;
  }
  const uint32_t guest_start = static_cast<uint32_t>(guest_start_u64);
  if (!ValidGuestAddress(guest_start)) {
    ++submission_debug_stats_.invalid_vertex_range_count;
    return false;
  }

  const uint32_t vb_size = SafeVertexBufferSize(vertex_count, stream.stride);
  if (vb_size == 0) {
    ++submission_debug_stats_.vertex_buffer_size_invalid_count;
    return false;
  }

  ID3D12Resource* host_vb = resource_manager_.GetOrCreateBuffer(guest_start, vb_size);
  if (!host_vb) {
    ++submission_debug_stats_.vertex_buffer_create_failure_count;
    return false;
  }

  const void* guest_data = memory_->TranslateVirtual(guest_start);
  if (!guest_data) {
    ++submission_debug_stats_.vertex_data_unavailable_count;
    return false;
  }

  resource_tracker_.TransitionBarrier(cmd, host_vb, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!resource_manager_.UploadData(cmd, host_vb, guest_data, vb_size)) {
    ++submission_debug_stats_.vertex_upload_failure_count;
    return false;
  }
  resource_tracker_.TransitionBarrier(cmd, host_vb, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  const auto vb_srv = resource_manager_.AllocateSRV();
  CreateRawBufferSRV(device_.Get(), host_vb, vb_size, vb_srv.cpu_handle);

  out_resources.valid = true;
  out_resources.vertex_stride = stream.stride;
  out_resources.vertex_buffer_size = vb_size;
  out_resources.color_offset = GuessColorOffset(stream.stride);
  out_resources.vertex_buffer_gpu = vb_srv.gpu_handle;
  out_resources.topology = PrimitiveTypeToTopology(command.primitive_type);
  out_resources.topology_type = PrimitiveTypeToTopologyType(command.primitive_type);
  return true;
}

// ---------------------------------------------------------------------------
// PSO hash
// ---------------------------------------------------------------------------
uint64_t D3D12Backend::MakePSOHash(DXGI_FORMAT rt_fmt, DXGI_FORMAT ds_fmt,
                                    D3D12_PRIMITIVE_TOPOLOGY_TYPE topo,
                                    bool soft_particle) const {
  struct Key { uint32_t rt, ds, topo; uint8_t sp; };
  Key k = { (uint32_t)rt_fmt, (uint32_t)ds_fmt, (uint32_t)topo, static_cast<uint8_t>(soft_particle ? 1u : 0u) };
  return FnvHash64(&k, sizeof(k));
}

#endif  // _WIN32

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void D3D12Backend::Shutdown() {
  if (!initialized_) return;

#if defined(_WIN32)
  WaitForGpu();

  resource_tracker_.Reset();
  resource_manager_.Shutdown();
  shader_manager_.Shutdown();

  output_texture_.Reset();
  output_rtv_heap_.Reset();

  if (fence_event_) {
    CloseHandle((HANDLE)fence_event_);
    fence_event_ = nullptr;
  }
  for (auto& c : frame_contexts_) {
    c.command_list.Reset();
    c.command_allocator.Reset();
  }
  frame_contexts_.clear();
  graphics_queue_.Reset();
  fence_.Reset();
  root_signature_.Reset();
  device_.Reset();
  dxgi_factory_.Reset();
#endif

  initialized_ = false;
  executor_status_ = {};
}

// ---------------------------------------------------------------------------
// Device / command object creation
// ---------------------------------------------------------------------------
#if defined(_WIN32)
bool D3D12Backend::CreateDevice() {
  if (device_) return true;

  UINT flags = 0;
#if defined(_DEBUG)
  Microsoft::WRL::ComPtr<ID3D12Debug> dbg;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
    dbg->EnableDebugLayer();
    flags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif
  HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&dxgi_factory_));
  if (FAILED(hr)) return false;

  hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
  if (FAILED(hr)) {
    Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
    hr = dxgi_factory_->EnumWarpAdapter(IID_PPV_ARGS(&warp));
    if (FAILED(hr)) return false;
    hr = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) return false;
  }
  REXLOG_INFO("D3D12Backend: device created 0x{:016X}", (uint64_t)device_.Get());
  return true;
}

bool D3D12Backend::CreateCommandObjects(uint32_t num_frames) {
  HRESULT hr;
  if (!graphics_queue_) {
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&graphics_queue_));
    if (FAILED(hr)) return false;
  }

  frame_contexts_.resize(num_frames);
  for (uint32_t i = 0; i < num_frames; ++i) {
    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          IID_PPV_ARGS(&frame_contexts_[i].command_allocator));
    if (FAILED(hr)) return false;
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     frame_contexts_[i].command_allocator.Get(), nullptr,
                                     IID_PPV_ARGS(&frame_contexts_[i].command_list));
    if (FAILED(hr)) return false;
    frame_contexts_[i].command_list->Close();
  }

  hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
  if (FAILED(hr)) return false;
  current_fence_value_ = 0;

  fence_event_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  return fence_event_ != nullptr;
}

bool D3D12Backend::CreateRootSignature() {
  // Slot 0: 16 root constants (b0)
  // Slot 1: SRV descriptor table (t0..t15) — visible to PS
  // Slot 2: Sampler descriptor table (s0..s15) — visible to PS
  D3D12_ROOT_PARAMETER params[2] = {};

  params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace  = 0;
  params[0].Constants.Num32BitValues = sizeof(DrawRootConstants) / sizeof(uint32_t);
  params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

  D3D12_DESCRIPTOR_RANGE srv_range = {};
  srv_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_range.NumDescriptors     = 1;
  srv_range.BaseShaderRegister = 0;
  srv_range.RegisterSpace      = 0;
  srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
  params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;

  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters     = 2;
  desc.pParameters       = params;
  desc.NumStaticSamplers = 0;
  desc.pStaticSamplers   = nullptr;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
  if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)))
    return false;
  return SUCCEEDED(device_->CreateRootSignature(
      0, blob->GetBufferPointer(), blob->GetBufferSize(),
      IID_PPV_ARGS(&root_signature_)));
}

void D3D12Backend::WaitForGpu() {
  if (!graphics_queue_ || !fence_ || !fence_event_) return;
  ++current_fence_value_;
  if (SUCCEEDED(graphics_queue_->Signal(fence_.Get(), current_fence_value_))) {
    if (fence_->GetCompletedValue() < current_fence_value_) {
      fence_->SetEventOnCompletion(current_fence_value_, (HANDLE)fence_event_);
      WaitForSingleObject((HANDLE)fence_event_, INFINITE);
    }
  }
}
#endif  // _WIN32

}  // namespace ac6::renderer
