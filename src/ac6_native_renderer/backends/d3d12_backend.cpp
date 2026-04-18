#include "d3d12_backend.h"

#include <rex/logging.h>

#if defined(_WIN32)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace ac6::renderer {

bool D3D12Backend::IsSupported() const {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

bool D3D12Backend::Initialize(const NativeRendererConfig& config) {
  if (initialized_) {
    return true;
  }

#if defined(_WIN32)
  if (!CreateDevice()) {
    REXLOG_ERROR("D3D12 CreateDevice failed.");
    return false;
  }

  if (!CreateCommandObjects(config.max_frames_in_flight)) {
    REXLOG_ERROR("D3D12 CreateCommandObjects failed.");
    return false;
  }

  frame_scheduler_.Configure(config.max_frames_in_flight);
#endif

  executor_status_ = {};
  executor_status_.initialized = true;
  initialized_ = true;
  REXLOG_INFO("AC6 native renderer D3D12 backend initialized successfully with max_frames_in_flight={}", config.max_frames_in_flight);
  return true;
}

bool D3D12Backend::SubmitExecutorFrame(const ReplayExecutorFrame& frame) {
  if (!initialized_) {
    return false;
  }

#if defined(_WIN32)
  frame_scheduler_.BeginFrame();
  uint32_t slot = frame_scheduler_.frame_slot();
  FrameContext& frame_ctx = frame_contexts_[slot];

  // Wait for the GPU to finish with this frame slot if needed.
  if (fence_->GetCompletedValue() < frame_ctx.fence_value) {
    fence_->SetEventOnCompletion(frame_ctx.fence_value, (HANDLE)fence_event_);
    WaitForSingleObject((HANDLE)fence_event_, INFINITE);
  }

  // Reset the command allocator for the current frame slot.
  HRESULT hr = frame_ctx.command_allocator->Reset();
  if (FAILED(hr)) {
    REXLOG_ERROR("Failed to reset command allocator.");
    return false;
  }

  // Reset the command list, using the reset allocator.
  hr = command_list_->Reset(frame_ctx.command_allocator.Get(), nullptr);
  if (FAILED(hr)) {
    REXLOG_ERROR("Failed to reset command list.");
    return false;
  }

  // -----------------------------------------------------------------
  // Workstreams 2 & 3: Minimal Resource Translation and Pipeline Setup
  // We mock the caching and PSO fetching by checking the requirement counts.
  // -----------------------------------------------------------------
  for (const ReplayExecutorPassPacket& pass : frame.passes) {
    if (pass.requires_resource_translation) {
      // Mock resource translation lookup
      for (const auto& cmd : pass.commands) {
        if (cmd.touches_render_target) {
          resource_cache_[cmd.execution_command_index] = dummy_output_resource_;
        }
      }
    }
    if (pass.requires_pipeline_state) {
      // Mock PSO fetch
      for (const auto& cmd : pass.commands) {
        if (cmd.requires_pipeline_state) {
          pso_cache_[cmd.execution_command_index] = nullptr; // mock PSO
        }
      }
    }
  }

  hr = command_list_->Close();
  if (FAILED(hr)) {
    REXLOG_ERROR("Failed to close command list.");
    return false;
  }

  ID3D12CommandList* ppCommandLists[] = { command_list_.Get() };
  graphics_queue_->ExecuteCommandLists(1, ppCommandLists);

  // Update the fence value for the current frame slot.
  current_fence_value_++;
  hr = graphics_queue_->Signal(fence_.Get(), current_fence_value_);
  if (FAILED(hr)) {
    REXLOG_ERROR("Failed to signal queue.");
    return false;
  }
  frame_ctx.fence_value = current_fence_value_;

#endif

  executor_status_ = {
      .initialized = true,
      .frame_valid = frame.summary.valid,
      .frame_index = frame.summary.frame_index,
      .submitted_pass_count = frame.summary.pass_count,
      .submitted_command_count = frame.summary.command_count,
      .graphics_pass_count = frame.summary.graphics_pass_count,
      .async_compute_pass_count = frame.summary.async_compute_pass_count,
      .copy_pass_count = frame.summary.copy_pass_count,
      .present_pass_count = frame.summary.present_pass_count,
      .resource_translation_pass_count =
          frame.summary.resource_translation_pass_count,
      .pipeline_state_pass_count = frame.summary.pipeline_state_pass_count,
      .descriptor_setup_pass_count = frame.summary.descriptor_setup_pass_count,
  };

  REXLOG_TRACE(
      "AC6 native renderer D3D12 submit frame={} passes={} commands={} graphics={} present={} resource={} pso={} descriptors={}",
      executor_status_.frame_index, executor_status_.submitted_pass_count,
      executor_status_.submitted_command_count,
      executor_status_.graphics_pass_count, executor_status_.present_pass_count,
      executor_status_.resource_translation_pass_count,
      executor_status_.pipeline_state_pass_count,
      executor_status_.descriptor_setup_pass_count);

  return true;
}

void D3D12Backend::Shutdown() {
  if (!initialized_) {
    return;
  }

#if defined(_WIN32)
  WaitForGpu();

  if (fence_event_) {
    CloseHandle((HANDLE)fence_event_);
    fence_event_ = nullptr;
  }

  command_list_.Reset();
  frame_contexts_.clear();
  graphics_queue_.Reset();
  fence_.Reset();
  device_.Reset();
  dxgi_factory_.Reset();
#endif

  executor_status_ = {};
  initialized_ = false;
}

#if defined(_WIN32)
bool D3D12Backend::CreateDevice() {
  UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  // Enable the D3D12 debug layer.
  Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif

  HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgi_factory_));
  if (FAILED(hr)) return false;

  // Try to create the device
  hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
  if (FAILED(hr)) {
    // Try WARP
    Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
    hr = dxgi_factory_->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
    if (FAILED(hr)) return false;

    hr = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) return false;
  }

  return true;
}

bool D3D12Backend::CreateCommandObjects(uint32_t num_frames) {
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  HRESULT hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&graphics_queue_));
  if (FAILED(hr)) return false;

  frame_contexts_.resize(num_frames);
  for (uint32_t i = 0; i < num_frames; ++i) {
    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame_contexts_[i].command_allocator));
    if (FAILED(hr)) return false;
  }

  hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame_contexts_[0].command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list_));
  if (FAILED(hr)) return false;
  
  // Close initially, since it will be reset on first submit
  command_list_->Close();

  hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
  if (FAILED(hr)) return false;
  current_fence_value_ = 0;

  fence_event_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (fence_event_ == nullptr) {
    return false;
  }

  // Create an RTV descriptor heap for the dummy output resource
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 1;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtv_heap_));
  if (FAILED(hr)) return false;

  rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  // Create a dummy output texture (1280x720, RGBA8)
  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heapProps.CreationNodeMask = 1;
  heapProps.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC resourceDesc = {};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resourceDesc.Alignment = 0;
  resourceDesc.Width = 1280;
  resourceDesc.Height = 720;
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.SampleDesc.Quality = 0;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  hr = device_->CreateCommittedResource(
      &heapProps,
      D3D12_HEAP_FLAG_NONE,
      &resourceDesc,
      D3D12_RESOURCE_STATE_RENDER_TARGET,
      nullptr,
      IID_PPV_ARGS(&dummy_output_resource_));
  if (FAILED(hr)) return false;

  // Create RTV
  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
  rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtvDesc.Texture2D.MipSlice = 0;
  rtvDesc.Texture2D.PlaneSlice = 0;
  device_->CreateRenderTargetView(dummy_output_resource_.Get(), &rtvDesc, rtv_heap_->GetCPUDescriptorHandleForHeapStart());

  return true;
}

void D3D12Backend::WaitForGpu() {
  if (graphics_queue_ && fence_ && fence_event_) {
    current_fence_value_++;
    HRESULT hr = graphics_queue_->Signal(fence_.Get(), current_fence_value_);
    if (SUCCEEDED(hr)) {
      if (fence_->GetCompletedValue() < current_fence_value_) {
        fence_->SetEventOnCompletion(current_fence_value_, (HANDLE)fence_event_);
        WaitForSingleObject((HANDLE)fence_event_, INFINITE);
      }
    }
  }
}
#endif

}  // namespace ac6::renderer