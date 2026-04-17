#include "ac6_native_assets.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/cvar.h>
#include <rex/crypto/sha256.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(
    ac6_native_assets_enabled, true, "AC6/Assets",
    "Enable the AC6 native asset registry and content-root override resolver");
REXCVAR_DEFINE_BOOL(
    ac6_native_assets_auto_index, true, "AC6/Assets",
    "Auto-index files under known native asset directories when no manifest entry is present");
REXCVAR_DEFINE_BOOL(
    ac6_native_assets_compute_hashes, false, "AC6/Assets",
    "Compute SHA-256 hashes for indexed native asset files during registry initialization");
REXCVAR_DEFINE_BOOL(
    ac6_native_assets_log_overrides, true, "AC6/Assets",
    "Log native asset override decisions while building the registry");
REXCVAR_DEFINE_STRING(
    ac6_native_assets_content_subdir, "native_content", "AC6/Assets",
    "Base and update content subdirectory scanned for AC6 native assets");
REXCVAR_DEFINE_STRING(
    ac6_native_assets_mods_dir, "mods", "AC6/Assets",
    "User-data subdirectory that contains AC6 native asset mod roots");
REXCVAR_DEFINE_STRING(
    ac6_native_assets_loose_override_dir, "override", "AC6/Assets",
    "User-data subdirectory that contains loose AC6 native asset overrides");

namespace ac6::assets {
namespace {

enum class ContentRootKind : uint8_t {
  kBase = 0,
  kUpdate,
  kMod,
  kLooseOverride,
};

struct RootDescriptor {
  ContentRootInfo info;
  ContentRootKind kind{ContentRootKind::kBase};
  uint32_t order{0};
  std::unordered_set<std::string> manifest_claimed_paths;
};

struct RegisteredAsset {
  AssetRecord record;
  uint32_t root_priority{0};
  uint32_t root_order{0};
};

struct AliasBinding {
  std::string asset_id;
  uint32_t root_priority{0};
  uint32_t root_order{0};
};

struct NativeAssetRegistryState {
  bool enabled{false};
  bool initialized{false};
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
  std::vector<RootDescriptor> roots;
  std::unordered_map<std::string, RegisteredAsset> assets;
  std::unordered_map<std::string, AliasBinding> aliases;
};

std::mutex g_asset_registry_mutex;
NativeAssetRegistryState g_asset_registry;

std::string LowercaseAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string NormalizeAssetId(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  bool previous_was_slash = false;
  for (unsigned char raw : value) {
    char c = static_cast<char>(raw);
    if (c == '\\') {
      c = '/';
    }
    if (c == '/') {
      if (previous_was_slash) {
        continue;
      }
      previous_was_slash = true;
      normalized.push_back(c);
      continue;
    }
    previous_was_slash = false;
    normalized.push_back(static_cast<char>(std::tolower(raw)));
  }
  while (!normalized.empty() && normalized.front() == '/') {
    normalized.erase(normalized.begin());
  }
  while (normalized.rfind("./", 0) == 0) {
    normalized.erase(0, 2);
  }
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  return normalized;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
    return false;
  }
  for (const auto& component : path) {
    if (component == "..") {
      return false;
    }
  }
  return true;
}

const char* ContentRootKindName(ContentRootKind kind) {
  switch (kind) {
    case ContentRootKind::kBase:
      return "base";
    case ContentRootKind::kUpdate:
      return "update";
    case ContentRootKind::kMod:
      return "mod";
    case ContentRootKind::kLooseOverride:
      return "loose";
  }
  return "unknown";
}

AssetClass AssetClassFromLeadingDirectory(const std::filesystem::path& relative_path) {
  auto it = relative_path.begin();
  if (it == relative_path.end()) {
    return AssetClass::kUnknown;
  }

  const std::string first_component = LowercaseAscii(it->generic_string());
  if (first_component == "textures" || first_component == "texture") {
    return AssetClass::kTexture;
  }
  if (first_component == "models" || first_component == "model" || first_component == "meshes" ||
      first_component == "mesh") {
    return AssetClass::kModel;
  }
  if (first_component == "materials" || first_component == "material") {
    return AssetClass::kMaterial;
  }
  if (first_component == "shaders" || first_component == "shader") {
    return AssetClass::kShader;
  }
  if (first_component == "effects" || first_component == "effect" || first_component == "fx") {
    return AssetClass::kEffect;
  }
  return AssetClass::kUnknown;
}

AssetClass AssetClassFromExtension(const std::filesystem::path& path) {
  const std::string extension = LowercaseAscii(path.extension().string());
  if (extension == ".dds" || extension == ".png" || extension == ".bmp" || extension == ".tga" ||
      extension == ".jpg" || extension == ".jpeg" || extension == ".tif" ||
      extension == ".tiff") {
    return AssetClass::kTexture;
  }
  if (extension == ".mesh" || extension == ".gltf" || extension == ".glb" || extension == ".fbx" ||
      extension == ".obj") {
    return AssetClass::kModel;
  }
  if (extension == ".material" || extension == ".mat") {
    return AssetClass::kMaterial;
  }
  if (extension == ".hlsl" || extension == ".dxil" || extension == ".cso") {
    return AssetClass::kShader;
  }
  if (extension == ".fx") {
    return AssetClass::kEffect;
  }
  return AssetClass::kUnknown;
}

AssetClass GuessAssetClass(const std::filesystem::path& relative_path) {
  AssetClass asset_class = AssetClassFromLeadingDirectory(relative_path);
  if (asset_class != AssetClass::kUnknown) {
    return asset_class;
  }
  return AssetClassFromExtension(relative_path);
}

AssetClass ParseAssetClass(std::string_view value) {
  const std::string lowered = NormalizeAssetId(value);
  if (lowered == "texture") {
    return AssetClass::kTexture;
  }
  if (lowered == "model" || lowered == "mesh") {
    return AssetClass::kModel;
  }
  if (lowered == "material") {
    return AssetClass::kMaterial;
  }
  if (lowered == "shader") {
    return AssetClass::kShader;
  }
  if (lowered == "effect" || lowered == "fx") {
    return AssetClass::kEffect;
  }
  return AssetClass::kUnknown;
}

bool ShouldSkipAutoIndexedPath(const std::filesystem::path& relative_path) {
  const std::string filename = LowercaseAscii(relative_path.filename().string());
  return filename == "manifest.toml" || filename == "readme.txt" || filename == "readme.md" ||
         filename == "thumbs.db" || filename == ".ds_store";
}

std::string ComputeContentHash(const std::filesystem::path& path) {
  if (!REXCVAR_GET(ac6_native_assets_compute_hashes)) {
    return {};
  }
  return rex::crypto::sha256_file(path);
}

uint64_t QueryFileSize(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  return ec ? 0ull : static_cast<uint64_t>(size);
}

void RegisterAlias(NativeAssetRegistryState& state, const RootDescriptor& root,
                   std::string_view alias, std::string_view asset_id) {
  const std::string normalized_alias = NormalizeAssetId(alias);
  const std::string normalized_asset_id = NormalizeAssetId(asset_id);
  if (normalized_alias.empty() || normalized_asset_id.empty() ||
      normalized_alias == normalized_asset_id) {
    return;
  }

  const auto existing = state.aliases.find(normalized_alias);
  if (existing != state.aliases.end()) {
    const bool should_override =
        root.info.priority > existing->second.root_priority ||
        (root.info.priority == existing->second.root_priority && root.order >= existing->second.root_order);
    if (!should_override) {
      return;
    }
    ++state.override_count;
  } else {
    ++state.alias_count;
  }

  state.aliases[normalized_alias] = AliasBinding{
      normalized_asset_id,
      root.info.priority,
      root.order,
  };
}

bool RegisterAsset(NativeAssetRegistryState& state, RootDescriptor& root, AssetRecord record) {
  record.asset_id = NormalizeAssetId(record.asset_id);
  if (record.asset_id.empty()) {
    return false;
  }

  auto existing = state.assets.find(record.asset_id);
  if (existing != state.assets.end()) {
    const bool should_override =
        root.info.priority > existing->second.root_priority ||
        (root.info.priority == existing->second.root_priority && root.order >= existing->second.root_order);
    if (!should_override) {
      return false;
    }

    record.overridden = true;
    ++state.override_count;
    if (REXCVAR_GET(ac6_native_assets_log_overrides)) {
      REXLOG_INFO("AC6 native assets: '{}' overridden by {} root '{}'",
                  record.asset_id, root.info.kind, root.info.name);
    }
  }

  for (const auto& alias : record.aliases) {
    RegisterAlias(state, root, alias, record.asset_id);
  }

  root.info.asset_count += 1;
  state.assets[record.asset_id] = RegisteredAsset{
      std::move(record),
      root.info.priority,
      root.order,
  };
  return true;
}

std::vector<std::string> ParseStringArray(const toml::node_view<const toml::node>& node) {
  std::vector<std::string> values;
  if (const toml::array* array = node.as_array()) {
    values.reserve(array->size());
    for (const toml::node& item : *array) {
      if (auto value = item.value<std::string>()) {
        values.push_back(*value);
      }
    }
  }
  return values;
}

void LoadManifestForRoot(NativeAssetRegistryState& state, RootDescriptor& root) {
  const std::filesystem::path manifest_path = root.info.path / "manifest.toml";
  if (!std::filesystem::exists(manifest_path)) {
    return;
  }

  toml::table manifest;
  try {
    manifest = toml::parse_file(manifest_path.string());
  } catch (const toml::parse_error& err) {
    ++state.manifest_error_count;
    REXLOG_WARN("AC6 native assets: failed to parse {}: {}", manifest_path.string(), err.description());
    return;
  }

  const toml::array* assets = manifest["asset"].as_array();
  if (!assets) {
    return;
  }

  for (const toml::node& asset_node : *assets) {
    const toml::table* table = asset_node.as_table();
    if (!table) {
      continue;
    }

    const auto id = (*table)["id"].value<std::string>();
    const auto path_value = (*table)["path"].value<std::string>();
    if (!id || !path_value) {
      ++state.manifest_error_count;
      continue;
    }

    const std::filesystem::path relative_path = std::filesystem::path(*path_value).lexically_normal();
    if (!IsSafeRelativePath(relative_path)) {
      ++state.manifest_error_count;
      REXLOG_WARN("AC6 native assets: rejected unsafe manifest path '{}' in {}", *path_value,
                  manifest_path.string());
      continue;
    }

    const std::filesystem::path resolved_path = root.info.path / relative_path;
    if (!std::filesystem::exists(resolved_path)) {
      ++state.manifest_error_count;
      REXLOG_WARN("AC6 native assets: manifest entry '{}' points to missing file {}", *id,
                  resolved_path.string());
      continue;
    }

    AssetRecord record;
    record.asset_id = *id;
    if (auto asset_class = (*table)["class"].value<std::string>()) {
      record.asset_class = ParseAssetClass(*asset_class);
    } else {
      record.asset_class = GuessAssetClass(relative_path);
    }
    record.relative_path = relative_path;
    record.resolved_path = resolved_path;
    record.source_root_path = root.info.path;
    record.source_root_name = root.info.name;
    record.source_reference = (*table)["source"].value_or(relative_path.generic_string());
    record.aliases = ParseStringArray((*table)["aliases"]);
    record.dependencies = ParseStringArray((*table)["dependencies"]);
    record.content_hash = ComputeContentHash(resolved_path);
    record.file_size = QueryFileSize(resolved_path);
    record.from_manifest = true;

    if (RegisterAsset(state, root, std::move(record))) {
      root.manifest_claimed_paths.insert(NormalizeAssetId(relative_path.generic_string()));
      ++state.manifest_asset_count;
    }
  }
}

void AutoIndexRoot(NativeAssetRegistryState& state, RootDescriptor& root) {
  if (!REXCVAR_GET(ac6_native_assets_auto_index) || !root.info.exists) {
    return;
  }

  std::error_code ec;
  std::filesystem::recursive_directory_iterator iter(
      root.info.path, std::filesystem::directory_options::skip_permission_denied, ec);
  std::filesystem::recursive_directory_iterator end;
  if (ec) {
    ++state.manifest_error_count;
    REXLOG_WARN("AC6 native assets: failed to enumerate {}: {}", root.info.path.string(), ec.message());
    return;
  }

  while (iter != end) {
    const std::filesystem::directory_entry entry = *iter;
    iter.increment(ec);
    if (ec) {
      ec.clear();
    }

    std::error_code status_ec;
    if (!entry.is_regular_file(status_ec) || status_ec) {
      continue;
    }

    std::filesystem::path relative_path = std::filesystem::relative(entry.path(), root.info.path, status_ec);
    if (status_ec || !IsSafeRelativePath(relative_path) || ShouldSkipAutoIndexedPath(relative_path)) {
      continue;
    }

    const std::string normalized_relative = NormalizeAssetId(relative_path.generic_string());
    if (root.manifest_claimed_paths.contains(normalized_relative)) {
      continue;
    }

    const AssetClass asset_class = GuessAssetClass(relative_path);
    if (asset_class == AssetClass::kUnknown) {
      continue;
    }

    AssetRecord record;
    record.asset_id = normalized_relative;
    record.asset_class = asset_class;
    record.relative_path = relative_path.lexically_normal();
    record.resolved_path = entry.path();
    record.source_root_path = root.info.path;
    record.source_root_name = root.info.name;
    record.source_reference = record.relative_path.generic_string();
    record.content_hash = ComputeContentHash(record.resolved_path);
    record.file_size = QueryFileSize(record.resolved_path);
    record.from_manifest = false;

    if (RegisterAsset(state, root, std::move(record))) {
      ++state.auto_indexed_asset_count;
    }
  }
}

RootDescriptor MakeRootDescriptor(ContentRootKind kind, std::string name, std::filesystem::path path,
                                  uint32_t priority, uint32_t order) {
  RootDescriptor root;
  root.info.name = std::move(name);
  root.info.kind = ContentRootKindName(kind);
  root.info.path = std::move(path);
  root.info.priority = priority;
  root.info.exists = std::filesystem::exists(root.info.path) &&
                     std::filesystem::is_directory(root.info.path);
  root.kind = kind;
  root.order = order;
  return root;
}

std::vector<RootDescriptor> DiscoverRoots(const std::filesystem::path& game_data_root,
                                          const std::filesystem::path& user_data_root,
                                          const std::filesystem::path& update_data_root,
                                          NativeAssetRegistryState& state) {
  std::vector<RootDescriptor> roots;
  uint32_t order = 0;
  const std::filesystem::path content_subdir = REXCVAR_GET(ac6_native_assets_content_subdir);
  const std::filesystem::path mods_dir_name = REXCVAR_GET(ac6_native_assets_mods_dir);
  const std::filesystem::path loose_override_dir_name = REXCVAR_GET(ac6_native_assets_loose_override_dir);

  state.base_content_root = game_data_root / content_subdir;
  state.update_content_root =
      update_data_root.empty() ? std::filesystem::path() : (update_data_root / content_subdir);
  state.mods_root = user_data_root.empty() ? std::filesystem::path() : (user_data_root / mods_dir_name);
  state.loose_override_root =
      user_data_root.empty() ? std::filesystem::path() : (user_data_root / loose_override_dir_name);

  roots.push_back(MakeRootDescriptor(ContentRootKind::kBase, "base", state.base_content_root, 10, order++));

  if (!state.update_content_root.empty()) {
    roots.push_back(
        MakeRootDescriptor(ContentRootKind::kUpdate, "update", state.update_content_root, 20, order++));
  }

  if (!state.mods_root.empty()) {
    std::error_code ec;
    std::vector<std::filesystem::path> mod_paths;
    for (std::filesystem::directory_iterator iter(
             state.mods_root, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         iter != end;) {
      const std::filesystem::directory_entry entry = *iter;
      iter.increment(ec);
      if (ec) {
        ec.clear();
      }
      if (entry.is_directory(ec) && !ec) {
        mod_paths.push_back(entry.path());
      }
      ec.clear();
    }

    std::sort(mod_paths.begin(), mod_paths.end(),
              [](const std::filesystem::path& left, const std::filesystem::path& right) {
                return LowercaseAscii(left.filename().string()) < LowercaseAscii(right.filename().string());
              });

    uint32_t mod_priority = 100;
    for (const auto& mod_path : mod_paths) {
      roots.push_back(MakeRootDescriptor(ContentRootKind::kMod, mod_path.filename().string(), mod_path,
                                         mod_priority++, order++));
    }
  }

  if (!state.loose_override_root.empty()) {
    roots.push_back(MakeRootDescriptor(ContentRootKind::kLooseOverride, "override",
                                       state.loose_override_root, 1000, order++));
  }

  for (const auto& root : roots) {
    if (!root.info.exists) {
      ++state.missing_root_count;
    }
  }

  return roots;
}

}  // namespace

const char* AssetClassName(AssetClass asset_class) {
  switch (asset_class) {
    case AssetClass::kTexture:
      return "texture";
    case AssetClass::kModel:
      return "model";
    case AssetClass::kMaterial:
      return "material";
    case AssetClass::kShader:
      return "shader";
    case AssetClass::kEffect:
      return "effect";
    case AssetClass::kUnknown:
      break;
  }
  return "unknown";
}

void InitializeNativeAssetRegistry(const std::filesystem::path& game_data_root,
                                   const std::filesystem::path& user_data_root,
                                   const std::filesystem::path& update_data_root) {
  std::lock_guard<std::mutex> lock(g_asset_registry_mutex);

  NativeAssetRegistryState next_state;
  next_state.enabled = REXCVAR_GET(ac6_native_assets_enabled);
  if (!next_state.enabled) {
    g_asset_registry = std::move(next_state);
    REXLOG_INFO("AC6 native assets: registry disabled");
    return;
  }

  next_state.roots = DiscoverRoots(game_data_root, user_data_root, update_data_root, next_state);
  for (auto& root : next_state.roots) {
    if (!root.info.exists) {
      continue;
    }
    LoadManifestForRoot(next_state, root);
    AutoIndexRoot(next_state, root);
  }

  next_state.initialized = true;
  g_asset_registry = std::move(next_state);

  REXLOG_INFO("AC6 native assets: {} roots, {} assets, {} aliases, {} overrides",
              g_asset_registry.roots.size(), g_asset_registry.assets.size(), g_asset_registry.alias_count,
              g_asset_registry.override_count);
  for (const auto& root : g_asset_registry.roots) {
    REXLOG_INFO("  [{}] {} -> {} ({}, {} assets)", root.info.kind, root.info.name,
                root.info.path.string(), root.info.exists ? "present" : "missing", root.info.asset_count);
  }
}

void ShutdownNativeAssetRegistry() {
  std::lock_guard<std::mutex> lock(g_asset_registry_mutex);
  g_asset_registry = NativeAssetRegistryState{};
}

AssetRegistryStatusSnapshot GetNativeAssetRegistryStatus() {
  std::lock_guard<std::mutex> lock(g_asset_registry_mutex);

  AssetRegistryStatusSnapshot snapshot;
  snapshot.enabled = g_asset_registry.enabled;
  snapshot.initialized = g_asset_registry.initialized;
  snapshot.content_root_count = static_cast<uint32_t>(g_asset_registry.roots.size());
  snapshot.registered_asset_count = static_cast<uint32_t>(g_asset_registry.assets.size());
  snapshot.manifest_asset_count = g_asset_registry.manifest_asset_count;
  snapshot.auto_indexed_asset_count = g_asset_registry.auto_indexed_asset_count;
  snapshot.alias_count = g_asset_registry.alias_count;
  snapshot.override_count = g_asset_registry.override_count;
  snapshot.manifest_error_count = g_asset_registry.manifest_error_count;
  snapshot.missing_root_count = g_asset_registry.missing_root_count;
  snapshot.base_content_root = g_asset_registry.base_content_root;
  snapshot.update_content_root = g_asset_registry.update_content_root;
  snapshot.mods_root = g_asset_registry.mods_root;
  snapshot.loose_override_root = g_asset_registry.loose_override_root;
  snapshot.content_roots.reserve(g_asset_registry.roots.size());
  for (const auto& root : g_asset_registry.roots) {
    snapshot.content_roots.push_back(root.info);
  }
  return snapshot;
}

std::optional<AssetRecord> ResolveAsset(std::string_view asset_id, AssetClass expected_class) {
  const std::string normalized_asset_id = NormalizeAssetId(asset_id);
  if (normalized_asset_id.empty()) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(g_asset_registry_mutex);

  std::string resolved_id = normalized_asset_id;
  const auto alias = g_asset_registry.aliases.find(normalized_asset_id);
  if (alias != g_asset_registry.aliases.end()) {
    resolved_id = alias->second.asset_id;
  }

  const auto asset = g_asset_registry.assets.find(resolved_id);
  if (asset == g_asset_registry.assets.end()) {
    return std::nullopt;
  }
  if (expected_class != AssetClass::kUnknown && asset->second.record.asset_class != expected_class) {
    return std::nullopt;
  }
  return asset->second.record;
}

}  // namespace ac6::assets
