# AC6 Audio Debug Progress

Updated: 2026-04-06

## Scope

This investigation is focused on AC6 movie/cutscene audio only:

- in-game audio is reported good
- malformed/distorted audio was isolated to:
  - in-engine cutscenes
  - pre-rendered cutscenes
  - briefings

## Phase Status

Based on `AUDIO_REWRITE_MASTER_PLAN.md`, current implementation is between Phase 1 and Phase 2:

- Phase 0: effectively in place
- Phase 1: queue-depth-driven runtime scheduling is in place
- Phase 2: still incomplete because render-driver tic semantics are still not fully guest-accurate
- Phase 3/4: XMA path needed fixes because AC6 exposed packet-boundary bugs

## Confirmed Findings So Far

### 1. Original major corruption issue was real XMA packet handling trouble

Earlier traces showed FFmpeg/XMA errors and cross-buffer packet misuse. That produced:

- random cutouts
- distortion
- delayed audio returns

The important XMA fix was correcting cross-buffer packet lookup so next-buffer packets use:

- `next_packet_index - current_input_packet_count`

instead of incorrectly always falling back to packet 0 in the next buffer.

This materially improved cutscene audio and removed the worst corruption.

### 2. Main-menu buzzing was a separate decoder stall

That was traced to a split-frame/end-of-buffer retry loop in XMA context handling. The decoder could get stuck re-reading an invalid next frame offset.

That was fixed by advancing/swapping buffers instead of retrying the same partial frame forever.

Result:

- main-menu buzzing stopped

### 3. Remaining cutscene issue is not host queue starvation

Repeated traces showed:

- SDL backend active
- `sample_frames_obtained=256`
- queue depth healthy, typically `2`
- no sustained underrun pattern causing the delay

So the remaining cutscene delay is not explained by SDL device buffering alone.

### 4. Current dominant symptom changed

The latest useful traces show:

- movie audio submission is active again
- the same sample buffer pointer is repeatedly submitted:
  - `samples=20431D00`
- submitted guest PCM is initially all zeros:
  - `guest_rms=0.000000`
  - `guest_zeroish_pct=100.00`

This means the current remaining issue is upstream of SDL playback:

- AC6 starts by feeding silent movie frames into the render-driver path
- or AC6 is reusing a silent preroll/staging buffer until its upstream worker/event path wakes up correctly

## Important Regressions Already Tested

### Forced sync dispatch on `worker_count=1`

This caused a hard regression:

- movie audio effectively disappeared
- `primary_submits` stayed stuck

That change was reverted.

### Narrower movie-audio-active sync forcing

That also regressed:

- audio stalled again

That change was reverted too.

Current state of `src/ac6_audio_hooks.cpp`:

- sync override only runs when `worker_count == 0`
- when `worker_count == 1`, guest worker dispatch is preserved

## Current Code Changes That Should Be Kept

### Policy / config

- `ac6_movie_audio_force_sync_dispatch` default is `true`
- build config currently forces:
  - `ac6_audio_deep_trace = true`
  - `ac6_movie_audio_force_sync_dispatch = true`

### XMA fixes / diagnostics

- deep XMA trace added in:
  - `thirdparty/rexglue-sdk/include/rex/audio/xma/context.h`
  - `thirdparty/rexglue-sdk/src/audio/xma/context.cpp`
- cross-buffer packet addressing fixed
- split-frame/end-of-buffer stall fixed

### Runtime / SDL latency reduction

- `audio_callback_low_water_frames = 1`
- `audio_callback_target_queue_depth = 2`
- SDL requested sample frames reduced to `256`
- SDL deep trace logs guest frame stats on submit

## Current Diagnosis

The original `rex_sub_823A6878` submit helper was compared against the generated guest function and is effectively identical in behavior. That means the current silent-frame problem is not coming from a bad rewrite of the submit hook itself.

Also, the helper `rex_sub_823A8648` is simple: it copies three words from the packet-provider object to the stack block used by `rex_sub_823A6878`, and the third word becomes the submitted sample pointer.

So the remaining problem is likely one of these:

1. the packet provider keeps pointing to a silent/stale buffer
2. the producer callback keeps leaving the packet block pointing at silence
3. AC6 is intentionally outputting silence because the render-driver timing/state it sees is still not what the movie path expects

## Latest Patch Added

Latest change is diagnostic only in:

- `src/ac6_audio_hooks.cpp`

New logging captures producer-side state in `rex_sub_823A6878`:

- packet provider pointer and base
- packet triple before producer callback
- packet triple after producer callback
- producer callback target
- pre-samples pointer
- post-samples pointer
- first 4 words of the pre/post sample buffer
- whether the same sample pointer was reused

The new log marker is:

- `AC6 producer snapshot ...`

## Next Step

Build locally and capture a fresh `ac6_audio_trace.log`.

The next thing to inspect is whether `AC6 producer snapshot ...` shows:

- the producer keeps reusing the same silent sample pointer
- the packet block changes but still points at zero-filled data
- the producer callback rewrites packet fields in a way that reveals the real intended buffer

## Latest Trace Result

The producer-side snapshot has now answered that question.

Observed in the earlier trace:

- producer callback is consistently `823A6688`
- packet provider stays fixed:
  - `provider=20431C8C/20431C84`
- packet contents do not change across the callback:
  - `packet_pre=[00060000 0000BB80 20431D00]`
  - `packet_post=[00060000 0000BB80 20431D00]`
- the sample pointer is reused every time:
  - `reused_samples=true`
- first words of the buffer are zero before and after callback:
  - `pre_head=[00000000 00000000 00000000 00000000]`
  - `post_head=[00000000 00000000 00000000 00000000]`

This meant:

- the producer callback is not selecting a different audio buffer
- the packet block is not being rewritten to valid PCM later in `rex_sub_823A6878`
- the source buffer is already silent before submit

So the remaining bug was upstream of render-driver submission:

- either the movie-audio buffer is never being filled
- or the worker/update path that should fill it is not running at the right time

## Newest Trace Result

The newest trace changed that conclusion in an important way.

Observed in `ac6_audio_trace.log` from the latest run:

- movie audio still registers immediately
- the async cutscene path is still used:
  - `worker_count=1`
  - `sync_override_bypassed=true`
- the movie sample buffer starts out silent for several seconds
- but the producer snapshots later show the same buffer `20431D00` becoming nonzero

Representative transition:

- early snapshots:
  - `pre_head=[00000000 00000000 00000000 00000000]`
- later snapshots:
  - `pre_head=[2CB07B5B 2D307B5B ...]`
  - many later snapshots continue with changing nonzero sample words

This means:

- the movie path is not permanently copying silence anymore
- the same producer buffer eventually fills with real PCM
- the remaining cutscene delay is now much more strongly tied to upstream scheduling / synchronization than to render-driver submit itself

## New Kernel Finding

The same trace also shows repeated kernel warnings later in the run:

- `[KeSetEvent] event pointer 0000000182916E3C missing native XEvent`

Relevant code path:

- `KeInitializeEvent` creates native events via `XObject::GetNativeObject<XEvent>(..., event_ptr, event_type)`
- `KeSetEvent` later looks them up via `XObject::GetNativeObject<XEvent>(..., event_ptr)`

The generic native-object lookup previously did this:

- if the guest dispatch header still had the XObject signature, it trusted the stashed handle
- if that handle no longer existed in the native object table, it returned `nullptr`
- later kernel event operations then stayed broken on that stale mapping

This is now patched in:

- `thirdparty/rexglue-sdk/src/system/xobject.cpp`

Current behavior:

- stale signed dispatch headers whose handles no longer resolve are treated as stale mappings
- the stale signature/handle is cleared
- the native object is rebuilt from the live guest dispatch header
- the rebuilt handle is stashed back into the guest header

Expected effect of this patch:

- later `KeSetEvent` / `KeWait*` calls can recover instead of repeatedly hitting a dead native-event mapping
- this may reduce or eliminate movie worker wakeup failures that still leave cutscene audio starting late

## Follow-Up Result After Stale-Mapping Recovery

The next trace showed:

- repeated `missing native XEvent` warnings are gone
- a few stale mapping recoveries are now logged later instead:
  - `native_ptr=82916E3C`
  - `native_ptr=82916E2C`
  - `native_ptr=82916E08`

So the stale-event recovery path is working as intended.

However, it did **not** materially improve the initial cutscene-audio delay:

- movie audio register:
  - `2026-04-06 16:14:17.571`
- first clearly nonzero producer snapshot:
  - `2026-04-06 16:14:24.482`
- elapsed:
  - about `6.9s`

That is still roughly the same delayed start window as before.

## Current Best Lead

The async guest worker path is still active the whole time:

- `worker_count=1`
- `sync_override_bypassed=true`

And the original guest code for `rex_sub_823AD9C0` shows that when `worker_count != 0`, it:

- calls `KeSetEvent` on `0x82916E3C`
- waits on two events:
  - `0x82916E2C`
  - `0x82916E08`

So the next diagnostic target is not SDL and not the producer submit helper. It is the result of that specific `KeWaitForMultipleObjects` site in the AC6 movie worker path.

## Latest Patch Added

Added narrow AC6-specific kernel wait tracing in:

- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_threading.cpp`

This logs, under deep trace:

- `KeSetEvent` on the AC6 movie worker event `0x82916E3C`
- `KeWaitForMultipleObjects` result for the AC6 two-event wait:
  - `0x82916E2C`
  - `0x82916E08`

This is meant to reveal whether the delayed audio start is really the guest worker waiting on the wrong object/result for those first ~6.9 seconds.

## Latest Trace Result After Kernel Wait Tracing

The new trace answers that question clearly.

Observed:

- the async movie worker path is still active:
  - `worker_count=1`
  - `sync_override_bypassed=true`
- every traced worker wakeup does:
  - `KeSetEvent(0x82916E3C, 1, 0)`
  - `KeWaitForMultipleObjects([0x82916E2C, 0x82916E08], ...)`
- the wait result is consistently:
  - `result=0`

Interpretation:

- the movie worker is not hanging in a bad wait state
- it is not timing out
- it is not waking on the wrong status code
- the worker/event path is behaving normally during the delayed-start window

The same trace also shows that the first nonzero producer buffer now appears much earlier than in the previous run:

- movie audio register:
  - `2026-04-06 16:23:43.407`
- first clearly nonzero producer snapshot:
  - `2026-04-06 16:23:46.905`
- elapsed:
  - about `3.5s`

So compared to the earlier ~`6.9s` delayed fill, this run is materially better, but still late.

Also important:

- there are no `missing native XEvent` warnings during startup / early playback
- stale mapping recovery only appears much later at shutdown/teardown time:
  - `16:25:33.049`

This means the stale-event recovery patch fixed a real bug, but that bug is no longer the main explanation for the remaining startup delay.

## Updated Best Lead

At this point:

- SDL/output queueing is healthy
- the async worker wait path is healthy
- producer buffers do eventually fill with real PCM
- but the guest still spends about `3.5s` submitting silent movie buffers before that happens

So the remaining delay is most likely due to guest-side movie timing / preroll / clock semantics rather than broken worker signaling.

That points back toward the still-unfinished Phase 2 area:

- render-driver tic / guest-consumption timing semantics

The most useful next diagnostic target is no longer the event path. It is the guest timing state that AC6 sees while it decides whether to keep outputting silence or start filling the movie buffer.

## Latest Patch Added

Added read-only timing snapshot instrumentation for the render-driver tic path:

- `thirdparty/rexglue-sdk/include/rex/audio/audio_runtime.h`
- `thirdparty/rexglue-sdk/include/rex/audio/audio_system.h`
- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/src/audio/audio_system.cpp`
- `thirdparty/rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_audio.cpp`

New runtime timing snapshot fields:

- `consumed_samples`
- `consumed_frames`
- `callback_dispatch_count`
- `callback_floor_tic`
- `host_elapsed_tic`
- `render_driver_tic`

`XAudioGetRenderDriverTic` now logs these values during early startup / low-tic windows so the next trace can show exactly what AC6 sees from the render-driver clock while it is still outputting silent movie buffers.

## Follow-Up Result After Render-Driver Tic Instrumentation

The newest trace showed an important negative result:

- there are no `XAudioGetRenderDriverTic ...` lines at all
- there are no logs containing:
  - `internal_tic`
  - `callback_floor_tic`
  - `host_elapsed_tic`
  - `clock_samples`
  - `clock_frames`

Generated-code inspection also confirms that the active movie path in `ac6recomp_recomp.28.cpp` uses:

- `XAudioRegisterRenderDriverClient`
- `XAudioSubmitRenderDriverFrame`
- `XAudioUnregisterRenderDriverClient`

but does **not** call `XAudioGetRenderDriverTic` in this path.

That means the remaining preroll delay is **not** being driven by the kernel render-driver tic export.

## Latest Trace Result

Observed in the newest trace:

- movie audio register:
  - `2026-04-06 16:30:15.754`
- async worker path still healthy:
  - repeated `KeSetEvent(0x82916E3C, 1, 0)`
  - repeated `KeWaitForMultipleObjects([0x82916E2C, 0x82916E08], ...)`
  - wait result remains `0`
- no startup `missing native XEvent` failures
- producer buffer `20431D00` remains zero initially
- first clearly nonzero producer snapshot appears around:
  - `2026-04-06 16:30:19.678`

Elapsed delayed-fill window in this run:

- about `3.9s`

So the current state is:

- much better than the earlier ~`6.9s` fill delay
- but still clearly delayed
- and still not explained by worker waits or render-driver queueing

## Updated Best Lead

The remaining issue now points more strongly at AC6's own guest-side pacing gate than at kernel audio exports.

Relevant generated-code lead in `ac6recomp_recomp.28.cpp`:

- `rex_sub_823AED88`
- `rex_sub_823AEE50`
- a loop around `mftb` + `KeQueryPerformanceFrequency`

Behavior of that code:

- `rex_sub_823AED88` repeatedly calls `rex_sub_823AEE50`
- if readiness is not reached quickly enough, it takes a timeout/fallback path

That is now the most likely place where AC6 is deciding to keep outputting silent movie buffers for the first few seconds.

## Latest Patch Added

Added guest-side pacing diagnostics in:

- `src/ac6_audio_hooks.cpp`

New hook coverage:

- `rex_sub_823AEE50`
- `rex_sub_823AED88`

New log markers:

- `AC6 movie gate poll ...`
- `AC6 movie gate wait ...`

These logs capture:

- gate pointer
- slot count / flags before and after
- first slot cookie/header words
- `rex_sub_823AEE50` return value
- `rex_sub_823AED88` result and host-side elapsed time

This should show whether AC6 is spending the remaining preroll in:

1. repeated not-ready polling
2. repeated short timed waits
3. a fallback/timeout path in the guest movie gate

## Follow-Up Result After Guest Gate Tracing

This trace narrowed the problem again.

Observed:

- movie audio register:
  - `2026-04-06 16:38:33.385`
- first `AC6 movie gate poll ...`:
  - `2026-04-06 16:38:37.122`
- first `AC6 movie gate wait ...`:
  - `2026-04-06 16:38:37.122`
- first clearly nonzero producer snapshot:
  - `2026-04-06 16:38:37.171`

Implication:

- the guest movie gate is only entered about `3.7s` after movie-audio registration
- once the gate path starts running, it reaches nonzero PCM in about `49 ms`

So the gate itself is **not** where the multi-second delay is spent.

### What the gate logs showed

- `AC6 movie gate poll ...` returns `result=1`
- `AC6 movie gate wait ...` returns `result=0`
- gate wait elapsed time is tiny:
  - about `0.13 ms` to `0.21 ms`
- flags frequently toggle:
  - `00050000 -> 00070000`
- slot/header words change as the gate cycles:
  - `07B0003F`
  - `C7B0003F`
  - `97B0003F`
  - `67B0003F`
  - `37B0003F`

Interpretation:

- there is no multi-second stall inside `rex_sub_823AED88`
- there is no meaningful long sleep/timeout inside this gate loop
- once AC6 reaches the gate, it progresses essentially immediately

## Updated Best Lead

The remaining delay is now most likely **before** the guest movie gate:

- either AC6 is not calling into the gate path for the first ~`3.7s`
- or a tiny readiness check immediately before it stays false until then

Most relevant immediate predecessors:

- `rex_sub_823AE3B8`
- `rex_sub_823AED08`

`rex_sub_823AE3B8` is especially important because it is just a flag-bit readiness test. If that stays `0` for the preroll window, it likely explains why the full gate path is reached late.

## Latest Patch Added

Added narrower pre-gate diagnostics in:

- `src/ac6_audio_hooks.cpp`

New log markers:

- `AC6 movie gate ready ...`
- `AC6 movie gate arm ...`

These capture:

- gate pointer
- flags before/after
- slot count
- helper return value
- caller LR

This should tell us whether AC6 is:

1. repeatedly checking a not-ready flag for ~`3.7s`
2. only entering the gate path late from a higher-level caller

## Follow-Up Result After Pre-Gate Tracing

This trace narrowed the delay one layer higher again.

Observed:

- movie audio register:
  - `2026-04-06 16:43:39.007`
- first `AC6 movie gate ready ...`:
  - `2026-04-06 16:43:43.120`
- first `AC6 movie gate arm ...`:
  - `2026-04-06 16:43:43.120`
- first clearly nonzero producer snapshot:
  - `2026-04-06 16:43:43.169`

Implication:

- the pre-gate helpers are only reached about `4.11s` after movie registration
- once they are reached, real PCM follows about `49 ms` later

So neither `rex_sub_823AE3B8` nor `rex_sub_823AED08` is consuming the multi-second delay.

### What the pre-gate logs showed

- `AC6 movie gate ready ...` is already returning `result=1` on first sight
- `AC6 movie gate arm ...` is returning `result=0`
- the observed callers are:
  - `caller_lr=823B3C18`
  - `caller_lr=823B3C28`
  - `caller_lr=823B45B8`

Generated-code inspection shows those callers are inside `rex_sub_823B3BA8`.

That means the new front edge of the delay is no longer the gate helpers. It is the point where AC6 first enters `rex_sub_823B3BA8`.

## Updated Best Lead

The remaining preroll is now most likely before or at entry to `rex_sub_823B3BA8`.

That function:

- raises IRQL
- manages a small spin-lock/refcount block at `state+296`
- drives the gate-ready / arm / wait sequence

Since the first entry into its inner gate logic is already late, the next question is:

1. does AC6 enter `rex_sub_823B3BA8` late
2. or does it enter earlier with different state and only reach the gate-driving branch later

## Latest Patch Added

Added a higher-level entry trace in:

- `src/ac6_audio_hooks.cpp`

New log marker:

- `AC6 movie gate driver entry ...`

This captures at `rex_sub_823B3BA8` entry:

- movie state pointer
- gate pointer
- arg4 / arg5
- voice count
- state byte at `+292`
- lock/refcount state at `+296`
- queue pointers/counters near `+200`
- gate flags
- caller LR

This should reveal whether the whole gate-driving block itself is being reached late, and what movie-state flags exist at that moment.

## Follow-Up Result After Gate-Driver Entry Tracing

This trace moved the delay boundary one more step earlier.

Observed:

- movie audio register:
  - `2026-04-06 16:48:16.726`
- first `AC6 movie gate driver entry ...`:
  - `2026-04-06 16:48:20.024`
- first clearly nonzero producer snapshot:
  - `2026-04-06 16:48:20.094`

Implication:

- the whole `rex_sub_823B3BA8` gate-driver block is only entered about `3.30s` after registration
- once it starts running, real PCM follows in about `70 ms`

So the multi-second delay is not inside:

- `rex_sub_823B3BA8`
- `rex_sub_823AE3B8`
- `rex_sub_823AED08`
- `rex_sub_823AED88`
- `rex_sub_823AEE50`

### What the new entry logs showed

The repeated driver-entry state is very stable:

- `caller_lr=823B9810`
- `voice_count=3`
- `state_292=1`
- `lock_count=0`
- `lock_owner=00000000`
- `queue_count=1`
- `gate` stays `A1910000`
- `arg5` stays `2042E258`

So the next front edge is the caller behind `823B9810`.

Generated-code inspection shows `823B9810` is the return site of an indirect call inside:

- `rex_sub_823B9700`

That means the remaining delay is now most likely before or at entry to `rex_sub_823B9700`.

## Latest Patch Added

Added a new higher-level trace in:

- `src/ac6_audio_hooks.cpp`

New log marker:

- `AC6 movie gate indirect entry ...`

This captures at `rex_sub_823B9700` entry:

- object pointer
- descriptor pointer
- caller LR
- first four words of the object
- first four words of the descriptor

That should show what object/descriptor pair is feeding the late indirect call into the movie gate driver and whether this upstream stage is the actual delayed edge.

## Follow-Up Result After Indirect-Entry Tracing

This trace invalidated the previous "late indirect-entry" hypothesis.

Observed:

- movie audio register:
  - `2026-04-06 16:52:29.522`
- first `AC6 movie gate indirect entry ...`:
  - `2026-04-06 16:52:29.523`
- first clearly nonzero producer snapshot:
  - much later, around `2026-04-06 16:52:36.684`

Implication:

- `rex_sub_823B9700` is **not** being reached late
- it is already active immediately at movie registration
- the delayed-start window now sits inside or after that indirect-call stage

### What the indirect-entry logs showed

The object/descriptor inputs are stable and begin immediately:

- `obj=20431C3C`
- `caller_lr=823B9F5C`
- alternating descriptors:
  - `desc=20433528`
  - `desc=20433530`

Representative object head:

- `obj_head=[8204DCB0 00000001 20431C20 02000000]`

Representative descriptor heads:

- `desc_head=[20433538 00010100 20433588 01010100]`
- `desc_head=[20433588 01010100 82017C30 00000000]`

So the next useful question is no longer "when does `rex_sub_823B9700` start?" It is:

1. what result does `rex_sub_823B9700` return during the silent preroll
2. do the descriptor flags/targets mutate before the delayed audio actually appears

## Updated Best Lead

The remaining delay is now most likely tied to the result or branch behavior of the indirect call inside `rex_sub_823B9700`, or to what those descriptors point at.

`caller_lr=823B9F5C` identifies the caller as `rex_sub_823B9F10`, which loops over descriptor entries and repeatedly calls `rex_sub_823B9700`.

That means the current front edge of the bug is:

- the descriptor-driven indirect path under `rex_sub_823B9F10`

## Latest Patch Added

Extended the `rex_sub_823B9700` hook in:

- `src/ac6_audio_hooks.cpp`

New data added:

- descriptor byte 5 / byte 6
- descriptor target word
- post-call return value
- post-call descriptor bytes/words when they change

New log marker:

- `AC6 movie gate indirect result ...`

This should show whether the silent preroll corresponds to:

1. repeated zero/negative returns from the indirect path
2. a descriptor flag transition before the movie gate finally starts

## Follow-Up Result After Indirect-Result Tracing

The latest trace narrowed this further.

### What the trace showed

The original movie path starts producing nonzero PCM well before any logged successful
`AC6 movie gate indirect result ...` lines appear.

Observed:

- first clearly nonzero movie producer snapshot:
  - `2026-04-06 16:57:35.393`
- representative original movie path during preroll:
  - `obj=20431C3C`
  - `desc=20433528` / `20433530`
  - `caller_lr=823B9F5C`
  - `desc_b5=01`
  - `desc_b6=01`
- first logged indirect success result:
  - much later, `2026-04-06 16:58:06.432`
  - but for a different object:
    - `obj=207F403C`
    - `desc=207F4088`
    - `desc_b6_before=04`
    - `result=1`

### New conclusion

This means the current success-only result trace is **not** capturing the original delayed movie
path.

The most likely interpretation is:

- the original movie descriptors under `823B9F10 -> 823B9700`
- are returning `0`
- without mutating descriptor state
- while the silent preroll is happening

So the next useful signal is the **zero-return** path for:

- `caller_lr=823B9F5C`
- `desc_b5=01`
- `desc_b6=01`

## Latest Patch Added

Extended the `rex_sub_823B9700` result hook again in:

- `src/ac6_audio_hooks.cpp`

New behavior:

- periodic tracing now also logs zero-return cases for the original movie path
- only for the narrow descriptor/caller combination above
- still sampled to avoid flooding the log

Updated log marker:

- `AC6 movie gate indirect result ... caller_lr=... result=0 periodic_zero=true ...`

This should confirm whether the delayed-start window is simply the original movie descriptors
spinning on stable zero returns before a later upstream state transition fills the PCM buffer.

## Follow-Up Result After Zero-Return Tracing

This trace invalidated that lead.

### What the trace showed

The original movie path does in fact return stable zero results exactly as expected:

- `obj=20431C3C`
- `desc=20433528` / `20433530`
- `caller_lr=823B9F5C`
- `result=0`
- `periodic_zero=true`
- descriptor bytes and descriptor head remain unchanged

Representative early lines:

- `2026-04-06 17:03:22.338`
- `2026-04-06 17:03:22.338`

At the same time, the producer snapshot is still silent:

- `pre_head=[00000000 00000000 00000000 00000000]`

However, much later in the same run the producer buffer becomes clearly nonzero while the same
movie worker path is still healthy:

- later producer snapshots around `2026-04-06 17:03:31.638` and after are nonzero
- `KeWaitForMultipleObjects` continues returning `0`
- a sampled zero-result line for the same original movie object still appears at:
  - `2026-04-06 17:03:31.638`

### New conclusion

That means:

- the `823B9F10 -> 823B9700` zero-return path is real
- but it is **not** the gating cause of the silent preroll
- because the movie PCM buffer can already contain real audio while that path is still returning
  zero with no descriptor mutation

So this descriptor-driven indirect path is now likely orthogonal or secondary to the actual delayed
buffer-fill path.

## Updated Best Lead

The remaining delay is now most likely in the producer/fill side for the reused movie PCM buffer:

- buffer pointer remains fixed:
  - `20431D00`
- provider remains fixed:
  - `20431C8C/20431C84`
- producer callback remains fixed:
  - `823A6688`
- buffer content changes from all-zero to real PCM later without any packet/block rewrite

So the next target should move back to the producer/fill chain that writes into `20431D00`, rather
than continuing to chase the `823B9700` indirect descriptor path.

## Latest Patch Added

Added a zero-to-nonzero transition snapshot in:

- `src/ac6_audio_hooks.cpp`

New behavior:

- when the reused movie sample buffer flips from all-zero to nonzero
- capture a wider snapshot of:
  - owner state at offsets `0..28`
  - provider-base state at offsets `0..28`
  - current driver pointer
  - producer callback pointer
  - sample pointer

New log marker:

- `AC6 producer transition ... zero_to_nonzero=true ...`

Purpose:

- identify what owner/provider state changes at the exact moment `20431D00` stops being silence
- keep the next diagnostic focused on the real delayed buffer-fill path

## Follow-Up Result After Producer-Transition Tracing

The transition log did fire and confirmed one more important point.

Observed in the latest trace:

- movie registration:
  - `2026-04-06 17:14:06.258`
- first zero-to-nonzero transition for the reused movie buffer:
  - `2026-04-06 17:14:11.081`
- delay from registration to first nonzero movie buffer:
  - about `4.8s`

Transition snapshot content was stable:

- `owner=20433EBC`
- `driver=41550000`
- `provider=20431C8C/20431C84`
- `producer=823A6688`
- `samples=20431D00`
- `owner_head=[8204C970 8204C954 00000001 20431C3C]`
- `owner_tail=[00000000 00000000 41550000 00000000]`
- `provider_head=[8204CA80 00000001 00000000 00060000]`
- `provider_packet=[0000BB80 00060000 0000BB80 20431D00]`

The same snapshot repeats on later zero-to-nonzero transitions as well.

### New conclusion

That means the immediate owner/provider packet state still does **not** change when the movie
buffer wakes up. The delayed fill is happening behind that stable shell.

So the next useful correlation is no longer owner/provider fields alone. It is:

- the movie singleton state driving the async worker path
- especially its primary/extra packet descriptors and worker/state bytes
- captured at the exact moment the producer buffer flips from zero to nonzero

## Latest Patch Added

Extended the same `AC6 producer transition ...` snapshot again in:

- `src/ac6_audio_hooks.cpp`

New data added:

- last seen movie singleton pointer from `rex_sub_823AD9C0`
- singleton state byte at `+292`
- singleton worker count at `+304`
- singleton primary packet triple at `+80`
- singleton extra packet triple at `+124`

This keeps the next trace on the real hot path without adding another heavy standalone hook.

## Follow-Up Result After Singleton-Transition Tracing

The latest trace gave a stronger correlation.

Observed:

- movie registration:
  - `2026-04-06 17:21:44.269`
- first zero-to-nonzero movie-buffer transition:
  - `2026-04-06 17:21:50.594`
- delayed-start window in this run:
  - about `6.3s`

Transition snapshot now includes singleton state:

- `singleton=2042DC3C`
- `state_292=00`
- `worker_count=1`
- `primary=[2076E1CC 207F304C 00000010]`
- `extra=[2042DDB4 03000000 3F800000]`

Later wakeups repeat the same shape with only the second word of the primary triple changing:

- `207F444C`
- `207F384C`
- `207F3BCC`
- `207F35CC`

while:

- `state_292` stays `00`
- `worker_count` stays `1`
- the extra triple stays fixed
- owner/provider state stays fixed

### New conclusion

The movie buffer wakeup now correlates most strongly with changes inside the singleton primary
packet triple, not with:

- render-driver registration
- provider shell state
- descriptor path `823B9F10 -> 823B9700`
- worker event health

So the next useful comparison is:

- the sampled singleton primary/extra triples during the silent period
- versus the sampled singleton primary/extra triples around wakeup

## Latest Patch Added

Adjusted the existing `worker_count=1` log in:

- `src/ac6_audio_hooks.cpp`

New behavior:

- when deep trace is enabled, sampled `AC6 cutscene audio keeping guest worker dispatch ...`
  lines now also include:
  - `state_292`
  - singleton primary triple
  - singleton extra triple

This reuses the already-confirmed hot path instead of adding another separate hook.

## Follow-Up Result After Sampled Worker-Dispatch Tracing

This trace gave the clearest before/after comparison yet.

Observed:

- movie registration:
  - `2026-04-06 17:26:42.204`
- first sampled silent-period worker dispatch:
  - `2026-04-06 17:26:42.203`
- first zero-to-nonzero producer transition:
  - `2026-04-06 17:26:46.641`
- delayed-start window in this run:
  - about `4.4s`

### Silent-period singleton state

At the start of the silent window, sampled worker-dispatch lines show:

- `singleton=2042DC3C`
- `state_292=00`
- `worker_count=1`
- `primary=[2042DC8C 2042DC8C 00000010]`
- `extra=[2042DDB4 03000000 3F800000]`

Then, still during the silent window, the primary triple changes to:

- `primary=[2076E1CC 207D13CC 00000010]`
- then quickly to:
  - `primary=[2076E1CC 207F0CCC 00000010]`

That `207F0CCC` state remains stable across many sampled silent-period lines.

### Wakeup-period singleton state

At the first producer wakeup:

- `2026-04-06 17:26:46.641`
- `primary=[2076E1CC 207F304C 00000010]`

So the strongest observed correlation is now:

- silent period:
  - `primary[1] = 207F0CCC`
- wakeup:
  - `primary[1] = 207F304C`

The difference is:

- `0x2380`

### New conclusion

This is the most concrete state boundary so far.

The delayed movie wakeup is now best explained by a higher-level primary-slot/frame-block advance in
the singleton worker path, not by:

- provider packet mutation
- render-driver submit behavior
- XMA descriptor path `823B9F10 -> 823B9700`
- worker event failures

## Latest Patch Added

Extended the sampled worker-dispatch and producer-transition logs in:

- `src/ac6_audio_hooks.cpp`

New data added:

- dereferenced heads of `primary[0]`
- dereferenced heads of `primary[1]`

New fields:

- `primary0_head=[...]`
- `primary1_head=[...]`

This should identify what the `207F0CCC -> 207F304C` primary-slot advance actually points to.

## Follow-Up Result After Primary-Head Tracing

The new log tightened the boundary again.

Observed in `ac6_audio_trace.log`:

- movie registration:
  - `2026-04-06 17:32:32.714`
- first stable silent-period movie worker state:
  - `primary=[2076E1CC 207F0CCC 00000010]`
  - `primary0_head=[20777DCC 2042DC8C 2042DC98/2042DCA4 ...]`
  - `primary1_head=[2042DC8C 207E8E8C 207F0CD4 207F0CD4]`
- first producer wakeup:
  - `2026-04-06 17:34:16.888`
  - `primary=[2076E1CC 207F304C 00000010]`
  - `primary0_head=[20777DCC 2042DC8C 207F3054 2042DC98]`
  - `primary1_head=[2042DC8C 207F0CCC 2042DC98 2076E1D4]`

### New conclusion

This is more specific than the earlier `primary[1]` correlation.

At wakeup:

- `primary[1]` advances from the silent block to the live block
- `primary0_head` starts pointing into the new block (`207F3054`)
- `primary1_head` still points back toward the old silent-period block (`207F0CCC`)

So the delayed-start boundary now looks like a primary-slot handoff / publish step in the movie
worker path, not just a single pointer flip in the provider shell.

That keeps the root cause narrowed to the singleton worker helpers, most likely the pair called
from `rex_sub_823AD9C0`:

- `rex_sub_823AD0D8`
- `rex_sub_823AD1C0`

## Latest Patch Added

Added direct tracing around the worker helpers in:

- `src/ac6_audio_hooks.cpp`

New log markers:

- `AC6 movie worker prepare ...`
- `AC6 movie worker finalize ...`

These capture before/after singleton state across:

- `state_292`
- primary triple
- extra triple
- dereferenced heads of `primary[0]`
- dereferenced heads of `primary[1]`

The next log should show which helper actually advances the singleton primary slot from the
silent block to the live block.

## Follow-Up Result After Direct `823AD0D8` / `823AD1C0` Tracing

The new log rules both helpers out as the actual publish step.

Observed in `ac6_audio_trace.log`:

- movie registration:
  - `2026-04-06 17:39:22.464`
- first producer wakeup:
  - `2026-04-06 17:39:29.898`
- delayed-start window in this run:
  - about `7.4s`

### What the helper traces show

`AC6 movie worker prepare ...`:

- leaves the singleton primary triple unchanged
- no `207F0CCC -> 207F304C` publish was observed here

`AC6 movie worker finalize ...`:

- also leaves the singleton primary triple unchanged
- during the silent period it only flips a small ping-pong field inside `primary0_head`
  between:
  - `2042DC98`
  - `2042DCA4`
- `primary1_head` stays on the older silent-path block while this happens

### New conclusion

This means the movie-slot publish is happening after `rex_sub_823AD1C0`, not inside:

- `rex_sub_823AD0D8`
- `rex_sub_823AD1C0`

The next likely candidates are the post-helper calls in the same `rex_sub_823AD9C0` flow:

- the indirect vfunc call on `singleton->64`
- `rex_sub_823B0DB8`
- `rex_sub_823B0DF0`

## Latest Patch Added

Added direct tracing around the next post-helper calls in:

- `src/ac6_audio_hooks.cpp`

New log markers:

- `AC6 movie worker post ...`
- `AC6 movie worker flush ...`

These capture before/after singleton state around:

- `rex_sub_823B0DB8`
- `rex_sub_823B0DF0`

The next log should tell us whether the publish happens in one of those calls or whether the
remaining boundary is the indirect vfunc immediately before them.

## Follow-Up Result After Direct `823B0DB8` / `823B0DF0` Tracing

The new log rules those out too.

Observed in `ac6_audio_trace.log`:

- movie registration:
  - `2026-04-06 17:44:54.926`
- first producer wakeup:
  - `2026-04-06 17:46:45.438`
- delayed-start window in this run:
  - about `110.5s`

### What the post-helper traces show

`AC6 movie worker post ...`:

- leaves the singleton primary triple unchanged
- no `207F0CCC -> 207F304C` publish happens there

`AC6 movie worker flush ...`:

- also leaves the singleton primary triple unchanged
- no publish happens there either

Even immediately before and after wakeup, those logs still show the primary triple unchanged across
their own call boundaries.

### New conclusion

That leaves one concrete remaining boundary in the `rex_sub_823AD9C0` worker path:

- the indirect vfunc call on `singleton->64` / vtable slot `68`

At this point the publish step is no longer somewhere vague in the worker tail. It is now narrowed
to that indirect call unless there is an untraced side effect hidden entirely outside the sampled
window, which is now unlikely.

## Practical Status

The tracing phase is effectively at the last boundary:

- `rex_sub_823AD0D8`: not the publish
- `rex_sub_823AD1C0`: not the publish
- `rex_sub_823B0DB8`: not the publish
- `rex_sub_823B0DF0`: not the publish
- remaining candidate:
  - the indirect vfunc call reached from `singleton->64`

## Latest Patch Added

Reduced the tracing to one final breadcrumb in:

- `src/ac6_audio_hooks.cpp`

New log marker:

- `AC6 movie worker async vfunc ...`

This logs only:

- singleton pointer
- object pointer at `singleton + 64`
- vtable pointer
- indirect target function pointer at vtable slot `68`

Purpose:

- identify the exact async vfunc target
- wrap that one function directly next
- avoid adding wider logging to the worker path again

## Latest Trace Result

The current `ac6_audio_trace.log` answered that breadcrumb cleanly.

Observed very early and then repeatedly through the delayed-start window:

- `AC6 movie worker async vfunc singleton=2042DC3C obj=20431C3C vtable=8204DCB0 target=823B7EC8`

This target is stable both:

- during the silent preroll
- after the movie buffer transitions to real PCM

That means the remaining publish boundary is no longer hypothetical. The exact async worker target is:

- `rex_sub_823B7EC8`

The same run also keeps the earlier primary-slot pattern:

- silent period:
  - `primary=[2076E1CC 207F364C 00000010]`
- first zero-to-nonzero wakeup:
  - `primary=[2076E1CC 207F304C 00000010]`

So the next trace should not stay on the outer `823AD9C0` breadcrumb. It should wrap `823B7EC8`
directly and log whether *that* call is the one that mutates the singleton primary triple or the
object state it consumes.

## Latest Patch Added

Added one direct wrapper in:

- `src/ac6_audio_hooks.cpp`

New log marker:

- `AC6 movie worker publish ...`

This wraps `rex_sub_823B7EC8` directly and logs:

- object pointer
- singleton pointer
- caller LR
- return value
- object head words before/after
- object `+32` word before/after
- singleton primary triple before/after
- dereferenced `primary0_head` / `primary1_head` before/after

Purpose:

- confirm whether `823B7EC8` is the actual publish boundary for the delayed movie-audio wakeup
- keep the next run scoped to the final remaining async worker function

## Latest Trace Result

The newest trace confirms that `823B7EC8` is the real publish boundary.

Key lines:

- early no-op publish calls:
  - `AC6 movie worker publish ... result=0 ... primary_before=[2042DC8C 2042DC8C 00000010] primary_after=[2042DC8C 2042DC8C 00000010]`
- first publish into the silent preroll block:
  - `primary_before=[2076E1CC 207A9C8C 00000010]`
  - `primary_after=[2076E1CC 207F0CCC 00000010]`
- final wakeup publish:
  - `primary_before=[2076E1CC 207F0CCC 00000010]`
  - `primary_after=[2076E1CC 207F304C 00000010]`
  - at the same point the movie buffer transitions to nonzero PCM in `AC6 producer transition ...`

Important detail:

- `823B7EC8` returns `0` the whole time
- object state logged around it stays effectively unchanged:
  - `obj_head_before == obj_head_after`
  - `obj32_before == obj32_after`

So the delayed-start publish is happening **inside** `823B7EC8`, but not at its outer visible object
surface. The flip is caused by one of its two internal calls:

- `rex_sub_823B9158`
- `rex_sub_823A8910`

## Latest Patch Added

Split the final worker publish path one level deeper in:

- `src/ac6_audio_hooks.cpp`

New log markers:

- `AC6 movie worker publish-step1 ...`
- `AC6 movie worker publish-step2 ...`

These are caller-LR gated so they only trace when reached from `823B7EC8`:

- `publish-step1` wraps `rex_sub_823B9158` when called from `0x823B7EE4`
- `publish-step2` wraps `rex_sub_823A8910` when called from `0x823B7F00`

Purpose:

- identify which internal call actually flips the singleton primary triple from the silent block to
  the live block
- make the next trace the last useful diagnostic split before attempting a real fix

## Latest Trace Result

The newest trace settles the `823B7EC8` split cleanly.

`publish-step1` (`rex_sub_823B9158`) is the mutator:

- first live wakeup seen directly at:
  - `primary_before=[2076E1CC 207F0CCC 00000010]`
  - `primary_after=[2076E1CC 207F304C 00000010]`
- this happens in:
  - `AC6 movie worker publish-step1 ...`

Representative lines:

- [publish-step1 wakeup]
  - `2026-04-06 18:01:29.389`
- [publish-step1 wakeup again]
  - `2026-04-06 18:02:01.591`

`publish-step2` (`rex_sub_823A8910`) is not the mutator:

- in the same periods it stays:
  - `primary_before == primary_after`
- its logged object-base state also stays unchanged:
  - `obj_base_before == obj_base_after`

Important nuance:

- `publish-step1` performs the flip
- but its currently logged visible object fields still do not change:
  - `obj44`
  - `obj48`
  - `obj61`
  - `obj64`

So the remaining hidden state is now narrowed to the internals of `823B9158`, specifically the data
flow between:

- the vfunc call at slot `72`
- the later vfunc call at slot `84`

This means the broad worker-path search is over. The root of the delayed publish is now inside
`rex_sub_823B9158`.

## Latest Patch Added

Refined the final mutator trace inside:

- `src/ac6_audio_hooks.cpp`

`rex_sub_823B9158` is now manually mirrored so the trace can stop at its internal boundaries instead
of only at function entry/exit.

The existing `AC6 movie worker publish-step1 ...` line now records:

- vfunc target at slot `72`
- slot-72 return value
- optional callback target from `obj+44`
- callback argument at stack `+80` before/after
- vfunc target at slot `84`
- slot-84 return value
- singleton primary triple:
  - before
  - after slot `72`
  - after the middle callback
  - after slot `84`

Purpose:

- determine whether the primary-slot flip happens:
  - immediately after slot `72`
  - during the optional callback through `obj+44`
  - or only after the slot-`84` consume step

This keeps the trace inside the confirmed mutator and avoids widening the search again.

## Latest Result

The newest trace from [ac6_audio_trace.log](/C:/AC6Recomp_ext/ac6_audio_trace.log) finally split
`publish-step1` cleanly enough to identify the active mutator boundary.

What the trace showed:

- slot `72` target is stable:
  - `820E36E8`
- slot `72` return is stable:
  - `20431C8C`
- the optional callback path is inactive the whole time:
  - `cb44 = 00000000`
  - `cb48 = 00000000`
- the slot `84` target is stable:
  - `823B7C80`
- when the singleton primary triple changes, it only changes in:
  - `primary_after_slot84`

Concrete example:

- [slot84 first visible change]
  - `2026-04-06 18:09:07.328`
  - `primary_before=[2042DC8C 2042DC8C 00000010]`
  - `primary_after_slot72=[2042DC8C 2042DC8C 00000010]`
  - `primary_after_callback=[2042DC8C 2042DC8C 00000010]`
  - `primary_after_slot84=[2076E1CC 2076E1CC 00000010]`

Conclusion:

- `rex_sub_823B7C80` is only the outer shell
- the actual primary-slot mutation happens inside its internal `rex_sub_823B9F10` call
- the final indirect call through `obj+68` / slot `24` (`823A6878`) is not the mutator

## Latest Patch Added

Added a direct wrapper for:

- `src/ac6_audio_hooks.cpp`
  - `rex_sub_823B7C80`

This wrapper is manually mirrored and only emits a narrow trace when called from:

- `caller_lr = 0x823B91E8`

New trace marker:

- `AC6 movie worker publish-step84 ...`

It records:

- object pointer and argument pointer
- `obj+68` before/after
- the vtable target used from `obj+68` slot `24`
- argument word `0` before/after
- singleton primary triple:
  - before
  - after the internal `823B9F10` call
  - after the final indirect call through `obj+68`

Purpose:

- determine whether the delayed publish happens inside:
  - `rex_sub_823B9F10`
  - or the final indirect consume step off `obj+68`

Result from the latest trace:

- `stage1_result` stays `0`
- `stage2_result` stays `0`
- but the primary triple mutates in `primary_after_stage1`
- it does not mutate between `primary_after_stage1` and `primary_after`

Concrete examples:

- [step84 first stage1 mutation]
  - `2026-04-06 18:15:41.758`
  - `primary_before=[2042DC8C 2042DC8C 00000010]`
  - `primary_after_stage1=[2076E1CC 2078A38C 00000010]`
  - `primary_after=[2076E1CC 207F0CCC 00000010]`

- [producer wakeup]
  - `2026-04-06 18:17:16.129`
  - movie buffer becomes nonzero shortly after the singleton primary reaches:
  - `primary=[2076E1CC 207F304C 00000010]`

So the new front edge is now inside the loop in `rex_sub_823B9F10`.

## Latest Patch Added

Added a direct loop wrapper for:

- `src/ac6_audio_hooks.cpp`
  - `rex_sub_823B9F10`

New trace marker:

- `AC6 movie worker publish-step9F10 ...`

It records:

- object pointer
- argument pointer
- iteration count from `obj+60`
- current iteration index
- current descriptor pointer from `obj+36 + index * 8`
- descriptor words `[0]` and `[1]`
- singleton primary triple before/after each `rex_sub_823B9700` iteration

Purpose:

- determine which descriptor iteration in `823B9F10` performs the real publish
- identify the exact descriptor/state boundary that advances silence into live movie PCM

This is now the narrowest remaining safe trace point.

## Files Touched During This Investigation

- `src/ac6_audio_policy.cpp`
- `src/ac6_audio_hooks.cpp`
- `out/build/win-amd64-relwithdebinfo/ac6recomp.toml`
- `thirdparty/rexglue-sdk/src/audio/audio_runtime.cpp`
- `thirdparty/rexglue-sdk/src/audio/sdl/sdl_audio_driver.cpp`
- `thirdparty/rexglue-sdk/include/rex/audio/xma/context.h`
- `thirdparty/rexglue-sdk/src/audio/xma/context.cpp`

## Build Note

No build was run from this session after the latest patch. User is building locally.

One compile issue was hit after the producer-snapshot diagnostics were added:

- `src/ac6_audio_hooks.cpp` used `PPC_LOAD_U32` in a helper without `base` in scope

That source issue has been corrected by threading `base` through the helper. The next local build should proceed past that error.
