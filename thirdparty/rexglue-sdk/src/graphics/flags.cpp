/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <rex/ui/renderdoc_api.h>

REXCVAR_DEFINE_BOOL(gpu_allow_invalid_fetch_constants, false, "GPU",
                    "Allow invalid fetch constants");
REXCVAR_DEFINE_BOOL(native_2x_msaa, true, "GPU", "Enable native 2x MSAA");
REXCVAR_DEFINE_BOOL(depth_float24_round, false, "GPU", "Round float24 depth values");
REXCVAR_DEFINE_BOOL(depth_float24_convert_in_pixel_shader, false, "GPU",
                    "Convert float24 depth in pixel shader");
REXCVAR_DEFINE_BOOL(depth_transfer_not_equal_test, true, "GPU",
                    "Use not-equal test for depth transfer");
REXCVAR_DEFINE_BOOL(gamma_render_target_as_unorm16, true, "GPU",
                    "Use R16G16B16A16_UNORM for gamma render targets (more accurate than sRGB)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU", "Path to dump shaders to");
REXCVAR_DEFINE_BOOL(use_fuzzy_alpha_epsilon, true, "GPU",
                    "Use approximate compare for alpha test values to prevent "
                    "flickering on NVIDIA graphics cards");
REXCVAR_DEFINE_BOOL(vfetch_index_rounding_bias, false, "GPU/Shader",
                    "Apply small epsilon bias to vertex fetch indices before "
                    "flooring to fix black triangles caused by RCP precision");
REXCVAR_DEFINE_BOOL(draw_resolution_scaled_texture_offsets, true, "GPU/Shader",
                    "Scale texture offsets with draw resolution");
REXCVAR_DEFINE_BOOL(param_gen_integer_guest_position, false, "GPU/Shader",
                    "At >1x draw resolution scale, floor the PsParamGen pixel "
                    "position to the integer guest-pixel index instead of keeping "
                    "the sub-guest-pixel fraction. Fixes the mosaic in games that "
                    "feed the position into their own integer pixel-address math "
                    "(e.g. AC6's deferred EDRAM restore/de-swizzle passes), whose "
                    "frac()-based bit extraction otherwise sees a doubled period and "
                    "scrambles the sample coordinate. Those passes then sample at "
                    "guest resolution. Off by default; harmless for shaders that pass "
                    "the position straight to tfetch only at 1x.");
REXCVAR_DEFINE_BOOL(param_gen_host_subpixel_restore, false, "GPU/Shader",
                    "Builds on param_gen_integer_guest_position (and implies it): for "
                    "resolution-scaled, position-derived 2D samples in pixel shaders "
                    "that use PsParamGen, re-adds the host sub-pixel offset to the "
                    "sample coordinate so the de-swizzle restore passes sample at full "
                    "host resolution instead of the guest-texel center. Turns the "
                    "mosaic fix from clean-but-soft into true resolution-scaled detail. "
                    "Off by default; may need per-shader scoping if it disturbs other "
                    "passes that read scaled render targets via interpolated coords.");
REXCVAR_DEFINE_STRING(ac6_neutralize_deswizzle_hashes, "", "GPU/Shader",
                    "AC6: comma/space-separated tokens \"<hash>[:<slot>[+<slot>...]]\" "
                    "naming guest pixel-shader ucode hashes (hex) whose param_gen 2D "
                    "texture samples manually de-swizzle the raw EDRAM sub-tile order of "
                    "their source. The emulator's texture cache detiles everything to "
                    "linear, so that de-swizzle is always a wrong texel permutation here "
                    "(the mosaic/streak class). For a matching fetch the sample "
                    "coordinate is replaced unconditionally with the identity host-texel "
                    "UV (SV_Position.xy / (guest_size * scale)) -- a 1:1 copy. A bare "
                    "hash overrides ALL of that shader's fetches; \":4\" limits it to "
                    "Xenos tfetch slot 4 (needed when only some fetches de-swizzle, e.g. "
                    "the AC6 cloud compositor: scene fetch de-swizzles, mask/cloud "
                    "fetches use plain UVs and must be left alone). Runtime, no rebuild.");
REXCVAR_DEFINE_BOOL(ac6_flare_drop_quad2, true, "GPU/Shader",
                    "AC6: cull the sun lens-flare's spurious second billboard "
                    "(vertices 4-7) to remove the faint rectangle in the sky");
REXCVAR_DEFINE_INT32(ac6_flare_drop_index_min, 4, "GPU/Shader",
                     "AC6: first vertex index of the flare draw to cull (the "
                     "spurious billboard is vertices 4-7 of 8)");
REXCVAR_DEFINE_BOOL(gpu_debug_markers, false, "GPU",
                    "Insert debug markers into GPU command streams for tools "
                    "like PIX and RenderDoc. Automatically enabled when "
                    "RenderDoc is detected.");
REXCVAR_DEFINE_BOOL(ac6_fix_trails, true, "AC6",
                    "Fix invisible missile/jet trails: drop stale cached vertex-buffer "
                    "residency so the GPU copy of AC6's fixed-address trail history ring "
                    "refreshes when the CPU writes it. On by default.");

bool IsGpuDebugMarkersEnabled() {
  static bool cached = false;
  static bool result = false;
  if (!cached) {
    cached = true;
    if (REXCVAR_GET(gpu_debug_markers)) {
      result = true;
      REXLOG_INFO("GPU debug markers enabled via CVar");
    } else {
      auto renderdoc_api = rex::ui::RenderDocAPI::CreateIfConnected();
      if (renderdoc_api) {
        result = true;
        REXLOG_INFO("GPU debug markers auto-enabled (RenderDoc detected)");
      }
    }
  }
  return result;
}
