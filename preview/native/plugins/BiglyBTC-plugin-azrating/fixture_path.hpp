// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>

namespace biglybtc::plugins::rating {

inline std::filesystem::path fixturePath(const char* input) {
  namespace fs = std::filesystem;
  std::error_code error;
  const auto temporary = fs::temp_directory_path(error);
  if (error) { return {}; }
  const auto root = fs::canonical(temporary, error);
  if (error) { return {}; }
  const auto path = fs::canonical(input, error);
  if (error) { return {}; }
  const auto relative = path.lexically_relative(root);
  if (relative.empty() || !relative.begin()->u8string().starts_with(u8"biglybtc-")
      || !fs::is_regular_file(path, error)) {
    return {};
  }
  return path;
}

} // namespace biglybtc::plugins::rating
