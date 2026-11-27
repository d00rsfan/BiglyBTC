// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include "biglybtc/download_archive_values.hpp"
#include <stop_token>

namespace biglybtc::storage {

inline constexpr char DownloadArchiveFile[] = "dlarchive.config";

struct DownloadArchive {
  std::optional<ConfigFile> source;
  std::vector<core::ArchivedDownload> stubs; // Original rows and order, including invalid identities.
};

std::expected<DownloadArchive, Error> readDownloadArchive(const ProfileLease&, std::stop_token = {});

} // namespace biglybtc::storage
