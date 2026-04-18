#pragma once

#include <memory>
#include <string_view>

#include "replay_executor.h"
#include "types.h"

namespace ac6::renderer {

class RenderDeviceBackend {
 public:
  virtual ~RenderDeviceBackend() = default;

  virtual BackendType GetType() const = 0;
  virtual std::string_view GetName() const = 0;
  virtual bool IsSupported() const = 0;
  virtual bool Initialize(const NativeRendererConfig& config) = 0;
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

  bool Initialize(const NativeRendererConfig& config);
  void Shutdown();
  bool SubmitExecutorFrame(const ReplayExecutorFrame& frame);

  bool initialized() const { return initialized_; }
  BackendType active_backend() const { return active_backend_; }
  std::string_view backend_name() const;
  BackendExecutorStatus executor_status() const;

 private:
  std::unique_ptr<RenderDeviceBackend> backend_;
  BackendType active_backend_ = BackendType::kUnknown;
  bool initialized_ = false;
};

std::unique_ptr<RenderDeviceBackend> CreateBackend(BackendType backend);

}  // namespace ac6::renderer
