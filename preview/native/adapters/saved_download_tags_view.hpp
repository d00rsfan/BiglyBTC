#pragma once
#include "biglybtc/saved_download_tags.hpp"
#include "profile.hpp"
#include <stop_token>

namespace biglybtc::storage {
struct SavedDownloadTagsView {
  std::optional<core::SavedDownloadTags> snapshot;
  std::optional<RecoverySource> recoverySource;
  bool missing() const noexcept { return !snapshot; }
};
// Borrow the caller's held lease. Read only tag.config and its loader recovery
// candidates under normal core::Limits{}, never active/*.dat, paths from config,
// managers or rates. Missing requires all three exact candidates absent.
// Cancellation is checked around blocking reads/projection, not inside read().
// Separate candidates are not a transaction against external file changes.
std::expected<SavedDownloadTagsView, Error> readSavedDownloadTagsView(
    const ProfileLease&, std::stop_token stop = {});
} // namespace biglybtc::storage
