/**
 * ReXGlue native filesystem layer
 * Part of the AC6 Recompilation project
 */

#include <native/filesystem/device.h>
#include <rex/logging.h>

namespace rex::filesystem {

Device::Device(const std::string_view mount_path) : mount_path_(mount_path) {}
Device::~Device() = default;

}  // namespace rex::filesystem
