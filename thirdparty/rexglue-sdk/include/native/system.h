// Native runtime - System utilities
// Part of the AC6 Recompilation native foundation

#pragma once

#include <filesystem>
#include <string_view>

#include <native/platform.h>
#include <native/string.h>

namespace rex {

// The URL must include the protocol.
void LaunchWebBrowser(const std::string_view url);
void LaunchFileExplorer(const std::filesystem::path& path);

enum class SimpleMessageBoxType {
  Help,
  Warning,
  Error,
};

// This is expected to block the caller until the message box is closed.
void ShowSimpleMessageBox(SimpleMessageBoxType type, std::string_view message);

}  // namespace rex
