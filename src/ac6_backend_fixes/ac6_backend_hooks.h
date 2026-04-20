#pragma once

#include <cstdint>
#include <string>

#include <native/audio/audio_runtime.h>
#include <rex/cvar.h>
#include <rex/system/interfaces/graphics.h>

#include "ac6_backend_capture_bridge.h"
#include "ac6_backend_pass_classifier.h"
#include "../d3d_state.h"

REXCVAR_DECLARE(bool, ac6_backend_debug_swap);
REXCVAR_DECLARE(bool, ac6_backend_log_signatures);

namespace ac6 {
struct FrameStats;
}

namespace ac6::backend {

enum class SwapSourceType : uint8_t {
  kUnknown,
  kGuestSwapTexture,
  kDirectDisplayFallback,
  kExperimentalReplayOverride,
};

struct BackendDiagnosticsSnapshot {
  bool valid = false;
  uint64_t frame_index = 0;
  uint64_t swap_submission_sequence = 0;
  bool swap_submission_valid = false;
  SwapSourceType swap_source = SwapSourceType::kUnknown;
  bool swap_source_scaled = false;
  uint32_t source_width = 0;
  uint32_t source_height = 0;
  uint32_t guest_output_width = 0;
  uint32_t guest_output_height = 0;
  uint32_t frontbuffer_width = 0;
  uint32_t frontbuffer_height = 0;
  uint32_t texture_format = 0;
  uint32_t color_space = 0;
  uint32_t audio_active_clients = 0;
  uint32_t audio_queued_frames = 0;
  uint32_t audio_peak_queued_frames = 0;
  uint32_t audio_dropped_frames = 0;
  uint32_t audio_underruns = 0;
  uint32_t audio_silence_injections = 0;
  uint32_t audio_startup_inflight_frames = 0;
  uint32_t audio_callback_dispatch_count = 0;
  uint32_t audio_callback_throttle_count = 0;
  uint64_t active_vertex_shader_hash = 0;
  uint64_t active_pixel_shader_hash = 0;
  uint64_t guest_vblank_interval_ticks = 0;
  uint64_t last_guest_vblank_tick = 0;
  uint64_t audio_consumed_frames = 0;
  uint64_t audio_queued_played_frames = 0;
  uint64_t audio_submitted_tic = 0;
  uint64_t audio_host_elapsed_tic = 0;
  double host_frame_time_ms = 0.0;
  double host_fps = 0.0;
  uint64_t host_frame_count = 0;
  uint32_t capture_draw_count = 0;
  uint32_t capture_clear_count = 0;
  uint32_t capture_resolve_count = 0;
  uint32_t repeated_signature_count = 0;
  bool audio_timing_valid = false;
  std::string audio_backend_name;
  std::string latest_signature_tags;
  RenderEventSignature latest_signature{};
};

void AnalyzeFrameBoundary(
    const ac6::d3d::FrameCaptureSnapshot& frame_capture,
    const ac6::d3d::FrameCaptureSummary& capture_summary,
    const ac6::d3d::ShadowState& shadow_state,
    const rex::system::GraphicsSwapSubmission* swap_submission,
    uint64_t swap_submission_sequence,
    uint64_t guest_vblank_interval_ticks,
    uint64_t last_guest_vblank_tick,
    const ac6::FrameStats& frame_stats,
    const rex::audio::AudioTelemetrySnapshot* audio_telemetry,
    const rex::audio::AudioClientTimingSnapshot* audio_timing);

void ReportSwapDecision(const rex::system::GraphicsSwapSubmission& submission,
                        uint64_t submission_sequence,
                        SwapSourceType swap_source,
                        bool swap_source_scaled,
                        uint32_t guest_output_width,
                        uint32_t guest_output_height,
                        uint32_t source_width,
                        uint32_t source_height,
                        uint64_t active_vertex_shader_hash,
                        uint64_t active_pixel_shader_hash);

BackendDiagnosticsSnapshot GetDiagnosticsSnapshot();
void ShutdownDiagnostics();

const char* ToString(SwapSourceType swap_source);

}  // namespace ac6::backend
