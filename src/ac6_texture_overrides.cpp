#include "ac6_texture_overrides.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <toml++/toml.hpp>

REXCVAR_DECLARE(std::string, user_data_root);

REXCVAR_DEFINE_BOOL(ac6_texture_swaps_enabled, true, "AC6/TextureSwaps",
                    "Enable AC6 texture dump and replacement support");
REXCVAR_DEFINE_BOOL(ac6_texture_swaps_dump_enabled, true, "AC6/TextureSwaps",
                    "Dump host-ready textures to the user-data texture dump folder");
REXCVAR_DEFINE_BOOL(ac6_texture_swaps_replace_enabled, true, "AC6/TextureSwaps",
                    "Load matching replacement DDS files from the user-data texture override folders");
REXCVAR_DEFINE_STRING(ac6_texture_swaps_dump_dir, "texture_dumps", "AC6/TextureSwaps",
                      "User-data subdirectory that stores dumped texture DDS files and metadata");
REXCVAR_DEFINE_STRING(ac6_texture_swaps_override_dir, "override/textures", "AC6/TextureSwaps",
                      "User-data subdirectory that stores loose replacement texture DDS files");
REXCVAR_DEFINE_STRING(ac6_texture_swaps_mods_dir, "mods", "AC6/TextureSwaps",
                      "User-data subdirectory containing mod folders with texture overrides");

namespace ac6::textures {
namespace {

constexpr uint32_t kDdsMagic = 0x20534444u;
constexpr uint32_t kDdsFourCcDx10 = 0x30315844u;
constexpr uint32_t kDdsFourCcDxt1 = 0x31545844u;
constexpr uint32_t kDdsFourCcDxt3 = 0x33545844u;
constexpr uint32_t kDdsFourCcDxt5 = 0x35545844u;
constexpr uint32_t kDdsHeaderFlagsTexture = 0x00001007u;
constexpr uint32_t kDdsHeaderFlagsPitch = 0x00000008u;
constexpr uint32_t kDdsHeaderFlagsLinearSize = 0x00080000u;
constexpr uint32_t kDdsHeaderFlagsMipmap = 0x00020000u;
constexpr uint32_t kDdsHeaderFlagsDepth = 0x00800000u;
constexpr uint32_t kDdsCapsTexture = 0x00001000u;
constexpr uint32_t kDdsCapsComplex = 0x00000008u;
constexpr uint32_t kDdsCapsMipmap = 0x00400000u;
constexpr uint32_t kDdsCaps2Volume = 0x00200000u;
constexpr uint32_t kDdsCaps2Cubemap = 0x00000200u;
constexpr uint32_t kDdsPixelFormatFlagsFourCc = 0x00000004u;
constexpr uint32_t kDdsResourceDimensionTexture1D = 2u;
constexpr uint32_t kDdsResourceDimensionTexture2D = 3u;
constexpr uint32_t kDdsResourceDimensionTexture3D = 4u;
constexpr uint32_t kDdsResourceMiscTextureCube = 0x4u;

#pragma pack(push, 1)
struct DdsPixelFormat {
  uint32_t size;
  uint32_t flags;
  uint32_t four_cc;
  uint32_t rgb_bit_count;
  uint32_t r_bit_mask;
  uint32_t g_bit_mask;
  uint32_t b_bit_mask;
  uint32_t a_bit_mask;
};

struct DdsHeader {
  uint32_t size;
  uint32_t flags;
  uint32_t height;
  uint32_t width;
  uint32_t pitch_or_linear_size;
  uint32_t depth;
  uint32_t mip_map_count;
  uint32_t reserved1[11];
  DdsPixelFormat pixel_format;
  uint32_t caps;
  uint32_t caps2;
  uint32_t caps3;
  uint32_t caps4;
  uint32_t reserved2;
};

struct DdsHeaderDx10 {
  uint32_t dxgi_format;
  uint32_t resource_dimension;
  uint32_t misc_flag;
  uint32_t array_size;
  uint32_t misc_flags2;
};
#pragma pack(pop)

struct DxgiLayoutInfo {
  uint32_t block_width;
  uint32_t block_height;
  uint32_t bytes_per_block;
  const char* name;
};

struct TextureSwapRule {
  std::filesystem::path source_path;
  std::vector<std::string> stable_keys;
  std::vector<std::string> stable_key_globs;
};

struct TextureSwapManifestCacheEntry {
  bool present = false;
  bool parse_failed = false;
  std::filesystem::file_time_type last_write_time{};
  std::vector<TextureSwapRule> rules;
};

std::mutex g_texture_swap_manifest_mutex;
std::unordered_map<std::string, TextureSwapManifestCacheEntry> g_texture_swap_manifest_cache;

bool EnsureParentExists(const std::filesystem::path& path, std::string* error_out);

std::filesystem::path GetUserDataRoot() {
  const std::string user_root = REXCVAR_GET(user_data_root);
  if (!user_root.empty()) {
    return std::filesystem::path(user_root);
  }
  return rex::filesystem::GetUserFolder() / "ac6recomp";
}

std::filesystem::path GetTextureDumpRoot() {
  return GetUserDataRoot() / REXCVAR_GET(ac6_texture_swaps_dump_dir);
}

std::string BuildTextureDumpSessionId() {
  const auto now = std::chrono::system_clock::now();
  const auto time_value = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &time_value);
#else
  localtime_r(&time_value, &local_time);
#endif
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
      std::chrono::seconds(1);

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y%m%d_%H%M%S") << "_" << std::setw(3)
         << std::setfill('0') << milliseconds.count();
  return stream.str();
}

const std::string& GetTextureDumpSessionId() {
  static const std::string session_id = BuildTextureDumpSessionId();
  return session_id;
}

std::filesystem::path GetTextureDumpSessionsRoot() {
  return GetTextureDumpRoot() / "sessions";
}

void PublishCurrentTextureDumpSessionInfo() {
  static std::once_flag once;
  std::call_once(once, []() {
    const std::filesystem::path session_root =
        GetTextureDumpSessionsRoot() / GetTextureDumpSessionId();
    std::string error;
    if (!EnsureParentExists(session_root / "placeholder", &error)) {
      REXLOG_WARN("Texture swap dump session: failed to create session root ({})", error);
      return;
    }

    std::ofstream file(GetTextureDumpRoot() / "current_session.txt", std::ios::out | std::ios::trunc);
    if (!file) {
      REXLOG_WARN("Texture swap dump session: failed to write current_session.txt");
      return;
    }

    file << "session_id=" << GetTextureDumpSessionId() << "\n";
    file << "session_path=" << session_root.string() << "\n";
  });
}

bool EnsureParentExists(const std::filesystem::path& path, std::string* error_out) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    if (error_out) {
      *error_out = "failed to create parent directory: " + ec.message();
    }
    return false;
  }
  return true;
}

std::string EscapeJson(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(c);
        break;
    }
  }
  return escaped;
}

char AsciiToLower(char value) {
  return (value >= 'A' && value <= 'Z') ? char(value - 'A' + 'a') : value;
}

bool EqualsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (AsciiToLower(lhs[i]) != AsciiToLower(rhs[i])) {
      return false;
    }
  }
  return true;
}

bool WildcardMatchRecursive(std::string_view pattern, std::string_view value, size_t pattern_index,
                            size_t value_index) {
  while (pattern_index < pattern.size()) {
    const char pattern_char = pattern[pattern_index];
    if (pattern_char == '*') {
      while (pattern_index + 1 < pattern.size() && pattern[pattern_index + 1] == '*') {
        ++pattern_index;
      }
      if (pattern_index + 1 == pattern.size()) {
        return true;
      }
      for (size_t candidate = value_index; candidate <= value.size(); ++candidate) {
        if (WildcardMatchRecursive(pattern, value, pattern_index + 1, candidate)) {
          return true;
        }
      }
      return false;
    }
    if (value_index >= value.size()) {
      return false;
    }
    if (pattern_char != '?' && AsciiToLower(pattern_char) != AsciiToLower(value[value_index])) {
      return false;
    }
    ++pattern_index;
    ++value_index;
  }
  return value_index == value.size();
}

bool WildcardMatch(std::string_view pattern, std::string_view value) {
  return WildcardMatchRecursive(pattern, value, 0, 0);
}

std::vector<std::string> ParseStringList(const toml::node_view<const toml::node>& node) {
  std::vector<std::string> values;
  if (const auto value = node.value<std::string>()) {
    values.push_back(*value);
    return values;
  }

  const toml::array* array = node.as_array();
  if (!array) {
    return values;
  }

  values.reserve(array->size());
  for (const toml::node& item : *array) {
    if (const auto string_value = item.value<std::string>()) {
      values.push_back(*string_value);
    }
  }
  return values;
}

bool PathStartsWith(const std::filesystem::path& path, const std::filesystem::path& prefix) {
  auto path_it = path.begin();
  auto prefix_it = prefix.begin();
  while (prefix_it != prefix.end()) {
    if (path_it == path.end()) {
      return false;
    }
    if (!EqualsIgnoreAsciiCase(path_it->string(), prefix_it->string())) {
      return false;
    }
    ++path_it;
    ++prefix_it;
  }
  return true;
}

std::optional<std::filesystem::path> ResolveManifestSourcePath(const std::filesystem::path& root,
                                                               std::string_view source_value) {
  if (source_value.empty()) {
    return std::nullopt;
  }

  const std::filesystem::path source_path = std::filesystem::path(source_value);
  if (source_path.is_absolute()) {
    return std::nullopt;
  }

  const std::filesystem::path normalized_root = root.lexically_normal();
  const std::filesystem::path resolved = (normalized_root / source_path).lexically_normal();
  if (!PathStartsWith(resolved, normalized_root)) {
    return std::nullopt;
  }

  return resolved;
}

std::filesystem::path GetTextureSwapManifestPath(const std::filesystem::path& root) {
  return root / "manifest.toml";
}

TextureSwapManifestCacheEntry LoadTextureSwapManifestCacheEntry(const std::filesystem::path& root) {
  TextureSwapManifestCacheEntry entry;
  const std::filesystem::path manifest_path = GetTextureSwapManifestPath(root);
  std::error_code ec;
  if (!std::filesystem::exists(manifest_path, ec) || ec) {
    return entry;
  }

  entry.present = true;
  entry.last_write_time = std::filesystem::last_write_time(manifest_path, ec);
  if (ec) {
    entry.last_write_time = {};
  }

  toml::table manifest;
  try {
    manifest = toml::parse_file(manifest_path.string());
  } catch (const toml::parse_error& err) {
    entry.parse_failed = true;
    REXLOG_WARN("Texture swap manifest {}: parse error: {}", manifest_path.string(), err.description());
    return entry;
  }

  const toml::array* swaps = manifest["swap"].as_array();
  if (!swaps) {
    return entry;
  }

  for (const toml::node& swap_node : *swaps) {
    const toml::table* table = swap_node.as_table();
    if (!table) {
      continue;
    }

    const auto source_value = (*table)["source"].value<std::string>();
    if (!source_value) {
      continue;
    }

    std::optional<std::filesystem::path> source_path = ResolveManifestSourcePath(root, *source_value);
    if (!source_path) {
      REXLOG_WARN("Texture swap manifest {}: ignoring rule with invalid source '{}'",
                  manifest_path.string(), *source_value);
      continue;
    }

    TextureSwapRule rule;
    rule.source_path = *source_path;
    auto append_values = [](std::vector<std::string>& out, std::vector<std::string>&& values) {
      out.insert(out.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
    };

    append_values(rule.stable_keys, ParseStringList((*table)["stable_key"]));
    append_values(rule.stable_keys, ParseStringList((*table)["stable_keys"]));
    append_values(rule.stable_keys, ParseStringList((*table)["key"]));
    append_values(rule.stable_keys, ParseStringList((*table)["keys"]));

    append_values(rule.stable_key_globs, ParseStringList((*table)["stable_key_glob"]));
    append_values(rule.stable_key_globs, ParseStringList((*table)["stable_key_globs"]));
    append_values(rule.stable_key_globs, ParseStringList((*table)["pattern"]));
    append_values(rule.stable_key_globs, ParseStringList((*table)["patterns"]));

    if (rule.stable_keys.empty() && rule.stable_key_globs.empty()) {
      REXLOG_WARN("Texture swap manifest {}: ignoring rule for {} with no keys or patterns",
                  manifest_path.string(), rule.source_path.string());
      continue;
    }

    entry.rules.push_back(std::move(rule));
  }

  return entry;
}

const TextureSwapManifestCacheEntry& GetTextureSwapManifestCacheEntry(const std::filesystem::path& root) {
  const std::filesystem::path normalized_root = root.lexically_normal();
  const std::string cache_key = normalized_root.string();
  const std::filesystem::path manifest_path = GetTextureSwapManifestPath(normalized_root);

  std::error_code ec;
  const bool present = std::filesystem::exists(manifest_path, ec) && !ec;
  std::filesystem::file_time_type last_write_time{};
  if (present) {
    last_write_time = std::filesystem::last_write_time(manifest_path, ec);
    if (ec) {
      last_write_time = {};
    }
  }

  std::lock_guard<std::mutex> lock(g_texture_swap_manifest_mutex);
  auto it = g_texture_swap_manifest_cache.find(cache_key);
  if (it == g_texture_swap_manifest_cache.end() || it->second.present != present ||
      (present && it->second.last_write_time != last_write_time)) {
    it = g_texture_swap_manifest_cache
             .insert_or_assign(cache_key, LoadTextureSwapManifestCacheEntry(normalized_root))
             .first;
  }

  return it->second;
}

bool TextureSwapRuleMatches(const TextureSwapRule& rule, std::string_view stable_key) {
  for (const std::string& exact_key : rule.stable_keys) {
    if (EqualsIgnoreAsciiCase(exact_key, stable_key)) {
      return true;
    }
  }
  for (const std::string& pattern : rule.stable_key_globs) {
    if (WildcardMatch(pattern, stable_key)) {
      return true;
    }
  }
  return false;
}

std::optional<std::filesystem::path> ResolveManifestReplacementDdsPath(
    const std::filesystem::path& root, std::string_view stable_key) {
  const TextureSwapManifestCacheEntry& entry = GetTextureSwapManifestCacheEntry(root);
  if (!entry.present || entry.parse_failed || entry.rules.empty()) {
    return std::nullopt;
  }

  std::optional<std::filesystem::path> resolved;
  std::error_code ec;
  for (const TextureSwapRule& rule : entry.rules) {
    if (!TextureSwapRuleMatches(rule, stable_key)) {
      continue;
    }
    if (std::filesystem::exists(rule.source_path, ec)) {
      resolved = rule.source_path;
    }
  }

  return resolved;
}

std::optional<std::filesystem::path> ResolveReplacementDdsPathInRoot(
    const std::filesystem::path& root, std::string_view stable_key) {
  const std::filesystem::path file_name = std::string(stable_key) + ".dds";
  std::error_code ec;

  const std::filesystem::path exact_path = root / file_name;
  if (std::filesystem::exists(exact_path, ec)) {
    return exact_path;
  }

  return ResolveManifestReplacementDdsPath(root, stable_key);
}

std::string HexU32(uint32_t value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
  return stream.str();
}

std::string HexU64(uint64_t value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
  return stream.str();
}

const char* DimensionTag(uint32_t dimension) {
  if (dimension == 0 || dimension == uint32_t(D3D12_RESOURCE_DIMENSION_TEXTURE1D)) {
    return "1d";
  }
  if (dimension == 1 || dimension == uint32_t(D3D12_RESOURCE_DIMENSION_TEXTURE2D)) {
    return "2d";
  }
  if (dimension == 2 || dimension == uint32_t(D3D12_RESOURCE_DIMENSION_TEXTURE3D)) {
    return "3d";
  }
  if (dimension == 3) {
    return "cube";
  }
  return "unknown";
}

bool GetDxgiLayoutInfo(DXGI_FORMAT format, DxgiLayoutInfo& out) {
  switch (format) {
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SINT:
      out = {1, 1, 1, "DXGI_FORMAT_R8"};
      return true;
    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SINT:
      out = {1, 1, 2, "DXGI_FORMAT_R8G8"};
      return true;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
      out = {1, 1, 4, "DXGI_FORMAT_R8G8B8A8"};
      return true;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
      out = {1, 1, 4, "DXGI_FORMAT_R10G10B10A2"};
      return true;
    case DXGI_FORMAT_B5G6R5_UNORM:
      out = {1, 1, 2, "DXGI_FORMAT_B5G6R5_UNORM"};
      return true;
    case DXGI_FORMAT_B5G5R5A1_UNORM:
      out = {1, 1, 2, "DXGI_FORMAT_B5G5R5A1_UNORM"};
      return true;
    case DXGI_FORMAT_B4G4R4A4_UNORM:
      out = {1, 1, 2, "DXGI_FORMAT_B4G4R4A4_UNORM"};
      return true;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R16_FLOAT:
      out = {1, 1, 2, "DXGI_FORMAT_R16"};
      return true;
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R16G16_FLOAT:
      out = {1, 1, 4, "DXGI_FORMAT_R16G16"};
      return true;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      out = {1, 1, 8, "DXGI_FORMAT_R16G16B16A16"};
      return true;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
      out = {1, 1, 4, "DXGI_FORMAT_R32"};
      return true;
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
      out = {1, 1, 8, "DXGI_FORMAT_R32G32"};
      return true;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
      out = {1, 1, 16, "DXGI_FORMAT_R32G32B32A32"};
      return true;
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
      out = {4, 4, 8, "DXGI_FORMAT_BC1"};
      return true;
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
      out = {4, 4, 16, "DXGI_FORMAT_BC2"};
      return true;
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
      out = {4, 4, 16, "DXGI_FORMAT_BC3"};
      return true;
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
      out = {4, 4, 8, "DXGI_FORMAT_BC4"};
      return true;
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
      out = {4, 4, 16, "DXGI_FORMAT_BC5"};
      return true;
    default:
      return false;
  }
}

uint32_t ComputeSubresourceCount(D3D12_RESOURCE_DIMENSION dimension, uint32_t depth_or_array_size,
                                 uint32_t mip_count) {
  return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? mip_count
                                                         : depth_or_array_size * mip_count;
}

bool MapDdsDimension(uint32_t dds_dimension, D3D12_RESOURCE_DIMENSION& out) {
  switch (dds_dimension) {
    case kDdsResourceDimensionTexture1D:
      out = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
      return true;
    case kDdsResourceDimensionTexture2D:
      out = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      return true;
    case kDdsResourceDimensionTexture3D:
      out = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
      return true;
    default:
      return false;
  }
}

bool MapLegacyDdsFourCc(uint32_t four_cc, DXGI_FORMAT& out) {
  switch (four_cc) {
    case kDdsFourCcDxt1:
      out = DXGI_FORMAT_BC1_UNORM;
      return true;
    case kDdsFourCcDxt3:
      out = DXGI_FORMAT_BC2_UNORM;
      return true;
    case kDdsFourCcDxt5:
      out = DXGI_FORMAT_BC3_UNORM;
      return true;
    default:
      return false;
  }
}

uint32_t ToDdsDimension(D3D12_RESOURCE_DIMENSION dimension) {
  switch (dimension) {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
      return kDdsResourceDimensionTexture1D;
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
      return kDdsResourceDimensionTexture3D;
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    default:
      return kDdsResourceDimensionTexture2D;
  }
}

}  // namespace

bool TextureSwapsEnabled() {
  return REXCVAR_GET(ac6_texture_swaps_enabled);
}

bool TextureDumpEnabled() {
  return TextureSwapsEnabled() && REXCVAR_GET(ac6_texture_swaps_dump_enabled);
}

bool TextureReplacementEnabled() {
  return TextureSwapsEnabled() && REXCVAR_GET(ac6_texture_swaps_replace_enabled);
}

bool IsSupportedTextureSwapFormat(DXGI_FORMAT format) {
  DxgiLayoutInfo layout = {};
  return GetDxgiLayoutInfo(format, layout);
}

bool GetTightTextureSubresourceLayout(DXGI_FORMAT format, uint32_t width, uint32_t height,
                                      TextureSubresourceLayout& out) {
  DxgiLayoutInfo info = {};
  if (!GetDxgiLayoutInfo(format, info)) {
    return false;
  }
  const uint32_t width_blocks = std::max((width + info.block_width - 1) / info.block_width, 1u);
  const uint32_t height_blocks = std::max((height + info.block_height - 1) / info.block_height, 1u);
  out.row_pitch = width_blocks * info.bytes_per_block;
  out.row_count = height_blocks;
  out.slice_pitch = out.row_pitch * out.row_count;
  return true;
}

std::string DescribeDxgiFormat(DXGI_FORMAT format) {
  DxgiLayoutInfo layout = {};
  if (GetDxgiLayoutInfo(format, layout)) {
    return layout.name;
  }
  std::ostringstream stream;
  stream << "DXGI_FORMAT_" << uint32_t(format);
  return stream.str();
}

std::string BuildTextureStableKey(uint64_t texture_key_hash, uint32_t base_page, uint32_t mip_page,
                                  uint32_t dimension, uint32_t width, uint32_t height,
                                  uint32_t depth_or_array_size, uint32_t mip_count,
                                  uint32_t guest_format, uint32_t endianness, bool tiled,
                                  bool packed_mips, bool signed_separate, bool scaled_resolve) {
  std::ostringstream stream;
  stream << "tex_" << HexU64(texture_key_hash) << "_bp" << HexU32(base_page) << "_mp"
         << HexU32(mip_page) << "_" << DimensionTag(dimension) << "_" << width << "x" << height
         << "x" << depth_or_array_size << "_m" << mip_count << "_fmt" << guest_format << "_e"
         << endianness << "_t" << (tiled ? 1 : 0) << "_p" << (packed_mips ? 1 : 0) << "_s"
         << (signed_separate ? 1 : 0) << "_r" << (scaled_resolve ? 1 : 0);
  return stream.str();
}

std::filesystem::path GetTextureDumpDdsPath(std::string_view stable_key) {
  PublishCurrentTextureDumpSessionInfo();
  return GetTextureDumpRoot() / (std::string(stable_key) + ".dds");
}

std::filesystem::path GetTextureDumpMetadataPath(std::string_view stable_key) {
  PublishCurrentTextureDumpSessionInfo();
  return GetTextureDumpRoot() / (std::string(stable_key) + ".json");
}

std::filesystem::path GetTextureDumpCurrentSessionRoot() {
  PublishCurrentTextureDumpSessionInfo();
  return GetTextureDumpSessionsRoot() / GetTextureDumpSessionId();
}

std::filesystem::path GetTextureDumpCurrentSessionDdsPath(std::string_view stable_key) {
  return GetTextureDumpCurrentSessionRoot() / (std::string(stable_key) + ".dds");
}

std::filesystem::path GetTextureDumpCurrentSessionMetadataPath(std::string_view stable_key) {
  return GetTextureDumpCurrentSessionRoot() / (std::string(stable_key) + ".json");
}

std::filesystem::path GetTextureDumpCurrentSessionInfoPath() {
  PublishCurrentTextureDumpSessionInfo();
  return GetTextureDumpRoot() / "current_session.txt";
}

bool DumpExists(std::string_view stable_key) {
  std::error_code ec;
  return std::filesystem::exists(GetTextureDumpDdsPath(stable_key), ec);
}

bool MirrorDumpToCurrentSession(std::string_view stable_key, std::string* error_out) {
  PublishCurrentTextureDumpSessionInfo();

  const std::filesystem::path source_dds = GetTextureDumpDdsPath(stable_key);
  const std::filesystem::path source_json = GetTextureDumpMetadataPath(stable_key);
  const std::filesystem::path dest_dds = GetTextureDumpCurrentSessionDdsPath(stable_key);
  const std::filesystem::path dest_json = GetTextureDumpCurrentSessionMetadataPath(stable_key);

  std::error_code ec;
  if (!std::filesystem::exists(source_dds, ec) || ec) {
    if (error_out) {
      *error_out = "source DDS dump does not exist";
    }
    return false;
  }
  ec.clear();
  if (!std::filesystem::exists(source_json, ec) || ec) {
    if (error_out) {
      *error_out = "source metadata dump does not exist";
    }
    return false;
  }

  std::string ensure_error;
  if (!EnsureParentExists(dest_dds, &ensure_error) || !EnsureParentExists(dest_json, &ensure_error)) {
    if (error_out) {
      *error_out = ensure_error;
    }
    return false;
  }

  auto copy_if_needed = [&](const std::filesystem::path& source, const std::filesystem::path& dest,
                            const char* label) -> bool {
    std::error_code local_ec;
    if (std::filesystem::exists(dest, local_ec) && !local_ec) {
      return true;
    }
    local_ec.clear();
    if (!std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing,
                                    local_ec)) {
      if (error_out) {
        *error_out = std::string("failed to mirror ") + label + ": " + local_ec.message();
      }
      return false;
    }
    return true;
  };

  return copy_if_needed(source_dds, dest_dds, "DDS") &&
         copy_if_needed(source_json, dest_json, "metadata");
}

std::optional<std::filesystem::path> ResolveReplacementDdsPath(std::string_view stable_key) {
  if (!TextureReplacementEnabled()) {
    return std::nullopt;
  }

  const std::filesystem::path user_root = GetUserDataRoot();
  std::error_code ec;

  const std::filesystem::path loose_root = user_root / REXCVAR_GET(ac6_texture_swaps_override_dir);
  if (const std::optional<std::filesystem::path> loose_path =
          ResolveReplacementDdsPathInRoot(loose_root, stable_key)) {
    return loose_path;
  }

  const std::filesystem::path mods_root = user_root / REXCVAR_GET(ac6_texture_swaps_mods_dir);
  if (!std::filesystem::exists(mods_root, ec) || ec) {
    return std::nullopt;
  }

  std::vector<std::filesystem::path> mod_roots;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(mods_root, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_directory()) {
      mod_roots.push_back(entry.path());
    }
  }
  std::sort(mod_roots.begin(), mod_roots.end());

  std::optional<std::filesystem::path> resolved;
  for (const std::filesystem::path& mod_root : mod_roots) {
    if (const std::optional<std::filesystem::path> candidate =
            ResolveReplacementDdsPathInRoot(mod_root / "textures", stable_key)) {
      resolved = candidate;
    }
  }
  return resolved;
}

bool LoadDdsFromFile(const std::filesystem::path& path, DdsImageData& out, std::string* error_out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error_out) {
      *error_out = "failed to open file";
    }
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::streamoff file_size = file.tellg();
  file.seekg(0, std::ios::beg);
  if (file_size < std::streamoff(sizeof(uint32_t) + sizeof(DdsHeader))) {
    if (error_out) {
      *error_out = "file is too small to be a DDS";
    }
    return false;
  }

  uint32_t magic = 0;
  DdsHeader header = {};
  file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  file.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!file || magic != kDdsMagic || header.size != sizeof(DdsHeader) ||
      header.pixel_format.size != sizeof(DdsPixelFormat)) {
    if (error_out) {
      *error_out = "invalid DDS header";
    }
    return false;
  }

  DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
  uint32_t depth_or_array_size = 1;
  size_t payload_offset = sizeof(uint32_t) + sizeof(DdsHeader);

  if (header.pixel_format.flags & kDdsPixelFormatFlagsFourCc) {
    if (header.pixel_format.four_cc == kDdsFourCcDx10) {
      if (file_size < std::streamoff(payload_offset + sizeof(DdsHeaderDx10))) {
        if (error_out) {
          *error_out = "file is too small to contain a DX10 DDS header";
        }
        return false;
      }
      DdsHeaderDx10 header_dx10 = {};
      file.read(reinterpret_cast<char*>(&header_dx10), sizeof(header_dx10));
      if (!file) {
        if (error_out) {
          *error_out = "failed to read DX10 DDS header";
        }
        return false;
      }
      payload_offset += sizeof(DdsHeaderDx10);
      if ((header_dx10.misc_flag & kDdsResourceMiscTextureCube) != 0) {
        if (error_out) {
          *error_out = "cube DDS files are not supported by the first-pass texture swap loader";
        }
        return false;
      }
      if (!MapDdsDimension(header_dx10.resource_dimension, dimension)) {
        if (error_out) {
          *error_out = "unsupported DDS resource dimension";
        }
        return false;
      }
      dxgi_format = DXGI_FORMAT(header_dx10.dxgi_format);
      depth_or_array_size =
          dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? std::max(header.depth, 1u)
                                                          : std::max(header_dx10.array_size, 1u);
    } else {
      if (!MapLegacyDdsFourCc(header.pixel_format.four_cc, dxgi_format)) {
        if (error_out) {
          *error_out = "unsupported legacy DDS compression format";
        }
        return false;
      }
      if (header.caps2 & (kDdsCaps2Cubemap | kDdsCaps2Volume)) {
        if (error_out) {
          *error_out = "legacy DDS support is limited to plain 2D textures";
        }
        return false;
      }
      dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      depth_or_array_size = 1;
    }
  } else {
    if (error_out) {
      *error_out = "only FourCC DDS files are supported";
    }
    return false;
  }

  const uint32_t mip_count = std::max(header.mip_map_count, 1u);
  const uint32_t width = std::max(header.width, 1u);
  const uint32_t height = std::max(header.height, 1u);

  if (!IsSupportedTextureSwapFormat(dxgi_format)) {
    if (error_out) {
      *error_out = "unsupported DXGI format in DDS file";
    }
    return false;
  }

  DdsImageData image;
  image.format = dxgi_format;
  image.dimension = dimension;
  image.width = width;
  image.height = height;
  image.depth_or_array_size = depth_or_array_size;
  image.mip_count = mip_count;
  image.is_cube = false;
  image.subresources.reserve(ComputeSubresourceCount(dimension, depth_or_array_size, mip_count));

  std::vector<uint8_t> payload(size_t(file_size) - payload_offset);
  file.read(reinterpret_cast<char*>(payload.data()), std::streamsize(payload.size()));
  if (!file && !payload.empty()) {
    if (error_out) {
      *error_out = "failed to read DDS payload";
    }
    return false;
  }
  size_t payload_cursor = 0;
  const uint32_t subresource_count = ComputeSubresourceCount(dimension, depth_or_array_size, mip_count);
  for (uint32_t subresource_index = 0; subresource_index < subresource_count; ++subresource_index) {
    const uint32_t mip_index =
        dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? subresource_index
                                                        : (subresource_index % mip_count);
    DdsSubresource subresource;
    subresource.width = std::max(width >> mip_index, 1u);
    subresource.height = std::max(height >> mip_index, 1u);
    subresource.depth = dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                            ? std::max(depth_or_array_size >> mip_index, 1u)
                            : 1u;
    TextureSubresourceLayout tight_layout = {};
    if (!GetTightTextureSubresourceLayout(image.format, subresource.width, subresource.height,
                                          tight_layout)) {
      if (error_out) {
        *error_out = "unsupported tight layout for DDS subresource";
      }
      return false;
    }
    const size_t subresource_size = size_t(tight_layout.slice_pitch) * subresource.depth;
    if (payload_cursor + subresource_size > payload.size()) {
      if (error_out) {
        *error_out = "DDS payload is truncated";
      }
      return false;
    }
    subresource.row_pitch = tight_layout.row_pitch;
    subresource.slice_pitch = tight_layout.slice_pitch;
    subresource.data.resize(subresource_size);
    std::copy_n(payload.data() + payload_cursor, subresource_size, subresource.data.data());
    payload_cursor += subresource_size;
    image.subresources.push_back(std::move(subresource));
  }

  out = std::move(image);
  return true;
}

bool WriteDdsToFile(const std::filesystem::path& path, const DdsImageData& data,
                    std::string* error_out) {
  if (data.is_cube) {
    if (error_out) {
      *error_out = "cube textures are not supported for DDS dumping";
    }
    return false;
  }

  const uint32_t expected_subresource_count =
      ComputeSubresourceCount(data.dimension, data.depth_or_array_size, data.mip_count);
  if (data.subresources.size() != expected_subresource_count) {
    if (error_out) {
      *error_out = "DDS subresource count does not match the texture description";
    }
    return false;
  }

  if (!EnsureParentExists(path, error_out)) {
    return false;
  }

  TextureSubresourceLayout base_layout = {};
  if (!GetTightTextureSubresourceLayout(data.format, data.width, data.height, base_layout)) {
    if (error_out) {
      *error_out = "unsupported DXGI format for DDS output";
    }
    return false;
  }

  DdsHeader header = {};
  header.size = sizeof(DdsHeader);
  header.flags = kDdsHeaderFlagsTexture;
  header.height = std::max(data.height, 1u);
  header.width = std::max(data.width, 1u);
  header.pitch_or_linear_size = base_layout.row_pitch;
  header.depth = data.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? data.depth_or_array_size : 0;
  header.mip_map_count = std::max(data.mip_count, 1u);
  header.pixel_format = {sizeof(DdsPixelFormat), kDdsPixelFormatFlagsFourCc, kDdsFourCcDx10, 0, 0, 0, 0, 0};
  header.caps = kDdsCapsTexture;
  header.caps2 = 0;
  if (data.mip_count > 1) {
    header.flags |= kDdsHeaderFlagsMipmap;
    header.caps |= kDdsCapsComplex | kDdsCapsMipmap;
  }
  if (data.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
    header.flags |= kDdsHeaderFlagsDepth;
    header.caps |= kDdsCapsComplex;
    header.caps2 |= kDdsCaps2Volume;
  } else if (data.depth_or_array_size > 1) {
    header.caps |= kDdsCapsComplex;
  }

  DxgiLayoutInfo format_info = {};
  GetDxgiLayoutInfo(data.format, format_info);
  if (format_info.block_width != 1 || format_info.block_height != 1) {
    header.flags |= kDdsHeaderFlagsLinearSize;
    header.pitch_or_linear_size = base_layout.slice_pitch;
  } else {
    header.flags |= kDdsHeaderFlagsPitch;
  }

  DdsHeaderDx10 header_dx10 = {};
  header_dx10.dxgi_format = uint32_t(data.format);
  header_dx10.resource_dimension = ToDdsDimension(data.dimension);
  header_dx10.array_size =
      data.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : std::max(data.depth_or_array_size, 1u);

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error_out) {
      *error_out = "failed to create DDS file";
    }
    return false;
  }

  file.write(reinterpret_cast<const char*>(&kDdsMagic), sizeof(kDdsMagic));
  file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  file.write(reinterpret_cast<const char*>(&header_dx10), sizeof(header_dx10));
  for (const DdsSubresource& subresource : data.subresources) {
    const size_t expected_size = size_t(subresource.slice_pitch) * subresource.depth;
    if (subresource.data.size() != expected_size) {
      if (error_out) {
        *error_out = "DDS subresource payload has an unexpected size";
      }
      return false;
    }
    file.write(reinterpret_cast<const char*>(subresource.data.data()),
               std::streamsize(subresource.data.size()));
  }
  if (!file) {
    if (error_out) {
      *error_out = "failed while writing DDS file";
    }
    return false;
  }

  return true;
}

bool WriteDumpMetadata(const std::filesystem::path& path, const TextureDumpMetadata& metadata,
                       std::string* error_out) {
  if (!EnsureParentExists(path, error_out)) {
    return false;
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error_out) {
      *error_out = "failed to create metadata file";
    }
    return false;
  }

  file << "{\n";
  file << "  \"stable_key\": \"" << EscapeJson(metadata.stable_key) << "\",\n";
  file << "  \"texture_key_hash\": \"0x" << HexU64(metadata.texture_key_hash) << "\",\n";
  file << "  \"base_page\": \"0x" << HexU32(metadata.base_page) << "\",\n";
  file << "  \"mip_page\": \"0x" << HexU32(metadata.mip_page) << "\",\n";
  file << "  \"dimension\": " << metadata.dimension << ",\n";
  file << "  \"width\": " << metadata.width << ",\n";
  file << "  \"height\": " << metadata.height << ",\n";
  file << "  \"depth_or_array_size\": " << metadata.depth_or_array_size << ",\n";
  file << "  \"mip_count\": " << metadata.mip_count << ",\n";
  file << "  \"guest_format\": " << metadata.guest_format << ",\n";
  file << "  \"endianness\": " << metadata.endianness << ",\n";
  file << "  \"dxgi_format\": " << metadata.dxgi_format << ",\n";
  file << "  \"dxgi_format_name\": \"" << EscapeJson(DescribeDxgiFormat(DXGI_FORMAT(metadata.dxgi_format)))
       << "\",\n";
  file << "  \"tiled\": " << (metadata.tiled ? "true" : "false") << ",\n";
  file << "  \"packed_mips\": " << (metadata.packed_mips ? "true" : "false") << ",\n";
  file << "  \"signed_separate\": " << (metadata.signed_separate ? "true" : "false") << ",\n";
  file << "  \"scaled_resolve\": " << (metadata.scaled_resolve ? "true" : "false") << ",\n";
  file << "  \"frame_index\": " << metadata.frame_index << ",\n";
  file << "  \"signature_stable_id\": \"0x" << HexU64(metadata.signature_stable_id) << "\",\n";
  file << "  \"active_vertex_shader_hash\": \"0x" << HexU64(metadata.active_vertex_shader_hash)
       << "\",\n";
  file << "  \"active_pixel_shader_hash\": \"0x" << HexU64(metadata.active_pixel_shader_hash)
       << "\",\n";
  file << "  \"signature_tags\": \"" << EscapeJson(metadata.signature_tags) << "\"\n";
  file << "}\n";

  if (!file) {
    if (error_out) {
      *error_out = "failed while writing metadata file";
    }
    return false;
  }
  return true;
}

}  // namespace ac6::textures
