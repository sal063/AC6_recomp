#pragma once

#include "../render_device.h"
#include "../frame_scheduler.h"

#include "d3d12_resource_manager.h"
#include "d3d12_resource_tracker.h"
#include "d3d12_shader_manager.h"

#include <vector>
#include <unordered_map>

#if defined(_WIN32)
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#endif

namespace ac6::renderer {

// Experimental replay backend retained for research and targeted override work.
// The authoritative default presentation path remains the RexGlue backend.
class D3D12Backend final : public RenderDeviceBackend {
 public:
  BackendType GetType() const override { return BackendType::kD3D12; }
  std::string_view GetName() const override { return "d3d12"; }
  bool IsSupported() const override;
  bool Initialize(const NativeRendererConfig& config, rex::memory::Memory* memory) override;
  bool InitializeShared(const NativeRendererConfig& config, rex::memory::Memory* memory,
                        ID3D12Device* device, ID3D12CommandQueue* queue);
  bool SubmitExecutorFrame(const ReplayExecutorFrame& frame) override;
  BackendExecutorStatus GetExecutorStatus() const override { return executor_status_; }
  void Shutdown() override;

  // Phase 4: Returns the native output texture for swapchain blit.
  // nullptr until a frame has been rendered.
  ID3D12Resource* GetOutputTexture() const { return output_texture_.Get(); }

 private:
  BackendExecutorStatus executor_status_{};
  bool initialized_ = false;
  rex::memory::Memory* memory_ = nullptr;

#if defined(_WIN32)
  struct FrameContext {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>       command_allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>    command_list;
    uint64_t fence_value = 0;
  };

  struct DrawResources {
    bool valid = false;
    bool indexed = false;
    uint32_t draw_count = 0;
    uint32_t draw_start = 0;
    uint32_t vertex_base_offset = 0;
    uint32_t vertex_stride = 0;
    uint32_t vertex_buffer_size = 0;
    uint32_t color_offset = 0xFFFFFFFFu;
    D3D12_GPU_DESCRIPTOR_HANDLE vertex_buffer_gpu{};
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  };

  struct SubmissionDebugStats {
    uint32_t draw_attempt_count = 0;
    uint32_t draw_success_count = 0;
    uint32_t draw_prepare_failure_count = 0;
    uint32_t draw_pso_failure_count = 0;
    uint32_t indexed_draw_count = 0;
    uint32_t non_indexed_draw_count = 0;
    uint32_t clear_command_count = 0;
    uint32_t resolve_command_count = 0;
    uint32_t invalid_stream_binding_count = 0;
    uint32_t invalid_index_buffer_count = 0;
    uint32_t index_count_overflow_count = 0;
    uint32_t index_data_unavailable_count = 0;
    uint32_t index_buffer_create_failure_count = 0;
    uint32_t index_upload_failure_count = 0;
    uint32_t zero_vertex_count = 0;
    uint32_t invalid_vertex_range_count = 0;
    uint32_t vertex_buffer_size_invalid_count = 0;
    uint32_t vertex_buffer_create_failure_count = 0;
    uint32_t vertex_data_unavailable_count = 0;
    uint32_t vertex_upload_failure_count = 0;
  };

  Microsoft::WRL::ComPtr<IDXGIFactory4>        dxgi_factory_;
  Microsoft::WRL::ComPtr<ID3D12Device>         device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue>   graphics_queue_;

  Microsoft::WRL::ComPtr<ID3D12Fence>          fence_;
  void*    fence_event_ = nullptr;
  uint64_t current_fence_value_ = 0;

  Microsoft::WRL::ComPtr<ID3D12RootSignature>  root_signature_;

  // Phase 4: output render target (native renderer draws into this)
  Microsoft::WRL::ComPtr<ID3D12Resource>       output_texture_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> output_rtv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE                  output_rtv_{};
  uint32_t output_width_  = 0;
  uint32_t output_height_ = 0;
  static constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  FrameScheduler          frame_scheduler_;
  std::vector<FrameContext> frame_contexts_;
  SubmissionDebugStats submission_debug_stats_{};

  D3D12ResourceManager resource_manager_;
  D3D12ResourceTracker resource_tracker_;
  D3D12ShaderManager   shader_manager_;

  // PSO state hash helper
  uint64_t MakePSOHash(DXGI_FORMAT rt_fmt, DXGI_FORMAT ds_fmt,
                       D3D12_PRIMITIVE_TOPOLOGY_TYPE topo, bool soft_particle) const;

  // Ensure output texture is created at the right size
  bool EnsureOutputTexture(uint32_t width, uint32_t height);

  bool CreateDevice();
  bool CreateCommandObjects(uint32_t num_frames);
  bool CreateRootSignature();
  void WaitForGpu();

  // Phase 3 helpers
  void DispatchPassCommands(ID3D12GraphicsCommandList* cmd,
                            const ReplayExecutorPassPacket& pass,
                            uint32_t slot);
  bool PrepareDrawResources(ID3D12GraphicsCommandList* cmd,
                            const ReplayExecutorCommandPacket& command,
                            uint32_t slot,
                            DrawResources& out_resources);
#endif
};

}  // namespace ac6::renderer
