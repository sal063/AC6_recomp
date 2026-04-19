#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "d3d12_shader_manager.h"

#include <cstring>

#include <rex/logging.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace ac6::renderer {

// ---------------------------------------------------------------------------
// Embedded HLSL shaders (compiled inline at runtime, no external files)
// ---------------------------------------------------------------------------

// Generic vertex-pulling shader. It reads stream 0 directly as a raw byte
// buffer, byte-swaps guest big-endian dwords, and interprets the first float3
// plus an optional fourth component as position data. Pre-transformed menu / UI vertices are projected from the
// captured viewport into clip space; already-transformed clip-space vertices
// are passed through.
static constexpr const char kPassthroughVS[] = R"HLSL(
cbuffer DrawConstants : register(b0) {
    uint vertex_base_offset;
    uint vertex_stride;
    uint vertex_buffer_size;
    uint viewport_x;
    uint viewport_y;
    uint viewport_width;
    uint viewport_height;
    uint color_offset;
    uint flags;
};

ByteAddressBuffer g_vertex_buffer : register(t0);

struct VSOut {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

uint ByteSwap32(uint v) {
    return (v << 24) | ((v << 8) & 0x00FF0000u) | ((v >> 8) & 0x0000FF00u) | (v >> 24);
}

bool CanLoadDword(uint byte_offset) {
    return (byte_offset & 3u) == 0u && (byte_offset + 4u) <= vertex_buffer_size;
}

float LoadGuestFloat(uint byte_offset, float fallback_value) {
    if (!CanLoadDword(byte_offset)) {
        return fallback_value;
    }
    return asfloat(ByteSwap32(g_vertex_buffer.Load(byte_offset)));
}

float4 LoadGuestColor(uint byte_offset, float4 fallback_value) {
    if (!CanLoadDword(byte_offset)) {
        return fallback_value;
    }
    uint packed = ByteSwap32(g_vertex_buffer.Load(byte_offset));
    return float4(
        float((packed >> 16) & 0xFFu) / 255.0f,
        float((packed >> 8) & 0xFFu) / 255.0f,
        float((packed >> 0) & 0xFFu) / 255.0f,
        max(float((packed >> 24) & 0xFFu) / 255.0f, 1.0f / 255.0f));
}

VSOut main(uint vid : SV_VertexID) {
    uint stride = vertex_stride;
    uint byte_offset = vertex_base_offset + vid * stride;
    float4 default_color = float4(1.0f, 1.0f, 1.0f, 1.0f);

    // Unsupported stream layouts should draw nothing rather than issue invalid
    // raw-buffer reads that can remove the device.
    if (stride == 0u || byte_offset >= vertex_buffer_size) {
        VSOut empty;
        empty.pos = float4(0.0f, 0.0f, 0.0f, 0.0f);
        empty.color = default_color;
        empty.uv = float2(0.0f, 0.0f);
        return empty;
    }

    float4 raw_pos = float4(
        LoadGuestFloat(byte_offset + 0, 0.0f),
        LoadGuestFloat(byte_offset + 4, 0.0f),
        LoadGuestFloat(byte_offset + 8, 0.0f),
        1.0f);
    if (stride >= 16u && CanLoadDword(byte_offset + 12u)) {
        float candidate_w = LoadGuestFloat(byte_offset + 12, 1.0f);
        if (candidate_w == candidate_w && abs(candidate_w) < 10000.0f) {
            raw_pos.w = candidate_w;
        }
    }
    bool has_viewport = viewport_width != 0 && viewport_height != 0;
    bool looks_screen_space =
        has_viewport &&
        (abs(raw_pos.x) > 2.5f || abs(raw_pos.y) > 2.5f || raw_pos.w > 2.0f || raw_pos.w < 0.0f);

    VSOut o;
    if (looks_screen_space) {
        float2 viewport_size = float2(max(viewport_width, 1u), max(viewport_height, 1u));
        float2 viewport_origin = float2(viewport_x, viewport_y);
        float2 pixel = raw_pos.xy - viewport_origin;
        float2 ndc = float2(
            pixel.x / viewport_size.x * 2.0f - 1.0f,
            1.0f - pixel.y / viewport_size.y * 2.0f);
        o.pos = float4(ndc, saturate(raw_pos.z), 1.0f);
    } else {
        float w = abs(raw_pos.w) > 1.0e-6f ? raw_pos.w : 1.0f;
        o.pos = float4(raw_pos.xyz, w);
    }

    if (color_offset != 0xFFFFFFFFu && (color_offset + 4u) <= stride) {
        o.color = LoadGuestColor(byte_offset + color_offset, default_color);
    } else {
        o.color = default_color;
    }
    o.uv = raw_pos.xy;
    return o;
}
)HLSL";

// Simple pixel shader: output the pulled vertex color.
static constexpr const char kPassthroughPS[] = R"HLSL(
struct PSIn {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target {
    return i.color;
}
)HLSL";

// Diagnostic variant for post-process / present-tagged passes. This makes
// successful fullscreen-style replay obvious even before real texture sampling
// is implemented.
static constexpr const char kSoftParticlePS[] = R"HLSL(
struct PSIn {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target {
    float2 pos_band = frac(abs(i.pos.xy) * 0.015625f);
    float2 uv_band = frac(abs(i.uv.xy) * 0.001953125f);
    float3 debug_color = float3(
        max(pos_band.x, 0.2f),
        max(pos_band.y, 0.2f),
        max(frac(uv_band.x + uv_band.y), 0.35f));
    return float4(debug_color, 1.0f);
}
)HLSL";

// ---------------------------------------------------------------------------

/*static*/ Microsoft::WRL::ComPtr<ID3DBlob> D3D12ShaderManager::CompileHLSL(
    const char* source, const char* entry_point, const char* target) {
  UINT flags = 0;
#if defined(_DEBUG)
  flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  Microsoft::WRL::ComPtr<ID3DBlob> blob;
  Microsoft::WRL::ComPtr<ID3DBlob> error;
  HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                          entry_point, target, flags, 0, &blob, &error);
  if (FAILED(hr)) {
    if (error) {
      REXLOG_ERROR("Shader compile error [{}]: {}", entry_point,
                   static_cast<const char*>(error->GetBufferPointer()));
    } else {
      REXLOG_ERROR("Shader compile error [{}]: hr=0x{:08X}", entry_point,
                   static_cast<uint32_t>(hr));
    }
    return nullptr;
  }
  return blob;
}

bool D3D12ShaderManager::Initialize(ID3D12Device* device) {
  device_ = device;

  REXLOG_INFO("D3D12ShaderManager: Compiling passthrough VS...");
  passthrough_vs_ = CompileHLSL(kPassthroughVS, "main", "vs_5_0");
  if (!passthrough_vs_) {
    REXLOG_ERROR("D3D12ShaderManager: Failed to compile passthrough VS");
    return false;
  }

  REXLOG_INFO("D3D12ShaderManager: Compiling passthrough PS...");
  passthrough_ps_ = CompileHLSL(kPassthroughPS, "main", "ps_5_0");
  if (!passthrough_ps_) {
    REXLOG_ERROR("D3D12ShaderManager: Failed to compile passthrough PS");
    return false;
  }

  REXLOG_INFO("D3D12ShaderManager: Compiling soft-particle PS...");
  soft_particle_ps_ = CompileHLSL(kSoftParticlePS, "main", "ps_5_0");
  if (!soft_particle_ps_) {
    REXLOG_WARN("D3D12ShaderManager: Soft-particle PS compile failed, using passthrough");
    soft_particle_ps_ = passthrough_ps_;
  }

  REXLOG_INFO("D3D12ShaderManager: All shaders compiled successfully");
  return true;
}

void D3D12ShaderManager::Shutdown() {
  pso_cache_.clear();
  passthrough_vs_.Reset();
  passthrough_ps_.Reset();
  soft_particle_ps_.Reset();
  device_ = nullptr;
}

ID3D12PipelineState* D3D12ShaderManager::GetOrCreatePSO(
    uint64_t state_hash,
    ID3D12RootSignature* root_sig,
    DXGI_FORMAT rt_format,
    DXGI_FORMAT ds_format,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type,
    bool use_soft_particle_ps) {
  auto it = pso_cache_.find(state_hash);
  if (it != pso_cache_.end()) {
    return it->second.pso.Get();
  }

  if (!device_ || !passthrough_vs_ || !passthrough_ps_) {
    return nullptr;
  }

  ID3DBlob* ps = use_soft_particle_ps ? soft_particle_ps_.Get() : passthrough_ps_.Get();

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_sig;
  desc.VS = {passthrough_vs_->GetBufferPointer(), passthrough_vs_->GetBufferSize()};
  desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.BlendState.RenderTarget[0].BlendEnable = use_soft_particle_ps ? TRUE : FALSE;
  desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
  desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  desc.DepthStencilState.DepthEnable = (ds_format != DXGI_FORMAT_UNKNOWN) ? TRUE : FALSE;
  desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  desc.SampleMask = UINT_MAX;
  desc.PrimitiveTopologyType = topology_type;
  desc.NumRenderTargets = (rt_format != DXGI_FORMAT_UNKNOWN) ? 1 : 0;
  if (rt_format != DXGI_FORMAT_UNKNOWN) {
    desc.RTVFormats[0] = rt_format;
  }
  desc.DSVFormat = ds_format;
  desc.SampleDesc.Count = 1;

  Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
  HRESULT hr = device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr)) {
    REXLOG_ERROR(
        "D3D12ShaderManager: CreateGraphicsPipelineState failed 0x{:08X} hash=0x{:016X}",
        static_cast<uint32_t>(hr), state_hash);
    return nullptr;
  }

  auto& entry = pso_cache_[state_hash];
  entry.pso = pso;
  return entry.pso.Get();
}

}  // namespace ac6::renderer
