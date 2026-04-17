/**
 * ReXGlue native filesystem layer
 * Part of the AC6 Recompilation project
 */

#pragma once

#include <string>

#include <native/filesystem.h>
#include <native/filesystem/entry.h>

namespace rex::filesystem {

class NullDevice;

class NullEntry : public Entry {
 public:
  NullEntry(Device* device, Entry* parent, std::string path);
  ~NullEntry() override;

  static NullEntry* Create(Device* device, Entry* parent, const std::string& path);

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  bool can_map() const override { return false; }

 private:
  friend class NullDevice;
};

}  // namespace rex::filesystem
