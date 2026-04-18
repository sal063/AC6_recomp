Roadmap

- Milestone 1: Lock down capture analysis by preserving replay-shaped commands inside each observed pass, then expose counts in debug UI.
- Milestone 2: Introduce a backend-agnostic replay IR that converts pass commands into explicit draw/clear/resolve execution packets.
- Milestone 3: Implement the real D3D12 backend path first: device, queue, allocators, fences, frame slots, and present.
- Milestone 4: Add guest-to-host resource translation for RTs, depth, textures, vertex/index buffers, and fetch constants.
- Milestone 5: Add pipeline/shader translation and PSO caching, then target first visible native output from one selected pass.
- Milestone 6: Add parity validation mode, capture-based comparisons, and rollout gates for bootstrap -> scene_submission -> parity_validation -> shipping.

Completed

- Milestone 1 is complete.
- Milestone 2 is now in place at the data-model level.

Work Completed

- Added a backend-agnostic observed command model with `ObservedCommandType` and `ObservedCommandDesc` in `ac6_render_frontend.h`.
- Extended each observed pass to retain its ordered command list in `ac6_render_frontend.h`.
- Updated frontend capture processing to materialize per-command draw, clear, and resolve records while preserving pass grouping in `ac6_render_frontend.cpp`.
- Added `total_command_count` to the frontend summary so the runtime can report more than just pass counts.
- Wired the frontend summary into runtime status in `ac6_native_graphics.h` and `ac6_native_graphics.cpp`.
- Surfaced frontend pass/command counts in `ac6_native_graphics_overlay.cpp`.
- Added a new replay IR layer in `replay_ir.h` and `replay_ir.cpp`.
- Introduced `ReplayPassRole`, `ReplayCommandDesc`, `ReplayPassDesc`, `ReplayFrameSummary`, and `ReplayFrame`.
- Added `ReplayIrBuilder` so the renderer can build a replay frame from frontend passes plus the frame plan.
- Added a new execution-plan layer in `execution_plan.h` and `execution_plan.cpp`.
- Introduced `ExecutionCommandCategory`, `ExecutionCommandPacket`, `ExecutionResourceRequirements`, `ExecutionPassPacket`, `ExecutionFrameSummary`, and `ExecutionFramePlan`.
- Added `ExecutionPlanBuilder` so the renderer can derive backend-ready pass packets from `ReplayFrame` plus frame-plan hints.
- Added a new replay-executor layer in `replay_executor.h` and `replay_executor.cpp`.
- Introduced `SubmissionQueueType`, `ReplayExecutorCommandPacket`, `ReplayExecutorPassPacket`, `ReplayExecutorFrameSummary`, and `ReplayExecutorFrame`.
- Added `ReplayExecutorPlanBuilder` so the renderer can derive submission-oriented pass packets from `ExecutionFramePlan`.
- Updated `NativeRenderer` to build replay IR first, then execution plan, then replay-executor packets, then derive the current `RenderGraph` from executor passes.
- Exposed replay summary data through `ac6_native_graphics.h` and `ac6_native_graphics.cpp`.
- Exposed execution-plan summary data through `ac6_native_graphics.h` and `ac6_native_graphics.cpp`.
- Exposed replay-executor summary data through `ac6_native_graphics.h` and `ac6_native_graphics.cpp`.
- Surfaced replay, execution, and executor pass/command counts plus output-present state in `ac6_native_graphics_overlay.cpp`.
- Updated `CMakeLists.txt` to compile `replay_ir.cpp`, `execution_plan.cpp`, and `replay_executor.cpp`.

Why This Matters

- The renderer no longer stops at pass heuristics alone; it now carries replay IR, execution-plan, and executor artifacts forward.
- This creates the bridge between capture analysis and future backend execution without forcing full D3D12 command-list submission too early.
- The execution plan tracks stable per-pass resource requirements and command categories, while the replay executor now shapes queue-ready submission packets.
- The overlay now shows whether frontend analysis, replay IR, execution planning, and executor shaping stay aligned frame to frame.

Verification

- VS Code diagnostics are clean for the edited files.
- Full preset build verification is still blocked by an existing Ninja build-tree issue: `Re-checking globbed directories... ninja: fatal: GetOverlappedResult: The operation completed successfully.`
- A fresh scratch configure also cannot complete in the current terminal environment because no C++ compiler is available in `PATH`.
- I did not see source-level diagnostics from the replay-executor changes themselves.

Next Step

- Start consuming `ReplayExecutorFrame` in a real D3D12 submission path instead of only translating it back into `RenderGraph`.
- Add guest-to-host resource translation for executor packets: render targets, depth, textures, vertex/index buffers, and fetch constants.
- Add D3D12-side placeholders for PSO binding, descriptor setup, and barrier/state transitions while the executor contract stabilizes.
