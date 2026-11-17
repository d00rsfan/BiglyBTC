// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/tag_rate_limits.hpp>
#include <libtorrent/session_handle.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <expected>
#include <memory>
namespace biglybtc::engine {
enum class SdkTagRateError {Thread,Lifetime,Limits,Foreign,Capacity,Engine,Deadline};
// Supports unlimited0 or10..INT32_MAX-1 bytes/s exactly at the SDK API.
// Disabled-1,tiny rounded rates and the SDK infinity sentinel remain refused.
// Low-level SDK owner only. This is NOT a saved-tag/profile writer grant.
// Actual imported/qualified SDK internals are used solely on its network thread.
// Caller serializes controls on the creating worker and releases FS gates first.
class SdkTagRateGroup final {
public:
 static std::expected<std::unique_ptr<SdkTagRateGroup>,SdkTagRateError> create(
     libtorrent::session_handle,std::uint64_t tag,core::SavedTagRateLimits);
 ~SdkTagRateGroup();
 SdkTagRateGroup(const SdkTagRateGroup&)=delete;
 SdkTagRateGroup& operator=(const SdkTagRateGroup&)=delete;
 std::expected<void,SdkTagRateError> attach(libtorrent::torrent_handle);
 std::expected<void,SdkTagRateError> detach(libtorrent::torrent_handle);
 std::expected<void,SdkTagRateError> setLimits(core::SavedTagRateLimits);
 std::expected<void,SdkTagRateError> close()noexcept;
 bool current()const noexcept;
private:
 struct Impl;
 explicit SdkTagRateGroup(std::unique_ptr<Impl>);
 std::unique_ptr<Impl> impl_;
};
}
