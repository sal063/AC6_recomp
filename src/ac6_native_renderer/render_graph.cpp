#include "render_graph.h"

#include <utility>

namespace ac6::renderer {

void RenderGraph::Reset() {
  passes_.clear();
}

uint32_t RenderGraph::AddPass(RenderPassDesc pass) {
  passes_.push_back(std::move(pass));
  return static_cast<uint32_t>(passes_.size() - 1);
}

}  // namespace ac6::renderer
