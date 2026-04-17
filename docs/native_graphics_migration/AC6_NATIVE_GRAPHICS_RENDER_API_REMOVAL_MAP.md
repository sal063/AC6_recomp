# AC6 Native Graphics Render API Removal Map

Date: 2026-04-17  
Scope: AC6 render-path symbols, interfaces, and build linkage

Purpose: convert the migration strategy and audit into a concrete, file-referenced retirement map for legacy PM4/Xenos render interfaces.

Related docs:

- `AC6_NATIVE_GRAPHICS_EMULATION_AUDIT_2026-04-17.md`
- `AC6_NATIVE_GRAPHICS_EXECUTION_PLAN_2026-04-17.md`
- `AC6_NATIVE_GRAPHICS_API_DEPRECATION_TIMELINE.md`
- `AC6_NATIVE_GRAPHICS_CONFIG_MIGRATION_MATRIX.md`

## Map Rules

- This map is AC6-scope only; non-graphics kernel compatibility exports may remain.
- `M3` means hybrid migration and routing controls.
- `M4` means native present authority.
- `M5` means render linkage removal from AC6 targets.
- Every `remove` entry must have a native replacement and validation artifact before deletion.

## Removal Matrix

| Legacy API / symbol / module | Current references (primary files) | Native replacement | Action | Target milestone | Compatibility-shim notes | Required validation artifact |
| --- | --- | --- | --- | --- | --- | --- |
| `rex::graphics::GraphicsSystem` as render owner | `thirdparty/rexglue-sdk/include/rex/graphics/graphics_system.h`, `thirdparty/rexglue-sdk/src/graphics/graphics_system.cpp` | `ac6_native_renderer` ownership split across `RenderDevice`, `RenderGraph`, `FrameScheduler` | remove from AC6 render route | M5 | retain minimal interrupt/timing bridge only | boot + frame submission logs showing AC6 render no longer instantiates legacy graphics owner for production |
| `rex::graphics::CommandProcessor` PM4 execution core | `thirdparty/rexglue-sdk/include/rex/graphics/command_processor.h`, `thirdparty/rexglue-sdk/src/graphics/command_processor.cpp` | native render graph compilation and submission from `Ac6RenderFrontend` | remove | M5 | no PM4 parsing in production route | link report proving `command_processor*` objects excluded from AC6 shipping target |
| PM4 packet path including `PM4_XE_SWAP` | `thirdparty/rexglue-sdk/src/graphics/packet_disassembler.cpp`, `thirdparty/rexglue-sdk/src/graphics/command_processor.cpp` | native frame boundary and present path | remove | M5 | temporary parity hooks allowed pre-M5 only | CI check proving no production frame depends on PM4 swap synthesis |
| `VdSwap_entry` PM4 swap synthesis behavior | `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp` | `VdSwap` as native frame boundary only | narrow then remove PM4 synthesis | M4->M5 | keep interrupt contract if required by title timing | contract doc + canary telemetry confirming native present authority |
| D3D12 emulation command processor (`D3D12CommandProcessor`) | `thirdparty/rexglue-sdk/include/rex/graphics/d3d12/command_processor.h`, `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp` | `ac6_native_renderer/backends/d3d12_*` | remove from AC6 link | M5 | none for render; backend native module remains | object-link exclusion report for emulation D3D12 command processor |
| Vulkan emulation command processor (`VulkanCommandProcessor`) | `thirdparty/rexglue-sdk/include/rex/graphics/vulkan/command_processor.h`, `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp` | `ac6_native_renderer/backends/vulkan_*` | remove from AC6 link | M5 | none for render; backend native module remains | object-link exclusion report for emulation Vulkan command processor |
| Emulated shared-memory render model | `thirdparty/rexglue-sdk/src/graphics/shared_memory.cpp`, `thirdparty/rexglue-sdk/src/graphics/d3d12/shared_memory.cpp`, `thirdparty/rexglue-sdk/src/graphics/vulkan/shared_memory.cpp` | host-native resource allocator + transient/upload arenas | remove | M5 | none | memory validation showing native allocator path only |
| Texture cache + untiling runtime conversion | `thirdparty/rexglue-sdk/src/graphics/pipeline/texture/cache.cpp`, `thirdparty/rexglue-sdk/src/graphics/pipeline/texture/conversion.cpp`, backend texture caches | cooked texture packages + streaming manager | remove | M5 | dynamic texture bridge allowed during M3-M4 only | parity and perf reports showing no hot-path guest untiling |
| Render-target cache and EDRAM emulation | `thirdparty/rexglue-sdk/src/graphics/pipeline/render_target/cache.cpp`, backend render target caches | explicit native render graph attachments/history buffers | remove | M5 | none for production render route | pass graph captures showing native attachment ownership |
| Runtime shader translation (`dxbc`/`spirv` translators) | `thirdparty/rexglue-sdk/src/graphics/pipeline/shader/translator.cpp`, `dxbc_translator*.cpp`, `spirv_translator*.cpp` | authored HLSL + offline DXIL/SPIR-V/MSL pipeline | remove | M5 | dev-only diagnostics may remain out of production path | CI shader manifest proving no runtime translation on native path |
| Xenos register/render state ownership | `thirdparty/rexglue-sdk/src/graphics/registers.cpp`, `register_file.cpp`, `xenos.cpp` | native render/material descriptors | remove from render-authoritative path | M5 | limited compatibility reads may remain outside render | code-level ownership audit showing no render pass built from Xenos register file |
| Emulation trace writer / PM4-centric diagnostics | `thirdparty/rexglue-sdk/src/graphics/trace_writer.cpp` and trace protocol stack | native capture + pass timeline dumps + backend markers | dev-only then remove from production | M6 | keep dev capture tooling if isolated from shipping path | production package scan with no PM4/Xenos trace dependencies |

## Keep And Adapt (Not Removed)

| Component | Primary files | Native migration disposition |
| --- | --- | --- |
| presenter / window / provider | `thirdparty/rexglue-sdk/src/native/ui/presenter.cpp`, `thirdparty/rexglue-sdk/src/native/ui/d3d12/d3d12_presenter.cpp`, provider and window modules | keep and adapt to consume native final-color output |
| runtime graphics injection seam | `thirdparty/rexglue-sdk/src/native/ui/rex_app.cpp`, `thirdparty/rexglue-sdk/src/system/runtime.cpp`, `thirdparty/rexglue-sdk/include/rex/system/interfaces/graphics.h` | keep and expand for native renderer lifecycle, telemetry, and kill switches |
| AC6-local migration bridge and diagnostics | `src/ac6_native_graphics.cpp`, `src/ac6_native_graphics_overlay.cpp`, `src/ac6_native_renderer/*` | keep during migration; progressively reduce legacy bridge responsibilities |

## Build/CI Retirement Checks

At minimum, AC6 Phase 5 completion must include:

1. link artifact proof that AC6 shipping target excludes:
   - `command_processor` objects
   - shader translator objects
   - shared-memory render resources
   - guest-state texture and render-target caches
2. config linter enforcement from `AC6_NATIVE_GRAPHICS_CONFIG_MIGRATION_MATRIX.md` for removed/dev-only legacy keys
3. parity, perf, and memory gates green on benchmark matrix after legacy linkage removal

## Sequencing Notes

- `M3`: route subsystems via native/legacy toggles while parity hardens
- `M4`: transfer present authority; `VdSwap` reduced to boundary semantics
- `M5`: remove legacy render linkage from AC6 production
- `M6`: clean dead flags and PM4/Xenos-only diagnostic dependencies from production deliverables

## Open Items

- confirm final AC6 shipping target(s) that CI link-exclusion checks run against
- add exact object/library names once build graph emits deterministic link manifests
- tie each matrix row to issue IDs in the migration epic tracker
