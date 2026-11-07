#pragma once

#include "biglybtc/library.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include "biglybtc/existing_piece_goal.hpp"
#include "biglybtc/torrent_intake_trackers.hpp"
#include "biglybtc/torrent_layout.hpp"
#include "profile.hpp"
#include "subscription_download_preparation.hpp"
#include "subscription_automatic_values.hpp"
#include "original_torrent_identity.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <memory>
#include <stop_token>
#include <string_view>

namespace biglybtc::app { class SavedSubscriptionAcquisition; }

namespace biglybtc::storage {

// Explicit import context only; grants no engine, router or identity authority.
enum class TorrentIntakeNetwork { Public, I2p, Mixed };

struct IntakeHistoryOutcome;

enum class TorrentIntakeStage { JournalSynced, StatePublished, ArchivePublished, InventoryPublished };

// Linux, serialized profile-owner worker only. The caller borrows one held
// lease; no extra lease/engine/payload allocation is performed. destination is
// the parent save directory; metadata's original name becomes save_file.
// Fixed owned paths: active/<UPPERCASE_HASH>.dat, torrents/<HASH>.torrent.
// Full validation precedes the first journal write. An I/O failure can leave a
// durable pending transaction, never a partial downloads.config row. Do not
// retry blindly: run explicit recovery before further inventory mutations.
// forceStart commits forceStart=1 + Waiting atomically (takes precedence over
// stopped); the optional trailing argument preserves existing source call sites.
std::expected<core::TorrentHash, Error> importTorrent(ProfileLease&,
    const std::filesystem::path& metadata, const std::filesystem::path& destination,
    bool stopped, void (*observer)(TorrentIntakeStage) = nullptr, bool forceStart = false,
    TorrentIntakeNetwork network = TorrentIntakeNetwork::Public, IntakeHistoryOutcome* history = nullptr);

// Read-only worker preview of the same validated raw metadata. The sealed value
// retains exact source bytes/inode/stat; no payload/profile changes or engine.
class TorrentIntakeMetadata final {
public:
  const core::TorrentHash& hash() const noexcept;
  const compat::OriginalLayout& layout() const noexcept;
  const std::optional<core::TorrentIntakeTrackerFields>& trackers() const noexcept;
  const core::TorrentIntakeFileDestinations& initialDestinations() const noexcept;
  const std::string& filenameConversions() const noexcept;
  bool decoderShowAll() const noexcept;
  const std::filesystem::path& source() const noexcept;
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  explicit TorrentIntakeMetadata(std::shared_ptr<const Data>);
  friend class PreparedStoppedPublicIntake;
  friend class ExistingStoppedPublicSubscriptionTarget;
  friend std::expected<TorrentIntakeMetadata, Error> inspectTorrentIntake(const std::filesystem::path&, std::stop_token,std::string,bool);
  friend std::expected<core::TorrentHash, Error> importTorrentWithFileOptions(ProfileLease&,
      const TorrentIntakeMetadata&, const std::filesystem::path&, bool, std::span<const compat::FilePolicy>,
      void (*)(TorrentIntakeStage), bool, std::stop_token, core::TorrentIntakeLimits, bool, bool, bool, std::optional<core::TorrentIntakeTrackerTiers>, std::optional<std::string>, bool, std::optional<core::TorrentIntakeFileDestinations>, core::ExistingVerificationGoal, core::TorrentAllocationMode, TorrentIntakeNetwork, IntakeHistoryOutcome*);
};
std::expected<TorrentIntakeMetadata, Error> inspectTorrentIntake(
    const std::filesystem::path&, std::stop_token = {},
    std::string filenameConversions=std::string(compat::OriginalDefaultFilenameConversions),bool decoderShowAll=false);
// Storage-built frozen recipe for the one concrete acquisition coordinator.
// It is never constructible from a public hash/receipt/description. Ordinary
// imports and recovery refuse its owner marker or its marked journal section.
class PreparedStoppedPublicIntake final {
public:
  PreparedStoppedPublicIntake(PreparedStoppedPublicIntake&&)noexcept;
  PreparedStoppedPublicIntake& operator=(PreparedStoppedPublicIntake&&)noexcept;
  ~PreparedStoppedPublicIntake();
  PreparedStoppedPublicIntake(const PreparedStoppedPublicIntake&)=delete;
  PreparedStoppedPublicIntake& operator=(const PreparedStoppedPublicIntake&)=delete;
private:
  friend class biglybtc::app::SavedSubscriptionAcquisition;
  enum class State { Before, Pending, Committed };
  struct Images {
    core::TorrentHash hash;
    std::optional<std::string_view> beforeInventory,beforeInventoryBackup;
    std::string_view afterInventory,afterState,afterArchive;
    std::string_view afterAssociationState;
  };
  struct Data;std::unique_ptr<Data> data_;
  explicit PreparedStoppedPublicIntake(std::unique_ptr<Data>)noexcept;
  static std::expected<PreparedStoppedPublicIntake,Error> prepare(ProfileLease&,
      const TorrentIntakeMetadata&,const std::filesystem::path& destination,
      std::optional<std::int64_t> derivedScrapeCache,std::stop_token={},
      std::optional<FixedSubscriptionDownloadPreparation> preparation={});
  static std::expected<PreparedStoppedPublicIntake,Error> prepareAutomatic(ProfileLease&,
      const TorrentIntakeMetadata&,FixedSubscriptionDownloadPreparation,std::stop_token={});
  // Passive views of a storage-derived automatic plan. No caller may supply
  // replacement geometry, queue bytes or a permission flag.
  const search::detail::SubscriptionAutomaticPolicyValues* automaticPolicy()const noexcept;
  const search::detail::SubscriptionAutomaticGeometry* automaticGeometry()const noexcept;
  std::string_view queuedInventory()const noexcept;
  std::optional<std::string_view> automaticInventoryBackup()const noexcept;
  std::expected<bool,Error> automaticArtifactPresent(ProfileLease&,std::string_view)const;
  std::expected<void,Error> prepareAutomaticArtifacts(ProfileLease&,std::string_view exactOwnerIntent);
  std::expected<void,Error> publishAutomaticArtifact(ProfileLease&,std::string_view exactOwnerIntent);
  std::expected<void,Error> rollbackAutomaticArtifact(ProfileLease&,std::string_view exactOwnerIntent);
  std::filesystem::path automaticInventoryPath()const;
  std::expected<void,Error> publishAutomaticQueue(ProfileLease&,std::string_view exactOwnerIntent,
      void(*observer)(SubscriptionDownloadStateStage)=nullptr);
  std::expected<void,Error> rollbackAutomaticQueue(ProfileLease&,std::string_view exactOwnerIntent);
  static std::expected<PreparedStoppedPublicIntake,Error> restore(ProfileLease&,
      std::string frozenJournal);
  std::string_view frozenJournal()const noexcept;
  const FixedSubscriptionDownloadPreparation* fixedPreparation()const noexcept;
  const Images& images()const noexcept;
  std::expected<State,Error> inspect(ProfileLease&,std::string_view exactOwnerIntent)const;
  enum class AssociationStatePhase : std::uint8_t { Base=1,Associated=2 };
  struct AssociationStateObservation {
    std::uint8_t primaryMatches=0;std::optional<std::uint8_t> savingMatches;
    bool primaryIs(AssociationStatePhase phase)const noexcept{return (primaryMatches&static_cast<std::uint8_t>(phase))!=0;}
    bool savingIs(AssociationStatePhase phase)const noexcept{return savingMatches&&( *savingMatches&static_cast<std::uint8_t>(phase))!=0;}
  };
  std::expected<AssociationStateObservation,Error> inspectAssociationState(ProfileLease&,std::string_view exactOwnerIntent)const;
  std::expected<void,Error> publishAssociationState(ProfileLease&,std::string_view exactOwnerIntent,
      void(*observer)(SubscriptionDownloadStateStage)=nullptr);
  std::expected<void,Error> rollbackAssociationState(ProfileLease&,std::string_view exactOwnerIntent);
  // Existing commit point: cancellation before the intake journal means no
  // effects. Pending recovery completes only this exact stopped recipe; a
  // committed recipe is idempotent. Only prepare-here plus fresh Before may
  // issue the existing optional auxiliary history receipt; recovery never does.
  std::expected<core::TorrentHash,Error> commit(ProfileLease&,
      std::string_view exactOwnerIntent,std::stop_token={},
      void(*observer)(TorrentIntakeStage)=nullptr,IntakeHistoryOutcome* history=nullptr);
};

// Worker previews supply readOriginalFilenameContext under the same sole
// lease. Both sealed settings are rechecked at import; these arguments grant no
// filesystem authority and never changes raw metadata/full torrent identities.
// Full original-index vector, including immutable skipped padding entries.
// All skipped => Stopped/no force, with no payload allocation. New journal option
// records retain original priority/storage choices through every recovery stage.
// Existing skipped leaves stay L; absent skipped leaves use source C/X policy.
// Appended default-zero limits are original byte rates, validated before effects
// and retained through the same durable file-options journal/recovery. No live
// mutation, sampler or implicit admission is performed by the adapter.
// Optional sequential intent sets only original attributes.flags bit0x2000;
// false preserves existing flags, matching original intake. Old journals omit it.
// Appended queued intent requests original STATE_QUEUED75. False retains the
// existing Waiting0 default. Queued cannot coexist with stopped/forceStart;
// all-skipped normalizes to Stopped/no force/no queued intent before journaling.
// Paused requires stopped/no force/no queued and atomically adds original
// pause_data(hash,force0,owner1) in the same inventory publication; all-skipped
// clears paused as well. It never performs engine admission or a later pause save.
std::expected<core::TorrentHash, Error> importTorrentWithFileOptions(ProfileLease&,
    const TorrentIntakeMetadata&, const std::filesystem::path& destination, bool stopped,
    std::span<const compat::FilePolicy>, void (*observer)(TorrentIntakeStage) = nullptr,
    bool forceStart = false, std::stop_token = {}, core::TorrentIntakeLimits = {}, bool sequentialDownload = false, bool queued = false, bool paused = false, std::optional<core::TorrentIntakeTrackerTiers> trackers = std::nullopt, std::optional<std::string> saveName = std::nullopt, bool verifyExisting = false, std::optional<core::TorrentIntakeFileDestinations> fileDestinations = std::nullopt, core::ExistingVerificationGoal goal=core::ExistingVerificationGoal::FullTorrent, core::TorrentAllocationMode allocation=core::TorrentAllocationMode::None, TorrentIntakeNetwork network=TorrentIntakeNetwork::Public, IntakeHistoryOutcome* history=nullptr);

// I2p context is a fresh unforced ordinary import (Waiting, Queued or Stopped, optionally paused),
// without verified-seeding or allocation intent. It saves [I2P] plus
// Tracker permission in the SAME transaction, persists context through recovery,
// and refuses incompatible inherited networks/explicitly denied Tracker. It
// never converts a saved Public row/alias or starts an owner; Public is default.
// Mixed context explicitly saves [Public,I2P], preserving an existing exact
// two-token selection verbatim (either order). It permits ordinary forced
// intent, rejects aliases/verification/allocation, and requires Tracker as for
// I2p. Accepted journals retain the literal Public+I2P; no runtime is granted.
// Optional saveName changes only the saved destination leaf. Missing retains
// old defaults; restoring layout.name clears manual rename intent. Changed
// names require an absent target and disjoint original library mappings, with
// checks before journal/each publication/recovery. No payload is renamed here.
// Literal pre-add override: absent leaves source bytes untouched; present empty
// stores the original decentralized marker. Only outer announce fields change;
// raw info/full selected identity and every other source value are preserved.
// Pure bounded helper shared by intake and the retained-original replay driver.
std::expected<std::string,Error> rewriteTorrentIntakeTrackers(std::string_view metadata,
    const std::optional<core::TorrentIntakeTrackerTiers>&);

// Explicit stopped companion, matching original hybrid "run as other version"
// identity shape. Source row/state/cache/control bytes remain unchanged. No
// payload/engine effects, implicit merge, rekey or simultaneous owner permission.
// Caller supplies COMPLETE current-owner retained identities (no default empty
// convenience argument); matching/shared identities reject before any effects.
// Initial bounded support: stopped nonforced list-form source, modern save root,
// selected/unlinked explicit linear real files with no physical control leaves.
std::expected<core::TorrentHash, Error> importHybridAlias(ProfileLease&,
    const core::TorrentHash& source, OriginalSelectedHash target,
    std::span<const OriginalTorrentIdentity> retained,
    void (*observer)(TorrentIntakeStage) = nullptr, std::stop_token = {}, IntakeHistoryOutcome* history=nullptr);

bool hasPendingTorrentIntake(const ProfileLease&) noexcept;

// Startup recovery before loading library/admitting operations. No pending
// transaction => nullopt. Rebuilds the exact append from journal input and
// publishes inventory last. Conflicting user edits/artifacts are preserved and
// reported. No generic primary/backup recovery promotion or payload effects.
std::expected<std::optional<core::TorrentHash>, Error> recoverTorrentIntake(
    ProfileLease&, void (*observer)(TorrentIntakeStage) = nullptr);

} // namespace biglybtc::storage
