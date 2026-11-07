// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "torrent_intake.hpp"
namespace biglybtc::app { class SessionBridge; }
namespace biglybtc::storage {
// Disk-only target observation. Actual Host handle/controller exclusion remains
// the concrete coordinator's responsibility; this class never queries an engine.
class ExistingStoppedPublicSubscriptionTarget final {
public:
  ExistingStoppedPublicSubscriptionTarget(ExistingStoppedPublicSubscriptionTarget&&) noexcept;
  ExistingStoppedPublicSubscriptionTarget& operator=(ExistingStoppedPublicSubscriptionTarget&&) noexcept;
  ~ExistingStoppedPublicSubscriptionTarget();
  ExistingStoppedPublicSubscriptionTarget(const ExistingStoppedPublicSubscriptionTarget&)=delete;
  ExistingStoppedPublicSubscriptionTarget& operator=(const ExistingStoppedPublicSubscriptionTarget&)=delete;
private:
  friend class biglybtc::app::SavedSubscriptionAcquisition;
  friend class biglybtc::app::SessionBridge; // Genuine read-only stopped-share validation.
  struct Images {
    core::TorrentHash hash;
    OriginalTorrentIdentity identity;
    std::string_view rawInfo,inventory,archive,state,attributes;
    std::string_view statePrepared,stateAfterAssociation;
    std::optional<std::string_view> inventoryBackup,archiveBackup,stateBackup;
    std::filesystem::path destination;
  };
  struct Data;
  explicit ExistingStoppedPublicSubscriptionTarget(std::unique_ptr<Data>) noexcept;
  std::unique_ptr<Data> data_;
  static std::expected<ExistingStoppedPublicSubscriptionTarget,Error> capture(
      const ProfileLease&,const TorrentIntakeMetadata&,const std::filesystem::path& chosenDestination,
      std::uint64_t session,std::stop_token={},std::string_view exactOwnerIntent={});
  static std::expected<ExistingStoppedPublicSubscriptionTarget,Error> captureSharingVerified(
      const ProfileLease&,const TorrentIntakeMetadata&,const std::filesystem::path&,
      std::uint64_t,std::stop_token);
  static std::expected<ExistingStoppedPublicSubscriptionTarget,Error> captureBound(
      const ProfileLease&,const TorrentIntakeMetadata&,const std::filesystem::path&,
      std::uint64_t,std::stop_token,std::string_view,bool sharingVerified);
  static std::expected<ExistingStoppedPublicSubscriptionTarget,Error> captureAutomatic(
      const ProfileLease&,const TorrentIntakeMetadata&,std::uint64_t session,std::stop_token={});
  std::expected<void,Error> prepareAutomatic(FixedSubscriptionDownloadPreparation);
  const search::detail::SubscriptionAutomaticPolicyValues* automaticPolicy()const noexcept;
  const search::detail::SubscriptionAutomaticGeometry* automaticGeometry()const noexcept;
  std::expected<bool,Error> automaticArtifactPresent(const ProfileLease&,std::uint64_t,std::string_view)const;
  std::expected<void,Error> prepareAutomaticArtifacts(const ProfileLease&,std::uint64_t,std::string_view exactOwnerIntent);
  std::expected<void,Error> publishAutomaticArtifact(const ProfileLease&,std::uint64_t,std::string_view exactOwnerIntent);
  std::expected<void,Error> rollbackAutomaticArtifact(const ProfileLease&,std::uint64_t,std::string_view exactOwnerIntent);
  // Pure preparation only; no filesystem effect. Methods below are private to
  // the real coordinator and accept only fixed recipe inputs, never postimages.
  std::expected<void,Error> prepare(FixedSubscriptionDownloadPreparation);
  std::string_view frozenRecipe()const noexcept;
  const FixedSubscriptionDownloadPreparation* fixedPreparation()const noexcept;
  static std::expected<ExistingStoppedPublicSubscriptionTarget,Error> restore(
      const ProfileLease&,const TorrentIntakeMetadata&,const std::filesystem::path& chosenDestination,
      std::uint64_t session,std::string frozenRecipe,std::string_view exactOwnerIntent);
  enum class StatePhase : std::uint8_t { Base=1,Prepared=2,Associated=4 };
  struct StateObservation {
    std::uint8_t primaryMatches=0;std::optional<std::uint8_t> savingMatches;
    bool primaryIs(StatePhase phase)const noexcept{return (primaryMatches&static_cast<std::uint8_t>(phase))!=0;}
    bool savingIs(StatePhase phase)const noexcept{return savingMatches&&(*savingMatches&static_cast<std::uint8_t>(phase))!=0;}
  };
  std::expected<StateObservation,Error> inspectState(const ProfileLease&,std::uint64_t session,std::string_view exactOwnerIntent)const;
  std::expected<void,Error> publishPreparedState(const ProfileLease&,std::uint64_t session,std::string_view exactOwnerIntent);
  std::expected<void,Error> publishAssociationState(const ProfileLease&,std::uint64_t session,std::string_view exactOwnerIntent,
      void(*observer)(SubscriptionDownloadStateStage)=nullptr);
  std::expected<void,Error> rollbackState(const ProfileLease&,std::uint64_t session,std::string_view exactOwnerIntent);
  const Images& images() const noexcept;
  std::expected<void,Error> current(const ProfileLease&,std::uint64_t session,
      std::string_view exactOwnerIntent={}) const;
};
} // namespace biglybtc::storage
