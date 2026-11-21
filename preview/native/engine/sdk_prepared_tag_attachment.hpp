// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sdk_tag_rate_group.hpp"
namespace libtorrent {struct add_torrent_params;}
namespace biglybtc::engine {
// One-shot SDK-class attachment for one exact future torrent/session.
// install only edits paused add parameters and may cross a Native worker.
// The SDK constructor callback attaches the real class before any connection.
// No profile/source/storage permission follows from this low-level capability.
class SdkPreparedTagAttachment final {
public:
 ~SdkPreparedTagAttachment();
 SdkPreparedTagAttachment(const SdkPreparedTagAttachment&)=delete;
 std::expected<void,SdkTagRateError> install(libtorrent::add_torrent_params&);
 // Full metainfo only. The SDK's stop_when_ready must prevent data transfer;
 // caller verifies boundTo before any later queue-authorized resume.
 std::expected<void,SdkTagRateError> installForChecking(libtorrent::add_torrent_params&);
 bool current()const noexcept;
 bool boundTo(libtorrent::torrent_handle)const noexcept;
private:
 friend class SdkTagRateGroup;
 std::expected<void,SdkTagRateError> installImpl(libtorrent::add_torrent_params&,bool checking);
 struct Impl;
 explicit SdkPreparedTagAttachment(std::shared_ptr<Impl>);
 std::shared_ptr<Impl> impl_;
};
}
