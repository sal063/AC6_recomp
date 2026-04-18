#pragma once

#include <cstdint>
#include <string_view>

namespace ac6::renderer {

enum class BackendType : uint8_t {
  kUnknown = 0,
  kD3D12 = 1,
  kVulkan = 2,
  kMetal = 3,
};

enum class FeatureLevel : uint8_t {
  kBootstrap = 0,
  kSceneSubmission = 1,
  kParityValidation = 2,
  kShipping = 3,
};

struct NativeRendererConfig {
  BackendType preferred_backend = BackendType::kUnknown;
  FeatureLevel feature_level = FeatureLevel::kBootstrap;
  uint32_t max_frames_in_flight = 2;
  bool enable_debug_markers = true;
  bool enable_validation = true;
};

struct NativeRendererStats {
  bool initialized = false;
  BackendType active_backend = BackendType::kUnknown;
  uint64_t frame_count = 0;
  uint64_t built_pass_count = 0;
  uint64_t backend_submit_count = 0;
  uint64_t transient_allocation_count = 0;
};

struct BackendExecutorStatus {
  bool initialized = false;
  bool frame_valid = false;
  uint64_t frame_index = 0;
  uint32_t submitted_pass_count = 0;
  uint32_t submitted_command_count = 0;
  uint32_t graphics_pass_count = 0;
  uint32_t async_compute_pass_count = 0;
  uint32_t copy_pass_count = 0;
  uint32_t present_pass_count = 0;
  uint32_t resource_translation_pass_count = 0;
  uint32_t pipeline_state_pass_count = 0;
  uint32_t descriptor_setup_pass_count = 0;
};

constexpr std::string_view ToString(BackendType backend) {
  switch (backend) {
    case BackendType::kD3D12:
      return "d3d12";
    case BackendType::kVulkan:
      return "vulkan";
    case BackendType::kMetal:
      return "metal";
    default:
      return "unknown";
  }
}

constexpr std::string_view ToString(FeatureLevel level) {
  switch (level) {
    case FeatureLevel::kBootstrap:
      return "bootstrap";
    case FeatureLevel::kSceneSubmission:
      return "scene_submission";
    case FeatureLevel::kParityValidation:
      return "parity_validation";
    case FeatureLevel::kShipping:
      return "shipping";
    default:
      return "unknown";
  }
}

#if defined(_WIN32)
constexpr BackendType kPlatformDefaultBackend = BackendType::kD3D12;
#elif defined(__APPLE__)
constexpr BackendType kPlatformDefaultBackend = BackendType::kMetal;
#else
constexpr BackendType kPlatformDefaultBackend = BackendType::kVulkan;
#endif

inline BackendType ResolveBackend(BackendType preferred_backend) {
  return preferred_backend == BackendType::kUnknown ? kPlatformDefaultBackend
                                                    : preferred_backend;
}

}  // namespace ac6::renderer
