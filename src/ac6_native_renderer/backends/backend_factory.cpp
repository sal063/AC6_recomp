#include "ac6_native_renderer/render_device.h"

#include <memory>

#include "ac6_native_renderer/backends/d3d12_backend.h"
#include "ac6_native_renderer/backends/metal_backend.h"
#include "ac6_native_renderer/backends/vulkan_backend.h"

namespace ac6::renderer {

std::unique_ptr<RenderDeviceBackend> CreateBackend(BackendType backend) {
  switch (backend) {
    case BackendType::kD3D12:
      return std::make_unique<D3D12Backend>();
    case BackendType::kVulkan:
      return std::make_unique<VulkanBackend>();
    case BackendType::kMetal:
      return std::make_unique<MetalBackend>();
    default:
      return nullptr;
  }
}

}  // namespace ac6::renderer
