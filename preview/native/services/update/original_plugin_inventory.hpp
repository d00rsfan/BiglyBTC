// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "state_file_io.hpp"
#include <QString>
#include <stop_token>
#include <vector>

namespace biglybtc::update {
struct InstalledJavaPluginDirectory {
  QString runtimeId;
  std::string directoryName;
};

std::expected<std::vector<InstalledJavaPluginDirectory>, storage::Error> installedJavaPluginDirectories(
    const storage::ProfileLease&, std::stop_token = {});

namespace detail {
struct OriginalDescriptor {
  QString runtimeId;
  std::string receipt;
};
std::expected<OriginalDescriptor, storage::Error> originalDescriptor(storage::detail::StateHandle, std::string_view);
}
} // namespace biglybtc::update
