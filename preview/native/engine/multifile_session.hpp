#pragma once

#include "local_session.hpp"
#include "torrent_state.hpp"

#include <libtorrent/torrent_info.hpp>

namespace biglybtc::engine::detail {
struct SavedPolicy;
std::expected<LocalReport, storage::Error> continueMultifile(const storage::ProfileLease& lease,
    const LocalOptions& options, const core::SavedDownload& saved, storage::ConfigFile& inventory,
    storage::TorrentStateStore& store, storage::TorrentStateFile& original,
    const std::shared_ptr<libtorrent::torrent_info>& metadata, const SavedPolicy& policy);
} // namespace biglybtc::engine::detail
