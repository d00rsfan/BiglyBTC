// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"

namespace biglybtc::storage {

struct SavedRpcListener {
  std::uint16_t port;
  std::string user;
  std::string passwordSha1;
  bool fullMode;
  bool operator==(const SavedRpcListener&) const = default;
};

// Read the named original WebPlugin configuration under the held profile.
std::expected<SavedRpcListener, Error> readSavedRpcListener(const ProfileLease&, std::string pluginDirectory);
std::expected<SavedRpcListener, Error> inspectSavedRpcListener(const ConfigFile&, std::string pluginDirectory);

} // namespace biglybtc::storage
