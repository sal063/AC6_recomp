#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ac6_render_frontend.h"
#include "frame_plan.h"

namespace ac6::renderer {

enum class ReplayPassRole : uint8_t {
  kUnknown = 0,
  kBootstrap = 1,
  kScene = 2,
  kPostProcess = 3,
  kUiComposite = 4,
  kPresent = 5,
};

struct ReplayCommandDesc {
  ObservedCommandType type = ObservedCommandType::kDraw;
  uint32_t sequence = 0;
  ac6::d3d::DrawCallKind draw_kind = ac6::d3d::DrawCallKind::kIndexed;
  uint32_t primitive_type = 0;
  uint32_t start = 0;
  uint32_t count = 0;
  uint32_t flags = 0;
  uint32_t rect_count = 0;
  uint32_t captured_rect_count = 0;
  uint32_t color = 0;
  uint32_t stencil = 0;
  float depth = 1.0f;
  uint32_t texture_count = 0;
  uint32_t stream_count = 0;
  uint32_t sampler_count = 0;
  uint32_t fetch_constant_count = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t viewport_x = 0;
  uint32_t viewport_y = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
};

struct ReplayPassDesc {
  std::string name;
  ReplayPassRole role = ReplayPassRole::kUnknown;
  bool source_pass_valid = false;
  uint32_t source_pass_index = 0;
  uint32_t draw_count = 0;
  uint32_t clear_count = 0;
  uint32_t resolve_count = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
  bool selected_for_present = false;
  std::vector<ReplayCommandDesc> commands;
};

struct ReplayFrameSummary {
  bool valid = false;
  uint64_t frame_index = 0;
  uint32_t pass_count = 0;
  uint32_t command_count = 0;
  uint32_t draw_count = 0;
  uint32_t clear_count = 0;
  uint32_t resolve_count = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  bool has_present_pass = false;
};

struct ReplayFrame {
  ReplayFrameSummary summary{};
  std::vector<ReplayPassDesc> passes;
};

class ReplayIrBuilder {
 public:
  ReplayFrame BuildBootstrapFrame(uint64_t frame_index) const;
  ReplayFrame Build(const FrontendFrameSummary& summary,
                    const std::vector<ObservedPassDesc>& passes,
                    const NativeFramePlan& frame_plan) const;
};

const char* ToString(ReplayPassRole role);

}  // namespace ac6::renderer
