#pragma once

#include <cstdint>
#include <string>

#include <rex/system/interfaces/graphics.h>

#include "../d3d_state.h"

namespace ac6::backend {

enum class SignatureClass : uint8_t {
  kUnknown,
  kScene,
  kPostProcess,
  kUiComposite,
  kParticles,
  kClouds,
  kSmoke,
  kExplosions,
  kMissileTrails,
};

struct RenderEventSignature {
  uint64_t stable_id = 0;
  uint64_t capture_record_signature = 0;
  uint64_t swap_texture_fetch_signature = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
  uint32_t draw_count = 0;
  uint32_t clear_count = 0;
  uint32_t resolve_count = 0;
  uint32_t indexed_draw_count = 0;
  uint32_t primitive_draw_count = 0;
  uint32_t texture_count = 0;
  uint32_t sampler_count = 0;
  uint32_t stream_count = 0;
  uint32_t fetch_constant_count = 0;
  uint32_t shader_gpr_alloc = 0;
  uint64_t active_vertex_shader_hash = 0;
  uint64_t active_pixel_shader_hash = 0;
  bool has_depth_stencil = false;
  bool has_resolve = false;
  bool half_res_like = false;
  bool post_process_like = false;
  bool ui_like = false;
  bool particle_like = false;
  bool additive_like = false;
  SignatureClass classification = SignatureClass::kUnknown;
};

uint64_t HashSwapTextureFetch(const rex::system::GraphicsSwapSubmission& submission);

RenderEventSignature BuildRenderEventSignature(
    const ac6::d3d::FrameCaptureSnapshot& frame_capture,
    const ac6::d3d::FrameCaptureSummary& capture_summary,
    const ac6::d3d::ShadowState& shadow_state,
    const rex::system::GraphicsSwapSubmission* swap_submission,
    uint64_t active_vertex_shader_hash,
    uint64_t active_pixel_shader_hash);

std::string BuildSignatureTags(const RenderEventSignature& signature);

}  // namespace ac6::backend
