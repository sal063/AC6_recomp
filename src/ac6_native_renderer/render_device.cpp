#include "render_device.h"

#include <utility>

#include <rex/logging.h>

namespace ac6::renderer {

RenderDevice::RenderDevice() = default;

RenderDevice::~RenderDevice() {
  Shutdown();
}

bool RenderDevice::Initialize(const NativeRendererConfig& config) {
  Shutdown();

  active_backend_ = ResolveBackend(config.preferred_backend);
  backend_ = CreateBackend(active_backend_);
  if (!backend_) {
    REXLOG_ERROR("AC6 native renderer has no backend factory for {}",
                 ToString(active_backend_));
    active_backend_ = BackendType::kUnknown;
    return false;
  }
  if (!backend_->IsSupported()) {
    REXLOG_WARN("AC6 native renderer backend {} is not supported on this platform",
                backend_->GetName());
    backend_.reset();
    active_backend_ = BackendType::kUnknown;
    return false;
  }
  if (!backend_->Initialize(config)) {
    REXLOG_ERROR("AC6 native renderer backend {} failed initialization",
                 backend_->GetName());
    backend_.reset();
    active_backend_ = BackendType::kUnknown;
    return false;
  }

  initialized_ = true;
  REXLOG_INFO("AC6 native renderer initialized backend={} feature_level={} frames_in_flight={}",
              backend_->GetName(), ToString(config.feature_level),
              config.max_frames_in_flight);
  return true;
}

void RenderDevice::Shutdown() {
  if (backend_) {
    backend_->Shutdown();
    backend_.reset();
  }
  initialized_ = false;
  active_backend_ = BackendType::kUnknown;
}

bool RenderDevice::SubmitExecutorFrame(const ReplayExecutorFrame& frame) {
  if (!backend_ || !initialized_) {
    return false;
  }
  return backend_->SubmitExecutorFrame(frame);
}

std::string_view RenderDevice::backend_name() const {
  return backend_ ? backend_->GetName() : ToString(BackendType::kUnknown);
}

BackendExecutorStatus RenderDevice::executor_status() const {
  return backend_ ? backend_->GetExecutorStatus() : BackendExecutorStatus{};
}

}  // namespace ac6::renderer
