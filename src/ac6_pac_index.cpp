#include "ac6_pac_index.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <rex/logging.h>

namespace ac6_pac_index {
namespace {

constexpr size_t kHeaderSize = 8;
constexpr size_t kEntrySize = 16;
constexpr uint32_t kGroupBitData01 = 0x01000000u;
constexpr uint32_t kGroupBitRaw = 0x00020000u;

uint32_t ReadBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

struct State {
    std::mutex mutex;
    bool loaded = false;
    std::vector<Entry> entries;
    // Key: archive (false=DATA00, true=DATA01) packed with offset and csize.
    std::unordered_map<uint64_t, uint32_t> by_offset_csize;
};

State& Get() {
    static State state;
    return state;
}

uint64_t MakeKey(bool is_data01, uint32_t offset, uint32_t csize) {
    return (uint64_t(is_data01 ? 1 : 0) << 63) | (uint64_t(offset) << 32) | uint64_t(csize);
}

bool AsciiContainsCi(std::string_view haystack, std::string_view needle) {
    auto eq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), eq) !=
           haystack.end();
}

}  // namespace

bool LoadFromBuffer(const uint8_t* data, size_t size) {
    if (!data || size < kHeaderSize) {
        return false;
    }
    const uint32_t count = ReadBE32(data);
    const size_t expected = kHeaderSize + (size_t(count) * kEntrySize);
    if (size != expected) {
        return false;
    }

    std::vector<Entry> entries;
    entries.reserve(count);
    std::unordered_map<uint64_t, uint32_t> by_key;
    by_key.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = data + kHeaderSize + (size_t(i) * kEntrySize);
        Entry e;
        e.index = i;
        e.group = ReadBE32(p + 0);
        e.offset = ReadBE32(p + 4);
        e.compressed_size = ReadBE32(p + 8);
        e.decompressed_size = ReadBE32(p + 12);
        e.is_data01 = (e.group & kGroupBitData01) != 0;
        e.storage_kind = (e.group & kGroupBitRaw) ? StorageKind::kRaw : StorageKind::kCompressed;
        entries.push_back(e);

        if (e.compressed_size > 0) {
            by_key.emplace(MakeKey(e.is_data01, e.offset, e.compressed_size), i);
        }
    }

    auto& s = Get();
    std::scoped_lock lock(s.mutex);
    s.entries = std::move(entries);
    s.by_offset_csize = std::move(by_key);
    s.loaded = true;

    REXFS_INFO("[AC6 PAC] DATA.TBL parsed: {} entries", s.entries.size());
    return true;
}

bool IsLoaded() {
    auto& s = Get();
    std::scoped_lock lock(s.mutex);
    return s.loaded;
}

std::optional<Entry> Find(bool is_data01, uint32_t offset, uint32_t compressed_size) {
    auto& s = Get();
    std::scoped_lock lock(s.mutex);
    if (!s.loaded) {
        return std::nullopt;
    }
    auto it = s.by_offset_csize.find(MakeKey(is_data01, offset, compressed_size));
    if (it == s.by_offset_csize.end()) {
        return std::nullopt;
    }
    return s.entries[it->second];
}

bool ClassifyPacPath(std::string_view path, bool* is_data01) {
    if (AsciiContainsCi(path, "DATA01.PAC")) {
        if (is_data01) *is_data01 = true;
        return true;
    }
    if (AsciiContainsCi(path, "DATA00.PAC")) {
        if (is_data01) *is_data01 = false;
        return true;
    }
    return false;
}

bool IsDataTblPath(std::string_view path) {
    return AsciiContainsCi(path, "DATA.TBL");
}

std::vector<uint32_t> FindOverlapping(bool is_data01, uint32_t range_begin, uint32_t range_end) {
    std::vector<uint32_t> hits;
    if (range_end <= range_begin) return hits;
    auto& s = Get();
    std::scoped_lock lock(s.mutex);
    if (!s.loaded) return hits;
    for (const auto& e : s.entries) {
        if (e.is_data01 != is_data01 || e.compressed_size == 0) continue;
        const uint32_t e_begin = e.offset;
        const uint32_t e_end = e.offset + e.compressed_size;
        if (e_begin < range_end && range_begin < e_end) {
            hits.push_back(e.index);
        }
    }
    return hits;
}

std::optional<Entry> GetByIndex(uint32_t entry_index) {
    auto& s = Get();
    std::scoped_lock lock(s.mutex);
    if (!s.loaded || entry_index >= s.entries.size()) return std::nullopt;
    return s.entries[entry_index];
}

}  // namespace ac6_pac_index
