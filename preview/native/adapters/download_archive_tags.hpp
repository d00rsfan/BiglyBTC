// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/library.hpp"
#include "profile.hpp"
#include "tag_manager_startup.hpp"

namespace biglybtc::storage::detail {

struct ArchiveTagRestore {
  std::optional<ConfigFile> original;
  std::string after;
};

std::expected<std::optional<ArchiveTagRestore>, Error> prepareArchiveTagRestore(const ProfileLease&,
    const TagManagerStartupMode&, const core::TorrentHash&, std::span<const std::string> names,
    std::int64_t addedSeconds, std::stop_token);

} // namespace biglybtc::storage::detail
