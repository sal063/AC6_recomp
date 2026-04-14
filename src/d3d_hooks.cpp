#include "d3d_hooks.h"

#include <algorithm>
#include <shared_mutex>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(ac6_d3d_trace, false, "AC6/Render",
                    "Log every D3D device state change and draw call");
REXCVAR_DEFINE_BOOL(ac6_render_capture, false, "AC6/Render",
                    "Capture per-frame draw, clear, and resolve records for the native renderer");

namespace {
const rex::LogCategoryId kLogGPU = rex::log::GPU;
}  // namespace

namespace {

std::shared_mutex g_shadow_mutex;
ac6::d3d::ShadowState g_shadow{};

ac6::d3d::DrawStats g_live_stats{};

std::shared_mutex g_snapshot_mutex;
ac6::d3d::DrawStatsSnapshot g_snapshot{};

std::shared_mutex g_capture_mutex;
ac6::d3d::FrameCaptureSnapshot g_capture_snapshot{};
ac6::d3d::FrameCaptureSummary g_capture_summary{};
uint64_t g_capture_live_frame_index = 1;
uint32_t g_capture_live_sequence = 0;
std::vector<ac6::d3d::DrawCallRecord> g_live_draws;
std::vector<ac6::d3d::ClearRecord> g_live_clears;
std::vector<ac6::d3d::ResolveRecord> g_live_resolves;

template <typename T, size_t N>
uint32_t CountNonZero(const std::array<T, N>& values) {
    uint32_t count = 0;
    for (const T& value : values) {
        if (value) {
            ++count;
        }
    }
    return count;
}

uint32_t CountNonZeroStreams(const std::array<ac6::d3d::StreamBinding, ac6::d3d::kMaxStreams>& streams) {
    uint32_t count = 0;
    for (const auto& stream : streams) {
        if (stream.buffer) {
            ++count;
        }
    }
    return count;
}

uint32_t CountNonZeroSamplers(
    const std::array<ac6::d3d::SamplerBinding, ac6::d3d::kMaxSamplers>& samplers) {
    uint32_t count = 0;
    for (const auto& sampler : samplers) {
        if (sampler.mag_filter || sampler.min_filter || sampler.mip_filter || sampler.mip_level ||
            sampler.border_color) {
            ++count;
        }
    }
    return count;
}

void HashU32(uint64_t& hash, uint32_t value) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    hash ^= value;
    hash *= kFnvPrime;
}

void HashDrawRecord(uint64_t& hash, const ac6::d3d::DrawCallRecord& draw) {
    HashU32(hash, static_cast<uint32_t>(draw.kind));
    HashU32(hash, draw.primitive_type);
    HashU32(hash, draw.start);
    HashU32(hash, draw.count);
    HashU32(hash, draw.flags);
    HashU32(hash, draw.shadow_state.render_targets[0]);
    HashU32(hash, draw.shadow_state.depth_stencil);
    HashU32(hash, draw.shadow_state.viewport.width);
    HashU32(hash, draw.shadow_state.viewport.height);
}

ac6::d3d::FrameCaptureSummary MakeFrameCaptureSummary(
    const ac6::d3d::FrameCaptureSnapshot& frame_capture) {
    ac6::d3d::FrameCaptureSummary summary;
    summary.capture_enabled = REXCVAR_GET(ac6_render_capture);
    summary.frame_index = frame_capture.frame_index;
    summary.draw_count = static_cast<uint32_t>(frame_capture.draws.size());
    summary.clear_count = static_cast<uint32_t>(frame_capture.clears.size());
    summary.resolve_count = static_cast<uint32_t>(frame_capture.resolves.size());
    summary.frame_end_render_target_count =
        CountNonZero(frame_capture.frame_end_shadow.render_targets);
    summary.frame_end_texture_count = CountNonZero(frame_capture.frame_end_shadow.textures);
    summary.frame_end_stream_count = CountNonZeroStreams(frame_capture.frame_end_shadow.streams);
    summary.frame_end_sampler_count = CountNonZeroSamplers(frame_capture.frame_end_shadow.samplers);
    summary.frame_end_texture_fetch_count =
        CountNonZero(frame_capture.frame_end_shadow.texture_fetch_ptrs);
    summary.frame_end_render_target_0 = frame_capture.frame_end_shadow.render_targets[0];
    summary.frame_end_depth_stencil = frame_capture.frame_end_shadow.depth_stencil;
    summary.frame_end_viewport_width = frame_capture.frame_end_shadow.viewport.width;
    summary.frame_end_viewport_height = frame_capture.frame_end_shadow.viewport.height;
    if (!frame_capture.draws.empty()) {
        summary.first_draw_render_target_0 =
            frame_capture.draws.front().shadow_state.render_targets[0];
        summary.last_draw_render_target_0 =
            frame_capture.draws.back().shadow_state.render_targets[0];
        const auto& last_draw = frame_capture.draws.back();
        summary.last_draw_primitive_type = last_draw.primitive_type;
        summary.last_draw_count = last_draw.count;
        summary.last_draw_flags = last_draw.flags;
    }
    if (summary.capture_enabled && !frame_capture.draws.empty()) {
        constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
        uint64_t signature = kFnvOffsetBasis;
        std::vector<uint32_t> unique_rt0s;
        uint32_t previous_rt0 = frame_capture.draws.front().shadow_state.render_targets[0];
        for (const auto& draw : frame_capture.draws) {
            switch (draw.kind) {
                case ac6::d3d::DrawCallKind::kIndexed:
                    ++summary.indexed_draw_count;
                    break;
                case ac6::d3d::DrawCallKind::kIndexedShared:
                    ++summary.indexed_shared_draw_count;
                    break;
                case ac6::d3d::DrawCallKind::kPrimitive:
                    ++summary.primitive_draw_count;
                    break;
            }
            uint32_t rt0 = draw.shadow_state.render_targets[0];
            if (std::find(unique_rt0s.begin(), unique_rt0s.end(), rt0) == unique_rt0s.end()) {
                unique_rt0s.push_back(rt0);
            }
            if (rt0 != previous_rt0) {
                ++summary.rt0_switch_count;
                previous_rt0 = rt0;
            }
            HashDrawRecord(signature, draw);
        }
        summary.unique_rt0_count = static_cast<uint32_t>(unique_rt0s.size());
        summary.record_signature = signature;
        summary.record_signature_valid = true;
    }
    return summary;
}

void RememberDevice(uint32_t device) {
    std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
    g_shadow.device = device;
}

ac6::d3d::ShadowState SnapshotShadowState(uint32_t device) {
    std::shared_lock<std::shared_mutex> lock(g_shadow_mutex);
    ac6::d3d::ShadowState shadow = g_shadow;
    if (device != 0) {
        shadow.device = device;
    }
    return shadow;
}

ac6::d3d::DrawStatsSnapshot SnapshotDrawStats() {
    return ac6::d3d::DrawStatsSnapshot{
        g_live_stats.draw_calls.load(std::memory_order_relaxed),
        g_live_stats.draw_calls_indexed.load(std::memory_order_relaxed),
        g_live_stats.draw_calls_indexed_shared.load(std::memory_order_relaxed),
        g_live_stats.draw_calls_primitive.load(std::memory_order_relaxed),
        g_live_stats.total_indices.load(std::memory_order_relaxed),
        g_live_stats.total_vertices.load(std::memory_order_relaxed),
        g_live_stats.set_texture_calls.load(std::memory_order_relaxed),
        g_live_stats.set_render_target_calls.load(std::memory_order_relaxed),
        g_live_stats.set_depth_stencil_calls.load(std::memory_order_relaxed),
        g_live_stats.set_vertex_decl_calls.load(std::memory_order_relaxed),
        g_live_stats.set_index_buffer_calls.load(std::memory_order_relaxed),
        g_live_stats.set_stream_source_calls.load(std::memory_order_relaxed),
        g_live_stats.set_viewport_calls.load(std::memory_order_relaxed),
        g_live_stats.set_sampler_state_calls.load(std::memory_order_relaxed),
        g_live_stats.set_texture_fetch_calls.load(std::memory_order_relaxed),
        g_live_stats.clear_calls.load(std::memory_order_relaxed),
        g_live_stats.resolve_calls.load(std::memory_order_relaxed),
    };
}

void CaptureDrawCall(ac6::d3d::DrawCallKind kind, uint32_t device, uint32_t primitive_type,
                     uint32_t start, uint32_t count, uint32_t flags) {
    if (!REXCVAR_GET(ac6_render_capture)) {
        return;
    }

    ac6::d3d::DrawCallRecord record;
    record.kind = kind;
    record.primitive_type = primitive_type;
    record.start = start;
    record.count = count;
    record.flags = flags;
    record.shadow_state = SnapshotShadowState(device);

    std::unique_lock<std::shared_mutex> lock(g_capture_mutex);
    record.sequence = ++g_capture_live_sequence;
    g_live_draws.push_back(std::move(record));
}

void CaptureClear(uint8_t* base, uint32_t device, uint32_t rect_count, uint32_t rects_ptr,
                  uint32_t flags, uint32_t color, uint32_t stencil, float depth) {
    if (!REXCVAR_GET(ac6_render_capture)) {
        return;
    }

    ac6::d3d::ClearRecord record;
    record.rect_count = rect_count;
    record.flags = flags;
    record.color = color;
    record.stencil = stencil;
    record.depth = depth;
    record.shadow_state = SnapshotShadowState(device);
    if (rects_ptr && rect_count) {
        const uint32_t captured_rect_count =
            std::min<uint32_t>(rect_count, ac6::d3d::kMaxClearRectsPerRecord);
        record.captured_rect_count = captured_rect_count;
        for (uint32_t i = 0; i < captured_rect_count; ++i) {
            const uint32_t rect_ptr = rects_ptr + i * 16;
            record.rects[i].left = PPC_LOAD_U32(rect_ptr + 0);
            record.rects[i].top = PPC_LOAD_U32(rect_ptr + 4);
            record.rects[i].right = PPC_LOAD_U32(rect_ptr + 8);
            record.rects[i].bottom = PPC_LOAD_U32(rect_ptr + 12);
        }
    }

    std::unique_lock<std::shared_mutex> lock(g_capture_mutex);
    record.sequence = ++g_capture_live_sequence;
    g_live_clears.push_back(std::move(record));
}

void CaptureResolve(uint32_t device) {
    if (!REXCVAR_GET(ac6_render_capture)) {
        return;
    }

    ac6::d3d::ResolveRecord record;
    record.shadow_state = SnapshotShadowState(device);

    std::unique_lock<std::shared_mutex> lock(g_capture_mutex);
    record.sequence = ++g_capture_live_sequence;
    g_live_resolves.push_back(std::move(record));
}

}  // namespace

PPC_EXTERN_FUNC(__imp__rex_sub_821DEF18);  // DrawIndexedVertices
PPC_EXTERN_FUNC(__imp__rex_sub_821DF300);  // DrawIndexedVertices_Shared
PPC_EXTERN_FUNC(__imp__rex_sub_821DEA48);  // DrawPrimitive
PPC_EXTERN_FUNC(__imp__rex_sub_821DD0A8);  // SetTexture
PPC_EXTERN_FUNC(__imp__rex_sub_821D95C8);  // SetRenderTarget
PPC_EXTERN_FUNC(__imp__rex_sub_821D9D38);  // SetDepthStencil
PPC_EXTERN_FUNC(__imp__rex_sub_821DE7D0);  // SetVertexDeclaration
PPC_EXTERN_FUNC(__imp__rex_sub_821DD1C8);  // SetIndexBuffer
PPC_EXTERN_FUNC(__imp__rex_sub_821DA698);  // SetViewport
PPC_EXTERN_FUNC(__imp__rex_sub_821DC538);  // SetStreamSource
PPC_EXTERN_FUNC(__imp__rex_sub_821DC6C8);  // SetSamplerState_MagFilter
PPC_EXTERN_FUNC(__imp__rex_sub_821DC9C0);  // SetSamplerState_C
PPC_EXTERN_FUNC(__imp__rex_sub_821DCA68);  // SetSamplerState_B
PPC_EXTERN_FUNC(__imp__rex_sub_821DCB08);  // SetSamplerState_MipLevel
PPC_EXTERN_FUNC(__imp__rex_sub_821DCB88);  // SetSamplerState_A
PPC_EXTERN_FUNC(__imp__rex_sub_821DBAF8);  // SetShaderGPRAlloc
PPC_EXTERN_FUNC(__imp__rex_sub_821E2380);  // Clear
PPC_EXTERN_FUNC(__imp__rex_sub_821E10C8);  // SetTextureFetchConstant
PPC_EXTERN_FUNC(__imp__rex_sub_821E2BB8);  // Resolve

// D3DDevice_DrawIndexedVertices (0x821DEF18)
PPC_FUNC_IMPL(rex_sub_821DEF18) {
    PPC_FUNC_PROLOGUE();

    uint32_t index_count = ctx.r6.u32;
    RememberDevice(ctx.r3.u32);

    g_live_stats.draw_calls.fetch_add(1, std::memory_order_relaxed);
    g_live_stats.draw_calls_indexed.fetch_add(1, std::memory_order_relaxed);
    g_live_stats.total_indices.fetch_add(index_count, std::memory_order_relaxed);
    CaptureDrawCall(ac6::d3d::DrawCallKind::kIndexed, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
                    index_count, 0);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "DrawIndexedVertices: prim={} start={} count={}",
            ctx.r4.u32, ctx.r5.u32, index_count);
    }

    __imp__rex_sub_821DEF18(ctx, base);
}

// D3DDevice_DrawIndexedVertices_Shared (0x821DF300)
PPC_FUNC_IMPL(rex_sub_821DF300) {
    PPC_FUNC_PROLOGUE();

    uint32_t index_count = ctx.r7.u32;
    RememberDevice(ctx.r3.u32);

    g_live_stats.draw_calls.fetch_add(1, std::memory_order_relaxed);
    g_live_stats.draw_calls_indexed_shared.fetch_add(1, std::memory_order_relaxed);
    g_live_stats.total_indices.fetch_add(index_count, std::memory_order_relaxed);
    CaptureDrawCall(ac6::d3d::DrawCallKind::kIndexedShared, ctx.r3.u32, ctx.r4.u32, ctx.r6.u32,
                    index_count, ctx.r5.u32);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "DrawIndexedVertices_Shared: prim={} flags={} start={} count={}",
            ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, index_count);
    }

    __imp__rex_sub_821DF300(ctx, base);
}

// D3DDevice_SetTexture (0x821DD0A8)
PPC_FUNC_IMPL(rex_sub_821DD0A8) {
    PPC_FUNC_PROLOGUE();

    uint32_t slot = ctx.r4.u32;
    uint32_t texture_ptr = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (slot < ac6::d3d::kMaxTextures) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.textures[slot] = texture_ptr;
    }
    g_live_stats.set_texture_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetTexture: slot={} texture=0x{:08X}",
            slot, texture_ptr);
    }

    __imp__rex_sub_821DD0A8(ctx, base);
}

// D3DDevice_SetRenderTarget (0x821D95C8)
PPC_FUNC_IMPL(rex_sub_821D95C8) {
    PPC_FUNC_PROLOGUE();

    uint32_t index = ctx.r4.u32;
    uint32_t surface = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (index < ac6::d3d::kMaxRenderTargets) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.render_targets[index] = surface;
    }
    g_live_stats.set_render_target_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetRenderTarget: index={} surface=0x{:08X}",
            index, surface);
    }

    __imp__rex_sub_821D95C8(ctx, base);
}

// D3DDevice_SetDepthStencil (0x821D9D38)
PPC_FUNC_IMPL(rex_sub_821D9D38) {
    PPC_FUNC_PROLOGUE();

    uint32_t surface = ctx.r4.u32;
    RememberDevice(ctx.r3.u32);
    {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.depth_stencil = surface;
    }
    g_live_stats.set_depth_stencil_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetDepthStencil: surface=0x{:08X}", surface);
    }

    __imp__rex_sub_821D9D38(ctx, base);
}

// D3DDevice_SetVertexDeclaration (0x821DE7D0)
PPC_FUNC_IMPL(rex_sub_821DE7D0) {
    PPC_FUNC_PROLOGUE();

    uint32_t decl = ctx.r4.u32;
    RememberDevice(ctx.r3.u32);
    {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.vertex_declaration = decl;
    }
    g_live_stats.set_vertex_decl_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetVertexDeclaration: decl=0x{:08X}", decl);
    }

    __imp__rex_sub_821DE7D0(ctx, base);
}

// D3DDevice_SetIndexBuffer (0x821DD1C8)
PPC_FUNC_IMPL(rex_sub_821DD1C8) {
    PPC_FUNC_PROLOGUE();

    uint32_t buffer = ctx.r4.u32;
    RememberDevice(ctx.r3.u32);
    {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.index_buffer = buffer;
    }
    g_live_stats.set_index_buffer_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetIndexBuffer: buffer=0x{:08X}", buffer);
    }

    __imp__rex_sub_821DD1C8(ctx, base);
}

// D3DDevice_SetViewport (0x821DA698)
PPC_FUNC_IMPL(rex_sub_821DA698) {
    PPC_FUNC_PROLOGUE();

    RememberDevice(ctx.r3.u32);
    {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.viewport.x      = ctx.r4.u32;
        g_shadow.viewport.y      = ctx.r5.u32;
        g_shadow.viewport.width  = ctx.r6.u32;
        g_shadow.viewport.height = ctx.r7.u32;
    }
    g_live_stats.set_viewport_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetViewport: {}x{} at ({},{})",
            g_shadow.viewport.width, g_shadow.viewport.height,
            g_shadow.viewport.x, g_shadow.viewport.y);
    }

    __imp__rex_sub_821DA698(ctx, base);
}

// D3DDevice_Resolve (0x821E2BB8)
PPC_FUNC_IMPL(rex_sub_821E2BB8) {
    PPC_FUNC_PROLOGUE();

    RememberDevice(ctx.r3.u32);
    g_live_stats.resolve_calls.fetch_add(1, std::memory_order_relaxed);
    CaptureResolve(ctx.r3.u32);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU, "Resolve");
    }

    __imp__rex_sub_821E2BB8(ctx, base);
}

// D3DDevice_DrawPrimitive (0x821DEA48)
// r3=pDevice, r4=PrimitiveType, r5=VertexCount
PPC_FUNC_IMPL(rex_sub_821DEA48) {
    PPC_FUNC_PROLOGUE();

    uint32_t prim_type = ctx.r4.u32;
    uint32_t vertex_count = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    g_live_stats.draw_calls.fetch_add(1, std::memory_order_relaxed);
    g_live_stats.draw_calls_primitive.fetch_add(1, std::memory_order_relaxed);
    g_live_stats.total_vertices.fetch_add(vertex_count, std::memory_order_relaxed);
    CaptureDrawCall(ac6::d3d::DrawCallKind::kPrimitive, ctx.r3.u32, prim_type, 0, vertex_count,
                    0);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "DrawPrimitive: prim={} count={}",
            prim_type, vertex_count);
    }

    __imp__rex_sub_821DEA48(ctx, base);
}

// D3DDevice_SetStreamSource (0x821DC538)
// r3=pDevice, r4=StreamNumber, r5=pStreamData, r6=OffsetInBytes, r7=Stride
PPC_FUNC_IMPL(rex_sub_821DC538) {
    PPC_FUNC_PROLOGUE();

    uint32_t stream = ctx.r4.u32;
    uint32_t buffer = ctx.r5.u32;
    uint32_t offset = ctx.r6.u32;
    uint32_t stride = ctx.r7.u32;
    RememberDevice(ctx.r3.u32);

    if (stream < ac6::d3d::kMaxStreams) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.streams[stream].buffer = buffer;
        g_shadow.streams[stream].offset = offset;
        g_shadow.streams[stream].stride = stride;
    }
    g_live_stats.set_stream_source_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetStreamSource: stream={} buffer=0x{:08X} offset={} stride={}",
            stream, buffer, offset, stride);
    }

    __imp__rex_sub_821DC538(ctx, base);
}

// D3DDevice_SetSamplerState_MagFilter (0x821DC6C8)
// r3=pDevice, r4=Sampler, r5=Value
PPC_FUNC_IMPL(rex_sub_821DC6C8) {
    PPC_FUNC_PROLOGUE();

    uint32_t sampler = ctx.r4.u32;
    uint32_t value = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (sampler < ac6::d3d::kMaxSamplers) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.samplers[sampler].mag_filter = value;
    }
    g_live_stats.set_sampler_state_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetSamplerState_MagFilter: sampler={} value={}",
            sampler, value);
    }

    __imp__rex_sub_821DC6C8(ctx, base);
}

// D3DDevice_SetSamplerState_A (0x821DCB88) — min filter
// r3=pDevice, r4=Sampler, r5=Value
PPC_FUNC_IMPL(rex_sub_821DCB88) {
    PPC_FUNC_PROLOGUE();

    uint32_t sampler = ctx.r4.u32;
    uint32_t value = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (sampler < ac6::d3d::kMaxSamplers) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.samplers[sampler].min_filter = value;
    }
    g_live_stats.set_sampler_state_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetSamplerState_A: sampler={} value={}",
            sampler, value);
    }

    __imp__rex_sub_821DCB88(ctx, base);
}

// D3DDevice_SetSamplerState_B (0x821DCA68) — mip filter
// r3=pDevice, r4=Sampler, r5=Value
PPC_FUNC_IMPL(rex_sub_821DCA68) {
    PPC_FUNC_PROLOGUE();

    uint32_t sampler = ctx.r4.u32;
    uint32_t value = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (sampler < ac6::d3d::kMaxSamplers) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.samplers[sampler].mip_filter = value;
    }
    g_live_stats.set_sampler_state_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetSamplerState_B: sampler={} value={}",
            sampler, value);
    }

    __imp__rex_sub_821DCA68(ctx, base);
}

// D3DDevice_SetSamplerState_C (0x821DC9C0) — border color
// r3=pDevice, r4=Sampler, r5=Value
PPC_FUNC_IMPL(rex_sub_821DC9C0) {
    PPC_FUNC_PROLOGUE();

    uint32_t sampler = ctx.r4.u32;
    uint32_t value = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (sampler < ac6::d3d::kMaxSamplers) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.samplers[sampler].border_color = value;
    }
    g_live_stats.set_sampler_state_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetSamplerState_C: sampler={} value={}",
            sampler, value);
    }

    __imp__rex_sub_821DC9C0(ctx, base);
}

// D3DDevice_SetSamplerState_MipLevel (0x821DCB08)
// r3=pDevice, r4=Sampler, r5=Value
PPC_FUNC_IMPL(rex_sub_821DCB08) {
    PPC_FUNC_PROLOGUE();

    uint32_t sampler = ctx.r4.u32;
    uint32_t value = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (sampler < ac6::d3d::kMaxSamplers) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.samplers[sampler].mip_level = value;
    }
    g_live_stats.set_sampler_state_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetSamplerState_MipLevel: sampler={} value={}",
            sampler, value);
    }

    __imp__rex_sub_821DCB08(ctx, base);
}

// D3DDevice_SetShaderGPRAlloc (0x821DBAF8)
// r3=pDevice, r4=Flags
PPC_FUNC_IMPL(rex_sub_821DBAF8) {
    PPC_FUNC_PROLOGUE();

    uint32_t flags = ctx.r4.u32;
    RememberDevice(ctx.r3.u32);
    {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.shader_gpr_alloc = flags;
    }

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetShaderGPRAlloc: flags=0x{:08X}", flags);
    }

    __imp__rex_sub_821DBAF8(ctx, base);
}

// D3DDevice_Clear (0x821E2380)
// r3=pDevice, r4=Count, r5=pRects, r6=Flags, r7=Color, f1=Z, r8=Stencil, r9=EDRAMClear
PPC_FUNC_IMPL(rex_sub_821E2380) {
    PPC_FUNC_PROLOGUE();

    RememberDevice(ctx.r3.u32);
    g_live_stats.clear_calls.fetch_add(1, std::memory_order_relaxed);
    CaptureClear(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32,
                 static_cast<float>(ctx.f1.f64));

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "Clear: count={} flags=0x{:X} color=0x{:08X} stencil={}",
            ctx.r4.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
    }

    __imp__rex_sub_821E2380(ctx, base);
}

// D3DDevice_SetTextureFetchConstant (0x821E10C8)
// r3=pDevice, r4=Register, r5=pTexture
PPC_FUNC_IMPL(rex_sub_821E10C8) {
    PPC_FUNC_PROLOGUE();

    uint32_t reg = ctx.r4.u32;
    uint32_t texture = ctx.r5.u32;
    RememberDevice(ctx.r3.u32);

    if (reg < ac6::d3d::kMaxFetchConstants) {
        std::unique_lock<std::shared_mutex> lock(g_shadow_mutex);
        g_shadow.texture_fetch_ptrs[reg] = texture;
    }
    g_live_stats.set_texture_fetch_calls.fetch_add(1, std::memory_order_relaxed);

    if (REXCVAR_GET(ac6_d3d_trace)) {
        REXLOG_CAT_TRACE(kLogGPU,
            "SetTextureFetchConstant: reg={} texture=0x{:08X}",
            reg, texture);
    }

    __imp__rex_sub_821E10C8(ctx, base);
}

namespace ac6::d3d {

void OnFrameBoundary() {
    ac6::d3d::DrawStatsSnapshot draw_stats = SnapshotDrawStats();

    std::unique_lock<std::shared_mutex> lock(g_snapshot_mutex);
    g_snapshot = draw_stats;
    lock.unlock();

    ac6::d3d::FrameCaptureSnapshot frame_capture;
    frame_capture.stats = draw_stats;
    frame_capture.frame_end_shadow = SnapshotShadowState(0);

    std::unique_lock<std::shared_mutex> capture_lock(g_capture_mutex);
    frame_capture.frame_index = g_capture_live_frame_index++;
    frame_capture.draws.swap(g_live_draws);
    frame_capture.clears.swap(g_live_clears);
    frame_capture.resolves.swap(g_live_resolves);
    g_capture_live_sequence = 0;
    g_capture_summary = MakeFrameCaptureSummary(frame_capture);
    g_capture_snapshot = std::move(frame_capture);

    g_live_stats.Reset();
}

DrawStatsSnapshot GetDrawStats() {
    std::shared_lock<std::shared_mutex> lock(g_snapshot_mutex);
    return g_snapshot;
}

FrameCaptureSnapshot GetFrameCapture() {
    std::shared_lock<std::shared_mutex> lock(g_capture_mutex);
    return g_capture_snapshot;
}

FrameCaptureSummary GetFrameCaptureSummary() {
    std::shared_lock<std::shared_mutex> lock(g_capture_mutex);
    return g_capture_summary;
}

ShadowState GetShadowState() {
    std::shared_lock<std::shared_mutex> lock(g_shadow_mutex);
    return g_shadow;
}

}  // namespace ac6::d3d
