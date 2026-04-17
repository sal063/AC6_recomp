#include "ac6_native_renderer/backends/vulkan_backend.h"

#include <rex/logging.h>

namespace ac6::renderer {

bool VulkanBackend::IsSupported() const {
#if defined(__linux__)
  return true;
#else
  return false;
#endif
}

bool VulkanBackend::Initialize(const NativeRendererConfig& config) {
  (void)config;
  if (initialized_) {
    return true;
  }
  initialized_ = true;
  REXLOG_INFO("AC6 native renderer Vulkan backend initialized (scaffold)");
  return true;
}

void VulkanBackend::Shutdown() {
  if (!initialized_) {
    return;
  }
  initialized_ = false;
}

}  // namespace ac6::renderer
