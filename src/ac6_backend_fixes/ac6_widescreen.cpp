// AC6 enhancement: arbitrary aspect ratio (ultrawide) - camera aspect patcher.
//
// Discovery (exchange/ultrawide/ac6recomp.log, 2026-07-03): the game keeps the
// display aspect ratio 16:9 as 1.7777778f (big-endian 0x3FE38E39) per camera
// object, laid out around the aspect field as
//   [-0x04] fov     (~0.40 / 0.44 / 0.68 observed; changes with zoom)
//   [+0x00] aspect  1.7777778f
//   [+0x04] near    (0.1 / 1.0 observed)
//   [+0x08] far     (24000.0 observed)
// with the camera's view rotation basis a few rows below. A global camera
// template lives at guest 0x82A160C8, live cameras on the physically-backed
// heap, and the game's static 16:9 default constant sits in a data table in
// the XEX image at 0x8206A0F4 (and 0x9206A0F4 through the second view).
//
// Mechanism: a background thread polls the game's mode-task state machine
// (the runtime-verified chain [0x8293B930] -> +0x8 -> vtable, see
// docs/re/subsystems/selftest_macro.md) every 250 ms and, on the 2 s cadence
// or immediately on a mission transition,
//   1. (re)patches the static default at fixed addresses, so cameras created
//      afterwards are born with the current target aspect, and
//   2. signature-scans committed guest memory for camera objects carrying the
//      previous aspect and pokes their aspect field.
// INSIDE a mission (mode task CModeTaskGame: gameplay, in-engine cutscenes,
// pause) the target is the wide aspect - the game builds its own wider
// projection, the compressed 16:9 guest output is presented stretched to the
// window (via the presenter's letterbox override), and the draw-time UI
// shrink keeps the 2D layer proportioned. OUTSIDE a mission everything is reverted to 16:9 and
// the presenter is forced to letterbox: menus, hangar, briefing, FMV and the
// attract demo render exactly vanilla.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <native/ui/presenter.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "../render_hooks.h"
#include "ac6_widescreen.h"

REXCVAR_DEFINE_BOOL(ac6_widescreen, false, "AC6/Enhancements",
                    "Ultrawide support (hor+), in missions only. The aspect follows "
                    "the window size; menus and the front end stay vanilla 16:9.");
REXCVAR_DEFINE_BOOL(ac6_widescreen_cinematics, true, "AC6/Enhancements",
                    "With ultrawide on, render in-engine cinematics wide too. They "
                    "are staged for 16:9, so widening can expose set edges.");
// Read (never written) only to report it in the activation log.
REXCVAR_DECLARE(bool, present_letterbox);

// This file drives guest camera objects directly, which needs three things the
// host OS provides differently: enumerating readable guest memory, reading a
// word that may fault, and writing one that may fault. Windows uses
// VirtualQuery plus __try/__except; POSIX uses process_vm_readv (the access
// happens in the kernel and reports EFAULT instead of raising SIGSEGV/SIGBUS -
// necessary because guest memory is an shm mapping where a page can be
// readable yet unbacked) and mprotect. Everything above that split is shared.

namespace {

constexpr float kNativeAspect = 1.7777778f;  // BE 0x3FE38E39, exactly what the game stores
constexpr uint64_t kGuestScanBegin = 0x00010000ull;  // page 0 is the null guard
constexpr uint64_t kGuestScanEnd = 0xC0000000ull;  // C0/E0 views alias A0 - skip them
#if !defined(_WIN32)
// Chunk the POSIX sweep reads guest memory in. Large enough that the syscall
// cost is negligible against the copy, small enough to keep the scratch buffer
// off the "large allocation" path.
constexpr size_t kSweepChunkBytes = 1u << 20;
#endif
// Mode poll every 250 ms (a cheap 3-dereference guest read) so mission
// transitions re-aim the cameras promptly; the full memory sweep still runs on
// the 2 s cadence (8 polls) or immediately on a transition.
constexpr uint32_t kModePollMs = 250;
constexpr uint32_t kSweepEveryPolls = 8;

// The game's front end is a mode-task state machine (docs/re/subsystems/
// selftest_macro.md, runtime-verified): [0x8293B930] -> CTaskModeManager
// singleton, +0x8 -> the currently-running CModeTask, +0x0 -> its vtable
// pointer, which is a static per-class address and therefore a screen id.
// The +0x8 slot is briefly null while the manager swaps tasks - treated as
// "hold the previous state".
constexpr uint32_t kModeManagerPtrEA = 0x8293B930;
constexpr uint32_t kModeTaskSlotOffset = 0x8;
// Mode tasks that present as gameplay and must render wide. Several exist -
// the campaign, the tutorial and the replay viewer are separate tasks - so
// this is a set rather than a single comparison.
//   0x820642F4  CModeTaskGame - campaign missions (gameplay, in-engine
//               cutscenes and the pause menu all run under it).
//   0x8206474C  tutorial. From a user log 2026-08-09: the last mode
//               transition before a session that ended in the tutorial.
//   0x820646EC  replay viewer. From the same reporter's second log: the only
//               task held for a long dwell (~38 s) between menu transitions
//               of 2-3 s, returning afterwards to the menu it came from.
// To add another mode, read its id from the "[AC6-WIDE] mode task 0x..."
// line (logged at error level on every transition) while that mode is on
// screen, and list it below.
constexpr uint32_t kModeTaskWideVtables[] = {
    0x820642F4,
    0x8206474C,
    0x820646EC,
    0x82066BC4,
};

bool IsWideModeTask(uint32_t vtable) {
  for (uint32_t candidate : kModeTaskWideVtables) {
    if (vtable == candidate) {
      return true;
    }
  }
  return false;
}

std::atomic<rex::memory::Memory*> g_ws_memory{nullptr};
// Bits of the UI X-shrink factor (16:9 / target aspect), published by the
// patcher thread for the per-draw ortho patch; 0 = UI patching disabled.
std::atomic<uint32_t> g_ui_shrink_bits{0};
// Whether the current scene should render/present wide: the mode task is the
// mission, or an in-engine cinematic is playing with
// ac6_widescreen_cinematics on. Published by the patcher thread's poll.
// Drives BOTH halves of the policy: the presentation (fill when wide,
// letterbox everywhere else) and the camera aspect target (wide vs native).
std::atomic<bool> g_wide_scene{false};
// Whether a valid WIDER-than-16:9 target is currently in effect (published by
// the patcher thread). False at 16:9 and at NARROWER windows - there the
// presentation must letterbox even in-mission, or the 16:9 guest frame would
// stretch vertically to fill a narrow window. (Narrower-than-16:9 rendering
// itself is not supported: the world could widen vertically by the same
// camera mechanism, but the 1280x720 UI cannot be expanded horizontally
// without pushing corner-anchored elements off-screen.)
std::atomic<bool> g_target_wide{false};

// Whether the UI shrink applies to the CURRENT scene (shared by the
// constant-level patch and the sub-viewport rect shrink; must stay in sync
// with the apply logic in WidescreenPatchUiOrtho). Wide scenes only: during
// world rendering (the crisp-HUD config) and in-mission without world draws
// (the pause menu over the frozen frame); outside wide scenes everything
// presents letterboxed vanilla and nothing is shrunk.
bool UiShrinkSceneActive() {
  return g_wide_scene.load(std::memory_order_relaxed);
}

// The target-marker vertex shader (guest ucode hash) is never UI-shrunk: the
// game places markers by projecting world coordinates through the widened
// camera, so they are already positioned for the full-width display -
// shrinking them would pull them off-target toward screen center. Their box
// graphics render proportionally wider instead; positions are exact.
constexpr uint64_t kMarkerVsUcodeHash = 0xB686E181ACD543E9ull;

// Per-swap "already narrowed" bookkeeping for the marker-quad fix. The game
// rotates three ~692 KB vertex arenas; 16384 vertices covers one at the
// observed 52-byte stride with room to spare. Command-processor thread only.
constexpr uint32_t kMarkerMaxVertices = 16384;
constexpr uint32_t kMarkerBitmapWords = kMarkerMaxVertices / 64;

struct MarkerArenaGuard {
  uint32_t base = 0;
  uint64_t bits[kMarkerBitmapWords] = {};

  // Returns true if the vertex was already narrowed this swap.
  bool TestAndSet(uint32_t vertex_index) {
    if (vertex_index >= kMarkerMaxVertices) {
      return true;  // Out of range: treat as done, i.e. leave it alone.
    }
    uint64_t& word = bits[vertex_index >> 6];
    uint64_t bit = uint64_t(1) << (vertex_index & 63);
    bool was_set = (word & bit) != 0;
    word |= bit;
    return was_set;
  }

  void Clear() { std::memset(bits, 0, sizeof(bits)); }
};

MarkerArenaGuard g_marker_guards[4];

MarkerArenaGuard& MarkerGuardFor(uint32_t arena_base) {
  for (MarkerArenaGuard& g : g_marker_guards) {
    if (g.base == arena_base) {
      return g;
    }
  }
  // Claim a free slot, or recycle the last one (the game uses three arenas).
  for (MarkerArenaGuard& g : g_marker_guards) {
    if (g.base == 0) {
      g.base = arena_base;
      g.Clear();
      return g;
    }
  }
  MarkerArenaGuard& g = g_marker_guards[rex::countof(g_marker_guards) - 1];
  g.base = arena_base;
  g.Clear();
  return g;
}

void MarkerGuardsResetForSwap() {
  for (MarkerArenaGuard& g : g_marker_guards) {
    g.Clear();
  }
}

uint32_t HostBitsOf(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return rex::byte_swap(bits);  // little-endian dword whose bytes read big-endian
}

// Fault-tolerant single-word accessors for the poke paths.
//
// Windows: the SDK's vectored handler runs BEFORE these __except filters, so a
// write fault on a GPU-write-watched physical page is still recovered
// transparently (like any guest write); only genuinely unrecoverable access
// violations land here and are reported as failure instead of crashing.
//
// POSIX: reads go through process_vm_readv so an unbacked shm page reports
// EFAULT instead of raising SIGBUS. Writes deliberately do NOT use
// process_vm_writev - a kernel-side write would bypass the SDK's userspace
// write-watch SIGSEGV handler, so the GPU would never learn the page changed.
// Instead the address is probed for readability first (which rejects the
// unmapped case) and then written normally, leaving write-watch semantics
// exactly as they are for any other guest write.
#if defined(_WIN32)

bool SafeReadU32(const uint32_t* p, uint32_t* out) noexcept {
  __try {
    *out = *p;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool SafeWriteU32(uint32_t* p, uint32_t value) noexcept {
  __try {
    *p = value;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

#else  // !_WIN32

bool SafeReadU32(const uint32_t* p, uint32_t* out) noexcept {
  iovec local{out, sizeof(*out)};
  iovec remote{const_cast<uint32_t*>(p), sizeof(*out)};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(sizeof(*out));
}

bool SafeWriteU32(uint32_t* p, uint32_t value) noexcept {
  uint32_t probe;
  if (!SafeReadU32(p, &probe)) {
    return false;
  }
  *p = value;
  return true;
}

#endif  // _WIN32

// Makes a host range writable. The XEX image copy holding the static default
// aspect is mapped read-only, so the poke needs this first. Already-writable
// pages make it a no-op on both platforms, so callers need no separate query.
// Only ever applied to the two fixed static addresses - never to swept heap
// pages, whose read-only state may be the SDK's GPU write watching and must be
// left for the SDK's own handler to recover.
bool MakeHostWritable(void* p, size_t bytes) noexcept {
#if defined(_WIN32)
  DWORD old_protect;
  return VirtualProtect(p, bytes, PAGE_READWRITE, &old_protect) != 0;
#else
  const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
  const uintptr_t start = uintptr_t(p) & ~(page - 1);
  const size_t len = size_t(uintptr_t(p) + bytes - start);
  return mprotect(reinterpret_cast<void*>(start), len, PROT_READ | PROT_WRITE) == 0;
#endif
}

// Reads a block of guest memory into a caller-owned buffer, returning the
// number of bytes actually obtained (0 if nothing there, short if the range
// runs into a gap). Used by the sweep on POSIX, where the scan works on a copy
// rather than in place.
#if !defined(_WIN32)
size_t SafeReadBlock(const void* p, void* out, size_t bytes) noexcept {
  iovec local{out, bytes};
  iovec remote{const_cast<void*>(p), bytes};
  const ssize_t got = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
  return got < 0 ? 0 : size_t(got);
}
#endif

// Big-endian guest dword read through the translated view, SEH-safe.
bool SafeReadGuestU32(rex::memory::Memory* memory, uint32_t guest_ea, uint32_t* out) {
  if (!guest_ea) {
    return false;
  }
  uint32_t raw;
  if (!SafeReadU32(memory->TranslateVirtual<const uint32_t*>(guest_ea), &raw)) {
    return false;
  }
  *out = rex::byte_swap(raw);
  return true;
}

// The current mode task's vtable pointer (= screen id), or 0 while unknown
// (manager not up yet, slot mid-swap, or the read faulted).
uint32_t ReadCurrentScreenId(rex::memory::Memory* memory) {
  uint32_t manager, task, vtable;
  if (!SafeReadGuestU32(memory, kModeManagerPtrEA, &manager) || !manager) {
    return 0;
  }
  if (!SafeReadGuestU32(memory, manager + kModeTaskSlotOffset, &task) || !task) {
    return 0;
  }
  if (!SafeReadGuestU32(memory, task, &vtable)) {
    return 0;
  }
  return vtable;
}

// Is words[i] an aspect field whose neighbours look like a camera (fov, near,
// far in sane ranges)? Pure predicate over an already-safe buffer - shared by
// both platforms' sweeps.
bool CameraSignatureAt(const uint32_t* words, size_t i, size_t count, uint32_t aspect_pattern,
                       uint32_t prev_pattern) {
  if (i < 1 || i + 2 >= count) {
    return false;
  }
  if (words[i] != aspect_pattern && (!prev_pattern || words[i] != prev_pattern)) {
    return false;
  }
  uint32_t w;
  float fov, near_plane, far_plane;
  w = rex::byte_swap(words[i - 1]);
  std::memcpy(&fov, &w, sizeof(fov));
  w = rex::byte_swap(words[i + 1]);
  std::memcpy(&near_plane, &w, sizeof(near_plane));
  w = rex::byte_swap(words[i + 2]);
  std::memcpy(&far_plane, &w, sizeof(far_plane));
  return fov > 0.05f && fov < 2.0f && near_plane > 0.005f && near_plane < 10.0f &&
         far_plane > 1000.0f && far_plane < 200000.0f;
}

#if defined(_WIN32)
// The raw signature scan, SEH-guarded against pages vanishing mid-read.
// Scalar-only frame so __try is legal; scans the live view in place. Returns
// match count, stores up to max_out dword indices.
size_t WideScanRegionRaw(const uint32_t* words, size_t count, uint32_t aspect_pattern,
                         uint32_t prev_pattern, uint32_t* out_indices,
                         size_t max_out) noexcept {
  size_t n = 0;
  __try {
    for (size_t i = 1; i + 2 < count; ++i) {
      if (CameraSignatureAt(words, i, count, aspect_pattern, prev_pattern)) {
        if (n < max_out) {
          out_indices[n] = uint32_t(i);
        }
        ++n;
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return n;
}
#endif  // _WIN32

// The previously applied target's big-endian pattern (0 = none) - lets the
// static defaults be retargeted when the auto-derived aspect changes (window
// resized mid-session).
uint32_t g_prev_target_bits = 0;  // Patcher thread only.

// The game's static 16:9 default aspect constant in the XEX image (and its
// second-view alias): cameras copy their initial aspect from here.
constexpr uint32_t kStaticDefaultAddrs[] = {0x8206A0F4u, 0x9206A0F4u};

// Keep the game's static 16:9 default constant(s) patched. Re-checked every
// sweep in case the game rewrites them (e.g. applying video settings).
void PatchStaticDefaults(rex::memory::Memory* memory, uint32_t aspect_pattern,
                         uint32_t target_bits, float target) {
  static uint32_t patch_logs = 0;
  static uint32_t unexpected_logs = 0;
  for (uint32_t guest : kStaticDefaultAddrs) {
    uint32_t* host = memory->TranslateVirtual<uint32_t*>(guest);
    // Early in boot the page may not be committed yet (this runs from ~2s
    // after graphics init, during the startup logo) - skip and retry on the
    // next sweep rather than touching it. SafeReadU32 already reports an
    // uncommitted page as failure on both platforms, so the read IS the
    // commit check.
    uint32_t cur;
    if (!SafeReadU32(host, &cur)) {
      continue;
    }
    if (cur == target_bits) {
      continue;  // Already patched.
    }
    bool retarget = g_prev_target_bits && cur == g_prev_target_bits;
    if (cur != aspect_pattern && !retarget) {
      // Not the value we expect - wrong address for this build/version, or
      // the game stores something else here right now. Don't touch it.
      if (unexpected_logs < 4) {
        ++unexpected_logs;
        REXLOG_ERROR("[AC6-WIDE] static default @ 0x{:08X}: unexpected 0x{:08X} "
                     "(expected 16:9), skipping",
                     guest, rex::byte_swap(cur));
      }
      continue;
    }
    // The XEX image copy may be mapped read-only - unprotect before writing
    // (kept writable; this field is re-checked every sweep anyway).
    if (!MakeHostWritable(host, sizeof(uint32_t))) {
      if (unexpected_logs < 4) {
        ++unexpected_logs;
        REXLOG_ERROR("[AC6-WIDE] static default @ 0x{:08X}: read-only and unprotect "
                     "failed, skipping",
                     guest);
      }
      continue;
    }
    if (SafeWriteU32(host, target_bits) && patch_logs < 8) {
      ++patch_logs;
      REXLOG_ERROR("[AC6-WIDE] static default @ 0x{:08X}: 1.77778 -> {:g}", guest, target);
    }
  }
}

// Is the guest page holding this address writable, according to the SDK's own
// page tracking? Used on POSIX in place of a host protection query - it is the
// same source of truth the guest itself sees, and on Linux it reads the
// in-process protection shadow rather than parsing /proc/self/maps.
bool GuestRangeWritable(rex::memory::Memory* memory, uint32_t guest_ea) {
  auto* heap = memory->LookupHeap(guest_ea);
  if (!heap) {
    return false;
  }
  const rex::memory::PageAccess access = heap->QueryRangeAccess(guest_ea, guest_ea + 3);
  return access == rex::memory::PageAccess::kReadWrite ||
         access == rex::memory::PageAccess::kExecuteReadWrite;
}

// Poke one camera's aspect field, given its guest address. Shared by both
// platforms' sweeps: it works purely off guest EAs, so it does not care
// whether the scan ran in place (Windows) or over a copy (POSIX).
bool PokeCameraAspect(rex::memory::Memory* memory, uint32_t hit_guest, bool region_writable,
                      uint32_t to_bits, float to_value) {
  static uint32_t poke_logs = 0;
  static uint32_t skipped_ro_logs = 0;
  // Physical-view pages (>= 0xA0000000) may be read-only due to the SDK's GPU
  // write watching; the poke faults and the SDK's handler recovers it like any
  // guest write. A read-only page in a plain virtual heap has no such
  // recovery - skip those.
  if (!region_writable && hit_guest < 0xA0000000u) {
    if (skipped_ro_logs < 4) {
      ++skipped_ro_logs;
      REXLOG_ERROR("[AC6-WIDE] camera @ 0x{:08X} in read-only region, skipping", hit_guest);
    }
    return false;
  }
  uint32_t* field = memory->TranslateVirtual<uint32_t*>(hit_guest);
  uint32_t fov_word = 0;
  SafeReadU32(field - 1, &fov_word);
  const uint32_t w = rex::byte_swap(fov_word);
  float fov;
  std::memcpy(&fov, &w, sizeof(fov));
  if (!SafeWriteU32(field, to_bits)) {
    return false;
  }
  if (poke_logs < 32) {
    ++poke_logs;
    REXLOG_ERROR("[AC6-WIDE] camera aspect @ 0x{:08X} (fov={:g}) -> {:g}", hit_guest, fov,
                 to_value);
  }
  return true;
}

// Signature-scan committed guest memory for camera objects whose aspect field
// matches match_a (or match_b, 0 = unused) and poke it to to_bits. Returns the
// number of fields patched. Used in both directions: native -> wide entering a
// mission (plus stale previous-wide values after a window resize), and
// wide -> native leaving one.
uint32_t WidescreenSweep(rex::memory::Memory* memory, uint32_t match_a, uint32_t match_b,
                         uint32_t to_bits, float to_value) {
  // One-time heartbeat pair: if the process dies between these two lines, the
  // log pinpoints the sweep as the culprit.
  static bool first_sweep = true;
  if (first_sweep) {
    REXLOG_ERROR("[AC6-WIDE] first sweep starting");
  }

  static uint32_t sweep_logs = 0;
  uint32_t patched = 0;

#if defined(_WIN32)
  // Windows: walk the committed regions with VirtualQuery and scan each one in
  // place - no copy, the __try in WideScanRegionRaw covers a page vanishing
  // mid-scan.
  constexpr size_t kMaxRegionMatches = 64;
  uint32_t indices[kMaxRegionMatches];
  uint64_t guest = kGuestScanBegin;
  while (guest < kGuestScanEnd) {
    uint8_t* host = memory->TranslateVirtual(uint32_t(guest));
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(host, &mbi, sizeof(mbi)) || !mbi.RegionSize) {
      break;
    }
    uint64_t skip = uint64_t(host - static_cast<uint8_t*>(mbi.BaseAddress));
    uint64_t len = std::min(uint64_t(mbi.RegionSize) - skip, kGuestScanEnd - guest);
    if (!len) {
      break;
    }
    bool readable =
        mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    bool writable = (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                                    PAGE_EXECUTE_WRITECOPY)) != 0;
    if (readable) {
      size_t found = WideScanRegionRaw(reinterpret_cast<const uint32_t*>(host), size_t(len / 4),
                                       match_a, match_b, indices, kMaxRegionMatches);
      size_t stored = std::min(found, kMaxRegionMatches);
      for (size_t h = 0; h < stored; ++h) {
        if (PokeCameraAspect(memory, uint32_t(guest + uint64_t(indices[h]) * 4), writable,
                             to_bits, to_value)) {
          ++patched;
        }
      }
    }
    guest += len;
  }
#else
  // POSIX: there is no VirtualQuery, and /proc/self/maps is far too slow to
  // parse per sweep. Instead stride the guest range in chunks read through
  // process_vm_readv, which fails cleanly on anything unmapped - so the walk
  // needs no protection map at all - and scan the copy. Writability comes from
  // the SDK's own page tracking rather than the host mapping.
  std::vector<uint32_t> chunk(kSweepChunkBytes / 4);
  uint64_t guest = kGuestScanBegin;
  while (guest < kGuestScanEnd) {
    const size_t want = size_t(std::min<uint64_t>(kSweepChunkBytes, kGuestScanEnd - guest));
    const size_t got =
        SafeReadBlock(memory->TranslateVirtual(uint32_t(guest)), chunk.data(), want);
    if (!got) {
      guest += want;  // nothing mapped here - stride past it
      continue;
    }
    const size_t words = got / 4;
    const bool writable = GuestRangeWritable(memory, uint32_t(guest));
    for (size_t i = 1; i + 2 < words; ++i) {
      if (!CameraSignatureAt(chunk.data(), i, words, match_a, match_b)) {
        continue;
      }
      if (PokeCameraAspect(memory, uint32_t(guest + uint64_t(i) * 4), writable, to_bits,
                           to_value)) {
        ++patched;
      }
    }
    guest += got;
    // A short read means the chunk ran into an unmapped page; step over it so
    // the next iteration starts past the gap instead of retrying the same one.
    if (got < want) {
      guest += 0x1000;
    }
  }
#endif

  if (first_sweep) {
    first_sweep = false;
    REXLOG_ERROR("[AC6-WIDE] first sweep done");
  }
  if (patched && sweep_logs < 16) {
    ++sweep_logs;
    REXLOG_ERROR("[AC6-WIDE] sweep: patched {} camera aspect field(s) -> {:g}", patched,
                 to_value);
  }
  return patched;
}

void WidescreenThread() {
  const uint32_t native_bits = HostBitsOf(kNativeAspect);
  uint32_t poll_count = 0;
  bool last_in_mission = false;
  bool last_wide_scene = false;
  uint32_t last_screen_id = 0;
  // The wide pattern last written anywhere (0 = never widened). Kept across
  // reverts so widen sweeps also convert stale leftovers.
  uint32_t applied_wide_bits = 0;
  // Consecutive sweeps that patched nothing since the last transition; out of
  // the mission the scan stops after two clean passes (the statics recheck is
  // cheap and continues) so the front end is not scanned forever.
  uint32_t clean_reverts = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kModePollMs));
    bool enabled = REXCVAR_GET(ac6_widescreen);
    rex::memory::Memory* memory = g_ws_memory.load(std::memory_order_acquire);
    // Mode poll, every cycle: cheap SEH-safe 3-dereference guest read. A null
    // read (manager not up, task slot mid-swap) holds the previous state.
    bool in_mission = last_in_mission;
    if (enabled && memory) {
      uint32_t id = ReadCurrentScreenId(memory);
      if (id != 0) {
        in_mission = IsWideModeTask(id);
        if (id != last_screen_id) {
          last_screen_id = id;
          static uint32_t mode_logs = 0;
          if (mode_logs < 32) {
            ++mode_logs;
            // The id log exists so an unlisted mode task that SHOULD present
            // wide (if some in-mission path swaps tasks) can be identified
            // from a user log and whitelisted.
            REXLOG_ERROR("[AC6-WIDE] mode task 0x{:08X} ({})", id,
                         in_mission ? "mission" : "front-end");
          }
        }
      }
    } else {
      in_mission = false;
    }
    last_in_mission = in_mission;
    // Opt-in: in-engine cinematics outside the mission task (story scenes in
    // the campaign flow) render wide too. Same demo-manager signal as the
    // cutscene frame-rate clamp, ~300 ms decay - the 250 ms poll tracks it.
    bool wide_scene =
        in_mission || (enabled && REXCVAR_GET(ac6_widescreen_cinematics) &&
                       ac6::IsCinematicActive());
    g_wide_scene.store(wide_scene, std::memory_order_relaxed);
    bool transition = wide_scene != last_wide_scene;
    last_wide_scene = wide_scene;
    ++poll_count;
    if (!transition && poll_count < kSweepEveryPolls) {
      continue;  // Sweep on the 2 s cadence or immediately on a transition.
    }
    poll_count = 0;
    // The target aspect is the actual window's, re-derived each sweep (a live
    // resize adapts). At or narrower than 16:9 the widening disables (clamped
    // to native -> target invalid -> letterboxed presentation).
    float target = 0.0f;
    if (enabled) {
      uint32_t surface_w, surface_h;
      if (rex::ui::GetPresentSurfaceSize(&surface_w, &surface_h) && surface_h) {
        target = float(surface_w) / float(surface_h);
        if (target > 8.0f) {
          target = 8.0f;
        }
        if (target < kNativeAspect) {
          target = kNativeAspect;
        }
        static float last_logged_target = 0.0f;
        static uint32_t auto_logs = 0;
        if (std::fabs(target - last_logged_target) > 1e-3f && auto_logs < 8) {
          ++auto_logs;
          last_logged_target = target;
          REXLOG_ERROR("[AC6-WIDE] auto aspect: window {}x{} -> target {:g}", surface_w,
                       surface_h, target);
        }
      }
      // No surface yet (very early boot): retry next sweep.
    }
    bool target_valid =
        target > 0.5f && target < 8.0f && std::fabs(target - kNativeAspect) >= 1e-4f;
    g_target_wide.store(enabled && target_valid, std::memory_order_relaxed);
    // Publish the UI shrink factor for the per-draw ortho patch (0 = off).
    // Scene gating (mission / world) happens at the consumers.
    uint32_t shrink_bits = 0;
    if (enabled && target_valid) {
      float shrink = kNativeAspect / target;
      std::memcpy(&shrink_bits, &shrink, sizeof(shrink_bits));
    }
    g_ui_shrink_bits.store(shrink_bits, std::memory_order_relaxed);
    if (!enabled || !memory) {
      continue;
    }
    bool widen = wide_scene && target_valid;
    // Any change in what the cameras should be aimed at re-arms the sweep
    // (scene transitions AND target flips, e.g. a mid-mission resize to or
    // from a <=16:9 window).
    static bool last_widen = false;
    if (widen != last_widen) {
      clean_reverts = 0;
    }
    last_widen = widen;
    if (widen) {
      // Entering / inside a wide scene (mission, or an opted-in cinematic):
      // keep the static defaults patched (so cameras are born wide) and
      // convert any native or stale-wide cameras.
      uint32_t to_bits = HostBitsOf(target);
      if (to_bits != applied_wide_bits) {
        clean_reverts = 0;
      }
      PatchStaticDefaults(memory, native_bits, to_bits, target);
      uint32_t stale =
          (applied_wide_bits && applied_wide_bits != to_bits) ? applied_wide_bits : 0;
      WidescreenSweep(memory, native_bits, stale, to_bits, target);
      applied_wide_bits = to_bits;
      g_prev_target_bits = to_bits;
    } else if (applied_wide_bits) {
      // Out of every wide scene (or the target became native, e.g. a live
      // resize to 16:9 or narrower): restore the static defaults and revert
      // wide cameras so everything renders vanilla 16:9.
      PatchStaticDefaults(memory, native_bits, native_bits, kNativeAspect);
      if (clean_reverts < 2) {
        uint32_t patched =
            WidescreenSweep(memory, applied_wide_bits, 0, native_bits, kNativeAspect);
        clean_reverts = patched ? 0 : clean_reverts + 1;
      }
    }
  }
}

}  // namespace

namespace ac6 {

void WidescreenInit(rex::memory::Memory* memory) {
  if (!memory) {
    return;
  }
  g_ws_memory.store(memory, std::memory_order_release);
  static std::once_flag once;
  std::call_once(once, [] {
    // No cvar is written here, deliberately. The feature drives presentation
    // through the presenter's letterbox OVERRIDE instead (see
    // WidescreenNotifySwapSource): writing present_letterbox would leak into
    // the user's saved config - the in-game settings menu persists current
    // cvar values - and would then keep the game stretched after the feature
    // was switched off again.
    if (REXCVAR_GET(ac6_widescreen)) {
      // present_letterbox is REPORTED here, never written: it must still read
      // exactly what the user configured (default true) with the feature on,
      // so an in-game settings save can never persist a value we chose.
      REXLOG_ERROR("[AC6-WIDE] widescreen active: cinematics={} (present_letterbox cvar "
                   "untouched at {}; presentation driven by the override)",
                   REXCVAR_GET(ac6_widescreen_cinematics) ? 1 : 0,
                   REXCVAR_GET(present_letterbox) ? 1 : 0);
    }
    std::thread(WidescreenThread).detach();
  });
}

bool WidescreenPatchUiOrtho(uint32_t* vs_float_constants, uint64_t vs_ucode_hash,
                            bool sub_viewport) {
  uint32_t shrink_bits = g_ui_shrink_bits.load(std::memory_order_relaxed);
  if (!shrink_bits) {
    return false;
  }
  float shrink;
  std::memcpy(&shrink, &shrink_bits, sizeof(shrink));
  bool world = ac6::WorldRenderActiveRecently();
  float* c = reinterpret_cast<float*>(vs_float_constants);
  // Scene logic (shared helper): wide scenes only - the shrink exists to
  // cancel the fill-window stretch, which is active exactly there.
  bool apply = UiShrinkSceneActive();
  // Sub-viewport draws (radar window, PiP inset) are placed by their
  // VIEWPORT - the viewport rect is shrunk instead (WidescreenViewportShrinkX
  // consumed in UpdateFixedFunctionState); their constants stay untouched.
  // World scenes only, matching the viewport-shrink gate: menu/hangar
  // sub-viewport panels keep their ordinary constant-level treatment.
  if (sub_viewport && world) {
    apply = false;
  }
  // The game-placed target markers stay full-width (see kMarkerVsUcodeHash).
  if (vs_ucode_hash == kMarkerVsUcodeHash) {
    apply = false;
  }
  // Runs on the command processor thread only.
  static uint32_t patch_logs = 0;
  // Idempotency ring: (a, tx) bit patterns this patch has produced. The
  // register file persists across draws, and unlike the old exact-value
  // match, the generalized shape rule would re-match its own output and
  // compound the shrink every draw that reuses stale constants - so anything
  // we ever emitted is recognized and skipped.
  static uint64_t shrunk_keys[256] = {};
  static uint32_t shrunk_key_count = 0;
  static uint32_t shrunk_key_next = 0;
  bool patched = false;
  // Generalized screen-space 2D transform detection: any 4-vec4 block shaped
  //   r0 = (m00, m01, 0, tx)   r1 = (m10, m11, 0, ty)
  //   r2 = (0,   0,   c, tz)   r3 = (0,   0,   0, 1)  exactly
  // with a tiny 2x2 (all |m| < 0.05 - screen transforms are 2/width-sized;
  // excludes identity, world matrices and perspective/billboard blocks, whose
  // w row is never (0,0,0,1)). Rotation is allowed - the radar map/blips spin
  // with heading. Covers the plain 1280x720 UI ortho AND composed 2D
  // transforms (radar contents, PiP window), so nested elements shrink
  // consistently with their frames. Scaling the whole X output row (m00, m01,
  // tx) shrinks around NDC 0 = screen center. The matrix needs 4 vec4s
  // starting at n, so scan c0..c60.
  for (uint32_t n = 0; n <= 60; ++n) {
    float* r0 = c + 4 * n;
    const float* r1 = r0 + 4;
    const float* r2 = r0 + 8;
    const float* r3 = r0 + 12;
    if (r3[0] != 0.0f || r3[1] != 0.0f || r3[2] != 0.0f || r3[3] != 1.0f) {
      continue;
    }
    if (r0[2] != 0.0f || r1[2] != 0.0f || r2[0] != 0.0f || r2[1] != 0.0f) {
      continue;
    }
    float m00_abs = std::fabs(r0[0]);
    float m01_abs = std::fabs(r0[1]);
    float m10_abs = std::fabs(r1[0]);
    float m11_abs = std::fabs(r1[1]);
    float x_row_max = m00_abs > m01_abs ? m00_abs : m01_abs;
    float y_row_max = m10_abs > m11_abs ? m10_abs : m11_abs;
    float all_max = x_row_max > y_row_max ? x_row_max : y_row_max;
    if (!(all_max < 0.05f && x_row_max > 1e-7f && y_row_max > 1e-7f)) {
      continue;
    }
    float* tx = &r0[3];
    // +-8: composed small-scale transforms overshoot +-1 considerably (the
    // PiP window quad sits at ty ~ 5.8).
    if (*tx < -8.0f || *tx > 8.0f || r1[3] < -8.0f || r1[3] > 8.0f) {
      continue;
    }
    // Skip transforms this patch already shrank (see the ring above): hash
    // the X output row we mutate (m00, m01, tx).
    uint32_t m00_bits, m01_bits, tx_bits;
    std::memcpy(&m00_bits, &r0[0], sizeof(m00_bits));
    std::memcpy(&m01_bits, &r0[1], sizeof(m01_bits));
    std::memcpy(&tx_bits, tx, sizeof(tx_bits));
    uint64_t key = 1469598103934665603ull;
    key = (key ^ m00_bits) * 1099511628211ull;
    key = (key ^ m01_bits) * 1099511628211ull;
    key = (key ^ tx_bits) * 1099511628211ull;
    bool already_shrunk = false;
    for (uint32_t i = 0; i < shrunk_key_count; ++i) {
      if (shrunk_keys[i] == key) {
        already_shrunk = true;
        break;
      }
    }
    if (already_shrunk || !apply) {
      continue;
    }
    r0[0] *= shrink;
    r0[1] *= shrink;
    *tx *= shrink;
    patched = true;
    // Remember the shrunk output so it is never shrunk again.
    std::memcpy(&m00_bits, &r0[0], sizeof(m00_bits));
    std::memcpy(&m01_bits, &r0[1], sizeof(m01_bits));
    std::memcpy(&tx_bits, tx, sizeof(tx_bits));
    key = 1469598103934665603ull;
    key = (key ^ m00_bits) * 1099511628211ull;
    key = (key ^ m01_bits) * 1099511628211ull;
    key = (key ^ tx_bits) * 1099511628211ull;
    shrunk_keys[shrunk_key_next] = key;
    shrunk_key_next = (shrunk_key_next + 1) & 255;
    if (shrunk_key_count < 256) {
      ++shrunk_key_count;
    }
    if (patch_logs < 8) {
      ++patch_logs;
      REXLOG_ERROR("[AC6-WIDE] screen transform @ c{} (m00={:g} tx={:g}) shrunk x{:g}", n,
                   r0[0] / shrink, *tx / shrink, shrink);
    }
  }
  return patched;
}

bool WidescreenWantsMarkerQuadFix(uint64_t vs_ucode_hash) {
  return vs_ucode_hash == kMarkerVsUcodeHash &&
         g_ui_shrink_bits.load(std::memory_order_relaxed) != 0 &&
         g_wide_scene.load(std::memory_order_relaxed);
}

void WidescreenShrinkMarkerQuads(uint8_t* vertices, uint32_t vertex_stride, uint32_t pos_offset,
                                 const uint8_t* indices, bool indices_32bit, uint32_t count,
                                 uint32_t arena_base) {
  uint32_t shrink_bits = g_ui_shrink_bits.load(std::memory_order_relaxed);
  if (!vertices || !vertex_stride || count < 4 || !shrink_bits) {
    return;
  }
  float shrink;
  std::memcpy(&shrink, &shrink_bits, sizeof(shrink));
  if (!(shrink > 0.0f) || shrink >= 0.999f) {
    return;
  }
  MarkerArenaGuard& guard = MarkerGuardFor(arena_base);
  uint32_t quads = count / 4;
  if (!quads) {
    return;
  }

  // Read every quad's bounds up front. Text is drawn one quad per GLYPH, so a
  // quad is not an element: narrowing each glyph about its own centre leaves
  // the string's letter spacing at full width (letters end up thin and spread
  // out). Elements are recovered below by grouping.
  struct QuadBounds {
    uint32_t vi[4];
    float xlo, xhi, ylo, yhi;
    bool valid;
  };
  static std::vector<QuadBounds> bounds;  // CP thread only; reused per draw.
  bounds.clear();
  bounds.reserve(quads);
  for (uint32_t q = 0; q < quads; ++q) {
    QuadBounds b{};
    b.valid = true;
    float x[4], y[4];
    for (uint32_t c = 0; c < 4 && b.valid; ++c) {
      uint32_t at = q * 4 + c;
      uint32_t vi;
      if (!indices) {
        vi = at;
      } else if (indices_32bit) {
        uint32_t raw;
        std::memcpy(&raw, indices + at * 4, sizeof(raw));
        vi = rex::byte_swap(raw);
      } else {
        uint16_t raw;
        std::memcpy(&raw, indices + at * 2, sizeof(raw));
        vi = rex::byte_swap(raw);
      }
      if (vi >= kMarkerMaxVertices) {
        b.valid = false;
        break;
      }
      b.vi[c] = vi;
      const uint8_t* vp = vertices + size_t(vi) * vertex_stride + pos_offset;
      uint32_t raw_x, raw_y;
      std::memcpy(&raw_x, vp, sizeof(raw_x));
      std::memcpy(&raw_y, vp + sizeof(float), sizeof(raw_y));
      raw_x = rex::byte_swap(raw_x);
      raw_y = rex::byte_swap(raw_y);
      std::memcpy(&x[c], &raw_x, sizeof(x[c]));
      std::memcpy(&y[c], &raw_y, sizeof(y[c]));
      if (!std::isfinite(x[c]) || !std::isfinite(y[c])) {
        b.valid = false;
      }
    }
    if (b.valid) {
      b.xlo = b.xhi = x[0];
      b.ylo = b.yhi = y[0];
      for (uint32_t c = 1; c < 4; ++c) {
        b.xlo = x[c] < b.xlo ? x[c] : b.xlo;
        b.xhi = x[c] > b.xhi ? x[c] : b.xhi;
        b.ylo = y[c] < b.ylo ? y[c] : b.ylo;
        b.yhi = y[c] > b.yhi ? y[c] : b.yhi;
      }
    }
    bounds.push_back(b);
  }

  // Group consecutive quads into elements. The game emits a string's glyphs
  // back to back, on one baseline, with a small kerning gap (measured: 8 px
  // glyphs on a 10 px pitch, i.e. 2 px gaps), so a run of quads sharing a Y
  // span and separated by less than kElementGapPx is one element. Everything
  // else stays its own element, including a lone box quad.
  constexpr float kElementGapPx = 6.0f;
  constexpr float kBaselineEpsPx = 1.0f;
  struct Element {
    uint32_t first, last;
    float xlo, xhi;
  };
  static std::vector<Element> elements;  // CP thread only; reused per draw.
  elements.clear();
  for (uint32_t q = 0; q < quads;) {
    if (!bounds[q].valid) {
      ++q;
      continue;
    }
    Element e{};
    e.first = e.last = q;
    e.xlo = bounds[q].xlo;
    e.xhi = bounds[q].xhi;
    const float line_ylo = bounds[q].ylo;
    const float line_yhi = bounds[q].yhi;
    for (uint32_t n = q + 1; n < quads; ++n) {
      const QuadBounds& b = bounds[n];
      if (!b.valid) {
        break;
      }
      bool same_line = std::fabs(b.ylo - line_ylo) <= kBaselineEpsPx &&
                       std::fabs(b.yhi - line_yhi) <= kBaselineEpsPx;
      if (!same_line || b.xlo < e.xhi - kBaselineEpsPx || b.xlo - e.xhi > kElementGapPx) {
        break;
      }
      e.xhi = b.xhi > e.xhi ? b.xhi : e.xhi;
      e.xlo = b.xlo < e.xlo ? b.xlo : e.xlo;
      e.last = n;
    }
    elements.push_back(e);
    q = e.last + 1;
  }

  // Every element narrows about its OWN centre.
  //
  // Pivoting a label about its nearest box instead (so the label's offset
  // from the box would shrink too) was tried and REVERTED: which marker a
  // label belongs to is not encoded in the vertex data, and proximity is not
  // a stable substitute - overlapping markers sit as little as 39 px apart,
  // so under aircraft roll the nearest-box choice flips from frame to frame
  // and the label visibly jumps between two spacings. A static, slightly wide
  // label-to-box gap beats a moving one. Fixing the gap properly needs the
  // game-side marker/label association, not screen geometry.
  uint32_t narrowed_elements = 0;
  for (const Element& e : elements) {
    const float cx = 0.5f * (e.xlo + e.xhi);
    for (uint32_t n = e.first; n <= e.last; ++n) {
      const QuadBounds& b = bounds[n];
      for (uint32_t c = 0; c < 4; ++c) {
        if (guard.TestAndSet(b.vi[c])) {
          continue;  // Already narrowed this swap - never compound.
        }
        uint8_t* vp = vertices + size_t(b.vi[c]) * vertex_stride + pos_offset;
        uint32_t raw;
        std::memcpy(&raw, vp, sizeof(raw));
        raw = rex::byte_swap(raw);
        float vx;
        std::memcpy(&vx, &raw, sizeof(vx));
        float nx = cx + (vx - cx) * shrink;
        std::memcpy(&raw, &nx, sizeof(raw));
        raw = rex::byte_swap(raw);
        std::memcpy(vp, &raw, sizeof(raw));
      }
    }
    ++narrowed_elements;
  }
  static uint32_t fix_logs = 0;
  if (fix_logs < 4) {
    ++fix_logs;
    REXLOG_ERROR("[AC6-WIDE] marker elements narrowed x{:g} ({} elements from {} quads, "
                 "arena 0x{:08X})",
                 shrink, narrowed_elements, quads, arena_base);
  }
}

float WidescreenViewportShrinkX() {
  // Cheapest test first: the shrink factor is zero unless the feature is
  // enabled AND a wider-than-16:9 target is in effect, so a disabled build
  // costs one relaxed atomic load per draw and never reads the clock.
  uint32_t shrink_bits = g_ui_shrink_bits.load(std::memory_order_relaxed);
  if (!shrink_bits) {
    return 1.0f;
  }
  // World scenes ONLY: the sub-viewport misregistration matters for the
  // in-mission radar/PiP insets. Menus and the hangar use sub-viewports for
  // ordinary panels - scaling those wrecks their layout (learned the hard
  // way), and their existing constant-level treatment is already correct.
  if (!ac6::WorldRenderActiveRecently()) {
    return 1.0f;
  }
  float shrink;
  std::memcpy(&shrink, &shrink_bits, sizeof(shrink));
  return shrink;
}

void WidescreenNotifySwapSource(bool gpu_composed, bool classification_valid) {
  // Mode-classified presentation: wide-scene frames (mission, opted-in
  // cinematics) fill the widened window; everything else (menus, hangar,
  // briefing, FMV, attract - whose cameras the patcher keeps at native 16:9)
  // presents letterboxed, i.e. vanilla. CPU-written frontbuffers (loading
  // images, FMV frames) letterbox even in wide scenes: those pixels are
  // 16:9-authored and must never stretch. And when no wider-than-16:9 target
  // is in effect (a 16:9 or NARROWER window), everything letterboxes - at
  // 16:9 that is pixel-identical to fill, and narrower windows get proper
  // bars instead of a vertical stretch.
  // Disabled: do no per-frame work at all. (Runs on the command processor
  // thread, once per swap.) The one thing a disabled build still owes is
  // releasing the presenter override if the cvar was switched off at
  // runtime - done once on the transition, not every frame.
  static bool s_was_enabled = false;
  if (!REXCVAR_GET(ac6_widescreen)) {
    if (s_was_enabled) {
      s_was_enabled = false;
      rex::ui::SetPresentLetterboxOverride(rex::ui::PresentLetterboxOverride::kUseCVar);
    }
    return;
  }
  s_was_enabled = true;
  // New frame: marker quads may be narrowed again (same CP thread as the
  // draws, so no synchronisation needed).
  MarkerGuardsResetForSwap();
  bool wide_scene = g_wide_scene.load(std::memory_order_relaxed);
  bool cpu_frame = classification_valid && !gpu_composed;
  // While the feature is on it owns the decision outright - fill only for a
  // wide scene rendered through the widened cameras, letterbox otherwise -
  // so the user's present_letterbox value is never consulted here.
  bool force = !wide_scene || cpu_frame || !g_target_wide.load(std::memory_order_relaxed);
  rex::ui::SetPresentLetterboxOverride(force ? rex::ui::PresentLetterboxOverride::kForceLetterbox
                                             : rex::ui::PresentLetterboxOverride::kForceFill);
  // Starts at 0 = "off", the presenter's actual initial state, so a disabled
  // build never logs a spurious first transition.
  static std::atomic<int> last_state{0};
  int state = force ? 1 : 0;
  if (last_state.exchange(state, std::memory_order_relaxed) != state) {
    static std::atomic<uint32_t> transition_logs{0};
    if (transition_logs.fetch_add(1, std::memory_order_relaxed) < 16) {
      REXLOG_ERROR("[AC6-WIDE] presenter letterbox {} ({})", force ? "ON" : "off",
                   wide_scene ? (cpu_frame ? "wide-scene cpu-frame" : "wide-scene")
                              : "front-end");
    }
  }
}

}  // namespace ac6
