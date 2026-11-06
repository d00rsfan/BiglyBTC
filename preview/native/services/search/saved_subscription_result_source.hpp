// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_subscription_source.hpp"
#include "subscription_download_values.hpp"
#include <array>
#include <cstddef>
namespace biglybtc::app {class SavedSubscriptionAcquisition;}
namespace biglybtc::search {
struct SavedSubscriptionResultSelector {
  SavedSubscriptionSelector subscription;
  std::string resultKey; // Exact raw10 key from the actual saved result row.
  bool operator==(const SavedSubscriptionResultSelector&)const=default;
};
// Read-only saved-result getter values; no caller-created value is accepted as
// source, network or intake authority. Counts are the SAVED getter phase.
struct SavedSubscriptionResultValue {
  QString name;
  std::int64_t size=0;
  std::int32_t seeds=0,peers=0;
  std::optional<std::array<std::byte,20>> expectedV1Hash;
  detail::SubscriptionRawScrapeCounts rawScrapeCounts; // True saved presence/Longs, before getter folding.
  std::optional<std::int64_t> initialScrapeCache; // Actual raw64 s/p, not normalized getter values.
};
struct SubscriptionResultGetDescription : RssGetDescription {
  SavedSubscriptionResultSelector selected;
  SavedSubscriptionResultValue result;
  std::optional<std::string> referer; // Actual engine root, only if saved policy requests it.
};
// Actual source only: owns the existing saved-subscription snapshot/witnesses.
// No URL/grant setter, caller description, write or intake operation exists.
class SavedSubscriptionResultSnapshot final {
public:
  static std::expected<SavedSubscriptionResultSnapshot,SavedRssSourceError> read(
      const storage::ProfileLease&,const std::filesystem::path& profileDirectory,
      std::uint64_t profileSession,SavedSubscriptionResultSelector);
  SavedSubscriptionResultSnapshot(SavedSubscriptionResultSnapshot&&)noexcept;
  SavedSubscriptionResultSnapshot& operator=(SavedSubscriptionResultSnapshot&&)noexcept;
  ~SavedSubscriptionResultSnapshot();
  SavedSubscriptionResultSnapshot(const SavedSubscriptionResultSnapshot&)=delete;
  SavedSubscriptionResultSnapshot& operator=(const SavedSubscriptionResultSnapshot&)=delete;
  const SubscriptionResultGetDescription& description()const noexcept;
  // Own unnested gate through the retained parent; exact current source/session,
  // creator worker and caller SAMEOFD required. No gate may span network work.
  std::expected<std::uint64_t,SavedRssSourceError> current(
      const storage::ProfileLease& callerHeldSameOFD,std::uint64_t currentSession)const;
private:
  friend class ::biglybtc::app::SavedSubscriptionAcquisition;
  std::expected<std::string,SavedRssSourceError> freezeAcquisitionSource(const storage::ProfileLease&,std::uint64_t)const;
  // Pure frozen-row scalars for private recovery binding; no profile/effect grant.
  static std::expected<detail::SubscriptionRawScrapeCounts,SavedRssSourceError> frozenRawScrapeCounts(std::string_view);
  struct Impl;
  explicit SavedSubscriptionResultSnapshot(std::unique_ptr<Impl>)noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::search
