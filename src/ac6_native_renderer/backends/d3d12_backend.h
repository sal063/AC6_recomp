#pragma once

#include "../render_device.h"

namespace ac6::renderer {

class D3D12Backend final : public RenderDeviceBackend {
 public:
  BackendType GetType() const override { return BackendType::kD3D12; }
  std::string_view GetName() const override { return "d3d12"; }
  bool IsSupported() const override;
  bool Initialize(const NativeRendererConfig& config) override;
  void Shutdown() override;

 private:
  bool initialized_ = false;
};

}  // namespace ac6::renderer
