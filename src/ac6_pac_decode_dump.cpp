#include "ac6_pac_decode_dump.h"
#include "ac6_pac_index.h"

#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

extern "C" {
#include <lzx.h>
#include <mspack.h>
}

namespace {

// ---------------------------------------------------------------------------
// AC6_DUMP_PAC_DECODED env-var gate
// ---------------------------------------------------------------------------
bool DumpingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("AC6_DUMP_PAC_DECODED");
        return value && value[0] && std::string_view(value) != "0";
    }();
    return enabled;
}

// ---------------------------------------------------------------------------
// Resolve dump directory anchored to the repo root if discoverable, otherwise
// next to the executable.
// ---------------------------------------------------------------------------
std::filesystem::path ExecutableDir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (len > 0 && len < std::size(buffer)) {
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path() : cwd;
}

std::optional<std::filesystem::path> FindRepoRoot(std::filesystem::path start) {
    std::error_code ec;
    for (auto cur = start; !cur.empty(); cur = cur.parent_path()) {
        if (std::filesystem::exists(cur / "tools" / "run_ac6_asset_pipeline.py", ec)) {
            return cur;
        }
        if (cur.has_parent_path() && cur == cur.parent_path()) {
            break;
        }
    }
    return std::nullopt;
}

const std::filesystem::path& DumpRoot() {
    static const std::filesystem::path root = [] {
        const auto exe_dir = ExecutableDir();
        const auto repo = FindRepoRoot(exe_dir);
        std::filesystem::path chosen = repo ? *repo / "out" / "ac6_pac_runtime_dump"
                                            : exe_dir / "ac6_pac_runtime_dump";
        return chosen;
    }();
    return root;
}

std::mutex& DumpMutex() {
    static std::mutex mutex;
    return mutex;
}

// ---------------------------------------------------------------------------
// In-memory libmspack LZX adapter (modeled on tools/pac_probe_lzx.cpp).
// ---------------------------------------------------------------------------
struct MemInput {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

struct MemOutput {
    std::vector<uint8_t> bytes;
};

struct MemFile {
    mspack_file base{};
    MemInput* in = nullptr;
    MemOutput* out = nullptr;
};

int MemRead(mspack_file* f, void* buf, int bytes) {
    auto* h = reinterpret_cast<MemFile*>(f);
    if (!h || !h->in || bytes < 0) return -1;
    const size_t avail = h->in->size - h->in->pos;
    const size_t n = std::min<size_t>(avail, static_cast<size_t>(bytes));
    if (n) std::memcpy(buf, h->in->data + h->in->pos, n);
    h->in->pos += n;
    return static_cast<int>(n);
}

int MemWrite(mspack_file* f, void* buf, int bytes) {
    auto* h = reinterpret_cast<MemFile*>(f);
    if (!h || !h->out || bytes < 0) return -1;
    const auto* src = static_cast<const uint8_t*>(buf);
    h->out->bytes.insert(h->out->bytes.end(), src, src + bytes);
    return bytes;
}

int MemSeek(mspack_file* f, off_t offset, int mode) {
    auto* h = reinterpret_cast<MemFile*>(f);
    if (!h || !h->in) return -1;
    size_t base = 0;
    switch (mode) {
        case MSPACK_SYS_SEEK_START: base = 0; break;
        case MSPACK_SYS_SEEK_CUR:   base = h->in->pos; break;
        case MSPACK_SYS_SEEK_END:   base = h->in->size; break;
        default:                    return -1;
    }
    if (offset < 0 && static_cast<size_t>(-offset) > base) return -1;
    const size_t next = offset >= 0 ? base + size_t(offset) : base - size_t(-offset);
    if (next > h->in->size) return -1;
    h->in->pos = next;
    return 0;
}

off_t MemTell(mspack_file* f) {
    auto* h = reinterpret_cast<MemFile*>(f);
    return h && h->in ? static_cast<off_t>(h->in->pos) : off_t(-1);
}

void  MemMessage(mspack_file*, const char*, ...) {}
void* MemAlloc(mspack_system*, size_t bytes) { return std::malloc(bytes); }
void  MemFree(void* p) { std::free(p); }
void  MemCopy(void* src, void* dst, size_t bytes) { std::memcpy(dst, src, bytes); }

mspack_system MakeMemSystem() {
    mspack_system s{};
    s.read = &MemRead;
    s.write = &MemWrite;
    s.seek = &MemSeek;
    s.tell = &MemTell;
    s.message = &MemMessage;
    s.alloc = &MemAlloc;
    s.free = &MemFree;
    s.copy = &MemCopy;
    return s;
}

bool TryLzxOnce(const uint8_t* compressed, uint32_t csize, uint32_t usize,
                int window_bits, int reset_interval, std::vector<uint8_t>* out) {
    MemInput in{compressed, csize, 0};
    MemOutput dst;
    MemFile in_file{};
    MemFile out_file{};
    in_file.in = &in;
    out_file.out = &dst;

    mspack_system sys = MakeMemSystem();
    auto* lzx = lzxd_init(&sys, &in_file.base, &out_file.base, window_bits, reset_interval,
                          1 << 15, static_cast<off_t>(usize), 0);
    if (!lzx) return false;
    const int status = lzxd_decompress(lzx, static_cast<off_t>(usize));
    lzxd_free(lzx);
    if (status != MSPACK_ERR_OK || dst.bytes.size() != usize) {
        return false;
    }
    *out = std::move(dst.bytes);
    return true;
}

struct LzxParams {
    int window_bits;
    int reset_interval;
};

bool DecompressLzx(const uint8_t* compressed, uint32_t csize, uint32_t usize,
                   std::vector<uint8_t>* out) {
    static std::mutex cache_mutex;
    static std::optional<LzxParams> cached;

    {
        std::scoped_lock lock(cache_mutex);
        if (cached) {
            if (TryLzxOnce(compressed, csize, usize, cached->window_bits, cached->reset_interval,
                           out)) {
                return true;
            }
            // Cached params failed; fall through to re-probe.
        }
    }

    constexpr std::array<int, 7> kResetCandidates{0, 1, 2, 4, 8, 16, 32};
    for (int wb = 15; wb <= 21; ++wb) {
        for (int ri : kResetCandidates) {
            if (TryLzxOnce(compressed, csize, usize, wb, ri, out)) {
                std::scoped_lock lock(cache_mutex);
                cached = LzxParams{wb, ri};
                REXFS_INFO("[AC6 PAC] LZX params discovered: window_bits={} reset_interval={}", wb,
                           ri);
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Filename helper.
// ---------------------------------------------------------------------------
std::filesystem::path BuildDumpPath(uint32_t entry_index, uint8_t mode, uint32_t csize,
                                    uint32_t usize, uint32_t source_offset) {
    std::ostringstream name;
    name << "entry_" << entry_index << "_mode" << uint32_t(mode) << "_c" << csize << "_u" << usize
         << "_off" << std::hex << source_offset << std::dec << ".bin";
    return DumpRoot() / name.str();
}

bool WriteBlob(const std::filesystem::path& path, const uint8_t* data, size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(DumpRoot(), ec);
    if (ec) {
        REXFS_ERROR("[AC6 PAC] failed to create dump directory {}: {}", DumpRoot().string(),
                    ec.message());
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        REXFS_ERROR("[AC6 PAC] failed to open dump {}", path.string());
        return false;
    }
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file) {
        REXFS_ERROR("[AC6 PAC] failed to write dump {}", path.string());
        return false;
    }
    return true;
}

const uint8_t* TranslateGuestBuffer(uint32_t guest_addr, uint32_t length) {
    if (guest_addr == 0 || length == 0) return nullptr;
    auto* memory = REX_KERNEL_MEMORY();
    if (!memory) return nullptr;
    if (guest_addr > UINT32_MAX - length) return nullptr;
    if (!memory->LookupHeap(guest_addr) || !memory->LookupHeap(guest_addr + length - 1)) {
        return nullptr;
    }
    return memory->TranslateVirtual<const uint8_t*>(guest_addr);
}

// ---------------------------------------------------------------------------
// Per-archive read-chunk tracking. The game streams PAC content in 0x40000
// chunks; we accumulate them and only dump once a known DATA.TBL entry's
// byte range is fully covered.
// ---------------------------------------------------------------------------
struct ChunkRec {
    uint32_t length;
    uint32_t guest_buffer;
};

struct ArchiveState {
    // file_offset -> chunk; sorted, allows range queries.
    std::map<uint32_t, ChunkRec> chunks;
    // Indices of DATA.TBL entries we have already written out.
    std::unordered_set<uint32_t> dumped;
};

ArchiveState& GetArchive(bool is_data01) {
    static ArchiveState data00;
    static ArchiveState data01;
    return is_data01 ? data01 : data00;
}

std::mutex& ArchiveMutex() {
    static std::mutex m;
    return m;
}

// Returns true if the half-open range [start, end) is fully covered by chunks
// in the (offset-sorted) map, with no gaps.
bool IsRangeCovered(const std::map<uint32_t, ChunkRec>& chunks, uint32_t start, uint32_t end) {
    if (end <= start) return false;
    auto it = chunks.upper_bound(start);
    if (it == chunks.begin()) return false;
    --it;  // greatest chunk_offset <= start
    uint32_t cursor = start;
    while (cursor < end) {
        const uint32_t chunk_off = it->first;
        const uint32_t chunk_end = chunk_off + it->second.length;
        if (chunk_off > cursor) return false;       // gap
        if (chunk_end <= cursor) return false;      // chunk ends before cursor
        cursor = chunk_end;
        ++it;
        if (cursor >= end) break;
        if (it == chunks.end()) return false;
        // The next chunk must start at or before cursor.
        if (it->first > cursor) return false;
    }
    return cursor >= end;
}

// Materialize the contiguous bytes [start, end) from the recorded chunks into
// a host buffer. Returns empty on failure.
std::vector<uint8_t> GatherRange(const std::map<uint32_t, ChunkRec>& chunks, uint32_t start,
                                 uint32_t end) {
    std::vector<uint8_t> out;
    if (end <= start) return out;
    out.reserve(end - start);

    auto it = chunks.upper_bound(start);
    if (it == chunks.begin()) return {};
    --it;

    uint32_t cursor = start;
    while (cursor < end) {
        const uint32_t chunk_off = it->first;
        const uint32_t chunk_len = it->second.length;
        if (chunk_off > cursor || chunk_off + chunk_len <= cursor) return {};
        const uint32_t local = cursor - chunk_off;
        const uint32_t take = std::min<uint32_t>(chunk_len - local, end - cursor);
        const uint8_t* host = TranslateGuestBuffer(it->second.guest_buffer + local, take);
        if (!host) return {};
        out.insert(out.end(), host, host + take);
        cursor += take;
        if (cursor >= end) break;
        ++it;
        if (it == chunks.end()) return {};
    }
    return out;
}

void TryDumpEntry(bool is_data01, uint32_t entry_index, ArchiveState* state) {
    if (state->dumped.count(entry_index)) return;
    auto rec = ac6_pac_index::GetByIndex(entry_index);
    if (!rec) return;
    if (rec->is_data01 != is_data01) return;
    if (rec->compressed_size == 0) return;

    const uint32_t start = rec->offset;
    const uint32_t end = rec->offset + rec->compressed_size;
    if (!IsRangeCovered(state->chunks, start, end)) return;

    std::vector<uint8_t> raw = GatherRange(state->chunks, start, end);
    if (raw.size() != rec->compressed_size) return;

    const uint16_t entry_index_u16 = static_cast<uint16_t>(rec->index & 0xFFFFu);

    if (rec->storage_kind == ac6_pac_index::StorageKind::kRaw) {
        if (rec->decompressed_size > raw.size()) {
            REXFS_WARN(
                "[AC6 PAC] raw entry size mismatch; refusing overread: entry={} csize=0x{:x} "
                "usize=0x{:x} pac_offset=0x{:x}",
                rec->index, rec->compressed_size, rec->decompressed_size, rec->offset);
            state->dumped.insert(entry_index);
            return;
        }
        Ac6DumpPacDecodedEntry(entry_index_u16, /*mode=*/0, rec->compressed_size,
                               rec->decompressed_size, rec->offset, raw.data());
        state->dumped.insert(entry_index);
        return;
    }

    // Compressed (AC6 "mode 1") entries use a custom codec, not vanilla LZX
    // (offline pac_extract_offline.exe failed on all 800 compressed entries).
    // Persist the raw compressed bytes for later analysis.
    static std::atomic<bool> logged_first_compressed{false};
    if (!logged_first_compressed.exchange(true)) {
        std::ostringstream hex;
        hex << std::hex << std::setfill('0');
        const size_t n = std::min<size_t>(64, raw.size());
        for (size_t i = 0; i < n; ++i) {
            hex << std::setw(2) << uint32_t(raw[i]) << ' ';
        }
        REXFS_INFO(
            "[AC6 PAC] first compressed entry (index={} csize=0x{:x} usize=0x{:x} "
            "pac_offset=0x{:x}) head[64]={}",
            rec->index, rec->compressed_size, rec->decompressed_size, rec->offset, hex.str());
    }

    std::filesystem::path out_path = BuildDumpPath(entry_index_u16, /*mode=*/1,
                                                   rec->compressed_size,
                                                   rec->decompressed_size, rec->offset);
    out_path.replace_extension(".compressed.bin");

    {
        std::scoped_lock dump_lock(DumpMutex());
        std::error_code ec;
        std::filesystem::create_directories(DumpRoot(), ec);
        std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
        if (f) {
            f.write(reinterpret_cast<const char*>(raw.data()),
                    static_cast<std::streamsize>(raw.size()));
        }
    }

    REXFS_WARN(
        "[AC6 PAC] compressed entry written as raw blob (no host-side mode-1 decoder): "
        "entry={} csize=0x{:x} usize=0x{:x} pac_offset=0x{:x} path={}",
        rec->index, rec->compressed_size, rec->decompressed_size, rec->offset, out_path.string());

    state->dumped.insert(entry_index);
}

}  // namespace

void Ac6DumpPacDecodedEntry(uint16_t entry_index, uint8_t codec_mode, uint32_t compressed_size,
                            uint32_t decompressed_size, uint32_t source_offset,
                            const uint8_t* host_data) {
    if (!DumpingEnabled() || !host_data || decompressed_size == 0) return;

    std::scoped_lock lock(DumpMutex());
    const auto path =
        BuildDumpPath(entry_index, codec_mode, compressed_size, decompressed_size, source_offset);
    if (!WriteBlob(path, host_data, decompressed_size)) return;

    REXFS_INFO(
        "[AC6 PAC] dumped decoded entry index={} mode={} compressed=0x{:x} decompressed=0x{:x} "
        "source_offset=0x{:x} path={}",
        entry_index, uint32_t(codec_mode), compressed_size, decompressed_size, source_offset,
        path.string());
}

void Ac6OnPacReadCompleted(std::string_view path, uint32_t guest_buffer, uint64_t file_offset,
                           uint32_t bytes_read) {
    if (!DumpingEnabled() || guest_buffer == 0 || bytes_read == 0) return;

    // DATA.TBL: parse and cache the index.
    if (ac6_pac_index::IsDataTblPath(path)) {
        if (file_offset != 0) return;  // require a full-file read starting at 0
        const uint8_t* host = TranslateGuestBuffer(guest_buffer, bytes_read);
        if (!host) return;
        if (ac6_pac_index::LoadFromBuffer(host, bytes_read)) {
            // Successfully indexed; nothing else to do for DATA.TBL itself.
        }
        return;
    }

    // DATA00/01.PAC: record this chunk, then check if any DATA.TBL entry's
    // full range is now covered by recorded reads.
    bool is_data01 = false;
    if (!ac6_pac_index::ClassifyPacPath(path, &is_data01)) return;
    if (!ac6_pac_index::IsLoaded()) return;
    if (file_offset > 0xFFFFFFFFu) return;

    const uint32_t offset_u32 = static_cast<uint32_t>(file_offset);
    const uint32_t end_u32 =
        bytes_read > UINT32_MAX - offset_u32 ? UINT32_MAX : offset_u32 + bytes_read;

    std::scoped_lock lock(ArchiveMutex());
    auto& archive = GetArchive(is_data01);
    archive.chunks[offset_u32] = ChunkRec{bytes_read, guest_buffer};

    const auto candidates = ac6_pac_index::FindOverlapping(is_data01, offset_u32, end_u32);

    // One-shot diagnostic on first overlapping read after DATA.TBL is loaded.
    static std::atomic<bool> logged_first_overlap{false};
    if (!candidates.empty() && !logged_first_overlap.exchange(true)) {
        const auto rec = ac6_pac_index::GetByIndex(candidates.front());
        REXFS_INFO(
            "[AC6 PAC] first overlap: archive=DATA0{} chunk[off=0x{:x},len=0x{:x}] candidate "
            "entry={} entry_range=[0x{:x},0x{:x}) csize=0x{:x}",
            is_data01 ? "1" : "0", offset_u32, bytes_read, candidates.front(),
            rec ? rec->offset : 0u, rec ? rec->offset + rec->compressed_size : 0u,
            rec ? rec->compressed_size : 0u);
    }

    for (uint32_t entry_index : candidates) {
        TryDumpEntry(is_data01, entry_index, &archive);
    }

    // Periodic progress log so we can see if entries ever fully cover.
    static std::atomic<uint64_t> chunk_count{0};
    const uint64_t n = chunk_count.fetch_add(1) + 1;
    if (n == 1 || n == 100 || n == 1000 || (n % 5000) == 0) {
        REXFS_INFO("[AC6 PAC] progress: archive=DATA0{} chunks_recorded={} dumped_entries={}",
                   is_data01 ? "1" : "0", archive.chunks.size(), archive.dumped.size());
    }
}
