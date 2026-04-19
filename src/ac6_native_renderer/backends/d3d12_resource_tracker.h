#pragma once

#include <unordered_map>
#include <vector>
#include <d3d12.h>

namespace ac6::renderer {

// Tracks D3D12 resource states for automatic barrier generation.
class D3D12ResourceTracker {
 public:
  void TrackResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initial_state);

  // Returns true if a barrier was needed and appended.
  bool TransitionBarrier(ID3D12GraphicsCommandList* cmd_list,
                         ID3D12Resource* resource,
                         D3D12_RESOURCE_STATES target_state);

  // Flush all pending barriers at once (batch mode).
  void FlushBarriers(ID3D12GraphicsCommandList* cmd_list);

  void Reset();

 private:
  struct TrackedResource {
    D3D12_RESOURCE_STATES current_state = D3D12_RESOURCE_STATE_COMMON;
  };

  std::unordered_map<ID3D12Resource*, TrackedResource> tracked_;
  std::vector<D3D12_RESOURCE_BARRIER> pending_barriers_;
};

}  // namespace ac6::renderer
