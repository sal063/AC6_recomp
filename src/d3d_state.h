#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace ac6::d3d {

inline constexpr uint32_t kMaxRenderTargets = 5;
inline constexpr uint32_t kMaxTextures = 16;
inline constexpr uint32_t kMaxStreams = 16;
inline constexpr uint32_t kMaxSamplers = 16;
inline constexpr uint32_t kMaxFetchConstants = 32;
inline constexpr uint32_t kMaxClearRectsPerRecord = 8;

struct DrawStats {
    std::atomic<uint32_t> draw_calls{0};
    std::atomic<uint32_t> draw_calls_indexed{0};
    std::atomic<uint32_t> draw_calls_indexed_shared{0};
    std::atomic<uint32_t> draw_calls_primitive{0};
    std::atomic<uint64_t> total_indices{0};
    std::atomic<uint64_t> total_vertices{0};
    std::atomic<uint32_t> set_texture_calls{0};
    std::atomic<uint32_t> set_render_target_calls{0};
    std::atomic<uint32_t> set_depth_stencil_calls{0};
    std::atomic<uint32_t> set_vertex_decl_calls{0};
    std::atomic<uint32_t> set_index_buffer_calls{0};
    std::atomic<uint32_t> set_stream_source_calls{0};
    std::atomic<uint32_t> set_viewport_calls{0};
    std::atomic<uint32_t> set_sampler_state_calls{0};
    std::atomic<uint32_t> set_texture_fetch_calls{0};
    std::atomic<uint32_t> clear_calls{0};
    std::atomic<uint32_t> resolve_calls{0};

    void Reset() {
        draw_calls.store(0, std::memory_order_relaxed);
        draw_calls_indexed.store(0, std::memory_order_relaxed);
        draw_calls_indexed_shared.store(0, std::memory_order_relaxed);
        draw_calls_primitive.store(0, std::memory_order_relaxed);
        total_indices.store(0, std::memory_order_relaxed);
        total_vertices.store(0, std::memory_order_relaxed);
        set_texture_calls.store(0, std::memory_order_relaxed);
        set_render_target_calls.store(0, std::memory_order_relaxed);
        set_depth_stencil_calls.store(0, std::memory_order_relaxed);
        set_vertex_decl_calls.store(0, std::memory_order_relaxed);
        set_index_buffer_calls.store(0, std::memory_order_relaxed);
        set_stream_source_calls.store(0, std::memory_order_relaxed);
        set_viewport_calls.store(0, std::memory_order_relaxed);
        set_sampler_state_calls.store(0, std::memory_order_relaxed);
        set_texture_fetch_calls.store(0, std::memory_order_relaxed);
        clear_calls.store(0, std::memory_order_relaxed);
        resolve_calls.store(0, std::memory_order_relaxed);
    }
};

struct DrawStatsSnapshot {
    uint32_t draw_calls;
    uint32_t draw_calls_indexed;
    uint32_t draw_calls_indexed_shared;
    uint32_t draw_calls_primitive;
    uint64_t total_indices;
    uint64_t total_vertices;
    uint32_t set_texture_calls;
    uint32_t set_render_target_calls;
    uint32_t set_depth_stencil_calls;
    uint32_t set_vertex_decl_calls;
    uint32_t set_index_buffer_calls;
    uint32_t set_stream_source_calls;
    uint32_t set_viewport_calls;
    uint32_t set_sampler_state_calls;
    uint32_t set_texture_fetch_calls;
    uint32_t clear_calls;
    uint32_t resolve_calls;
};

enum class DrawCallKind : uint8_t {
    kIndexed,
    kIndexedShared,
    kPrimitive,
};

struct StreamBinding {
    uint32_t buffer{0};       // Guest address of D3DVertexBuffer
    uint32_t offset{0};       // Offset in bytes
    uint32_t stride{0};       // Vertex stride in bytes
};

struct SamplerBinding {
    uint32_t mag_filter{0};   // D3DTEXTUREFILTERTYPE
    uint32_t min_filter{0};   // Sampler state A
    uint32_t mip_filter{0};   // Sampler state B
    uint32_t mip_level{0};    // Max mip level
    uint32_t border_color{0}; // Sampler state C
};

// All values are guest addresses into PPC address space unless noted.
struct ShadowState {
    uint32_t device{0};
    std::array<uint32_t, kMaxRenderTargets> render_targets{};
    uint32_t depth_stencil{0};
    std::array<uint32_t, kMaxTextures> textures{};
    uint32_t vertex_declaration{0};
    uint32_t index_buffer{0};
    std::array<StreamBinding, kMaxStreams> streams{};
    std::array<SamplerBinding, kMaxSamplers> samplers{};
    std::array<uint32_t, kMaxFetchConstants> texture_fetch_ptrs{};
    uint32_t shader_gpr_alloc{0};
    uint64_t vertex_fetch_layout_signature{0};
    uint64_t texture_fetch_layout_signature{0};
    uint64_t resource_binding_signature{0};

    struct {
        uint32_t x{0};
        uint32_t y{0};
        uint32_t width{0};
        uint32_t height{0};
    } viewport;
};

struct DrawCallRecord {
    uint32_t sequence{0};
    DrawCallKind kind{DrawCallKind::kIndexed};
    uint32_t primitive_type{0};
    uint32_t start{0};
    uint32_t count{0};
    uint32_t flags{0};
    ShadowState shadow_state{};
};

struct ClearRect {
    uint32_t left{0};
    uint32_t top{0};
    uint32_t right{0};
    uint32_t bottom{0};
};

struct ClearRecord {
    uint32_t sequence{0};
    uint32_t rect_count{0};
    uint32_t captured_rect_count{0};
    uint32_t flags{0};
    uint32_t color{0};
    uint32_t stencil{0};
    float depth{1.0f};
    std::array<ClearRect, kMaxClearRectsPerRecord> rects{};
    ShadowState shadow_state{};
};

struct ResolveRecord {
    uint32_t sequence{0};
    ShadowState shadow_state{};
};

struct FrameCaptureSnapshot {
    uint64_t frame_index{0};
    DrawStatsSnapshot stats{};
    ShadowState frame_end_shadow{};
    std::vector<DrawCallRecord> draws;
    std::vector<ClearRecord> clears;
    std::vector<ResolveRecord> resolves;
};

struct FrameCaptureSummary {
    bool capture_enabled{false};
    bool record_signature_valid{false};
    uint64_t frame_index{0};
    uint64_t record_signature{0};
    uint32_t draw_count{0};
    uint32_t clear_count{0};
    uint32_t resolve_count{0};
    uint32_t indexed_draw_count{0};
    uint32_t indexed_shared_draw_count{0};
    uint32_t primitive_draw_count{0};
    /// Per-frame guest D3D draw counters (same window as this capture; cleared each frame boundary).
    DrawStatsSnapshot frame_stats{};
    /// Histogram of `primitive_type` on all captured draws (D3DPRIMITIVETYPE: 1=point list … 6=fan).
    uint32_t topology_pointlist{0};
    uint32_t topology_linelist{0};
    uint32_t topology_linestrip{0};
    uint32_t topology_trianglelist{0};
    uint32_t topology_trianglestrip{0};
    uint32_t topology_trianglefan{0};
    uint32_t topology_other{0};
    uint32_t unique_rt0_count{0};
    uint32_t rt0_switch_count{0};
    uint32_t frame_end_render_target_count{0};
    uint32_t frame_end_texture_count{0};
    uint32_t frame_end_stream_count{0};
    uint32_t frame_end_sampler_count{0};
    uint32_t frame_end_texture_fetch_count{0};
    uint32_t frame_end_render_target_0{0};
    uint32_t frame_end_depth_stencil{0};
    uint32_t first_draw_render_target_0{0};
    uint32_t last_draw_render_target_0{0};
    uint32_t frame_end_viewport_width{0};
    uint32_t frame_end_viewport_height{0};
    uint32_t last_draw_primitive_type{0};
    uint32_t last_draw_count{0};
    uint32_t last_draw_flags{0};
};

}  // namespace ac6::d3d
