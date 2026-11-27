// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "download_archive.hpp"
#include "download_archive_export.hpp"
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
  std::vector<std::string> manualTags;
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

struct DownloadArchiveDiscardEdits {
  DownloadArchiveRestoreSource source;
  ProfileFileSnapshot backup;
  std::string archiveAfter;
};

std::expected<DownloadArchiveDiscardEdits, Error> prepareDownloadArchiveDiscard(
    const ProfileLease&, const core::TorrentHash&, std::stop_token);

struct ArchiveExportCacheEdit {
  std::string leaf;
  ProfileFileSnapshot source;
  std::string after;
};

struct DownloadArchiveExportEdits {
  DownloadArchiveExportRecord record;
  ConfigFile inventory;
  std::optional<ConfigFile> catalog;
  std::string inventoryAfter;
  std::string catalogAfter;
  StateDescriptor activeDirectory;
  StateStamp activeDirectoryStamp;
  StateDescriptor archiveDirectory;
  std::optional<StateStamp> archiveDirectoryStamp;
  ProfileFileSnapshot activeState;
  ProfileFileSnapshot activeBackup;
  std::vector<ArchiveExportCacheEdit> caches;
  std::vector<ArchivedStateEntry> auxiliary;
};

std::expected<DownloadArchiveExportEdits, Error> prepareDownloadArchiveExport(const ProfileLease&,
    const core::TorrentHash&, std::int64_t archivedMillis, std::span<const std::string> manualTags, std::stop_token);

} // namespace biglybtc::storage::detail
