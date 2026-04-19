#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "d3d12_resource_manager.h"

#include <rex/logging.h>
#include <rex/graphics/xenos.h>

namespace ac6::renderer {

bool D3D12ResourceManager::Initialize(ID3D12Device* device, uint32_t max_frames) {
  device_ = device;
  max_frames_ = max_frames;

  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
  rtv_desc.NumDescriptors = kMaxRtvDescriptors;
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  if (FAILED(device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_)))) return false;
  rtv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {};
  dsv_desc.NumDescriptors = kMaxDsvDescriptors;
  dsv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  if (FAILED(device_->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap_)))) return false;
  dsv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
  srv_desc.NumDescriptors = kMaxSrvDescriptors;
  srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device_->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap_)))) return false;
  srv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  frame_contexts_.resize(max_frames);
  for (uint32_t i = 0; i < max_frames; ++i) {
    D3D12_HEAP_PROPERTIES upload_props = {};
    upload_props.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buffer_desc = {};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = kUploadBufferSize;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device_->CreateCommittedResource(&upload_props, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame_contexts_[i].upload_buffer)))) {
      return false;
    }

    D3D12_RANGE read_range = {0, 0};
    if (FAILED(frame_contexts_[i].upload_buffer->Map(0, &read_range, reinterpret_cast<void**>(&frame_contexts_[i].upload_ptr)))) {
      return false;
    }
  }

  return true;
}

void D3D12ResourceManager::Shutdown() {
  for (auto& ctx : frame_contexts_) {
    if (ctx.upload_buffer) {
      ctx.upload_buffer->Unmap(0, nullptr);
    }
  }
  frame_contexts_.clear();
  resource_cache_.clear();
  rtv_heap_.Reset();
  dsv_heap_.Reset();
  srv_heap_.Reset();
}

void D3D12ResourceManager::BeginFrame(uint32_t frame_index) {
  current_frame_index_ = frame_index;
  // Reset transient descriptors
  rtv_ptr_ = 0;
  dsv_ptr_ = 0;
  srv_ptr_ = 0;

  FrameContext& ctx = frame_contexts_[current_frame_index_ % max_frames_];
  ctx.upload_offset = 0;

  // Simple LRU cleanup could go here
}

ID3D12Resource* D3D12ResourceManager::GetOrCreateBuffer(uint32_t guest_address, uint32_t size, D3D12_RESOURCE_FLAGS flags) {
  if (guest_address == 0) return nullptr;

  auto it = resource_cache_.find(guest_address);
  if (it != resource_cache_.end()) {
    if (it->second.size_bytes >= size) {
      it->second.last_used_frame = current_frame_index_;
      return it->second.resource.Get();
    }
    REXLOG_INFO("Growing cached D3D12 buffer for guest address 0x{:08X} from {} to {} bytes",
                guest_address, it->second.size_bytes, size);
    resource_cache_.erase(it);
  }

  if (size == 0) {
    return nullptr;
  }

  D3D12_HEAP_PROPERTIES heap_props = {};
  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;

  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  if (FAILED(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)))) {
    REXLOG_ERROR("Failed to create D3D12 buffer for guest address 0x{:08X}", guest_address);
    return nullptr;
  }

  resource_cache_[guest_address] = {resource, size, current_frame_index_};
  return resource.Get();
}

ID3D12Resource* D3D12ResourceManager::GetOrCreateTexture(uint32_t guest_address, const d3d::ShadowState& state) {
  // In a real implementation, we would extract width, height, format from the fetch constant at guest_address
  // For this scaffold realization, we use guest_address as the key.
  (void)state;
  
  if (guest_address == 0) return nullptr;

  auto it = resource_cache_.find(guest_address);
  if (it != resource_cache_.end()) {
    it->second.last_used_frame = current_frame_index_;
    return it->second.resource.Get();
  }

  // Placeholder texture creation
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 1024; // Mock
  desc.Height = 1024; // Mock
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_HEAP_PROPERTIES heap_props = {};
  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  if (FAILED(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)))) {
    return nullptr;
  }

  resource_cache_[guest_address] = {resource, 0, current_frame_index_};
  return resource.Get();
}

D3D12ResourceManager::ResourceView D3D12ResourceManager::AllocateRTV() {
  uint32_t index = rtv_ptr_++;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += index * rtv_size_;
  return {cpu, {0}, index};
}

D3D12ResourceManager::ResourceView D3D12ResourceManager::AllocateDSV() {
  uint32_t index = dsv_ptr_++;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += index * dsv_size_;
  return {cpu, {0}, index};
}

D3D12ResourceManager::ResourceView D3D12ResourceManager::AllocateSRV() {
  uint32_t index = srv_ptr_++;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = srv_heap_->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE gpu = srv_heap_->GetGPUDescriptorHandleForHeapStart();
  cpu.ptr += index * srv_size_;
  gpu.ptr += index * srv_size_;
  return {cpu, gpu, index};
}

bool D3D12ResourceManager::UploadData(ID3D12GraphicsCommandList* command_list, ID3D12Resource* destination, const void* data, uint64_t size, uint64_t destination_offset) {
  if (!destination || !data || size == 0) return false;

  FrameContext& ctx = frame_contexts_[current_frame_index_ % max_frames_];
  
  // Align offset to 256 bytes for good practice, though not strictly required for all buffer copies
  uint32_t aligned_offset = (ctx.upload_offset + 255) & ~255;
  if (aligned_offset + size > kUploadBufferSize) {
    REXLOG_ERROR("Upload buffer overflow in frame {}", current_frame_index_);
    return false;
  }

  memcpy(ctx.upload_ptr + aligned_offset, data, size);
  ctx.upload_offset = aligned_offset + static_cast<uint32_t>(size);

  command_list->CopyBufferRegion(destination, destination_offset, ctx.upload_buffer.Get(), aligned_offset, size);
  return true;
}

DXGI_FORMAT D3D12ResourceManager::TranslateColorFormat(uint32_t guest_format) {
  using namespace rex::graphics::xenos;
  switch (static_cast<ColorRenderTargetFormat>(guest_format)) {
    case ColorRenderTargetFormat::k_8_8_8_8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case ColorRenderTargetFormat::k_2_10_10_10: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case ColorRenderTargetFormat::k_16_16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case ColorRenderTargetFormat::k_32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case ColorRenderTargetFormat::k_32_32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
    default: return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
}

DXGI_FORMAT D3D12ResourceManager::TranslateDepthFormat(uint32_t guest_format) {
  using namespace rex::graphics::xenos;
  switch (static_cast<DepthRenderTargetFormat>(guest_format)) {
    case DepthRenderTargetFormat::kD24S8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case DepthRenderTargetFormat::kD24FS8: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT; // Nearest
    default: return DXGI_FORMAT_D24_UNORM_S8_UINT;
  }
}

DXGI_FORMAT D3D12ResourceManager::TranslateTextureFormat(uint32_t guest_format) {
  using namespace rex::graphics::xenos;
  switch (static_cast<TextureFormat>(guest_format)) {
    case TextureFormat::k_8_8_8_8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::k_DXT1: return DXGI_FORMAT_BC1_UNORM;
    case TextureFormat::k_DXT2_3: return DXGI_FORMAT_BC2_UNORM;
    case TextureFormat::k_DXT4_5: return DXGI_FORMAT_BC3_UNORM;
    case TextureFormat::k_16_16_16_16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::k_32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    default: return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
}

DXGI_FORMAT D3D12ResourceManager::TranslateVertexFormat(uint32_t guest_format) {
  using namespace rex::graphics::xenos;
  switch (static_cast<VertexFormat>(guest_format)) {
    case VertexFormat::k_32_32_32_32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::k_32_32_32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::k_32_32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::k_32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::k_16_16_16_16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VertexFormat::k_16_16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case VertexFormat::k_8_8_8_8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
  }
}

}  // namespace ac6::renderer
