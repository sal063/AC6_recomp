# AC6 Native Graphics API Deprecation Timeline

Date: 2026-04-17
Scope: AC6 recomp graphics only

This document defines which legacy emulation-facing APIs, flags, and behaviors are deprecated, when they will be removed from AC6 shipping builds, and what replaces them.

Companion planning documents:

- `AC6_NATIVE_GRAPHICS_MIGRATION_STRATEGY_2026-04-17.md` defines target architecture, phase goals, and validation gates.
- `AC6_NATIVE_GRAPHICS_EMULATION_AUDIT_2026-04-17.md` defines the current emulation-owned components and their retirement priorities.
- `AC6_NATIVE_GRAPHICS_EXECUTION_PLAN_2026-04-17.md` defines workstreams, owner roles, dependencies, and required phase artifact packages.

## Principles

- Deprecations are communicated with a hard calendar date and a version tag.
- Every deprecated API has:
  - a replacement API
  - a migration script or mechanical recipe
  - a test plan proving parity before removal
- AC6 production releases remain stable; removals happen only after a full milestone with native default-on canary success.

## Timeline

### Milestone M0: Announcement and inventory (T0)

- Freeze: no new features added to legacy PM4/Xenos render path.
- Publish: migration strategy and emulation audit.
- Add: compiler warnings or runtime warnings for deprecated config values.

### Milestone M1: Parallel native scaffolding (T0 + 4-6 weeks)

- Add: native renderer modules behind opt-in flags.
- Keep: legacy renderer as production default.
- Deprecate (soft): legacy render debug flags that only apply to emulation internals.

### Milestone M2: Native assets and shaders (T0 + 8-12 weeks)

- Add: offline shader compilation and asset cooker.
- Deprecate (soft): runtime shader translation on the native path (native must not depend on it).

### Milestone M3: Hybrid subsystem migration (T0 + 12-20 weeks)

- Add: subsystem routing flags.
- Deprecate (hard): any new reliance on `PM4_XE_SWAP` behavior in AC6 builds.
- Start: dual-render parity gates for selected scenes in CI.

### Milestone M4: Native present authority (T0 + 20-28 weeks)

- Default-on: native present for canary channel.
- Deprecate (hard): `VdSwap` synthesized PM4 swap as a required behavior in AC6 shipping.

### Milestone M5: Remove emulation render linkage from AC6 (T0 + 28-36 weeks)

- Remove from AC6 target linkage:
  - `CommandProcessor`
  - Xenos shader translators
  - texture cache / render target cache tied to guest state
  - shared memory hot-path GPU resource model
- Keep only:
  - presenter / windowing
  - minimal kernel compatibility shims

### Milestone M6: Clean-up and stabilization (T0 + 36-48 weeks)

- Delete: dead flags, emulation-only code paths, and debug tools that depend on PM4/Xenos.
- Lock: final API surface and documentation.
- Start: 90-day regression watch window for production.

## Deprecated / Removed Items (AC6 Scope)

### Legacy swap authority

Deprecated:

- `PM4_XE_SWAP` as the authoritative display path for AC6.

Replacement:

- native frame boundary and final composite owned by native renderer, presented via presenter.

Removal milestone:

- M5

### Runtime shader translation and guest shader caches

Deprecated:

- any reliance on Xenos microcode translation for AC6-native rendering

Replacement:

- offline-compiled HLSL outputs (DXIL, SPIR-V, MSL) and pipeline libraries

Removal milestone:

- M5

### Guest texture decode / untiling / cache invalidation (shipping)

Deprecated:

- texture cache for static content in shipping builds

Replacement:

- cooked textures and explicit streaming residency

Removal milestone:

- M5 (with transitional support in M3-M4 for genuinely dynamic textures only)

## Compatibility Flags Policy

- Flags that control emulation internals remain dev-only immediately after M2.
- Production builds may keep:
  - presenter scaling options
  - vsync / tearing options
  - GPU debug markers toggles
  - quality settings backed by native renderer options

## Required Documentation Updates

- `README.md` must describe:
  - how to enable native renderer
  - supported platforms and backends
  - how to run parity tests
  - how to run benchmarks and leak checks

## Migration Script Requirements

All legacy config keys must have:

- mechanical mapping or safe removal guidance
- a linter mode to detect unknown or deprecated keys
- CI job that validates clean config on the default distribution
