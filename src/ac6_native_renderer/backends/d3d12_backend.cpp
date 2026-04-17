#include "d3d12_backend.h"

#include <rex/logging.h>

#if REX_HAS_D3D12
#include <d3d12.h>
#endif

namespace ac6::renderer {

bool D3D12Backend::IsSupported() const {
#if REX_HAS_D3D12 && defined(_WIN32)
  return true;
#else
  return false;
#endif
}

bool D3D12Backend::Initialize(const NativeRendererConfig& config) {
  (void)config;
  if (initialized_) {
    return true;
  }
  // Phase-1 scaffold: we deliberately do not create a device yet, to avoid
  // conflicting with the existing Rexglue provider during parallel bring-up.
  initialized_ = true;
  REXLOG_INFO("AC6 native renderer D3D12 backend initialized (scaffold)");
  return true;
}

void D3D12Backend::Shutdown() {
  if (!initialized_) {
    return;
  }
  initialized_ = false;
}

}  // namespace ac6::renderer
