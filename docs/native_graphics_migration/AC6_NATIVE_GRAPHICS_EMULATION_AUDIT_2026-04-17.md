# AC6 Native Graphics Emulation Audit

Date: 2026-04-17
Scope: `AC6 only`, `PC only` (`Windows`, `Linux`, `macOS`)
Goal: catalog every current graphics component that exists because the title still runs through a Xenia-derived Xenos / PM4 / guest-D3D emulation model.

Companion planning documents:

- `AC6_NATIVE_GRAPHICS_MIGRATION_STRATEGY_2026-04-17.md` defines the target architecture and release gates.
- `AC6_NATIVE_GRAPHICS_API_DEPRECATION_TIMELINE.md` defines when legacy interfaces become dev-only, blocked, or removed.
- `AC6_NATIVE_GRAPHICS_EXECUTION_PLAN_2026-04-17.md` maps the audit items in this file to workstreams, owner roles, dependencies, and phase exit artifacts.

## Executive Summary

The active AC6 graphics stack is still emulator-authoritative.

Current live path:

`guest D3D call -> AC6 hook/shadow capture -> guest-side D3D implementation -> VdSwap -> GraphicsSwapSubmission -> optional AC6 interception -> PM4_XE_SWAP fallback -> CommandProcessor -> D3D12/Vulkan backend -> Presenter -> host swapchain`

What is already native enough to keep for the migration:

- window creation
- presenter / swapchain ownership
- host graphics provider abstraction
- overlays / immediate UI
- runtime injection seam via `RuntimeConfig.graphics`

What must be retired to eliminate graphics emulation:

- PM4 packet submission for rendering
- Xenos register/MMIO ownership of frame construction
- command processor driven draw/resolve execution
- shader microcode translation on the hot path
- guest GPU shared-memory resource interpretation on the hot path
- EDRAM emulation / resolve heuristics / render-target ownership emulation
- swap extraction from emulated frontbuffer state

## Current Authoritative Render Ownership

### App / title-local layer

Files:

- `src/ac6recomp_app.h`
- `src/ac6_native_graphics.cpp`
- `src/ac6_native_graphics_overlay.cpp`
- `src/d3d_hooks.cpp`
- `src/d3d_state.h`
- `src/render_hooks.cpp`

Current role:

- bootstraps an alternate graphics system
- captures guest D3D state and frame summaries
- performs swap interception experiments
- shows diagnostic overlay state
- does not build the real frame yet

Migration disposition:

- keep as temporary bridge
- evolve into title-native capture, validation, and compatibility control plane
- eventually split into:
  - native renderer frontend
  - compatibility shim layer
  - debug / validation tools

### Runtime injection seam

Files:

- `thirdparty/rexglue-sdk/src/native/ui/rex_app.cpp`
- `thirdparty/rexglue-sdk/src/system/runtime.cpp`
- `thirdparty/rexglue-sdk/include/rex/system/interfaces/graphics.h`

Current role:

- chooses default graphics backend
- boots injected graphics implementation
- exposes `GraphicsSwapSubmission`

Migration disposition:

- keep
- expand interface surface for native renderer lifecycle, telemetry, capture, and validation

## Emulation Dependency Inventory

### 1. Guest submission and swap synthesis

Files:

- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp`

Emulation behavior:

- `VdInitializeRingBuffer` and `VdEnableRingBufferRPtrWriteBack` preserve guest command-buffer semantics
- `VdSwap` interprets guest texture fetch state and can still synthesize `PM4_XE_SWAP`
- guest-visible interrupt semantics depend on this path

Why it must go:

- frame submission is still modeled as Xbox 360 GPU behavior rather than native frame production
- the native renderer cannot fully own frame pacing, presentation, or resource lifetime while `VdSwap` remains a PM4 bridge

Replacement target:

- AC6-native frame boundary API
- thin `xboxkrnl_video` compatibility shim that forwards title state into native frame submission
- retained interrupt timing contract only where the recompiled title still observes it

Retirement priority: `P0`

### 2. Graphics system and MMIO ownership

Files:

- `thirdparty/rexglue-sdk/include/rex/graphics/graphics_system.h`
- `thirdparty/rexglue-sdk/src/graphics/graphics_system.cpp`

Emulation behavior:

- maps GPU MMIO
- owns vblank worker
- owns command processor lifecycle
- owns register file / guest-facing GPU register semantics

Why it must go:

- the graphics core is still described as an emulated device
- render lifetime is still coupled to MMIO and command ring state

Replacement target:

- `Ac6NativeRenderSystem`
- explicit frame scheduler
- host-owned timing, telemetry, resource device, and native presentation coordinator

Retirement priority: `P0`

### 3. PM4 command stream parsing and command processor

Files:

- `thirdparty/rexglue-sdk/src/graphics/command_processor.cpp`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp`
- `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp`
- `thirdparty/rexglue-sdk/include/rex/graphics/command_processor.h`

Emulation behavior:

- parses PM4 packet stream
- executes draw/clear/resolve through emulated Xenos semantics
- handles swap packets
- owns emulated GPU caches and synchronization conventions

Why it must go:

- it is the central emulation layer
- any renderer built on top of it still pays the cost and correctness risks of emulation

Replacement target:

- title-native render graph compiler
- pass scheduler fed directly by recompiled AC6 draw APIs and extracted asset/material metadata

Retirement priority: `P0`

### 4. Xenos register, packet, and hardware model

Files:

- `thirdparty/rexglue-sdk/src/graphics/registers.cpp`
- `thirdparty/rexglue-sdk/src/graphics/register_file.cpp`
- `thirdparty/rexglue-sdk/src/graphics/xenos.cpp`
- `thirdparty/rexglue-sdk/src/graphics/packet_disassembler.cpp`
- headers under `thirdparty/rexglue-sdk/include/rex/graphics/*`

Emulation behavior:

- maintains Xbox 360 GPU data model
- drives command interpretation, shader decoding, and state binding

Why it must go:

- the native renderer should consume AC6-native pass/material/resource data, not Xenos state

Replacement target:

- native render-state descriptors
- native material schema
- compiled scene/pass metadata

Retirement priority: `P1`

### 5. Shared guest GPU memory and aliasing model

Files:

- `thirdparty/rexglue-sdk/src/graphics/shared_memory.cpp`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/shared_memory.cpp`
- `thirdparty/rexglue-sdk/src/graphics/vulkan/shared_memory.cpp`

Emulation behavior:

- interprets guest GPU memory as if the host were managing Xbox 360 resource backing
- aliases memory through guest physical/virtual address translation

Why it must go:

- this is the main CPU overhead source for dynamic resource interpretation
- it preserves emulator-style lifetime and coherency problems

Replacement target:

- host-native resource allocator
- upload / streaming heap manager
- explicit transient arena
- immutable asset pool for converted textures and geometry

Retirement priority: `P1`

### 6. Texture cache, tiling, format conversion, and invalidation

Files:

- `thirdparty/rexglue-sdk/src/graphics/pipeline/texture/cache.cpp`
- `thirdparty/rexglue-sdk/src/graphics/pipeline/texture/conversion.cpp`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp`
- `thirdparty/rexglue-sdk/src/graphics/vulkan/texture_cache.cpp`

Emulation behavior:

- decodes Xbox 360 texture layout and formats on demand
- performs untiling / conversion / invalidation logic
- reconstructs swap textures from emulated state

Why it must go:

- texture correctness bugs are still tied to emulated decode and filtering behavior
- texture conversion belongs in offline cooking, not hot-path per-frame interpretation

Replacement target:

- offline asset cooker
- host-native texture packages
- runtime streaming for only genuinely dynamic textures

Retirement priority: `P1`

### 7. Render-target cache, EDRAM emulation, and resolve ownership

Files:

- `thirdparty/rexglue-sdk/src/graphics/pipeline/render_target/cache.cpp`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/render_target_cache.cpp`
- `thirdparty/rexglue-sdk/src/graphics/vulkan/render_target_cache.cpp`

Emulation behavior:

- reconstructs EDRAM-backed render-target ownership
- manages resolve and aliasing behaviors to mimic Xbox 360 output rules

Why it must go:

- effect/composite bugs are still heavily correlated with this layer
- native render targets should be explicit passes, not inferred ownership transfers

Replacement target:

- native render graph attachments
- explicit post-process history buffers
- explicit effect composition passes

Retirement priority: `P1`

### 8. Shader microcode interpretation and translation

Files:

- `thirdparty/rexglue-sdk/src/graphics/pipeline/shader/interpreter.cpp`
- `thirdparty/rexglue-sdk/src/graphics/pipeline/shader/translator.cpp`
- `thirdparty/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator*.cpp`
- `thirdparty/rexglue-sdk/src/graphics/pipeline/shader/spirv_translator*.cpp`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp`
- `thirdparty/rexglue-sdk/src/graphics/vulkan/pipeline_cache.cpp`

Emulation behavior:

- translates Xenos microcode into host shader forms at runtime
- caches host pipelines based on guest shader identity and emulated state

Why it must go:

- runtime translation is a major CPU cost and validation risk
- native renderer should use author-owned shaders with offline compilation

Replacement target:

- canonical HLSL source set
- offline compilation to DXIL, SPIR-V, and MSL
- stable pipeline-library generation and shader reflection at build time

Retirement priority: `P1`

### 9. Emulation diagnostics and traces

Files:

- `thirdparty/rexglue-sdk/src/graphics/trace_writer.cpp`
- trace headers / trace dump / trace player / trace viewer
- PM4 logging paths

Emulation behavior:

- introspects guest GPU traffic and emulated resources

Why it must change:

- title-native diagnostics still matter, but they should observe native frame graph and assets, not PM4/Xenos internals

Replacement target:

- native render capture
- pass timeline dumps
- asset/material resolution logs
- backend-specific GPU markers and RenderDoc/Xcode capture guides

Retirement priority: `P2`

### 10. Presenter / provider / window system

Files:

- `thirdparty/rexglue-sdk/src/native/ui/presenter.cpp`
- `thirdparty/rexglue-sdk/src/native/ui/d3d12/d3d12_presenter.cpp`
- provider / window / overlay code in `thirdparty/rexglue-sdk/src/native/ui/*`

Current state:

- already host-native enough
- not an emulation layer by itself
- currently consumes an emulation-produced guest output image

Migration disposition:

- keep, but retarget to native renderer outputs
- extend for backend-agnostic swapchain and frame pacing metrics

Retirement priority: `keep and adapt`

## Build-System Evidence

The current build still compiles the emulator-era graphics stack:

- project root `CMakeLists.txt` still relies on `generated/rexglue.cmake`
- `thirdparty/rexglue-sdk/src/graphics/CMakeLists.txt` still includes:
  - `graphics_system.cpp`
  - `command_processor.cpp`
  - `shared_memory.cpp`
  - `pipeline/texture/cache.cpp`
  - `pipeline/texture/conversion.cpp`
  - `pipeline/render_target/cache.cpp`
  - `pipeline/shader/*translator*`
  - `d3d12/*`
  - `vulkan/*`

Conclusion:

- the emulation stack is not auxiliary
- it is still linked as the shipping graphics core

## Components To Keep Versus Replace

### Keep and extend

- `RuntimeConfig.graphics` injection seam
- native presenter / provider / window abstractions
- AC6-local capture hooks during transition
- debug overlay plumbing
- title timing stats

### Replace completely

- PM4 swap path
- ringbuffer submission path
- `GraphicsSystem` as renderer owner
- `CommandProcessor`
- register file and Xenos state ownership for rendering
- shared memory hot-path resource model
- texture cache / untiling on the hot path
- EDRAM emulation
- runtime shader translation
- backend-specific emulated renderers

### Narrow into compatibility shims

- `xboxkrnl_video` exports
- guest interrupt callback semantics
- limited AC6 guest-D3D bridge while call sites are redirected

## Retirement Order

1. Introduce native renderer side by side with current presenter/provider.
2. Make `VdSwap` a native frame-boundary call, not a PM4 authoring point.
3. Redirect AC6 guest-D3D hot paths into native command recording.
4. Replace swap-source extraction with native final-composite ownership.
5. Replace shader translation with offline-native shaders.
6. Replace texture cache and guest resource ownership with cooked native assets.
7. Remove EDRAM / render-target emulation dependencies.
8. Delete PM4 / command-processor rendering path from AC6 build.

## Audit Exit Criteria

This audit is complete enough to drive implementation when the following are accepted:

- every component above is assigned an owner and milestone
- each `P0` / `P1` item has a native replacement module defined
- validation targets are attached to each retirement step
- issue-tracker bugs are mapped to migration epics rather than ad hoc fixes

## Non-Goals

- iOS and Android support
- keeping Xenos semantics as a hidden fallback indefinitely
- pixel-perfect guarantees for every build during bring-up; parity gates apply before production
