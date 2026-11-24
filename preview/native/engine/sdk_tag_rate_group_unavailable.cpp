// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_prepared_tag_attachment.hpp"

namespace biglybtc::engine {
namespace {
constexpr auto Unavailable = std::unexpected(SdkTagRateError::Engine);
}

struct SdkTagRateGroup::Impl {};
struct SdkPreparedTagAttachment::Impl {};

SdkTagRateGroup::~SdkTagRateGroup() = default;
SdkPreparedTagAttachment::~SdkPreparedTagAttachment() = default;

bool SdkTagRateGroup::supports(core::SavedTagRateLimits) noexcept { return false; }
bool SdkTagRateGroup::current() const noexcept { return false; }
core::TagRateReadiness SdkTagRateGroup::readiness() const noexcept { return {}; }

std::expected<std::unique_ptr<SdkTagRateGroup>, SdkTagRateError> SdkTagRateGroup::create(
    libtorrent::session_handle session, std::uint64_t tag, core::SavedTagRateLimits limits) {
  return create(session, tag, limits, {});
}

std::expected<std::unique_ptr<SdkTagRateGroup>, SdkTagRateError> SdkTagRateGroup::create(
    libtorrent::session_handle, std::uint64_t, core::SavedTagRateLimits,
    std::shared_ptr<SdkSharedTagBudget>) {
  return Unavailable;
}

std::expected<std::unique_ptr<SdkPreparedTagAttachment>, SdkTagRateError>
SdkTagRateGroup::prepareAttachment(const libtorrent::info_hash_t&) { return Unavailable; }

std::expected<void, SdkTagRateError> SdkTagRateGroup::attach(libtorrent::torrent_handle) { return Unavailable; }
std::expected<void, SdkTagRateError> SdkTagRateGroup::detach(libtorrent::torrent_handle) { return Unavailable; }
std::expected<void, SdkTagRateError> SdkTagRateGroup::setLimits(core::SavedTagRateLimits) { return Unavailable; }
std::expected<void, SdkTagRateError> SdkTagRateGroup::configureOriginalLanBypass(bool) { return Unavailable; }
std::expected<void, SdkTagRateError> SdkTagRateGroup::close() noexcept { return {}; }

bool SdkPreparedTagAttachment::current() const noexcept { return false; }
bool SdkPreparedTagAttachment::boundTo(libtorrent::torrent_handle) const noexcept { return false; }

std::expected<void, SdkTagRateError> SdkPreparedTagAttachment::install(libtorrent::add_torrent_params&) {
  return Unavailable;
}

std::expected<void, SdkTagRateError> SdkPreparedTagAttachment::installForChecking(libtorrent::add_torrent_params&) {
  return Unavailable;
}
}
