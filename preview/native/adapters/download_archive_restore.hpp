// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "download_archive.hpp"
#include "original_torrent_identity.hpp"
#include "profile_file_snapshot.hpp"

namespace biglybtc::storage::detail {

struct ArchivedStateEntry {
  std::filesystem::path relativePath;
  StateStamp stamp;
  StateDescriptor directory;
  std::optional<ProfileFileSnapshot> file;
};

// The restorable row and state come from the same held original profile.
// Auxiliary entries are in parent-first order, including the root directory.
struct DownloadArchiveRestoreSource {
  OriginalTorrentIdentity identity;
  ConfigFile configuration;
  std::size_t rowIndex;
  core::Document inventoryRow;
  StateDescriptor archiveDirectory;
  StateStamp archiveDirectoryStamp;
  ProfileFileSnapshot state;
  std::vector<ArchivedStateEntry> auxiliary;
};

std::expected<DownloadArchiveRestoreSource, Error> readDownloadArchiveRestoreSource(
    const ProfileLease&, const core::TorrentHash&, std::stop_token = {});

struct DownloadArchiveRestoreEdits {
  DownloadArchiveRestoreSource source;
  StateDescriptor activeDirectory;
  StateStamp activeDirectoryStamp;
  std::optional<ConfigFile> inventory;
  std::string inventoryAfter;
  std::string archiveAfter;
};

std::expected<DownloadArchiveRestoreEdits, Error> prepareDownloadArchiveRestore(
    const ProfileLease&, const core::TorrentHash&, std::stop_token = {});

} // namespace biglybtc::storage::detail
