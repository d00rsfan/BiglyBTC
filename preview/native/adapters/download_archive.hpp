// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include "biglybtc/download_archive_values.hpp"
#include <span>
#include <stop_token>

namespace biglybtc::storage {

class TagManagerStartupMode;
inline constexpr char DownloadArchiveFile[] = "dlarchive.config";

struct DownloadArchive {
  std::optional<ConfigFile> source;
  std::vector<core::ArchivedDownload> stubs; // Original rows and order, including invalid identities.
};

std::expected<DownloadArchive, Error> readDownloadArchive(const ProfileLease&, std::stop_token = {});
std::string archiveRestoredTagName(const TagManagerStartupMode&);

enum class DownloadArchiveRestoreStage { JournalSynced, StatePublished, InventoryPublished, CatalogPublished, TagsPublished };
using DownloadArchiveRestoreObserver = void (*)(DownloadArchiveRestoreStage);

// Runs on the serial profile owner. On interruption the exact intent can be
// resumed under a freshly held lease before any ordinary library writers start.
std::expected<void, Error> restoreArchivedDownload(const ProfileLease&, const core::TorrentHash&,
    std::stop_token = {}, DownloadArchiveRestoreObserver = nullptr, const TagManagerStartupMode* = nullptr,
    bool markRestored = false);
std::expected<void, Error> archiveStoppedDownload(const ProfileLease&, const core::TorrentHash&,
    std::int64_t archivedMillis, std::span<const std::string> manualTags = {}, std::stop_token = {},
    DownloadArchiveRestoreObserver = nullptr);
std::expected<void, Error> removeArchivedDownload(const ProfileLease&, const core::TorrentHash&,
    std::stop_token = {}, DownloadArchiveRestoreObserver = nullptr);
bool hasPendingArchiveRestore(const ProfileLease&) noexcept;
std::expected<bool, Error> recoverArchiveRestore(const ProfileLease&);

} // namespace biglybtc::storage
