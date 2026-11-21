// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile_tag_rates.hpp"
namespace libtorrent {struct add_torrent_params;}
namespace biglybtc::engine {
// Source/cohort-bound pending SDK target;no storage or writer grant.
// install edits paused parameters without source access or SDK waits.
class ProfilePreparedTagAttachment final {
public:
 ~ProfilePreparedTagAttachment();
 ProfilePreparedTagAttachment(const ProfilePreparedTagAttachment&)=delete;
 std::expected<void,storage::Error> install(libtorrent::add_torrent_params&);
 // Full metadata check under native stop_when_ready;no later resume grant.
 std::expected<void,storage::Error> installForChecking(libtorrent::add_torrent_params&);
 bool current()const noexcept;
 bool boundTo(libtorrent::torrent_handle)const noexcept;
private:
 friend class ProfileTagRates;
 std::expected<void,storage::Error> installImpl(libtorrent::add_torrent_params&,bool checking);
 struct Impl;
 explicit ProfilePreparedTagAttachment(std::shared_ptr<Impl>);
 std::shared_ptr<Impl> impl_;
};
}
