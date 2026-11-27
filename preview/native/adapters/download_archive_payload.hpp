// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include "state_file_io.hpp"
#include "biglybtc/library.hpp"
#include <vector>
#include <stop_token>

namespace biglybtc::storage::detail {
struct DownloadArchiveRestoreSource;
struct ArchivedPayloadFile {
  std::filesystem::path path;
  StateStamp stamp;
};
struct ArchivePayloadSelection {
  std::filesystem::path root;
  std::vector<ArchivedPayloadFile> files;
};
std::expected<ArchivePayloadSelection,Error> selectArchivePayload(
    const ProfileLease&,const DownloadArchiveRestoreSource&,std::stop_token={});
std::expected<void,Error> verifyArchivePayloadReferences(
    const ProfileLease&,const core::TorrentHash&,const ArchivePayloadSelection&,std::stop_token={});
}
