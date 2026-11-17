// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sdk_tag_rate_group.hpp"
#include "tag_manager_startup.hpp"
#include "biglybtc/library.hpp"
namespace biglybtc::engine {
// Read-only restoration of actual manual-tag rate groups. No source edit grant.
// All SDK waits occur after source gates have been released, on this worker.
class ProfileTagRates final {
public:
 static std::expected<std::unique_ptr<ProfileTagRates>,storage::Error> open(
     const storage::ProfileLease&,const storage::TagManagerStartupMode&);
 ~ProfileTagRates();
 std::expected<void,storage::Error> prepare(const core::TorrentHash&,libtorrent::session_handle);
 std::expected<void,storage::Error> attach(const core::TorrentHash&,libtorrent::torrent_handle);
 std::expected<void,storage::Error> synchronize(libtorrent::session_handle);
 std::expected<void,storage::Error> close();
private:
 struct Impl;explicit ProfileTagRates(std::unique_ptr<Impl>);
 std::unique_ptr<Impl> impl_;
};
}
