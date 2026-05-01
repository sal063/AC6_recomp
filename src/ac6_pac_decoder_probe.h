#pragma once

#include <rex/ppc/types.h>

// Mid-asm hook on the AC6 PAC stream-worker's work-item dispatch site.
//
// Wired to `0x82343E78` in `rex_sub_82343E18` (the streamer's `bctrl` that
// dispatches the next queued work item). At the moment of the call,
// `ctr` holds the guest function pointer about to run (the work item's
// virtual method[1]) and `r28` holds the work item itself.
//
// Gated by env var `AC6_TRACE_PAC_WORK_ITEMS=1`. When enabled, each
// distinct dispatch target is logged once. The decoder we are hunting for
// will appear here as a target that runs after a compressed PAC entry
// has been fully streamed.
void ac6PacWorkerDispatchHook(PPCRegister& r28, PPCRegister& ctr);

// Second-level dispatch probe, installed at the first `bctrl` inside each of
// the five non-error PAC stream-worker state handlers (rex_sub_82345608,
// _738, _860, _A00, _B28). Each handler invokes a different vtable slot on
// the same streaming sub-class; this hook records the target function each
// slot resolves to. The mode-1 decoder is reached through one of these slots.
//
// All five sites share this single hook; cross-reference the captured target
// addresses against `[AC6 PAC] compressed entry written` lines to identify
// the decoder.
void ac6PacWorkerL2DispatchHook(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5,
                                PPCRegister& r6, PPCRegister& ctr);

// Mid-asm hook on the AC6 mode-1 decoder's post-decode site.
//
// Wired to `0x821CCC5C` in the streamer's per-entry processing function,
// immediately after `add r11,r9,r11` resolves the compress-table entry
// record pointer. At this point the prior `bl 0x822CF510` has decompressed
// the entry, `r4` holds the destination buffer's guest VA, `r11` points at
// the entry record (codec_mode at +1, csize at +8, usize at +12), `r10`
// has the entry tag (low 16 bits), and `*(r31+22888)` holds the source
// offset. Captured originally as a hand-edit in the Apr 23 build that
// produced FHM-magic'd dumps; re-introduced here as a proper midasm hook
// so codegen regeneration doesn't lose it.
void ac6PacDecoderDumpHook(PPCRegister& r4, PPCRegister& r10, PPCRegister& r11,
                           PPCRegister& r31);

// Forward decl for the dumper sink (defined in src/ac6_pac_decode_dump.cpp).
void Ac6DumpPacDecodedEntry(uint16_t entry_index, uint8_t codec_mode,
                            uint32_t compressed_size, uint32_t decompressed_size,
                            uint32_t source_offset, const uint8_t* host_data);
