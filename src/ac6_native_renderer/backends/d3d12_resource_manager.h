#pragma once

#include <unordered_map>
#include <vector>

#include <wrl/client.h>
#include <d3d12.h>

#include "../types.h"
#include "../../d3d_state.h"

namespace ac6::renderer {

class D3D12ResourceManager {
 public:
  struct ResourceView {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    uint32_t heap_index;
  };

  bool Initialize(ID3D12Device* device, uint32_t max_frames);
  void Shutdown();

  void BeginFrame(uint32_t frame_index);

  // Translation
  ID3D12Resource* GetOrCreateBuffer(uint32_t guest_address, uint32_t size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource* GetOrCreateTexture(uint32_t guest_address, const d3d::ShadowState& state);
  
  // Descriptor management
  ResourceView AllocateRTV();
  ResourceView AllocateDSV();
  ResourceView AllocateSRV();
  ID3D12DescriptorHeap* GetSrvHeap() const { return srv_heap_.Get(); }

  // Data sync
  bool UploadData(ID3D12GraphicsCommandList* command_list, ID3D12Resource* destination, const void* data, uint64_t size, uint64_t destination_offset = 0);

  // Format translation
  DXGI_FORMAT TranslateColorFormat(uint32_t guest_format);
  DXGI_FORMAT TranslateDepthFormat(uint32_t guest_format);
  DXGI_FORMAT TranslateTextureFormat(uint32_t guest_format);
  DXGI_FORMAT TranslateVertexFormat(uint32_t guest_format);

 private:
  ID3D12Device* device_ = nullptr;
  uint32_t max_frames_ = 0;
  uint32_t current_frame_index_ = 0;

  struct CachedResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    uint64_t size_bytes = 0;
    uint64_t last_used_frame = 0;
  };

  std::unordered_map<uint32_t, CachedResource> resource_cache_;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;

  uint32_t rtv_ptr_ = 0;
  uint32_t dsv_ptr_ = 0;
  uint32_t srv_ptr_ = 0;

  uint32_t rtv_size_ = 0;
  uint32_t dsv_size_ = 0;
  uint32_t srv_size_ = 0;

  struct FrameContext {
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer;
    uint8_t* upload_ptr = nullptr;
    uint32_t upload_offset = 0;
  };
  std::vector<FrameContext> frame_contexts_;

  static constexpr uint32_t kMaxRtvDescriptors = 1024;
  static constexpr uint32_t kMaxDsvDescriptors = 256;
  static constexpr uint32_t kMaxSrvDescriptors = 4096;
  static constexpr uint32_t kUploadBufferSize = 16 * 1024 * 1024; // 16 MB per frame
};

}  // namespace ac6::renderer
