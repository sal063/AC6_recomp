# Audio Native Migration Files

Date: 2026-04-06
Repo: `C:\AC6Recomp_ext`

## Purpose

This document maps the current audio stack from the old mixed emulator/rewrite state to the target
native recomp-oriented state.

The rule is:

- preserve guest-visible `XAudio*` / `XMA*` behavior AC6 depends on
- replace host/runtime/decode internals that are still emulator-shaped

This is **not** a plan to delete guest contracts.
It is a plan to replace emulator internals with native recomp internals.

## Current Regression Note

Current native-host experiment status:

- audio output works on the new WASAPI path
- gameplay is reported as sped up
- latest trace shows:
  - backend `wasapi`
  - requested period `256`
  - actual endpoint buffer `562`
  - persistent runtime drift around `784 ms`

This means the host sink is alive, but the runtime/clock/callback contract above it is still not
native enough. Host replacement alone is not sufficient.

## Replace vs Preserve

### Preserve

These are guest contract surfaces and should remain, though their internals can change:

- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio.cpp`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp`
- guest-visible `XAudio*` handles and callback registration
- guest-visible `XMA*` context semantics

### Replace

These are the real migration targets:

- SDL-first host playback
- driver-owned queueing
- frame-count-only tic progression
- callback-credit style scheduling
- legacy Xenia-centric XMA control flow
- AC6 hook sprawl used as permanent architecture

## File Map

### 1. Host Output Layer

Current primary/fallback files:

- `thirdparty/rexglue-sdk/src/audio/sdl/sdl_audio_driver.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/sdl/sdl_audio_driver.h`
- `thirdparty/rexglue-sdk/src/audio/sdl/sdl_audio_system.cpp`

Native replacements / targets:

- `thirdparty/rexglue-sdk/src/audio/wasapi/wasapi_audio_driver.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/wasapi/wasapi_audio_driver.h`
- optional future:
  - `thirdparty/rexglue-sdk/src/audio/host/xaudio2_audio_backend.cpp`
  - `thirdparty/rexglue-sdk/include/rex/audio/host/xaudio2_audio_backend.h`

Instructions:

- keep SDL only as fallback
- make WASAPI the real Windows primary path
- keep output event-driven
- report actual host latency / engine period
- do not let host buffering decide guest semantics
- add device reset / unplug recovery

### 2. Audio Runtime Core

Current files:

- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/audio_runtime.h`
- `thirdparty/rexglue-sdk/include/rex/audio/audio_client.h`
- `thirdparty/rexglue-sdk/src/audio/audio_clock.cpp`

Native replacement direction:

- keep these files, but make them authoritative instead of partially shadowing driver state

Instructions:

- move real queue ownership into `AudioRuntime`
- stop relying on per-driver private queues as the active source of truth
- convert `AudioDriver` into a sink that consumes runtime-owned frames
- make `PumpBackendIfNeeded()` real or remove it
- replace `submitted/consumed` shadow accounting with one authoritative runtime path
- make tic sample-accurate and derived from real consumption, not frame counts alone
- expose queue lead, host period, callback cadence, and drift as first-class telemetry

### 3. Host Backend Abstraction

Current files:

- `thirdparty/rexglue-sdk/include/rex/audio/host/host_audio_backend.h`
- `thirdparty/rexglue-sdk/src/audio/host/null_audio_backend.cpp`

Native replacements / additions:

- `thirdparty/rexglue-sdk/src/audio/host/wasapi_audio_backend.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/host/wasapi_audio_backend.h`

Instructions:

- stop leaving `IHostAudioBackend` mostly unused
- either:
  - finish the backend abstraction and move WASAPI into it
  - or remove the abstraction and make runtime->driver ownership explicit
- do not leave both models half-alive

Recommendation:

- long term, prefer `IHostAudioBackend` as the top-level host sink abstraction
- short term, the current native `WasapiAudioDriver` is acceptable as a migration bridge

### 4. Guest Kernel Audio Contract

Current files:

- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio.cpp`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp`

Replacement direction:

- keep file locations
- narrow responsibilities to guest validation / translation / status handling only

Instructions:

- preserve:
  - `XAudioRegisterRenderDriverClient`
  - `XAudioSubmitRenderDriverFrame`
  - `XAudioGetRenderDriverTic`
  - `XMA*` guest memory semantics
- remove host scheduling assumptions from these files
- avoid backend-specific logic here
- make `XAudioGetRenderDriverTic` reflect authoritative runtime clock state only

### 5. XMA Context and Decode Layer

Current mixed files:

- `thirdparty/rexglue-sdk/src/audio/xma/context.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/xma/context.h`
- `thirdparty/rexglue-sdk/src/audio/xma/decoder.cpp`
- `thirdparty/rexglue-sdk/src/audio/xma/xma_context_pool.cpp`
- `thirdparty/rexglue-sdk/src/audio/xma/xma_decoder_backend.cpp`
- `thirdparty/rexglue-sdk/src/audio/xma/xma_packet_parser.cpp`

Replacement direction:

- make context pool authoritative
- keep FFmpeg backend swappable, not architectural center

Instructions:

- preserve guest-visible:
  - valid bits
  - read/write offsets
  - loop state
  - block/enable/disable semantics
- move decode ownership away from synchronous kick/wait control
- remove polling-sleep behavior from guest-observable paths
- keep packet assembly and guest state mutation deterministic

### 6. Audio System Entry Layer

Current file:

- `thirdparty/rexglue-sdk/src/audio/audio_system.cpp`

Replacement direction:

- keep file
- simplify startup so it clearly states active backend, active scheduler model, and active XMA path

Instructions:

- log the actual backend selected after fallback
- log whether runtime queue ownership is authoritative
- log whether XMA path is legacy/bridge/native

### 7. AC6 Policy Layer

Current files:

- `src/ac6_audio_policy.cpp`
- `src/ac6_audio_policy.h`
- `src/ac6_audio_hooks.cpp`

Replacement direction:

- keep `ac6_audio_policy.*`
- reduce `ac6_audio_hooks.cpp` over time

Instructions:

- title-specific workarounds belong in `ac6_audio_policy.*`
- diagnostic trampolines and emergency workarounds in `ac6_audio_hooks.cpp` should be retired as
  core runtime/backend correctness improves
- do not let hook-based behavior remain the permanent engine architecture

### 8. Diagnostics and Trace

Current files:

- `thirdparty/rexglue-sdk/src/audio/audio_trace.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/audio_trace.h`
- `C:/AC6Recomp_ext/ac6_audio_trace.log`

Replacement direction:

- keep trace system
- reduce ad hoc AC6-only hook logging once runtime/backend signals are sufficient

Instructions:

- keep categories for:
  - audio core
  - kernel
  - clock
  - host
  - xma
  - AC6 policy
- add explicit host-period / endpoint-buffer / padding / drift fields to runtime-side logs
- rely less on giant hook traces

### 9. Build and Config Surface

Current files:

- `thirdparty/rexglue-sdk/src/audio/CMakeLists.txt`
- `out/build/win-amd64-relwithdebinfo/ac6recomp.toml`

Replacement direction:

- keep these files
- make backend choice explicit and restart-scoped

Instructions:

- backend config should support:
  - `wasapi`
  - `sdl`
- keep `null` only if reintroduced safely through runtime-owned deterministic consumption
- WASAPI must stay Windows-only in CMake

## Concrete Replacement Order

### Phase A: Stop SDL Being the Default Architecture

Files:

- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/src/audio/CMakeLists.txt`
- `thirdparty/rexglue-sdk/src/audio/wasapi/wasapi_audio_driver.cpp`

Instructions:

- done in part
- finish by making fallback/reporting robust
- measure real host period and actual device cadence

### Phase B: Make Runtime Queue Ownership Real

Files:

- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/audio_client.h`
- `thirdparty/rexglue-sdk/include/rex/audio/audio_driver.h`
- `thirdparty/rexglue-sdk/src/audio/sdl/sdl_audio_driver.cpp`
- `thirdparty/rexglue-sdk/src/audio/wasapi/wasapi_audio_driver.cpp`

Instructions:

- drivers should pull/copy from runtime-owned frame objects
- drivers should stop owning the primary queue model
- runtime should account partial consumption precisely

### Phase C: Replace Frame-Based Tic With Real Clock Semantics

Files:

- `thirdparty/rexglue-sdk/src/audio/audio_clock.cpp`
- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio.cpp`

Instructions:

- tic should reflect real consumed samples
- partial-frame host writes must affect tic correctly
- callback floor should remain a startup safety net, not the main clock

### Phase D: Remove Legacy XMA Control Flow

Files:

- `thirdparty/rexglue-sdk/src/audio/xma/decoder.cpp`
- `thirdparty/rexglue-sdk/src/audio/xma/xma_context_pool.cpp`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp`

Instructions:

- replace synchronous kick/wait assumptions
- move to runtime-owned state transitions
- preserve guest memory semantics exactly

### Phase E: Retire Hook Sprawl

Files:

- `src/ac6_audio_hooks.cpp`
- `src/ac6_audio_policy.cpp`

Instructions:

- remove diagnostics/workarounds that were only needed to discover the bug
- keep only title policy that still has evidence behind it

## Files That Likely Stay But Shrink

- `src/ac6_audio_hooks.cpp`
- `thirdparty/rexglue-sdk/src/audio/sdl/sdl_audio_driver.cpp`
- `thirdparty/rexglue-sdk/src/audio/host/null_audio_backend.cpp`

Expected end state:

- hooks: debug/compat only
- SDL: fallback only
- null backend: deterministic test mode only

## Files That Become Primary

- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/src/audio/audio_clock.cpp`
- `thirdparty/rexglue-sdk/src/audio/wasapi/wasapi_audio_driver.cpp`
- `thirdparty/rexglue-sdk/src/audio/xma/xma_context_pool.cpp`
- `src/ac6_audio_policy.cpp`

## Immediate Next Technical Fix

The current “game sped up” regression says the next fix is **not** another host driver swap.

The next fix is:

- rework runtime consumption/tic to be driven by actual host sample progress
- stop the new native sink from inheriting the old queue-depth callback contract unchanged

Concrete focus files:

- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/src/audio/audio_clock.cpp`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio.cpp`
- `thirdparty/rexglue-sdk/src/audio/wasapi/wasapi_audio_driver.cpp`

## Build Note

No build was run from this editing session.
User builds locally.
