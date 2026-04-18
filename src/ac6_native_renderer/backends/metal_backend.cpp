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
  executor_status_ = {};
  executor_status_.initialized = true;
  initialized_ = true;
  REXLOG_INFO("AC6 native renderer Metal backend initialized (scaffold)");
  return true;
}

bool MetalBackend::SubmitExecutorFrame(const ReplayExecutorFrame& frame) {
  if (!initialized_) {
    return false;
  }
  executor_status_.initialized = true;
  executor_status_.frame_valid = frame.summary.valid;
  executor_status_.frame_index = frame.summary.frame_index;
  executor_status_.submitted_pass_count = frame.summary.pass_count;
  executor_status_.submitted_command_count = frame.summary.command_count;
  executor_status_.graphics_pass_count = frame.summary.graphics_pass_count;
  executor_status_.async_compute_pass_count =
      frame.summary.async_compute_pass_count;
  executor_status_.copy_pass_count = frame.summary.copy_pass_count;
  executor_status_.present_pass_count = frame.summary.present_pass_count;
  executor_status_.resource_translation_pass_count =
      frame.summary.resource_translation_pass_count;
  executor_status_.pipeline_state_pass_count =
      frame.summary.pipeline_state_pass_count;
  executor_status_.descriptor_setup_pass_count =
      frame.summary.descriptor_setup_pass_count;
  return true;
}

void MetalBackend::Shutdown() {
  if (!initialized_) {
    return;
  }
  executor_status_ = {};
  initialized_ = false;
}

}  // namespace ac6::renderer
