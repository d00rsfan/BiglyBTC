// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_subscription_result_source.hpp"
#include "subscription_metainfo_fetch.hpp"
#include "torrent_intake.hpp"
#include "durable_intake_receipt.hpp"
#include <functional>
namespace biglybtc::app {
class SessionBridge;
// Serialized Host worker only. Construction and all effectful steps are private
// to the actual bridge; no URL/hash/receipt/serialized-plan grant is accepted.
class SavedSubscriptionAcquisition final {
public:
  struct Outcome {bool imported=false,associated=false,read=false,cancelled=false,needsReview=false;std::optional<core::TorrentHash> hash;QString warning;};
  ~SavedSubscriptionAcquisition();
  SavedSubscriptionAcquisition(const SavedSubscriptionAcquisition&)=delete;
  void cancel()noexcept;
  std::expected<void,storage::Error> close();
private:
  friend class SessionBridge;
  using Factory=std::function<std::unique_ptr<search::SubscriptionMetainfoFetch>(const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,const search::SavedSubscriptionResultSelector&,std::stop_token)>;
  static std::expected<std::unique_ptr<SavedSubscriptionAcquisition>,storage::Error> begin(
      const storage::ProfileLease&,std::filesystem::path,std::uint64_t,search::SavedSubscriptionResultSelector,
      std::filesystem::path destination,std::stop_token receiver,Factory={});
  // Null outcome: pending network or ready for private stopped intake. No effect
  // is imported by poll; Host::publishTarget must surround saveIntake.
  std::expected<std::optional<Outcome>,storage::Error> poll(storage::ProfileLease&,std::uint64_t);
  bool readyForIntake()const noexcept;
  const Outcome& outcome()const noexcept;
  const core::TorrentHash& intakeHash()const noexcept;
  std::expected<void,storage::Error> saveIntake(storage::ProfileLease&,std::uint64_t,storage::IntakeHistoryOutcome*);
  // Consume any genuine fresh auxiliary history receipt BEFORE this A/R/F step.
  std::expected<Outcome,storage::Error> finishSubscription(storage::ProfileLease&,std::uint64_t);
  static std::expected<std::optional<Outcome>,storage::Error> recover(storage::ProfileLease&,const std::filesystem::path&);
  struct Impl;explicit SavedSubscriptionAcquisition(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::app
