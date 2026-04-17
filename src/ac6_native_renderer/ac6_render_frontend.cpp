#include "ac6_render_frontend.h"

#include <algorithm>

namespace ac6::renderer {
namespace {

template <typename T, size_t N>
uint32_t CountNonZeroEntries(const std::array<T, N>& values) {
  uint32_t count = 0;
  for (const T& value : values) {
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

enum class CapturedFrameEventType : uint8_t {
  kDraw = 0,
  kClear = 1,
  kResolve = 2,
};

struct CapturedFrameEvent {
  uint32_t sequence = 0;
  CapturedFrameEventType type = CapturedFrameEventType::kDraw;
  const ac6::d3d::ShadowState* shadow_state = nullptr;
};

bool SamePassBinding(const ac6::d3d::ShadowState& left,
                     const ac6::d3d::ShadowState& right) {
  return left.render_targets[0] == right.render_targets[0] &&
         left.depth_stencil == right.depth_stencil &&
         left.viewport.x == right.viewport.x &&
         left.viewport.y == right.viewport.y &&
         left.viewport.width == right.viewport.width &&
         left.viewport.height == right.viewport.height;
}

bool MatchesFrameEndViewport(const ObservedPassDesc& pass,
                             const ac6::d3d::ShadowState& frame_end_shadow) {
  return pass.viewport_width != 0 && pass.viewport_height != 0 &&
         pass.viewport_width == frame_end_shadow.viewport.width &&
         pass.viewport_height == frame_end_shadow.viewport.height;
}

uint32_t ScorePassCandidate(const ObservedPassDesc& pass, bool is_last_pass) {
  uint32_t score = 0;
  score += std::min<uint32_t>(pass.draw_count * 3, 180u);
  score += std::min<uint32_t>(pass.clear_count * 8, 40u);
  score += std::min<uint32_t>(pass.resolve_count * 30, 90u);
  score += std::min<uint32_t>(pass.max_texture_count * 2, 24u);
  score += std::min<uint32_t>(pass.max_stream_count * 3, 24u);
  if (pass.matches_frame_end_viewport) {
    score += 80;
  }
  if (pass.render_target_0 != 0) {
    score += 20;
  }
  if (is_last_pass) {
    score += 25;
  }
  return score;
}

ObservedPassKind ClassifyPass(const ObservedPassDesc& pass, bool is_last_pass) {
  if (pass.resolve_count != 0) {
    return ObservedPassKind::kPostProcess;
  }
  const bool likely_ui =
      pass.draw_count != 0 && pass.clear_count == 0 && pass.max_texture_count >= 2 &&
      pass.max_stream_count <= 2 && pass.max_sampler_count <= 4 &&
      pass.viewport_width != 0 && pass.viewport_height != 0;
  if ((is_last_pass && pass.draw_count <= 16 && pass.clear_count == 0) ||
      (likely_ui && pass.draw_count <= 24 && pass.max_draw_call_count <= 1024)) {
    return ObservedPassKind::kUiComposite;
  }
  if (pass.draw_count != 0 || pass.clear_count != 0) {
    return ObservedPassKind::kScene;
  }
  return ObservedPassKind::kUnknown;
}

}  // namespace

const char* ToString(ObservedPassKind kind) {
  switch (kind) {
    case ObservedPassKind::kScene:
      return "scene";
    case ObservedPassKind::kPostProcess:
      return "post_process";
    case ObservedPassKind::kUiComposite:
      return "ui_composite";
    case ObservedPassKind::kUnknown:
    default:
      return "unknown";
  }
}

void Ac6RenderFrontend::Reset() {
  summary_ = {};
  passes_.clear();
}

FrontendFrameSummary Ac6RenderFrontend::BuildFromCapture(
    const ac6::d3d::FrameCaptureSnapshot& frame_capture) {
  Reset();

  summary_.frame_index = frame_capture.frame_index;
  summary_.capture_valid =
      !frame_capture.draws.empty() || !frame_capture.clears.empty() ||
      !frame_capture.resolves.empty();
  if (!summary_.capture_valid) {
    return summary_;
  }

  std::vector<CapturedFrameEvent> events;
  events.reserve(frame_capture.draws.size() + frame_capture.clears.size() +
                 frame_capture.resolves.size());

  for (const auto& draw : frame_capture.draws) {
    events.push_back({draw.sequence, CapturedFrameEventType::kDraw,
                      &draw.shadow_state});
    ++summary_.total_draw_count;
  }
  for (const auto& clear : frame_capture.clears) {
    events.push_back({clear.sequence, CapturedFrameEventType::kClear,
                      &clear.shadow_state});
    ++summary_.total_clear_count;
  }
  for (const auto& resolve : frame_capture.resolves) {
    events.push_back({resolve.sequence, CapturedFrameEventType::kResolve,
                      &resolve.shadow_state});
    ++summary_.total_resolve_count;
  }

  std::sort(events.begin(), events.end(),
            [](const CapturedFrameEvent& left, const CapturedFrameEvent& right) {
              return left.sequence < right.sequence;
            });

  const ac6::d3d::ShadowState empty_shadow{};
  const ac6::d3d::ShadowState* current_binding = &empty_shadow;
  bool current_pass_valid = false;
  ObservedPassDesc current_pass{};

  auto flush_pass = [&]() {
    if (!current_pass_valid) {
      return;
    }
    passes_.push_back(current_pass);
    current_pass = {};
    current_pass_valid = false;
  };

  for (const CapturedFrameEvent& event : events) {
    if (event.shadow_state == nullptr) {
      continue;
    }
    if (!current_pass_valid || !SamePassBinding(*current_binding, *event.shadow_state)) {
      flush_pass();
      current_binding = event.shadow_state;
      current_pass_valid = true;
      current_pass.start_sequence = event.sequence;
      current_pass.render_target_0 = event.shadow_state->render_targets[0];
      current_pass.depth_stencil = event.shadow_state->depth_stencil;
      current_pass.viewport_x = event.shadow_state->viewport.x;
      current_pass.viewport_y = event.shadow_state->viewport.y;
      current_pass.viewport_width = event.shadow_state->viewport.width;
      current_pass.viewport_height = event.shadow_state->viewport.height;
    }

    current_pass.end_sequence = event.sequence;
    switch (event.type) {
      case CapturedFrameEventType::kDraw:
        ++current_pass.draw_count;
        if (event.shadow_state != nullptr) {
          current_pass.max_texture_count = std::max(
              current_pass.max_texture_count,
              CountNonZeroEntries(event.shadow_state->textures));
          current_pass.max_stream_count = std::max(
              current_pass.max_stream_count,
              CountBoundStreams(event.shadow_state->streams));
          current_pass.max_sampler_count = std::max(
              current_pass.max_sampler_count,
              CountBoundSamplers(event.shadow_state->samplers));
          current_pass.max_fetch_constant_count = std::max(
              current_pass.max_fetch_constant_count,
              CountNonZeroEntries(event.shadow_state->texture_fetch_ptrs));
        }
        break;
      case CapturedFrameEventType::kClear:
        ++current_pass.clear_count;
        break;
      case CapturedFrameEventType::kResolve:
        ++current_pass.resolve_count;
        break;
    }
  }

  flush_pass();

  summary_.pass_count = static_cast<uint32_t>(passes_.size());
  if (passes_.empty()) {
    summary_.capture_valid = false;
    return summary_;
  }

  for (const auto& draw : frame_capture.draws) {
    for (ObservedPassDesc& pass : passes_) {
      if (draw.sequence < pass.start_sequence || draw.sequence > pass.end_sequence) {
        continue;
      }
      switch (draw.kind) {
        case ac6::d3d::DrawCallKind::kIndexed:
          ++pass.indexed_draw_count;
          break;
        case ac6::d3d::DrawCallKind::kIndexedShared:
          ++pass.indexed_shared_draw_count;
          break;
        case ac6::d3d::DrawCallKind::kPrimitive:
          ++pass.primitive_draw_count;
          break;
      }
      pass.max_draw_call_count = std::max(pass.max_draw_call_count, draw.count);
      break;
    }
  }

  uint32_t selected_pass_index = 0;
  uint32_t selected_pass_score = 0;
  bool selected_pass_valid = false;
  for (uint32_t i = 0; i < passes_.size(); ++i) {
    ObservedPassDesc& pass = passes_[i];
    const bool is_last_pass = (i + 1) == passes_.size();
    pass.matches_frame_end_viewport =
        MatchesFrameEndViewport(pass, frame_capture.frame_end_shadow);
    pass.kind = ClassifyPass(pass, is_last_pass);
    const uint32_t score = ScorePassCandidate(pass, is_last_pass);
    if (!selected_pass_valid || score > selected_pass_score ||
        (score == selected_pass_score && is_last_pass)) {
      selected_pass_valid = true;
      selected_pass_index = i;
      selected_pass_score = score;
    }

    switch (pass.kind) {
      case ObservedPassKind::kScene:
        ++summary_.scene_pass_count;
        break;
      case ObservedPassKind::kPostProcess:
        ++summary_.post_process_pass_count;
        break;
      case ObservedPassKind::kUiComposite:
        ++summary_.ui_pass_count;
        break;
      case ObservedPassKind::kUnknown:
        break;
    }
  }

  if (selected_pass_valid) {
    passes_[selected_pass_index].selected_for_present = true;
    summary_.selected_pass_index = selected_pass_index;
  }

  return summary_;
}

}  // namespace ac6::renderer
