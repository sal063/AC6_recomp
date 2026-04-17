#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ac6::d3d {

inline constexpr uint32_t kMaxRenderTargets = 5;
inline constexpr uint32_t kMaxTextures = 16;
inline constexpr uint32_t kMaxStreams = 16;
inline constexpr uint32_t kMaxSamplers = 16;
inline constexpr uint32_t kMaxFetchConstants = 32;

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

    struct {
        uint32_t x{0};
        uint32_t y{0};
        uint32_t width{0};
        uint32_t height{0};
    } viewport;
};

}  // namespace ac6::d3d
