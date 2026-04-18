#pragma once

#include "../render_device.h"

namespace ac6::renderer {

class VulkanBackend final : public RenderDeviceBackend {
 public:
  BackendType GetType() const override { return BackendType::kVulkan; }
  std::string_view GetName() const override { return "vulkan"; }
  bool IsSupported() const override;
  bool Initialize(const NativeRendererConfig& config) override;
  bool SubmitExecutorFrame(const ReplayExecutorFrame& frame) override;
  BackendExecutorStatus GetExecutorStatus() const override { return executor_status_; }
  void Shutdown() override;

 private:
  BackendExecutorStatus executor_status_{};
  bool initialized_ = false;
};

}  // namespace ac6::renderer
