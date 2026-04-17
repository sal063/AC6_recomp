# AC6 Native Graphics Doc Tracker

Date: 2026-04-17
Scope: `AC6 recomp`, `Windows + Linux + macOS`
Purpose: living documentation tracker for the AC6 native graphics migration package. This file is intended to be updated often as the doc set grows, changes direction, or closes gaps.

## Current Doc Set

| File | Role | Status |
| --- | --- | --- |
| `AC6_NATIVE_GRAPHICS_MIGRATION_STRATEGY_2026-04-17.md` | high-level target architecture, phases, validation, governance, and rollout rules | drafted and broadly structured |
| `AC6_NATIVE_GRAPHICS_EMULATION_AUDIT_2026-04-17.md` | inventory of the emulator-authoritative graphics stack, retirement priorities, and replacement targets | drafted and actionable |
| `AC6_NATIVE_GRAPHICS_API_DEPRECATION_TIMELINE.md` | deprecation schedule, removal milestones, compatibility flag policy, and migration-script requirements | drafted but still narrow in scope |
| `AC6_NATIVE_GRAPHICS_EXECUTION_PLAN_2026-04-17.md` | workstreams, ownership model, phase artifact packages, gates, and first-30-day execution planning | drafted and actionable |

## Scan Summary

What is already covered well:

- architecture direction for the native renderer
- phased rollout from scaffolding to final emulation removal
- current emulation dependency inventory with `P0` / `P1` / `P2` retirement priorities
- deprecation timing for the biggest legacy render behaviors
- execution structure with workstreams, gates, and first-month checkpoints
- implementation now includes parallel native frame observation, an initial frame-plan stage, and stage-aware diagnostic composition for scene, post-FX, and UI preview bring-up with per-stage captured clear history

What is not yet documented deeply enough:

- exact legacy config key to native replacement mapping
- exact legacy API / symbol / build-linkage removal matrix
- benchmark scene manifest definitions and ownership
- concrete benchmark hardware matrix and baseline capture format
- status history of what doc work has been added on each pass

## Completed Documentation Work

As of this scan, the native graphics docs package has completed the following planning layers:

1. Strategy:
   - defines success criteria, renderer architecture, phased rollout, validation, security, rollback, and governance
2. Audit:
   - identifies current emulator-owned graphics systems, why they must go, and what replaces them
3. Deprecation:
   - assigns milestone-based retirement timing for major legacy graphics-facing behavior
4. Execution:
   - maps planning into workstreams, owner roles, decision gates, artifact packages, and a first-30-day plan
5. Early implementation tracking:
   - records that AC6 bootstrap code now builds observed passes from captured D3D activity, derives an initial native frame plan for scene, post-FX, UI, and present selection, feeds that planner into the live swap path, and composes stage-aware preview output in both minimal-UAV and raster preview modes using per-stage captured clear colors and rect coverage

## Gaps To Fill Next

Priority order for missing docs:

1. `config migration matrix`
   - map every legacy graphics config key to `replace`, `rename`, `keep`, `dev-only`, or `delete`
2. `render API removal map`
   - list legacy render interfaces, where they are referenced, what replaces them, and in which milestone they disappear
3. `benchmark and parity pack`
   - define scene manifests, golden image ownership, naming, storage, and pass criteria
4. `module bring-up notes`
   - track backend bring-up status for `D3D12`, `Vulkan`, and `Metal`

## Active Focus

Current doc work should focus on making the package easier to execute, not broader.

That means:

- prefer concrete mappings over new strategy prose
- prefer tables and checklists over long narrative sections
- prefer documents that unblock implementation reviews and milestone signoff
- implementation follow-up should convert stage-aware diagnostic fills into real stage-specific replay inputs, starting with scene-stage ownership and then post-FX and UI composition
- next replay milestone should prioritize scene-stage draw and state reconstruction over more preview polish, because the preview path now already reflects stage-local clear structure
- keep the presenter and swap interception seam stable while replacing diagnostic color fills with captured-resource or native-resource driven content

## Working Rules For This File

- update this file every time a new native graphics migration doc is added
- update this file every time a major section is added or removed from an existing doc
- update `Completed Documentation Work`, `Gaps To Fill Next`, and `Next Doc To Write` together
- keep entries short and factual
- do not treat this as final design authority; it is the control page for the doc set

## Next Doc To Write

Recommended next document:

- `AC6_NATIVE_GRAPHICS_CONFIG_MIGRATION_MATRIX.md`

Why this is next:

- it is explicitly required by the deprecation timeline
- it converts policy into a migration tool for actual builds and configs
- it closes one of the biggest remaining gaps between planning and implementation

Suggested minimum contents:

- legacy key
- current meaning
- replacement key or behavior
- action (`keep`, `rename`, `remove`, `dev-only`)
- milestone of enforcement
- user migration note
- CI or linter rule needed

## Change Log

### 2026-04-17

- scanned the current native graphics migration doc folder
- confirmed four core planning docs exist: strategy, audit, deprecation timeline, and execution plan
- identified the main missing layer as concrete migration matrices and validation-pack detail
- created this living tracker so future doc work has a single update point
- noted implementation progress: the parallel native renderer now ingests captured frame activity, classifies observed passes, and derives an initial explicit frame plan for present bring-up
- updated implementation status: the live AC6 direct-swap path now consumes the parallel frame planner when selecting the present candidate instead of relying only on the older replay heuristic
- updated implementation status: both the minimal-UAV preview path and the raster preview path now compose a stage-aware diagnostic frame using planned scene, post-FX, and UI stages
- updated implementation status: planned scene, post-FX, and UI stages now carry captured clear histories and clear rect coverage into the stage-aware preview paths, so the preview output reflects stage-local clear structure rather than only broad stage tinting
- next implementation instruction: keep Phase 3 and Phase 4 scope focused on replacing stage-aware diagnostic fills with actual stage replay or native resource composition, not on adding new behavior to the legacy PM4 or Xenos path

## Ready State

The docs are no longer empty planning notes. They now form a usable planning stack:

- strategy answers `what the target is`
- audit answers `what must be replaced`
- deprecation answers `when legacy behavior goes away`
- execution answers `how the work gets organized`
- this tracker answers `what doc work is done and what comes next`
