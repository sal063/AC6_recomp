#pragma once

#include <memory>
#include <string_view>

#if defined(_WIN32)
#include <d3d12.h>
#else
struct ID3D12Device;
struct ID3D12CommandQueue;
#endif

#include <rex/memory.h>

#include "replay_executor.h"
#include "types.h"

namespace ac6::renderer {

class RenderDeviceBackend {
 public:
  virtual ~RenderDeviceBackend() = default;

  virtual BackendType GetType() const = 0;
  virtual std::string_view GetName() const = 0;
  virtual bool IsSupported() const = 0;
  virtual bool Initialize(const NativeRendererConfig& config, rex::memory::Memory* memory) = 0;
  virtual bool InitializeShared(const NativeRendererConfig& config, rex::memory::Memory* memory, ID3D12Device* device, ID3D12CommandQueue* queue) = 0;
  virtual bool SubmitExecutorFrame(const ReplayExecutorFrame& frame) = 0;
  virtual BackendExecutorStatus GetExecutorStatus() const = 0;
  virtual void Shutdown() = 0;
};

class RenderDevice {
 public:
  RenderDevice();
  ~RenderDevice();

  RenderDevice(const RenderDevice&) = delete;
  RenderDevice& operator=(const RenderDevice&) = delete;

  bool Initialize(const NativeRendererConfig& config, rex::memory::Memory* memory);
  bool InitializeShared(const NativeRendererConfig& config, rex::memory::Memory* memory, ID3D12Device* device, ID3D12CommandQueue* queue);
  void Shutdown();
  bool SubmitExecutorFrame(const ReplayExecutorFrame& frame);

  bool initialized() const { return initialized_; }
  BackendType active_backend() const { return active_backend_; }
  std::string_view backend_name() const;
  BackendExecutorStatus executor_status() const;

  // Raw backend pointer — used by NativeRenderer to downcast to D3D12Backend*.
  RenderDeviceBackend* backend() const { return backend_.get(); }

 private:
  std::unique_ptr<RenderDeviceBackend> backend_;
  BackendType active_backend_ = BackendType::kUnknown;
  bool initialized_ = false;
};

std::unique_ptr<RenderDeviceBackend> CreateBackend(BackendType backend);

}  // namespace ac6::renderer
