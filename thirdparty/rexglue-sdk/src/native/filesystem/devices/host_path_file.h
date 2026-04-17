/**
 * ReXGlue native filesystem layer
 * Part of the AC6 Recompilation project
 */

#pragma once

#include <span>
#include <string>

#include <native/filesystem.h>
#include <native/filesystem/file.h>

namespace rex::filesystem {

class HostPathEntry;

class HostPathFile : public File {
 public:
  HostPathFile(uint32_t file_access, HostPathEntry* entry,
               std::unique_ptr<rex::filesystem::FileHandle> file_handle);
  ~HostPathFile() override;

  void Destroy() override;

  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read) override;
  X_STATUS WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                     size_t* out_bytes_written) override;
  X_STATUS SetLength(size_t length) override;

 private:
  std::unique_ptr<rex::filesystem::FileHandle> file_handle_;
};

}  // namespace rex::filesystem
