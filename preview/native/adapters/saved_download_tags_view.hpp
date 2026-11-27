#pragma once
#include "biglybtc/saved_download_tags.hpp"
#include "profile.hpp"
#include "state_file_io.hpp"
#include <array>
#include <stop_token>

namespace biglybtc::core { struct QueueTorrentFacts; }

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

// The caller's worker already holds source access. Returned values own no
// profile gate or mutation authority and may be handed to the queue worker.
class SavedTagQueueReader final {
public:
  std::shared_ptr<const core::SavedTagQueueRules> read(
      const ProfileLease&, const ProfileSourceAccess&) noexcept;
private:
  using Stamps = std::array<std::optional<detail::StateStamp>, 3>;
  std::optional<Stamps> stamps_;
  std::shared_ptr<const core::SavedTagQueueRules> priorities_;
};
void applySavedTagQueueRules(const core::SavedTagQueueRules*, std::string_view hash,
    core::QueueTorrentFacts&);
} // namespace biglybtc::storage
