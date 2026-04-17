#pragma once

#include "ac6_native_renderer/render_device.h"

namespace ac6::renderer {

class MetalBackend final : public RenderDeviceBackend {
 public:
  BackendType GetType() const override { return BackendType::kMetal; }
  std::string_view GetName() const override { return "metal"; }
  bool IsSupported() const override;
  bool Initialize(const NativeRendererConfig& config) override;
  void Shutdown() override;

 private:
  bool initialized_ = false;
};

}  // namespace ac6::renderer
