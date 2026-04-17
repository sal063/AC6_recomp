#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ac6::assets {

enum class AssetClass : uint8_t {
  kUnknown = 0,
  kTexture,
  kModel,
  kMaterial,
  kShader,
  kEffect,
};

struct ContentRootInfo {
  std::string name;
  std::string kind;
  std::filesystem::path path;
  uint32_t priority{0};
  uint32_t asset_count{0};
  bool exists{false};
};

struct AssetRecord {
  std::string asset_id;
  AssetClass asset_class{AssetClass::kUnknown};
  std::filesystem::path relative_path;
  std::filesystem::path resolved_path;
  std::filesystem::path source_root_path;
  std::string source_root_name;
  std::string source_reference;
  std::vector<std::string> aliases;
  std::vector<std::string> dependencies;
  std::string content_hash;
  uint64_t file_size{0};
  bool from_manifest{false};
  bool overridden{false};
};

struct AssetRegistryStatusSnapshot {
  bool enabled{false};
  bool initialized{false};
  uint32_t content_root_count{0};
  uint32_t registered_asset_count{0};
  uint32_t manifest_asset_count{0};
  uint32_t auto_indexed_asset_count{0};
  uint32_t alias_count{0};
  uint32_t override_count{0};
  uint32_t manifest_error_count{0};
  uint32_t missing_root_count{0};
  std::filesystem::path base_content_root;
  std::filesystem::path update_content_root;
  std::filesystem::path mods_root;
  std::filesystem::path loose_override_root;
  std::vector<ContentRootInfo> content_roots;
};

const char* AssetClassName(AssetClass asset_class);

void InitializeNativeAssetRegistry(const std::filesystem::path& game_data_root,
                                   const std::filesystem::path& user_data_root,
                                   const std::filesystem::path& update_data_root);
void ShutdownNativeAssetRegistry();

AssetRegistryStatusSnapshot GetNativeAssetRegistryStatus();
std::optional<AssetRecord> ResolveAsset(
    std::string_view asset_id,
    AssetClass expected_class = AssetClass::kUnknown);

}  // namespace ac6::assets
