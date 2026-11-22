// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/tag_rate_limits.hpp>
#include <biglybtc/tag_rate_readiness.hpp>
#include <libtorrent/session_handle.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <expected>
#include <memory>
namespace biglybtc::engine {
class SdkSharedTagBudget;
class SdkPreparedTagAttachment;
enum class SdkTagRateError {Thread,Lifetime,Limits,Foreign,Capacity,Engine,Deadline};
// The selected SDK must represent every requested direction exactly.
// Extended providers support disabled-1 and positive1..9;older providers refuse.
// The SDK signed-infinity sentinel remains refused.
// Low-level SDK owner only. This is NOT a saved-tag/profile writer grant.
// Actual imported/qualified SDK internals are used solely on its network thread.
// Caller serializes controls on the creating worker and releases FS gates first.
class SdkTagRateGroup final {
public:
 static bool supports(core::SavedTagRateLimits)noexcept;
 static std::expected<std::unique_ptr<SdkTagRateGroup>,SdkTagRateError> create(
     libtorrent::session_handle,std::uint64_t tag,core::SavedTagRateLimits);
 static std::expected<std::unique_ptr<SdkTagRateGroup>,SdkTagRateError> create(
     libtorrent::session_handle,std::uint64_t tag,core::SavedTagRateLimits,
     std::shared_ptr<SdkSharedTagBudget>);
 ~SdkTagRateGroup();
 SdkTagRateGroup(const SdkTagRateGroup&)=delete;
 SdkTagRateGroup& operator=(const SdkTagRateGroup&)=delete;
 // Create on this control worker;install on a future paused add from any
 // Native worker. The separate token proves SDK attachment only.
 std::expected<std::unique_ptr<SdkPreparedTagAttachment>,SdkTagRateError> prepareAttachment(
     const libtorrent::info_hash_t&);
 std::expected<void,SdkTagRateError> attach(libtorrent::torrent_handle);
 std::expected<void,SdkTagRateError> detach(libtorrent::torrent_handle);
 std::expected<void,SdkTagRateError> setLimits(core::SavedTagRateLimits);
 // Configure before any attachment;old preparations are revoked. Logical
 // I2P peers retain WAN classes even when their transport uses loopback.
 std::expected<void,SdkTagRateError> configureOriginalLanBypass(bool);
 std::expected<void,SdkTagRateError> close()noexcept;
 bool current()const noexcept;
 core::TagRateReadiness readiness()const noexcept;
private:
 struct Impl;
 explicit SdkTagRateGroup(std::unique_ptr<Impl>);
 std::unique_ptr<Impl> impl_;
};
}
