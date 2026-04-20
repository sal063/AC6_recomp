#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <rex/memory.h>

#include "ac6_backend_fixes/ac6_backend_hooks.h"
#include "ac6_native_renderer/ac6_render_frontend.h"
#include "ac6_native_renderer/execution_plan.h"
#include "ac6_native_renderer/frame_plan.h"
#include "ac6_native_renderer/replay_executor.h"
#include "ac6_native_renderer/replay_ir.h"
#include "ac6_native_renderer/types.h"
#include "d3d_state.h"

struct ID3D12Resource;

namespace ac6::graphics {

enum class GraphicsRuntimeMode : uint8_t {
  kDisabled,
  kAnalysisOnly,
  kHybridBackendFixes,
  kLegacyReplayExperimental,
};

std::string_view ToString(GraphicsRuntimeMode mode);

struct NativeGraphicsRuntimeStatus {
  struct PassDiagnosticsEntry {
    struct BoundResourceSample {
      uint32_t count = 0;
      std::array<uint32_t, 4> slots{};
      std::array<uint32_t, 4> values{};
    };

    bool valid = false;
    uint32_t pass_index = 0;
    ac6::renderer::ObservedPassKind kind = ac6::renderer::ObservedPassKind::kUnknown;
    uint32_t score = 0;
    uint32_t render_target_0 = 0;
    uint32_t depth_stencil = 0;
    uint32_t viewport_x = 0;
    uint32_t viewport_y = 0;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    uint32_t draw_count = 0;
    uint32_t clear_count = 0;
    uint32_t resolve_count = 0;
    uint32_t max_texture_count = 0;
    uint32_t max_stream_count = 0;
    uint32_t max_sampler_count = 0;
    uint32_t max_fetch_constant_count = 0;
    uint32_t max_shader_gpr_alloc = 0;
    uint64_t pass_signature = 0;
    uint64_t first_texture_fetch_layout_signature = 0;
    uint64_t last_texture_fetch_layout_signature = 0;
    uint64_t first_resource_binding_signature = 0;
    uint64_t last_resource_binding_signature = 0;
    BoundResourceSample first_textures{};
    BoundResourceSample last_textures{};
    BoundResourceSample first_fetch_constants{};
    BoundResourceSample last_fetch_constants{};
    bool selected_for_present = false;
    bool matches_frame_end_viewport = false;
  };

  struct ResolveDiagnosticsEntry {
    bool valid = false;
    uint32_t sequence = 0;
    uint32_t render_target_0 = 0;
    uint32_t depth_stencil = 0;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    std::array<uint32_t, 7> args{};
    float depth_or_scale = 0.0f;
  };

  bool enabled = false;
  GraphicsRuntimeMode mode = GraphicsRuntimeMode::kHybridBackendFixes;
  bool capture_enabled = false;
  bool authoritative_renderer_active = false;
  bool experimental_replay_present = false;
  uint32_t draw_resolution_scale_x = 1;
  uint32_t draw_resolution_scale_y = 1;
  bool direct_host_resolve = true;
  bool draw_resolution_scaled_texture_offsets = true;
  bool initialized = false;
  bool had_init_failure = false;
  bool showing_latched_snapshot = false;
  uint64_t init_attempts = 0;
  uint64_t init_successes = 0;
  uint64_t analysis_frames_observed = 0;
  uint64_t replay_frames_built = 0;
  uint64_t latest_capture_frame_index = 0;
  uint64_t latest_renderer_frame_index = 0;
  uint64_t last_meaningful_capture_frame_index = 0;
  uint64_t last_meaningful_renderer_frame_index = 0;

  ac6::renderer::BackendType active_backend = ac6::renderer::BackendType::kUnknown;
  ac6::renderer::FeatureLevel feature_level = ac6::renderer::FeatureLevel::kBootstrap;
  ac6::renderer::NativeRendererStats renderer_stats{};
  ac6::renderer::FrontendFrameSummary frontend_summary{};
  ac6::renderer::ReplayFrameSummary replay_summary{};
  ac6::renderer::ExecutionFrameSummary execution_summary{};
  ac6::renderer::ReplayExecutorFrameSummary executor_summary{};
  ac6::renderer::BackendExecutorStatus backend_executor_status{};
  ac6::d3d::FrameCaptureSummary capture_summary{};
  ac6::backend::BackendDiagnosticsSnapshot backend_diagnostics{};
  ac6::renderer::NativeFramePlan frame_plan{};
  uint32_t pass_diagnostics_count = 0;
  std::array<PassDiagnosticsEntry, 6> pass_diagnostics{};
  uint32_t resolve_diagnostics_count = 0;
  std::array<ResolveDiagnosticsEntry, 8> resolve_diagnostics{};
};

void OnFrameBoundary(rex::memory::Memory* memory);
void Shutdown();

NativeGraphicsRuntimeStatus GetRuntimeStatus();
ID3D12Resource* GetNativeOutputTexture();
rex::memory::Memory* GetCapturedMemory();

}  // namespace ac6::graphics
