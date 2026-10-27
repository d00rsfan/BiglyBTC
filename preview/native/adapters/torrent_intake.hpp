#pragma once

#include "biglybtc/library.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include "biglybtc/existing_piece_goal.hpp"
#include "biglybtc/torrent_intake_trackers.hpp"
#include "biglybtc/torrent_layout.hpp"
#include "profile.hpp"
#include "original_torrent_identity.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <memory>
#include <stop_token>

namespace biglybtc::storage {

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
    bool stopped, void (*observer)(TorrentIntakeStage) = nullptr, bool forceStart = false);

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
  friend std::expected<TorrentIntakeMetadata, Error> inspectTorrentIntake(const std::filesystem::path&, std::stop_token,std::string,bool);
  friend std::expected<core::TorrentHash, Error> importTorrentWithFileOptions(ProfileLease&,
      const TorrentIntakeMetadata&, const std::filesystem::path&, bool, std::span<const compat::FilePolicy>,
      void (*)(TorrentIntakeStage), bool, std::stop_token, core::TorrentIntakeLimits, bool, bool, bool, std::optional<core::TorrentIntakeTrackerTiers>, std::optional<std::string>, bool, std::optional<core::TorrentIntakeFileDestinations>, core::ExistingVerificationGoal, core::TorrentAllocationMode);
};
std::expected<TorrentIntakeMetadata, Error> inspectTorrentIntake(
    const std::filesystem::path&, std::stop_token = {},
    std::string filenameConversions=std::string(compat::OriginalDefaultFilenameConversions),bool decoderShowAll=false);
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
    bool forceStart = false, std::stop_token = {}, core::TorrentIntakeLimits = {}, bool sequentialDownload = false, bool queued = false, bool paused = false, std::optional<core::TorrentIntakeTrackerTiers> trackers = std::nullopt, std::optional<std::string> saveName = std::nullopt, bool verifyExisting = false, std::optional<core::TorrentIntakeFileDestinations> fileDestinations = std::nullopt, core::ExistingVerificationGoal goal=core::ExistingVerificationGoal::FullTorrent, core::TorrentAllocationMode allocation=core::TorrentAllocationMode::None);

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
    void (*observer)(TorrentIntakeStage) = nullptr, std::stop_token = {});

bool hasPendingTorrentIntake(const ProfileLease&) noexcept;

// Startup recovery before loading library/admitting operations. No pending
// transaction => nullopt. Rebuilds the exact append from journal input and
// publishes inventory last. Conflicting user edits/artifacts are preserved and
// reported. No generic primary/backup recovery promotion or payload effects.
std::expected<std::optional<core::TorrentHash>, Error> recoverTorrentIntake(
    ProfileLease&, void (*observer)(TorrentIntakeStage) = nullptr);

} // namespace biglybtc::storage
