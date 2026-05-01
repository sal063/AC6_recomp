#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ac6_pac_index {

enum class StorageKind : uint8_t {
    kRaw = 0,
    kCompressed = 1,
};

struct Entry {
    uint32_t index;
    uint32_t group;
    uint32_t offset;
    uint32_t compressed_size;
    uint32_t decompressed_size;
    StorageKind storage_kind;
    bool is_data01;
};

// Parses DATA.TBL bytes and populates the in-memory index. Idempotent: subsequent
// successful parses replace prior state.
bool LoadFromBuffer(const uint8_t* data, size_t size);

bool IsLoaded();

// Find an entry by (pac archive selector, byte offset, compressed size).
std::optional<Entry> Find(bool is_data01, uint32_t offset, uint32_t compressed_size);

// Returns true if the given resolved guest path names DATA00.PAC or DATA01.PAC,
// and writes the archive selector to *is_data01.
bool ClassifyPacPath(std::string_view path, bool* is_data01);

bool IsDataTblPath(std::string_view path);

// Returns indices of entries in the given archive whose [offset, offset+csize)
// range overlaps the half-open file-byte range [range_begin, range_end).
std::vector<uint32_t> FindOverlapping(bool is_data01, uint32_t range_begin, uint32_t range_end);

// Returns the entry at the given DATA.TBL row index, or nullopt if unloaded
// or out of range.
std::optional<Entry> GetByIndex(uint32_t entry_index);

}  // namespace ac6_pac_index

