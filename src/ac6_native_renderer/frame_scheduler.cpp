#include "frame_scheduler.h"

namespace ac6::renderer {

void FrameScheduler::Configure(uint32_t max_frames_in_flight) {
  max_frames_in_flight_ = max_frames_in_flight == 0 ? 1 : max_frames_in_flight;
  frame_slot_ = 0;
}

void FrameScheduler::BeginFrame() {
  ++frame_index_;
  frame_slot_ = static_cast<uint32_t>(frame_index_ % max_frames_in_flight_);
}

}  // namespace ac6::renderer
