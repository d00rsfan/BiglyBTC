#pragma once
#include "biglybtc/tracker_statistics.hpp"

#include "local_session.hpp"
#include "managed_download.hpp"
#include "coexist_queue.hpp"
#include "original_torrent_identity.hpp"
#include "live_settings.hpp"
#include "mainline_scrape_estimate.hpp"
#include "biglybtc/runtime_metrics.hpp"
#include "biglybtc/peer_payload_ledger.hpp"
#include "biglybtc/effective_rate_limits.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

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
// Last authenticated regular library response. This owns values only and
// supplies no new admission, scheduler/peer permission or synthetic queue epoch.
struct LibraryTrackerResponse {
  core::TorrentHash hash;
  core::QueueOriginalScrapeResponse response;
  std::int64_t observedWallMillis=0;
  std::uint64_t admission=0; // Zero is saved-only; nonzero requires this exact live owner.
};
// Actual retained per-URL tracker observation for one live managed admission.
// It is not a best-active selection, regular scrape/queue permission or rank.
struct ManagedTrackerUrlResponse {
  std::string url;
  core::QueueOriginalScrapeResponse response;
  std::int64_t observedWallMillis=0;
  std::uint64_t sequence=0;
};
struct MainlineScrapeCandidateStatus {
  core::TorrentHash hash;
  std::uint64_t admission;
};
struct MainlineDiscoveryStatus {
  enum class State { Disabled, Running, Unavailable, Closed };
  State state=State::Disabled;
  std::uint64_t lookups=0, announces=0, deliveredPeers=0;
  std::optional<storage::Error> failure{};
  bool checkpointLoaded=false, checkpointSaved=false;
  std::size_t legacyHints=0;
  std::optional<storage::Error> checkpointFailure{};
  // Optional source-bound PORT bridge; counters describe accepted provider wire events.
  bool portBridgeAvailable=false,portHintsLost=false;
  std::uint64_t portHintsObserved=0,portHintsAccepted=0,portHintsRejected=0;
  std::optional<storage::Error> queuedScrapeFailure{};
  bool originalIdentityRequested=false,originalIdentityActive=false,originalIdentityJoined=false;
  std::uint64_t originalIdentityAdoptions=0,originalIdentityRetirements=0;
  std::vector<MainlineScrapeCandidateStatus> admittedScrapeCandidates{};
};
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
  // The held profile lease must outlive this owner when Mainline caching is enabled.
  // Bind the held lease and initialize the same isolated LT session used by
  // prepare(); no torrent, synthetic sample, new pump or final receipt is made.
  // create() stays lazy/source-guarded for existing independent API callers.
  std::expected<void, AccountingError> startIdle(const storage::ProfileLease&);

  // Configure before startIdle/admission. No live routing-policy mutation.
  std::expected<void, storage::Error> configureNetwork(const NetworkPolicy&);
  // Explicit bootstrap authority for owned/private deployments; empty means no routers.
  // Must be configured before startup. No provider, DNS or sockets are touched here.
  // Explicit Host opt-in before startIdle. Retains the SAME OFD lease and
  // original source; a failed restore latches optional discovery unavailable.
  // Does not acquire another owner, write original files or start a service.
  std::expected<void, storage::Error> configureOriginalMainlineIdentity(const storage::ProfileLease&);
  std::expected<void, storage::Error> configureMainlineBootstrap(std::vector<std::pair<std::string,int>>);
  MainlineDiscoveryStatus mainlineDiscovery() const;
  // Last accepted saved-target estimates; no LT/profile IO or rank authority.
  std::vector<MainlineScrapeEstimate> mainlineScrapeEstimates() const;
  std::vector<LibraryTrackerResponse> libraryTrackerResponses() const; // Passive last successful worker refresh, no IO/drain.
  void revokeMainlineScrape(const core::TorrentHash&) noexcept;
  // Call only after a successful explicit saved-intent/controller transaction.
  std::expected<void,storage::Error> rearmMainlineScrape(const core::TorrentHash&);
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
  // Explicit Host union controller; configure before the first managed admission.
  // Snapshot does not evaluate a plan or perform filesystem writes. Queue must
  // settle on BOTH providers before the Host submits either provider's Run.
  std::expected<void,storage::Error> configureExternalQueue(bool);
  std::expected<ExternalQueueSnapshot,storage::Error> externalQueueSnapshot();
  std::expected<bool,storage::Error> applyExternalQueue(std::uint64_t sequence,
      std::span<const ExternalQueueDecision>,std::uint32_t unavailableInputs,core::QueueAction phase);
  std::expected<void,storage::Error> applyExternalQueuePositions(
      std::span<const std::pair<QueueBinding,std::uint32_t>>);
  std::expected<void,storage::Error> applyExternalRateLimits(const core::SessionRateLimits&);
  RetainedPayloads retainedPayloads() const;
  // Host-worker IO only. The callbacks wrap synchronous source mutations, never
  // session startup, handle methods, blocking SDK barriers or caller observers.
  std::expected<void,storage::Error> configureSourcePublication(TargetPublication,PositionsPublication);
  // Worker-affine, nonblocking admission/control. The sole poll pump advances
  // checking, transfer, flush/removal and compatible checkpoints for every hash.
  std::expected<void, storage::Error> startManaged(const storage::ProfileLease&, const ManagedDownloadOptions&, const RetainedPayloads& external = {});
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
  // Passive worker-only copy of the current rich library response. No RPC,
  // callback draining, status query or new permission; exact admission required.
  std::optional<core::QueueOriginalScrapeResponse> managedTrackerResponse(
      const core::TorrentHash&, std::uint64_t admission) const;
  // Passive worker-only URL-labelled observations from the same rich provider
  // ring. No drain, RPC, current-announcer inference or additional IO.
  std::vector<ManagedTrackerUrlResponse> managedTrackerUrlResponses(
      const core::TorrentHash&, std::uint64_t admission) const;
  // Narrow original single-helper selection: one saved tier of numeric UDP
  // URLs, current URL from actual dispatch alerts, same retained response.
  // Passive counts only; this does not enable a regular/Mainline scrape.
  std::optional<ManagedTrackerUrlResponse> managedActiveUdpTrackerResponse(
      const core::TorrentHash&, std::uint64_t admission) const;
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
