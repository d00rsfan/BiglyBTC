// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "original_torrent_identity.hpp"

#include <span>
#include <stop_token>

namespace biglybtc::storage::detail {

struct DownloadArchiveExportRecord {
  OriginalTorrentIdentity identity;
  std::size_t inventoryIndex;
  std::string stub;
  std::string state;
};

// Project the actual stopped original row and its authoritative primary/cache
// state into Java's archive format. The owner still guards and publishes the
// selected source and removes the original library entry.
std::expected<DownloadArchiveExportRecord, Error> prepareArchiveExportRecord(const ProfileLease&,
    const core::TorrentHash&, std::int64_t archivedMillis, std::span<const std::string> manualTags = {},
    std::stop_token = {});

} // namespace biglybtc::storage::detail
