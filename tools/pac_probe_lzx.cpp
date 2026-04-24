#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include "mspack/lzx.h"
#include "mspack/mspack.h"
}

namespace {

constexpr uint32_t kEntrySize = 16;
constexpr uint32_t kHeaderSize = 8;
constexpr int kInputBufferSize = 1 << 15;

struct TblEntry {
  uint32_t group;
  uint32_t offset;
  uint32_t compressed_size;
  uint32_t decompressed_size;
};

struct ProbeInput {
  std::vector<uint8_t> data;
  size_t pos = 0;
};

struct ProbeOutput {
  std::vector<uint8_t> data;
};

struct ProbeFile {
  mspack_file base{};
  ProbeInput* input = nullptr;
  ProbeOutput* output = nullptr;
};

struct ProbeResult {
  int window_bits = 0;
  int reset_interval = 0;
  int status = MSPACK_ERR_ARGS;
  std::vector<uint8_t> output;
};

uint32_t ReadBE32(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
         (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

std::string HexPrefix(const std::vector<uint8_t>& data, size_t count) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  const size_t limit = std::min(count, data.size());
  for (size_t i = 0; i < limit; ++i) {
    out << std::setw(2) << unsigned(data[i]);
  }
  return out.str();
}

std::string AsciiPrefix(const std::vector<uint8_t>& data, size_t count) {
  std::string out;
  const size_t limit = std::min(count, data.size());
  out.reserve(limit);
  for (size_t i = 0; i < limit; ++i) {
    const unsigned char c = data[i];
    out.push_back(std::isprint(c) ? char(c) : '.');
  }
  return out;
}

std::optional<std::vector<uint8_t>> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!data.empty()) {
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) {
      return std::nullopt;
    }
  }
  return data;
}

std::optional<std::vector<TblEntry>> ParseTbl(const std::string& path) {
  const auto bytes = ReadFile(path);
  if (!bytes || bytes->size() < kHeaderSize) {
    return std::nullopt;
  }

  const uint32_t count = ReadBE32(bytes->data());
  const uint32_t pack_count = ReadBE32(bytes->data() + 4);
  (void)pack_count;
  if (bytes->size() != kHeaderSize + (size_t(count) * kEntrySize)) {
    return std::nullopt;
  }

  std::vector<TblEntry> entries;
  entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* p = bytes->data() + kHeaderSize + (i * kEntrySize);
    entries.push_back(TblEntry{
        ReadBE32(p + 0),
        ReadBE32(p + 4),
        ReadBE32(p + 8),
        ReadBE32(p + 12),
    });
  }
  return entries;
}

int ProbeRead(mspack_file* file, void* buffer, int bytes) {
  auto* handle = reinterpret_cast<ProbeFile*>(file);
  if (!handle || !handle->input || bytes < 0) {
    return -1;
  }
  const size_t remaining = handle->input->data.size() - handle->input->pos;
  const size_t to_read = std::min<size_t>(remaining, static_cast<size_t>(bytes));
  if (to_read > 0) {
    std::memcpy(buffer, handle->input->data.data() + handle->input->pos, to_read);
    handle->input->pos += to_read;
  }
  return static_cast<int>(to_read);
}

int ProbeWrite(mspack_file* file, void* buffer, int bytes) {
  auto* handle = reinterpret_cast<ProbeFile*>(file);
  if (!handle || !handle->output || bytes < 0) {
    return -1;
  }
  const auto* src = reinterpret_cast<const uint8_t*>(buffer);
  handle->output->data.insert(handle->output->data.end(), src, src + bytes);
  return bytes;
}

int ProbeSeek(mspack_file* file, off_t offset, int mode) {
  auto* handle = reinterpret_cast<ProbeFile*>(file);
  if (!handle || !handle->input) {
    return -1;
  }

  size_t base = 0;
  switch (mode) {
    case MSPACK_SYS_SEEK_START:
      base = 0;
      break;
    case MSPACK_SYS_SEEK_CUR:
      base = handle->input->pos;
      break;
    case MSPACK_SYS_SEEK_END:
      base = handle->input->data.size();
      break;
    default:
      return -1;
  }

  if (offset < 0 && static_cast<size_t>(-offset) > base) {
    return -1;
  }

  const size_t next = offset >= 0 ? base + static_cast<size_t>(offset)
                                  : base - static_cast<size_t>(-offset);
  if (next > handle->input->data.size()) {
    return -1;
  }
  handle->input->pos = next;
  return 0;
}

off_t ProbeTell(mspack_file* file) {
  auto* handle = reinterpret_cast<ProbeFile*>(file);
  if (!handle || !handle->input) {
    return off_t(-1);
  }
  return static_cast<off_t>(handle->input->pos);
}

void ProbeMessage(mspack_file*, const char* format, ...) {
  std::va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  std::fputc('\n', stderr);
  va_end(args);
}

void* ProbeAlloc(mspack_system*, size_t bytes) {
  return std::malloc(bytes);
}

void ProbeFree(void* ptr) {
  std::free(ptr);
}

void ProbeCopy(void* src, void* dest, size_t bytes) {
  std::memcpy(dest, src, bytes);
}

ProbeResult TryLzx(const std::vector<uint8_t>& compressed, uint32_t expected_size,
                   int window_bits, int reset_interval) {
  ProbeInput input{compressed, 0};
  ProbeOutput output;
  ProbeFile in_file{};
  ProbeFile out_file{};
  in_file.input = &input;
  out_file.output = &output;

  mspack_system system{};
  system.open = nullptr;
  system.close = nullptr;
  system.read = &ProbeRead;
  system.write = &ProbeWrite;
  system.seek = &ProbeSeek;
  system.tell = &ProbeTell;
  system.message = &ProbeMessage;
  system.alloc = &ProbeAlloc;
  system.free = &ProbeFree;
  system.copy = &ProbeCopy;
  system.null_ptr = nullptr;

  ProbeResult result;
  result.window_bits = window_bits;
  result.reset_interval = reset_interval;

  lzxd_stream* lzx = lzxd_init(&system, &in_file.base, &out_file.base, window_bits,
                               reset_interval, kInputBufferSize, expected_size, 0);
  if (!lzx) {
    result.status = MSPACK_ERR_NOMEMORY;
    return result;
  }

  result.status = lzxd_decompress(lzx, expected_size);
  result.output = std::move(output.data);
  lzxd_free(lzx);
  return result;
}

std::string PacPathForGroup(const std::string& asset_root, uint32_t group) {
  const bool is_data01 = (group & 0x01000000u) != 0;
  return asset_root + "\\" + (is_data01 ? "DATA01.PAC" : "DATA00.PAC");
}

void Usage() {
  std::cerr << "usage: pac_probe_lzx <asset_root> <entry_index>\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    Usage();
    return 1;
  }

  const std::string asset_root = argv[1];
  const int entry_index = std::atoi(argv[2]);
  if (entry_index < 0) {
    std::cerr << "invalid entry index\n";
    return 1;
  }

  const auto entries = ParseTbl(asset_root + "\\DATA.TBL");
  if (!entries) {
    std::cerr << "failed to parse DATA.TBL\n";
    return 1;
  }
  if (static_cast<size_t>(entry_index) >= entries->size()) {
    std::cerr << "entry index out of range\n";
    return 1;
  }

  const TblEntry& entry = (*entries)[entry_index];
  const auto pac_bytes = ReadFile(PacPathForGroup(asset_root, entry.group));
  if (!pac_bytes) {
    std::cerr << "failed to read PAC file\n";
    return 1;
  }
  if (size_t(entry.offset) + size_t(entry.compressed_size) > pac_bytes->size()) {
    std::cerr << "entry is out of bounds for PAC file\n";
    return 1;
  }

  const std::vector<uint8_t> compressed(
      pac_bytes->begin() + entry.offset,
      pac_bytes->begin() + entry.offset + entry.compressed_size);

  std::cout << "entry=" << entry_index << " group=0x" << std::hex << entry.group
            << " offset=0x" << entry.offset << " csize=0x" << entry.compressed_size
            << " usize=0x" << entry.decompressed_size << std::dec << "\n";
  std::cout << "compressed_head_hex=" << HexPrefix(compressed, 32) << "\n";

  std::array<int, 7> reset_candidates{0, 1, 2, 4, 8, 16, 32};
  bool found = false;
  for (int window_bits = 15; window_bits <= 21; ++window_bits) {
    for (int reset_interval : reset_candidates) {
      ProbeResult result = TryLzx(compressed, entry.decompressed_size, window_bits, reset_interval);
      if (result.status == MSPACK_ERR_OK && result.output.size() == entry.decompressed_size) {
        found = true;
        std::cout << "OK window_bits=" << window_bits
                  << " reset_interval=" << reset_interval
                  << " out_head_hex=" << HexPrefix(result.output, 32)
                  << " out_head_ascii=" << AsciiPrefix(result.output, 32) << "\n";
      } else {
        std::cout << "FAIL window_bits=" << window_bits
                  << " reset_interval=" << reset_interval
                  << " status=" << result.status
                  << " produced=" << result.output.size() << "\n";
      }
    }
  }

  return found ? 0 : 2;
}
