#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/ui/d3d12/d3d12_api.h>

namespace ac6::textures {

struct TextureSubresourceLayout {
  uint32_t row_pitch = 0;
  uint32_t slice_pitch = 0;
  uint32_t row_count = 0;
};

struct DdsSubresource {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 1;
  uint32_t row_pitch = 0;
  uint32_t slice_pitch = 0;
  std::vector<uint8_t> data;
};

struct DdsImageData {
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth_or_array_size = 1;
  uint32_t mip_count = 1;
  bool is_cube = false;
  std::vector<DdsSubresource> subresources;
};

struct TextureDumpMetadata {
  std::string stable_key;
  uint64_t texture_key_hash = 0;
  uint32_t base_page = 0;
  uint32_t mip_page = 0;
  uint32_t dimension = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth_or_array_size = 1;
  uint32_t mip_count = 1;
  uint32_t guest_format = 0;
  uint32_t endianness = 0;
  uint32_t dxgi_format = 0;
  bool tiled = false;
  bool packed_mips = false;
  bool signed_separate = false;
  bool scaled_resolve = false;
  uint64_t frame_index = 0;
  uint64_t signature_stable_id = 0;
  uint64_t active_vertex_shader_hash = 0;
  uint64_t active_pixel_shader_hash = 0;
  std::string signature_tags;
};

bool TextureSwapsEnabled();
bool TextureDumpEnabled();
bool TextureReplacementEnabled();

bool IsSupportedTextureSwapFormat(DXGI_FORMAT format);
bool GetTightTextureSubresourceLayout(DXGI_FORMAT format, uint32_t width, uint32_t height,
                                      TextureSubresourceLayout& out);
std::string DescribeDxgiFormat(DXGI_FORMAT format);

std::string BuildTextureStableKey(uint64_t texture_key_hash, uint32_t base_page, uint32_t mip_page,
                                  uint32_t dimension, uint32_t width, uint32_t height,
                                  uint32_t depth_or_array_size, uint32_t mip_count,
                                  uint32_t guest_format, uint32_t endianness, bool tiled,
                                  bool packed_mips, bool signed_separate, bool scaled_resolve);

std::filesystem::path GetTextureDumpDdsPath(std::string_view stable_key);
std::filesystem::path GetTextureDumpMetadataPath(std::string_view stable_key);
std::filesystem::path GetTextureDumpCurrentSessionRoot();
std::filesystem::path GetTextureDumpCurrentSessionDdsPath(std::string_view stable_key);
std::filesystem::path GetTextureDumpCurrentSessionMetadataPath(std::string_view stable_key);
std::filesystem::path GetTextureDumpCurrentSessionInfoPath();
bool DumpExists(std::string_view stable_key);
bool MirrorDumpToCurrentSession(std::string_view stable_key, std::string* error_out = nullptr);

std::optional<std::filesystem::path> ResolveReplacementDdsPath(std::string_view stable_key);

bool LoadDdsFromFile(const std::filesystem::path& path, DdsImageData& out,
                     std::string* error_out = nullptr);
bool WriteDdsToFile(const std::filesystem::path& path, const DdsImageData& data,
                    std::string* error_out = nullptr);
bool WriteDumpMetadata(const std::filesystem::path& path, const TextureDumpMetadata& metadata,
                       std::string* error_out = nullptr);

}  // namespace ac6::textures
