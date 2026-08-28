/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/logging.h>
#include <native/ui/renderdoc_api.h>

namespace rex {
namespace ui {

std::unique_ptr<RenderDocAPI> RenderDocAPI::CreateIfConnected() {
  std::unique_ptr<RenderDocAPI> renderdoc_api(new RenderDocAPI());

  pRENDERDOC_GetAPI get_api = nullptr;

  // Must not LOAD RenderDoc - only notice one that has injected itself. On
  // Linux librenderdoc.so sits in a system library directory whenever
  // RenderDoc is installed, so a plain dlopen here pulled it into every run,
  // which is what put its overlay on the swapchain (and silently turned
  // gpu_debug_markers on via IsGpuDebugMarkersEnabled).
  if (!renderdoc_api->library_.LoadIfAlreadyLoaded(platform::lib_names::kRenderDoc)) {
    return nullptr;
  }
  get_api = renderdoc_api->library_.GetSymbol<pRENDERDOC_GetAPI>("RENDERDOC_GetAPI");

  if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, (void**)&renderdoc_api->api_1_0_0_) ||
      !renderdoc_api->api_1_0_0_) {
    return nullptr;
  }

  REXLOG_INFO("RenderDoc API initialized");

  return renderdoc_api;
}

RenderDocAPI::~RenderDocAPI() {
  library_.Close();
}

}  // namespace ui
}  // namespace rex
