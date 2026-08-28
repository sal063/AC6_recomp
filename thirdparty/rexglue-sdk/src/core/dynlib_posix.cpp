#include <rex/platform.h>
#include <rex/platform/dynlib.h>

static_assert(REX_PLATFORM_LINUX || REX_PLATFORM_MAC, "This file is POSIX-only");

#include <dlfcn.h>

namespace rex::platform {

DynamicLibrary::~DynamicLibrary() {
  Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

bool DynamicLibrary::Load(const std::filesystem::path& path) {
  Close();
  handle_ = dlopen(path.c_str(), RTLD_LAZY);
  return handle_ != nullptr;
}

bool DynamicLibrary::LoadIfAlreadyLoaded(const std::filesystem::path& path) {
  Close();
  // RTLD_NOLOAD returns a handle only when the library is already mapped; it
  // still takes a reference, so Close()'s dlclose stays balanced.
  handle_ = dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
  return handle_ != nullptr;
}

void DynamicLibrary::Close() {
  if (handle_) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

void* DynamicLibrary::GetRawSymbol(const char* name) const {
  if (!handle_)
    return nullptr;
  return dlsym(handle_, name);
}

}  // namespace rex::platform
