#include "ac6_backend_capture_bridge.h"

#include <algorithm>
#include <array>

namespace ac6::backend {
namespace {

template <typename Container>
uint32_t CountNonZero(const Container& values) {
  uint32_t count = 0;
  for (const auto& value : values) {
    if (value) {
      ++count;
    }
  }
  return count;
}

uint32_t CountBoundStreams(
    const std::array<ac6::d3d::StreamBinding, ac6::d3d::kMaxStreams>& streams) {
  uint32_t count = 0;
  for (const auto& stream : streams) {
    if (stream.buffer) {
      ++count;
    }
  }
  return count;
}

uint32_t CountBoundSamplers(
    const std::array<ac6::d3d::SamplerBinding, ac6::d3d::kMaxSamplers>& samplers) {
  uint32_t count = 0;
  for (const auto& sampler : samplers) {
    if (sampler.mag_filter || sampler.min_filter || sampler.mip_filter ||
        sampler.mip_level || sampler.border_color) {
      ++count;
    }
  }
  return count;
}

bool IsSamplerBound(const ac6::d3d::SamplerBinding& sampler) {
  return sampler.mag_filter || sampler.min_filter || sampler.mip_filter ||
         sampler.mip_level || sampler.border_color;
}

struct SamplerFilterSummary {
  uint32_t point_min_count = 0;
  uint32_t linear_min_count = 0;
  uint32_t point_mip_count = 0;
  uint32_t linear_mip_count = 0;
  uint32_t anisotropic_count = 0;
  uint32_t mip_clamp_count = 0;
  uint32_t max_mip_level = 0;
};

SamplerFilterSummary SummarizeSamplerFiltering(
    const std::array<ac6::d3d::SamplerBinding, ac6::d3d::kMaxSamplers>& samplers) {
  constexpr uint32_t kTexfPoint = 1;
  constexpr uint32_t kTexfLinear = 2;
  constexpr uint32_t kTexfAnisotropic = 3;

  SamplerFilterSummary summary;
  for (const auto& sampler : samplers) {
    if (!IsSamplerBound(sampler)) {
      continue;
    }

    if (sampler.min_filter == kTexfPoint) {
      ++summary.point_min_count;
    } else if (sampler.min_filter == kTexfLinear) {
      ++summary.linear_min_count;
    }

    if (sampler.mip_filter == kTexfPoint) {
      ++summary.point_mip_count;
    } else if (sampler.mip_filter == kTexfLinear) {
      ++summary.linear_mip_count;
    }

    if (sampler.mag_filter == kTexfAnisotropic || sampler.min_filter == kTexfAnisotropic) {
      ++summary.anisotropic_count;
    }

    if (sampler.mip_level != 0) {
      ++summary.mip_clamp_count;
      summary.max_mip_level = std::max(summary.max_mip_level, sampler.mip_level);
    }
  }
  return summary;
}

void HashU32(uint64_t& hash, uint32_t value) {
  constexpr uint64_t kFnvPrime = 1099511628211ull;
  hash ^= value;
  hash *= kFnvPrime;
}

void HashU64(uint64_t& hash, uint64_t value) {
  HashU32(hash, uint32_t(value & 0xFFFFFFFFull));
  HashU32(hash, uint32_t(value >> 32));
}

bool IsHalfResLike(const ac6::d3d::ShadowState& shadow_state,
                   const rex::system::GraphicsSwapSubmission* swap_submission) {
  if (!swap_submission || !swap_submission->frontbuffer_width ||
      !swap_submission->frontbuffer_height || !shadow_state.viewport.width ||
      !shadow_state.viewport.height) {
    return false;
  }

  const uint32_t swap_width = swap_submission->frontbuffer_width;
  const uint32_t swap_height = swap_submission->frontbuffer_height;
  return shadow_state.viewport.width * 4 <= swap_width * 3 ||
         shadow_state.viewport.height * 4 <= swap_height * 3;
}

bool IsQuarterResLike(const ac6::d3d::ShadowState& shadow_state,
                      const rex::system::GraphicsSwapSubmission* swap_submission) {
  if (!swap_submission || !swap_submission->frontbuffer_width ||
      !swap_submission->frontbuffer_height || !shadow_state.viewport.width ||
      !shadow_state.viewport.height) {
    return false;
  }

  const uint32_t swap_width = swap_submission->frontbuffer_width;
  const uint32_t swap_height = swap_submission->frontbuffer_height;
  return shadow_state.viewport.width * 2 <= swap_width &&
         shadow_state.viewport.height * 2 <= swap_height;
}

bool IsLikelyUiPass(const ac6::d3d::FrameCaptureSummary& capture_summary,
                    const ac6::d3d::ShadowState& shadow_state,
                    const rex::system::GraphicsSwapSubmission* swap_submission) {
  if (shadow_state.depth_stencil != 0 || !swap_submission ||
      !swap_submission->frontbuffer_width || !swap_submission->frontbuffer_height) {
    return false;
  }

  const bool viewport_matches_swap =
      shadow_state.viewport.width == swap_submission->frontbuffer_width &&
      shadow_state.viewport.height == swap_submission->frontbuffer_height;
  const bool low_complexity =
      capture_summary.draw_count > 0 && capture_summary.draw_count <= 96 &&
      capture_summary.resolve_count == 0;
  return viewport_matches_swap && low_complexity;
}

bool IsLikelyParticlePass(const ac6::d3d::FrameCaptureSummary& capture_summary,
                          const ac6::d3d::ShadowState& shadow_state) {
  const bool mostly_primitive =
      capture_summary.primitive_draw_count > capture_summary.indexed_draw_count &&
      capture_summary.primitive_draw_count > 0;
  const bool light_bindings =
      CountBoundSamplers(shadow_state.samplers) <= 4 &&
      CountBoundStreams(shadow_state.streams) <= 4;
  return mostly_primitive || (capture_summary.resolve_count > 0 && light_bindings &&
                              shadow_state.depth_stencil == 0);
}

bool IsLikelyPointSpritePass(const ac6::d3d::FrameCaptureSummary& capture_summary,
                             const ac6::d3d::ShadowState& shadow_state) {
  if (capture_summary.topology_pointlist == 0 || capture_summary.primitive_draw_count == 0) {
    return false;
  }

  const bool mostly_point_sprites =
      capture_summary.topology_pointlist * 2 >= capture_summary.primitive_draw_count;
  const bool compact_stream_layout = CountBoundStreams(shadow_state.streams) <= 2;
  const bool textured = CountNonZero(shadow_state.textures) != 0;
  return mostly_point_sprites && compact_stream_layout && textured;
}

bool IsLikelyAdditive(const ac6::d3d::FrameCaptureSummary& capture_summary,
                      const ac6::d3d::ShadowState& shadow_state) {
  if (shadow_state.depth_stencil != 0) {
    return false;
  }
  return capture_summary.clear_count == 0 &&
         CountNonZero(shadow_state.textures) > 0 &&
         CountBoundSamplers(shadow_state.samplers) > 0;
}

}  // namespace

uint64_t HashSwapTextureFetch(const rex::system::GraphicsSwapSubmission& submission) {
  constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
  uint64_t hash = kFnvOffsetBasis;
  for (uint32_t word : submission.texture_fetch) {
    HashU32(hash, word);
  }
  return hash;
}

RenderEventSignature BuildRenderEventSignature(
    const ac6::d3d::FrameCaptureSnapshot& frame_capture,
    const ac6::d3d::FrameCaptureSummary& capture_summary,
    const ac6::d3d::ShadowState& shadow_state,
    const rex::system::GraphicsSwapSubmission* swap_submission,
    const uint64_t active_vertex_shader_hash,
    const uint64_t active_pixel_shader_hash) {
  constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
  (void)frame_capture;

  RenderEventSignature signature;
  signature.capture_record_signature = capture_summary.record_signature;
  signature.swap_texture_fetch_signature =
      swap_submission ? HashSwapTextureFetch(*swap_submission) : 0;
  signature.render_target_0 = shadow_state.render_targets[0];
  signature.depth_stencil = shadow_state.depth_stencil;
  signature.viewport_width = shadow_state.viewport.width;
  signature.viewport_height = shadow_state.viewport.height;
  signature.draw_count = capture_summary.draw_count;
  signature.clear_count = capture_summary.clear_count;
  signature.resolve_count = capture_summary.resolve_count;
  signature.indexed_draw_count = capture_summary.indexed_draw_count;
  signature.primitive_draw_count = capture_summary.primitive_draw_count;
  signature.topology_pointlist_count = capture_summary.topology_pointlist;
  signature.texture_count = CountNonZero(shadow_state.textures);
  signature.sampler_count = CountBoundSamplers(shadow_state.samplers);
  const SamplerFilterSummary sampler_filters = SummarizeSamplerFiltering(shadow_state.samplers);
  signature.point_min_sampler_count = sampler_filters.point_min_count;
  signature.linear_min_sampler_count = sampler_filters.linear_min_count;
  signature.point_mip_sampler_count = sampler_filters.point_mip_count;
  signature.linear_mip_sampler_count = sampler_filters.linear_mip_count;
  signature.anisotropic_sampler_count = sampler_filters.anisotropic_count;
  signature.mip_clamp_sampler_count = sampler_filters.mip_clamp_count;
  signature.max_sampler_mip_level = sampler_filters.max_mip_level;
  signature.stream_count = CountBoundStreams(shadow_state.streams);
  signature.fetch_constant_count = CountNonZero(shadow_state.texture_fetch_ptrs);
  signature.shader_gpr_alloc = shadow_state.shader_gpr_alloc;
  signature.active_vertex_shader_hash = active_vertex_shader_hash;
  signature.active_pixel_shader_hash = active_pixel_shader_hash;
  signature.has_depth_stencil = shadow_state.depth_stencil != 0;
  signature.has_resolve = capture_summary.resolve_count != 0;
  signature.half_res_like = IsHalfResLike(shadow_state, swap_submission);
  signature.quarter_res_like = IsQuarterResLike(shadow_state, swap_submission);
  signature.post_process_like =
      signature.has_resolve && !signature.has_depth_stencil;
  signature.ui_like =
      IsLikelyUiPass(capture_summary, shadow_state, swap_submission);
  signature.particle_like =
      IsLikelyParticlePass(capture_summary, shadow_state);
  signature.point_sprite_like =
      IsLikelyPointSpritePass(capture_summary, shadow_state);
  signature.point_filtered_like =
      sampler_filters.point_min_count != 0 || sampler_filters.point_mip_count != 0;
  signature.mip_clamped_like = sampler_filters.mip_clamp_count != 0;
  signature.additive_like =
      IsLikelyAdditive(capture_summary, shadow_state);

  uint64_t hash = kFnvOffsetBasis;
  HashU64(hash, signature.capture_record_signature);
  HashU64(hash, signature.swap_texture_fetch_signature);
  HashU32(hash, signature.render_target_0);
  HashU32(hash, signature.depth_stencil);
  HashU32(hash, signature.viewport_width);
  HashU32(hash, signature.viewport_height);
  HashU32(hash, signature.draw_count);
  HashU32(hash, signature.resolve_count);
  HashU32(hash, signature.texture_count);
  HashU32(hash, signature.sampler_count);
  HashU32(hash, signature.stream_count);
  HashU32(hash, signature.fetch_constant_count);
  HashU32(hash, signature.shader_gpr_alloc);
  HashU64(hash, shadow_state.vertex_fetch_layout_signature);
  HashU64(hash, shadow_state.texture_fetch_layout_signature);
  HashU64(hash, shadow_state.resource_binding_signature);
  signature.stable_id = hash;

  return signature;
}

std::string BuildSignatureTags(const RenderEventSignature& signature) {
  std::string tags;
  auto append = [&tags](const char* token) {
    if (!tags.empty()) {
      tags.append(", ");
    }
    tags.append(token);
  };

  if (signature.has_depth_stencil) {
    append("depth");
  }
  if (signature.has_resolve) {
    append("resolve");
  }
  if (signature.half_res_like) {
    append("half_res");
  }
  if (signature.quarter_res_like) {
    append("quarter_res");
  }
  if (signature.post_process_like) {
    append("post");
  }
  if (signature.ui_like) {
    append("ui");
  }
  if (signature.particle_like) {
    append("particles");
  }
  if (signature.point_sprite_like) {
    append("point_sprites");
  }
  if (signature.point_filtered_like) {
    append("point_filter");
  }
  if (signature.mip_clamped_like) {
    append("mip_clamp");
  }
  if (signature.anisotropic_sampler_count != 0) {
    append("aniso");
  }
  if (signature.additive_like) {
    append("additive");
  }
  if (tags.empty()) {
    tags = "unclassified";
  }
  return tags;
}

}  // namespace ac6::backend
