#pragma once

#include "biglybtc/library.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_layout.hpp"
#include "biglybtc/existing_piece_goal.hpp"
#include "biglybtc/queue_file_priority.hpp"
#include "profile.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include "original_torrent_identity.hpp"
#include "managed_payload_claim.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <cstdint>
#include <memory>
#include <filesystem>
#include <vector>
#include <span>
#include <string>
#include <stop_token>

namespace biglybtc::engine {
struct NetworkPolicy;
class OwnedI2pPolicy;
class MixedPublicPolicy;
namespace detail {

class OriginalDiskFiles;
struct ManagedPayloadCounterBridge;


struct ManagedMainlinePolicy {
  bool discover=false, announce=false;
  bool operator==(const ManagedMainlinePolicy&) const = default;
};
struct ManagedBadPayloadBytes { std::int64_t discarded=0,hashFailed=0; };

struct ManagedQueueMetadata {
  std::int64_t position = 1, downloaded = 0, uploaded = 0, discarded = 0, hashFailed = 0;
  std::int64_t secondsDownloading = 0, secondsSeeding = 0;
  std::optional<std::int32_t> minimumShareRatio, maximumShareRatio;
  std::int32_t secondsSinceUpload=-1;
  std::optional<std::string> aggregateScrapeCache;
  std::optional<std::int64_t> legacyScrapeCache;
  std::optional<std::vector<std::array<unsigned char,32>>> trackerOrder;
  // Absent loss marker is not current counter proof; an actual V3 epoch is still required.
  // Original no-DiskManager classification for queue ranking only. This is
  // neither a verified piece map nor permission to serve payload.
  bool savedSelectedComplete=false;
  bool badPayloadCountersLost=false;
  std::optional<std::string> badCounterLossSource{};
};

// Storage prerequisite only: the future owner must independently hold checking
// until actual all-piece verification and fresh saved-intent CAS. This is not
// seed_mode, verified resume, permission to auto-resume or an engine admission.
enum class ManagedStoragePurpose { Download, VerifyExistingReadOnly, VerifySelectedReadOnly, RecheckReadOnly, AllocateOnly };
struct ManagedAllocationProgress { std::int64_t completedBytes=0,totalBytes=0;bool complete=false; };
// Retained actual matched checked-state event, independent of later final
// piece status at stop. Internal metadata input, never a GUI/payload grant.
struct ManagedHistoryCompletionEvent {std::int64_t wallMillis=0;bool complete=false,checkedIncompleteObserved=false;};

// Owns authenticated payload descriptors/state for ONE managed admission. The
// caller retains the same profile lease for preparation, engine use and save.
class ManagedStorage final {
public:
  static std::expected<ManagedStorage,storage::Error> prepare(
      const storage::ProfileLease&,const core::TorrentHash&,const NetworkPolicy&,
      std::span<const ManagedPayloadClaim> claimed={},
      std::span<const storage::OriginalTorrentIdentity> retainedIdentities={},
      std::shared_ptr<const ManagedPayloadCounterBridge> swarmBridge={},
      ManagedStoragePurpose purpose=ManagedStoragePurpose::Download,std::stop_token = {});
  // Separate minted I2P-only policy; ordinary prepare/global policy unchanged.
  static std::expected<ManagedStorage,storage::Error> prepareOwnedI2p(
      const storage::ProfileLease&,const core::TorrentHash&,const OwnedI2pPolicy&,
      std::span<const ManagedPayloadClaim> claimed={},
      std::span<const storage::OriginalTorrentIdentity> retainedIdentities={});
  static std::expected<ManagedStorage,storage::Error> prepareMixedPublic(
      const storage::ProfileLease&,const core::TorrentHash&,const NetworkPolicy&,
      const MixedPublicPolicy&,std::span<const ManagedPayloadClaim>,
      std::span<const storage::OriginalTorrentIdentity>,
      std::shared_ptr<const ManagedPayloadCounterBridge>,ManagedStoragePurpose);
  std::string preparedStateSnapshot()const;
  std::string_view originalResumeDecision()const noexcept;
  // Internal checkpoint metadata result only; neither getter mints a receipt.
  bool historyCompletionRecorded()const noexcept;
  std::optional<std::int64_t> historyCheckpointBytes()const noexcept;
  std::optional<storage::ErrorCode> historyCompletionProblem()const noexcept;

  // Actual committed category metadata only; prepared owner/lease stays exact.
  // FS/pure only, safe inside a trusted publication boundary (no SDK waits).
  std::expected<void,storage::Error> rebaseCategory(const storage::ProfileLease&);
  // Validated committed physical afterimage, never pending native resume.
  std::string_view categoryStateSnapshot() const noexcept;
  ManagedStorage(ManagedStorage&&) noexcept;
  ManagedStorage& operator=(ManagedStorage&&) noexcept;
  ManagedStorage(const ManagedStorage&) = delete;
  ManagedStorage& operator=(const ManagedStorage&) = delete;
  ~ManagedStorage();
  libtorrent::add_torrent_params parameters() const;
  std::vector<std::int64_t> inspectFileProgress(std::span<const std::uint8_t>) const;
  ManagedMainlinePolicy mainlinePolicy() const noexcept;
  ManagedStoragePurpose purpose() const noexcept;
  // Immutable NEED mask, never observed/verified data. Full stays all pieces.
  std::span<const std::uint8_t> existingVerificationPieces() const noexcept;
  // Owner-thread pure named-fd snapshot check, safe alongside read-only disk
  // jobs. No codec/failure-cache access, queue release or verified-piece proof.
  std::expected<void,storage::Error> validateExistingSnapshot() const;
  std::vector<std::uint8_t> recheckHashOutcomes() const;
  std::int64_t recheckScannedBytes() const;
  bool recheckSaveAttempted() const noexcept;
  std::expected<void,storage::Error> checkpointRecheck(const storage::ProfileLease&,
      std::span<const std::uint8_t> verifiedPieces,bool allComplete);
  // Existing worker only, bounded real zero/read work. No LT handle or piece
  // authority; read-only purposes reject these entry points before any IO.
  ManagedAllocationProgress allocationProgress() const noexcept;
  std::expected<ManagedAllocationProgress,storage::Error> advanceAllocation(std::size_t byteBudget=4*1024*1024);
  std::expected<void,storage::Error> settleAllocation(const storage::ProfileLease&,bool successful);
  // Actual read-only checked share geometry and same-OFD payload snapshot.
  std::expected<void,storage::Error> validateSharedSeedSource(
      const storage::ProfileLease&,const compat::OriginalLayout&,
      const std::filesystem::path& destination) const;
  const ManagedQueueMetadata& queueMetadata() const;
  // Captured original mapping for passive same-admission group authority.
  const core::SavedDownload& capturedDownload() const;
  std::span<const compat::FilePolicy> capturedFilePolicy() const noexcept;
  const std::string& capturedQueueMappingScope() const noexcept;
  std::shared_ptr<OriginalDiskFiles> mappedFiles() const;
  std::int64_t originalBytes() const noexcept;
  bool originalProgressRequiresPieces() const noexcept;
  std::expected<core::QueueFilePriorityStats,core::QueueFilePriorityError> filePriorityStatistics(
      std::span<const std::uint8_t>) const noexcept;
  std::expected<std::int64_t,storage::Error> verifiedOriginalBytes(std::span<const std::uint8_t>) const;
  std::span<const ManagedPayloadClaim> claims() const noexcept;
  const storage::OriginalTorrentIdentity& identity() const;
  // Only after this exact handle's save-resume flush + storage weak-expiration
  // removal fence. Totals are this admission's actual payload byte deltas;
  // they are checked and added to the captured saved cumulative counters.
  // allComplete must be actual is_seeding, never selected-files is_finished.
  // mapClassified additionally requires this admission's checked alert and a
  // current checked piece map; an interrupted check must leave unknown pieces.
  // retainUnclassifiedOriginal is owner-only: no checked alert, resume rejection
  // or hash failure, after flush/join. Unchanged checked input bits may survive
  // a paused admission; fresh payload proofs are still mandatory here.
  std::expected<void,storage::Error> checkpoint(const storage::ProfileLease&,
      std::span<const std::uint8_t> verifiedPieces,bool allComplete,
      std::int64_t downloaded,std::int64_t uploaded,
      std::int64_t secondsDownloading = 0, std::int64_t secondsSeeding = 0,
      std::optional<std::int32_t> secondsSinceUpload = {},
      std::optional<ManagedBadPayloadBytes> badPayloadBytes = {},
      bool badPayloadComplete = true,std::optional<ManagedHistoryCompletionEvent> historyEvent = {},
      bool mapClassified = false,bool retainUnclassifiedOriginal = false);
private:
  static std::expected<ManagedStorage,storage::Error> prepareImpl(
      const storage::ProfileLease&,const core::TorrentHash&,const NetworkPolicy&,
      std::span<const ManagedPayloadClaim>,std::span<const storage::OriginalTorrentIdentity>,
      std::shared_ptr<const ManagedPayloadCounterBridge>,ManagedStoragePurpose,const OwnedI2pPolicy*,const MixedPublicPolicy* = nullptr,std::stop_token = {});
  std::expected<void,storage::Error> beginAllocation(const storage::ProfileLease&);
  struct Impl;
  explicit ManagedStorage(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

} // namespace detail
} // namespace biglybtc::engine
