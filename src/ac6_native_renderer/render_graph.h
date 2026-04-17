#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ac6::renderer {

enum class RenderPassKind : uint8_t {
  kUnknown = 0,
  kScene = 1,
  kPostProcess = 2,
  kUiComposite = 3,
  kBootstrap = 4,
};

struct RenderPassDesc {
  std::string name;
  RenderPassKind kind = RenderPassKind::kUnknown;
  bool async_compute = false;
  uint32_t draw_count = 0;
  uint32_t clear_count = 0;
  uint32_t resolve_count = 0;
  uint32_t render_target_0 = 0;
  uint32_t depth_stencil = 0;
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
  bool selected_for_present = false;
};

class RenderGraph {
 public:
  void Reset();
  uint32_t AddPass(RenderPassDesc pass);

  uint32_t pass_count() const { return static_cast<uint32_t>(passes_.size()); }
  const std::vector<RenderPassDesc>& passes() const { return passes_; }

 private:
  std::vector<RenderPassDesc> passes_;
};

}  // namespace ac6::renderer
