# AC6 Native Graphics Execution Plan

Date: 2026-04-17
Scope: `AC6 recomp`, `Windows + Linux + macOS`
Purpose: convert the migration strategy, emulation audit, and deprecation timeline into an execution model with named workstreams, owner roles, dependencies, deliverables, and near-term checkpoints.

Related documents:

- `AC6_NATIVE_GRAPHICS_MIGRATION_STRATEGY_2026-04-17.md`
- `AC6_NATIVE_GRAPHICS_EMULATION_AUDIT_2026-04-17.md`
- `AC6_NATIVE_GRAPHICS_API_DEPRECATION_TIMELINE.md`

## Operating Rules

- The strategy document defines target architecture, release gates, and phase sequence.
- The audit document defines what must be retired and its replacement targets.
- The deprecation timeline governs when legacy interfaces become dev-only, blocked, or removed.
- This execution plan assigns work ownership by role, defines handoff artifacts, and turns phase goals into weekly-trackable work.
- No workstream may expand the legacy PM4 / Xenos path except for temporary validation hooks required to compare outputs or preserve release safety.

## Workstream Ownership Matrix

| ID | Workstream | Primary owner role | Supporting roles | Source driver | Exit artifact |
| --- | --- | --- | --- | --- | --- |
| WS1 | Native render core (`RenderDevice`, `RenderGraph`, `FrameScheduler`) | backend lead | engine integration lead, QA / validation lead | strategy phases 1, 3, 4 | bootable native renderer core on all target platforms |
| WS2 | AC6 frontend integration (`Ac6RenderFrontend`, patched entry points, routing flags) | engine integration lead | backend lead, QA / validation lead | strategy phases 1, 3 | redirected AC6 render call sites with subsystem routing controls |
| WS3 | Asset cooker and package schemas | assets / tools lead | backend lead, security lead | strategy phase 2 | versioned cooker outputs and schema validation job |
| WS4 | Shader authoring and offline compilation | backend lead | assets / tools lead, QA / validation lead | strategy phase 2 | DXIL / SPIR-V / MSL package pipeline with reflection manifests |
| WS5 | Validation harness, benchmark scenes, golden images | QA / validation lead | engine integration lead, backend lead | strategy phases 0, 1, 3, 6 | deterministic scene manifests, diff thresholds, perf dashboards |
| WS6 | Presentation cutover and frame pacing | backend lead | engine integration lead, QA / validation lead | strategy phase 4 | native-present shipping route with pacing telemetry |
| WS7 | Compatibility shims (`xboxkrnl_video`, interrupt timing, limited bridge exports) | engine integration lead | backend lead | audit P0 items, strategy phases 3, 4, 5 | minimal non-render compatibility layer after cutover |
| WS8 | Emulation retirement and AC6 link cleanup | backend lead | engine integration lead, security lead | audit P0 and P1 retirements, strategy phase 5 | build evidence showing AC6 render linkage no longer depends on emulation stack |
| WS9 | Security, fuzzing, and package hardening | security lead | assets / tools lead, backend lead | strategy phase 6 | signed or checked package path, parser review, fuzz coverage |
| WS10 | Docs, config migration, and deprecation closure | AC6 graphics migration lead | all leads | deprecation timeline phases 2 through 6 | user and developer docs, config mappings, removed-API change log |

## Audit Item To Workstream Mapping

| Audit item | Priority | Replacement target | Owning workstream |
| --- | --- | --- | --- |
| `VdSwap` / guest submission / PM4 swap synthesis | `P0` | AC6-native frame boundary API | WS2, WS6, WS7 |
| `GraphicsSystem` MMIO ownership | `P0` | `Ac6NativeRenderSystem` behavior split across native core and scheduler | WS1, WS7 |
| `CommandProcessor` and PM4 parsing | `P0` | title-native render graph compiler | WS1, WS2, WS8 |
| Xenos registers and packet model | `P1` | native render-state and material descriptors | WS2, WS4, WS8 |
| shared guest GPU memory model | `P1` | host-native allocators and transient arenas | WS1, WS8 |
| texture cache / untiling / invalidation | `P1` | cooked textures and explicit streaming | WS3, WS8 |
| render-target cache / EDRAM / resolve ownership | `P1` | explicit render graph attachments and history buffers | WS1, WS6, WS8 |
| shader translation and guest shader caches | `P1` | authored HLSL plus offline backend outputs | WS4, WS8 |
| emulation traces and PM4 diagnostics | `P2` | native capture and pass timeline logs | WS5, WS10 |
| presenter / provider / windowing | keep and adapt | native final-color presentation path | WS6 |

## Phase Exit Package

Every phase closes only when the following package exists in source control or CI artifacts.

### Phase 0 package

- baseline performance report on agreed hardware matrix
- frozen benchmark scene manifest list
- initial golden image set from the current shipping path
- issue-tracker export for `graphics-emulation`
- accepted ownership table for all `P0` and `P1` audit items

### Phase 1 package

- boot logs proving native stack initialization on Windows, Linux, and macOS
- dual-run telemetry showing native scene-build cost
- compile-time flag and runtime kill switch for native scaffolding
- first integration document for patched render entry points

### Phase 2 package

- shader compile manifest with per-backend outputs
- asset cooker schema reference and versioning policy
- placeholder-correct render evidence for benchmark scenes
- CI jobs for shader validation and schema validation

### Phase 3 package

- subsystem routing matrix showing native versus legacy coverage
- parity dashboard for benchmark scenes per migrated subsystem
- canary performance deltas for each subsystem cutover
- rollback instructions for subsystem-level disablement

### Phase 4 package

- `VdSwap` contract note describing its reduced role as native frame boundary only
- present timing telemetry against baseline
- proof that production no longer depends on emulated swap extraction
- release-branch rollback plan for one milestone

### Phase 5 package

- link or build report proving AC6 no longer links `CommandProcessor`, shader translators, shared-memory render resources, or guest-state render caches
- updated compatibility shim inventory showing what remains and why
- full regression matrix results against parity, perf, and memory gates
- removed-interface change log draft for release notes

### Phase 6 package

- security review signoff
- fuzzing coverage report and unresolved issue list
- 90-day watch plan with owners for crash, regression, and memory signals
- deprecation closure checklist marked complete

## Dependency Chain

The main sequence for delivery is:

1. WS5 establishes baselines and benchmark scenes.
2. WS1 and WS2 make native scaffolding boot and accept AC6-originated requests.
3. WS3 and WS4 replace runtime asset and shader interpretation.
4. WS5 validates subsystem migration while WS2 routes one subsystem at a time.
5. WS6 moves presentation authority to native outputs.
6. WS7 keeps only the smallest compatibility surface needed by the title.
7. WS8 removes emulation-linked rendering code from the AC6 target.
8. WS9 and WS10 close production-hardening and deprecation requirements.

Blocking dependencies:

- WS2 depends on WS1 interface stability.
- WS3 and WS4 must land before any subsystem is considered production-native.
- WS6 must not cut over until WS5 parity dashboards are green for the selected canary set.
- WS8 must not delete linkage until WS6 has held stable for one milestone and deprecation gates are satisfied.
- WS10 cannot close until config migration rules, README updates, and removed-interface notes are merged.

## First 30 Days

### Week 1

- confirm owner roles for WS1 through WS10
- freeze target hardware matrix and benchmark scene candidates
- create the `ac6_native_renderer/` module tree and backend interface skeleton
- define the runtime flag names for global enable plus subsystem routing

### Week 2

- patch one AC6 render entry point into `Ac6RenderFrontend`
- establish dual-run telemetry capture for scene build cost
- write the initial benchmark scene manifest format
- inventory every config flag or runtime switch that currently affects emulation-era graphics behavior

### Week 3

- stand up shader compile prototypes for DXIL and SPIR-V
- draft asset cooker schema version `v0`
- capture the first golden images for UI, cockpit, terrain, and effects scenes
- define the canary perf dashboard and threshold owners

### Week 4

- review boot status on all three desktop platforms
- decide whether the first migrated subsystem after scaffolding is post-FX or terrain
- publish the deprecation warning list for dev-only emulation flags
- lock the first milestone review deck with risks, blockers, and go / no-go criteria

## Weekly Review Format

Every weekly program review must answer the following:

- which workstreams changed state this week
- which audit `P0` or `P1` items moved closer to retirement
- which benchmark or parity gates improved or regressed
- whether any subsystem rollback switch was required
- which deprecation warnings were added, expanded, or blocked by compatibility needs
- which unresolved risks threaten the next milestone

Required metrics:

- open `graphics-emulation` issues
- migrated subsystem count
- benchmark delta versus baseline
- parity diff pass rate
- native boot status by platform
- memory watermark trend
- crash-free soak hours

## Decision Gates

### Gate A: Native scaffolding ready

Approve only if:

- native stack boots on Windows, Linux, and macOS
- native scene build executes without presenting
- kill switch restores current shipping behavior

### Gate B: Native assets and shaders ready

Approve only if:

- no runtime shader translation remains on the native path
- native assets render placeholder-correct benchmark scenes
- cooker and shader validation jobs run in CI

### Gate C: Subsystem canary ready

Approve only if:

- target subsystem passes parity thresholds for benchmark scenes
- canary performance stays within agreed tolerance
- subsystem rollback flag is proven in a test build

### Gate D: Native present authority ready

Approve only if:

- `VdSwap` is no longer required to synthesize `PM4_XE_SWAP`
- present timing meets or beats baseline
- no production frame depends on emulated frontbuffer extraction

### Gate E: Emulation render linkage removal ready

Approve only if:

- AC6 link outputs exclude the retired emulation render objects
- perf, memory, and parity release gates are green
- fallback strategy exists only as a tagged branch, not a shipping runtime path

## Deprecation Closure Checklist

- every legacy graphics-facing config key is mapped, replaced, or explicitly removed
- dev-only emulation flags are blocked from production builds after phase 2
- every removed interface has a replacement or compatibility note
- CI validates that the default distribution contains no deprecated production config
- release notes include the removal milestone and user-visible migration guidance

## Open Risks

| Risk | Impact | Owner role | Mitigation |
| --- | --- | --- | --- |
| AC6 render entry points are more fragmented than expected | slows WS2 and increases bridge complexity | engine integration lead | front-load call-site inventory and group hooks by subsystem |
| Cross-platform backend drift causes different visual behavior | blocks parity and canary promotion | backend lead | keep shared render graph semantics strict and validate with deterministic scene manifests |
| Asset cooker schema churn invalidates earlier native content | slows WS3 and WS5 | assets / tools lead | version schemas from day one and pin benchmark manifests to versions |
| `VdSwap` timing contracts affect non-render title behavior | delays WS6 and WS7 | engine integration lead | isolate interrupt-sensitive behavior into compatibility shims and test title timing explicitly |
| Removal of emulation diagnostics leaves developers blind during bring-up | increases defect turnaround time | QA / validation lead | deliver native capture, pass timeline dumps, and GPU marker conventions before broad deletion |

## Definition Of Done

This execution plan is considered complete enough to drive delivery when:

- every workstream has an accepted owner role
- every `P0` and `P1` audit item is mapped to a workstream and target phase
- every phase has a required artifact package and approval gate
- the first 30 days of work can begin without waiting for another planning document
