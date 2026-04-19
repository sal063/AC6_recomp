#include "d3d12_resource_tracker.h"

namespace ac6::renderer {

void D3D12ResourceTracker::TrackResource(ID3D12Resource* resource,
                                          D3D12_RESOURCE_STATES initial_state) {
  if (!resource) return;
  tracked_[resource] = {initial_state};
}

bool D3D12ResourceTracker::TransitionBarrier(ID3D12GraphicsCommandList* cmd_list,
                                              ID3D12Resource* resource,
                                              D3D12_RESOURCE_STATES target_state) {
  if (!resource || !cmd_list) return false;

  auto it = tracked_.find(resource);
  if (it == tracked_.end()) {
    // First time seeing this resource — assume COMMON
    tracked_[resource] = {D3D12_RESOURCE_STATE_COMMON};
    it = tracked_.find(resource);
  }

  if (it->second.current_state == target_state) {
    return false;  // No transition needed
  }

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = it->second.current_state;
  barrier.Transition.StateAfter = target_state;

  cmd_list->ResourceBarrier(1, &barrier);
  it->second.current_state = target_state;
  return true;
}

void D3D12ResourceTracker::FlushBarriers(ID3D12GraphicsCommandList* cmd_list) {
  if (pending_barriers_.empty() || !cmd_list) return;
  cmd_list->ResourceBarrier(static_cast<UINT>(pending_barriers_.size()),
                            pending_barriers_.data());
  pending_barriers_.clear();
}

void D3D12ResourceTracker::Reset() {
  tracked_.clear();
  pending_barriers_.clear();
}

}  // namespace ac6::renderer
