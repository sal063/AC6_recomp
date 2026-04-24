#include <rex/logging.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace {

bool DumpingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("AC6_DUMP_PAC_DECODED");
        return value && value[0] && std::string_view(value) != "0";
    }();
    return enabled;
}

std::filesystem::path DumpRoot() {
    return std::filesystem::path("out") / "ac6_pac_runtime_dump";
}

std::mutex& DumpMutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

void Ac6DumpPacDecodedEntry(uint16_t entry_index, uint8_t codec_mode, uint32_t compressed_size,
                            uint32_t decompressed_size, uint32_t source_offset,
                            const uint8_t* host_data) {
    if (!DumpingEnabled() || !host_data || decompressed_size == 0) {
        return;
    }

    std::scoped_lock lock(DumpMutex());

    std::error_code ec;
    const auto root = DumpRoot();
    std::filesystem::create_directories(root, ec);
    if (ec) {
        REXFS_ERROR("[AC6 PAC] failed to create dump directory {}: {}", root.string(), ec.message());
        return;
    }

    std::ostringstream name;
    name << "entry_" << entry_index << "_mode" << static_cast<uint32_t>(codec_mode) << "_c" << compressed_size
         << "_u" << decompressed_size << "_off" << std::hex << source_offset << std::dec << ".bin";
    const auto path = root / name.str();

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        REXFS_ERROR("[AC6 PAC] failed to open decoded dump {}", path.string());
        return;
    }

    file.write(reinterpret_cast<const char*>(host_data), static_cast<std::streamsize>(decompressed_size));
    if (!file) {
        REXFS_ERROR("[AC6 PAC] failed to write decoded dump {}", path.string());
        return;
    }

    REXFS_INFO(
        "[AC6 PAC] dumped decoded entry index={} mode={} compressed=0x{:x} decompressed=0x{:x} "
        "source_offset=0x{:x} path={}",
        entry_index, static_cast<uint32_t>(codec_mode), compressed_size, decompressed_size, source_offset,
        path.string());
}
