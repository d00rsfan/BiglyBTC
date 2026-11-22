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
 // Composition may retain the owner while damaged tag settings are repaired.
 // The first admission still validates the complete source before SDK effects.
 static std::expected<std::unique_ptr<ProfileTagRates>,storage::Error> openDeferred(
     const storage::ProfileLease&,const storage::TagManagerStartupMode&);
 ~ProfileTagRates();
 // Host composition only,before SDK admission. Backend tests/defaults retain
 // their all-peer byte-budget policy.
 std::expected<void,storage::Error> configureOriginalLanBypass(bool);
 // Configure the profile-wide global/LAN cells before the first SDK. Later
 // changes join all actual network contexts on this creating worker.
 std::expected<void,storage::Error> configureNetworkRates(core::SavedTagRateLimits global,
     core::SavedTagRateLimits lan);
 bool hasCommonNetworkRates()const noexcept;
 bool tagManagerEnabled()const noexcept;
 // Admit the composition-owned empty SDK before any private service can run.
 // Does not read damaged tag settings or mint tag/source/storage authority.
 std::expected<void,storage::Error> admitIdleSession(libtorrent::session_handle);
 std::expected<void,storage::Error> configureOriginalProtocol(bool includeUpload,bool includeDownload);
 // Passive creating-worker/profile/startup ownership check;no SDK/source work.
 bool matchesProfile(const storage::ProfileLease&,const storage::TagManagerStartupMode&)const noexcept;
 std::expected<void,storage::Error> prepare(const core::TorrentHash&,libtorrent::session_handle);
 // Reserve a pending row before a paused add on another Native worker.
 std::expected<std::unique_ptr<ProfilePreparedTagAttachment>,storage::Error> prepareAttachment(
     const core::TorrentHash&,const libtorrent::info_hash_t&,libtorrent::session_handle);
 // Separate sealed current-source preparation for the actual future private SDK.
 std::expected<PrivateTagRateSource,storage::Error> preparePrivateSource(
     const core::TorrentHash&,libtorrent::session_handle,const ProfilePreparedTagAttachment&);
 // Capture current complete readiness for an already admitted actual handle.
 // No pre-add token,source write,private storage or transport grant follows.
 std::expected<PrivateTagRateSource,storage::Error> refreshPrivateSource(
     const core::TorrentHash&,libtorrent::session_handle,libtorrent::torrent_handle);
 // Stable creating-worker ownership,including an expired source epoch.
 bool ownsPrivateSource(const PrivateTagRateSource&)const noexcept;
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
