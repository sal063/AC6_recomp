#pragma once

#include <cstdint>

namespace ac6::renderer {

class FrameScheduler {
 public:
  void Configure(uint32_t max_frames_in_flight);
  void BeginFrame();

  uint64_t frame_index() const { return frame_index_; }
  uint32_t frame_slot() const { return frame_slot_; }
  uint32_t max_frames_in_flight() const { return max_frames_in_flight_; }

 private:
  uint64_t frame_index_ = 0;
  uint32_t frame_slot_ = 0;
  uint32_t max_frames_in_flight_ = 2;
};

}  // namespace ac6::renderer
