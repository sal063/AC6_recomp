#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>

#include <wrl/client.h>
#include <d3d12.h>
#include <d3dcompiler.h>

namespace ac6::renderer {

// Manages native passthrough shaders and a PSO cache for the native renderer.
// Uses D3DCompile to compile embedded HLSL source inline at startup — no
// external shader files required.
class D3D12ShaderManager {
 public:
  bool Initialize(ID3D12Device* device);
  void Shutdown();

  // Returns the shared passthrough vertex shader blob (compiled once).
  ID3DBlob* GetPassthroughVS() const { return passthrough_vs_.Get(); }

  // Returns the shared solid-color pixel shader blob (compiled once).
  ID3DBlob* GetPassthroughPS() const { return passthrough_ps_.Get(); }

  // Returns the soft-particle / high-precision effect pixel shader blob.
  ID3DBlob* GetSoftParticlePS() const { return soft_particle_ps_.Get(); }

  // Get or create a PSO for the given state hash.
  // root_sig must already be created on the device.
  // Returns nullptr on failure.
  ID3D12PipelineState* GetOrCreatePSO(
      uint64_t state_hash,
      ID3D12RootSignature* root_sig,
      DXGI_FORMAT rt_format,
      DXGI_FORMAT ds_format,
      D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type,
      bool use_soft_particle_ps = false);

  // Backwards compat stubs (unused but keep old callers linking)
  ID3DBlob* GetVertexShader(uint32_t /*guest_hash*/) { return passthrough_vs_.Get(); }
  ID3DBlob* GetPixelShader(uint32_t /*guest_hash*/)  { return passthrough_ps_.Get(); }
  ID3DBlob* GetGenericSoftParticleShader()            { return soft_particle_ps_.Get(); }

 private:
  ID3D12Device* device_ = nullptr;

  Microsoft::WRL::ComPtr<ID3DBlob> passthrough_vs_;
  Microsoft::WRL::ComPtr<ID3DBlob> passthrough_ps_;
  Microsoft::WRL::ComPtr<ID3DBlob> soft_particle_ps_;

  struct CachedPSO {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
  };
  std::unordered_map<uint64_t, CachedPSO> pso_cache_;

  static Microsoft::WRL::ComPtr<ID3DBlob> CompileHLSL(
      const char* source, const char* entry_point, const char* target);
};

}  // namespace ac6::renderer
