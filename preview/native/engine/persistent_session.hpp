#pragma once
#include "biglybtc/tracker_statistics.hpp"

#include "local_session.hpp"
#include "managed_download.hpp"
#include "original_torrent_identity.hpp"
#include "live_settings.hpp"
#include "biglybtc/runtime_metrics.hpp"
#include "biglybtc/peer_payload_ledger.hpp"
#include "biglybtc/effective_rate_limits.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>

namespace biglybtc::engine {

struct AccountingIdentity {
  std::uint64_t applicationRun = 0;
  std::uint64_t profileSession = 0;
  std::uint64_t engineEpoch = 0;
  bool operator==(const AccountingIdentity&) const = default;
};
enum class AccountingState { Starting, Idle, Active, Retiring, Closing, Closed, Uncertified };
enum class AccountingError {
  InvalidIdentity, WrongThread, ActiveOperation, Closing, Deadline,
  CounterMapping, CounterResponseLost, CounterInvalid, CounterRegression,
  StorageFenceLost, OwnerResetRequired, Memory, Engine, DependencyUnqualified
};
struct ApplicationPeerObservation {
  AccountingIdentity identity;
  std::uint64_t sequence = 0;
  AccountingState state = AccountingState::Starting;
  // All measurements from the SAME persistent LT session. No operation sums.
  std::optional<core::EngineStatistics> observed;
  // Actual owner lifetime, separate from EngineStatistics first-observation time.
  std::int64_t ownerElapsedNanoseconds = 0;
  core::SessionRateLimits rateLimits{};
};

using ApplicationObservationSink = std::function<void(ApplicationPeerObservation)>;

struct NetworkPolicy;
struct PendingMagnet;
namespace detail { class PersistentCall; class PersistentOperation; }
class PersistentSession;
// Only the source-bound successful retirement protocol may mint a settled
// checkpoint. It is NOT epoch-final while a later admission is possible.
// Copying a receipt cannot turn a last-observed tail into settled evidence.
class SettledPeerCounters final {
public:
  const AccountingIdentity& identity() const noexcept { return identity_; }
  const core::EngineCounters& counters() const noexcept { return counters_; }
  std::uint64_t sequence() const noexcept { return sequence_; }
private:
  friend class PersistentSession;
  SettledPeerCounters(AccountingIdentity, core::EngineCounters, std::uint64_t) noexcept;
  AccountingIdentity identity_;
  core::EngineCounters counters_;
  std::uint64_t sequence_;
};
struct RetirementReceipt {
  // Nonzero admission attempt, including repeats of the same info hash.
  std::uint64_t operation = 0;
  // A lifetime/retirement barrier, NOT partfile metadata durability. Existing
  // explicit payload fsync and compatible state publication remain mandatory.
  bool storageReleased = false;
  std::optional<SettledPeerCounters> peerCounters;
  // No value means no diagnostics failure; presence never fabricates a seal.
  std::optional<AccountingError> accountingFailure;
};
struct CloseReceipt {
  RetirementReceipt retirement;
  // Epoch-final evidence requires this successful explicit close (permanently
  // closed admission gate), joined==true, storageReleased and peerCounters.
  bool sessionJoined = false;
  std::optional<storage::Error> checkpointFailure{};
};

// Confined to the existing Host executor's worker thread from creation through
// destruction. The service outlives individual continuation operations. The
// managed path retains multiple torrents in the same LT session. The legacy
// continuation path remains exclusive and reuses the same absolute counters.
// No raw session/handle is exposed to the UI or callers; all pop_alerts go through
// one internal pump. No original profile or new public network is implied.
class PersistentSession final {
public:
  // Default builds reject reuse before creating any LT session. Only a private
  // reviewed source/runtime binding enables the pending qualification path.
  static std::expected<std::unique_ptr<PersistentSession>, AccountingError> create(AccountingIdentity);
  ~PersistentSession(); // Fallback abort+join only; never promises an exact seal.
  PersistentSession(const PersistentSession&) = delete;
  PersistentSession& operator=(const PersistentSession&) = delete;
  PersistentSession(PersistentSession&&) = delete;
  PersistentSession& operator=(PersistentSession&&) = delete;

  // Explicit application/profile startup only, never Statistics navigation.
  // Bind the held lease and initialize the same isolated LT session used by
  // prepare(); no torrent, synthetic sample, new pump or final receipt is made.
  // create() stays lazy/source-guarded for existing independent API callers.
  std::expected<void, AccountingError> startIdle(const storage::ProfileLease&);

  // Configure before startIdle/admission. No live routing-policy mutation.
  std::expected<void, storage::Error> configureNetwork(const NetworkPolicy&);
  // Host calls pure prepareLiveSettings BEFORE persistence, this method only
  // AFTER successful save. Routing changes remain pending for owner recreation.
  // Before startIdle this caches safe values without creating an LT session.
  std::expected<LiveSettingsResult, storage::Error> applyLiveSettings(
      const NetworkPolicy&, std::optional<LiveQueueLimits> queues = {});
  // Actual get_settings projection; worker-only/no engine creation or alert pop.
  std::optional<LiveSettingsSnapshot> liveSettings() const noexcept;
  // Optional original-rule controller. Pure validation precedes activation;
  // it uses this owner's sole pump and never enables a second LT scheduler.
  std::expected<void, storage::Error> configureQueuePolicy(const core::QueuePolicy&, int configuredSmoothingSeconds=30);
  std::optional<core::QueuePolicy> queuePolicy() const;
  // Worker-affine, nonblocking admission/control. The sole poll pump advances
  // checking, transfer, flush/removal and compatible checkpoints for every hash.
  std::expected<void, storage::Error> startManaged(const storage::ProfileLease&, const ManagedDownloadOptions&);
  std::expected<void, storage::Error> pauseManaged(const core::TorrentHash&, bool paused);
  std::expected<void, storage::Error> queueManaged(const core::TorrentHash&, bool forceStart, bool queued);
  std::expected<void, storage::Error> stopManaged(const core::TorrentHash&);
  // Release retained path descriptors/obsolete overlay only after successful
  // flush, exact handle removal and compatible checkpoint. Owner counters stay.
  std::expected<void, storage::Error> forgetManaged(const core::TorrentHash&, bool removePeerHints = false);
  // Uses THIS actual owner, not a caller-supplied empty retained list. Stop
  // first; certified source retirement releases obsolete descriptors through
  // forgetManaged. New companion remains stopped and is never auto-admitted.
  std::expected<core::TorrentHash, storage::Error> createHybridCompanion(
      storage::ProfileLease&, const core::TorrentHash&, storage::OriginalSelectedHash, std::stop_token = {});
  std::vector<ManagedDownloadStatus> managedDownloads() const;
  // Last accepted bits for diagnostics only; copies never grant release.
  std::vector<std::uint8_t> verifiedSeedPieces(const core::TorrentHash&) const;
  bool managedAvailable() const noexcept;
  std::optional<unsigned short> tcpListenPort() const noexcept;
  // Already-persisted original pending intent. Metadata uses a non-filesystem
  // disk route in this SAME session; actual validated metadata is imported only
  // after its exact handle has retired. The held profile path is authenticated.
  std::expected<void, storage::Error> startMagnet(storage::ProfileLease&, const PendingMagnet&,
      const std::filesystem::path& profile);
  std::expected<void, storage::Error> cancelMagnet(const core::TorrentHash&);
  std::vector<PendingMagnetStatus> pendingMagnets() const;
  std::expected<void, storage::Error> reorderManaged(std::span<const core::TorrentHash>);

  // Stable owner-level observation sink/mailbox is connected outside the ctor.
  // UI callback failure/coalescing must not disable counter retention or sealing.
  std::expected<void, AccountingError> setObservationSink(ApplicationObservationSink);
  void clearObservationSink() noexcept;
  std::optional<ApplicationPeerObservation> latest() const;
  // Worker-only optional last accepted stats snapshot. No request or engine creation.
  std::shared_ptr<const core::RuntimeMetrics> diagnostics() const noexcept;
  // Passive, lossy per-peer observations only. No IO/request, complete tail,
  // country mapping, aggregate byte total or accounting certification.
  std::optional<core::PeerPayloadSnapshot> peerPayloads() const noexcept;
  // Passive read, with the same isolation policy as this owner.
  std::optional<core::TrackerStatistics> trackerStatistics() const noexcept;
  // Cleared before each admitted call, so failed preparation cannot expose an
  // earlier operation receipt. Returns owned scalar evidence only.
  std::optional<RetirementReceipt> lastRetirement() const;
  // Called by a coalesced worker poll while idle; active waits pump internally.
  // Requests at most one stats response and does no file/profile writes.
  std::expected<void, AccountingError> poll();

  // Explicit owner-thread close before the GUI disappears. No new admissions.
  // Deadline bounds certification effort, not an OS/LT thread-join guarantee.
  // Failure returns uncertified; no destructor callback or inferred final0.
  CloseReceipt close(std::chrono::steady_clock::time_point deadline) noexcept;
private:
  friend class detail::PersistentCall;
  friend class detail::PersistentOperation;
  struct Impl;
  explicit PersistentSession(std::unique_ptr<Impl>) noexcept;
  std::unique_ptr<Impl> impl_;
  friend std::expected<LocalReport, storage::Error> continueLocal(PersistentSession&,
      const storage::ProfileLease&, const LocalOptions&);
};

// New opt-in persistent path. Existing convenience API remains available for
// independent generated/CLI callers; its owner must still close/join explicitly.
// Success requires the SAME original payload/state completion prerequisites plus
// the matching storage-retirement fence. Stats failure alone is diagnosed, not
// translated into fake download failure or a fabricated exact final count.
std::expected<LocalReport, storage::Error> continueLocal(PersistentSession&,
    const storage::ProfileLease&, const LocalOptions&);

} // namespace biglybtc::engine
