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
  executor_status_ = {};
  executor_status_.initialized = true;
  initialized_ = true;
  REXLOG_INFO("AC6 native renderer D3D12 backend initialized (scaffold)");
  return true;
}

bool D3D12Backend::SubmitExecutorFrame(const ReplayExecutorFrame& frame) {
  if (!initialized_) {
    return false;
  }

  executor_status_ = {
      .initialized = true,
      .frame_valid = frame.summary.valid,
      .frame_index = frame.summary.frame_index,
      .submitted_pass_count = frame.summary.pass_count,
      .submitted_command_count = frame.summary.command_count,
      .graphics_pass_count = frame.summary.graphics_pass_count,
      .async_compute_pass_count = frame.summary.async_compute_pass_count,
      .copy_pass_count = frame.summary.copy_pass_count,
      .present_pass_count = frame.summary.present_pass_count,
      .resource_translation_pass_count =
          frame.summary.resource_translation_pass_count,
      .pipeline_state_pass_count = frame.summary.pipeline_state_pass_count,
      .descriptor_setup_pass_count = frame.summary.descriptor_setup_pass_count,
  };

  REXLOG_TRACE(
      "AC6 native renderer D3D12 scaffold submit frame={} passes={} commands={} graphics={} present={} resource={} pso={} descriptors={}",
      executor_status_.frame_index, executor_status_.submitted_pass_count,
      executor_status_.submitted_command_count,
      executor_status_.graphics_pass_count, executor_status_.present_pass_count,
      executor_status_.resource_translation_pass_count,
      executor_status_.pipeline_state_pass_count,
      executor_status_.descriptor_setup_pass_count);
  return true;
}

void D3D12Backend::Shutdown() {
  if (!initialized_) {
    return;
  }
  executor_status_ = {};
  initialized_ = false;
}

}  // namespace ac6::renderer
