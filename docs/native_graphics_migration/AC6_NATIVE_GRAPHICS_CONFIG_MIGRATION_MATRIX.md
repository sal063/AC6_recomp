# AC6 Native Graphics Config Migration Matrix

Date: 2026-04-17  
Scope: `AC6 recomp` graphics configuration migration  
Status: initial actionable matrix (to be extended as new graphics keys are discovered)

Related planning docs:

- `AC6_NATIVE_GRAPHICS_MIGRATION_STRATEGY_2026-04-17.md`
- `AC6_NATIVE_GRAPHICS_API_DEPRECATION_TIMELINE.md`
- `AC6_NATIVE_GRAPHICS_EXECUTION_PLAN_2026-04-17.md`

## Purpose

This document converts deprecation policy into concrete key-by-key actions. It defines:

- what legacy graphics keys do today
- whether each key is kept, renamed, made dev-only, or removed
- when enforcement happens (`M2` through `M6`)
- what migration behavior and CI checks are required

## Enforcement Legend

- `keep`: remains valid in production native renderer
- `rename`: replaced by native key; legacy alias can exist temporarily
- `dev-only`: blocked in production builds; permitted in developer builds
- `remove`: deleted from AC6 production path after milestone gate

## Key Migration Matrix

| Legacy key | Current meaning | Native replacement / behavior | Action | Enforcement milestone | User migration note | CI / linter rule |
| --- | --- | --- | --- | --- | --- | --- |
| `vsync` | controls vertical sync pacing in emulation-era GPU path | `native.present.vsync` | rename | M4 | migrate to native present setting; same semantic intent | error on legacy key in production config after M4 |
| `guest_vblank_sync_to_refresh` | keeps guest vblank cadence tied to guest refresh | `compat.timing.guest_vblank_sync_to_refresh` | dev-only | M2 | keep only for timing investigations | fail production build if enabled |
| `swap_post_effect` | applies post effect at emulated swap stage (`none`, `fxaa`, `fxaa_extreme`) | `native.postfx.swap_effect` | rename | M4 | same options initially, later unified into post-FX stack config | warn on legacy key in M3, error in M4 |
| `resolution` | emulation-era output resolution selector | `native.output.resolution` | rename | M4 | migrate value directly | auto-fix mapping in config migration tool |
| `resolution_scale` | global emulation resolution scale | `native.render.scale` | rename | M3 | use native scale with subsystem routing support | error if both keys present |
| `draw_resolution_scale_x` | emulation draw-scale X | `native.render.scale_x` | rename | M3 | migrate only if non-default | warning if legacy key present in M3+ |
| `draw_resolution_scale_y` | emulation draw-scale Y | `native.render.scale_y` | rename | M3 | migrate only if non-default | warning if legacy key present in M3+ |
| `draw_resolution_scaled_texture_offsets` | emulation compensation for scaled texture offsets | native render graph handles coordinate transforms explicitly | remove | M5 | no user replacement; behavior internalized | error if key present after M5 |
| `resolve_resolution_scale_fill_half_pixel_offset` | half-pixel handling during emulated resolve scale fill | handled by native resolve pass shaders | remove | M5 | no replacement needed | error on key after M5 |
| `readback_resolve` | CPU readback mode for render-to-texture resolve | `compat.readback.resolve_mode` (dev-only) | dev-only | M2 | use only during parity/debug bring-up | fail production build if not default |
| `readback_resolve_half_pixel_offset` | resolve readback sampling offset tweak | `compat.readback.resolve_half_pixel_offset` (dev-only) | dev-only | M2 | debug-only parity aid | fail production build if enabled |
| `readback_memexport` | CPU readback for shader memexport coherency | `compat.readback.memexport` (dev-only) | dev-only | M2 | needed only while legacy bridge remains | fail production build if enabled |
| `readback_memexport_fast` | fast memexport readback mode | `compat.readback.memexport_fast` (dev-only) | dev-only | M2 | debug/perf experiment only | fail production build if enabled |
| `vulkan_readback_resolve` | Vulkan legacy alias for readback resolve | none (legacy alias removed) | remove | M3 | migrate to shared compat key if needed in dev builds | reject key always in production |
| `vulkan_readback_memexport` | Vulkan legacy alias for memexport readback | none (legacy alias removed) | remove | M3 | migrate to shared compat key if needed in dev builds | reject key always in production |
| `d3d12_readback_resolve` | D3D12 legacy alias for readback resolve | none (legacy alias removed) | remove | M3 | migrate to shared compat key if needed in dev builds | reject key always in production |
| `d3d12_readback_memexport` | D3D12 legacy alias for memexport readback | none (legacy alias removed) | remove | M3 | migrate to shared compat key if needed in dev builds | reject key always in production |
| `async_shader_compilation` | async runtime shader/pipeline compilation in emulation path | `native.shader.runtime_async_compile` (dev-only fallback) | dev-only | M2 | native path uses offline compiled shaders; runtime path debug-only | fail production config if runtime compile enabled |
| `dump_shaders` | dumps runtime translated shaders | `native.debug.dump_shaders` (dev-only) | dev-only | M2 | for migration diagnostics only | block in production config |
| `dxbc_switch` | legacy DXBC translator switch | none | remove | M5 | no replacement; offline shader pipeline supersedes | error on key after M5 |
| `dxbc_source_map` | legacy translator source map output | none | remove | M5 | no replacement; use offline reflection artifacts | error on key after M5 |
| `vfetch_index_rounding_bias` | Xenos translator behavior knob | none | remove | M5 | no replacement on native path | error on key after M5 |
| `texture_cache_memory_limit_render_to_texture` | emulated texture cache memory cap for RTT | `native.streaming.texture_budget_mb` | rename | M4 | migrate to native residency budget | warning when legacy key used |
| `texture_cache_memory_limit_soft` | soft emulated texture cache limit | `native.streaming.texture_budget_soft_mb` | rename | M4 | migrate value with documented unit conversion | linter auto-fix if units are valid |
| `texture_cache_memory_limit_hard` | hard emulated texture cache limit | `native.streaming.texture_budget_hard_mb` | rename | M4 | migrate value with documented unit conversion | linter auto-fix if units are valid |
| `texture_cache_memory_limit_soft_lifetime` | cache residency lifetime heuristic | native streamer residency policy tables | remove | M5 | no user replacement | error on key after M5 |
| `gpu_3d_to_2d_texture` | emulated texture workaround | `compat.texture.legacy_3d_to_2d` | dev-only | M3 | only for parity triage | fail production if enabled |
| `gpu_allow_invalid_fetch_constants` | tolerate invalid fetch constants | `compat.validation.allow_invalid_fetch_constants` | dev-only | M2 | debug-only escape hatch | fail production if true |
| `non_seamless_cube_map` | compatibility toggle for cube map sampling | `native.sampling.non_seamless_cube_map` | keep | M4 | kept as hardware compatibility option | ensure default distribution value documented |
| `anisotropic_override` | forced anisotropic filtering level | `native.quality.anisotropy_override` | rename | M4 | migrate directly; same value range | validate range in config linter |
| `occlusion_query_enable` | enable host occlusion query handling | `native.query.occlusion.enable` | rename | M4 | migrate directly | error if both old and new keys set |
| `query_occlusion_fake_sample_count` | fake sample count for emulated queries | `compat.query.fake_sample_count` | dev-only | M3 | debugging only; not for production | fail production if non-default |
| `depth_float24_round` | depth precision workaround | `compat.depth.float24_round` | dev-only | M3 | only use for parity troubleshooting | fail production if true |
| `depth_float24_convert_in_pixel_shader` | depth conversion workaround | `compat.depth.float24_ps_convert` | dev-only | M3 | debug-only parity aid | fail production if true |
| `depth_transfer_not_equal_test` | depth transfer compare workaround | `compat.depth.transfer_not_equal_test` | dev-only | M3 | debug-only parity aid | fail production if non-default |
| `native_stencil_value_output` | stencil output behavior toggle | `native.depth_stencil.stencil_output` | keep | M4 | stays as backend compatibility toggle | validate supported backend combinations |
| `native_stencil_value_output_d3d12_intel` | Intel-specific stencil behavior toggle | `native.depth_stencil.d3d12_intel_stencil_output` | keep | M4 | keep platform-specific escape hatch | limit to Windows+D3D12 in linter |
| `gamma_render_target_as_unorm16` | gamma RT format behavior | `native.render_target.gamma_unorm16` | keep | M4 | retained as quality/compat setting | ensure default value parity-tested |
| `native_2x_msaa` | native 2x MSAA toggle | `native.quality.msaa_2x` | rename | M4 | migrate directly | warn if legacy key used after M4 |
| `snorm16_render_target_full_range` | emulated RT format behavior toggle | `compat.render_target.snorm16_full_range` | dev-only | M3 | parity bring-up only | fail production if enabled |
| `mrt_edram_used_range_clamp_to_min` | EDRAM-era MRT behavior workaround | none | remove | M5 | no replacement in native render graph | error on key after M5 |
| `direct_host_resolve` | direct host resolve path toggle | none (native resolve passes are authoritative) | remove | M5 | no replacement | error on key after M5 |
| `execute_unclipped_draw_vs_on_cpu` | CPU fallback for draw processing | none | remove | M5 | no replacement on native path | error on key after M5 |
| `execute_unclipped_draw_vs_on_cpu_for_psi_render_backend` | backend-specific CPU fallback | none | remove | M5 | no replacement on native path | error on key after M5 |
| `execute_unclipped_draw_vs_on_cpu_with_scissor` | CPU fallback variant | none | remove | M5 | no replacement on native path | error on key after M5 |
| `force_convert_line_loops_to_strips` | primitive conversion workaround | `compat.primitive.line_loop_to_strip` | dev-only | M3 | debug-only compatibility fallback | fail production if enabled |
| `force_convert_quad_lists_to_triangle_lists` | primitive conversion workaround | `compat.primitive.quad_to_tri` | dev-only | M3 | debug-only compatibility fallback | fail production if enabled |
| `force_convert_triangle_fans_to_lists` | primitive conversion workaround | `compat.primitive.fan_to_list` | dev-only | M3 | debug-only compatibility fallback | fail production if enabled |
| `primitive_processor_cache_min_indices` | primitive cache threshold | `native.frontend.primitive_cache_min_indices` | rename | M4 | migrate directly after native frontend cutover | range-check in linter |
| `trace_gpu_prefix` | trace output prefix for GPU traces | `native.debug.trace_prefix` (dev-only) | dev-only | M2 | diagnostics only | fail production if non-empty |
| `trace_gpu_stream` | stream GPU trace continuously | `native.debug.trace_stream` (dev-only) | dev-only | M2 | diagnostics only | fail production if true |
| `gpu_debug_markers` | GPU markers for tools like PIX/RenderDoc | `native.debug.gpu_markers` | keep | M4 | supported for dev and optionally production troubleshooting | allow but default off in release config |
| `vulkan_sparse_shared_memory` | Vulkan shared-memory emulation mode | none | remove | M5 | no native replacement | error on key after M5 |
| `vulkan_submit_on_primary_buffer_end` | Vulkan emulation submit timing behavior | `native.vulkan.submit_policy` | rename | M4 | migrate to native queue submit policy | warn then error after M4 |
| `vulkan_dynamic_rendering` | Vulkan dynamic rendering toggle | `native.vulkan.dynamic_rendering` | keep | M4 | retained as backend capability switch | validate backend support |
| `vulkan_async_skip_incomplete_frames` | Vulkan frame skip behavior | `native.vulkan.allow_incomplete_frame_skip` | rename | M4 | retain as backend tuning option | validate only on Vulkan backend |
| `vulkan_pipeline_creation_threads` | Vulkan runtime pipeline thread count | `native.vulkan.pipeline_threads` | rename | M4 | used for native pipeline library management | range validation |
| `vulkan_tessellation_wireframe` | Vulkan tessellation wireframe debug mode | `native.debug.vulkan.tess_wireframe` | dev-only | M2 | debug-only mode | fail production if true |
| `vulkan_force_expand_point_sprites_in_vs` | Vulkan compatibility workaround | `compat.vulkan.expand_point_sprites_in_vs` | dev-only | M3 | parity fallback only | fail production if true |
| `vulkan_force_expand_rectangle_lists_in_vs` | Vulkan compatibility workaround | `compat.vulkan.expand_rect_lists_in_vs` | dev-only | M3 | parity fallback only | fail production if true |
| `vulkan_force_convert_quad_lists_to_triangle_lists` | Vulkan primitive conversion workaround | `compat.vulkan.quad_to_tri` | dev-only | M3 | parity fallback only | fail production if true |
| `render_target_path_vulkan` | Vulkan render target debug dump path | `native.debug.vulkan.render_target_path` (dev-only) | dev-only | M2 | debug output only | fail production if non-empty |
| `d3d12_bindless` | D3D12 bindless toggle | `native.d3d12.bindless` | keep | M4 | retained as backend feature flag | validate support on hardware tier |
| `d3d12_submit_on_primary_buffer_end` | D3D12 emulation submit timing behavior | `native.d3d12.submit_policy` | rename | M4 | migrate to native queue submit policy | warn then error after M4 |
| `d3d12_dxbc_disasm` | DXBC disassembly diagnostics | `native.debug.d3d12.dxbc_disasm` (dev-only) | dev-only | M2 | diagnostics only | fail production if true |
| `d3d12_dxbc_disasm_dxilconv` | DXIL conversion disassembly diagnostics | `native.debug.d3d12.dxbc_disasm_dxilconv` (dev-only) | dev-only | M2 | diagnostics only | fail production if true |
| `d3d12_pipeline_creation_threads` | D3D12 runtime pipeline thread count | `native.d3d12.pipeline_threads` | rename | M4 | migrate directly | range validation |
| `d3d12_tessellation_wireframe` | D3D12 tessellation wireframe debug mode | `native.debug.d3d12.tess_wireframe` | dev-only | M2 | diagnostics only | fail production if true |
| `d3d12_tiled_shared_memory` | D3D12 tiled shared-memory emulation toggle | none | remove | M5 | no replacement | error on key after M5 |
| `render_target_path_d3d12` | D3D12 render target debug dump path | `native.debug.d3d12.render_target_path` (dev-only) | dev-only | M2 | diagnostics only | fail production if non-empty |

## Mechanical Migration Rules

1. Apply all `rename` mappings first.
2. If both legacy and native key are present, native key wins and linter emits warning in `M2-M3`, error in `M4+`.
3. `dev-only` keys:
   - allowed in local and CI debug/dev profiles
   - blocked in production distribution artifacts immediately at the listed milestone
4. `remove` keys:
   - warning one milestone before removal
   - hard error at removal milestone and later

## Required Tooling

- Config migration script:
  - input: existing user/developer config
  - output: rewritten config with `rename` rules applied and deprecation report
- Config linter modes:
  - `warn` mode for pre-enforcement milestones
  - `enforce` mode for release pipelines
- CI checks:
  - fail if default shipping config contains `dev-only` or `remove` keys past enforcement milestone
  - fail if unknown graphics keys are present

## Open Items

- confirm final native key namespace before M3 freeze (`native.*` versus split subsystem namespaces)
- define unit conversion policy for memory-budget key migrations where old/new units differ
- attach examples for common user migration paths (`performance`, `debug`, `capture`) in README updates
