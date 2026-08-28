// AC6 enhancement: native-resolution effects (game-side patch).
//
// The game registers every render buffer once at init (guest sub_821D51C0)
// into a buffer-manager singleton through two thin wrappers:
//   sub_82331F20(id, width, height, samples, bpp)         -> mgr method CB48
//   sub_82331F48(id, width, height, a, b, c)              -> mgr method CB90
// The half-res effects chain is registered as
//   id 0x8004  640x360 samples=1       bpp=32   (resolve texture)
//   id 0x8008  640x360 samples=4       r7=0     (EDRAM surface, 4xMSAA)
//   id 0x800A  640x360 r6=0x00030006   bpp=32   (packed variant)
// (matching RenderDoc: the effects surface is EDRAM base 0, pitch 16 tiles,
// 4xMSAA, k_8_8_8_8 - the scene is DOWNSCALED into it, effects draw on top,
// then it resolves and composites over the full-res scene.)
//
// Patch: rewrite those registrations to 1280x720 and drop 4xMSAA to 1x.
// 640x360@4xMSAA and 1280x720@1x occupy EXACTLY the same 720 EDRAM tiles
// (16-tile pitch both), so the game's EDRAM layout does not move; a
// 1280x720@4x surface could not exist anyway (2880 > 2048 tiles). Allocation
// sizes, texture headers, auto-viewports and resolve rects all flow from the
// registry, so the game stays self-consistent downstream. The emulator sees
// an ordinary full-res surface - no RT-cache changes needed.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/uio.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "ac6_fullres_effects.h"

REXCVAR_DEFINE_BOOL(ac6_fullres_effects, false, "AC6/Enhancements",
                    "Draw the clouds, smoke and explosions at the scene resolution "
                    "instead of the half resolution the game uses, so they stop "
                    "looking soft next to the world at raised resolution scales.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

// The two halves of the half-res effects chain, and the only registry entries
// this touches. 8004 is the resolve texture, 8008 its EDRAM surface.
// Deliberately NOT 8009/800A: those are the silhouette pair, they are not sized
// from the registry anyway, and enlarging them only starves the texture pool
// until the game stops booting.
constexpr uint32_t kFxResolveTextureId = 0x8004;
constexpr uint32_t kFxEdramSurfaceId = 0x8008;

// Extra bytes reserved for the game's shared texture pool. The enlarged buffers
// need about 19 MB more than vanilla; without the reserve the pool runs dry and
// whatever allocates last fails - in practice the 2048x2048 shadow map and the
// 1 MB effect buffers behind the afterburner and smoke.
constexpr uint32_t kFxTexturePoolBonus = 24u * 1024u * 1024u;

bool FullresEffectsOn() { return REXCVAR_GET(ac6_fullres_effects); }

bool FxBufIdSelected(uint32_t id) {
  return id == kFxResolveTextureId || id == kFxEdramSurfaceId;
}

}  // namespace

// One line the first time each mechanism engages - the feature's entire log
// surface. Enough to tell from a user's log which parts took effect, and
// nothing per frame. Error level so it survives ac6_performance_mode's
// log_level=error, like the other AC6 activation lines.
#define FX_LOG_ONCE(...)                                          \
  do {                                                            \
    static std::atomic<bool> fx_logged_{false};                   \
    if (!fx_logged_.exchange(true, std::memory_order_relaxed)) {  \
      REXLOG_ERROR(__VA_ARGS__);                                  \
    }                                                             \
  } while (false)

// Buffer registration wrapper A: (id, width, height, samples, bpp).
PPC_EXTERN_FUNC(__imp__rex_sub_82331F20);
PPC_FUNC_IMPL(rex_sub_82331F20) {
    PPC_FUNC_PROLOGUE();

    if (FullresEffectsOn() && ctx.r4.u32 == 640 && ctx.r5.u32 == 360 &&
        FxBufIdSelected(ctx.r3.u32)) {
        ctx.r4.u64 = 1280;
        ctx.r5.u64 = 720;
        if (ctx.r6.u32 == 4) {
            // 4xMSAA half-res -> 1x full-res: the same EDRAM tiles, and the
            // only option - a 1280x720 4xMSAA surface needs 2880 of 2048 tiles.
            ctx.r6.u64 = 1;
        }
        FX_LOG_ONCE("[AC6 full-res effects] registry {:04X}/{:04X} -> 1280x720, 1xMSAA",
                    kFxResolveTextureId, kFxEdramSurfaceId);
    }

    __imp__rex_sub_82331F20(ctx, base);
}

namespace {

// The game module that renders the effects chain (sub_820AF4C8 and its
// neighbors: downscale pass setup, viewport caller 820AF540, resolve callers
// 820AF5A8/5DC/634). Its per-frame 640x360 immediates are rewritten at the
// D3D API boundary, gated to callers from this range.
constexpr uint32_t kFxRenderModuleStart = 0x820AF000;
constexpr uint32_t kFxRenderModuleEnd = 0x820B0000;

bool FxRenderModuleCaller(uint64_t guest_lr) {
  const uint32_t lr = uint32_t(guest_lr);
  return lr >= kFxRenderModuleStart && lr < kFxRenderModuleEnd;
}

}  // namespace

// The one fx-module pass that must KEEP its 640x360 viewport: the silhouette
// DOWNSCALER (fixed 2:1 box filter with the x2 baked into its pixel shader;
// lifting its output viewport makes the baked x2 read texels 0..2560 of the
// 1280-wide full-res source -> wraps twice -> the 2x2 quad silhouette that
// haunted rounds 7-21). It is the only fx-module pass rendering into an
// R32_FLOAT target (Xenos k_32_FLOAT = 36): discriminate by the bound RT0
// surface object's format field (packed fmt dword at obj+40, low 6 bits).
static bool FxRt0IsR32Float(uint8_t* base, uint32_t device) {
    if (!device) {
        return false;
    }
    const uint32_t rt0 = PPC_LOAD_U32(device + 12432);
    if (!rt0) {
        return false;
    }
    const uint32_t fmt_word = PPC_LOAD_U32(rt0 + 40);
    return (fmt_word & 0x3F) == 36;  // k_32_FLOAT
}

// D3DDevice_SetViewport (int variant): r4 -> {X, Y, Width, Height, MinZ,
// MaxZ}. The effects module sets 640x360 from immediates each frame; rewrite
// to 1280x720 in the caller's struct (rebuilt every call - safe to mutate).
// 640x360 requests from other callers are left untouched.
PPC_EXTERN_FUNC(__imp__rex_sub_821DD028);
PPC_FUNC_IMPL(rex_sub_821DD028) {
    PPC_FUNC_PROLOGUE();

    const uint32_t vp_ptr = ctx.r4.u32;
    bool patched = false;
    if (FullresEffectsOn() && vp_ptr) {
        const uint32_t w = PPC_LOAD_U32(vp_ptr + 8);
        const uint32_t h = PPC_LOAD_U32(vp_ptr + 12);
        if (w == 640 && h == 360) {
            // fx render module ONLY. The orchestrator wrapper (8234E47C) must
            // NOT be rewritten: it derives viewports from its objects' dims,
            // so doubled objects request 1280x720 by themselves - the 640x360
            // requests left are passes whose targets legitimately stay small
            // (the k_8 mask producers; force-doubling those crops the masks
            // and kills the afterburner/smoke particles - round 8-11 bug).
            // The DOWNSCALER (R32F target) must also keep 640x360: its 2:1
            // box filter has the x2 baked into the shader. The device slot
            // can be stale at SetViewport time (game binds after setting the
            // viewport), so check BOTH the freshly latched SetRenderTarget
            // surface and the device slot.
            bool downscaler = false;
            if (FxRenderModuleCaller(ctx.lr)) {
                const uint32_t latched_rt0 = ac6::backend::LastRt0Bound();
                const bool latch_r32f =
                    latched_rt0 && (PPC_LOAD_U32(latched_rt0 + 40) & 0x3F) == 36;
                const bool slot_r32f = FxRt0IsR32Float(base, ctx.r3.u32);
                downscaler = latch_r32f || slot_r32f;
            }
            patched = FxRenderModuleCaller(ctx.lr) && !downscaler;
            if (patched) {
                PPC_STORE_U32(vp_ptr + 8, 1280);
                PPC_STORE_U32(vp_ptr + 12, 720);
                FX_LOG_ONCE("[AC6 full-res effects] effects module viewport -> 1280x720");
            }
        }
    }

    __imp__rex_sub_821DD028(ctx, base);

    // The orchestrator wrapper may pass a viewport struct living inside a
    // PERSISTENT object rather than a stack temp. The values are consumed
    // synchronously inside the call above; restore them so CPU-side size
    // math reading the same object never sees the doubled dims.
    if (patched) {
        PPC_STORE_U32(vp_ptr + 8, 640);
        PPC_STORE_U32(vp_ptr + 12, 360);
    }
}

namespace {

// The orchestrator's view-builder methods (8234E908 / 8234E9A0) construct
// textures from OBJECT fields: width at [obj+12], height at [obj+16], fmt at
// [obj+20], XG header at obj+28. The stencil resolve destination (e.g. the
// 640x360 texture the aircraft-silhouette mask lands in) is built here.
// Poking the fields at builder entry doubles the header AND any downstream
// size math reading the same fields. Persistent on the object - consistent
// across rebuilds.
// The builders' backing allocation is sized by a CALLER-provided argument
// (r4/r5), not by the XG header they create - doubling only the header made
// a later header-sized memcpy overrun the 640x360-sized buffer (ctd-3, AV in
// VCRUNTIME memcpy at boot). Known 640x360 byte sizes from the texhdr return
// logs: 0xF0000 (8888 tiled) and 0x43800 (k_8 tiled). Only when one of the
// args carries a recognized size is it scaled x4 alongside the dims poke;
// otherwise the object is left untouched (safe boot, evidence logged).
void FxPatchViewObjectDims(PPCContext& ctx, uint8_t* base) {
    if (!FullresEffectsOn()) {
        return;
    }
    const uint32_t obj = ctx.r3.u32;
    if (!obj) {
        return;
    }
    const uint32_t w = PPC_LOAD_U32(obj + 12);
    const uint32_t h = PPC_LOAD_U32(obj + 16);
    if (w != 640 || h != 360) {
        return;
    }
    const uint32_t fmt = PPC_LOAD_U32(obj + 20);
    auto is_known_size = [](uint32_t v) { return v == 0xF0000 || v == 0x43800; };
    if (is_known_size(ctx.r4.u32)) {
        ctx.r4.u64 = uint64_t(ctx.r4.u32) * 4;
    } else if (is_known_size(ctx.r5.u32)) {
        ctx.r5.u64 = uint64_t(ctx.r5.u32) * 4;
    } else {
        // No recognized size argument: leave the object alone rather than
        // enlarge a header over a buffer that stays 640x360-sized.
        return;
    }
    PPC_STORE_U32(obj + 12, 1280);
    PPC_STORE_U32(obj + 16, 720);
    FX_LOG_ONCE("[AC6 full-res effects] orchestrator view objects -> 1280x720");
}

}  // namespace

PPC_EXTERN_FUNC(__imp__rex_sub_8234E908);
PPC_FUNC_IMPL(rex_sub_8234E908) {
    PPC_FUNC_PROLOGUE();
    FxPatchViewObjectDims(ctx, base);
    __imp__rex_sub_8234E908(ctx, base);
}

// The shared texture pool's backing reserve (called at pool init with the
// reserve size in r3; physical alloc placed anywhere by the emulator). Grown by
// kFxTexturePoolBonus so the enlarged buffers stop starving the allocations
// that happen to come last (shadow map, effect buffers).
PPC_EXTERN_FUNC(__imp__rex_sub_8233C280);
PPC_FUNC_IMPL(rex_sub_8233C280) {
    PPC_FUNC_PROLOGUE();

    if (FullresEffectsOn()) {
        ctx.r3.u64 = ctx.r3.u32 + kFxTexturePoolBonus;
        FX_LOG_ONCE("[AC6 full-res effects] texture pool reserve +{} MB",
                    kFxTexturePoolBonus / (1024 * 1024));
    }

    __imp__rex_sub_8233C280(ctx, base);
}

PPC_EXTERN_FUNC(__imp__rex_sub_8234E9A0);
PPC_FUNC_IMPL(rex_sub_8234E9A0) {
    PPC_FUNC_PROLOGUE();
    FxPatchViewObjectDims(ctx, base);
    __imp__rex_sub_8234E9A0(ctx, base);
}

namespace ac6::backend {

// RT0 latch (relocated from the removed recon module): the last surface bound
// through D3DDevice_SetRenderTarget slot 0. See ac6_fullres_effects.h.
namespace {
thread_local uint32_t g_last_rt0_bound = 0;
}  // namespace
void NoteRt0Bound(uint32_t index, uint32_t surface) {
    if (index == 0) {
        g_last_rt0_bound = surface;
    }
}
uint32_t LastRt0Bound() { return g_last_rt0_bound; }

namespace {
// The downscaler-halving flags the next resolve, whose dest
// data base is the silhouette mask - the compositor's fetch at that base is
// cropped to 640x360.
std::atomic<uint32_t> g_downscaler_resolve_pending{0};
std::atomic<uint32_t> g_mask_base{0};

bool SafeCopyU32Array(const uint32_t* host, uint32_t* out, uint32_t count) noexcept {
#if defined(_WIN32)
    __try {
        for (uint32_t i = 0; i < count; ++i) {
            out[i] = host[i];
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    // The source is guest memory, which is an shm mapping: a page can be
    // readable and still unbacked, so a plain dereference raises SIGBUS rather
    // than a recoverable fault. process_vm_readv performs the access in the
    // kernel and reports EFAULT instead of signalling, which is the guard this
    // needs. SEH_TRY was the other candidate, but it recovers by throwing from
    // a signal handler - far heavier than a bounded read, and it would have to
    // be armed on every thread that can reach here.
    iovec local{out, size_t(count) * sizeof(uint32_t)};
    iovec remote{const_cast<uint32_t*>(host), size_t(count) * sizeof(uint32_t)};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) ==
           ssize_t(size_t(count) * sizeof(uint32_t));
#endif
}
}  // namespace

bool FxFixDownscalerDraw(uint64_t ps_ucode_hash, uint32_t* surface_info,
                         uint32_t* vp_xscale, uint32_t* vp_yscale,
                         uint32_t* vp_xoffset, uint32_t* vp_yoffset) {
    if (!FullresEffectsOn()) {
        return false;
    }
    if (ps_ucode_hash != UINT64_C(0x8EE463763E880F35)) {
        return false;
    }
    const uint32_t pitch = *surface_info & 0x3FFF;
    if (pitch <= 640) {
        return false;  // already vanilla-sized (not doubled) - nothing to do
    }
    // Restore this draw's target to 640 wide: halve the surface pitch and the
    // viewport so the 2:1 downscale reads its 1280-wide input exactly once.
    *surface_info = (*surface_info & ~0x3FFFu) | (pitch / 2);
    auto halve = [](uint32_t* reg_bits) {
        float v;
        std::memcpy(&v, reg_bits, sizeof(v));
        v *= 0.5f;
        std::memcpy(reg_bits, &v, sizeof(v));
    };
    halve(vp_xscale);
    halve(vp_yscale);
    halve(vp_xoffset);
    halve(vp_yoffset);
    FX_LOG_ONCE("[AC6 full-res effects] silhouette downscaler kept at its native 640 wide");
    // Flag: the NEXT guest Resolve is this downscaler's - capture its dest as
    // the mask base for the compositor crop.
    g_downscaler_resolve_pending.store(1, std::memory_order_relaxed);
    return true;
}

// Called from the guest D3D Resolve hook with the dest-texture object pointer
// (D3DDevice_Resolve r6). If the downscaler just ran, this resolve writes the
// silhouette mask - record its guest data base for the compositor crop. The
// dest object embeds a GPU fetch constant; its base is dword_1 (offset 32)
// bits 12-31 (like the texture-header dumps).
void FxNoteResolveDest(uint32_t dest_obj, uint8_t* base) {
    if (!FullresEffectsOn()) {
        return;
    }
    if (g_downscaler_resolve_pending.exchange(0, std::memory_order_relaxed) == 0) {
        return;
    }
    if (!dest_obj || dest_obj < 0x1000 || dest_obj >= 0xE0000000u) {
        g_downscaler_resolve_pending.store(1, std::memory_order_relaxed);  // keep looking
        return;
    }
    uint32_t* host = reinterpret_cast<uint32_t*>(base + dest_obj);
    uint32_t words[10];
    if (!SafeCopyU32Array(host, words, 10)) {
        g_downscaler_resolve_pending.store(1, std::memory_order_relaxed);
        return;
    }
    // words are big-endian; fetch dword_1 (base+fmt) at [8], dword_2 (dims) at [9].
    const uint32_t d1 = rex::byte_swap(words[8]);
    const uint32_t d2 = rex::byte_swap(words[9]);
    const uint32_t view_base = (d1 >> 12) << 12;
    const uint32_t phys_base = view_base & 0x1FFFFFFFu;  // strip the 0xA0/0xC0 view
    const uint32_t w = (d2 & 0x1FFF) + 1;
    // The mask is the full-res (~1280 wide) resolve dest; skip small bloom
    // resolves. Keep the flag armed until we find it (or a frame passes).
    if (w >= 1000) {
        g_mask_base.store(phys_base, std::memory_order_relaxed);
    } else {
        g_downscaler_resolve_pending.store(1, std::memory_order_relaxed);  // keep looking
    }
}

void FxCropCompositorMask(uint64_t vs_hash, uint64_t ps_hash, uint32_t* fetch_constants) {
    if (!FullresEffectsOn() ||
        !fetch_constants) {
        return;
    }
    // AUTO-DETECT the mask base (address-independent, updated whenever a draw
    // shows the signature): the silhouette mask is the only texture the cloud
    // draws bind at BOTH fetch slot 2 AND slot 13 with the same base (effect
    // layers each sit at a single slot). Confirmed base 1AF09000 at 2+13.
    // Stored globally so it is then cropped at EVERY draw that reads it -
    // including cloud draws that read the mask WITHOUT the 2==13 signature
    // (those uncropped draws are what regressed the per-draw version).
    static std::atomic<uint32_t> g_auto_mask_base{0};
    {
        const uint32_t* f2 = &fetch_constants[2 * 6];
        const uint32_t* f13 = &fetch_constants[13 * 6];
        if ((f2[0] & 0x3) == 2 && (f13[0] & 0x3) == 2 && (f2[1] & 0x3F) == 6) {
            const uint32_t b2 = ((f2[1] >> 12) << 12) & 0x1FFFFFFFu;
            const uint32_t b13 = ((f13[1] >> 12) << 12) & 0x1FFFFFFFu;
            const uint32_t w2 = (f2[2] & 0x1FFF) + 1;
            if (b2 && b2 == b13 && w2 >= 1000) {
                g_auto_mask_base.store(b2, std::memory_order_relaxed);
            }
        }
    }
    const uint32_t mask_base = g_auto_mask_base.load(std::memory_order_relaxed);
    if (!mask_base) {
        return;
    }
    for (uint32_t f = 0; f < 32; ++f) {
        uint32_t* fc = &fetch_constants[f * 6];
        if ((fc[0] & 0x3) != 2) {
            continue;
        }
        const uint32_t base = ((fc[1] >> 12) << 12) & 0x1FFFFFFFu;
        if (base != mask_base) {
            continue;
        }
        const uint32_t w = (fc[2] & 0x1FFF) + 1;
        if (w >= 1000) {
            // size_2d: width-1 (bits 0..12), height-1 (bits 13..25) -> 640x360.
            fc[2] = (fc[2] & 0xFC000000u) | ((640u - 1u) & 0x1FFF) | (((360u - 1u) & 0x1FFF) << 13);
            FX_LOG_ONCE("[AC6 full-res effects] silhouette mask {:08X} cropped to its "
                        "filled 640x360 quarter",
                        mask_base);
        }
    }
}

// every call) to 1280x720.
void FullresFixResolveRect(PPCContext& ctx, uint8_t* base) {
    if (!FullresEffectsOn()) {
        return;
    }
    if (!FxRenderModuleCaller(ctx.lr)) {
        return;
    }
    // The downscaler's resolve must keep its vanilla extents too.
    if (FxRt0IsR32Float(base, ctx.r3.u32)) {
        return;
    }
    const uint32_t rect_ptr = ctx.r5.u32;
    if (!rect_ptr) {
        return;
    }
    const int32_t left = int32_t(PPC_LOAD_U32(rect_ptr + 0));
    const int32_t top = int32_t(PPC_LOAD_U32(rect_ptr + 4));
    const int32_t right = int32_t(PPC_LOAD_U32(rect_ptr + 8));
    const int32_t bottom = int32_t(PPC_LOAD_U32(rect_ptr + 12));
    if (right - left == 640 && bottom - top == 360) {
        PPC_STORE_U32(rect_ptr + 8, uint32_t(left + 1280));
        PPC_STORE_U32(rect_ptr + 12, uint32_t(top + 720));
        FX_LOG_ONCE("[AC6 full-res effects] effects module resolve rect -> 1280x720");
    }
}

}  // namespace ac6::backend

namespace {

// The two guest functions that build the compositor's effects input textures
// with hardcoded 640x360 dims (they do NOT read the patched registry):
// sub_820AE858 = boot path, XGSetTextureHeader-style views over the id-0x8008
// block; sub_820AEAB8 = its sibling creating two textures. Only calls from
// inside them are rewritten - other callers of the same D3D functions (e.g.
// FMV video planes) keep their dims.
constexpr uint32_t kFxTexBuilderStart = 0x820AE858;
constexpr uint32_t kFxTexBuilderEnd = 0x820AEBD8;  // next function after AB8
// The orchestrator's companion-texture creations for the registry 8009/800A
// buffers (createtex call sites 82346F38 / 823470F4): hardcoded 640x360 args,
// self-sized internal allocation. Must double together with the registry
// entries or copies between the pair overrun (the round-1 boot CTD).
constexpr uint32_t kFxCompanionTexStart = 0x82346000;
constexpr uint32_t kFxCompanionTexEnd = 0x82348000;

bool FxTexPatchWanted(const PPCContext& ctx) {
  if (!FullresEffectsOn() ||
      ctx.r3.u32 != 640 || ctx.r4.u32 != 360) {
    return false;
  }
  const uint32_t lr = uint32_t(ctx.lr);
  // Round 22: back to the round-6 gate for good. The whole 8009/800A +
  // companion campaign chased a mirage: the "silhouette buffer" (8009) is
  // the OUTPUT of a fixed 2:1 downscaler whose input is already full-res in
  // vanilla - nothing on that side ever needed doubling. The 2x2 quads were
  // the downscaler's baked x2 wrapping after OUR module-wide viewport lift
  // reached it (fixed by the R32F-target discriminator, FxRt0IsR32Float).
  return lr >= kFxTexBuilderStart && lr < kFxTexBuilderEnd;
}

}  // namespace

// XGSetTextureHeader-style: (width, height, levels, usage, ..., fmt, hdr...).
PPC_EXTERN_FUNC(__imp__rex_sub_821FBE70);
PPC_FUNC_IMPL(rex_sub_821FBE70) {
    PPC_FUNC_PROLOGUE();

    if (FxTexPatchWanted(ctx)) {
        ctx.r3.u64 = 1280;
        ctx.r4.u64 = 720;
        FX_LOG_ONCE("[AC6 full-res effects] compositor input texture headers -> 1280x720");
    }

    __imp__rex_sub_821FBE70(ctx, base);
}

// CreateTexture-style: (width, height, fmt, levels, pool, ...). Returns the
// texture object in r3; the game does NOT null-check it (a failed allocation
// surfaces later as a null surface deref inside D3D Resolve - the ctd-2
// crash at exe+0xDF3E49). If the 1280x720 creation fails, retry at the
// original 640x360 so the game keeps running (quilted effects instead of a
// CTD) and log the failure loudly.
PPC_EXTERN_FUNC(__imp__rex_sub_821E0EC8);
PPC_FUNC_IMPL(rex_sub_821E0EC8) {
    PPC_FUNC_PROLOGUE();

    const bool patch = FxTexPatchWanted(ctx);
    const uint32_t lr32 = uint32_t(ctx.lr);
    const uint64_t saved_r3 = ctx.r3.u64, saved_r4 = ctx.r4.u64, saved_r5 = ctx.r5.u64,
                   saved_r6 = ctx.r6.u64, saved_r7 = ctx.r7.u64, saved_r8 = ctx.r8.u64,
                   saved_r9 = ctx.r9.u64, saved_r10 = ctx.r10.u64;
    if (patch) {
        ctx.r3.u64 = 1280;
        ctx.r4.u64 = 720;
        // r6 = D3DMULTISAMPLE enum (2 = 4 samples). 1280x720@4x = 2880 tiles
        // forces the tile manager's >2048 spill path, whose physical backing
        // allocation is what actually fails (round-4 telemetry: pool granted
        // 2880, creation still returned null). 1280x720@1x = 720 tiles =
        // exactly the old 640x360@4x footprint - no spill, no extra memory.
        if (ctx.r6.u32 == 2) {
            ctx.r6.u64 = 0;
        }
        FX_LOG_ONCE("[AC6 full-res effects] compositor input textures -> 1280x720, 1xMSAA");
    }

    __imp__rex_sub_821E0EC8(ctx, base);

    if (patch && ctx.r3.u32 == 0) {
        REXLOG_ERROR("[AC6 full-res effects] a 1280x720 compositor texture failed to "
                     "allocate - falling back to 640x360 for it (lr={:08X})", lr32);
        ctx.r3.u64 = saved_r3;
        ctx.r4.u64 = saved_r4;
        ctx.r5.u64 = saved_r5;
        ctx.r6.u64 = saved_r6;
        ctx.r7.u64 = saved_r7;
        ctx.r8.u64 = saved_r8;
        ctx.r9.u64 = saved_r9;
        ctx.r10.u64 = saved_r10;
        __imp__rex_sub_821E0EC8(ctx, base);
        REXLOG_ERROR("[AC6 full-res effects] 640x360 fallback returned {:08X}", ctx.r3.u32);
    }
}
