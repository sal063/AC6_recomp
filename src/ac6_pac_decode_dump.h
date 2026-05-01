#pragma once

#include <cstdint>
#include <string_view>

// Writes a single decoded PAC entry to the runtime dump directory.
// Filename format: entry_<index>_mode<mode>_c<csize>_u<usize>_off<hex_offset>.bin
void Ac6DumpPacDecodedEntry(uint16_t entry_index, uint8_t codec_mode, uint32_t compressed_size,
                            uint32_t decompressed_size, uint32_t source_offset,
                            const uint8_t* host_data);

// Hook called from the kernel-side NtReadFile completion path for any read
// targeting an AC6 PAC archive (DATA00.PAC, DATA01.PAC) or DATA.TBL itself.
// - For DATA.TBL reads: parses and caches the index.
// - For DATA00/01.PAC reads whose (offset, length) match a cached DATA.TBL
//   entry: dumps the entry (decompressing first if compressed).
//
// All work is gated on AC6_DUMP_PAC_DECODED=1; otherwise this is a no-op.
//
// Args:
//   path           - guest path of the file just read (e.g. "game:\\DATA00.PAC")
//   guest_buffer   - guest virtual address of the read destination buffer
//   file_offset    - byte offset within the file where the read started
//   bytes_read     - number of bytes successfully read
void Ac6OnPacReadCompleted(std::string_view path, uint32_t guest_buffer, uint64_t file_offset,
                           uint32_t bytes_read);
