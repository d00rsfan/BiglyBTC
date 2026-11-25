// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"

namespace biglybtc::storage::detail {

inline bool validConfigName(std::string_view name) {
  if (name.empty() || name.size() > 240 || name.front() == '.' || (!name.ends_with(".config") && name != "BiglyBT.statistics")) {
    return false;
  }
  for (const unsigned char value : name) {
    if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_')) {
      return false;
    }
  }
  return true;
}

struct ConfigDirectoryIdentity {
  std::uint64_t volume;
  std::uint64_t file;
};

std::expected<ConfigDirectoryIdentity, Error> profileConfigDirectory(const ProfileLease& lease);
std::expected<std::optional<std::string>, Error> readProfileConfig(const ProfileLease& lease,
    const std::string& filename, std::size_t maximum);

} // namespace biglybtc::storage::detail
