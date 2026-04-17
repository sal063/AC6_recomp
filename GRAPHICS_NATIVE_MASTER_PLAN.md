# AC6 Graphics Native Master Plan

Date: 2026-04-14
Repo: `C:\AC6Recomp_ext`

## Objective

Replace the current emulator-era graphics stack with an AC6-specific native graphics stack so the host OS and host GPU own frame construction, resource lifetime, presentation, and asset loading like a native PC port.

This plan is not "make the placeholder renderer look better."

This is a migration from:

- guest D3D calls feeding Xenos-style command emulation
- PM4/ringbuffer/MMIO frame submission
- shared-memory resource interpretation on the hot path
- runtime shader translation on the hot path
- EDRAM/resolve ownership emulation

to:

- native frame graph execution
- native D3D12 resource ownership
- native asset loading and override layers
- native shader binaries and render pipelines
- native present path

The immediate bug targets remain:

- invisible missile trails
- pixelated clouds
- pixelated explosions
- other effect/composite mismatches caused by the emulated render path

The broader end-state target is:

- a host-native graphics subsystem that behaves like a native port
- assets visible to the host filesystem and host tooling
- mod-friendly model and texture replacement workflows

## Important Scope Boundary

The title can become native in graphics and asset ownership without instantly deleting every runtime shim in one step.

In practice:

- graphics, presentation, and asset/resource ownership can become native
- title services can be narrowed to thin AC6-specific ABI shims
- PPC/runtime/kernel glue can be reduced aggressively
- some ABI compatibility surface may still exist during transition

If the goal is "the OS owns the game like a native port," the key requirement is that graphics assets and rendering stop depending on guest GPU memory and Xenos command semantics on the hot path.

That is the priority.

## Current Live Renderer State

### 1. The current renderer is still emulator-authoritative

The live path is still:

`guest D3D -> AC6 hook capture/shadow -> original guest D3D execution -> VdSwap -> HandleVideoSwap -> PM4_XE_SWAP fallback -> command processor -> D3D12 IssueSwap -> Presenter`

Key code:

- `src/ac6recomp_app.h:30`
- `src/ac6_native_graphics.cpp:654`
- `src/ac6_native_graphics.cpp:733`
- `src/ac6_native_graphics.cpp:785`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp:434`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp:500`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp:522`
- `thirdparty/rexglue-sdk/src/graphics/command_processor.cpp:1076`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp:1923`

Meaning:

- the native backend can observe and optionally intercept swap
- but the real frame is still built by the Xenia-derived path unless takeover is explicitly enabled
- even then, current takeover is only a replay-preview scaffold, not true AC6 rendering

### 2. AC6-local graphics code is capture and heuristic scaffolding

Current AC6-local capture records:

- draw/clear/resolve events
- shadow state for RT/DS/textures/streams/samplers/fetch constants
- per-frame summaries and pass heuristics

Key code:

- `src/d3d_state.h:97`
- `src/d3d_state.h:152`
- `src/d3d_hooks.cpp:272`
- `src/d3d_hooks.cpp:783`
- `src/ac6_native_graphics.cpp:355`
- `src/ac6_native_graphics.cpp:592`

What is missing for real native replay:

- shader identity capture robust enough for direct native shader selection
- float/bool/loop constant capture
- blend/raster/depth state capture
- index/vertex layout reconstruction beyond basic pointers/strides
- actual resource contents and ownership
- stable material classification
- explicit effect-system semantics

### 3. Presentation is already partly native, but not frame construction

The presenter stack is already host-native enough to keep for now:

- `thirdparty/rexglue-sdk/src/native/ui/presenter.cpp`
- `thirdparty/rexglue-sdk/src/native/ui/d3d12/d3d12_presenter.cpp`

That code is not the main blocker.

The real blocker is that the source image being presented still comes from an emulated graphics core.

### 4. Remaining visible bugs still map to effect-path subsystems

Missile trails remain strongly consistent with memexport/resource coherency problems.

Relevant code:

- `thirdparty/rexglue-sdk/src/graphics/util/draw.cpp:722`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp:2626`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp:2643`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp:2803`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp:2813`

Cloud/explosion pixelation still maps primarily to texture format and filtering behavior:

- `thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp:96`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp:427`

Conclusion:

- presenter work alone is not enough
- frame ownership and effect-path resource ownership are the real migration target

## Native End-State Definition

The target state is not "less emulation."

The target state is:

1. AC6 rendering is described in native render passes, not Xenos packets.
2. AC6 resources are native D3D12 resources owned by host systems.
3. Textures/models/materials are loaded through native asset pipelines visible to the OS.
4. Guest GPU shared memory is no longer the primary rendering source.
5. Runtime shader translation is no longer required on the hot path for known AC6 workloads.
6. `VdSwap` is only a compatibility boundary, not the real render submission mechanism.
7. The presenter consumes a native AC6 backbuffer/final composite, not a swap texture extracted from emulated state.
8. Loose-file and packaged asset overrides are supported for texture/model swaps.

## "OS Owns The Game" Translation

The phrase "the OS owns the game like a native port" needs a concrete engineering meaning.

For this project, that means:

- the executable is a host-native process with host-native rendering
- host filesystems provide the authoritative asset source
- host tools can inspect, replace, and override assets without guest GPU-memory surgery
- the host graphics API owns texture/buffer residency and synchronization
- the host renderer owns shader/pipeline creation
- the host windowing/presentation layer owns swapchain behavior

It does not literally require deleting every compatibility shim on day one.

It does require making the following things host-authoritative:

- asset discovery
- asset loading
- resource decoding
- resource creation
- frame submission
- final composition

## Native Asset Ownership And Modding Target

This is a first-class requirement, not an optional post-launch feature.

### Required outcome

Users must be able to do:

- texture swaps
- model swaps
- material swaps
- effect texture replacements
- optional loose-file overrides during development
- packaged mod loading for distribution

without editing guest memory or relying on emulator-specific patching.

### Required architecture

The native renderer and asset pipeline must introduce:

1. Native asset registry

- canonical asset IDs for textures, models, materials, shaders, and effect resources
- mapping from legacy AC6 asset references to host-native asset IDs

2. Native filesystem-backed content roots

- base game extracted content root
- optional update/DLC root
- optional mod root stack
- deterministic precedence order

3. Override resolver

- loose files override packaged content
- mod packages override base extracted assets
- conflict resolution rules are deterministic and logged

4. Import/conversion pipeline

- texture decode to host-authoritative intermediate or final GPU-ready formats
- model conversion to native mesh/skin/material structures
- material/effect descriptor extraction
- shader/effect metadata extraction where possible

5. Stable resource manifests

- asset ID
- original source path/container reference
- converted output path
- hashes for invalidation
- dependency graph

6. Runtime resource cache

- native texture cache keyed by asset ID and variant
- native mesh cache keyed by asset ID and LOD/variant
- native material cache keyed by asset ID and permutation

### Modding workflow target

The intended modding workflow should become:

1. Extract or convert base AC6 assets into a host-visible content tree.
2. Build a manifest that maps game asset references to native asset IDs.
3. Allow `mods/<modname>/...` content roots with identical asset IDs or manifest aliases.
4. Resolve overrides before resource creation.
5. Rebuild only affected native caches when assets change.

For development, support should exist for:

- startup scan of mod roots
- verbose override logging
- optional hot-reload for textures/materials in debug builds

Hot-reload for skeletal models can come later if needed, but the file format and ownership model should support it from the start.

## Architecture Principles

### Principle 1: Keep the host-native UI/presenter stack where it already works

Do not rewrite:

- windowing
- surface ownership
- basic presenter shell
- immediate drawer / overlays

unless the native frame path proves they are structurally blocking.

### Principle 2: Remove graphics emulation from the hot path before chasing total runtime deletion

The first big win is:

- no PM4/ringbuffer/MMIO submission on the frame hot path
- no shared-memory texture interpretation on the frame hot path
- no shader translation on the frame hot path

### Principle 3: Make effects a first-wave native target

Opaque geometry is not enough.

The migration must explicitly prioritize:

- missile trails
- clouds
- explosions
- fullscreen composites
- post-processing

### Principle 4: Asset ownership and renderer ownership must migrate together

If rendering becomes native but assets still come from guest-style GPU memory interpretation, modding remains weak and effect bugs remain harder to isolate.

### Principle 5: D3D12 only first

No Vulkan parity work until:

- D3D12 native frame ownership is stable
- effects are correct
- asset override workflows exist
- performance is acceptable

## Major Systems To Retire

These are the emulator-era graphics systems that must eventually leave the default path:

1. PM4/ringbuffer submission path

- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp`
- `thirdparty/rexglue-sdk/src/graphics/command_processor.cpp`

2. Xenos command processor execution

- `thirdparty/rexglue-sdk/src/graphics/command_processor.cpp`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp`

3. Shared-memory guest resource interpretation on the render hot path

- `thirdparty/rexglue-sdk/include/rex/graphics/shared_memory.h`
- `thirdparty/rexglue-sdk/src/graphics/d3d12/shared_memory.*`

4. Texture decode/fetch behavior driven by guest texture state

- `thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp`

5. Shader microcode translation on the render hot path

- `thirdparty/rexglue-sdk/src/graphics/pipeline/shader/*`

6. EDRAM/render-target ownership transfer emulation

- `thirdparty/rexglue-sdk/src/graphics/d3d12/render_target_cache.cpp`

7. Swap-texture extraction as the final present source

- `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp:1923`

## Major Native Systems To Add

The native port path needs new systems, not only removals.

### 1. Native asset subsystem

Proposed tree:

- `src/native/assets/*`
- `src/native/assets/import/*`
- `src/native/assets/runtime/*`
- `src/native/assets/mods/*`

Responsibilities:

- asset registry
- manifests
- content roots
- override resolution
- conversion products
- host-visible metadata

### 2. Native graphics subsystem

Proposed tree:

- `src/native/graphics/*`
- `src/native/graphics/d3d12/*`
- `src/native/graphics/framegraph/*`
- `src/native/graphics/resources/*`
- `src/native/graphics/effects/*`

Responsibilities:

- renderer init/shutdown
- frame execution
- pass graph
- resource lifetime
- final composite
- integration with presenter

### 3. Native shader subsystem

Proposed tree:

- `src/native/graphics/shaders/*`
- `assets/native/shaders/*`

Responsibilities:

- shader fingerprint database
- native HLSL/DXIL compilation
- permutation management
- compatibility fallback during transition

### 4. Native effect subsystem

Proposed tree:

- `src/native/graphics/effects/trails/*`
- `src/native/graphics/effects/particles/*`
- `src/native/graphics/effects/clouds/*`
- `src/native/graphics/effects/explosions/*`

Responsibilities:

- native effect buffer formats
- native effect update and draw paths
- correct filtering/blending/composite behavior

## Phased Migration Plan

### Phase 0: Lock the migration target

Deliverables:

- declare D3D12 as the only first-wave native graphics backend
- declare asset ownership/modding as a core scope item
- freeze new feature work inside the emulator graphics path except bug containment and required diagnostics

Exit criteria:

- master plan accepted as the working architecture target

### Phase 1: Expand capture from heuristic to reconstruction-grade

Current capture is insufficient for native replay.

Add:

- vertex shader identity
- pixel shader identity
- float constant blocks
- bool/loop constant state
- blend/depth/raster state
- scissor state
- index format/base/size
- vertex declaration details
- texture descriptors, not just texture pointers
- resolve source/destination descriptors
- memexport stream descriptors

Potential touch points:

- `src/d3d_hooks.cpp`
- `src/d3d_state.h`

Exit criteria:

- a single captured frame can be serialized into a deterministic AC6 frame description with enough data to classify passes and drive partial native replay

### Phase 2: Build a native asset registry and conversion pipeline

This is the first real "OS owns the game" milestone.

Add:

- extracted content root conventions
- manifest format
- conversion database
- texture import path
- model import path
- material import path
- mod override root stack

Requirements:

- all textures used by swap/scene/effects can be resolved by host asset ID
- converted outputs are reproducible
- overrides are logged

Exit criteria:

- texture swaps work through loose files or mod roots before the full native renderer is complete
- at least one model class can be replaced by host asset override

### Phase 3: Break hard dependency on PM4 swap for frame ownership

Replace `VdSwap` behavior progressively:

- keep callback semantics
- keep guest frame-boundary expectations
- stop requiring synthetic `PM4_XE_SWAP` for normal presentation

Required code:

- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp`
- `thirdparty/rexglue-sdk/include/rex/system/interfaces/graphics.h`

Exit criteria:

- normal frame present can complete without injecting swap packets into the command processor

### Phase 4: Stand up a real native renderer skeleton

Build a native renderer that owns:

- device-facing render resources
- command recording
- transient buffers
- transient render targets
- final backbuffer handoff to presenter

Do not aim for parity yet.

Initial goal:

- framegraph shell
- pass scheduling
- native swap/present ownership
- debug visualization of native passes

Exit criteria:

- a native frame can be submitted and presented without going through the emulated swap-texture path

### Phase 5: Native texture ownership and effect texture correctness

Make texture creation host-authoritative.

Tasks:

- move texture decode/import from emulated fetch-time interpretation to asset import/load time where possible
- establish native sampler/filter policy per asset/material/effect use
- preserve exact handling for AC6 effect-critical formats
- separate scene textures from effect/intermediate textures

Why this phase matters:

- clouds and explosions are directly tied to texture correctness and filtering

Exit criteria:

- cloud/explosion textures are loaded through native resource ownership
- the native renderer can sample them without relying on the D3D12 emulation texture cache

### Phase 6: Native memexport/effect-buffer replacement

This phase is the key to fixing missile trails.

Tasks:

- identify all AC6 passes using memexport-driven effect data
- replace shader-to-guest-memory export semantics with native structured buffers or typed effect buffers
- preserve any CPU-visible side effects only where the game actually consumes them
- create a compatibility bridge for transitional cases

Important rule:

- do not keep guest shared memory as the authoritative effect buffer source if the goal is native ownership

Exit criteria:

- missile trails are produced from native effect buffers
- the effect no longer depends on CPU readback from memexport ranges

### Phase 7: Native shader strategy

Move away from runtime microcode translation for known AC6 workloads.

Tasks:

- fingerprint hot shaders from capture
- cluster by real AC6 material/effect role
- hand-author or generate stable native HLSL/DXIL equivalents
- keep translator-backed fallback only for unknown signatures during transition

Priority order:

1. final composite / present-adjacent passes
2. clouds/explosions/trails
3. HUD/post
4. opaque scene passes

Exit criteria:

- effect and final composite passes no longer require runtime Xbox shader translation

### Phase 8: Replace EDRAM/resolve emulation with an explicit native framegraph

The native renderer must explicitly own:

- scene color/depth targets
- transparent/effect intermediate targets
- post targets
- resolves
- composites
- final output

Tasks:

- define transient render target pools
- define pass resource dependencies
- define barriers explicitly
- stop inheriting Xenos ownership-transfer behavior

Exit criteria:

- frame execution is graph-defined rather than inferred from guest EDRAM semantics

### Phase 9: Cut over default rendering

At this point:

- native renderer is default
- emulated graphics path remains behind a debug flag only
- presenter consumes native final output

Exit criteria:

- normal gameplay does not require the emulated graphics backend

### Phase 10: Retire emulator-era graphics code from default build

Remove default reliance on:

- PM4 swap synthesis
- command processor frame execution
- swap texture extraction
- shared-memory hot-path rendering
- shader translation hot path
- EDRAM render-target ownership logic

Keep only:

- minimal compatibility stubs required by the remaining runtime surface

Exit criteria:

- the graphics hot path is native-owned end to end

## Modding Plan In Detail

### Texture swaps

Minimum supported workflow:

- extract/convert base textures to a host-visible format tree
- map original asset IDs to converted file paths
- allow mod roots to override by asset ID
- create native GPU textures from overridden files at startup

Preferred formats:

- lossless intermediates for archival and conversion stability
- GPU-compressed deliverables where practical

### Model swaps

Minimum supported workflow:

- convert mesh containers into host-native mesh format
- preserve material slot mapping
- preserve skeleton/bone naming if animated
- preserve collision/attachment metadata if required by gameplay

A model override should be allowed only if:

- required material slots exist
- required bones/attachments exist for animated assets
- bounds and dependency validation pass

### Material/effect swaps

Minimum supported workflow:

- allow material descriptors to override texture bindings, blend modes, and shader variants within validated limits

### Manifest design

Each asset should have:

- stable asset ID
- source container/path
- asset class
- dependency list
- converted path
- mod override path if active
- hash/version metadata

### Safety and diagnostics

The runtime should log:

- which overrides were applied
- which overrides were rejected
- why they were rejected

This is necessary for actual mod usability.

## Validation Matrix

Validation must be scenario-driven, not just boot-driven.

### Core scenarios

- boot to menu
- hangar
- mission start
- heavy cloud map
- explosion-heavy combat
- repeated missile firing with trails in view
- cutscene with HUD transitions
- fullscreen and windowed
- alt-tab and resize

### Graphics gates

- clouds no longer pixelated
- explosions no longer pixelated
- missile trails visible and stable
- HUD correct
- post effects correct
- no black present frames
- no persistent flicker

### Native-ownership gates

- texture override works from host filesystem
- at least one model override works from host filesystem
- asset override precedence is deterministic
- no guest-memory patching required for those swaps

### Performance gates

- lower CPU overhead than the PM4 path in identical scenes
- fewer shader-compile hitches
- fewer readback-induced stalls
- stable frame pacing with FPS unlock on and off

## Risks

### Risk 1: shader replacement scope explodes

Mitigation:

- target stable hot passes first
- keep fallback path temporarily
- fingerprint and cluster before rewriting

### Risk 2: AC6 still depends on guest-visible side effects in obscure places

Mitigation:

- add compatibility bridges only where verified
- do not keep broad shared-memory semantics just because they are convenient

### Risk 3: model swap support is blocked by opaque asset formats

Mitigation:

- build extraction/conversion tooling in parallel with renderer work
- define intermediate host-native formats early

### Risk 4: a half-native state becomes permanent

Mitigation:

- define retirement criteria for each old subsystem
- do not accept "native preview" as done

## Recommended Immediate Work Order

This is the highest-value sequence from the current repo state.

1. Expand AC6 capture so it records reconstruction-grade draw/material/shader state.
2. Build the native asset registry and content-root override system.
3. Add texture override support first to prove host-native asset ownership.
4. Rework `VdSwap` so native frame boundaries can complete without PM4 swap synthesis.
5. Build the native framegraph shell and presenter handoff.
6. Replace memexport-driven trail buffers with native effect buffers.
7. Replace cloud/explosion texture handling with native-owned effect textures and material paths.
8. Introduce native shader replacements for effect/composite passes.
9. Move opaque scene rendering into the native path.
10. Cut over default rendering and retire emulator graphics code.

## Definition Of Success

The migration is successful when:

- the graphics hot path does not depend on Xenos packet execution
- the presenter is fed by a native AC6 final image
- clouds, explosions, and missile trails render correctly in the native path
- textures and models can be swapped through host-native asset overrides
- the game behaves like a native PC title from the perspective of rendering, assets, and presentation

At that point, the OS effectively "owns" the game in the way that matters:

- host filesystem owns content discovery
- host GPU API owns rendering
- host resource systems own assets
- mods operate through native content overrides instead of emulator-specific memory behavior
