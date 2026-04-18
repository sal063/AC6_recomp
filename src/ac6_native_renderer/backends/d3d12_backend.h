#pragma once

#include "../render_device.h"
#include "../frame_scheduler.h"

#include <vector>
#include <unordered_map>

#if defined(_WIN32)
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#endif

namespace ac6::renderer {

class D3D12Backend final : public RenderDeviceBackend {
 public:
  BackendType GetType() const override { return BackendType::kD3D12; }
  std::string_view GetName() const override { return "d3d12"; }
  bool IsSupported() const override;
  bool Initialize(const NativeRendererConfig& config) override;
  bool SubmitExecutorFrame(const ReplayExecutorFrame& frame) override;
  BackendExecutorStatus GetExecutorStatus() const override { return executor_status_; }
  void Shutdown() override;

 private:
  BackendExecutorStatus executor_status_{};
  bool initialized_ = false;

#if defined(_WIN32)
  struct FrameContext {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    uint64_t fence_value = 0;
  };

  Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi_factory_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphics_queue_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;

  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  void* fence_event_ = nullptr; // HANDLE
  uint64_t current_fence_value_ = 0;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> dummy_output_resource_;
  uint32_t rtv_descriptor_size_ = 0;

  FrameScheduler frame_scheduler_;
  std::vector<FrameContext> frame_contexts_;

  std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>> resource_cache_;
  std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pso_cache_;

  bool CreateDevice();
  bool CreateCommandObjects(uint32_t num_frames);
  void WaitForGpu();
#endif
};

}  // namespace ac6::renderer