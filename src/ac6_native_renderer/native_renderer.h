#pragma once

#include <memory>

#include <rex/memory.h>

#include "ac6_render_frontend.h"
#include "execution_plan.h"
#include "frame_scheduler.h"
#include "frame_plan.h"
#include "replay_ir.h"
#include "replay_executor.h"
#include "render_device.h"
#include "render_graph.h"
#include "types.h"

// Forward declare so callers can access the output texture without pulling in
// all of d3d12_backend.h transitively.
struct ID3D12Resource;

namespace ac6::renderer {

// Experimental capture-replay renderer retained for diagnostics and future
// targeted overrides. It is not the default presentation path.
class NativeRenderer {
 public:
  NativeRenderer();
  ~NativeRenderer();

  NativeRenderer(const NativeRenderer&) = delete;
  NativeRenderer& operator=(const NativeRenderer&) = delete;

  bool Initialize(const NativeRendererConfig& config, rex::memory::Memory* memory);
  bool InitializeShared(const NativeRendererConfig& config, rex::memory::Memory* memory, ID3D12Device* device, ID3D12CommandQueue* queue);
  void Shutdown();

  void BeginFrame();
  void BuildBootstrapFrame();
  void BuildCapturedFrame(const ac6::d3d::FrameCaptureSnapshot& frame_capture);

  NativeRendererStats GetStats() const { return stats_; }
  FeatureLevel feature_level() const { return config_.feature_level; }
  uint32_t frame_slot() const { return scheduler_.frame_slot(); }
  uint32_t max_frames_in_flight() const { return scheduler_.max_frames_in_flight(); }
  FrontendFrameSummary frontend_summary() const { return frontend_.summary(); }
  const std::vector<ObservedPassDesc>& frontend_passes() const {
    return frontend_.passes();
  }
  NativeFramePlan frame_plan() const { return frame_plan_; }
  ReplayFrameSummary replay_summary() const { return replay_frame_.summary; }
  const ReplayFrame& replay_frame() const { return replay_frame_; }
  ExecutionFrameSummary execution_summary() const { return execution_plan_.summary; }
  const ExecutionFramePlan& execution_plan() const { return execution_plan_; }
  ReplayExecutorFrameSummary executor_summary() const { return executor_frame_.summary; }
  const ReplayExecutorFrame& executor_frame() const { return executor_frame_; }
  BackendExecutorStatus backend_executor_status() const {
    return device_.executor_status();
  }

  // Phase 4: returns the native output texture produced by the D3D12 backend,
  // or nullptr if not yet available. The texture is in
  // D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE after SubmitExecutorFrame.
  ID3D12Resource* GetOutputTexture() const;

  // Phase 4: returns the raw D3D12Backend* (nullptr for non-D3D12 backends).
  class D3D12Backend* GetD3D12Backend() const;

 private:
  NativeRendererConfig config_{};
  NativeRendererStats stats_{};
  RenderDevice device_{};
  RenderGraph graph_{};
  FrameScheduler scheduler_{};
  Ac6RenderFrontend frontend_{};
  FramePlanner planner_{};
  ReplayIrBuilder replay_builder_{};
  ExecutionPlanBuilder execution_builder_{};
  ReplayExecutorPlanBuilder executor_builder_{};
  NativeFramePlan frame_plan_{};
  ReplayFrame replay_frame_{};
  ExecutionFramePlan execution_plan_{};
  ReplayExecutorFrame executor_frame_{};
};

}  // namespace ac6::renderer
