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
 bool current()const noexcept;
 bool boundTo(libtorrent::torrent_handle)const noexcept;
private:
 friend class ProfileTagRates;
 struct Impl;
 explicit ProfilePreparedTagAttachment(std::shared_ptr<Impl>);
 std::shared_ptr<Impl> impl_;
};
}
