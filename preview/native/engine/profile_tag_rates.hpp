// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sdk_tag_rate_group.hpp"
#include "tag_manager_startup.hpp"
#include "prepared_tag_rate_source.hpp"
#include <span>
#include "biglybtc/library.hpp"
namespace biglybtc::engine {
class ProfilePreparedTagAttachment;
class PrivateTagRateSource;
// Worker-confined common saved-tag owner for one profile and admitted SDK image.
// Old SDKs retain single-session restoration. No source edit grant.
// All SDK waits occur after source gates have been released, on this worker.
class ProfileTagRates final {
public:
 static std::expected<std::unique_ptr<ProfileTagRates>,storage::Error> open(
     const storage::ProfileLease&,const storage::TagManagerStartupMode&);
 ~ProfileTagRates();
 std::expected<void,storage::Error> prepare(const core::TorrentHash&,libtorrent::session_handle);
 // Reserve a pending row before a paused add on another Native worker.
 std::expected<std::unique_ptr<ProfilePreparedTagAttachment>,storage::Error> prepareAttachment(
     const core::TorrentHash&,const libtorrent::info_hash_t&,libtorrent::session_handle);
 // Separate sealed current-source preparation for the actual future private SDK.
 std::expected<PrivateTagRateSource,storage::Error> preparePrivateSource(
     const core::TorrentHash&,libtorrent::session_handle,const ProfilePreparedTagAttachment&);
 std::expected<void,storage::Error> attach(const core::TorrentHash&,libtorrent::torrent_handle);
 std::expected<void,storage::Error> attach(const core::TorrentHash&,libtorrent::torrent_handle,libtorrent::session_handle);
 // Release this session before SDK shutdown;remaining owners retain the budget.
 std::expected<void,storage::Error> release(libtorrent::session_handle);
 std::expected<void,storage::Error> synchronize(libtorrent::session_handle);
 std::expected<storage::PreparedTagRateSource,storage::Error> prepareSource(
     const storage::ProfileLease&,const storage::ManualTagStore&,const storage::ManualTagSnapshot&,
     std::span<const std::int32_t>,libtorrent::session_handle);

 std::expected<void,storage::Error> close();
private:
 struct Impl;explicit ProfileTagRates(std::unique_ptr<Impl>);
 std::unique_ptr<Impl> impl_;
};
}
