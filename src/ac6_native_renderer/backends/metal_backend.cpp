#include "ac6_native_renderer/backends/metal_backend.h"

#include <rex/logging.h>

namespace ac6::renderer {

bool MetalBackend::IsSupported() const {
#if defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

bool MetalBackend::Initialize(const NativeRendererConfig& config) {
  (void)config;
  if (initialized_) {
    return true;
  }
  initialized_ = true;
  REXLOG_INFO("AC6 native renderer Metal backend initialized (scaffold)");
  return true;
}

void MetalBackend::Shutdown() {
  if (!initialized_) {
    return;
  }
  initialized_ = false;
}

}  // namespace ac6::renderer
