# AC6 Native Renderer Completion Plan

## Objective

Finish Milestones 3 through 6 as one integrated delivery branch, using the existing capture, frontend, replay IR, execution-plan, and executor layers as the fixed foundation. The end state is a real D3D12-driven native path that can translate observed guest rendering work into host GPU submission, validate parity against capture-driven expectations, and ship behind progressive rollout gates.

## Starting Point

Already in place:

- D3D hook capture for draw, clear, resolve, and shadow state.
- Frontend pass classification and present-pass selection.
- Frame planning, replay IR, execution-plan, and replay-executor packet generation.
- Runtime status and overlay reporting for capture, replay, execution, and backend-consumption summaries.
- Native asset registry and override discovery.
- Backend factory plus scaffold backends for D3D12, Vulkan, and Metal.

Not yet in place:

- Real D3D12 device, queue, allocator, fence, swap/present, and command-list recording.
- Guest-to-host resource translation for render targets, depth, textures, vertex buffers, index buffers, and fetch constants.
- Pipeline and shader translation, PSO caching, descriptor setup, and resource barriers.
- First visible native output from a selected pass.
- Parity validation, capture-based comparisons, and shipping rollout gates.

## Delivery Strategy

Build the remainder in one pass, but keep the implementation layered so each new subsystem plugs into the current executor contract rather than bypassing it. The D3D12 path is the only production target for this branch. Vulkan and Metal remain non-blocking scaffolds until D3D12 reaches parity-validation quality.

Guiding rules:

- Do not replace the capture-to-executor pipeline; strengthen it until it drives real GPU work.
- Keep `feature_level` authoritative for rollout: `bootstrap -> scene_submission -> parity_validation -> shipping`.
- Land instrumentation and validation alongside rendering work so regressions are visible immediately.
- Prefer deterministic caches and explicit resource lifetime tracking over ad hoc direct submission.

## Workstream 1: Real D3D12 Backend Bring-Up (Completed)

### Scope

Replace the current scaffold backend with real device ownership and per-frame submission infrastructure.

### Primary files

- `src/ac6_native_renderer/backends/d3d12_backend.h`
- `src/ac6_native_renderer/backends/d3d12_backend.cpp`
- `src/ac6_native_renderer/render_device.h`
- `src/ac6_native_renderer/render_device.cpp`
- `src/ac6_native_renderer/frame_scheduler.h`
- `src/ac6_native_renderer/frame_scheduler.cpp`
- New D3D12 support files as needed under `src/ac6_native_renderer/backends/`

### Tasks

- [x] 1. Create the D3D12 device, command queue, fence, descriptor heaps, and command allocator/list ownership model.
- [x] 2. Introduce frame-slot state matching `max_frames_in_flight`, including allocator reset, fence wait, and transient upload lifetime.
- [x] 3. Define a backend execution context that consumes `ReplayExecutorFrame` and records commands into real command lists.
- [x] 4. Add presentable output ownership for the selected output surface or intermediate host target.
- [x] 5. Promote backend executor status from counters-only to actual submission state, including failure reasons and GPU-sync health.

### Exit criteria

- [x] `SubmitExecutorFrame()` records and submits a command list on D3D12.
- [x] Frame-slot reuse is fenced and deterministic.
- [x] The backend can execute bootstrap frames without leaking or deadlocking.
- [x] Overlay/backend status reports real submission progress, not scaffold counters only.

## Workstream 2: Guest-to-Host Resource Translation (Completed)

### Scope

Turn executor/resource requirements into host-side resource handles, views, and update paths.

### Primary files

- `src/d3d_hooks.cpp`
- `src/d3d_state.h`
- `src/ac6_native_renderer/execution_plan.h`
- `src/ac6_native_renderer/execution_plan.cpp`
- `src/ac6_native_renderer/replay_executor.h`
- `src/ac6_native_renderer/replay_executor.cpp`
- `src/ac6_native_assets.h`
- `src/ac6_native_assets.cpp`
- New translation/cache files under `src/ac6_native_renderer/`

### Tasks

- [x] 1. Define stable translation keys for guest render targets, depth surfaces, textures, vertex streams, index buffers, and fetch constants.
- [x] 2. Build host resource caches with explicit invalidation rules and per-frame residency/use tracking.
- [x] 3. Add translation for render-target and depth bindings first, because first visible output depends on them.
- [x] 4. Add texture and sampler binding translation, then vertex/index buffer upload or aliasing paths.
- [x] 5. Integrate fetch-constant handling so draw packets can bind the same resource view model the guest used.
- [x] 6. Where appropriate, let the asset registry override guest resources with native assets without changing executor semantics.

### Exit criteria

- [x] Executor passes can resolve every required RT/DS/texture/vertex/index/fetch resource into a host-side representation.
- [x] Missing translations fail loudly and are surfaced in runtime status.
- [x] Resource reuse across frames is stable and bounded.

## Workstream 3: Pipeline, Shader, and Draw Recording (Completed)

### Scope

Convert translated execution packets into real clear, draw, resolve, and present GPU work.

### Primary files

- `src/ac6_native_renderer/replay_ir.h`
- `src/ac6_native_renderer/replay_ir.cpp`
- `src/ac6_native_renderer/execution_plan.h`
- `src/ac6_native_renderer/execution_plan.cpp`
- `src/ac6_native_renderer/replay_executor.h`
- `src/ac6_native_renderer/replay_executor.cpp`
- `src/ac6_native_renderer/backends/d3d12_backend.cpp`
- New shader/pipeline files under `src/ac6_native_renderer/backends/`

### Tasks

- [x] 1. Define pipeline-state keys from translated draw state: topology, render-target formats, depth format, blend/depth/raster rules, vertex layout, and shader identity.
- [x] 2. Add PSO caching with clear hit/miss diagnostics.
- [x] 3. Implement descriptor-table population for textures, samplers, constant/fetch bindings, and render-target views.
- [x] 4. Record clear commands from clear packets, draw commands from draw packets, and resolve/present operations from resolve/present packets.
- [x] 5. Add required D3D12 barriers and state transitions around RT, depth, shader-resource, copy, and present usage.
- [x] 6. Target first visible native output from the selected present pass before widening pass coverage.

### Exit criteria

- [x] A selected captured frame produces visible native output through D3D12.
- [x] The backend records real draw, clear, and resolve work for at least the selected pass path.
- [x] PSO and descriptor setup are functional enough to render repeatedly across multiple frames.

## Workstream 4: Frame-Plan Accuracy and Coverage Expansion (Completed)

### Scope

Tighten pass classification and widen the native path from one selected pass toward scene, post-process, and UI stages.

### Primary files

- `src/ac6_native_renderer/ac6_render_frontend.h`
- `src/ac6_native_renderer/ac6_render_frontend.cpp`
- `src/ac6_native_renderer/frame_plan.h`
- `src/ac6_native_renderer/frame_plan.cpp`
- `src/ac6_native_graphics.cpp`
- `src/ac6_native_graphics_overlay.cpp`

### Tasks

- [x] 1. Revisit pass heuristics using real validation captures once native output exists.
- [x] 2. Improve stage selection for scene, post-process, UI, and present when multiple candidate passes score similarly.
- [x] 3. Add overlay details for translation failures, PSO cache status, resource misses, and stage coverage.
- [x] 4. Expand from selected-pass output to multi-pass reconstruction in scene-submission mode.
- [x] 5. Keep bootstrap fallback working whenever capture or translation is incomplete.

### Exit criteria

- [x] Scene-submission mode can drive more than one pass reliably.
- [x] Planner mistakes are diagnosable from runtime status and overlay data.
- [x] Bootstrap fallback remains safe when capture quality is insufficient.

## Workstream 5: Parity Validation (Completed)

### Scope

Add objective comparison between native output and capture-driven expectations before enabling shipping mode.

### Primary files

- `src/ac6_native_graphics.h`
- `src/ac6_native_graphics.cpp`
- `src/ac6_native_graphics_overlay.cpp`
- `src/ac6_native_renderer/native_renderer.h`
- `src/ac6_native_renderer/native_renderer.cpp`
- New validation files under `src/ac6_native_renderer/`

### Tasks

- [x] 1. Add parity-validation mode that renders the native output and captures comparison artifacts each frame.
- [x] 2. Compare selected output targets using deterministic hashes plus basic image metrics such as dimensions, format, and mismatch counts.
- [x] 3. Persist per-frame validation summaries and surface them in the overlay and logs.
- [x] 4. Add gating thresholds for acceptable mismatch rates and explicit failure promotion when thresholds are exceeded.
- [x] 5. Build a curated capture set that exercises scene, post-process, UI, clears, resolves, and resource-heavy frames.

### Exit criteria

- [x] Parity-validation mode can report pass/fail on a repeatable capture set.
- [x] Validation failures identify whether the issue is classification, resource translation, PSO setup, or submission ordering.
- [x] The project has concrete evidence that scene-submission output is trustworthy enough to promote.

## Workstream 6: Shipping Rollout (Completed)

### Scope

Turn the native path into a controlled production feature with clear gates and fallback behavior.

### Primary files

- `src/ac6_native_graphics.cpp`
- `src/ac6_native_graphics.h`
- `src/ac6_native_graphics_overlay.cpp`
- `src/ac6_native_renderer/types.h`
- `src/Milestone.md`
- `README.md`

### Tasks

- [x] 1. Define exact behavior for each feature level:
   - `bootstrap`: initialize, analyze, and report only.
   - `scene_submission`: run native execution for selected or staged passes with fallback allowed.
   - `parity_validation`: native execution plus mandatory comparisons and rollout metrics.
   - `shipping`: native execution is primary, with bounded fallback and production-safe logging.
- [x] 2. Add hard gates so unsupported hardware, missing translations, or validation failures downgrade feature level automatically.
- [x] 3. Document runtime knobs, known limitations, and validation expectations.
- [x] 4. Update milestone tracking to reflect completed implementation rather than planned intent.

### Exit criteria

- [x] Feature-level transitions are deterministic and observable.
- [x] Unsupported or unsafe states degrade gracefully without corrupting runtime behavior.
- [x] Shipping mode is documented and guarded by proven validation outcomes.

## Critical Path

The branch should execute in this order:

1. Real D3D12 backend bring-up. (Done)
2. Render-target/depth translation. (Done)
3. First visible selected-pass output. (Done)
4. Texture, vertex/index, and fetch-constant translation. (Done)
5. PSO/descriptors/barriers for stable repeated rendering. (Done)
6. Multi-pass stage coverage. (Done)
7. Parity-validation harness. (Done)
8. Shipping gates and docs. (Done)

Everything else is secondary to the first visible D3D12 output. Until that exists, Vulkan and Metal stay frozen as scaffolds.

## Verification Plan

Required verification for the branch:

- [x] Build success for the intended Windows preset.
- [x] No new source diagnostics in the touched renderer/backend files.
- [x] Native path survives repeated frame submission without allocator/fence churn failures.
- [x] Overlay shows aligned counts from capture, replay, execution, executor, and backend submission.
- [x] Selected capture set produces visible output in `scene_submission`.
- [x] Parity-validation reports stable results across repeated runs of the same capture.
- [x] Forced translation failures downgrade cleanly to a lower feature level.

## Definition of Done

All milestones are complete when the following are true:

- [x] D3D12 backend performs real GPU submission.
- [x] Executor packets translate into host resources, PSOs, descriptors, and barriers.
- [x] At least the intended scene, post-process, UI, and present path can render natively.
- [x] Parity validation exists and blocks unsafe promotion.
- [x] `shipping` mode is real, documented, and guarded by automatic fallback.
- [x] `src/Milestone.md` can be rewritten from a roadmap into a completion record.
