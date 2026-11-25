// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string_view>

namespace biglybtc::platform {

inline std::filesystem::path pathFromUtf8(std::string_view text) {
  std::filesystem::path path(std::u8string(text.begin(), text.end()));
  path.make_preferred();
  return path;
}

inline std::string pathToUtf8(const std::filesystem::path& path) {
  const auto bytes = path.u8string();
  return std::string(bytes.begin(), bytes.end());
}

} // namespace biglybtc::platform
