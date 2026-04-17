# AC6 Native Graphics Migration Strategy

Date: 2026-04-17
Scope: `AC6 recomp`, `Windows + Linux + macOS`
Target: replace Rexglue's emulation-authoritative graphics stack with an AC6-native renderer while keeping production builds releasable at every phase.

Companion planning documents:

- `AC6_NATIVE_GRAPHICS_EMULATION_AUDIT_2026-04-17.md` catalogs the current emulation-owned stack and replacement targets.
- `AC6_NATIVE_GRAPHICS_API_DEPRECATION_TIMELINE.md` defines the deprecation and removal schedule for legacy graphics interfaces.
- `AC6_NATIVE_GRAPHICS_EXECUTION_PLAN_2026-04-17.md` translates this strategy into workstreams, owner roles, dependencies, artifact packages, and weekly checkpoints.

## Success Criteria

The migration is considered successful only when all of the following are true in production:

- emulation-authoritative rendering code is removed from the AC6 build
- issue tracker shows `100 percent` closure of bugs tagged `graphics-emulation`
- CPU overhead attributable to graphics drops by `20 percent` or more relative to the current baseline
- renderer-owned memory footprint drops by `30 percent` or more relative to the current baseline
- median frame time stays at or below `16.6 ms` on the 30 FPS production profile
- average GPU utilization stays below `80 percent` on the validated target matrix
- no critical or major severity regressions are opened within `90 days` of production release

These are release gates, not assumptions. Baselines must be captured before Phase 1 ends.

## Native Replacement Stack

### Rendering API strategy

Canonical engine interfaces:

- `D3D12` backend for Windows
- `Vulkan` backend for Linux
- `Metal` backend for macOS

Renderer architecture:

- API-agnostic `RenderDevice` / `RenderGraph` / `FrameScheduler` core
- backend-specific device modules behind a thin, explicit portability layer
- no translation layer such as ANGLE, MoltenVK-as-primary, or emulated guest GPU state

Decision:

- author the renderer around explicit resource states, pipeline objects, descriptor sets/heaps, and timeline-style frame scheduling
- do not preserve Xenos concepts in the public rendering API

### Shader stack

Canonical source:

- `HLSL` as the authored shading language for all AC6-native shaders

Offline outputs:

- `DXIL` for D3D12
- `SPIR-V` for Vulkan
- `MSL` for Metal

Toolchain:

- `DXC` for HLSL to DXIL / SPIR-V
- `SPIRV-Cross` or equivalent validated codegen for MSL
- offline reflection manifest generation for resource layouts, permutations, and pipeline compatibility

Rules:

- no runtime Xenos microcode translation
- no guest-shader-derived pipeline creation in shipping mode
- shader permutations are compiled and cached before runtime, with pipeline libraries per backend

### Buffer management

Static content:

- cooked immutable GPU buffers for meshes, index data, and prebuilt acceleration tables if needed later

Dynamic content:

- per-frame transient arena for constants, skinning, UI, and streaming draws
- persistently mapped upload ring for small dynamic updates
- segmented upload jobs for large texture or geometry streaming

Lifetime model:

- frame-indexed retirement with fence-backed reclamation
- descriptor lifetime tied to resource generations rather than guest addresses

### Texture handling

Cooked asset formats:

- `KTX2` with `BasisU` or backend-native compressed output for static textures where feasible
- backend-native HDR / depth-capable formats for runtime render targets
- explicit metadata for color space, sampler class, mips, and streaming priority

Runtime behavior:

- no hot-path Xbox 360 untiling for shipping builds
- no guest texture cache for native-owned resources
- explicit residency manager for large terrains, cockpits, and effects textures

### Asset loading

Offline asset pipeline:

- extract AC6 meshes, materials, textures, shaders, and effect descriptors from recompiled or game asset sources
- convert into native packages with stable IDs and versioned schemas
- bake material graphs and shader permutation keys at cook time

Runtime asset loader:

- async streaming on background threads
- integrity-checked package manifests
- deterministic scene bootstrap manifests for benchmark and pixel tests

### Synchronization primitives

Core abstraction:

- timeline-style `RenderFence`
- `BinarySemaphore` only where backend interop requires it
- explicit resource barriers and pass dependencies in the render graph

Backend mapping:

- D3D12 fences + queue synchronization
- Vulkan timeline semaphores + pipeline barriers
- Metal shared events / command-buffer completion handlers

Requirement:

- frame submission and async compute ownership must remain host-native and explicit

## Target Renderer Architecture

### Core modules

1. `Ac6RenderFrontend`
   - receives patched calls from recompiled AC6 code
   - builds scene, pass, and material requests

2. `Ac6AssetCooker`
   - converts source assets to native packages
   - emits stable schemas and version manifests

3. `RenderDevice`
   - owns backend objects, queues, descriptor allocators, and memory allocators

4. `RenderGraph`
   - describes passes, attachments, synchronization, and transient resources

5. `FrameScheduler`
   - controls frame pacing, in-flight frames, async uploads, and capture points

6. `MaterialSystem`
   - resolves shader permutations, resource layouts, pipeline variants, and material constants

7. `SceneStreaming`
   - loads meshes, textures, and effect resources asynchronously

8. `PostFXStack`
   - tonemap, bloom, HUD composite, anti-aliasing, and display transforms

9. `ValidationHarness`
   - pixel diff, perf telemetry, memory tracking, deterministic captures, and replayable scene manifests

### Game integration model

The title code is allowed to change, so the correct strategy is direct call-site replacement rather than deeper emulation.

Phase-in model:

- patch AC6 rendering entry points to call `Ac6RenderFrontend`
- preserve the current hook-based frame capture only long enough to compare outputs
- migrate one subsystem at a time:
  - frame setup
  - camera / transforms
  - terrain / environment
  - aircraft
  - particles / trails
  - UI / HUD
  - post-processing

Hard rule:

- no new rendering features may be added to the PM4/Xenos path once migration starts

## Phased Rollout

### Phase 0: Baseline and ownership

Deliverables:

- freeze current performance baselines on target hardware
- tag and export all issue-tracker items caused by graphics emulation
- lock the AC6 render-surface contract and benchmark scenes
- complete the emulation audit

Exit gates:

- baseline frame time, CPU cost, GPU utilization, and memory footprint recorded
- current golden images captured from the shipping emulator-authoritative path

### Phase 1: Native scaffolding in parallel

Deliverables:

- add `Ac6RenderFrontend`, `RenderDevice`, `RenderGraph`, and backend stubs
- keep current renderer shipping
- add dual-run instrumentation: native scene build runs but does not present

Exit gates:

- AC6 boots with native stack initialized on all three PC platforms
- no user-visible behavior change
- telemetry compares native scene graph generation cost against emulated path

Rollback:

- disable with single runtime feature flag and compile-time option

### Phase 2: Native asset and shader pipeline

Deliverables:

- offline shader compilation pipeline
- asset cooker for textures, materials, meshes, and effect metadata
- package manifests and versioning

Exit gates:

- benchmark scenes can render placeholder-correct geometry from native assets
- no runtime shader translation remains on the native path

Rollback:

- keep cooked assets optional while legacy assets remain authoritative

### Phase 3: Hybrid native rendering by subsystem

Deliverables:

- terrain / sky / cockpit / aircraft / particle / HUD passes migrate incrementally
- native renderer writes final composite to existing presenter
- dual-render compare mode for selected scenes

Exit gates per subsystem:

- golden image threshold passes for benchmark scenes
- frame time does not regress more than agreed tolerance during canary
- issue count trends downward against emulation-tagged bug bucket

Zero-downtime mechanism:

- runtime routing flag per subsystem
- canary builds support mixed mode:
  - native terrain with legacy particles
  - native post-FX with legacy geometry
  - native UI with legacy world render

Rollback:

- turn off only the failing subsystem, not the whole renderer

### Phase 4: Native presentation authority

Deliverables:

- `VdSwap` becomes native frame boundary only
- PM4 `XE_SWAP` path removed from AC6 shipping route
- presenter consumes only native final-color output and UI overlays

Exit gates:

- no frame in production depends on emulated swap-source extraction
- native frame pacing and present timing match or beat baseline

Rollback:

- release branch keeps previous mixed-mode build for one milestone

### Phase 5: Delete emulation rendering path from AC6 build

Deliverables:

- remove `CommandProcessor`, shader translators, shared memory, texture cache, and render-target cache from AC6 linkage
- keep only minimal compatibility exports required by non-render kernel behavior

Exit gates:

- build proves no AC6 rendering object files are linked from the emulation stack
- perf, memory, and parity goals pass full regression test matrix

Rollback:

- tagged fallback branch only; no hidden production runtime switch after signoff

### Phase 6: Production hardening

Deliverables:

- security signoff
- fuzzing signoff
- 90-day production watch plan
- deprecation closure of legacy graphics configuration and docs

Exit gates:

- zero critical or major regressions within 90 days
- emulation bug label stays at zero open issues

## Validation and Test Strategy

### Pixel validation

Primary method:

- golden image diff on deterministic scenes

Modes:

- exact match for UI and deterministic post-FX scenes
- tolerance-based diff for scenes with backend precision drift
- review workflow for approved intentional visual changes

Required coverage:

- menus and HUD
- cockpit
- missile trails and smoke
- clouds and sky
- water / terrain
- lighting-heavy missions
- mission briefing / loading overlays

### Performance benchmarks

Metrics:

- median, P95, and worst-frame CPU frame time
- GPU frame time
- GPU utilization
- draw / dispatch counts
- upload bandwidth
- shader compilation stalls

Release gates:

- `<= 16.6 ms` frame time on 30 FPS profile
- `< 80 percent` average GPU utilization on target matrix
- `>= 20 percent` lower graphics CPU overhead than baseline

### Memory and leak detection

Checks:

- renderer heap watermark
- transient arena peaks
- descriptor exhaustion
- texture residency churn
- shutdown leak scan

Tools by platform:

- D3D12 debug layer and DRED
- Vulkan validation and memory alloc telemetry
- Metal API validation and Xcode memory capture
- ASan / LSan capable builds where available

Release gates:

- `>= 30 percent` lower renderer memory footprint than baseline
- zero unbounded growth in 4-hour soak tests

### Cross-platform matrix

Platforms:

- Windows: D3D12 primary
- Linux: Vulkan primary
- macOS: Metal primary

Required test classes:

- smoke boot
- benchmark scenes
- full-mission traversal
- alt-tab / resize / suspend-resume
- shader cache cold / warm runs

### Automated regression suite

Required CI jobs:

- shader compile validation
- asset cooker schema validation
- scene-manifest render tests
- golden image diff
- performance benchmark trend check
- memory leak / sanitizer runs
- fuzz suite for malformed assets and package manifests

## Security Review Requirements

Mandatory review areas:

- native code injection points used to patch recompiled AC6 render call sites
- asset package parsing
- shader package loading
- descriptor / buffer bounds validation
- file-format conversion tools
- telemetry and capture ingestion

Controls:

- signed or checksummed native asset packages
- bounds-checked parsers
- hardened debug-only injection interfaces
- explicit separation between shipping and dev-only capture hooks

Fuzzing scope:

- malformed textures
- malformed mesh buffers
- corrupted package manifests
- invalid shader permutation metadata
- oversized or cyclic material graphs

## Documentation Package Requirements

The migration is not complete until the following ship:

- renderer architecture guide
- backend bring-up guide for D3D12 / Vulkan / Metal
- asset cooker schema reference
- shader authoring and permutation guide
- API change log for all removed Rexglue-era render interfaces
- user migration guide for config and build switches
- deprecation schedule for legacy render flags and code paths

## Rollback and Zero-Downtime Policy

Rules:

- production builds stay releasable after every phase
- every phase has a feature flag rollback
- subsystem-level rollback is preferred over whole-renderer rollback
- no destructive removal happens until the native path has passed parity and perf gates for at least one milestone

Cutover policy:

- internal canary
- opt-in external experimental
- default-on stable
- legacy branch retained for one release train only

## Governance

Program owner:

- AC6 graphics migration lead

Required leads:

- backend lead
- assets / tools lead
- engine integration lead
- QA / validation lead
- security lead

Weekly review metrics:

- open emulation bugs remaining
- migrated subsystems count
- benchmark trend deltas
- memory trend deltas
- parity diff pass rate
- crash-free hours in soak runs

## Recommended Implementation Order

1. native renderer core and telemetry
2. asset cooker and shader pipeline
3. native post-FX and final composite
4. terrain / sky / environment
5. aircraft and world objects
6. particles and effects
7. HUD / menus / overlays
8. swap ownership and presenter handoff
9. deletion of PM4 / Xenos render path from AC6 target

## Immediate Next Actions

1. create `ac6_native_renderer/` module tree and backend interfaces
2. capture benchmark baselines on the agreed hardware matrix
3. inventory issue-tracker bugs tagged `graphics-emulation`
4. define the first three benchmark scene manifests
5. patch one AC6 render entry point into a no-op native frontend path
