#include "ac6_native_graphics_overlay.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/xenos.h>
#include <rex/memory.h>

#include "ac6_native_graphics.h"

namespace ac6::graphics {
namespace {

struct DecodedTextureFetch {
  bool valid = false;
  uint32_t header_offset = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 0;
  uint32_t base_address = 0;
  const char* format_name = nullptr;
  rex::graphics::xenos::DataDimension dimension =
      rex::graphics::xenos::DataDimension::k2DOrStacked;
  bool tiled = false;
};

template <size_t N>
struct DecodedSampleSet {
  uint32_t count = 0;
  std::array<DecodedTextureFetch, N> entries{};
};

bool IsReadableGuestRange(rex::memory::Memory* memory, uint32_t guest_ptr, uint32_t size) {
  if (memory == nullptr || guest_ptr == 0 || size == 0) {
    return false;
  }
  const uint32_t guest_end = guest_ptr + size - 1;
  if (guest_end < guest_ptr) {
    return false;
  }
  auto* start_heap = memory->LookupHeap(guest_ptr);
  if (start_heap == nullptr) {
    return false;
  }
  auto* end_heap = memory->LookupHeap(guest_end);
  if (end_heap != start_heap) {
    return false;
  }
  const auto access = start_heap->QueryRangeAccess(guest_ptr, guest_end);
  return access == rex::memory::PageAccess::kReadOnly ||
         access == rex::memory::PageAccess::kReadWrite ||
         access == rex::memory::PageAccess::kExecuteReadOnly ||
         access == rex::memory::PageAccess::kExecuteReadWrite;
}

bool LoadTextureFetchAt(rex::memory::Memory* memory, uint32_t guest_ptr,
                        rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch) {
  if (!IsReadableGuestRange(memory, guest_ptr, sizeof(fetch))) {
    return false;
  }
  const uint8_t* fetch_base = memory->TranslateVirtual<const uint8_t*>(guest_ptr);
  if (fetch_base == nullptr) {
    return false;
  }
  fetch.dword_0 = rex::memory::load_and_swap<uint32_t>(fetch_base + 0);
  fetch.dword_1 = rex::memory::load_and_swap<uint32_t>(fetch_base + 4);
  fetch.dword_2 = rex::memory::load_and_swap<uint32_t>(fetch_base + 8);
  fetch.dword_3 = rex::memory::load_and_swap<uint32_t>(fetch_base + 12);
  fetch.dword_4 = rex::memory::load_and_swap<uint32_t>(fetch_base + 16);
  fetch.dword_5 = rex::memory::load_and_swap<uint32_t>(fetch_base + 20);
  return true;
}

bool IsPlausibleTextureFetchHeader(const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch) {
  using rex::graphics::xenos::DataDimension;

  if (static_cast<uint32_t>(fetch.format) >= 64) {
    return false;
  }

  switch (fetch.dimension) {
    case DataDimension::k1D:
    case DataDimension::k3D:
      return !fetch.stacked;
    case DataDimension::k2DOrStacked:
      return true;
    case DataDimension::kCube:
      return !fetch.stacked && fetch.size_2d.stack_depth == 5;
    default:
      return false;
  }
}

bool IsPlausibleTextureInfo(const rex::graphics::TextureInfo& info) {
  const uint32_t width = info.width + 1;
  const uint32_t height = info.height + 1;
  const uint32_t depth = info.depth + 1;
  return width >= 1 && width <= 8192 && height >= 1 && height <= 8192 && depth >= 1 &&
         depth <= 2048 && info.memory.base_address != 0 && info.format_info() != nullptr;
}

bool DecodeTextureFetch(uint32_t guest_ptr, DecodedTextureFetch& out) {
  rex::memory::Memory* memory = GetCapturedMemory();
  if (!IsReadableGuestRange(memory, guest_ptr, sizeof(rex::graphics::xenos::xe_gpu_texture_fetch_t))) {
    return false;
  }

  constexpr uint32_t kProbeLimit = 0x80;
  for (uint32_t offset = 0; offset + sizeof(rex::graphics::xenos::xe_gpu_texture_fetch_t) <=
                               kProbeLimit;
       offset += 4) {
    if (!IsReadableGuestRange(memory, guest_ptr + offset,
                              sizeof(rex::graphics::xenos::xe_gpu_texture_fetch_t))) {
      continue;
    }
    rex::graphics::xenos::xe_gpu_texture_fetch_t fetch{};
    if (!LoadTextureFetchAt(memory, guest_ptr + offset, fetch)) {
      continue;
    }
    if (fetch.type != rex::graphics::xenos::FetchConstantType::kTexture) {
      continue;
    }
    if (!IsPlausibleTextureFetchHeader(fetch)) {
      continue;
    }

    rex::graphics::TextureInfo texture_info;
    if (!rex::graphics::TextureInfo::Prepare(fetch, &texture_info) ||
        !IsPlausibleTextureInfo(texture_info)) {
      continue;
    }

    out.valid = true;
    out.header_offset = offset;
    out.dimension = texture_info.dimension;
    out.tiled = texture_info.is_tiled;
    out.width = texture_info.width + 1;
    out.height = texture_info.height + 1;
    out.depth = texture_info.depth + 1;
    out.base_address = texture_info.memory.base_address;
    out.format_name = texture_info.format_info()->name;
    return true;
  }
  return false;
}

std::string FormatDecodedTextureObject(uint32_t guest_ptr) {
  if (guest_ptr == 0) {
    return "none";
  }
  DecodedTextureFetch decoded;
  if (!DecodeTextureFetch(guest_ptr, decoded) || !decoded.valid) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%08X", guest_ptr);
    return buffer;
  }

  char buffer[128];
  const char* tiled_tag = decoded.tiled ? "t" : "l";
  if (decoded.dimension == rex::graphics::xenos::DataDimension::k3D || decoded.depth > 1) {
    std::snprintf(buffer, sizeof(buffer), "%08X+%02X[%ux%ux%u %s @%08X %s]", guest_ptr,
                  decoded.header_offset, decoded.width, decoded.height, decoded.depth, tiled_tag,
                  decoded.base_address, decoded.format_name ? decoded.format_name : "?");
  } else {
    std::snprintf(buffer, sizeof(buffer), "%08X+%02X[%ux%u %s @%08X %s]", guest_ptr,
                  decoded.header_offset, decoded.width, decoded.height, tiled_tag,
                  decoded.base_address, decoded.format_name ? decoded.format_name : "?");
  }
  return buffer;
}

template <typename Sample>
DecodedSampleSet<4> DecodeSampleSet(const Sample& sample) {
  DecodedSampleSet<4> decoded_set;
  const uint32_t shown =
      std::min<uint32_t>(sample.count, static_cast<uint32_t>(sample.values.size()));
  for (uint32_t i = 0; i < shown; ++i) {
    DecodedTextureFetch decoded;
    if (!DecodeTextureFetch(sample.values[i], decoded) || !decoded.valid) {
      continue;
    }
    decoded_set.entries[decoded_set.count++] = decoded;
  }
  return decoded_set;
}

template <size_t N>
bool SampleUsesBaseAddress(const DecodedSampleSet<N>& sample_set, uint32_t base_address) {
  if (base_address == 0) {
    return false;
  }
  for (uint32_t i = 0; i < sample_set.count; ++i) {
    if (sample_set.entries[i].base_address == base_address) {
      return true;
    }
  }
  return false;
}

std::string FormatResolveArgCandidates(
    const NativeGraphicsRuntimeStatus::ResolveDiagnosticsEntry& resolve) {
  std::string formatted;
  for (uint32_t i = 0; i < resolve.args.size(); ++i) {
    if (resolve.args[i] == 0) {
      continue;
    }
    DecodedTextureFetch decoded;
    if (!DecodeTextureFetch(resolve.args[i], decoded) || !decoded.valid) {
      continue;
    }
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "r%u=%08X+%02X[%ux%u @%08X %s]", 4u + i,
                  resolve.args[i], decoded.header_offset, decoded.width, decoded.height,
                  decoded.base_address, decoded.format_name ? decoded.format_name : "?");
    if (!formatted.empty()) {
      formatted.append(", ");
    }
    formatted.append(buffer);
  }
  return formatted.empty() ? "none" : formatted;
}

template <size_t N>
std::string FormatPassSources(const DecodedSampleSet<N>& first_fetch,
                              const DecodedSampleSet<N>& last_fetch,
                              const NativeGraphicsRuntimeStatus& status, uint32_t self_index) {
  std::string formatted;
  for (uint32_t other_index = 0; other_index < status.pass_diagnostics_count; ++other_index) {
    if (other_index == self_index) {
      continue;
    }
    const auto& other_pass = status.pass_diagnostics[other_index];
    if (!other_pass.valid || other_pass.render_target_0 == 0) {
      continue;
    }
    DecodedTextureFetch decoded_rt0;
    if (!DecodeTextureFetch(other_pass.render_target_0, decoded_rt0) || !decoded_rt0.valid ||
        decoded_rt0.base_address == 0) {
      continue;
    }
    if (!SampleUsesBaseAddress(first_fetch, decoded_rt0.base_address) &&
        !SampleUsesBaseAddress(last_fetch, decoded_rt0.base_address)) {
      continue;
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "#%u(rt0 #%u %ux%u @%08X)", other_index,
                  other_pass.pass_index, decoded_rt0.width, decoded_rt0.height,
                  decoded_rt0.base_address);
    if (!formatted.empty()) {
      formatted.append(", ");
    }
    formatted.append(buffer);
  }
  return formatted.empty() ? "none" : formatted;
}

template <size_t N>
std::string FormatResolveSources(const DecodedSampleSet<N>& first_fetch,
                                 const DecodedSampleSet<N>& last_fetch,
                                 const NativeGraphicsRuntimeStatus& status) {
  std::string formatted;
  for (uint32_t resolve_index = 0; resolve_index < status.resolve_diagnostics_count;
       ++resolve_index) {
    const auto& resolve = status.resolve_diagnostics[resolve_index];
    if (!resolve.valid) {
      continue;
    }
    for (uint32_t arg_index = 0; arg_index < resolve.args.size(); ++arg_index) {
      if (resolve.args[arg_index] == 0) {
        continue;
      }
      DecodedTextureFetch decoded;
      if (!DecodeTextureFetch(resolve.args[arg_index], decoded) || !decoded.valid ||
          decoded.base_address == 0) {
        continue;
      }
      if (!SampleUsesBaseAddress(first_fetch, decoded.base_address) &&
          !SampleUsesBaseAddress(last_fetch, decoded.base_address)) {
        continue;
      }
      char buffer[72];
      std::snprintf(buffer, sizeof(buffer), "#%u:r%u[%ux%u @%08X]", resolve_index, 4u + arg_index,
                    decoded.width, decoded.height, decoded.base_address);
      if (!formatted.empty()) {
        formatted.append(", ");
      }
      formatted.append(buffer);
    }
  }
  return formatted.empty() ? "none" : formatted;
}

template <typename Sample>
std::string FormatBoundResourceSample(const Sample& sample) {
  if (sample.count == 0) {
    return "none";
  }
  std::string formatted;
  const uint32_t shown = std::min<uint32_t>(sample.count, static_cast<uint32_t>(sample.values.size()));
  for (uint32_t i = 0; i < shown; ++i) {
    if (!formatted.empty()) {
      formatted.append(", ");
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%u:%08X", sample.slots[i], sample.values[i]);
    formatted.append(buffer);
  }
  if (sample.count > shown) {
    formatted.append(", ...");
  }
  return formatted;
}

template <typename Sample>
std::string FormatFetchConstantSample(const Sample& sample) {
  if (sample.count == 0) {
    return "none";
  }
  std::string formatted;
  const uint32_t shown = std::min<uint32_t>(sample.count, static_cast<uint32_t>(sample.values.size()));
  for (uint32_t i = 0; i < shown; ++i) {
    if (!formatted.empty()) {
      formatted.append(", ");
    }
    char buffer[96];
    DecodedTextureFetch decoded;
    if (DecodeTextureFetch(sample.values[i], decoded) && decoded.valid) {
      const char* tiled_tag = decoded.tiled ? "t" : "l";
      if (decoded.dimension == rex::graphics::xenos::DataDimension::k3D ||
          decoded.depth > 1) {
        std::snprintf(buffer, sizeof(buffer),
                      "%u:%08X+%02X[%ux%ux%u %s @%08X %s]", sample.slots[i], sample.values[i],
                      decoded.header_offset, decoded.width, decoded.height, decoded.depth,
                      tiled_tag, decoded.base_address,
                      decoded.format_name ? decoded.format_name : "?");
      } else {
        std::snprintf(buffer, sizeof(buffer), "%u:%08X+%02X[%ux%u %s @%08X %s]",
                      sample.slots[i], sample.values[i], decoded.header_offset, decoded.width,
                      decoded.height, tiled_tag, decoded.base_address,
                      decoded.format_name ? decoded.format_name : "?");
      }
    } else {
      std::snprintf(buffer, sizeof(buffer), "%u:%08X", sample.slots[i], sample.values[i]);
    }
    formatted.append(buffer);
  }
  if (sample.count > shown) {
    formatted.append(", ...");
  }
  return formatted;
}

}  // namespace

NativeGraphicsStatusDialog::NativeGraphicsStatusDialog(rex::ui::ImGuiDrawer* imgui_drawer)
    : ImGuiDialog(imgui_drawer) {}

NativeGraphicsStatusDialog::~NativeGraphicsStatusDialog() = default;

void NativeGraphicsStatusDialog::OnDraw(ImGuiIO& io) {
  (void)io;
  if (!visible_) {
    return;
  }

  if (!ImGui::Begin("AC6 Graphics Diagnostics##status", &visible_,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  const NativeGraphicsRuntimeStatus status = GetRuntimeStatus();
  const auto& diagnostics = status.backend_diagnostics;

  ImGui::Text("module: %s", status.enabled ? "enabled" : "disabled");
  ImGui::Text("mode: %.*s", static_cast<int>(ToString(status.mode).size()),
              ToString(status.mode).data());
  ImGui::Text("authoritative renderer: %s",
              status.authoritative_renderer_active ? "RexGlue/Xenia D3D12 backend"
                                                   : "disabled");
  ImGui::Text("capture active: %s", status.capture_enabled ? "yes" : "no");
  ImGui::Text("draw resolution scale: %ux%u", status.draw_resolution_scale_x,
              status.draw_resolution_scale_y);
  ImGui::Text("scaled tex offsets / direct host resolve: %s / %s",
              status.draw_resolution_scaled_texture_offsets ? "on" : "off",
              status.direct_host_resolve ? "on" : "off");
  ImGui::Text("experimental replay present override: %s",
              status.experimental_replay_present ? "enabled" : "disabled");
  ImGui::Text("analysis frames / replay frames: %llu / %llu",
              static_cast<unsigned long long>(status.analysis_frames_observed),
              static_cast<unsigned long long>(status.replay_frames_built));

  ImGui::Separator();
  ImGui::Text("capture frame: %llu",
              static_cast<unsigned long long>(status.capture_summary.frame_index));
  ImGui::Text("capture draws / clears / resolves: %u / %u / %u",
              status.capture_summary.draw_count, status.capture_summary.clear_count,
              status.capture_summary.resolve_count);
  ImGui::Text("capture indexed / shared / primitive: %u / %u / %u",
              status.capture_summary.indexed_draw_count,
              status.capture_summary.indexed_shared_draw_count,
              status.capture_summary.primitive_draw_count);
  ImGui::Text("capture rt0 switches / unique rt0: %u / %u",
              status.capture_summary.rt0_switch_count,
              status.capture_summary.unique_rt0_count);
  ImGui::Text("frame-end viewport: %ux%u",
              status.capture_summary.frame_end_viewport_width,
              status.capture_summary.frame_end_viewport_height);
  ImGui::Text("frontend passes scene/post/ui: %u / %u / %u / %u",
              status.frontend_summary.pass_count, status.frontend_summary.scene_pass_count,
              status.frontend_summary.post_process_pass_count,
              status.frontend_summary.ui_pass_count);
  if (status.frame_plan.present_stage.valid) {
    ImGui::Text(
        "present stage: #%u %s vp=%ux%u rt0=%08X depth=%08X draws/resolves=%u/%u",
        status.frame_plan.present_stage.pass_index,
        ac6::renderer::ToString(status.frame_plan.present_stage.kind),
        status.frame_plan.present_stage.viewport_width,
        status.frame_plan.present_stage.viewport_height,
        status.frame_plan.present_stage.render_target_0,
        status.frame_plan.present_stage.depth_stencil,
        status.frame_plan.present_stage.draw_count,
        status.frame_plan.present_stage.resolve_count);
  }
  if (status.frame_plan.scene_stage.valid) {
    ImGui::Text(
        "scene stage: #%u %s vp=%ux%u rt0=%08X depth=%08X draws/resolves=%u/%u",
        status.frame_plan.scene_stage.pass_index,
        ac6::renderer::ToString(status.frame_plan.scene_stage.kind),
        status.frame_plan.scene_stage.viewport_width,
        status.frame_plan.scene_stage.viewport_height,
        status.frame_plan.scene_stage.render_target_0,
        status.frame_plan.scene_stage.depth_stencil,
        status.frame_plan.scene_stage.draw_count,
        status.frame_plan.scene_stage.resolve_count);
  }
  if (status.frame_plan.post_process_stage.valid) {
    ImGui::Text(
        "post stage: #%u %s vp=%ux%u rt0=%08X depth=%08X draws/resolves=%u/%u",
        status.frame_plan.post_process_stage.pass_index,
        ac6::renderer::ToString(status.frame_plan.post_process_stage.kind),
        status.frame_plan.post_process_stage.viewport_width,
        status.frame_plan.post_process_stage.viewport_height,
        status.frame_plan.post_process_stage.render_target_0,
        status.frame_plan.post_process_stage.depth_stencil,
        status.frame_plan.post_process_stage.draw_count,
        status.frame_plan.post_process_stage.resolve_count);
  }
  if (status.frame_plan.ui_stage.valid) {
    ImGui::Text(
        "ui stage: #%u %s vp=%ux%u rt0=%08X depth=%08X draws/resolves=%u/%u",
        status.frame_plan.ui_stage.pass_index,
        ac6::renderer::ToString(status.frame_plan.ui_stage.kind),
        status.frame_plan.ui_stage.viewport_width,
        status.frame_plan.ui_stage.viewport_height,
        status.frame_plan.ui_stage.render_target_0,
        status.frame_plan.ui_stage.depth_stencil,
        status.frame_plan.ui_stage.draw_count,
        status.frame_plan.ui_stage.resolve_count);
  }
  for (uint32_t i = 0; i < status.pass_diagnostics_count; ++i) {
    const auto& pass = status.pass_diagnostics[i];
    if (!pass.valid) {
      continue;
    }
    ImGui::Text(
        "pass[%u]: #%u %s score=%u vp=%ux%u+%u+%u rt0=%08X depth=%08X d/c/r=%u/%u/%u tex/str/samp/fetch=%u/%u/%u/%u gpr=%u %s%s",
        i, pass.pass_index, ac6::renderer::ToString(pass.kind), pass.score,
        pass.viewport_width, pass.viewport_height, pass.viewport_x, pass.viewport_y,
        pass.render_target_0, pass.depth_stencil, pass.draw_count, pass.clear_count,
        pass.resolve_count, pass.max_texture_count, pass.max_stream_count,
        pass.max_sampler_count, pass.max_fetch_constant_count, pass.max_shader_gpr_alloc,
        pass.selected_for_present ? "present " : "",
        pass.matches_frame_end_viewport ? "frame_end" : "");
    ImGui::Text(
        "          sig=%016llX tfetch=%016llX->%016llX bind=%016llX->%016llX",
        static_cast<unsigned long long>(pass.pass_signature),
        static_cast<unsigned long long>(pass.first_texture_fetch_layout_signature),
        static_cast<unsigned long long>(pass.last_texture_fetch_layout_signature),
        static_cast<unsigned long long>(pass.first_resource_binding_signature),
        static_cast<unsigned long long>(pass.last_resource_binding_signature));
    const std::string first_textures = FormatBoundResourceSample(pass.first_textures);
    const std::string last_textures = FormatBoundResourceSample(pass.last_textures);
    const std::string first_fetch = FormatFetchConstantSample(pass.first_fetch_constants);
    const std::string last_fetch = FormatFetchConstantSample(pass.last_fetch_constants);
    const std::string rt0_object = FormatDecodedTextureObject(pass.render_target_0);
    const std::string depth_object = FormatDecodedTextureObject(pass.depth_stencil);
    const DecodedSampleSet<4> decoded_first_fetch = DecodeSampleSet(pass.first_fetch_constants);
    const DecodedSampleSet<4> decoded_last_fetch = DecodeSampleSet(pass.last_fetch_constants);
    const std::string source_passes =
        FormatPassSources(decoded_first_fetch, decoded_last_fetch, status, i);
    const std::string resolve_sources =
        FormatResolveSources(decoded_first_fetch, decoded_last_fetch, status);
    ImGui::Text("          tex %s -> %s", first_textures.c_str(), last_textures.c_str());
    ImGui::Text("          fet %s -> %s", first_fetch.c_str(), last_fetch.c_str());
    ImGui::Text("          rt0 %s depth %s", rt0_object.c_str(), depth_object.c_str());
    ImGui::Text("          src %s", source_passes.c_str());
    ImGui::Text("          rslv %s", resolve_sources.c_str());
  }
  for (uint32_t i = 0; i < status.resolve_diagnostics_count; ++i) {
    const auto& resolve = status.resolve_diagnostics[i];
    if (!resolve.valid) {
      continue;
    }
    ImGui::Text(
        "resolve[%u]: seq=%u vp=%ux%u rt0=%08X depth=%08X f1=%.3f", i, resolve.sequence,
        resolve.viewport_width, resolve.viewport_height, resolve.render_target_0,
        resolve.depth_stencil, resolve.depth_or_scale);
    const std::string candidates = FormatResolveArgCandidates(resolve);
    ImGui::Text("            %s", candidates.c_str());
  }

  ImGui::Separator();
  ImGui::Text("swap source: %s", ac6::backend::ToString(diagnostics.swap_source));
  ImGui::Text("frontbuffer / guest output: %ux%u / %ux%u",
              diagnostics.frontbuffer_width, diagnostics.frontbuffer_height,
              diagnostics.guest_output_width, diagnostics.guest_output_height);
  ImGui::Text("swap source extent: %ux%u (%s)",
              diagnostics.source_width, diagnostics.source_height,
              diagnostics.swap_source_scaled ? "scaled" : "unscaled");
  ImGui::Text("present classification: %s",
              ac6::backend::ToString(diagnostics.latest_signature.classification));
  ImGui::Text("signature: %016llX hits=%u",
              static_cast<unsigned long long>(diagnostics.latest_signature.stable_id),
              diagnostics.repeated_signature_count);
  const uint32_t signature_viewport_width = diagnostics.latest_signature.viewport_width;
  const uint32_t signature_viewport_height = diagnostics.latest_signature.viewport_height;
  const uint32_t viewport_scale_x = diagnostics.frontbuffer_width
                                        ? (signature_viewport_width * 100) /
                                              diagnostics.frontbuffer_width
                                        : 0;
  const uint32_t viewport_scale_y = diagnostics.frontbuffer_height
                                        ? (signature_viewport_height * 100) /
                                              diagnostics.frontbuffer_height
                                        : 0;
  ImGui::Text("signature viewport: %ux%u (%u%% x %u%% of frontbuffer)",
              signature_viewport_width, signature_viewport_height,
              viewport_scale_x, viewport_scale_y);
  ImGui::Text("signature point-list / primitive draws: %u / %u",
              diagnostics.latest_signature.topology_pointlist_count,
              diagnostics.latest_signature.primitive_draw_count);
  ImGui::Text("effect hints: half_res=%s quarter_res=%s point_sprites=%s additive=%s",
              diagnostics.latest_signature.half_res_like ? "yes" : "no",
              diagnostics.latest_signature.quarter_res_like ? "yes" : "no",
              diagnostics.latest_signature.point_sprite_like ? "yes" : "no",
              diagnostics.latest_signature.additive_like ? "yes" : "no");
  ImGui::Text("sampler hints: point min/mip=%u/%u linear min/mip=%u/%u mip clamp=%u max mip=%u",
              diagnostics.latest_signature.point_min_sampler_count,
              diagnostics.latest_signature.point_mip_sampler_count,
              diagnostics.latest_signature.linear_min_sampler_count,
              diagnostics.latest_signature.linear_mip_sampler_count,
              diagnostics.latest_signature.mip_clamp_sampler_count,
              diagnostics.latest_signature.max_sampler_mip_level);
  ImGui::TextWrapped("signature tags: %s",
                     diagnostics.latest_signature_tags.empty()
                         ? "none"
                         : diagnostics.latest_signature_tags.c_str());
  const bool draw_scale_is_native =
      status.draw_resolution_scale_x <= 1 && status.draw_resolution_scale_y <= 1;
  const bool draw_scale_is_scaled =
      status.draw_resolution_scale_x > 1 || status.draw_resolution_scale_y > 1;
  const bool likely_point_filtered =
      diagnostics.latest_signature.point_min_sampler_count != 0 ||
      diagnostics.latest_signature.point_mip_sampler_count != 0;
  if (draw_scale_is_native) {
    ImGui::TextWrapped(
        "likely cause: guest output is still native 720p/1x, so particles and low-res effect "
        "passes will look blocky on higher-resolution displays.");
  } else if (!diagnostics.swap_source_scaled) {
    ImGui::TextWrapped(
        "likely cause: draw scaling is enabled, but the presented swap source is still "
        "unscaled. This points to an unscaled resolve / swap-texture path.");
  } else if (status.direct_host_resolve) {
    ImGui::TextWrapped(
        "likely cause: draw scaling is active and the swap source is scaled, so the failure is "
        "likely in the scaled resolve path rather than scaling being ignored.");
  } else if (diagnostics.latest_signature.point_sprite_like && likely_point_filtered) {
    ImGui::TextWrapped(
        "likely cause: the active effect pass looks like point-sprite rendering with point "
        "sampling, so close sprites can stay pixelated even when they are near the camera.");
  }
  ImGui::Text("authoritative VS / PS: %016llX / %016llX",
              static_cast<unsigned long long>(diagnostics.active_vertex_shader_hash),
              static_cast<unsigned long long>(diagnostics.active_pixel_shader_hash));
  ImGui::Text("vblank interval / last tick: %llu / %llu",
              static_cast<unsigned long long>(diagnostics.guest_vblank_interval_ticks),
              static_cast<unsigned long long>(diagnostics.last_guest_vblank_tick));
  ImGui::Text("host frame time / fps: %.2f ms / %.2f",
              diagnostics.host_frame_time_ms, diagnostics.host_fps);

  ImGui::Separator();
  ImGui::Text("audio backend: %s",
              diagnostics.audio_backend_name.empty()
                  ? "unavailable"
                  : diagnostics.audio_backend_name.c_str());
  ImGui::Text("audio clients / queued / peak: %u / %u / %u",
              diagnostics.audio_active_clients, diagnostics.audio_queued_frames,
              diagnostics.audio_peak_queued_frames);
  ImGui::Text("audio underruns / dropped / silence inject: %u / %u / %u",
              diagnostics.audio_underruns, diagnostics.audio_dropped_frames,
              diagnostics.audio_silence_injections);
  ImGui::Text("audio consumed / queued-played / submitted tic: %llu / %llu / %llu",
              static_cast<unsigned long long>(diagnostics.audio_consumed_frames),
              static_cast<unsigned long long>(diagnostics.audio_queued_played_frames),
              static_cast<unsigned long long>(diagnostics.audio_submitted_tic));
  ImGui::Text("audio host tic: %llu",
              static_cast<unsigned long long>(diagnostics.audio_host_elapsed_tic));
  ImGui::Text("audio startup inflight / callback dispatch / throttle: %u / %u / %u",
              diagnostics.audio_startup_inflight_frames,
              diagnostics.audio_callback_dispatch_count,
              diagnostics.audio_callback_throttle_count);

  if (status.mode == GraphicsRuntimeMode::kLegacyReplayExperimental) {
    ImGui::Separator();
    ImGui::TextUnformatted("legacy replay diagnostics (experimental):");
    ImGui::Text("initialized: %s", status.initialized ? "true" : "false");
    ImGui::Text("init failures seen: %s", status.had_init_failure ? "true" : "false");
    ImGui::Text("replay backend: %s",
                ac6::renderer::ToString(status.active_backend).data());
    ImGui::Text("replay feature level: %s",
                ac6::renderer::ToString(status.feature_level).data());
    ImGui::Text("frontend / replay / execution commands: %u / %u / %u",
                status.frontend_summary.total_command_count,
                status.replay_summary.command_count,
                status.execution_summary.command_count);
    ImGui::Text("backend draw attempts / success: %u / %u",
                status.backend_executor_status.draw_attempt_count,
                status.backend_executor_status.draw_success_count);
    ImGui::Text("planned output: %ux%u", status.frame_plan.output_width,
                status.frame_plan.output_height);
  }

  ImGui::End();
}

}  // namespace ac6::graphics
