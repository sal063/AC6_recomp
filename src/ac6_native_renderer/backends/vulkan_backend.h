#pragma once

#include "../render_device.h"

namespace ac6::renderer {

class VulkanBackend final : public RenderDeviceBackend {
 public:
  BackendType GetType() const override { return BackendType::kVulkan; }
  std::string_view GetName() const override { return "vulkan"; }
  bool IsSupported() const override;
  bool Initialize(const NativeRendererConfig& config) override;
  void Shutdown() override;

 private:
  bool initialized_ = false;
};

}  // namespace ac6::renderer
