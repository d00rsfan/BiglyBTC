#include "persistent_session_support.hpp"
#ifdef BIGLYBTC_ENGINE_SHARED_CHAT
#include "../services/shared_udp/provider.hpp"
#endif
#include "shared_seed_announce.hpp"
#include "verified_seeding.hpp"
#include "allocation_intent.hpp"
#include "torrent_state.hpp"
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include "managed_payload_counters.hpp"
#include "managed_queue_observations.hpp"
#include "managed_tracker_responses.hpp"
#include "library_udp_policy.hpp"
#include "library_http_policy.hpp"
#include "tracker_statistics_sampler.hpp"
#include "persistent_runtime_guard.hpp"
#include "runtime_metrics_sampler.hpp"
#include "effective_rate_limits.hpp"
#include "peer_payload_observer.hpp"
#include "managed_storage.hpp"
#include "check_scheduler.hpp"
#include "mixed_i2p_source.hpp"
#include "mixed_i2p_transport.hpp"
#include "library_commands.hpp"
#include "queue_positions.hpp"
#include "magnet_intake.hpp"
#include "torrent_intake.hpp"
#include "durable_intake_receipt.hpp"
#include "metadata_disk_router.hpp"
#include "pending_metadata_source.hpp"
#include "mmap_disk_latency.hpp"
#include "original_disk_io.hpp"
#include <libtorrent/session_params.hpp>
#include <libtorrent/write_resume_data.hpp>
#include "network_policy.hpp"
#include "mainline_dht.hpp"
#include "mainline_bind_plan.hpp"
#include "plugin_settings.hpp"
#include "queued_mainline_scrape.hpp"
#include "mainline_checkpoint.hpp"
#if BIGLYBTC_ORIGINAL_MLDHT_IDENTITY_READER
#include "mainline_identity.hpp"
#endif
#include <libtorrent/peer_info.hpp>
#include <libtorrent/hasher.hpp>
#include <openssl/rand.h>
#include "network_binding.hpp"
#include "network_peer_classes.hpp"
#include "biglybtc/queue_rate_providers.hpp"
#include <libtorrent/torrent_status.hpp>
#include <map>
#include <atomic>
#include <libtorrent/extensions.hpp>
#if defined(__linux__) && __has_include(<libtorrent/biglybtc_mainline_port.hpp>) && !defined(TORRENT_DISABLE_EXTENSIONS)
#include <libtorrent/biglybtc_mainline_port.hpp>
#if TORRENT_BIGLYBTC_MAINLINE_PORT_ABI == 1
#include <dlfcn.h>
#define BIGLYBTC_MAINLINE_PORT_BRIDGE 1
#endif
#endif
#ifndef BIGLYBTC_MAINLINE_PORT_BRIDGE
#define BIGLYBTC_MAINLINE_PORT_BRIDGE 0
#endif

#include <libtorrent/alert_types.hpp>
#include <libtorrent/socket_type.hpp>
#include <libtorrent/operations.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <iterator>
#include <array>
#include <limits>
#include <new>
#include <string_view>
#include <utility>
#include <ratio>
#include <sys/stat.h>
#include <thread>
#include <type_traits>

namespace biglybtc::engine {
namespace {
namespace lt = libtorrent;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
static_assert(LIBTORRENT_VERSION_NUM == 20012,
    "The conditional peer-counter seal is source-bound to libtorrent 2.0.12");
static_assert(Clock::is_steady && std::ratio_equal_v<Clock::period, std::nano>);
static_assert(std::is_integral_v<Clock::rep> && std::is_signed_v<Clock::rep>
    && std::numeric_limits<Clock::rep>::digits <= std::numeric_limits<std::int64_t>::digits);
constexpr std::int64_t CadenceNanoseconds = 200000000;
constexpr auto CertificationBudget = 5s;
constexpr std::array<std::string_view, 4> MetricNames{
    "net.recv_bytes", "net.sent_bytes", "net.recv_payload_bytes", "net.sent_payload_bytes"};

bool subtract(const core::EngineCounters& value, const core::EngineCounters& baseline,
    core::EngineCounters& result) noexcept {
  // Each subtraction is within [0,MAX]; the accumulator independently checks
  // that derived protocol counters are also nonnegative and monotone.
  if (value.received < baseline.received || value.sent < baseline.sent
      || value.payloadReceived < baseline.payloadReceived || value.payloadSent < baseline.payloadSent) { return false; }
  result = {value.received - baseline.received, value.sent - baseline.sent,
      value.payloadReceived - baseline.payloadReceived, value.payloadSent - baseline.payloadSent};
  return true;
}
} // namespace

#if !defined(TORRENT_DISABLE_EXTENSIONS)
namespace { class RecheckPeerGate final:public lt::torrent_plugin {
  std::shared_ptr<lt::peer_plugin> new_connection(const lt::peer_connection_handle& connection) override {
    auto peer=connection;peer.disconnect(lt::error_code(lt::errors::torrent_paused),lt::operation_t::bittorrent);return {};
  }
}; }
#endif
struct PersistentSession::Impl {
  struct MainlinePortQueue;
  struct OperationToken { std::uint64_t number = 0; } token;
  struct Managed {
    std::unique_ptr<OperationToken> token;
    detail::ManagedStorage storage;
    std::shared_ptr<detail::MixedManagedLifetime> mixedLifetime;
    std::shared_ptr<const MixedManagedSource> mixedSource;
    std::shared_ptr<detail::ManagedPayloadCounters> payloadCounters = std::make_shared<detail::ManagedPayloadCounters>();
    lt::torrent_handle handle;
    ManagedDownloadStatus status;
    bool manualPaused = false, forceStart = false, stop = false;
    bool categoryRebaseFailed=false; // SDK stop deferred until outside FS gate.
    std::shared_ptr<MainlinePortQueue> mainlinePort;
    bool mainlineTrackerHealthy=false;
    Clock::time_point mainlineNext{};
    bool policyHeld = false, completedTransition = false, completionHandled = false;
    bool externalPauseSettled=false,externalPausePending=false,checkedAlert=false,resumeRejected=false,observedHashFailure=false;
    bool checkPermit=false,checkingHeld=false,checkPauseDrained=false;
    std::shared_ptr<const storage::ProfileLease> historyLease;
    std::optional<bool> historyPreviousComplete;
    bool historySawIncomplete=false,historyEventComplete=false;
    std::optional<std::int64_t> historyCompletionMillis;
    std::optional<storage::ErrorCode> historyCompletionProblem;
    bool historyCheckpointed=false,historyTaken=false;Clock::time_point historyCheckpointEnd{};

    core::QueueActivityMemory activity;
    std::optional<core::QueueFilePriorityStats> filePriority;
    std::optional<std::int64_t> queueObservedMillis;
    std::optional<core::QueueProbeScope> probeScope;
    std::unique_ptr<core::QueueTargetEta> targetEta;
    std::uint64_t targetStatisticsSequence=0;
    std::shared_ptr<detail::ManagedQueueObservations> queueObservations;
    std::shared_ptr<detail::ManagedTrackerResponse> trackerResponse;
    std::optional<std::string> activeUdpTrackerScope;
    std::shared_ptr<detail::MmapDiskLatency> mmapLatency;
    std::optional<Clock::time_point> runningSince, seedingSince;
    std::optional<lt::time_point> trackerRunningSince, trackerSeedingSince;
    std::int64_t secondsDownloading = 0, secondsSeeding = 0;
    lt::time_point timelineCursor = lt::clock_type::now();
    lt::torrent_status::state_t timelineState = lt::torrent_status::checking_resume_data;
    bool timelinePaused = true, timelineLost = false;
    lt::time_duration downloadElapsed{}, seedElapsed{};
    void advanceTimeline(lt::time_point stamp) {
      if (stamp < timelineCursor) { timelineLost = true; return; }
      if (!timelinePaused) {
        if (timelineState == lt::torrent_status::downloading) { downloadElapsed += stamp-timelineCursor; }
        else if (timelineState == lt::torrent_status::finished || timelineState == lt::torrent_status::seeding) { seedElapsed += stamp-timelineCursor; }
      }
      timelineCursor = stamp;
    }
    void projectTimeline(lt::time_point now) {
      auto downloading=downloadElapsed, seeding=seedElapsed;
      if (now>=timelineCursor && !timelinePaused) {
        if (timelineState == lt::torrent_status::downloading) { downloading += now-timelineCursor; }
        else if (timelineState == lt::torrent_status::finished || timelineState == lt::torrent_status::seeding) { seeding += now-timelineCursor; }
      }
      secondsDownloading=std::chrono::duration_cast<std::chrono::seconds>(downloading).count();
      secondsSeeding=std::chrono::duration_cast<std::chrono::seconds>(seeding).count();
    }
    std::optional<std::int64_t> idleUploadSeconds;
    core::QueueIdleUploadAge idleUpload;bool idleUploadKnown=false;std::uint64_t idleSequence=0;
    std::optional<std::pair<std::int32_t,std::int64_t>> idleCheckpoint;
    std::optional<core::QueueSwarm> swarm;
    int connectedLeechers = 0;
    core::QueueObservedFacts queueSource;
    std::unique_ptr<core::QueueSendRateSmoothing> sendSmoothing;
    std::optional<std::int64_t> sendFirstMillis,smoothedUpload;
    std::uint64_t sendSequence=0;
    std::optional<std::uint32_t> queuePosition;
    std::optional<bool> positionTransition;
    std::uint64_t positionTransitionOrder=0;
    bool positionTransitionLost=false;
    std::int64_t remainingBytes = 0;
    std::optional<detail::ManagedBadPayloadBytes> badPayloadBytes;
    bool flushed = false, removing = false, removed = false;
    Clock::time_point deadline{};
    std::vector<std::uint8_t> verified;
    std::optional<storage::VerifiedSeedIntent> seedIntent;
    std::optional<storage::AllocationIntent> allocationIntent;
    std::stop_token recheckStop;bool recheckChecked=false,recheckCancelled=false;
    bool allocationAttempted=false;
    bool seedAttempted=false,seedPiecesExact=false;
    Clock::time_point seedDeadline{};
    lt::add_torrent_params seedNetwork;
    Managed(std::uint64_t id, detail::ManagedStorage files, const core::TorrentHash& hash)
        : token(std::make_unique<OperationToken>(OperationToken{id})), storage(std::move(files)),
          status{.hash = hash, .admission = id} {status.originalResumeDecision=storage.originalResumeDecision();idleUpload=core::QueueIdleUploadAge(storage.queueMetadata().secondsSinceUpload);}
    ~Managed(){if(mixedLifetime){mixedLifetime->attached=false;mixedLifetime->retained=false;}}
  };
  std::map<std::string, std::unique_ptr<Managed>> managed;
  std::map<lt::torrent_handle, Managed*> managedByHandle;
  const storage::ProfileLease* managedLease = nullptr; // Same bound lease, owned by Host.
  std::shared_ptr<const storage::ProfileLease> historyConsumerLease; // Explicit optional consumer only.
  std::optional<NetworkPolicy> network;
  std::optional<NetworkBinding> networkBinding;
  std::optional<LiveQueueLimits> liveQueueLimits;
  std::optional<core::QueuePolicy> queuePolicy;
  std::optional<detail::CheckPolicy> checkPolicy;
  bool checkPolicyRead=false;
  bool externalQueue=false,externalSnapshotValid=false;
  std::uint64_t externalQueueSequence=0;
  std::optional<core::SessionRateLimits> externalRates;
  TargetPublication targetPublication;PositionsPublication positionsPublication;
  std::expected<void,storage::Error> publishTarget(const core::TorrentHash& hash,const SourceMutation& f){
    return targetPublication?targetPublication(hash,f):f();
  }
  std::expected<void,storage::Error> publishPositions(const SourceMutation& f){
    return positionsPublication?positionsPublication(f):f();
  }
  int targetSmoothingSeconds=30;
  bool queuePositionsPending=true,queuePositionsUnavailable=true;
  // A failed save invocation may have published its CAS. Never replay its old
  // event in this owner. Refusals before save may retry on a new real trigger;
  // pending=false prevents repeated reads from otherwise unchanged poll().
  bool queuePositionsBlocked=false;
  std::uint64_t queuePositionObservation=0;
  std::shared_ptr<const storage::Error> queuePositionFailure;
  core::QueueDownloadProbes downloadProbes;
  core::QueueGlobalReceiveAverage queueGlobalAverage;
  std::optional<std::int32_t> queueGlobalReceiveKiB;
  std::optional<std::int64_t> queueGlobalObservedMillis;
  core::QueueSendRateSmoothing queueGlobalSend;
  std::optional<std::int64_t> queueGlobalSendRate;
  bool appliedSeedingOnly = false;
  struct Magnet {
    std::unique_ptr<OperationToken> token;
    PendingMagnet intent;
    lt::torrent_handle handle;
    PendingMagnetStatus status;
    Clock::time_point expires{}, deadline{}, mainlineNext{};
    std::shared_ptr<MainlinePortQueue> mainlinePort;
    bool removing = false, removed = false, cancel = false, shutdown = false;
    std::string metadata;
    std::shared_ptr<detail::PendingMetadataLifetime> pendingLifetime;
    std::shared_ptr<const PendingMetadataSource> pendingSource;
    lt::add_torrent_params pendingParameters;
    bool metadataTaken=false,expiryCrossed=false;
    Magnet(std::uint64_t id, PendingMagnet value)
        : token(std::make_unique<OperationToken>(OperationToken{id})), intent(std::move(value)),
          status{.hash = intent.hash, .admission = id, .name = intent.displayName} {}
  };
  std::map<std::string, std::unique_ptr<Magnet>> magnets;
  std::shared_ptr<detail::MetadataDiskPolicy> diskPolicy = std::make_shared<detail::MetadataDiskPolicy>();
  storage::ProfileLease* magnetLease = nullptr;
  std::filesystem::path magnetProfile;
  std::vector<ManagedDownloadOptions> autoStarts;
  std::map<std::string, std::vector<lt::tcp::endpoint>> importPeers;
  bool closingAdmission = false;
  AccountingIdentity identity;
  const std::thread::id worker = std::this_thread::get_id();
  const std::int64_t origin = static_cast<std::int64_t>(Clock::now().time_since_epoch().count());
  std::shared_ptr<core::PeerPayloadLedger> peerPayloadLedger{};
  std::unique_ptr<lt::session> session{};
  bool mixedNetworks=false;
  std::shared_ptr<const detail::MixedManagedOrigin> mixedOrigin;
  PersistentSession::MixedSourceBinding mixedBinder;
  std::shared_ptr<MixedI2pTransport> mixedTransport;
  std::optional<ListenerFailureObservation> listenerFailure;
  AccountingState phase = AccountingState::Starting;
  core::SessionRateLimits rateLimits{};
  bool call = false;
  bool deferPublication = false;
  bool poisoned = false;
  bool closed = false;
  bool profileBound = false;
  dev_t profileDevice{};
  ino_t profileInode{};
  std::uint64_t operation = 0;
  lt::torrent_handle active{}; // Weak identity, including after object expiration.
  bool registered = false;
  bool removalSeen = false;
  bool removalLost = false;
  bool storageError = false;
  bool transferError = false;
  std::optional<RetirementReceipt> retirement{};
  std::optional<CloseWithoutCheckpointReceipt> drainReceipt;
  std::optional<ApplicationPeerObservation> observation{};
  ApplicationObservationSink sink{};
  MagnetCompletionHook magnetCompletionHook{};
  std::shared_ptr<const storage::ProfileLease> magnetCompletionLease;
  std::function<void(const core::EngineStatistics&)> operationSink{};
  std::optional<core::EngineCounters> operationBaseline{};
  core::EngineStatisticsAccumulator operationAccumulator{};
  core::EngineStatisticsAccumulator absoluteAccumulator{};
  detail::RuntimeMetricsSampler runtimeMetrics{};
  std::int64_t lastDiagnostics = -1;
  std::array<int, 4> indices{{-1, -1, -1, -1}};
  std::optional<AccountingError> counterFailure{};
  std::optional<core::EngineStatistics> lastStatistics{};
  std::uint64_t sequence = 0;
  std::uint64_t statisticsSequence = 0;
  std::uint64_t responses = 0;
  bool outstanding = false;
  bool forceNext = false;
  std::int64_t lastPost = 0;

  explicit Impl(AccountingIdentity value) : identity(value) {
    // Optional diagnostics cannot stop creation, transfer or retirement.
    try { peerPayloadLedger = std::make_shared<core::PeerPayloadLedger>(
        core::PeerPayloadIdentity{value.applicationRun, value.profileSession, value.engineEpoch}); }
    catch (...) { peerPayloadLedger.reset(); }
  }
  bool onWorker() const noexcept { return worker == std::this_thread::get_id(); }
  bool elapsed(std::int64_t& result) const noexcept {
    const auto now = static_cast<std::int64_t>(Clock::now().time_since_epoch().count());
    if (origin < 0 || now < origin) { return false; }
    result = now - origin;
    return true;
  }
  void counterError(AccountingError error) noexcept {
    // An outstanding request is NOT abandoned and relabeled as a fresh one.
    // Once sampling is disabled this epoch never posts or certifies again.
    if (!counterFailure) { counterFailure = error; }
    forceNext = false;
  }
  void publish() noexcept {
    if (sequence == std::numeric_limits<std::uint64_t>::max()) {
      counterError(AccountingError::CounterInvalid); return;
    }
    std::int64_t now = 0;
    if (!elapsed(now)) { counterError(AccountingError::CounterInvalid); return; }
    observation = ApplicationPeerObservation{identity, ++sequence,
        counterFailure ? AccountingState::Uncertified : phase, lastStatistics, now, rateLimits};
    if (deferPublication || !sink) { return; }
    try {
      // A local owning copy allows the optional sink to clear its registration
      // without destroying the std::function whose invocation is on the stack.
      const auto callback = sink;
      callback(*observation);
    } catch (...) { sink = {}; } // Essential accounting is already retained.
  }
  void mapCounters() noexcept {
    try {
      const auto metrics = lt::session_stats_metrics();
      for (std::size_t wanted = 0; wanted < MetricNames.size(); ++wanted) {
        bool found = false;
        for (const auto& metric : metrics) {
          if (!metric.name || std::string_view(metric.name) != MetricNames[wanted]) { continue; }
          if (found || metric.type != lt::metric_type_t::counter || metric.value_index < 0) {
            counterError(AccountingError::CounterMapping); return;
          }
          found = true; indices[wanted] = metric.value_index;
        }
        if (!found) { counterError(AccountingError::CounterMapping); return; }
        for (std::size_t earlier = 0; earlier < wanted; ++earlier) {
          if (indices[earlier] == indices[wanted]) { counterError(AccountingError::CounterMapping); return; }
        }
      }
      static_cast<void>(runtimeMetrics.map({metrics.data(), metrics.size()}));
    } catch (...) { counterError(AccountingError::CounterMapping); }
  }
  void post(bool force) noexcept {
    if (!session || counterFailure) { return; }
    if (outstanding) { forceNext |= force; return; }
    std::int64_t now = 0;
    if (!elapsed(now) || now < lastPost) { counterError(AccountingError::CounterInvalid); return; }
    if (!force && !forceNext && now - lastPost < CadenceNanoseconds) { return; }
    outstanding = true; forceNext = false; lastPost = now;
    try { session->post_session_stats(); }
    catch (...) { counterError(AccountingError::Engine); }
  }
  void retain(const lt::session_stats_alert& alert) noexcept {
    if (counterFailure) { return; }
    if (!outstanding || responses == std::numeric_limits<std::uint64_t>::max()) {
      counterError(AccountingError::CounterResponseLost); return;
    }
    outstanding = false;
    const auto counters = alert.counters();
    for (const int index : indices) {
      if (index < 0 || index >= counters.size()) { counterError(AccountingError::CounterMapping); return; }
    }
    const core::EngineCounters value{counters[indices[0]], counters[indices[1]],
        counters[indices[2]], counters[indices[3]]};
    std::int64_t now = 0;
    if (!elapsed(now)) { counterError(AccountingError::CounterInvalid); return; }
    const auto accepted = absoluteAccumulator.observe(value, now);
    if (!accepted) {
      counterError(accepted.error() == core::EngineStatisticsError::CounterRegression
          ? AccountingError::CounterRegression : AccountingError::CounterInvalid);
      return;
    }
    ++responses;
    lastStatistics = *accepted; // Fixed primitive retention precedes all sinks.
    // Optional queue arithmetic consumes this SAME accepted combined peer sample.
    // It never posts/pops, reads a clock, changes the counter seal, or substitutes
    // an instantaneous payload rate. OwnerCombined is explicitly native scope.
    if (origin <= std::numeric_limits<std::int64_t>::max() - now) {
      const auto stamp = (origin + now) / 1000000;
      queueGlobalReceiveKiB = queueGlobalAverage.observe(
          {{identity.engineEpoch, responses, stamp}, core::QueueRateScope::OwnerCombined, value.received});
      queueGlobalObservedMillis = stamp;
      const auto ticks=(stamp-origin/1000000)/1000;
      queueGlobalSendRate=ticks<=std::numeric_limits<std::int32_t>::max()
          ?queueGlobalSend.observe({{identity.engineEpoch,responses,stamp},core::QueueRateScope::OwnerCombined,value.sent},
              static_cast<std::int32_t>(ticks),targetSmoothingSeconds):std::optional<std::int64_t>{};
    } else {
      queueGlobalAverage = {};
      queueGlobalReceiveKiB.reset(); queueGlobalObservedMillis.reset();queueGlobalSend={};queueGlobalSendRate.reset();
    }
    // Optional copying cannot alter retained peer counters or their response/fence state.
    // Optional catalog copies run at 1Hz; essential peer accounting above
    // retains every accepted response at its independent higher cadence.
    if (lastDiagnostics < 0 || now - lastDiagnostics >= 1000000000) {
      lastDiagnostics = now;
      static_cast<void>(runtimeMetrics.capture(
          {counters.data(), static_cast<std::size_t>(counters.size())}, responses, now));
    }
    publish();
    statisticsSequence = sequence;
    if (!operationSink || !operationBaseline) { return; }
    core::EngineCounters relative{};
    if (!subtract(value, *operationBaseline, relative)) { operationSink = {}; return; }
    const auto sample = operationAccumulator.observe(relative, now);
    if (!sample) { operationSink = {}; return; }
    try { const auto callback = operationSink; callback(*sample); }
    catch (...) { operationSink = {}; } // Optional operation callback only.
  }
  bool matches(const lt::torrent_handle& handle) const noexcept {
    return registered && handle == active;
  }
  std::vector<lt::alert*> pop() {
    detail::require(bindingHealthy(), "Explicit network binding was lost; owner joined without a final certificate", storage::ErrorCode::Io);
    std::vector<lt::alert*> pending;
    for(auto& [key,value]:managed){(void)key;if(value->trackerResponse){value->trackerResponse->beginDispatchCollection();}}
    session->pop_alerts(&pending); // The ONLY pop_alerts consumer for this owner.
    if (pending.size() > 4096) {
      for(auto& [key,value]:managed){(void)key;if(value->trackerResponse){value->trackerResponse->loseDispatch();}value->activeUdpTrackerScope.reset();}
      removalLost = true; transferError = true;
      counterError(AccountingError::CounterResponseLost);
      return pending;
    }
    // Record losses/errors before publishing any optional callback. No borrowed
    // alert storage escapes this turn. A same-hash predecessor cannot match the
    // active weak ownership identity or its operation token.
    for (auto* alert : pending) {
      if (!alert) {
        for(auto& [key,value]:managed){(void)key;if(value->trackerResponse){value->trackerResponse->loseDispatch();}value->activeUdpTrackerScope.reset();}
        removalLost = true; transferError = true;
        counterError(AccountingError::CounterResponseLost); continue;
      }
      if (auto* dropped = lt::alert_cast<lt::alerts_dropped_alert>(alert)) {
        if(dropped->dropped_alerts.test(lt::tracker_announce_alert::alert_type)){
          for(auto& [key,value]:managed){(void)key;if(value->trackerResponse){value->trackerResponse->loseDispatch();}value->activeUdpTrackerScope.reset();}
        }
        if (dropped->dropped_alerts.test(lt::session_stats_alert::alert_type)) {
          counterError(AccountingError::CounterResponseLost);
        }
        if (dropped->dropped_alerts.test(lt::session_error_alert::alert_type)) {
          // An unattributed async engine exception may have affected removal or
          // storage, not just stats. No optional-request attribution is safe.
          transferError = true; removalLost = true;
          counterError(AccountingError::Engine);
        }
        if(externalQueue&&(dropped->dropped_alerts.test(lt::torrent_checked_alert::alert_type)
            ||dropped->dropped_alerts.test(lt::torrent_paused_alert::alert_type)
            ||dropped->dropped_alerts.test(lt::torrent_resumed_alert::alert_type))){transferError=true;removalLost=true;}
        for (int type : {lt::state_changed_alert::alert_type, lt::torrent_paused_alert::alert_type,
            lt::torrent_resumed_alert::alert_type}) {
          if (dropped->dropped_alerts.test(type)) {
            for (auto& [key,value] : managed) { static_cast<void>(key);value->timelineLost=true; }
          }
        }
        for (int type : {lt::torrent_removed_alert::alert_type, lt::file_error_alert::alert_type,
            lt::torrent_error_alert::alert_type, lt::save_resume_data_failed_alert::alert_type,
            lt::listen_failed_alert::alert_type}) {
          if (dropped->dropped_alerts.test(type)) { removalLost = true; transferError = true; }
        }
      }
      if (auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert);
          removed && matches(removed->handle) && removed->userdata.get<OperationToken>() == &token) {
        removalSeen = true;
      }
      if (auto* failed = lt::alert_cast<lt::file_error_alert>(alert); failed && matches(failed->handle)) {
        storageError = true; transferError = true;
      }
      if (auto* failed = lt::alert_cast<lt::torrent_error_alert>(alert); failed && matches(failed->handle)) {
        storageError = true; transferError = true;
      }
      if (auto* failed = lt::alert_cast<lt::save_resume_data_failed_alert>(alert); failed && matches(failed->handle)) {
        storageError = true; transferError = true;
      }
      if (const auto* failed=lt::alert_cast<lt::listen_failed_alert>(alert)) {
        // The qualified mixed SAM ACCEPT is optional to the sole Public owner.
        // This does not forgive TCP/UDP bind/listen/accept loss, unconfigured
        // I2P alerts, or any dropped essential alert. No route is regranted.
        const bool mixedAccept=mixedNetworks&&mixedTransport
            &&mixedTransport->state_->pin&&mixedTransport->state_->configured.load()
            &&failed->socket_type==lt::socket_type_t::i2p
            &&failed->op==lt::operation_t::sock_accept;
        const auto previous=listenerFailure?listenerFailure->sequence:std::uint64_t{0};
        listenerFailure=ListenerFailureObservation{previous==UINT64_MAX?previous:previous+1,
            static_cast<int>(failed->socket_type),static_cast<int>(failed->op),
            failed->error.value(),mixedAccept};
        if(!mixedAccept){transferError=true;}
      }
      if (lt::alert_cast<lt::session_error_alert>(alert)) {
        transferError = true; removalLost = true;
        counterError(AccountingError::Engine);
      }
    }
    for (auto* alert : pending) {
      if (!alert) { continue; }
      if (auto* statistics = lt::alert_cast<lt::session_stats_alert>(alert)) { retain(*statistics); }
      const auto* torrentAlert = dynamic_cast<const lt::torrent_alert*>(alert);
      if (!torrentAlert) { continue; }
      const auto owned = managedByHandle.find(torrentAlert->handle);
      if (owned == managedByHandle.end()) { continue; }
      auto& value = *owned->second;
      if(lt::alert_cast<lt::tracker_announce_alert>(alert)){value.status.trackerState=1;}
      if(lt::alert_cast<lt::tracker_reply_alert>(alert)){value.status.trackerState=2;}
      if(lt::alert_cast<lt::tracker_error_alert>(alert)){value.status.trackerState=3;}
      if(const auto* dispatched=lt::alert_cast<lt::tracker_announce_alert>(alert);dispatched&&value.trackerResponse&&!value.trackerResponse->sole()){
        auto& ring=*value.trackerResponse;
        const bool token=value.token&&value.token->number==value.status.admission&&ring.admission==value.status.admission;
        const auto& endpoint=dispatched->local_endpoint;
        const bool bound=networkBinding&&networkBindingCurrent(*networkBinding)&&networkBinding->effective.bindAddresses.size()==1
            &&networkBinding->effective.listenPort>0&&endpoint.port()==networkBinding->effective.listenPort
            &&endpoint.address().to_string()==networkBinding->effective.bindAddresses.front();
        if(!token||!bound||value.stop||value.removing){ring.loseDispatch();value.activeUdpTrackerScope.reset();}
        else{ring.observeUdpDispatch(dispatched->tracker_url(),dispatched->version==lt::protocol_version::V1?1u:2u,
            std::chrono::duration_cast<std::chrono::nanoseconds>(dispatched->timestamp().time_since_epoch()).count());}
      }
      if (lt::alert_cast<lt::tracker_reply_alert>(alert)) { value.mainlineTrackerHealthy=true; }
      if (lt::alert_cast<lt::tracker_error_alert>(alert)) { value.mainlineTrackerHealthy=false; }
      if (const auto* state = lt::alert_cast<lt::state_changed_alert>(alert)) {
        value.advanceTimeline(state->timestamp()); value.timelineState=state->state;
      }
      if(lt::alert_cast<lt::fastresume_rejected_alert>(alert)){value.resumeRejected=true;}
      if(lt::alert_cast<lt::hash_failed_alert>(alert)){value.observedHashFailure=true;}
      if(lt::alert_cast<lt::torrent_checked_alert>(alert)){value.checkedAlert=true;}
      if (const auto* paused = lt::alert_cast<lt::torrent_paused_alert>(alert)) {
        value.externalPauseSettled=true;value.externalPausePending=false;value.checkPauseDrained=true;
        value.advanceTimeline(paused->timestamp()); value.timelinePaused=true;
      }
      if (const auto* resumed = lt::alert_cast<lt::torrent_resumed_alert>(alert)) {
        value.advanceTimeline(resumed->timestamp()); value.timelinePaused=false;
      }
      if (const auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert);
          removed && removed->userdata.get<OperationToken>()==value.token.get()) {
        value.advanceTimeline(removed->timestamp());value.timelinePaused=true;value.removed=true;
      }
      if (const auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert); saved && value.stop) {value.flushed=true;}
      if(lt::alert_cast<lt::file_error_alert>(alert)||lt::alert_cast<lt::torrent_error_alert>(alert)
          ||lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
        value.status.failure=storage::Error{storage::ErrorCode::Io,"Torrent storage/flush failed; saved pieces require recheck"};
        value.status.phase=ManagedPhase::Failed;if(!value.stop){requestStop(value);}
      }
    }
    for (auto* alert : pending) {
      if (!alert) { continue; }
      auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert);
      auto* fileError = lt::alert_cast<lt::file_error_alert>(alert);
      auto* torrentError = lt::alert_cast<lt::torrent_error_alert>(alert);
      if (!removed && !fileError && !torrentError) { continue; }
      for (auto& [key, entry] : magnets) {
        static_cast<void>(key); auto& value = *entry;
        if (removed && removed->handle == value.handle
            && removed->userdata.get<OperationToken>() == value.token.get()) { value.removed = true; }
        if ((fileError && fileError->handle == value.handle) || (torrentError && torrentError->handle == value.handle)) {
          value.status.failure = storage::Error{storage::ErrorCode::Io, "Magnet metadata transfer failed; recovery intent retained"};
          requestMagnetRetirement(value);
        }
      }
    }
    return pending;
  }
  bool fresh(Clock::time_point deadline) noexcept {
    if (!session || counterFailure) { return false; }
    try {
      // Consume the actual older response before posting a new request. Timeout
      // disables this epoch: an old response can never become a new response.
      while (outstanding && !counterFailure && Clock::now() < deadline) {
        static_cast<void>(pop());
        if (outstanding) { std::this_thread::sleep_for(20ms); }
      }
      if (outstanding || counterFailure) {
        counterError(AccountingError::CounterResponseLost); return false;
      }
      if (Clock::now() >= deadline) {
        counterError(AccountingError::Deadline); return false;
      }
      const auto before = responses;
      post(true);
      while (!counterFailure && responses == before && Clock::now() < deadline) {
        static_cast<void>(pop());
        if (responses == before) { std::this_thread::sleep_for(20ms); }
      }
      if (counterFailure || responses == before) {
        counterError(AccountingError::CounterResponseLost); return false;
      }
      return true;
    } catch (...) { counterError(AccountingError::Engine); return false; }
  }
  void joinPoisoned() noexcept {
    magnetCompletionHook={};magnetCompletionLease.reset();
    operationSink = {};
    poisoned = true;
    phase = AccountingState::Uncertified;
    // Not bounded by the certification deadline. No callback is made by LT
    // destruction, and no joined tail is relabeled as a final counter sample.
    retireMainlinePorts();
    closeMainline();
    mainlineStatus.state=MainlineDiscoveryStatus::State::Closed;
    for(auto& [key,member]:managed){(void)key;if(member->mixedLifetime){member->mixedLifetime->attached=false;member->mixedLifetime->retained=false;}}
    if(mixedTransport){mixedTransport->retire();mixedTransport.reset();}
    session.reset();
    releaseRegularTrackerLease();
    for (auto& [key, entry] : managed) {
      static_cast<void>(key);
      if(entry->allocationIntent&&entry->status.retained&&!entry->allocationAttempted&&managedLease){
        entry->allocationAttempted=true;static_cast<void>(entry->storage.settleAllocation(*managedLease,false));
      }
      entry->status.retained = false;
      if(entry->mmapLatency){entry->mmapLatency->retire();}
      entry->queueSource={};entry->smoothedUpload.reset();entry->sendSmoothing.reset();entry->sendFirstMillis.reset();
      entry->status.queueSourceFacts.reset();entry->status.queueSmoothedPayloadUploadRate.reset();entry->status.queueIdleUploadSeconds.reset();entry->idleUploadSeconds.reset();entry->idleUploadKnown=false;
      entry->status.diskReadLatencyMillis.reset();entry->status.diskWriteLatencyMillis.reset();
      entry->status.staleRankOffset.reset();
      if(entry->status.verificationHeld){entry->status.verificationHeld=false;entry->seedAttempted=true;entry->status.verifiedSeeding=VerifiedSeedingPhase::Failed;}
      entry->status.phase = ManagedPhase::Failed;
      entry->status.queueTargetStatistics.reset(); entry->targetEta.reset();
      entry->status.failure = storage::Error{storage::ErrorCode::Io,
          "Engine owner joined after failure; original pieces require recheck"};
    }
    for (auto& [key, entry] : magnets) {
      static_cast<void>(key);if(entry->pendingLifetime){entry->pendingLifetime->retained.store(false);}
      entry->status.retained = false; entry->status.phase = MagnetPhase::Failed;
      entry->status.failure = storage::Error{storage::ErrorCode::Io, "Engine joined after failure; magnet recovery intent retained"};
    }
    outstanding = false;
    counterError(AccountingError::OwnerResetRequired);
    publish(); // Explicit uncertified state; retained values are last-observed.
  }
  bool bindingHealthy() noexcept {
    if (!session || !networkBinding || networkBindingCurrent(*networkBinding)) { return !poisoned; }
    transferError = true; removalLost = true; counterError(AccountingError::Engine);
    // This check occurs before pop_alerts; no borrowed alert is invalidated in
    // this turn. Joined destruction closes all existing network paths. There
    // is no rebind, wildcard replacement, second pump or new sampling timer.
    joinPoisoned(); return false;
  }
  void bind(const storage::ProfileLease& lease) {
    struct stat profile{};
    detail::require(lease.valid() && ::fstat(lease.directoryHandle(), &profile) == 0 && S_ISDIR(profile.st_mode),
        "Persistent owner requires its held profile lease", storage::ErrorCode::UnsafePath);
    if (profileBound) {
      detail::require(profile.st_dev == profileDevice && profile.st_ino == profileInode,
          "Persistent owner cannot cross profile identities", storage::ErrorCode::Conflict);
    } else { profileDevice = profile.st_dev; profileInode = profile.st_ino; profileBound = true; }
    if(libraryRegularTrackerMode()){
      if(regularTrackerLease){detail::require(lease.sharesOwnership(*regularTrackerLease),"Regular tracker profile ownership changed",storage::ErrorCode::Conflict);}
      else{auto retained=lease.retain();detail::require(retained.has_value(),"Regular tracker profile retention failed",storage::ErrorCode::Conflict);regularTrackerLease=std::move(*retained);}
    }
    if(!checkPolicyRead&&network&&network->mode==NetworkMode::Normal&&network->context==NetworkPolicyContext::ExistingLibraryPublic&&!externalQueue){
      auto config=storage::ConfigFile::load(lease,"biglybt.config");
      detail::require(config.has_value()||config.error().code==storage::ErrorCode::Missing,"Cannot read original checking policy",storage::ErrorCode::Io);
      auto policy=config?detail::readCheckPolicy(config->document().root()):std::expected<detail::CheckPolicy,const char*>{detail::CheckPolicy{}};
      detail::require(policy.has_value(),"Unsupported original checking policy",storage::ErrorCode::Unsupported);
      checkPolicy=*policy;checkPolicyRead=true;
    }
    mainlineLease=mainlineOriginalLease?mainlineOriginalLease.get():(regularTrackerLease?regularTrackerLease.get():&lease);
    detail::require(bindingHealthy(), "Explicit network binding was lost", storage::ErrorCode::Io);
  }
  #include "mainline_owner.inc"

  void ensureSession() {
    detail::require(bindingHealthy(), "Explicit network binding was lost", storage::ErrorCode::Io);
    if (!session) {
      try {
        auto settings = detail::settings();
        if (network) {
          auto resolved = prepareNetworkBinding(*network);
          detail::require(resolved.has_value(), "Configured network binding is unavailable", storage::ErrorCode::Io);
          networkBinding = std::move(*resolved);
          auto translated = makeNetworkSettings(networkBinding->effective);
          detail::require(translated.has_value(), "Network policy cannot be applied");
          settings = std::move(*translated);
        }
        if (liveQueueLimits) {
          auto live = makeLiveSettingsPack(network.value_or(NetworkPolicy{}), liveQueueLimits, appliedSeedingOnly);
          detail::require(live.has_value(), "Live startup settings cannot be applied");
          for (const int key : {lt::settings_pack::upload_rate_limit, lt::settings_pack::download_rate_limit,
              lt::settings_pack::active_downloads, lt::settings_pack::active_seeds, lt::settings_pack::active_limit}) {
            settings.set_int(key, live->get_int(key));
          }
          settings.set_bool(lt::settings_pack::dont_count_slow_torrents, false);
        }
        if(externalQueue&&externalRates){
          settings.set_int(lt::settings_pack::download_rate_limit,static_cast<int>(externalRates->download.bytesPerSecond));
          settings.set_int(lt::settings_pack::upload_rate_limit,static_cast<int>(externalRates->upload.bytesPerSecond));
        }
        if (queuePolicy || libraryUdpRegularMode() || (network && network->dht)) {
          settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::tracker
              | lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage));
        } else settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error
            | lt::alert_category::status | lt::alert_category::storage));
        lt::session_params parameters(settings);
        parameters.disk_io_constructor = detail::metadataDiskConstructor(diskPolicy);
        session = std::make_unique<lt::session>(std::move(parameters));
        session->pause();
        auto classes = makeNetworkPeerClassFilter(network.value_or(NetworkPolicy{}));
        detail::require(classes.has_value(), "Configured LAN rate classes are unavailable");
        session->set_peer_class_filter(*classes);
        applyNetworkLanRates(*session, network.value_or(NetworkPolicy{}));
        rateLimits = detail::readEffectiveRateLimits(*session); // Optional read, never applies settings.
        phase = AccountingState::Idle;
        mapCounters();
        startMainline();
      } catch (...) { joinPoisoned(); throw; }
    }
  }
  void prepare(const storage::ProfileLease& lease, const LocalOptions& options) {
    detail::require(call && onWorker() && !registered && !managedActive() && !magnetActive() && !poisoned && !closed,
        "Persistent owner is not available", storage::ErrorCode::Conflict);
    bind(lease);
    // Optional callback allocation failure does not prevent safe transfer or
    // essential absolute accounting. Every operation gets a fresh accumulator.
    operationSink = {};
    try { operationSink = options.statistics; } catch (...) { operationSink = {}; }
    operationBaseline.reset(); operationAccumulator = {};
    transferError = false; storageError = false; removalLost = false; removalSeen = false;
    ensureSession();
    // Fresh absolute baseline while no torrent is admitted. Failure omits the
    // operation observer; it never substitutes absolute owner totals for it.
    if (fresh(Clock::now() + CertificationBudget) && lastStatistics) {
      operationBaseline = lastStatistics->counters;
    }
    if (transferError || removalLost) {
      joinPoisoned();
      throw detail::Failure{{storage::ErrorCode::Io, "Persistent listener failed before admission"}};
    }
  }
  lt::torrent_handle add(lt::add_torrent_params parameters) {
    detail::require(call && !registered && !poisoned && session,
        "Persistent admission is unavailable", storage::ErrorCode::Conflict);
    token.number = operation;
    parameters.userdata = &token;
    detail::attachPeerPayloadObserver(parameters, peerPayloadLedger, operation);
    // Keep the new torrent paused until its returned weak handle is registered.
    // Any exception/ambiguous add forces abort+join before another admission.
    parameters.flags |= lt::torrent_flags::paused | lt::torrent_flags::duplicate_is_error;
    try {
      lt::error_code error;
      active = session->add_torrent(std::move(parameters), error);
      if (error || !active.is_valid()) {
        throw detail::Failure{{storage::ErrorCode::Io, "Cannot register persistent original payload"}};
      }
      registered = true;
      phase = AccountingState::Active;
      active.resume();
      session->resume();
      publish();
      return active;
    } catch (...) {
      if (!registered) {
        retirement = RetirementReceipt{operation, false, {}, AccountingError::OwnerResetRequired};
        joinPoisoned();
      }
      throw;
    }
  }
  bool managedActive() const noexcept {
    for (const auto& [key, entry] : managed) {
      static_cast<void>(key);
      if ((entry->allocationIntent&&entry->status.retained)||entry->handle.is_valid() || (entry->removing && !entry->removed)) { return true; }
    }
    return false;
  }
  void sampleManaged(Managed& value, bool pieces = false) {
    if(const auto mapped=value.storage.mappedFiles()){value.status.storageHashOperations=mapped->hashOperations();}

    value.queueSource={};value.smoothedUpload.reset();value.status.queueSourceFacts.reset();value.status.queueSmoothedPayloadUploadRate.reset();value.status.queueIdleUploadSeconds.reset();value.idleUploadSeconds.reset();value.idleUploadKnown=false;
    value.status.diskReadLatencyMillis.reset();value.status.diskWriteLatencyMillis.reset();
    value.status.cumulativeReceived.reset();value.status.cumulativeSent.reset();
    value.status.staleRankOffset.reset();
    if (!value.handle.is_valid() || value.removing) {
      value.status.queueTargetStatistics.reset(); value.targetEta.reset();value.sendSmoothing.reset();value.sendFirstMillis.reset(); return;
    }
    const bool originalProgress=value.storage.originalProgressRequiresPieces();
    const bool priorityObservation=queuePolicy&&queuePolicy->downloadOrder==core::QueueDownloadOrder::FilePriority;
    const bool needPieces=pieces||originalProgress||priorityObservation||value.status.verificationHeld||value.status.recheck.has_value();
    value.filePriority.reset();value.status.queueFilePriority.reset();
    value.status.combinedReceived.reset();value.status.combinedSent.reset();
    value.status.queueTargetStatistics.reset();
    lt::torrent_status sample;
    detail::ManagedPayloadCounters::Sample actual;
    for (int attempt = 0; attempt < 4; ++attempt) {
      const auto epoch = value.payloadCounters->epoch();
      if (!epoch) { break; }
      sample = value.handle.status(needPieces ? lt::torrent_handle::query_pieces : lt::status_flags_t{});
      std::optional<detail::ManagedPayloadCounters::Protocol> protocol;
      if(sample.total_payload_download>=0&&sample.total_payload_upload>=0
          &&sample.total_download>=sample.total_payload_download&&sample.total_upload>=sample.total_payload_upload){
        // Exact same accepted synchronous status and reset epoch, no extra call.
        protocol=detail::ManagedPayloadCounters::Protocol{sample.total_download-sample.total_payload_download,
            sample.total_upload-sample.total_payload_upload};
      }
      std::optional<detail::ManagedPayloadCounters::Bad> bad;
      if(sample.total_redundant_bytes>=0&&sample.total_failed_bytes>=0
          &&sample.total_redundant_bytes<std::numeric_limits<std::int64_t>::max()
          &&sample.total_failed_bytes<std::numeric_limits<std::int64_t>::max()){
        bad=detail::ManagedPayloadCounters::Bad{sample.total_redundant_bytes,sample.total_failed_bytes};
      }
      actual = value.payloadCounters->combine(*epoch, sample.total_payload_download, sample.total_payload_upload, protocol,bad);
      if (actual.state != detail::ManagedPayloadCounters::Read::Retry) { break; }
    }
    if (actual.state != detail::ManagedPayloadCounters::Read::Accepted) {
      value.status.failure = storage::Error{storage::ErrorCode::Io,
          "Exact managed transfer counters are unavailable; saved state requires recheck"};
      requestStop(value);
      value.status.phase = ManagedPhase::Failed;
      return; // Never checkpoint guessed/zero/tick-lagged transfer totals.
    }
    if(value.status.verificationHeld&&sample.errc){
      value.status.failure=storage::Error{storage::ErrorCode::Io,"Existing-data verification reported an actual disk error"};
      requestStop(value);value.status.phase=ManagedPhase::Failed;return;
    }
    value.status.retained = true;
    value.status.completed = sample.total_done;
    value.status.bytes = value.storage.originalBytes();
    value.status.received = actual.received;
    value.status.sent = actual.sent;
    const auto& original=value.storage.capturedDownload();
    value.status.cumulativeReceived=cumulativePayload(original.downloaded,actual.received);
    value.status.cumulativeSent=cumulativePayload(original.uploaded,actual.sent);
    value.status.combinedReceived=actual.combinedReceived;value.status.combinedSent=actual.combinedSent;
    value.badPayloadBytes.reset();value.status.discardedBytes.reset();value.status.hashFailedBytes.reset();
    if(actual.badPrefix){value.badPayloadBytes=detail::ManagedBadPayloadBytes{actual.badPrefix->discarded,actual.badPrefix->hashFailed};}
    if(actual.bad){
      value.status.discardedBytes=actual.bad->discarded;value.status.hashFailedBytes=actual.bad->hashFailed;
    }
    if(!value.probeScope){value.probeScope=actual.combinedReceived?core::QueueProbeScope::NativeCombined:core::QueueProbeScope::NativePayload;}
    value.status.downloadRate = sample.download_payload_rate;
    value.status.uploadRate = sample.upload_payload_rate;
    value.status.pieces = sample.num_pieces;
    value.remainingBytes = std::max<std::int64_t>(0, sample.total_wanted - sample.total_wanted_done);
    const auto now = Clock::now();
    value.queueObservedMillis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if(!value.stop&&!value.status.failure){
      if(const auto mapped=value.storage.mappedFiles()){
        // Same accepted target-status pipeline, matching retained descriptor
        // owner. A later completion race is unavailable until a future sample.
        const auto latency=mapped->diskLatency().snapshot(*value.queueObservedMillis);
        value.status.diskReadLatencyMillis=latency.readMillis;
        value.status.diskWriteLatencyMillis=latency.writeMillis;
      }else if(value.mmapLatency&&value.mmapLatency->admission()==value.status.admission){
        const auto latency=value.mmapLatency->snapshot(*value.queueObservedMillis);
        value.status.diskReadLatencyMillis=latency.readMillis;
        value.status.diskWriteLatencyMillis=latency.writeMillis;
      }
    }
    const bool transferring = !(sample.flags & lt::torrent_flags::paused)
        && sample.state != lt::torrent_status::checking_files
        && sample.state != lt::torrent_status::checking_resume_data && !value.stop;
    if(value.queueObservations){value.queueSource=value.queueObservations->facts(*value.queueObservedMillis);}
    if(queuePolicy&&transferring&&!value.status.failure&&value.sendSequence!=std::numeric_limits<std::uint64_t>::max()){
      try {
        if(!value.sendSmoothing){value.sendSmoothing=std::make_unique<core::QueueSendRateSmoothing>();value.sendFirstMillis=*value.queueObservedMillis;}
        const auto ticks=(*value.queueObservedMillis-*value.sendFirstMillis)/1000;
        if(ticks<=std::numeric_limits<std::int32_t>::max()){
          value.smoothedUpload=value.sendSmoothing->observe({{value.status.admission,++value.sendSequence,*value.queueObservedMillis},
              core::QueueRateScope::TargetPayload,value.status.sent},static_cast<std::int32_t>(ticks),targetSmoothingSeconds);
        }
      }catch(...){value.sendSmoothing.reset();value.sendFirstMillis.reset();}
    }else{value.sendSmoothing.reset();value.sendFirstMillis.reset();}
    if(!value.stop&&!value.status.failure){
      if(value.queueSource.durations||value.queueSource.swarm){value.status.queueSourceFacts=value.queueSource;}
      value.status.queueSmoothedPayloadUploadRate=value.smoothedUpload;
    }
    // Reuse this SAME accepted payload status. Optional target statistics may
    // neither issue a status query nor fail transfer/accounting/retirement.
    if(queuePolicy&&queuePolicy->downloadOrder==core::QueueDownloadOrder::Eta&&transferring
        &&!sample.is_finished&&sample.total_wanted>=0&&sample.total_wanted_done>=0
        &&sample.total_wanted_done<=sample.total_wanted
        &&value.targetStatisticsSequence!=std::numeric_limits<std::uint64_t>::max()){
      try{
        if(!value.targetEta){value.targetEta=std::make_unique<core::QueueTargetEta>();}
        const auto observed=value.targetEta->observe({value.status.admission,++value.targetStatisticsSequence,
            *value.queueObservedMillis,value.status.received,value.remainingBytes,{},targetSmoothingSeconds});
        if(observed){value.status.queueTargetStatistics=*observed;}
        else{value.targetEta.reset();}
      }catch(...){value.targetEta.reset();}
    }else{value.targetEta.reset();}
    if (transferring) {
      if (!value.runningSince) { value.runningSince = now; value.trackerRunningSince = lt::clock_type::now(); }
      if (sample.is_finished && !value.seedingSince) { value.seedingSince = now; value.trackerSeedingSince = lt::clock_type::now(); }
    } else { value.runningSince.reset(); value.seedingSince.reset(); value.trackerRunningSince.reset(); value.trackerSeedingSince.reset(); }
    if (!sample.is_finished) { value.seedingSince.reset(); value.trackerSeedingSince.reset(); }
    // LT active_duration includes verification time. Original counters track
    // peer-manager download/seeding time, so use actual state/pause alert
    // timestamps retained by this owner's sole pump instead.
    const auto wallNow=lt::clock_type::now();
    if(sample.last_download.time_since_epoch().count()!=0){value.status.secondsSinceDownload=std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::seconds>(wallNow-sample.last_download).count());} // Original idle is wall-based; duration source stays steady.
    value.projectTimeline(wallNow);
    if(value.queueSource.durations){value.secondsDownloading=value.queueSource.durations->downloading;value.secondsSeeding=value.queueSource.durations->seeding;}
    value.idleUploadSeconds.reset();value.idleUploadKnown=false;value.idleCheckpoint.reset();
    if(value.queueSource.idleEpoch&&value.idleSequence!=std::numeric_limits<std::uint64_t>::max()){
      core::QueueIdleUploadSample idle;idle.admission=value.status.admission;idle.sequence=++value.idleSequence;
      idle.epoch=*value.queueSource.idleEpoch;
      idle.nowWallMillis=std::chrono::duration_cast<std::chrono::milliseconds>(wallNow.time_since_epoch()).count();
      idle.rawPayloadSent=sample.total_payload_upload;idle.cumulativePayloadSent=actual.sent;
      if(sample.last_upload.time_since_epoch().count()!=0){
        idle.lastPayloadWallMillis=std::chrono::duration_cast<std::chrono::milliseconds>(sample.last_upload.time_since_epoch()).count();
      }
      const auto observed=value.idleUpload.observe(idle);value.idleUploadKnown=observed.known;
      if(observed.known&&idle.epoch.stoppedWallMillis&&(sample.flags & lt::torrent_flags::paused)){
        value.idleCheckpoint=std::pair{observed.seconds,actual.sent};
      }
      if(observed.known){
        if(observed.seconds>=0){value.idleUploadSeconds=observed.seconds;}
        if(!value.stop&&!value.status.failure){value.status.queueIdleUploadSeconds=observed.seconds;}
      }
    }
    value.connectedLeechers = std::max(0, sample.num_peers - sample.num_seeds);
    const bool positionChecked=sample.state!=lt::torrent_status::checking_files
        &&sample.state!=lt::torrent_status::checking_resume_data&&sample.state!=lt::torrent_status::downloading_metadata;
    if(value.status.checked&&positionChecked&&sample.is_finished!=value.status.selectedComplete){
      if(value.positionTransition||queuePositionObservation==std::numeric_limits<std::uint64_t>::max()){
        value.positionTransitionLost=true;
      }else{value.positionTransition=sample.is_finished;value.positionTransitionOrder=++queuePositionObservation;}
      queuePositionsPending=true;queuePositionsUnavailable=true;
    }
    if (value.status.checked && sample.is_finished && !value.status.selectedComplete) {
      value.completedTransition = true;
    }
    value.status.forceStart = value.forceStart;
    value.status.manuallyPaused = value.manualPaused;

    value.status.complete = sample.is_seeding;
    value.status.checked = (!externalQueue||value.checkedAlert)&&sample.state != lt::torrent_status::checking_files
        && sample.state != lt::torrent_status::checking_resume_data
        && sample.state != lt::torrent_status::downloading_metadata;
    value.status.selectedComplete = sample.is_finished;
    value.status.checkingProgressPpm.reset();
    if(sample.state==lt::torrent_status::checking_files&&sample.progress_ppm>=0&&sample.progress_ppm<=1000000){value.status.checkingProgressPpm=sample.progress_ppm;}
    if(value.status.recheck){auto& progress=*value.status.recheck;progress.totalBytes=value.storage.originalBytes();
      progress.scannedBytes=value.storage.recheckScannedBytes();}

    // Neither startup checking nor an already-complete saved/zero-size row is
    // a new completion event. Require two actual accepted checked observations.
    if(value.checkedAlert&&value.status.checked&&!value.stop&&!value.status.failure
        &&value.storage.purpose()==detail::ManagedStoragePurpose::Download&&value.status.bytes>0&&value.status.totalPieces>0
        &&std::ranges::none_of(value.storage.capturedFilePolicy(),[](const auto& policy){return policy.skipped;})){
      const bool complete=sample.is_seeding;
      // First real checked-incomplete is the negative producer. An initially
      // checked complete row is NOT an event; no saved/startup reconstruction.
      const bool event=(!complete&&!value.historyPreviousComplete)||(value.historyPreviousComplete&&*value.historyPreviousComplete!=complete);
      if(event){
        if(!complete){value.historySawIncomplete=true;}value.historyEventComplete=complete;
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(now>0){value.historyCompletionMillis=now;value.historyCompletionProblem.reset();}
        else{value.historyCompletionMillis.reset();value.historyCompletionProblem=storage::ErrorCode::InvalidFormat;}
      }
      value.historyPreviousComplete=complete;
    }

    value.seedPiecesExact=needPieces&&sample.pieces.size()==value.status.totalPieces;
    if (needPieces) {
      value.verified.assign(static_cast<std::size_t>(value.status.totalPieces), 0);
      for (int i = 0; i < sample.pieces.size() && i < value.status.totalPieces; ++i) {
        value.verified[static_cast<std::size_t>(i)] = sample.pieces[lt::piece_index_t{i}] ? 1 : 0;
      }
    }
    if(value.status.recheck){sampleManagedRecheckCounts(value);
      // Local checksum/missing outcomes are not peer bad-payload traffic.
      value.badPayloadBytes.reset();value.status.discardedBytes.reset();value.status.hashFailedBytes.reset();
    }
    if(priorityObservation&&value.status.checked&&sample.pieces.size()==value.status.totalPieces) {
      const auto priority=value.storage.filePriorityStatistics(value.verified);
      if(priority) {value.filePriority=*priority;value.status.queueFilePriority=*priority;} // Missing/malformed observation grants no invented stats.
    }
    if(originalProgress) {
      const auto projected=value.storage.verifiedOriginalBytes(value.verified);
      if(sample.pieces.size()!=value.status.totalPieces||!projected) {
        value.status.failure=storage::Error{storage::ErrorCode::Io,"Actual original verified progress is unavailable; state requires recheck"};
        requestStop(value);value.status.phase=ManagedPhase::Failed;return;
      }
      value.status.completed=*projected;
    }
    if (value.status.failure) { value.status.phase = ManagedPhase::Failed; }
    else if (value.stop) { value.status.phase = ManagedPhase::Stopping; }
    else if (value.manualPaused) { value.status.phase = ManagedPhase::Paused; }
    else if (sample.state == lt::torrent_status::checking_files
        || sample.state == lt::torrent_status::checking_resume_data) { value.status.phase = ManagedPhase::Checking; }
    else if (sample.flags & lt::torrent_flags::paused) { value.status.phase = ManagedPhase::Queued; }
    else { value.status.phase = sample.is_finished ? ManagedPhase::Seeding : ManagedPhase::Downloading; }
  }
  void requestStop(Managed& value) {
    if(value.mixedLifetime){value.mixedLifetime->attached=false;value.mixedLifetime->retained=false;}
    externalSnapshotValid=false;
    retireMainlinePort(value.mainlinePort);
    if(value.allocationIntent&&value.status.retained){value.stop=true;value.status.phase=ManagedPhase::Stopping;return;}
    if(value.mmapLatency){value.mmapLatency->retire();}
    value.queueSource={};value.smoothedUpload.reset();value.sendSmoothing.reset();value.sendFirstMillis.reset();
    value.status.queueSourceFacts.reset();value.status.queueSmoothedPayloadUploadRate.reset();value.status.queueIdleUploadSeconds.reset();value.idleUploadSeconds.reset();value.idleUploadKnown=false;
    value.status.queueTargetStatistics.reset(); value.targetEta.reset();
    value.status.diskReadLatencyMillis.reset();value.status.diskWriteLatencyMillis.reset();
    value.status.staleRankOffset.reset();
    if(value.status.verificationHeld){value.status.verificationHeld=false;value.seedAttempted=true;
      value.status.verifiedSeeding=value.status.failure?VerifiedSeedingPhase::Failed:VerifiedSeedingPhase::Cancelled;}
    if (value.stop || !value.handle.is_valid()) { return; }
    value.stop = true;
    value.status.phase = ManagedPhase::Stopping;
    value.deadline = Clock::now() + CertificationBudget;
    value.handle.unset_flags(lt::torrent_flags::auto_managed);
    value.handle.pause();
    value.handle.save_resume_data(lt::torrent_handle::flush_disk_cache);
  }
  void normalizeManagedPositions() noexcept {
    if(externalQueue||queuePositionsBlocked||!queuePositionsPending||!queuePolicy||!managedLease||managed.empty()){return;}
    for(const auto& [key,value]:managed){(void)key;if(value->allocationIntent&&value->status.retained){return;}}
    // Deferred checking is not an unknown=complete guess and performs no IO.
    for(const auto& [key,value]:managed){
      static_cast<void>(key);
      if(value->status.verificationHeld||!value->handle.is_valid()||!value->status.retained||!value->status.checked
          ||value->stop||value->removing||value->status.failure){return;}
    }
    queuePositionsPending=false; // One attempt per actual admission/transition.
    bool saveAttempted=false;
    try{
      std::vector<storage::LibraryPositionObservation> observations;
      std::vector<Managed*> owners;owners.reserve(managed.size());
      std::vector<std::pair<std::uint64_t,storage::LibraryGroupTransition>> ordered;
      observations.reserve(managed.size());ordered.reserve(managed.size());
      for(const auto& [key,value]:managed){
        static_cast<void>(key);
        if(value->positionTransitionLost){throw detail::Failure{{storage::ErrorCode::Unsupported,"Original group transition order was lost; reopen to normalize from saved positions"}};}
        const auto policy=value->storage.capturedFilePolicy();
        observations.push_back({value->status.hash,value->storage.identity(),value->storage.capturedDownload(),
            std::vector<compat::FilePolicy>(policy.begin(),policy.end()),value->status.selectedComplete,
            value->storage.capturedQueueMappingScope()});owners.push_back(value.get());
        if(value->positionTransition){ordered.push_back({value->positionTransitionOrder,{value->status.hash,*value->positionTransition}});}
      }
      std::ranges::sort(ordered,[](const auto& a,const auto& b){return a.first<b.first;});
      std::vector<storage::LibraryGroupTransition> transitions;transitions.reserve(ordered.size());
      for(const auto& row:ordered){transitions.push_back(row.second);}
      std::expected<storage::LibraryPositionNormalization,storage::Error> normalized;
      const auto publication=publishPositions([&]()->std::expected<void,storage::Error>{
        normalized=storage::normalizeLibraryPositions(*managedLease,observations,transitions,queuePolicy->newSeedsMoveTop,&saveAttempted);
        return normalized?std::expected<void,storage::Error>{}:std::unexpected(normalized.error());
      });
      if(!publication){throw detail::Failure{publication.error()};}
      if(!normalized){throw detail::Failure{normalized.error()};}
      // All allocations and the actual whole-inventory CAS finished before any
      // queue rank sees these committed source positions.
      for(std::size_t i=0;i<owners.size();++i){
        owners[i]->queuePosition=normalized->positions[i].position;
        owners[i]->status.queuePosition=normalized->positions[i].position;
      }
      for(auto& [key,value]:managed){static_cast<void>(key);value->positionTransition.reset();}
      queuePositionsUnavailable=false;queuePositionFailure.reset();
    }catch(const detail::Failure& failure){
      queuePositionsBlocked=saveAttempted;queuePositionsUnavailable=true;
      try{queuePositionFailure=std::make_shared<const storage::Error>(failure.error);}catch(...){queuePositionFailure.reset();}
    }catch(...){queuePositionsBlocked=saveAttempted;queuePositionsUnavailable=true;queuePositionFailure.reset();}
  }
  ExternalQueueSnapshot collectQueue(bool positionProof=false) {
    std::vector<core::QueueTorrentFacts> facts;
    facts.reserve(managed.size());
    std::map<std::uint64_t, Managed*> targets;
    const auto now = Clock::now();
    const auto millis = [](Clock::duration duration) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    core::QueueContext context;
    context.nowMillis = millis(now.time_since_epoch());
    context.ownerStartedMillis = origin / 1000000;
    context.uploadLimitBytesPerSecond = rateLimits.upload.bytesPerSecond;
    context.downloadLimitBytesPerSecond = rateLimits.download.bytesPerSecond;
    std::int64_t upload = 0, download = 0;
    bool any = false, onlySeeds = true;
    for (const auto& [key, entry] : managed) {
      static_cast<void>(key);
      const auto& value = *entry;
      if (!value.handle.is_valid() || value.removing || value.stop || value.status.failure || value.status.verificationHeld || value.status.recheck) { continue; }
      const auto& saved = value.storage.queueMetadata();
      core::QueueTorrentFacts item;
      item.token = value.status.admission;
      item.position = value.queuePosition.value_or(static_cast<std::uint32_t>(std::clamp<std::int64_t>(saved.position, 1,
          std::numeric_limits<std::int32_t>::max())));
      switch (value.status.phase) {
        case ManagedPhase::Checking: item.state = core::QueueTorrentState::Checking; break;
        case ManagedPhase::Downloading: item.state = core::QueueTorrentState::Downloading; break;
        case ManagedPhase::Seeding: item.state = core::QueueTorrentState::Seeding; break;
        case ManagedPhase::Paused: item.state = core::QueueTorrentState::Stopped; break;
        default: item.state = core::QueueTorrentState::Queued; break;
      }
      item.complete = value.status.selectedComplete;
      item.forceStart = value.forceStart; item.manuallyPaused = value.manualPaused;
      item.newlyCompleted = value.completedTransition && !value.completionHandled;
      item.minimumShareRatio = saved.minimumShareRatio;
      item.maximumShareRatio = saved.maximumShareRatio;
      // Original share ratio uses good received data. Both saved and actual
      // admission counters are checked before addition; never wrap a rank input.
      if (value.badPayloadBytes&&value.status.discardedBytes&&value.status.hashFailedBytes&&!saved.badPayloadCountersLost
          &&value.status.received <= std::numeric_limits<std::int64_t>::max() - saved.downloaded
          &&value.status.sent <= std::numeric_limits<std::int64_t>::max() - saved.uploaded
          &&value.badPayloadBytes->discarded<=std::numeric_limits<std::int64_t>::max()-saved.discarded
          &&value.badPayloadBytes->hashFailed<=std::numeric_limits<std::int64_t>::max()-saved.hashFailed) {
        item.shareRatio=core::sourceQueueShareRatio(saved.downloaded+value.status.received,
            saved.uploaded+value.status.sent,saved.discarded+value.badPayloadBytes->discarded,
            saved.hashFailed+value.badPayloadBytes->hashFailed);
      }
      if(const auto durations=value.queueSource.durations){
        const auto downloaded=core::sourceQueueSum(saved.secondsDownloading,durations->downloading);
        const auto seeded=core::sourceQueueSum(saved.secondsSeeding,durations->seeding);
        item.secondsDownloading=downloaded;item.secondsSeeding=seeded;
      }
      item.secondsSinceLastUpload = value.idleUploadSeconds;item.idleUploadSourceKnown=value.idleUploadKnown;
      item.runningMillis=value.queueSource.runningMillis;
      item.seedingMillis=value.queueSource.seedingMillis;
      if(const auto source=value.queueSource.swarm){item.swarm=core::QueueSwarm{source->seeds,source->peers,source->success,source->startupSuccess};}
      item.smoothedUploadBytesPerSecond=value.smoothedUpload;
      item.connectedLeechers = value.connectedLeechers;
      item.downloadBytesPerSecond = value.status.downloadRate;
      item.uploadBytesPerSecond = value.status.uploadRate;
      item.sizeBytes = value.status.bytes; item.remainingBytes = value.remainingBytes;
      if (value.status.downloadRate > 0) { item.etaSeconds = value.remainingBytes / value.status.downloadRate; }
      item.activity = value.activity;
      item.diskReadLatencyMillis=value.status.diskReadLatencyMillis;
      item.diskWriteLatencyMillis=value.status.diskWriteLatencyMillis;
      item.sourceStaleTiming=true;
      if(value.filePriority) {item.filePriority.emplace(value.filePriority->begin(),value.filePriority->end());}
      upload += value.status.uploadRate; download += value.status.downloadRate;
      any = true; onlySeeds = onlySeeds && item.complete;
      targets.emplace(item.token, entry.get());
      facts.push_back(std::move(item));
    }
    context.uploadBytesPerSecond = upload; context.downloadBytesPerSecond = download;
    context.seedingOnly = any && onlySeeds && !magnetActive();
    if(!counterFailure&&queueGlobalObservedMillis&&context.nowMillis>=*queueGlobalObservedMillis
        &&context.nowMillis-*queueGlobalObservedMillis<=core::QueueGlobalReceiveAverage::MaximumGapMillis){
      context.smoothedUploadBytesPerSecond=queueGlobalSendRate;
    }
    core::QueueProbeConfig probeConfig;
    probeConfig.enabled=queuePolicy->downloadOrder==core::QueueDownloadOrder::Speed
        ||queuePolicy->downloadOrder==core::QueueDownloadOrder::Eta;
    probeConfig.testSeconds=queuePolicy->downloadProbeSeconds;
    probeConfig.retestMinutes=queuePolicy->downloadProbeRetestMinutes;
    probeConfig.testActive=queuePolicy->downloadProbeActive;
    if(probeConfig.enabled){
      std::vector<core::QueueProbeInput> probeInputs;probeInputs.reserve(facts.size());
      for(const auto& fact:facts){
        const auto& value=*targets.at(fact.token);
        core::QueueProbeInput input;input.token=fact.token;input.position=fact.position;
        if(!fact.complete&&!fact.forceStart&&!fact.manuallyPaused&&fact.controllable&&!fact.moving){
          if(fact.state==core::QueueTorrentState::Queued){input.state=core::QueueProbeState::Queued;}
          else if(fact.state==core::QueueTorrentState::Downloading){input.state=core::QueueProbeState::Downloading;}
        }
        input.scope=value.probeScope.value_or(core::QueueProbeScope::NativePayload);
        if(value.queueObservedMillis){
          if(input.scope==core::QueueProbeScope::NativeCombined){
            if(value.status.combinedReceived){input.receivedBytes=value.status.combinedReceived;input.observedMillis=value.queueObservedMillis;}
            else if(input.state!=core::QueueProbeState::Other){context.downloadProbeUnavailable=true;}
          }else{input.receivedBytes=value.status.received;input.observedMillis=value.queueObservedMillis;}
        }
        // Independent configured payload smoothing; the old instant-rate ETA
        // is deliberately never used as a substitute. Native selected bytes
        // are an explicit counterpart to original peer/disk max-remaining.
        if(value.status.queueTargetStatistics){
          const auto& observed=*value.status.queueTargetStatistics;
          if(observed.admission==fact.token&&value.queueObservedMillis
              &&observed.observedMillis==*value.queueObservedMillis
              &&observed.etaSeconds&&*observed.etaSeconds>=0){input.smoothedEtaSeconds=observed.etaSeconds;}
        }
        probeInputs.push_back(input);
      }
      const auto limit=context.downloadLimitBytesPerSecond;
      core::QueueProbeContext probeContext{context.nowMillis,context.ownerStartedMillis,
          limit/1024+(limit%1024!=0),{}};
      // Retain the source1500ms/ten-slot arithmetic over actual owner observations.
      // Variable native delivery is not original timer/global-scope parity.
      // Missing, failed, future or stale input leaves the capped gate unavailable.
      if (!counterFailure && queueGlobalReceiveKiB && queueGlobalObservedMillis
          && context.nowMillis >= *queueGlobalObservedMillis
          && context.nowMillis - *queueGlobalObservedMillis <= core::QueueGlobalReceiveAverage::MaximumGapMillis) {
        probeContext.averagedDownloadKiB = *queueGlobalReceiveKiB;
      }
      const auto probes=downloadProbes.observe(probeConfig,probeInputs,probeContext);
      if(probes){
        context.downloadProbeUnavailable=context.downloadProbeUnavailable||probes->unavailable;
        for(std::size_t i=0;i<facts.size();++i){facts[i].downloadProbe=probes->observations[i];}
      }else{downloadProbes.cancel();context.downloadProbeUnavailable=true;}
    }else{downloadProbes.cancel();}
    ExternalQueueSnapshot snapshot;snapshot.context=context;snapshot.rows.reserve(facts.size());
    for(auto& fact:facts){const auto& value=*targets.at(fact.token);
      ExternalQueueRow row;row.binding={value.status.hash,value.status.admission};row.facts=std::move(fact);row.checked=value.status.checked;
      if(positionProof){const auto policy=value.storage.capturedFilePolicy();
        row.position={value.status.hash,value.storage.identity(),value.storage.capturedDownload(),
            std::vector<compat::FilePolicy>(policy.begin(),policy.end()),value.status.selectedComplete,value.storage.capturedQueueMappingScope()};
        row.transitionOrder=value.positionTransitionOrder;
        if(value.positionTransition&&!value.positionTransitionLost){row.transition=storage::LibraryGroupTransition{value.status.hash,*value.positionTransition};}
      }snapshot.rows.push_back(std::move(row));
    }return snapshot;
  }
  void applyQueue() {
    if(externalQueue){return;}
    normalizeManagedPositions();
    auto snapshot=collectQueue();std::vector<core::QueueTorrentFacts> facts;facts.reserve(snapshot.rows.size());
    std::map<std::uint64_t,Managed*> targets;
    for(auto& row:snapshot.rows){targets.emplace(row.binding.admission,managed.at(row.binding.hash.hex()).get());facts.push_back(std::move(row.facts));}
    auto plan = core::evaluateQueue(*queuePolicy, facts, snapshot.context);
    if (!plan) {
      for (auto& [key, value] : managed) {
        static_cast<void>(key); value->status.unavailableQueueInputs |= 0x80000000U;
      }
      return; // No partial effects from a rejected plan.
    }
    // Release slots before starting replacements. Every effect remains bound
    // to the same retained handle/admission, not to a row or stale UI index.
    for (const auto action : {core::QueueAction::Queue, core::QueueAction::Run, core::QueueAction::Preserve}) {
      for (const auto& decision : plan->decisions) {
        if (decision.action != action) { continue; }
        const auto found = targets.find(decision.token);
        if (found == targets.end()) { continue; }
        auto& value = *found->second;
        if (!value.handle.is_valid() || value.stop || value.removing || value.status.failure || value.status.verificationHeld || value.status.recheck) { continue; }
        value.activity = decision.activity;
        value.status.staleRankOffset=decision.staleRankOffset;
        value.status.queueReason = decision.reason;
        value.status.seedingRank = decision.seedingRank;
        value.status.firstPriority = decision.firstPriority;
        value.status.unavailableQueueInputs = plan->unavailableInputs;
        if(queuePositionsUnavailable){value.status.unavailableQueueInputs|=1U<<5;}
        value.status.queuePositionFailure=queuePositionFailure;
        if (decision.clearForceStart && value.forceStart && managedLease) {
          const auto saved=publishTarget(value.status.hash,[&]()->std::expected<void,storage::Error>{
            auto result=storage::clearLibraryForceStart(*managedLease,value.status.hash);
            return result?std::expected<void,storage::Error>{}:std::unexpected(result.error());
          });
          if (!saved) { value.status.unavailableQueueInputs |= 0x40000000U; continue; }
          value.forceStart = false; value.status.forceStart = false;
        }
        if (decision.moveNewSeedToTop&&queuePositionsUnavailable) {
          value.status.unavailableQueueInputs |= 1U << 5;
        }
        if (value.completedTransition) { value.completionHandled = true; }
        if (action == core::QueueAction::Preserve || value.manualPaused || !value.status.checked) { continue; }
        value.handle.unset_flags(lt::torrent_flags::auto_managed | lt::torrent_flags::stop_when_ready);
        if (action == core::QueueAction::Queue) {
          if (!value.policyHeld) { value.handle.pause(); value.policyHeld = true; }
        } else {
          value.handle.resume(); value.policyHeld = false;
        }
      }
    }
  }

  void advanceVerifiedSeed(Managed& value) {
    if(!value.status.verificationHeld||value.seedAttempted||value.stop||value.removing){return;}
    const auto fail=[&](storage::Error error){value.status.failure=std::move(error);requestStop(value);value.status.phase=ManagedPhase::Failed;};
    if(closingAdmission||poisoned||counterFailure||storageError||transferError||removalLost||!managedLease||!value.handle.is_valid()){
      fail({storage::ErrorCode::Cancelled,"Verified seeding was interrupted; explicit retry required"});return;
    }
    if(Clock::now()>=value.seedDeadline){fail({storage::ErrorCode::Cancelled,"Existing-data verification deadline expired; explicit retry required"});return;}
    if(!value.status.checked){return;}
    // Every bit comes from sampleManaged's SAME accepted status/counter epoch;
    // seeding alone, saved completion, file size and progress counts never certify.
    const bool selected=value.seedIntent&&value.seedIntent->goal==core::ExistingVerificationGoal::SelectedFiles;
    const bool fullProof=value.status.complete&&value.seedPiecesExact&&value.verified.size()==static_cast<std::size_t>(value.status.totalPieces)
        &&!value.verified.empty()&&std::ranges::all_of(value.verified,[](auto bit){return bit==1;});
    const bool selectedProof=selected&&value.seedPiecesExact&&value.verified.size()==static_cast<std::size_t>(value.status.totalPieces)
        &&core::satisfiesExistingPieceGoal(value.storage.existingVerificationPieces(),value.verified);
    if(selected?!selectedProof:!fullProof){
      fail({storage::ErrorCode::Conflict,selected?"Selected existing pieces did not verify; no download or repair was started":"Existing data did not verify completely; no download or repair was started"});return;
    }
    value.seedAttempted=true; // No poll replay, including a failed/uncertain CAS.
    const auto fresh=value.storage.validateExistingSnapshot();
    if(!fresh){fail(fresh.error());return;}
    const auto currentState=storage::validateVerifiedSeedState(*managedLease,value.status.hash,
        value.storage.identity(),value.storage.capturedQueueMappingScope());
    if(!currentState){fail(currentState.error());return;}
    const auto committed=storage::publishVerifiedSeed(*managedLease,*value.seedIntent);
    if(!committed){fail(committed.error());return;}
    const auto stillFresh=value.storage.validateExistingSnapshot();
    if(!stillFresh){fail(stillFresh.error());return;}
    // Restore only sources already filtered by THIS admission's immutable policy.
    // upload_mode and manual management remain: this RO admission cannot repair.
    std::vector<lt::announce_entry> trackers;
    for(std::size_t n=0;n<value.seedNetwork.trackers.size();++n){
      lt::announce_entry entry(value.seedNetwork.trackers[n]);
      if(n<value.seedNetwork.tracker_tiers.size()){entry.tier=value.seedNetwork.tracker_tiers[n];}
      trackers.push_back(std::move(entry));
    }
    value.handle.replace_trackers(trackers);
    for(const auto& url:value.seedNetwork.url_seeds){value.handle.add_url_seed(url);}
    for(const auto& url:value.seedNetwork.http_seeds){value.handle.add_http_seed(url);}
    constexpr auto discovery=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
    value.handle.set_flags(value.seedNetwork.flags&discovery,discovery);
    value.handle.unset_flags(lt::torrent_flags::stop_when_ready|lt::torrent_flags::auto_managed);
    value.handle.set_flags(lt::torrent_flags::upload_mode);
    value.status.verificationHeld=false;value.status.verifiedSeeding=VerifiedSeedingPhase::Released;
    queuePositionsPending=true;queuePositionsUnavailable=true;
    if(value.manualPaused){value.handle.pause();value.policyHeld=true;}
    else if(!queuePolicy){value.handle.resume();} // Otherwise the existing queue owns admission.
  }

  #include "managed_allocation_owner.inc"

  #include "managed_recheck_owner.inc"

  void applyChecks() {
    if(!checkPolicy||externalQueue||closingAdmission||poisoned){return;}
    std::vector<detail::CheckRequest> requests;
    for(auto& [key,entry]:managed){
      static_cast<void>(key);auto& value=*entry;
      if(value.checkPermit&&((value.checkedAlert&&value.status.checked&&!value.stop)||!value.status.retained
          ||(value.manualPaused&&value.checkPauseDrained&&!value.stop))){value.checkPermit=false;}
      const bool pending=value.handle.is_valid()&&value.status.retained&&!value.status.checked&&!value.allocationIntent;
      const bool eligible=pending&&!value.stop&&!value.status.failure&&!value.manualPaused;
      if(!eligible&&!value.checkPermit){continue;}
      detail::CheckRequest request;request.admission=value.status.admission;request.retainedPermit=value.checkPermit;request.eligible=eligible;
      const auto progress=value.status.checkingProgressPpm.value_or(0);
      request.remainingBytes=value.status.bytes-(value.status.bytes/1000000*progress+(value.status.bytes%1000000)*progress/1000000);
      for(const auto& claim:value.storage.claims()){if(claim.inode){request.filesystems.push_back(claim.device);}}
      std::ranges::sort(request.filesystems);request.filesystems.erase(std::unique(request.filesystems.begin(),request.filesystems.end()),request.filesystems.end());
      requests.push_back(std::move(request));
    }
    const auto permitted=detail::scheduleChecks(*checkPolicy,requests);
    for(auto& [key,entry]:managed){
      static_cast<void>(key);auto& value=*entry;
      if(!value.handle.is_valid()||!value.status.retained||value.status.checked||value.allocationIntent||value.stop||value.status.failure||value.manualPaused){continue;}
      if(permitted.contains(value.status.admission)){
        const bool resume=!value.checkPermit||value.checkingHeld;
        value.checkPermit=true;value.checkingHeld=false;
        if(resume){value.checkPauseDrained=false;value.handle.resume();}
      }else if(!value.checkingHeld){value.checkingHeld=true;value.checkPauseDrained=false;value.handle.pause();}
    }
  }
  void resumeAfterCheckGate(Managed& value) {
    if(checkPolicy&&!externalQueue&&!value.status.checked){applyChecks();if(!value.checkPermit){return;}}
    value.checkPauseDrained=false;value.handle.resume();
  }

  void advanceManaged() {
    for (auto& [key, entry] : managed) {
      static_cast<void>(key);
      auto& value = *entry;
      if(value.categoryRebaseFailed){value.categoryRebaseFailed=false;requestStop(value);}
      if(value.trackerResponse){static_cast<void>(value.trackerResponse->drain());}
      if(value.allocationIntent){advanceAllocation(value);continue;}
      sampleManaged(value);
      advanceManagedRecheck(value);
      try{advanceVerifiedSeed(value);}
      catch(...){value.status.failure=storage::Error{storage::ErrorCode::Io,"Verified-seeding release did not finish; explicit recovery required"};requestStop(value);value.status.phase=ManagedPhase::Failed;}
      if (!value.stop || (!value.handle.is_valid() && !value.removing)) { continue; }
      if (value.flushed && !value.removing) {
        sampleManaged(value, true);
        session->remove_torrent(value.handle); // Retain token/storage until exact weak expiration.
        value.removing = true;
      }
      if (value.removing && value.removed && !value.handle.is_valid()) {
        // Actual weak expiry is the last callback fence: drain final terminal
        // updates before the admission may disappear, retain library response.
        if(value.trackerResponse){static_cast<void>(value.trackerResponse->drain());value.trackerResponse->cancelRequests();} // Actual weak-expiry: no later torrent callback can own this pending regular request.
        if(value.timelineLost && !value.status.failure) {
          value.status.failure=storage::Error{storage::ErrorCode::Io,"Original active-time observations were lost; saved state requires recheck"};
        }
        if (!value.status.failure && managedLease) {
          value.projectTimeline(lt::clock_type::now());
          // No clock-domain translation or guessed end stamp: the actual
          // stopped callback has settled the monotone source epoch already.
          if(value.queueObservations){if(const auto times=value.queueObservations->settledDurations()){
            value.secondsDownloading=times->downloading;value.secondsSeeding=times->seeding;}}
          if(value.status.recheck){
            auto& checked=*value.status.recheck;
            value.recheckCancelled=value.recheckCancelled||closingAdmission||value.recheckStop.stop_requested();
            if(value.recheckChecked&&!value.recheckCancelled){
              auto saved=publishTarget(value.status.hash,[&](){return value.storage.checkpointRecheck(*managedLease,value.verified,value.status.complete);});
              if(!saved){value.status.failure=saved.error();checked.saveUncertain=value.storage.recheckSaveAttempted();}else{checked.pieceMapSaved=true;checked.saveUncertain=false;}
            }
          }else{
          auto saved = publishTarget(value.status.hash,[&](){return value.storage.checkpoint(*managedLease, value.verified, value.status.complete,
              value.status.received, value.status.sent, value.secondsDownloading, value.secondsSeeding,
              value.idleCheckpoint&&value.idleCheckpoint->second==value.status.sent
                  ?std::optional<std::int32_t>{value.idleCheckpoint->first}:std::nullopt,value.badPayloadBytes,
              value.status.discardedBytes.has_value()&&value.status.hashFailedBytes.has_value(),
              value.historyCompletionMillis?std::optional<detail::ManagedHistoryCompletionEvent>{detail::ManagedHistoryCompletionEvent{*value.historyCompletionMillis,value.historyEventComplete,value.historySawIncomplete}}:std::nullopt,value.checkedAlert&&value.status.checked&&value.storage.purpose()==detail::ManagedStoragePurpose::Download&&network&&network->mode==NetworkMode::Normal&&network->context==NetworkPolicyContext::ExistingLibraryPublic,
              !value.checkedAlert&&!value.resumeRejected&&!value.observedHashFailure&&!value.status.failure);});
          if (!saved) { value.status.failure = saved.error(); }
          else if(historyConsumerLease){
            value.historyCheckpointed=true;value.historyCheckpointEnd=Clock::now()+std::chrono::seconds(30);
            if(const auto problem=value.storage.historyCompletionProblem()){value.historyCompletionProblem=*problem;}
            if(value.historyCompletionMillis&&!value.storage.historyCompletionRecorded()){value.historyCompletionMillis.reset();}
            if(value.historyCompletionMillis||value.historyCompletionProblem){
              try{auto retained=managedLease->retain();
                if(retained){value.historyLease=std::move(*retained);}else{value.historyCompletionProblem=retained.error().code;}
              }catch(...){value.historyCompletionProblem=storage::ErrorCode::Io;}
            }
          }
          }
        }
        if(value.status.recheck){finishManagedRecheck(value);}
        value.status.retained = false;
        if(value.mmapLatency){value.mmapLatency->retire();}
        value.queueSource={};value.smoothedUpload.reset();value.sendSmoothing.reset();value.sendFirstMillis.reset();
        value.status.queueSourceFacts.reset();value.status.queueSmoothedPayloadUploadRate.reset();value.status.queueIdleUploadSeconds.reset();value.idleUploadSeconds.reset();value.idleUploadKnown=false;
        value.status.queueTargetStatistics.reset(); value.targetEta.reset();
        value.status.diskReadLatencyMillis.reset();value.status.diskWriteLatencyMillis.reset();
        value.status.staleRankOffset.reset();
        value.status.phase = value.status.failure ? ManagedPhase::Failed : ManagedPhase::Stopped;
        value.status.downloadRate = 0; value.status.uploadRate = 0;
        value.removing = false;
      } else if (Clock::now() >= value.deadline) {
        value.status.failure = storage::Error{storage::ErrorCode::Io,
            "Torrent storage retirement deadline expired; owner must join before release"};
        value.status.phase = ManagedPhase::Failed;
        removalLost = true; transferError = true;
      }
    }
    applyChecks();
    if (queuePolicy && !closingAdmission && !registered && !poisoned) { applyQueue(); }
    if (!registered && !managedActive() && !magnetActive() && !closed && !poisoned) { phase = AccountingState::Idle; }
    if (network && session && !poisoned && !externalQueue) {
      bool any = false, onlySeeds = true;
      for (const auto& [key, entry] : managed) {
        static_cast<void>(key);
        if (!entry->handle.is_valid() || entry->stop) { continue; }
        any = true; onlySeeds = onlySeeds && entry->status.selectedComplete;
      }
      const bool seedingOnly = any && onlySeeds && !magnetActive();
      if (seedingOnly != appliedSeedingOnly) {
        auto settings = makeLiveSettingsPack(*network, liveQueueLimits, seedingOnly);
        if (settings) {
          settings->set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error
              | lt::alert_category::status | lt::alert_category::storage
              | ((queuePolicy || libraryUdpRegularMode() || (network&&network->dht)) ? lt::alert_category::tracker : lt::alert_category_t{})));
          session->apply_settings(*settings);
          rateLimits = detail::readEffectiveRateLimits(*session);
          appliedSeedingOnly = seedingOnly;
        }
      }
    }
  }
  bool retireManaged(Clock::time_point deadline) noexcept {
    try {
      for (auto& [key, entry] : managed) { static_cast<void>(key); requestStop(*entry); }
      while (managedActive() && Clock::now() < deadline && !removalLost) {
        if(session){static_cast<void>(pop());}
        advanceManaged();
        if (managedActive()) { std::this_thread::sleep_for(20ms); }
      }
      if (managedActive() || removalLost) { return false; }
      for(const auto& [key,entry]:managed){(void)key;if(entry->allocationIntent&&entry->status.failure){return false;}}
      return true;
    } catch (...) { return false; }
  }
  bool magnetActive() const noexcept {
    for (const auto& [key, entry] : magnets) {
      static_cast<void>(key);
      if (entry->handle.is_valid() || (entry->removing && !entry->removed)) { return true; }
    }
    return false;
  }
  void requestMagnetRetirement(Magnet& value) {
    if(value.pendingLifetime){value.pendingLifetime->retiring.store(true,std::memory_order_release);value.pendingLifetime->authorized.store(false);}
    retireMainlinePort(value.mainlinePort);
    if (value.removing || !value.handle.is_valid()) { return; }
    value.status.phase = MagnetPhase::Retiring;
    value.deadline = Clock::now() + CertificationBudget;
    value.handle.unset_flags(lt::torrent_flags::auto_managed);
    value.handle.pause();
    session->remove_torrent(value.handle); // Registered route has no filesystem storage.
    value.removing = true;
  }
  void advanceMagnets() {
    for (auto& [key, entry] : magnets) {
      static_cast<void>(key); auto& value = *entry;
      if (!value.status.retained) { continue; }
      if(value.pendingSource&&!value.pendingLifetime->attached.load()){
        // A failed route may never reach add_torrent. Its reservation still
        // consumes the original finite metadata budget, without a handle.
        if(Clock::now()>=value.expires){
          value.pendingLifetime->retiring.store(true,std::memory_order_release);
          value.pendingLifetime->authorized.store(false);
          value.pendingLifetime->retained.store(false,std::memory_order_release);
          value.status.retained=false;value.status.phase=MagnetPhase::Failed;
          value.status.failure=storage::Error{storage::ErrorCode::Io,"Magnet metadata deadline expired before activation; recovery intent retained"};
        }
        continue;
      }
      if (!value.removing && !value.cancel && !value.shutdown && !value.status.failure) {
        try {
          const auto metadata = value.handle.torrent_file();
          if (metadata && metadata->is_valid()) {
            const auto actual = metadata->info_hashes();
            detail::require((!value.intent.requested.has_v1() || (actual.has_v1() && actual.v1 == value.intent.requested.v1))
                && (!value.intent.requested.has_v2() || (actual.has_v2() && actual.v2 == value.intent.requested.v2)),
                "Received metadata does not match every requested full hash", storage::ErrorCode::InvalidFormat);
            lt::add_torrent_params parameters;
            parameters.ti = std::make_shared<lt::torrent_info>(*metadata);
            const auto bytes = lt::write_torrent_file_buf(parameters, {});
            detail::require(!bytes.empty() && bytes.size() <= 32 * 1024 * 1024,
                "Received magnet metadata exceeds its bound", storage::ErrorCode::InvalidFormat);
            value.metadata.assign(bytes.data(), bytes.size());
            requestMagnetRetirement(value);
          } else if (Clock::now() >= value.expires) {
            value.status.failure = storage::Error{storage::ErrorCode::Io, "Magnet metadata deadline expired; recovery intent retained"};
            requestMagnetRetirement(value);
          }
        } catch (const detail::Failure& failure) { value.status.failure = failure.error; requestMagnetRetirement(value); }
        catch (...) {
          value.status.failure = storage::Error{storage::ErrorCode::Io, "Cannot retain received magnet metadata; recovery intent retained"};
          requestMagnetRetirement(value);
        }
      }
      if (value.removing && value.removed && !value.handle.is_valid()) {
        if(value.pendingSource){
          // Metadata completion cannot reuse the no-ti registration as payload.
          // Cross the executor then consume a subsequent whole alert batch.
          if(!value.expiryCrossed){(void)session->get_settings();value.expiryCrossed=true;continue;}
          value.pendingLifetime->retained.store(false,std::memory_order_release);
          value.status.retained=false;value.removing=false;
          value.status.phase=value.status.failure?MagnetPhase::Failed:value.cancel?MagnetPhase::Cancelled:!value.metadata.empty()?MagnetPhase::Retiring:MagnetPhase::Resolving;
          continue; // Host worker owns selected-row publication and fresh import.
        }
        value.status.retained = false;
        value.removing = false;
        if (value.status.failure) { value.status.phase = MagnetPhase::Failed; continue; }
        try {
          detail::require(magnetLease && network, "Magnet profile context is unavailable", storage::ErrorCode::Conflict);
          if (value.cancel) {
            detail::done(cancelPendingMagnet(*magnetLease, value.intent));
            value.status.phase = MagnetPhase::Cancelled;
          } else if (!value.metadata.empty()) {
            const auto filename = "BiglyBTC.metadata." + value.intent.hash.hex() + ".config";
            auto existing = storage::ConfigFile::load(*magnetLease, filename);
            if (existing) {
              detail::require(existing->recoverySource() == storage::RecoverySource::Primary
                  && existing->document().serialize() == value.metadata,
                  "Received metadata staging file conflicts with an earlier admission", storage::ErrorCode::Conflict);
            } else {
              if (existing.error().code != storage::ErrorCode::Missing) { throw detail::Failure{existing.error()}; }
              static_cast<void>(detail::take(storage::ConfigFile::createMissing(*magnetLease, filename, value.metadata)));
            }
            value.status.newlyImported=false;value.status.importGeneration=0;
            const auto hash = detail::take(completePendingMagnet(*magnetLease, value.intent, magnetProfile / filename, *network,&value.status.newlyImported,
                !closingAdmission&&magnetCompletionHook&&magnetCompletionLease
                    &&magnetCompletionLease->sharesOwnership(*magnetLease)?magnetCompletionHook:MagnetCompletionHook{},
                &value.status.historyWarning));
            if(value.status.newlyImported){value.status.importGeneration=identity.engineEpoch;}
            value.status.phase = MagnetPhase::Imported;
            // These numeric hints were admitted under this owner's immutable
            // bind/proxy policy. Retain them across metadata and payload handles
            // (and a later same-owner Stop/Start), without bypassing LT routing.
            importPeers.insert_or_assign(hash.hex(), value.intent.peers);
            if (!value.intent.stopped && !closingAdmission) { autoStarts.push_back({hash, 0, value.intent.forceStart, false}); }
            value.metadata.clear(); value.metadata.shrink_to_fit();
          } else { value.status.phase = MagnetPhase::Resolving; } // Normal close preserves unresolved original intent.
        } catch (const detail::Failure& failure) { value.status.newlyImported=false;value.status.importGeneration=0;value.status.failure = failure.error; value.status.phase = MagnetPhase::Failed; }
        catch (...) {
          value.status.newlyImported=false;value.status.importGeneration=0;
          value.status.failure = storage::Error{storage::ErrorCode::Io, "Magnet import failed; recovery intent retained"};
          value.status.phase = MagnetPhase::Failed;
        }
      } else if (value.removing && Clock::now() >= value.deadline) {
        value.status.failure = storage::Error{storage::ErrorCode::Io, "Magnet retirement fence was lost; recovery intent retained"};
        removalLost = true; transferError = true;
      }
    }
  }
  bool retireMagnets(Clock::time_point deadline) noexcept {
    try {
      for (auto& [key, entry] : magnets) {
        static_cast<void>(key);
        if (entry->handle.is_valid()) { entry->shutdown = true; requestMagnetRetirement(*entry); }
      }
      advanceMagnets();
      while (magnetActive() && Clock::now() < deadline && !removalLost) {
        static_cast<void>(pop()); advanceMagnets();
        if (magnetActive()) { std::this_thread::sleep_for(20ms); }
      }
      return !magnetActive() && !removalLost;
    } catch (...) { return false; }
  }
  RetirementReceipt finishRetirement(RetirementReceipt result) noexcept {
    retirement = result; // Receipt and essential counters exist BEFORE callbacks.
    deferPublication = false;
    publish();
    return result;
  }
  RetirementReceipt retire(Clock::time_point deadline) noexcept {
    deferPublication = true;
    RetirementReceipt result{operation, false, {}, {}};
    operationSink = {}; operationBaseline.reset(); // Before any cleanup/publish.
    if (!session) {
      if(!retireManaged(deadline)){result.accountingFailure=AccountingError::StorageFenceLost;joinPoisoned();return finishRetirement(result);}
      result.storageReleased = !poisoned;
      if (poisoned) { result.accountingFailure = AccountingError::OwnerResetRequired; }
      return finishRetirement(result);
    }
    phase = AccountingState::Retiring;
    try {
      // Start both retirement paths before waiting so independent disk and
      // metadata lifetimes share the close budget rather than consuming it twice.
      for (auto& [key, entry] : magnets) {
        static_cast<void>(key);
        if (entry->handle.is_valid()) { entry->shutdown = true; requestMagnetRetirement(*entry); }
      }
      if (!retireManaged(deadline) || !retireMagnets(deadline)) {
        result.accountingFailure = AccountingError::StorageFenceLost;
        joinPoisoned(); return finishRetirement(result);
      }
      session->pause();
      if (registered) {
        active.pause();
        session->remove_torrent(active); // Never delete payloads.
        while ((!removalSeen || active.is_valid()) && !removalLost && Clock::now() < deadline) {
          static_cast<void>(pop());
          if (!removalSeen || active.is_valid()) { std::this_thread::sleep_for(20ms); }
        }
        if (!removalSeen || active.is_valid() || removalLost) {
          result.accountingFailure = AccountingError::StorageFenceLost;
          joinPoisoned(); return finishRetirement(result);
        }
      }
      // Drain already queued errors after weak expiration, even if statistics
      // have been disabled. No old torrent object can now enqueue later work.
      static_cast<void>(pop());
      if (storageError || removalLost) {
        result.accountingFailure = AccountingError::StorageFenceLost;
        joinPoisoned(); return finishRetirement(result);
      }
      result.storageReleased = true;
      // SOURCE-BOUND CONDITIONAL SEAL: pinned LT2.0.12 peers can mutate these
      // four counters only by locking a torrent. All registered weak torrents
      // are expired; no new admission is allowed. This is not wire/IP/DHT or
      // metadata durability evidence. Independent source/runtime qualification
      // must bind that exact dependency before this receipt earns credit.
      const bool sampled = fresh(deadline);
      if (storageError || removalLost) {
        result.storageReleased = false;
        result.accountingFailure = AccountingError::StorageFenceLost;
        joinPoisoned(); return finishRetirement(result);
      }
      if (sampled && lastStatistics) {
        result.peerCounters = SettledPeerCounters(identity, lastStatistics->counters, statisticsSequence);
      } else { result.accountingFailure = counterFailure.value_or(AccountingError::Deadline); }
      registered = false;
      phase = AccountingState::Idle;
      return finishRetirement(result);
    } catch (...) {
      result.storageReleased = false;
      result.accountingFailure = AccountingError::StorageFenceLost;
      joinPoisoned(); return finishRetirement(result);
    }
  }
};

SettledPeerCounters::SettledPeerCounters(AccountingIdentity identity, core::EngineCounters counters,
    std::uint64_t sequence) noexcept : identity_(identity), counters_(counters), sequence_(sequence) {}
PersistentSession::PersistentSession(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
std::expected<std::unique_ptr<PersistentSession>, AccountingError> PersistentSession::create(AccountingIdentity identity) {
  if (!identity.applicationRun || !identity.profileSession || !identity.engineEpoch) {
    return std::unexpected(AccountingError::InvalidIdentity);
  }
  if (!detail::persistentRuntimeQualified()) { return std::unexpected(AccountingError::DependencyUnqualified); }
  try {
    auto impl = std::make_unique<Impl>(identity);
    if (impl->origin < 0) { return std::unexpected(AccountingError::CounterInvalid); }
    return std::unique_ptr<PersistentSession>(new PersistentSession(std::move(impl)));
  } catch (const std::bad_alloc&) { return std::unexpected(AccountingError::Memory); }
  catch (...) { return std::unexpected(AccountingError::Engine); }
}
PersistentSession::~PersistentSession() {
  // Owner-thread destruction is a lifetime precondition. No destructor signal,
  // fresh measurement, final seal or bounded join is promised by this fallback.
  impl_->magnetCompletionHook={};impl_->magnetCompletionLease.reset();
  impl_->sink = {}; impl_->operationSink = {};
  impl_->retireMainlinePorts();
  impl_->closeMainline();
  for(auto& [key,magnet]:impl_->magnets){(void)key;if(magnet->pendingLifetime){magnet->pendingLifetime->attached=false;magnet->pendingLifetime->retained=false;}}
  for(auto& [key,member]:impl_->managed){(void)key;if(member->mixedLifetime){member->mixedLifetime->attached=false;member->mixedLifetime->retained=false;}}
  if(impl_->mixedTransport){impl_->mixedTransport->retire();impl_->mixedTransport.reset();}
  impl_->session.reset();
  impl_->releaseRegularTrackerLease();
}
std::expected<void,storage::Error> PersistentSession::configureMagnetCompletionHook(
    const storage::ProfileLease& lease,MagnetCompletionHook hook){
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||impl_->closingAdmission
      ||!impl_->magnets.empty()||impl_->magnetCompletionHook||!hook){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Magnet history hook requires current owner before metadata admission"});
  }
  try{impl_->bind(lease);auto held=lease.retain();if(!held){return std::unexpected(held.error());}
    impl_->magnetCompletionLease=std::move(*held);impl_->magnetCompletionHook=std::move(hook);return {};
  }catch(const detail::Failure& e){return std::unexpected(e.error);}
  catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot retain optional magnet history owner"});}
}
void PersistentSession::clearMagnetCompletionHook()noexcept{
  if(impl_->onWorker()&&!impl_->call){impl_->magnetCompletionHook={};impl_->magnetCompletionLease.reset();}
}
std::expected<void, AccountingError> PersistentSession::startIdle(const storage::ProfileLease& lease) {
  if (!impl_->onWorker()) { return std::unexpected(AccountingError::WrongThread); }
  if (impl_->call) { return std::unexpected(AccountingError::ActiveOperation); }
  if (impl_->closed) { return std::unexpected(AccountingError::Closing); }
  if (impl_->poisoned) { return std::unexpected(AccountingError::OwnerResetRequired); }
  impl_->call = true;
  try {
    impl_->bind(lease);
    impl_->ensureSession();
    impl_->call = false;
    return {};
  } catch (const std::bad_alloc&) {
    impl_->call = false;
    return std::unexpected(AccountingError::Memory);
  } catch (...) {
    impl_->call = false;
    return std::unexpected(AccountingError::Engine);
  }
}
std::expected<void, storage::Error> PersistentSession::configureNetwork(const NetworkPolicy& policy) {
  if (!impl_->onWorker() || impl_->call || impl_->session || impl_->closed || impl_->poisoned || impl_->mainlineOriginalConfigured) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Configure networking before original identity opt-in and owner startup"});
  }
  auto settings = validateNetworkPolicy(policy);
  if (!settings) { return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,
      "Unsupported network setting: " + settings.error().field}); }
  impl_->network = policy;
  return {};
}
std::expected<void, storage::Error> PersistentSession::configureOriginalMainlineIdentity(const storage::ProfileLease& lease) {
  if(!impl_->onWorker()||impl_->call||impl_->session||impl_->closed||impl_->poisoned||impl_->mainlineOriginalConfigured||!impl_->network){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Configure original Mainline identity once after policy and before owner startup"});
  }
  impl_->mainlineOriginalConfigured=true;
  const auto& policy=*impl_->network;
  if(policy.mode!=NetworkMode::Normal||!policy.dht||policy.proxy.kind!=NetworkProxyKind::None||!policy.mainlineRestoreIdentity){return {};}
  impl_->mainlineStatus.originalIdentityRequested=true;
  const auto refused=[&](storage::Error failure)->std::expected<void,storage::Error>{
    impl_->mainlineOriginalRejected=true;impl_->mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    impl_->mainlineStatus.failure=failure;return std::unexpected(std::move(failure));
  };
  try{
#if BIGLYBTC_ORIGINAL_MLDHT_IDENTITY_READER
    auto retained=lease.retain();if(!retained){return refused(retained.error());}
    // This binds the owner to the retained SAME-OFD profile before any source
    // is installed. A later startIdle on a different profile is refused.
    impl_->mainlineOriginalLease=std::move(*retained);
    impl_->bind(*impl_->mainlineOriginalLease); // Own bind's raw reference even if bind throws.
    if(policy.mainlineOriginalBindPlan){
      auto current=impl_->originalBindSource();if(!current){return refused(current.error());}
      if(!impl_->sameOriginalBindPolicy(current->policy)){return refused({storage::ErrorCode::Conflict,"Original Mainline policy changed before bind capture"});}
      auto capture=MainlineBindCapture::capture(*impl_->mainlineOriginalLease,policy,current->options);
      if(!capture){return refused(capture.error());}
      auto numeric=projectMainlineBindPlan(policy,capture->plan());if(!numeric){return refused(numeric.error());}
      impl_->mainlineBindCapture.emplace(std::move(*capture));impl_->mainlineResolvedPolicy.emplace(std::move(*numeric));
      auto after=impl_->originalBindSource();if(!after){return refused(after.error());}
      if(!impl_->sameOriginalBindPolicy(after->policy)||after->options!=impl_->mainlineBindCapture->options()){
        return refused({storage::ErrorCode::Conflict,"Original Mainline settings changed during bind capture"});
      }
    }
    auto source=storage::OriginalMainlineIdentity::load(*impl_->mainlineOriginalLease,policy.mainlineDirectory,policy.mainlineRestoreIdentity);
    if(!source){return refused(source.error());}
    if(source->snapshot().state==storage::OriginalMainlineIdentityState::Missing){
      source=storage::OriginalMainlineIdentity::createMissing(*impl_->mainlineOriginalLease,policy.mainlineDirectory,policy.mainlineRestoreIdentity);
      if(!source){return refused(source.error());}
    }
    if(source->snapshot().state!=storage::OriginalMainlineIdentityState::Imported){
      return refused({storage::ErrorCode::Missing,"Original Mainline root is unavailable"});
    }
    impl_->mainlineOriginalSource=std::make_shared<storage::OriginalMainlineIdentity>(std::move(*source));
    return {};
#else
    (void)lease;
    return refused({storage::ErrorCode::Unsupported,"Original Mainline identity reader is not enabled in this build"});
#endif
  }catch(const detail::Failure& failure){return refused(failure.error);}
  catch(...){return refused({storage::ErrorCode::Io,"Cannot retain the original Mainline identity source"});}
}
std::expected<void, storage::Error> PersistentSession::configureMainlineBootstrap(std::vector<std::pair<std::string,int>> nodes) {
  if (!impl_->onWorker() || impl_->call || impl_->session || impl_->closed || impl_->poisoned || nodes.size()>64) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Configure bounded Mainline bootstrap before owner startup"});
  }
  impl_->mainlineBootstrap=std::move(nodes);
  return {};
}
MainlineDiscoveryStatus PersistentSession::mainlineDiscovery() const {
  if (!impl_->onWorker()) { return {MainlineDiscoveryStatus::State::Unavailable,0,0,0,storage::Error{storage::ErrorCode::Conflict,"Mainline observation requires its owner worker"}}; }
  auto result=impl_->mainlineStatus;result.queuedScrapeFailure=impl_->mainlineScrapeFailure;return result;
}
std::optional<LiveSettingsSnapshot> PersistentSession::liveSettings() const noexcept {
  if (!impl_->onWorker() || !impl_->session || impl_->closed || impl_->poisoned) { return {}; }
  try {
    const auto actual = impl_->session->get_settings();
    LiveSettingsSnapshot value;
    value.rateLimits = detail::projectEffectiveRateLimits(actual);
    value.activeDownloads = actual.get_int(lt::settings_pack::active_downloads);
    value.activeSeeds = actual.get_int(lt::settings_pack::active_seeds);
    value.activeLimit = actual.get_int(lt::settings_pack::active_limit);
    value.countSlowTorrents = !actual.get_bool(lt::settings_pack::dont_count_slow_torrents);
    const auto local = impl_->session->get_peer_class(lt::session_handle::local_peer_class_id);
    value.lanUpload = core::effectiveSessionRateLimit(local.upload_limit);
    value.lanDownload = core::effectiveSessionRateLimit(local.download_limit);
    const auto classes = impl_->session->get_peer_class_filter();
    value.separateLanClass = (classes.access(lt::make_address("127.0.0.1"))
        & (1U << static_cast<unsigned>(lt::session_handle::local_peer_class_id))) != 0;
    value.listenInterfaces = actual.get_str(lt::settings_pack::listen_interfaces);
    value.outgoingInterfaces = actual.get_str(lt::settings_pack::outgoing_interfaces);
    if (value.listenInterfaces.size() > 4096 || value.outgoingInterfaces.size() > 4096) { return {}; }
    value.proxyType = actual.get_int(lt::settings_pack::proxy_type);
    value.dht = actual.get_bool(lt::settings_pack::enable_dht);
    value.localDiscovery = actual.get_bool(lt::settings_pack::enable_lsd);
    value.upnp = actual.get_bool(lt::settings_pack::enable_upnp);
    value.natpmp = actual.get_bool(lt::settings_pack::enable_natpmp);
    return value;
  } catch (...) { return {}; }
}
std::expected<LiveSettingsResult, storage::Error> PersistentSession::applyLiveSettings(
    const NetworkPolicy& requested, std::optional<LiveQueueLimits> queues) {
  if (!impl_->onWorker() || impl_->call || impl_->registered || impl_->closed || impl_->closingAdmission || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Live settings owner is unavailable"});
  }
  auto plan = prepareLiveSettings(impl_->network.value_or(NetworkPolicy{}), requested, queues ? queues : impl_->liveQueueLimits);
  if (!plan) { return std::unexpected(plan.error()); }
  auto settings = makeLiveSettingsPack(plan->effectivePolicy, plan->queues, impl_->appliedSeedingOnly);
  if (!settings) { return std::unexpected(settings.error()); }
  if(impl_->externalQueue){
    const auto actual=impl_->externalRates.value_or(impl_->rateLimits);
    settings->set_int(lt::settings_pack::download_rate_limit,static_cast<int>(actual.download.bytesPerSecond));
    settings->set_int(lt::settings_pack::upload_rate_limit,static_cast<int>(actual.upload.bytesPerSecond));
  }
  impl_->call = true;
  struct Reset { Impl& value; ~Reset() { value.call = false; } } reset{*impl_};
  try {
    const LiveSettingsResult result{plan->restartFields, bool(impl_->session)};
    // Only the narrow pack reaches LT, and only after Host's successful save.
    // get_settings below is the same network-thread FIFO barrier after apply.
    if (impl_->session) {
      impl_->session->apply_settings(*settings);
      applyNetworkLanRates(*impl_->session, plan->effectivePolicy);
    }
    impl_->network = std::move(plan->effectivePolicy);
    impl_->liveQueueLimits = plan->queues;
    if (!impl_->session) { return result; }
    const auto actual = liveSettings();
    const auto expectedUpload = core::effectiveSessionRateLimit(settings->get_int(lt::settings_pack::upload_rate_limit));
    const auto expectedDownload = core::effectiveSessionRateLimit(settings->get_int(lt::settings_pack::download_rate_limit));
    const bool confirmed = actual && actual->rateLimits.upload == expectedUpload && actual->rateLimits.download == expectedDownload
        && actual->lanUpload == core::effectiveSessionRateLimit(networkLanRate(impl_->network->lanUploadBytesPerSecond))
        && actual->lanDownload == core::effectiveSessionRateLimit(networkLanRate(impl_->network->lanDownloadBytesPerSecond))
        && (!impl_->liveQueueLimits || (actual->activeDownloads == settings->get_int(lt::settings_pack::active_downloads)
            && actual->activeLimit == settings->get_int(lt::settings_pack::active_limit)
            && actual->activeSeeds == -1 && actual->countSlowTorrents));
    if (!confirmed) {
      impl_->rateLimits = {}; impl_->publish();
      return std::unexpected(storage::Error{storage::ErrorCode::Io,
          "Saved live settings were submitted but their actual application is unavailable; restart the owner"});
    }
    impl_->rateLimits = actual->rateLimits;
    impl_->publish(); // Optional publication cannot reenter live mutation while call is held.
    return result;
  } catch (...) {
    impl_->rateLimits = {}; impl_->publish();
    return std::unexpected(storage::Error{storage::ErrorCode::Io,
        "Live settings application failed; saved preferences remain and the owner may require restart"});
  }
}
std::expected<void, storage::Error> PersistentSession::configureQueuePolicy(const core::QueuePolicy& policy,int configuredSmoothingSeconds) {
  if (!managedAvailable() || impl_->call) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Queue policy is unavailable"});
  }
  const auto validated = core::evaluateQueue(policy, {}, {});
  if (!validated) { return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,
      "Invalid queue policy: " + validated.error().key}); }
  impl_->queuePolicy = policy;impl_->externalSnapshotValid=false;
  impl_->queuePositionsPending=true;impl_->queuePositionsUnavailable=true;
  if (policy.downloadOrder != core::QueueDownloadOrder::Eta) {
    for (auto& [key, value] : impl_->managed) {
      static_cast<void>(key); value->status.queueTargetStatistics.reset(); value->targetEta.reset();
    }
  }
  const auto smoothing=core::sourceSmoothingPolicy(configuredSmoothingSeconds).windowSeconds;
  if(impl_->targetSmoothingSeconds!=smoothing){
    impl_->queueGlobalSend={};impl_->queueGlobalSendRate.reset();
    for(auto& [key,value]:impl_->managed){static_cast<void>(key);value->sendSmoothing.reset();value->sendFirstMillis.reset();value->smoothedUpload.reset();}
  }
  impl_->targetSmoothingSeconds=smoothing;
  if (impl_->session) {
    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::tracker
        | lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage));
    impl_->session->apply_settings(settings);
    for (auto& [key, value] : impl_->managed) {
      static_cast<void>(key);
      if (value->handle.is_valid() && !value->stop) { value->handle.unset_flags(lt::torrent_flags::auto_managed); }
    }
    impl_->applyQueue();
  }
  return {};
}
std::optional<core::QueuePolicy> PersistentSession::queuePolicy() const {
  return impl_->onWorker() ? impl_->queuePolicy : std::nullopt;
}

std::expected<void,storage::Error> PersistentSession::configureExternalQueue(bool enabled){
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||(!impl_->managed.empty()&&impl_->externalQueue!=enabled)){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Configure external queue before admission"});
  }impl_->externalQueue=enabled;impl_->externalSnapshotValid=false;return {};
}
std::expected<void,storage::Error> PersistentSession::configureSourcePublication(TargetPublication target,PositionsPublication positions){
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Source publication owner unavailable"});}
  impl_->targetPublication=std::move(target);impl_->positionsPublication=std::move(positions);return {};
}
RetainedPayloads PersistentSession::retainedPayloads()const{
  RetainedPayloads out;if(!impl_->onWorker()){return out;}
  for(const auto& [key,m]:impl_->managed){(void)key;const auto claims=m->storage.claims();
    out.claims.insert(out.claims.end(),claims.begin(),claims.end());out.identities.push_back(m->storage.identity());
  }return out;
}
std::expected<ExternalQueueSnapshot,storage::Error> PersistentSession::externalQueueSnapshot(){
  if(!impl_->onWorker()||!impl_->externalQueue||!impl_->queuePolicy||impl_->call||impl_->closed||impl_->poisoned
      ||impl_->externalQueueSequence==std::numeric_limits<std::uint64_t>::max()){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"External queue snapshot unavailable"});
  }
  auto snapshot=impl_->collectQueue(true);snapshot.sequence=++impl_->externalQueueSequence;impl_->externalSnapshotValid=true;return snapshot;
}
std::expected<bool,storage::Error> PersistentSession::applyExternalQueue(std::uint64_t sequence,
    std::span<const ExternalQueueDecision> decisions,std::uint32_t unavailable,core::QueueAction phase){
  if(!impl_->onWorker()||!impl_->externalQueue||!impl_->externalSnapshotValid||!sequence||sequence!=impl_->externalQueueSequence||impl_->call||impl_->closed||impl_->poisoned
      ||impl_->transferError||impl_->storageError||impl_->removalLost||impl_->counterFailure
      ||(phase!=core::QueueAction::Queue&&phase!=core::QueueAction::Run&&phase!=core::QueueAction::Preserve)){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"External queue plan is stale"});
  }
  // Validate the entire supplied batch before any effects. Same hash/admission
  // and actual retained handle are required again at every phase.
  std::vector<Impl::Managed*> targets;targets.reserve(decisions.size());
  for(const auto& row:decisions){const auto found=impl_->managed.find(row.binding.hash.hex());
    if(found==impl_->managed.end()||!row.binding.admission||row.binding.admission!=row.decision.token
        ||found->second->status.admission!=row.binding.admission||!found->second->handle.is_valid()
        ||found->second->stop||found->second->removing||found->second->status.failure||found->second->status.verificationHeld
        ||std::ranges::find(targets,found->second.get())!=targets.end()){
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"External queue binding changed"});
    }targets.push_back(found->second.get());
  }
  bool settled=true;
  for(std::size_t i=0;i<decisions.size();++i){const auto& decision=decisions[i].decision;auto& value=*targets[i];
    if(phase!=core::QueueAction::Queue&&decision.action!=phase){continue;}
    value.activity=decision.activity;value.status.staleRankOffset=decision.staleRankOffset;
    value.status.queueReason=decision.reason;value.status.seedingRank=decision.seedingRank;
    value.status.firstPriority=decision.firstPriority;value.status.unavailableQueueInputs=unavailable;
    if(decision.clearForceStart&&value.forceStart&&impl_->managedLease){
      auto saved=impl_->publishTarget(value.status.hash,[&]()->std::expected<void,storage::Error>{
        auto r=storage::clearLibraryForceStart(*impl_->managedLease,value.status.hash);return r?std::expected<void,storage::Error>{}:std::unexpected(r.error());
      });if(!saved){return std::unexpected(saved.error());}value.forceStart=false;value.status.forceStart=false;
    }
    if(value.completedTransition){value.completionHandled=true;}
    if(decision.action!=phase){continue;}
    if(phase==core::QueueAction::Preserve){continue;}
    if(!value.checkedAlert||!value.status.checked){settled=false;continue;}
    const auto actual=value.handle.status();if(actual.errc){settled=false;continue;}const bool paused=bool(actual.flags & lt::torrent_flags::paused);
    value.handle.unset_flags(lt::torrent_flags::auto_managed);
    if(phase==core::QueueAction::Queue){value.policyHeld=true;
      if(!paused&&!value.externalPausePending){value.externalPauseSettled=false;value.externalPausePending=true;value.handle.pause();}
      settled&=paused&&value.externalPauseSettled&&!value.externalPausePending;
    }else if(!value.manualPaused&&value.policyHeld){
      if(!paused&&!value.externalPausePending){value.externalPauseSettled=false;value.externalPausePending=true;value.handle.pause();}
      if(paused&&value.externalPauseSettled&&!value.externalPausePending){
        value.handle.unset_flags(lt::torrent_flags::stop_when_ready);value.externalPauseSettled=false;value.handle.resume();value.policyHeld=false;
      }else{settled=false;}
    }else if(!value.manualPaused&&paused){
      if(value.externalPauseSettled&&!value.externalPausePending){value.handle.unset_flags(lt::torrent_flags::stop_when_ready);value.externalPauseSettled=false;value.handle.resume();}
      else{settled=false;}
    }
  }return settled;
}
std::expected<void,storage::Error> PersistentSession::applyExternalQueuePositions(
    std::span<const std::pair<QueueBinding,std::uint32_t>> rows){
  if(!impl_->onWorker()||!impl_->externalQueue||impl_->call||impl_->closed||impl_->poisoned){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"External positions unavailable"});}
  for(const auto& [binding,position]:rows){const auto i=impl_->managed.find(binding.hash.hex());
    if(!position||position>std::numeric_limits<std::int32_t>::max()||i==impl_->managed.end()||i->second->status.admission!=binding.admission){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"External position binding changed"});}
  }
  for(const auto& [binding,position]:rows){auto& m=*impl_->managed.at(binding.hash.hex());m.queuePosition=position;m.status.queuePosition=position;m.positionTransition.reset();}
  impl_->queuePositionsPending=false;impl_->queuePositionsUnavailable=false;impl_->queuePositionsBlocked=false;impl_->queuePositionFailure.reset();return {};
}
std::expected<void,storage::Error> PersistentSession::applyExternalRateLimits(const core::SessionRateLimits& limits){
  if(!impl_->onWorker()||!impl_->externalQueue||impl_->call||impl_->closed||impl_->poisoned||!validExternalRateLimits(limits)){
    return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Invalid coordinated rate caps"});
  }
  impl_->externalRates=limits;
  if(impl_->session){lt::settings_pack pack;pack.set_int(lt::settings_pack::download_rate_limit,static_cast<int>(limits.download.bytesPerSecond));
    pack.set_int(lt::settings_pack::upload_rate_limit,static_cast<int>(limits.upload.bytesPerSecond));impl_->session->apply_settings(pack);
    impl_->rateLimits=detail::readEffectiveRateLimits(*impl_->session);
    if(impl_->rateLimits.download!=limits.download||impl_->rateLimits.upload!=limits.upload){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Coordinated Public rate caps unavailable"});}
  }return {};
}

std::expected<void, storage::Error> PersistentSession::startManaged(
    const storage::ProfileLease& lease,const ManagedDownloadOptions& options,const RetainedPayloads& external){
  return startManagedImpl(lease,options,external,false,{});
}
std::expected<void, storage::Error> PersistentSession::startManagedImpl(
    const storage::ProfileLease& lease,const ManagedDownloadOptions& options,const RetainedPayloads& external,
    bool recheck,std::stop_token recheckStop) {
  if (!impl_->onWorker() || impl_->call || impl_->registered || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed admission is unavailable"});
  }
  if (impl_->queuePolicy && static_cast<std::size_t>(std::ranges::count_if(impl_->managed,
      [](const auto& row){return row.second->status.retained||row.second->removing;})) >= core::MaximumQueueTorrents) {
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Original-rule queue capacity exceeded"});
  }
  if(!core::validTorrentAllocationMode(options.allocationMode)){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Invalid managed allocation mode"});}
  if(impl_->externalQueue&&(recheck||options.verifyExisting||options.allocationMode!=core::TorrentAllocationMode::None)){
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"External queue admits ordinary downloads only"});
  }
  if(impl_->historyConsumerLease&&!impl_->historyConsumerLease->sharesOwnership(lease)){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Managed history consumer profile OFD differs"});}
  const auto key = options.hash.hex();
  if(const auto pending=impl_->magnets.find(key);pending!=impl_->magnets.end()&&pending->second->pendingSource&&pending->second->status.retained){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Retire actual metadata before payload admission"});}
  const auto previous=impl_->managed.find(key);
  if(previous!=impl_->managed.end()) {
    const auto& row=*previous->second;
    if(impl_->historyConsumerLease&&!row.historyTaken&&row.historyCheckpointed&&(row.historyCompletionMillis||row.historyCompletionProblem)){
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Consume the prior durable completion before replacing its owner"});
    }
    if(row.handle.is_valid()||row.removing||row.status.retained||row.status.phase!=ManagedPhase::Stopped
        ||!row.flushed||!row.removed||row.status.failure) {
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Torrent is already admitted or retirement is uncertified"});
    }
  }
  if(std::ranges::any_of(impl_->magnets,[](const auto& item){return item.second->pendingSource&&item.second->status.retained;})){
    auto store=storage::TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}auto state=store->load(lease,options.hash);if(!state){return std::unexpected(state.error());}
    const auto subject=storage::originalTorrentInfoSubject(state->document());if(!subject){return std::unexpected(subject.error());}
    const auto* info=state->document().find("info");const auto* version=info->find("meta version");const bool v2=version&&version->type==core::Node::Type::Integer&&version->integer==2;const bool v1=!v2||info->find("pieces");
    const auto sha1=lt::hasher(subject->data(),static_cast<int>(subject->size())).final();const auto sha256=lt::hasher256(subject->data(),static_cast<int>(subject->size())).final();
    for(const auto& [name,entry]:impl_->magnets){(void)name;if(!entry->pendingSource||!entry->status.retained){continue;}const auto& wanted=entry->intent.requested;
      if((wanted.has_v1()&&v1&&wanted.v1==sha1)||(wanted.has_v2()&&v2&&wanted.v2==sha256)){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Retire actual metadata alias before payload preparation"});}}

  }
  // Required raw counter support is admitted before any ManagedStorage
  // beginUpdate/create/truncate. Stock/old approved providers stay unsupported.
  const auto payloadBridge = detail::managedPayloadCounterBridge();
  if (!payloadBridge) {
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,
        "Managed counter/selected-swarm provider ABI is unavailable"});
  }
  impl_->call = true;impl_->externalSnapshotValid=false;
  try {
    impl_->bind(lease);
    impl_->retirement.reset();
    detail::require(!options.ownedLoopbackPeerPort || !impl_->network
        || impl_->network->mode == NetworkMode::IsolatedLoopback,
        "Owned endpoint shortcut requires isolated networking");
    detail::require(impl_->operation != std::numeric_limits<std::uint64_t>::max(), "Admission identity exhausted");
    if (!impl_->network) { impl_->network = NetworkPolicy{}; }
    std::vector<detail::ManagedPayloadClaim> claimed;
    std::vector<storage::OriginalTorrentIdentity> retainedIdentities;
    for(const auto& [identity,row]:impl_->managed) {
      (void)identity;if(previous!=impl_->managed.end()&&row.get()==previous->second.get()){continue;}
      const auto claims=row->storage.claims();detail::require(claims.size()<=detail::MaximumManagedPayloadClaims-claimed.size(),"Retained payload claim bound exceeded",storage::ErrorCode::Unsupported);
      claimed.insert(claimed.end(),claims.begin(),claims.end());
      retainedIdentities.push_back(row->storage.identity());
    }
    detail::require(external.claims.size()<=detail::MaximumManagedPayloadClaims-claimed.size(),"Cross-provider payload claim bound exceeded",storage::ErrorCode::Unsupported);
    detail::require(external.identities.size()<=core::MaximumQueueTorrents,"Cross-provider identity bound exceeded",storage::ErrorCode::Unsupported);
    claimed.insert(claimed.end(),external.claims.begin(),external.claims.end());
    retainedIdentities.insert(retainedIdentities.end(),external.identities.begin(),external.identities.end());
    const auto allocation=detail::take(storage::readAllocationIntent(lease,options.hash));
    const bool allocating=options.allocationMode!=core::TorrentAllocationMode::None;
    detail::require(!allocating||(allocation&&allocation->state!=storage::AllocationState::Complete&&allocation->mode==options.allocationMode
        &&!options.verifyExisting&&!options.forceStart&&!options.queued&&!options.manuallyPaused&&!options.ownedLoopbackPeerPort),"Allocation requires its exact stopped/unforced durable intent",storage::ErrorCode::Conflict);
    detail::require(allocating||!allocation||allocation->state==storage::AllocationState::Complete,"Pending allocation requires an explicit allocation retry",storage::ErrorCode::Conflict);
    const auto intent=detail::take(storage::readVerifiedSeedIntent(lease,options.hash));
    detail::require(!recheck||(!intent&&!allocation&&!options.mixedNetworks&&!options.verifyExisting&&!recheckStop.stop_requested()),"Standalone recheck refuses overlapping typed intent or cancellation",storage::ErrorCode::Conflict);
#if defined(TORRENT_DISABLE_EXTENSIONS)
    detail::require(!recheck,"Standalone recheck requires the fixed per-admission peer refusal gate",storage::ErrorCode::Unsupported);
#endif
    detail::require(!impl_->externalQueue||!intent,"Existing verified-seeding intent is unavailable in the external queue",storage::ErrorCode::Unsupported);
    detail::require(!options.verifyExisting||intent.has_value(),"No durable verified-seeding intent",storage::ErrorCode::Conflict);
    if(intent){
      detail::require(!options.ownedLoopbackPeerPort,"Verified seeding cannot use a pre-verification peer shortcut",storage::ErrorCode::Conflict);
      detail::require(intent->state!=storage::VerifiedSeedState::Pending
          ||(!intent->paused&&!intent->forceStart&&!options.forceStart&&!options.manuallyPaused),
          "Initial verification requires an unforced, unpaused stopped intent",storage::ErrorCode::Conflict);
      detail::require(intent->state!=storage::VerifiedSeedState::Pending||options.verifyExisting,
          "Stopped verification requires an explicit retry",storage::ErrorCode::Conflict);
      detail::require(intent->state!=storage::VerifiedSeedState::Pending||intent->startup==core::StartupState::Stopped,
          "Pending verification must remain stopped",storage::ErrorCode::Conflict);
      auto store=detail::take(storage::TorrentStateStore::open(lease));
      const auto state=detail::take(store.load(lease,options.hash));
      const auto flags=core::originalDownloadFlags(state.document().find("attributes"));
      detail::require(flags&&(*flags&0x8),"Verified seeding requires the original completion-move flag before preparation",storage::ErrorCode::Conflict);
    }
    detail::require(!options.mixedNetworks||impl_->mixedNetworks,"Mixed storage requires its qualified runtime mode",storage::ErrorCode::Unsupported);
    detail::require(!options.mixedNetworks||(!intent&&!allocating&&!options.ownedLoopbackPeerPort),"Mixed storage requires ordinary original intent",storage::ErrorCode::Unsupported);
    std::optional<MixedPublicPolicy> mixedPolicy;
    if(options.mixedNetworks){mixedPolicy.emplace(MixedPublicPolicy(detail::take(lease.retain()),options.hash));}
    std::unique_ptr<Impl::Managed> mixedPrepared;
    std::optional<detail::ManagedStorage> preparedStorage;
    detail::done(impl_->publishTarget(options.hash,[&]()->std::expected<void,storage::Error>{
      auto prepared=mixedPolicy?detail::ManagedStorage::prepareMixedPublic(lease,options.hash,*impl_->network,*mixedPolicy,claimed,retainedIdentities,payloadBridge,detail::ManagedStoragePurpose::Download)
          :detail::ManagedStorage::prepare(lease,options.hash,*impl_->network,claimed,retainedIdentities,payloadBridge,
            recheck?detail::ManagedStoragePurpose::RecheckReadOnly:allocating?detail::ManagedStoragePurpose::AllocateOnly:intent?(intent->goal==core::ExistingVerificationGoal::SelectedFiles?detail::ManagedStoragePurpose::VerifySelectedReadOnly:detail::ManagedStoragePurpose::VerifyExistingReadOnly):detail::ManagedStoragePurpose::Download,recheckStop);
      if(!prepared){return std::unexpected(prepared.error());}
      if(mixedPolicy){
        mixedPrepared=std::make_unique<Impl::Managed>(++impl_->operation,std::move(*prepared),options.hash);
        auto& member=*mixedPrepared;member.mixedLifetime=std::make_shared<detail::MixedManagedLifetime>();
        const auto parameters=member.storage.parameters();const auto claims=member.storage.claims();
        member.mixedSource=std::shared_ptr<const MixedManagedSource>(new MixedManagedSource(detail::take(lease.retain()),member.storage.identity(),member.status.admission,
            member.storage.preparedStateSnapshot(),parameters.trackers,{claims.begin(),claims.end()},member.mixedLifetime,impl_->mixedOrigin));
        if(impl_->mixedBinder){auto bound=impl_->mixedBinder(member.mixedSource);if(!bound){return std::unexpected(bound.error());}}
      }else{preparedStorage.emplace(std::move(*prepared));}
      return {};
    }));
    detail::require(preparedStorage.has_value()||bool(mixedPrepared),"Source publication did not execute storage preparation",storage::ErrorCode::Conflict);
    detail::require(!recheck||!recheckStop.stop_requested(),"Recheck cancelled after safe preparation; resume remains invalid",storage::ErrorCode::Cancelled);
    // A certified same-hash restart replaces its own old claim only after the
    // new read-only collision gate and complete preparation succeeded.
    if(previous!=impl_->managed.end()){if(previous->second->trackerResponse){static_cast<void>(previous->second->trackerResponse->drain());}impl_->managedByHandle.erase(previous->second->handle);impl_->managed.erase(previous);}
    if(!allocating){impl_->ensureSession();}
    auto value = mixedPrepared?std::move(mixedPrepared):std::make_unique<Impl::Managed>(++impl_->operation,std::move(*preparedStorage),options.hash);
    if(allocating){
      value->allocationIntent=allocation;value->status.allocationMode=options.allocationMode;value->status.phase=ManagedPhase::Allocating;value->status.retained=true;
      const auto progress=value->storage.allocationProgress();value->status.allocationBytes=progress.completedBytes;value->status.allocationTotalBytes=progress.totalBytes;
      value->status.bytes=value->storage.parameters().ti->total_size();value->status.totalPieces=value->storage.parameters().ti->num_pieces();
      const auto [slot,inserted]=impl_->managed.emplace(key,std::move(value));detail::require(inserted,"Duplicate allocation admission",storage::ErrorCode::Conflict);
      impl_->managedLease=&lease;impl_->phase=AccountingState::Active;impl_->publish();impl_->call=false;return {};
    }
    value->seedIntent=intent;
    if(recheck){value->status.recheck.emplace();value->recheckStop=recheckStop;}

    if(intent){value->status.verificationHeld=true;value->status.verifiedSeeding=VerifiedSeedingPhase::Checking;
      value->seedDeadline=options.verificationDeadline.value_or(Clock::now()+std::chrono::minutes{5});}
    value->manualPaused = options.manuallyPaused||(intent&&intent->state==storage::VerifiedSeedState::Released
        &&(intent->paused||intent->startup==core::StartupState::Stopped));
    value->status.manuallyPaused = value->manualPaused;
    auto parameters = value->storage.parameters();
    if(const auto mapped=value->storage.mappedFiles()) {
      const auto virtualPath=std::filesystem::path(parameters.save_path)/".BiglyBTC-original-storage"/(options.hash.hex()+"-"+std::to_string(impl_->operation));
      detail::done(impl_->diskPolicy->registerOriginalPath(virtualPath,mapped));parameters.save_path=virtualPath.string();
    }else{
      // Optional observer is registered BEFORE add_torrent, against the SAME
      // exact public storage_params key used by torrent::init/get_best().
      try{
        auto latency=std::make_shared<detail::MmapDiskLatency>(value->status.admission);
        const auto best=parameters.ti->info_hashes().get_best();std::array<unsigned char,20> hash{};
        std::copy(best.begin(),best.end(),hash.begin());
        if(impl_->diskPolicy->registerMmapLatency(std::filesystem::path(parameters.save_path),hash,latency)){
          value->mmapLatency=std::move(latency);
        }
      }catch(...){/* Optional observation cannot alter storage/admission success. */}
    }
    // ManagedStorage deliberately reconstructs checked original state instead
    // of restoring LT transfer totals. Never charge inherited totals twice.
    detail::require(parameters.total_downloaded == 0 && parameters.total_uploaded == 0,
        "Managed admission requires fresh transfer counter bases", storage::ErrorCode::Conflict);
    detail::require(!impl_->diskPolicy->isMetadataPath(std::filesystem::path(parameters.save_path)),
        "Reserved metadata storage cannot be used for ordinary payload", storage::ErrorCode::UnsafePath);
    if (const auto hints = impl_->importPeers.find(key); hints != impl_->importPeers.end() && !hints->second.empty()) {
      detail::require(impl_->network->mode == NetworkMode::Normal,
          "Numeric magnet peer hints cannot bypass isolation", storage::ErrorCode::Unsupported);
      parameters.peers = hints->second;
    }
    value->status.totalPieces = parameters.ti->num_pieces();
    value->status.bytes = parameters.ti->total_size();
    if(recheck){
      parameters.trackers.clear();parameters.tracker_tiers.clear();parameters.url_seeds.clear();parameters.http_seeds.clear();
      parameters.peers.clear();parameters.banned_peers.clear();parameters.dht_nodes.clear();parameters.have_pieces.clear();parameters.verified_pieces.clear();
      parameters.flags|=lt::torrent_flags::override_trackers|lt::torrent_flags::override_web_seeds|lt::torrent_flags::disable_dht
          |lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex|lt::torrent_flags::upload_mode|lt::torrent_flags::stop_when_ready;
      parameters.flags&=~(lt::torrent_flags::seed_mode|lt::torrent_flags::auto_managed);
#if !defined(TORRENT_DISABLE_EXTENSIONS)
      parameters.extensions.push_back([](const lt::torrent_handle&,lt::client_data_t){return std::make_shared<RecheckPeerGate>();});
#endif
    }
    if(intent){
      value->seedNetwork=parameters;
      parameters.trackers.clear();parameters.tracker_tiers.clear();parameters.url_seeds.clear();parameters.http_seeds.clear();
      parameters.peers.clear();parameters.banned_peers.clear();parameters.dht_nodes.clear();
      parameters.flags|=lt::torrent_flags::override_trackers|lt::torrent_flags::override_web_seeds
          |lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex
          |lt::torrent_flags::upload_mode;
      parameters.flags&=~lt::torrent_flags::seed_mode;
    }
    parameters.flags |= lt::torrent_flags::disable_dht;
    parameters.dht_nodes.clear();
    parameters.userdata = value->token.get();
    try {
      value->queueObservations=std::make_shared<detail::ManagedQueueObservations>(
          value->storage.identity().selected==storage::OriginalSelectedHash::V1?1u:2u);
      const auto& metadata=value->storage.queueMetadata();
      value->queueObservations->trackerOrder(metadata.trackerOrder);
      value->queueObservations->restore(metadata.aggregateScrapeCache?std::optional<std::string_view>{*metadata.aggregateScrapeCache}:std::nullopt,
          metadata.legacyScrapeCache);
    }catch(...){value->queueObservations.reset();} // Optional policy facts cannot fail storage admission.
    detail::require(detail::attachManagedPayloadCounters(parameters, value->payloadCounters, payloadBridge,value->queueObservations),
        "Exact managed transfer counters require the approved source ABI", storage::ErrorCode::Unsupported);
    if(!intent&&!recheck){value->trackerResponse=detail::attachManagedTrackerResponse(parameters,lease,
        value->storage.identity(),value->status.admission,impl_->trackerResponses,
        impl_->libraryRegularTrackerMode()?&detail::libraryHttpAnnounceScrapeDeadline:nullptr);}
    detail::attachPeerPayloadObserver(parameters, impl_->peerPayloadLedger, impl_->operation);
    if(!recheck&&value->storage.mainlinePolicy().discover){value->mainlinePort=impl_->attachMainlinePort(parameters,value->status.admission,
        value->storage.identity().originalKey,value->storage.identity().selected==storage::OriginalSelectedHash::V1?1u:2u);}
    parameters.flags |= lt::torrent_flags::paused | lt::torrent_flags::duplicate_is_error;
    if (impl_->queuePolicy || options.manuallyPaused || intent || recheck || impl_->externalQueue) {
      parameters.flags &= ~lt::torrent_flags::auto_managed;
      parameters.flags |= lt::torrent_flags::stop_when_ready;
    }
    else if (options.forceStart) { parameters.flags &= ~lt::torrent_flags::auto_managed; }
    else { parameters.flags |= lt::torrent_flags::auto_managed; }
    if(impl_->externalQueue){parameters.flags&=~lt::torrent_flags::paused;}
    value->forceStart = intent&&intent->state==storage::VerifiedSeedState::Released?intent->forceStart:options.forceStart;
    auto [slot, inserted] = impl_->managed.emplace(key, std::move(value));
    detail::require(inserted, "Duplicate managed admission", storage::ErrorCode::Conflict);
    lt::error_code error;
    try { slot->second->handle = impl_->session->add_torrent(std::move(parameters), error); }
    catch (...) { impl_->joinPoisoned(); throw; }
    if (error || !slot->second->handle.is_valid()) {
      impl_->managed.erase(slot);
      throw detail::Failure{{storage::ErrorCode::Io, "Cannot add original torrent to the managed session"}};
    }
    if(slot->second->mixedLifetime){
      // torrent_info caches its I2P classification even with override_trackers.
      // This genuine two-network admission must retain its Public transport
      // before a separately sourced I2P route exists.
      slot->second->handle.unset_flags(lt::torrent_flags::i2p_torrent);
      slot->second->mixedLifetime->attached.store(true,std::memory_order_release);
    }
    try { impl_->managedByHandle.emplace(slot->second->handle,slot->second.get()); }
    catch (...) { impl_->joinPoisoned(); throw; }
    slot->second->status.retained = true;
    impl_->managedLease = &lease;
    impl_->queuePositionsPending=true;impl_->queuePositionsUnavailable=true;
    impl_->phase = AccountingState::Active;
    if(recheck){slot->second->handle.force_recheck();}
    if (!impl_->externalQueue&&(impl_->queuePolicy || options.manuallyPaused || intent || recheck || !options.queued || options.forceStart)) { impl_->resumeAfterCheckGate(*slot->second); }
    // stop_when_ready atomically pauses at the end of checking, before payload
    // transfer; the sole owner poll then applies the selected original rule.
    impl_->session->resume();
    if (options.ownedLoopbackPeerPort) {
      detail::require(impl_->network->mode == NetworkMode::IsolatedLoopback,
          "Owned endpoint shortcut requires isolated networking");
      slot->second->handle.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(), options.ownedLoopbackPeerPort));
    }
    impl_->publish(); impl_->call = false;
    return {};
  } catch (const detail::Failure& failure) { impl_->call = false; return std::unexpected(failure.error); }
  catch (...) { impl_->call = false; return std::unexpected(storage::Error{storage::ErrorCode::Io, "Managed admission failed"}); }
}
#include "managed_recheck_api.inc"

std::expected<void, storage::Error> PersistentSession::pauseManaged(const core::TorrentHash& hash, bool paused) {
  revokeMainlineScrape(hash);
  if (!impl_->onWorker() || impl_->call || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed control is unavailable"});
  }
  if (!impl_->bindingHealthy()) { return std::unexpected(storage::Error{storage::ErrorCode::Io, "Explicit network binding was lost; restart the owner"}); }
  auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end() || !found->second->handle.is_valid() || found->second->stop) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Torrent is not active"});
  }
  auto& value = *found->second;
  if(value.status.recheck||value.status.verificationHeld){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Verification is held; cancel or wait for actual completion"});}
  value.manualPaused = paused;impl_->externalSnapshotValid=false;
  if(impl_->externalQueue){
    if(paused){impl_->forgetTrackerDhtAuthority(hash);if(value.mainlinePort){value.mainlinePort->advertised.store(0,std::memory_order_release);}}
    value.handle.unset_flags(lt::torrent_flags::auto_managed);value.policyHeld=true;
    const auto actual=value.handle.status();
    if(!paused&&!value.checkedAlert&&(actual.state==lt::torrent_status::checking_files||actual.state==lt::torrent_status::checking_resume_data)){
      value.handle.set_flags(lt::torrent_flags::stop_when_ready);value.externalPauseSettled=false;value.externalPausePending=false;value.handle.resume();
      impl_->sampleManaged(value);return {}; // Explicit Resume restarts checking, never a payload Run.
    }
    if(!(actual.flags & lt::torrent_flags::paused)&&!value.externalPausePending){
      value.externalPauseSettled=false;value.externalPausePending=true;value.handle.pause();
    }impl_->sampleManaged(value);return {};
  }
  if (paused) {
    impl_->forgetTrackerDhtAuthority(hash);
    if(value.mainlinePort){value.mainlinePort->advertised.store(0,std::memory_order_release);}
    value.handle.unset_flags(lt::torrent_flags::auto_managed);value.checkPauseDrained=false; value.handle.pause();
    if (impl_->queuePolicy) { impl_->sampleManaged(value); }
  }
  else {
    if(impl_->checkPolicy&&!value.status.checked){impl_->resumeAfterCheckGate(value);impl_->sampleManaged(value);return {};}
    if (!impl_->queuePolicy) { value.handle.unset_flags(lt::torrent_flags::stop_when_ready); }
    if (!value.forceStart && !impl_->queuePolicy && !value.seedIntent) { value.handle.set_flags(lt::torrent_flags::auto_managed); }
    if (impl_->queuePolicy) {
      value.handle.unset_flags(lt::torrent_flags::auto_managed);
      value.handle.pause(); value.policyHeld = true;
      impl_->sampleManaged(value); impl_->applyQueue();
    } else { value.handle.resume(); value.policyHeld = false; }
  }
  return {};
}
std::expected<void, storage::Error> PersistentSession::queueManaged(const core::TorrentHash& hash, bool forceStart, bool queued) {
  revokeMainlineScrape(hash);
  if (!managedAvailable() || impl_->call) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Managed queue control is unavailable"});
  }
  if (!impl_->bindingHealthy()) { return std::unexpected(storage::Error{storage::ErrorCode::Io, "Explicit network binding was lost; restart the owner"}); }
  const auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end() || !found->second->handle.is_valid() || found->second->stop) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Torrent is not active"});
  }
  auto& value = *found->second;
  if(value.status.recheck||value.status.verificationHeld){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Verification is held; queue commands cannot release it"});}
  value.manualPaused = false; value.forceStart = forceStart;impl_->externalSnapshotValid=false;
  if(impl_->externalQueue){
    value.handle.unset_flags(lt::torrent_flags::auto_managed);value.policyHeld=true;
    if(!value.checkedAlert||!value.status.checked){impl_->sampleManaged(value);return {};}
    const auto actual=value.handle.status();if(!(actual.flags & lt::torrent_flags::paused)&&!value.externalPausePending){
      value.externalPauseSettled=false;value.externalPausePending=true;value.handle.pause();
    }impl_->sampleManaged(value);return {};
  }
  if (!impl_->queuePolicy) { value.handle.unset_flags(lt::torrent_flags::stop_when_ready); }
  if(impl_->checkPolicy&&!value.status.checked){value.handle.unset_flags(lt::torrent_flags::auto_managed);impl_->resumeAfterCheckGate(value);impl_->sampleManaged(value);return {};}
  if (forceStart) { value.handle.unset_flags(lt::torrent_flags::auto_managed); value.handle.resume(); }
  else if (impl_->queuePolicy) {
    value.handle.unset_flags(lt::torrent_flags::auto_managed);
    value.handle.pause(); value.policyHeld = true;
  } else { if(!value.seedIntent){value.handle.set_flags(lt::torrent_flags::auto_managed);} if (queued) { value.handle.pause(); } else { value.handle.resume(); } }
  if (impl_->queuePolicy) { impl_->sampleManaged(value); impl_->applyQueue(); }
  return {};
}
std::expected<bool,storage::Error> PersistentSession::rebaseCategory(const storage::ProfileLease& lease,const core::TorrentHash& hash){
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Category refresh requires the current owner worker and same profile OFD"});
  }
  const auto found=impl_->managed.find(hash.hex());
  if(found==impl_->managed.end()||!found->second->status.retained){return false;}
  if(!impl_->managedLease||!impl_->managedLease->sharesOwnership(lease)){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Category refresh requires the actual profile OFD"});
  }
  auto& value=*found->second;
  if(value.status.hash!=hash||!value.status.admission||value.status.failure){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Category refresh admission is unavailable"});
  }
  auto rebased=value.storage.rebaseCategory(lease);
  if(rebased&&value.mixedSource){
    try{
      const auto& old=*value.mixedSource;
      auto replacement=std::shared_ptr<const MixedManagedSource>(new MixedManagedSource(old.lease_,old.identity_,old.admission_,
          std::string(value.storage.categoryStateSnapshot()),old.trackers_,old.claims_,old.alive_,old.origin_));
      value.mixedSource=std::move(replacement); // Old shared immutable seals are unchanged.
    }catch(const std::bad_alloc&){rebased=std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot refresh immutable mixed category seal"});}
  }
  if(!rebased){
    value.status.failure=rebased.error();value.status.phase=ManagedPhase::Failed;value.categoryRebaseFailed=true;
    if(value.mixedLifetime){value.mixedLifetime->retained=false;value.mixedLifetime->attached=false;}
    // No requestStop/status/SDK calls while the caller may hold publication F.
    // Existing next poll stops/removes; failure prevents an unsafe checkpoint.
    return std::unexpected(rebased.error());
  }
  return true;
}
std::expected<void, storage::Error> PersistentSession::stopManaged(const core::TorrentHash& hash) {
  revokeMainlineScrape(hash);
  if (!impl_->onWorker() || impl_->call || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed control is unavailable"});
  }
  const auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end()) { return {}; }
  try { if(found->second->status.recheck&&found->second->status.retained){found->second->recheckCancelled=true;found->second->status.recheck->phase=ManagedRecheckPhase::Retiring;} impl_->forgetTrackerDhtAuthority(hash); impl_->requestStop(*found->second); return {}; }
  catch (...) { return std::unexpected(storage::Error{storage::ErrorCode::Io, "Cannot request torrent flush"}); }
}
std::expected<void, storage::Error> PersistentSession::forgetManaged(const core::TorrentHash& hash, bool removePeerHints) {
  if (!impl_->onWorker() || impl_->call || impl_->registered || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed retirement is unavailable"});
  }
  const auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end()) { return {}; }
  const auto& value = *found->second;
  if (value.handle.is_valid() || value.removing || value.status.retained
      || value.status.phase != ManagedPhase::Stopped || !value.flushed || !value.removed) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Torrent files are still retained or retirement is uncertified"});
  }
  if (value.status.failure) { return std::unexpected(*value.status.failure); }
  if(impl_->historyConsumerLease&&!value.historyTaken&&value.historyCheckpointed&&(value.historyCompletionMillis||value.historyCompletionProblem)){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Consume the durable completion before releasing its owner"});
  }
  impl_->managedByHandle.erase(value.handle);
  impl_->managed.erase(found); // Releases ManagedStorage's path/file descriptors.
  impl_->queuePositionsPending=true;impl_->queuePositionsUnavailable=true;
  if (removePeerHints) { impl_->importPeers.erase(hash.hex()); }
  return {};
}
std::expected<core::TorrentHash,storage::Error> PersistentSession::createHybridCompanion(
    storage::ProfileLease& lease,const core::TorrentHash& source,storage::OriginalSelectedHash target,std::stop_token stop,storage::IntakeHistoryOutcome* history) {
  if(!managedAvailable()||impl_->call||impl_->closingAdmission){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Hybrid companion owner is unavailable"});
  }
  if(source.bytes().size()!=20||(target!=storage::OriginalSelectedHash::V1&&target!=storage::OriginalSelectedHash::V2)){
    return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Invalid original hybrid source/target identity"});
  }
  if(stop.stop_requested()){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Hybrid companion cancelled"});}
  try {
    impl_->bind(lease);
    // This existing path checks actual handle removal, flush/checkpoint result,
    // retained flag and failure before releasing old payload descriptors.
    const auto forgotten=forgetManaged(source);
    if(!forgotten){return std::unexpected(forgotten.error());}
    impl_->call=true;
    struct Reset {Impl& value;~Reset(){value.call=false;}} reset{*impl_};
    std::vector<storage::OriginalTorrentIdentity> retained;
    for(const auto& [key,value]:impl_->managed){(void)key;retained.push_back(value->storage.identity());}
    return storage::importHybridAlias(lease,source,target,retained,nullptr,stop,history);
  }catch(const detail::Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Hybrid companion failed; inspect pending import before retry"});}
}
std::optional<unsigned short> PersistentSession::tcpListenPort() const noexcept {
  if (!impl_->onWorker() || !impl_->session || impl_->closed || impl_->poisoned
      || (impl_->network && !impl_->network->incomingTcp)) { return std::nullopt; }
  try { const auto port = impl_->session->listen_port(); return port ? std::optional{port} : std::nullopt; }
  catch (...) { return std::nullopt; }
}
std::expected<void, storage::Error> PersistentSession::startMagnet(storage::ProfileLease& lease,
    const PendingMagnet& intent, const std::filesystem::path& profile) {
  if (!managedAvailable() || impl_->call || impl_->closingAdmission || intent.network!=MagnetNetwork::Public) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Magnet admission is unavailable"});
  }
  const auto key = intent.hash.hex();
  const auto old = impl_->magnets.find(key);
  if (old != impl_->magnets.end() && old->second->status.retained) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Magnet is already resolving or retiring"});
  }
  if (const auto active = impl_->managed.find(key); active != impl_->managed.end() && active->second->status.retained) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Torrent is already admitted"});
  }
  impl_->call = true;
  try {
    impl_->bind(lease); impl_->retirement.reset();
    detail::require(!intent.sourceEntry.empty() && intent.destination.has_value(),
        "Persist the magnet and select its destination before resolving", storage::ErrorCode::Conflict);
    detail::require(profile.is_absolute() && profile.lexically_normal() == profile,
        "Magnet profile path must be absolute and normalized", storage::ErrorCode::UnsafePath);
    const auto directory = detail::directory(profile);
    struct stat held{}, named{};
    detail::require(::fstat(lease.directoryHandle(), &held) == 0 && ::fstat(directory.get(), &named) == 0
        && held.st_dev == named.st_dev && held.st_ino == named.st_ino,
        "Magnet profile path differs from its held lease", storage::ErrorCode::Conflict);
    detail::require(intent.timeoutMillis > 0 && intent.timeoutMillis <= 24LL * 60 * 60 * 1000,
        "Unsupported magnet resolution deadline", storage::ErrorCode::InvalidFormat);
    detail::require(impl_->operation != std::numeric_limits<std::uint64_t>::max(), "Admission identity exhausted");
    if (!impl_->network) { impl_->network = NetworkPolicy{}; }
    const auto current = detail::take(readPendingMagnets(lease, *impl_->network));
    const auto saved = std::ranges::find_if(current, [&](const auto& value) { return value.hash == intent.hash; });
    detail::require(saved != current.end() && saved->sourceEntry == intent.sourceEntry,
        "Magnet recovery intent is stale", storage::ErrorCode::Conflict);
    const auto admission = ++impl_->operation;
    const auto virtualPath = profile / (".BiglyBTC.metadata." + key + "." + std::to_string(admission));
    detail::done(impl_->diskPolicy->registerMetadataPath(virtualPath));
    auto parameters = detail::take(metadataOnlyParameters(intent, *impl_->network, virtualPath));
    auto value = std::make_unique<Impl::Magnet>(admission, intent);
    parameters.flags |= lt::torrent_flags::disable_dht;
    parameters.dht_nodes.clear();
    parameters.userdata = value->token.get();
    detail::attachPeerPayloadObserver(parameters, impl_->peerPayloadLedger, admission);
    parameters.flags |= lt::torrent_flags::paused | lt::torrent_flags::duplicate_is_error;
    parameters.flags &= ~lt::torrent_flags::auto_managed;
    impl_->ensureSession();
    value->mainlinePort=impl_->attachMainlinePort(parameters,admission,value->intent.hash,value->intent.requested.has_v1()?1u:2u);
    if (old != impl_->magnets.end()) { impl_->magnets.erase(old); }
    auto [slot, inserted] = impl_->magnets.emplace(key, std::move(value));
    detail::require(inserted, "Duplicate magnet admission", storage::ErrorCode::Conflict);
    lt::error_code error;
    try { slot->second->handle = impl_->session->add_torrent(std::move(parameters), error); }
    catch (...) { impl_->joinPoisoned(); throw; }
    if (error || !slot->second->handle.is_valid()) {
      impl_->magnets.erase(slot);
      throw detail::Failure{{storage::ErrorCode::Io, "Cannot admit magnet metadata; recovery intent retained"}};
    }
    impl_->magnetLease = &lease; impl_->managedLease = &lease; impl_->magnetProfile = profile;
    slot->second->expires = Clock::now() + std::chrono::milliseconds(intent.timeoutMillis);
    slot->second->status.retained = true;
    impl_->phase = AccountingState::Active;
    slot->second->handle.resume(); impl_->session->resume(); impl_->publish();
    impl_->call = false; return {};
  } catch (const detail::Failure& failure) { impl_->call = false; return std::unexpected(failure.error); }
  catch (...) { impl_->call = false; return std::unexpected(storage::Error{storage::ErrorCode::Io, "Magnet admission failed; recovery intent retained"}); }
}
std::expected<std::shared_ptr<const PendingMetadataSource>,storage::Error> PersistentSession::prepareMagnetMetadata(
    storage::ProfileLease& lease,const PendingMagnet& intent,const std::filesystem::path& profile,PendingMetadataBinding binder){
#if defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  if(!managedAvailable()||impl_->call||impl_->closingAdmission||!impl_->mixedNetworks||!impl_->mixedOrigin||intent.network!=MagnetNetwork::PublicAndI2p||!binder){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Explicit mixed metadata owner unavailable"});}
  const auto entered=Clock::now();const auto key=intent.hash.hex();
  if(const auto old=impl_->magnets.find(key);old!=impl_->magnets.end()&&(old->second->status.retained||old->second->pendingSource)){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Pending metadata entry is retained"});}
  if(const auto old=impl_->managed.find(key);old!=impl_->managed.end()&&old->second->status.retained){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Payload identity is already retained"});}
  for(const auto& [name,member]:impl_->managed){(void)name;if(!member->status.retained&&!member->removing&&!member->handle.is_valid()){continue;}const auto full=member->storage.parameters().ti->info_hashes();
    if((intent.requested.has_v1()&&full.has_v1()&&intent.requested.v1==full.v1)||(intent.requested.has_v2()&&full.has_v2()&&intent.requested.v2==full.v2)){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Pending metadata aliases an actual retained payload admission"});}}
  for(const auto& [name,entry]:impl_->magnets){(void)name;if(!entry->status.retained){continue;}const auto& full=entry->intent.requested;
    if((intent.requested.has_v1()&&full.has_v1()&&intent.requested.v1==full.v1)||(intent.requested.has_v2()&&full.has_v2()&&intent.requested.v2==full.v2)){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Pending metadata alias is already retained"});}}
  impl_->call=true;
  try{
    impl_->bind(lease);detail::require(intent.destination&&!intent.sourceEntry.empty()&&intent.timeoutMillis>0&&intent.timeoutMillis<=3600000,"Persisted bounded metadata intent required",storage::ErrorCode::Conflict);
    detail::require(profile.is_absolute()&&profile.lexically_normal()==profile,"Metadata profile path must be normalized",storage::ErrorCode::UnsafePath);
    const auto directory=detail::directory(profile);struct stat held{},named{};
    detail::require(::fstat(lease.directoryHandle(),&held)==0&&::fstat(directory.get(),&named)==0&&held.st_dev==named.st_dev&&held.st_ino==named.st_ino,"Metadata path differs from retained profile",storage::ErrorCode::Conflict);
    if(!impl_->network){impl_->network=NetworkPolicy{};}
    (void)detail::take(validatePendingMagnet(lease,intent,*impl_->network));
    detail::require(impl_->operation!=std::numeric_limits<std::uint64_t>::max(),"Metadata admission exhausted");const auto admission=++impl_->operation;
    const auto virtualPath=profile/(".BiglyBTC.metadata."+key+"."+std::to_string(admission));detail::done(impl_->diskPolicy->registerMetadataPath(virtualPath));
    auto value=std::make_unique<Impl::Magnet>(admission,intent);value->expires=entered+std::chrono::milliseconds(intent.timeoutMillis);
    value->pendingParameters=detail::take(metadataOnlyParameters(intent,*impl_->network,virtualPath));
    value->pendingParameters.flags|=lt::torrent_flags::paused|lt::torrent_flags::duplicate_is_error;
    value->pendingParameters.userdata=value->token.get();
    value->pendingLifetime=std::make_shared<detail::PendingMetadataLifetime>();
    value->pendingSource=std::shared_ptr<const PendingMetadataSource>(new PendingMetadataSource(detail::take(lease.retain()),intent,admission,value->pendingLifetime,impl_->mixedOrigin));
    auto result=value->pendingSource;
    impl_->ensureSession(); // No SDK wait is allowed inside source publication.
    const auto old=impl_->magnets.find(key);if(old!=impl_->magnets.end()){impl_->magnets.erase(old);}
    auto [slot,ok]=impl_->magnets.emplace(key,std::move(value));detail::require(ok,"Metadata reservation collision",storage::ErrorCode::Conflict);slot->second->status.retained=true;
    // Caller owns the narrow pending-row publication (possibly composed key1
    // then key0 anchors). No generic full-download target publisher is used.
    auto bound=binder(slot->second->pendingSource);
    if(!bound){slot->second->pendingLifetime->retained.store(false);impl_->magnets.erase(slot);throw detail::Failure{bound.error()};}

    impl_->magnetLease=&lease;impl_->managedLease=&lease;impl_->magnetProfile=profile;impl_->call=false;return result;
  }catch(const detail::Failure& e){if(const auto it=impl_->magnets.find(key);it!=impl_->magnets.end()&&it->second->pendingLifetime){it->second->pendingLifetime->retained.store(false);impl_->magnets.erase(it);}impl_->call=false;return std::unexpected(e.error);}
  catch(...){if(const auto it=impl_->magnets.find(key);it!=impl_->magnets.end()&&it->second->pendingLifetime){it->second->pendingLifetime->retained.store(false);impl_->magnets.erase(it);}impl_->call=false;return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot reserve mixed metadata entry"});}
#else
  (void)lease;(void)intent;(void)profile;(void)binder;return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Qualified discovery SDK unavailable"});
#endif
}
std::expected<void,storage::Error> PersistentSession::activatePreparedMagnet(const core::TorrentHash& hash){
  if(!managedAvailable()||impl_->call||impl_->closingAdmission){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata activation unavailable"});}
  const auto at=impl_->magnets.find(hash.hex());if(at==impl_->magnets.end()||!at->second->pendingSource){return std::unexpected(storage::Error{storage::ErrorCode::Missing,"No reserved metadata entry"});}
  auto& value=*at->second;
  if(!value.pendingSource->effectsCurrent()||!value.pendingLifetime->authorized.load(std::memory_order_acquire)||value.pendingLifetime->attached.load()||value.cancel||Clock::now()>=value.expires){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata authorization is stale or expired"});}
  impl_->call=true;
  try{lt::error_code error;value.handle=impl_->session->add_torrent(value.pendingParameters,error);
    if(error||!value.handle.is_valid()){value.pendingLifetime->retained.store(false);value.status.retained=false;if(value.handle.is_valid()){impl_->joinPoisoned();}throw detail::Failure{{storage::ErrorCode::Io,"Provider rejected the actual metadata-only handle"}};}
    value.pendingLifetime->attached.store(true,std::memory_order_release);value.handle.resume();impl_->session->resume();impl_->phase=AccountingState::Active;impl_->call=false;return {};
  }catch(const detail::Failure& e){impl_->call=false;return std::unexpected(e.error);}catch(...){value.pendingLifetime->retained.store(false);impl_->joinPoisoned();impl_->call=false;return std::unexpected(storage::Error{storage::ErrorCode::Io,"Metadata ownership became uncertain"});}
}
std::expected<void,storage::Error> PersistentSession::withdrawPreparedMagnet(
    const core::TorrentHash& hash,storage::Error reason){
  if(!impl_->onWorker()||impl_->call){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata withdrawal requires its actual owner"});}
  const auto at=impl_->magnets.find(hash.hex());
  if(at==impl_->magnets.end()||!at->second->pendingSource||at->second->cancel){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"A noncancelled mixed metadata admission is required"});}
  auto& value=*at->second;impl_->call=true;
  try{
    // Source failure is not a user cancellation. Preserve the exact saved row
    // while ending all new effects immediately, before actual remove/weak expiry.
    if(!value.status.failure){value.status.failure=std::move(reason);}
    value.pendingLifetime->retiring.store(true,std::memory_order_release);
    value.pendingLifetime->authorized.store(false);
    if(!value.pendingLifetime->attached.load()||!value.status.retained){
      value.pendingLifetime->retained.store(false,std::memory_order_release);value.status.retained=false;value.status.phase=MagnetPhase::Failed;
    }else{
      detail::require(value.handle.is_valid()||value.removing,"Attached metadata lifetime lost before its removal fence",storage::ErrorCode::Conflict);
      impl_->requestMagnetRetirement(value); // Already-removing keeps its original fence/deadline.
    }
    impl_->call=false;return {};
  }catch(...){impl_->joinPoisoned();impl_->call=false;return std::unexpected(storage::Error{storage::ErrorCode::Io,"Metadata source withdrawal ownership became uncertain; recovery intent retained"});}
}
std::expected<std::string,storage::Error> PersistentSession::takeCompletedMagnetMetadata(const core::TorrentHash& hash){
  if(!impl_->onWorker()||impl_->call){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata result owner unavailable"});}
  const auto at=impl_->magnets.find(hash.hex());if(at==impl_->magnets.end()){return std::unexpected(storage::Error{storage::ErrorCode::Missing,"No metadata entry"});}auto& value=*at->second;
  if(!value.pendingSource||value.status.retained||!value.removed||value.handle.is_valid()||!value.expiryCrossed||value.status.failure||value.cancel||value.metadata.empty()){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Actual metadata removal/result fence incomplete"});}
  return value.metadata; // Retained until explicit pending lifecycle release; failed import may retry.
}
std::expected<void,storage::Error> PersistentSession::releasePreparedMagnet(const core::TorrentHash& hash){
  if(!impl_->onWorker()||impl_->call){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata result owner unavailable"});}
  const auto at=impl_->magnets.find(hash.hex());if(at==impl_->magnets.end()||!at->second->pendingSource||at->second->status.retained||at->second->handle.is_valid()||at->second->pendingLifetime->retained.load()){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Actual metadata lifetime remains retained"});}
  impl_->magnets.erase(at);return {};
}
std::expected<void, storage::Error> PersistentSession::cancelMagnet(const core::TorrentHash& hash) {
  if (!managedAvailable() || impl_->call) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Magnet cancellation is unavailable"});
  }
  const auto found = impl_->magnets.find(hash.hex());
  if (found == impl_->magnets.end()) { return std::unexpected(storage::Error{storage::ErrorCode::Missing, "Magnet is not admitted"}); }
  auto& value = *found->second;
  if(value.pendingSource){value.cancel=true;if(value.handle.is_valid()){impl_->requestMagnetRetirement(value);}else{value.pendingLifetime->retained.store(false);value.status.retained=false;value.status.phase=MagnetPhase::Cancelled;}return {};}
  if (value.status.phase == MagnetPhase::Imported || value.status.phase == MagnetPhase::Cancelled) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Magnet has already left the pending lifecycle"});
  }
  try {
    value.cancel = true;
    if (value.status.retained) { impl_->requestMagnetRetirement(value); }
    else {
      if (!impl_->magnetLease) { return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Magnet lease is unavailable"}); }
      auto removed = cancelPendingMagnet(*impl_->magnetLease, value.intent);
      if (!removed) { return removed; }
      value.status.failure.reset(); value.status.phase = MagnetPhase::Cancelled;
    }
    return {};
  } catch (...) { return std::unexpected(storage::Error{storage::ErrorCode::Io, "Cannot cancel magnet; recovery intent retained"}); }
}
std::vector<PendingMagnetStatus> PersistentSession::pendingMagnets() const {
  std::vector<PendingMagnetStatus> result;
  if (!impl_->onWorker()) { return result; }
  result.reserve(impl_->magnets.size());
  for (const auto& [key, entry] : impl_->magnets) { static_cast<void>(key); result.push_back(entry->status); }
  return result;
}
std::expected<void, storage::Error> PersistentSession::reorderManaged(std::span<const core::TorrentHash> order) {
  if (!managedAvailable() || impl_->call) { return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed queue is unavailable"}); }
  int position = 0;
  for (const auto& hash : order) {
    const auto found = impl_->managed.find(hash.hex());
    if (found == impl_->managed.end() || !found->second->handle.is_valid()
        || found->second->stop || found->second->status.selectedComplete) { continue; }
    found->second->queuePosition = ++position;
    found->second->handle.queue_position_set(lt::queue_position_t{position-1});
  }
  impl_->queuePositionsPending=true;impl_->queuePositionsUnavailable=true;
  if(impl_->queuePolicy){impl_->normalizeManagedPositions();}
  return {};
}
bool PersistentSession::managedAvailable() const noexcept {
  return impl_->onWorker() && !impl_->closed && !impl_->poisoned && !impl_->registered;
}
std::vector<std::uint8_t> PersistentSession::verifiedSeedPieces(const core::TorrentHash& hash) const {
  if(!impl_->onWorker()){return {};}
  const auto found=impl_->managed.find(hash.hex());
  return found!=impl_->managed.end()&&found->second->seedIntent&&found->second->seedPiecesExact
      ?found->second->verified:std::vector<std::uint8_t>{};
}
std::expected<void,storage::Error> PersistentSession::configureHistoryCompletions(const storage::ProfileLease& lease){
  if(!impl_->onWorker()||impl_->call||impl_->registered||impl_->closed||impl_->poisoned||impl_->closingAdmission){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"History interest requires the actual idle owner worker"});}
  try{
    if(impl_->historyConsumerLease){detail::require(impl_->historyConsumerLease->sharesOwnership(lease),"History interest cannot cross profile OFDs",storage::ErrorCode::Conflict);(void)detail::take(lease.retain());return {};}
    detail::require(impl_->managed.empty(),"History interest must precede managed admissions; no retrospective capture",storage::ErrorCode::Conflict);
    auto access=detail::take(lease.sourceAccess());auto held=detail::take(lease.retain());struct stat profile{};
    detail::require(::fstat(lease.directoryHandle(),&profile)==0&&S_ISDIR(profile.st_mode),"History interest profile is unavailable",storage::ErrorCode::UnsafePath);
    detail::require(!impl_->profileBound||(profile.st_dev==impl_->profileDevice&&profile.st_ino==impl_->profileInode),"History interest actual profile changed",storage::ErrorCode::Conflict);
    for(const auto* actual:std::array<const storage::ProfileLease*,4>{impl_->managedLease,impl_->magnetLease,impl_->regularTrackerLease.get(),impl_->mainlineOriginalLease.get()}){detail::require(!actual||lease.sharesOwnership(*actual),"History interest actual OFD changed",storage::ErrorCode::Conflict);}
    impl_->historyConsumerLease=std::move(held);return {};
  }catch(const detail::Failure& failure){return std::unexpected(failure.error);}catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Optional history interest could not be retained"});}
}
std::expected<std::optional<storage::CompletionHistoryOutcome>,storage::Error> PersistentSession::takeHistoryCompletion(){
  if(!impl_->onWorker()||impl_->call){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Completion receipt requires the actual idle owner worker"});}
  if(!impl_->historyConsumerLease){return std::optional<storage::CompletionHistoryOutcome>{};}
  if(impl_->poisoned){for(auto& [key,m]:impl_->managed){(void)key;m->historyTaken=true;m->historyCompletionMillis.reset();m->historyCompletionProblem.reset();m->historyLease.reset();}return std::optional<storage::CompletionHistoryOutcome>{};}
  for(auto& [key,m]:impl_->managed){(void)key;auto& value=*m;
    if(value.historyTaken||!value.historyCheckpointed||value.status.retained||value.status.failure
        ||(!value.historyCompletionMillis&&!value.historyCompletionProblem)){continue;}
    value.historyTaken=true;storage::CompletionHistoryOutcome outcome;
    try{
      if(Clock::now()>=value.historyCheckpointEnd){outcome.warning=storage::Error{storage::ErrorCode::Conflict,"Checkpoint saved; delayed completion history event expired without replay"};}
      else if(value.historyCompletionProblem){outcome.warning=storage::Error{*value.historyCompletionProblem,"Checkpoint saved; original completion metadata needs review"};}
      else if(value.historyLease&&value.historyCompletionMillis&&value.storage.historyCompletionRecorded()&&value.storage.historyCheckpointBytes()){
        auto issued=storage::DurableCompletionReceipt::issue(*value.historyLease,value.storage.identity(),value.storage.capturedDownload(),
            {impl_->identity.applicationRun,impl_->identity.profileSession,impl_->identity.engineEpoch},value.status.admission,
            value.storage.preparedStateSnapshot(),*value.historyCompletionMillis,
            value.historyEventComplete?storage::DurableCompletionReceipt::Transition::Complete:storage::DurableCompletionReceipt::Transition::Incomplete,
            *value.storage.historyCheckpointBytes(),value.status.received,value.status.sent,value.storage.originalBytes());
        if(issued){outcome.receipt.emplace(std::move(*issued));}else{outcome.warning=issued.error();}
      }else{outcome.warning=storage::Error{storage::ErrorCode::Conflict,"Checkpoint saved; completion source ownership is unavailable"};}
    }catch(...){outcome.warning=storage::Error{storage::ErrorCode::Io,"Checkpoint saved; completion history proof could not be captured"};}
    value.historyLease.reset();
    return std::optional<storage::CompletionHistoryOutcome>{std::move(outcome)};
  }
  return std::optional<storage::CompletionHistoryOutcome>{};
}

bool PersistentSession::historyReconstructionAvailable()const noexcept{
  if(!impl_->onWorker()||impl_->call||impl_->registered||impl_->closed||impl_->poisoned
      ||impl_->closingAdmission||!impl_->network||impl_->network->mode!=NetworkMode::Normal){return false;}
  return std::ranges::none_of(impl_->managed,[](const auto& pair){const auto& m=*pair.second;return m.status.retained||m.handle.is_valid()||m.status.failure.has_value();})
      &&std::ranges::none_of(impl_->magnets,[](const auto& pair){return pair.second->status.retained||pair.second->handle.is_valid();});
}
std::expected<storage::HistoryReconstructionReceipt,storage::Error> PersistentSession::prepareHistoryReconstruction(
    const storage::ProfileLease& lease,std::stop_token stop,std::chrono::steady_clock::time_point end){
  if(!historyReconstructionAvailable()){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"History Reset requires the complete stopped normal library; retire retained owners first"});}
  try{
    // Do not call bind()/bindingHealthy(): a snapshot operation must never
    // join or call the provider while its caller holds a source read section.
    struct stat profile{};detail::require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0&&S_ISDIR(profile.st_mode),"History Reset requires the retained profile",storage::ErrorCode::UnsafePath);
    detail::require(!impl_->profileBound||(profile.st_dev==impl_->profileDevice&&profile.st_ino==impl_->profileInode),"History Reset cannot cross actual owner profiles",storage::ErrorCode::Conflict);
    detail::require(!impl_->managedLease||lease.sharesOwnership(*impl_->managedLease),"History Reset managed profile OFD changed",storage::ErrorCode::Conflict);
    detail::require(!impl_->magnetLease||lease.sharesOwnership(*impl_->magnetLease),"History Reset metadata profile OFD changed",storage::ErrorCode::Conflict);
    detail::require(!impl_->regularTrackerLease||lease.sharesOwnership(*impl_->regularTrackerLease),"History Reset profile OFD changed",storage::ErrorCode::Conflict);
    detail::require(!impl_->mainlineOriginalLease||lease.sharesOwnership(*impl_->mainlineOriginalLease),"History Reset discovery profile OFD changed",storage::ErrorCode::Conflict);
    return storage::HistoryReconstructionReceipt::issue(lease,{impl_->identity.applicationRun,impl_->identity.profileSession,impl_->identity.engineEpoch},stop,end);
  }catch(const detail::Failure& failure){return std::unexpected(failure.error);}catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot capture actual normal history Reset source"});}
}

void PersistentSession::discardHistoryCompletions()noexcept{
  if(!impl_->onWorker()||impl_->call){return;}
  // Explicit auxiliary-history abandonment. This cannot grant effects, write
  // files or erase an actual owner; future Reload must not replay old events.
  for(auto& [key,m]:impl_->managed){(void)key;if(!m->historyCheckpointed){continue;}
    m->historyTaken=true;m->historyCompletionMillis.reset();m->historyCompletionProblem.reset();m->historyLease.reset();}
}

std::expected<void,storage::Error> PersistentSession::validateSharedSeed(
    const storage::ProfileLease& lease,const storage::TorrentIntakeMetadata& metadata,
    const std::filesystem::path& destination) const {
  using storage::Error;using storage::ErrorCode;
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||impl_->closingAdmission
      ||!impl_->managedLease||!lease.valid()||!lease.sharesOwnership(*impl_->managedLease)){
    return std::unexpected(Error{ErrorCode::Conflict,"Shared seed owner/profile is unavailable"});
  }
  const auto found=impl_->managed.find(metadata.hash().hex());
  if(found==impl_->managed.end()){
    return std::unexpected(Error{ErrorCode::Conflict,"Shared torrent has no actual managed seed"});
  }
  const auto& value=*found->second;
  if(!value.token||!value.status.admission||value.token->number!=value.status.admission
      ||!value.status.retained||value.stop||value.removing||value.status.failure||value.manualPaused||value.policyHeld
      ||!value.checkedAlert||!value.status.checked||!value.status.complete||!value.status.selectedComplete
      ||value.status.phase!=ManagedPhase::Seeding||value.status.verificationHeld
      ||value.status.verifiedSeeding!=VerifiedSeedingPhase::Released||!value.handle.is_valid()
      ||value.storage.identity().originalKey!=metadata.hash()){
    return std::unexpected(Error{ErrorCode::Conflict,"Shared torrent is not a live fully verified seed"});
  }
  try{
    const auto actual=value.handle.status();
    if(!actual.is_seeding||actual.errc||(actual.flags & lt::torrent_flags::paused)){
      return std::unexpected(Error{ErrorCode::Conflict,"Shared torrent is paused or no longer seeding"});
    }
    return value.storage.validateSharedSeedSource(lease,metadata.layout(),destination);
  }catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Cannot validate shared seed owner"});}
}
std::expected<void,storage::Error> detail::PersistentCall::announceSharedSeed(
    PersistentSession& owner,const storage::ProfileLease& lease,
    const storage::TorrentIntakeMetadata& metadata,const std::filesystem::path& destination) {
  const auto verified=owner.validateSharedSeed(lease,metadata,destination);
  if(!verified){return std::unexpected(verified.error());}
  auto& impl=*owner.impl_;
  if(!impl.bindingHealthy()){
    return std::unexpected(storage::Error{storage::ErrorCode::Io,"Shared seed network binding was lost"});
  }
  if(!impl.network||impl.network->mode!=NetworkMode::Normal||!impl.network->httpTrackers
      ||!metadata.trackers()||!metadata.trackers()->announce.starts_with("http://")){
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Shared tracker is not an admitted HTTP source"});
  }
  try{
    auto& value=*impl.managed.at(metadata.hash().hex());
    const auto trackers=value.handle.trackers();
    for(std::size_t index=0;index<trackers.size();++index){
      if(trackers[index].url==metadata.trackers()->announce){
        value.handle.force_reannounce(0,static_cast<int>(index),
            lt::torrent_handle::ignore_min_interval|lt::torrent_handle::high_priority);
        return {};
      }
    }
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Shared tracker is absent from this actual seed admission"});
  }catch(const std::exception&){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot request shared seed announcement"});}
}
std::expected<void,storage::Error> announceSharedSeed(PersistentSession& owner,
    const storage::ProfileLease& lease,const storage::TorrentIntakeMetadata& metadata,
    const std::filesystem::path& destination) {
  return detail::PersistentCall::announceSharedSeed(owner,lease,metadata,destination);
}
std::vector<ManagedDownloadStatus> PersistentSession::managedDownloads() const {
  std::vector<ManagedDownloadStatus> result;
  if (!impl_->onWorker()) { return result; }
  result.reserve(impl_->managed.size());
  for (const auto& [key, value] : impl_->managed) { static_cast<void>(key); result.push_back(value->status); }
  return result;
}
std::optional<core::QueueOriginalScrapeResponse> PersistentSession::managedTrackerResponse(
    const core::TorrentHash& hash, std::uint64_t admission) const {
  if(!impl_->onWorker()||impl_->closed||impl_->poisoned||impl_->closingAdmission||!admission){return {};}
  const auto found=impl_->managed.find(hash.hex());if(found==impl_->managed.end()){return {};}
  const auto& value=*found->second;
  if(value.status.admission!=admission||!value.token||value.token->number!=admission||(value.trackerResponse&&value.trackerResponse->admission!=admission)
      ||!value.status.retained||value.status.failure||value.stop||value.removing
      ||!value.handle.is_valid()||!value.trackerResponse||!value.trackerResponse->sole()
      ||!value.trackerResponse->installed.load(std::memory_order_acquire)
      ||value.trackerResponse->lost.load(std::memory_order_acquire)){return {};}
  return value.trackerResponse->response->response(); // Existing poll owns every terminal drain.
}
std::optional<ManagedTrackerUrlResponse> PersistentSession::managedActiveUdpTrackerResponse(
    const core::TorrentHash& hash,std::uint64_t admission)const {
  if(!impl_->onWorker()||impl_->closed||impl_->poisoned||impl_->closingAdmission||!admission||impl_->mainlineScrapes.revoked(hash)){return {};}
  const auto found=impl_->managed.find(hash.hex());if(found==impl_->managed.end()){return {};}
  const auto& value=*found->second;
  if(!value.activeUdpTrackerScope||!value.token||value.token->number!=admission||value.status.admission!=admission
      ||!value.status.checked||!value.status.retained||value.status.failure||value.stop||value.removing||value.manualPaused||value.policyHeld
      ||value.status.verificationHeld||value.seedIntent||value.allocationIntent||!value.handle.is_valid()||!value.trackerResponse
      ||value.trackerResponse->admission!=admission||(value.status.phase!=ManagedPhase::Downloading&&value.status.phase!=ManagedPhase::Seeding)){return {};}
  auto current=value.trackerResponse->currentUdpTerminal();if(!current){return {};}
  return ManagedTrackerUrlResponse{std::move(current->url),std::move(current->response),current->terminal.terminal.observedWallMillis,current->terminal.sequence};
}
std::vector<ManagedTrackerUrlResponse> PersistentSession::managedTrackerUrlResponses(
    const core::TorrentHash& hash,std::uint64_t admission)const {
  if(!impl_->onWorker()||impl_->closed||impl_->poisoned||impl_->closingAdmission||!admission){return {};}
  const auto found=impl_->managed.find(hash.hex());if(found==impl_->managed.end()){return {};}
  const auto& value=*found->second;
  if(!value.token||value.token->number!=admission||value.status.admission!=admission||!value.status.retained||!value.status.checked
      ||value.status.failure||value.stop||value.removing||value.manualPaused||value.policyHeld||value.status.verificationHeld
      ||value.seedIntent||value.allocationIntent||!value.handle.is_valid()||!value.trackerResponse||value.trackerResponse->admission!=admission
      ||(value.status.phase!=ManagedPhase::Downloading&&value.status.phase!=ManagedPhase::Seeding)){return {};}
  std::vector<ManagedTrackerUrlResponse> result;
  for(auto& row:value.trackerResponse->urlTerminals()){
    result.push_back({std::move(row.url),std::move(row.response),row.terminal.terminal.observedWallMillis,row.terminal.sequence});
  }
  return result;
}
std::expected<void, AccountingError> PersistentSession::setObservationSink(ApplicationObservationSink sink) {
  if (!impl_->onWorker()) { return std::unexpected(AccountingError::WrongThread); }
  if (impl_->call) { return std::unexpected(AccountingError::ActiveOperation); }
  if (impl_->closed) { return std::unexpected(AccountingError::Closing); }
  impl_->sink = std::move(sink);
  return {};
}
void PersistentSession::clearObservationSink() noexcept {
  if (impl_->onWorker()) { impl_->sink = {}; }
}
std::optional<ApplicationPeerObservation> PersistentSession::latest() const {
  if (!impl_->onWorker()) { return {}; }
  return impl_->observation;
}
std::vector<MainlineScrapeEstimate> PersistentSession::mainlineScrapeEstimates() const {
  if(!impl_->onWorker()||impl_->closed||impl_->poisoned||impl_->closingAdmission||!impl_->mainline){return {};}
  return impl_->mainlineScrapes.estimates();
}
std::vector<LibraryTrackerResponse> PersistentSession::libraryTrackerResponses()const {
  if(!impl_->onWorker()||impl_->closed||impl_->poisoned||impl_->closingAdmission){return {};}
  auto result=impl_->regularTrackerViews;
  std::erase_if(result,[&](const auto& row){
    if(impl_->mainlineScrapes.revoked(row.hash)){return true;}
    if(!row.admission){return !impl_->libraryUdpUnadmitted(row.hash);}
    const auto entry=impl_->managed.find(row.hash.hex());if(entry==impl_->managed.end()){return true;}const auto& value=*entry->second;
    return !value.token||value.token->number!=row.admission||value.status.admission!=row.admission||!value.status.retained||!value.status.checked
        ||value.status.failure||value.stop||value.removing||value.manualPaused||value.policyHeld||value.status.verificationHeld
        ||value.seedIntent||value.allocationIntent||!value.handle.is_valid()||!value.trackerResponse||!value.trackerResponse->sole()
        ||!value.trackerResponse->installed.load(std::memory_order_acquire)||value.trackerResponse->lost.load(std::memory_order_acquire)
        ||(value.status.phase!=ManagedPhase::Downloading&&value.status.phase!=ManagedPhase::Seeding);
  });
  for(const auto& [key,value]:impl_->managed){(void)key;
    if(impl_->mainlineScrapes.revoked(value->status.hash)){continue;}
    const auto selected=managedActiveUdpTrackerResponse(value->status.hash,value->status.admission);
    if(selected&&selected->response.type!=core::QueueOriginalScrapeType::Unknown&&selected->response.content){
      result.push_back({value->status.hash,selected->response,selected->observedWallMillis,value->status.admission});
    }
  }
  return result;
}
void PersistentSession::revokeMainlineScrape(const core::TorrentHash& hash) noexcept {
  if(!impl_->onWorker()){return;}
  std::erase_if(impl_->mainlineStatus.admittedScrapeCandidates,[&](const auto& row){return row.hash==hash;});
  impl_->cancelLibraryUdp(&hash);impl_->cancelLibraryHttp(&hash);
  std::erase_if(impl_->regularTrackerViews,[&](const auto& row){return row.hash==hash;});
  if(!impl_->mainline){impl_->mainlineScrapes.revoke(hash);return;}
  try{
    const auto active=impl_->mainlineScrapes.activeTarget();
    if(active&&active->hash==hash&&impl_->mainlineScrapes.active()&&impl_->mainline){
      static_cast<void>(impl_->mainline->cancelScrape(*impl_->mainlineScrapes.active()));
    }
  }catch(...){}
  impl_->mainlineScrapes.revoke(hash);
}
std::expected<void,storage::Error> PersistentSession::rearmMainlineScrape(const core::TorrentHash& hash) {
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||(!impl_->mainline&&!impl_->libraryRegularTrackerMode())||!impl_->mainlineLease){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Mainline saved-intent rearm is unavailable"});
  }
  revokeMainlineScrape(hash); // Cancel any old cookie before fresh authority IO.
  // This is independent fresh saved authority; no caller-supplied scope or
  // row cache can clear a failed command's tombstone by itself.
  auto fresh=impl_->libraryRegularTrackerMode()?impl_->regularTrackerTargets()
      :readSavedMainlineScrapeTargets(*impl_->mainlineLease);
  if(!fresh){impl_->mainlineScrapeFailure=fresh.error();return std::unexpected(fresh.error());}
  impl_->mainlineScrapes.settled(hash);impl_->mainlineScrapeRefresh={};impl_->regularTrackerRefresh={};return {};
}
std::optional<core::TrackerStatistics> PersistentSession::trackerStatistics() const noexcept {
  if (!impl_ || !impl_->onWorker()) { return std::nullopt; }
  const auto policy = impl_->network && impl_->network->mode == NetworkMode::Normal
      ? core::TrackerPolicy::Permitted : core::TrackerPolicy::DisabledByIsolation;
  for (const auto& [key, entry] : impl_->managed) {
    static_cast<void>(key);
    if (entry->handle.is_valid()) { return sampleTrackerStatistics(entry->handle, policy); }
  }
  return sampleTrackerStatistics(impl_->active, policy);
}
std::optional<core::PeerPayloadSnapshot> PersistentSession::peerPayloads() const noexcept {
  if (!impl_->onWorker() || !impl_->peerPayloadLedger) { return std::nullopt; }
  return impl_->peerPayloadLedger->snapshot();
}
std::shared_ptr<const core::RuntimeMetrics> PersistentSession::diagnostics() const noexcept {
  if (!impl_->onWorker()) { return {}; }
  return impl_->runtimeMetrics.latest();
}
std::optional<ListenerFailureObservation> PersistentSession::lastListenerFailure() const noexcept {
  return impl_->onWorker()?impl_->listenerFailure:std::nullopt;
}
std::optional<RetirementReceipt> PersistentSession::lastRetirement() const {
  if (!impl_->onWorker()) { return {}; }
  return impl_->retirement;
}
std::expected<void, AccountingError> PersistentSession::poll() {
  if (!impl_->onWorker()) { return std::unexpected(AccountingError::WrongThread); }
  if (impl_->call) { return std::unexpected(AccountingError::ActiveOperation); }
  if (impl_->closed) { return std::unexpected(AccountingError::Closing); }
  if (impl_->poisoned) { return std::unexpected(AccountingError::OwnerResetRequired); }
  if (!impl_->session&&!impl_->managedActive()) { return {}; } // Allocation remains independent of LT/network startup.
  impl_->call = true;
  try {
    if(impl_->session){static_cast<void>(impl_->pop());}
    impl_->advanceManaged();
    if(impl_->session){impl_->advanceMagnets();}
    impl_->advanceMainline();
    if (impl_->transferError || impl_->removalLost) {
      impl_->counterError(AccountingError::Engine);
      impl_->joinPoisoned();
    } else if(impl_->session) { impl_->post(false); }
  } catch (...) { impl_->counterError(AccountingError::Engine); }
  // An idle failure has no later operation-phase publication. Advance the
  // immutable status once, retaining the last actual sample and its timestamp.
  // Keep the call guard through the optional sink, as with normal sampling.
  if (impl_->counterFailure && (!impl_->observation
      || impl_->observation->state != AccountingState::Uncertified)) { impl_->publish(); }
  impl_->call = false;
  auto starts = std::move(impl_->autoStarts); impl_->autoStarts.clear();
  if (impl_->magnetLease && !impl_->poisoned && !impl_->closingAdmission) {
    for (const auto& options : starts) {
      auto admitted = startManaged(*impl_->magnetLease, options);
      if (!admitted) {
        const auto found = impl_->magnets.find(options.hash.hex());
        if (found != impl_->magnets.end()) { found->second->status.failure = admitted.error(); }
      }
    }
  }
  if (impl_->counterFailure) { return std::unexpected(*impl_->counterFailure); }
  return {};
}
CloseWithoutCheckpointReceipt PersistentSession::closeWithoutCheckpoint(Clock::time_point deadline)noexcept{
  if(!impl_->onWorker()){return {false,false,AccountingError::WrongThread};}
  if(impl_->call){return {false,false,AccountingError::ActiveOperation};}
  if(impl_->drainReceipt){return *impl_->drainReceipt;}
  impl_->call=true;impl_->closingAdmission=true;impl_->phase=AccountingState::Closing;
  for(auto& [key,m]:impl_->managed){(void)key;m->historyTaken=true;m->historyCompletionMillis.reset();m->historyCompletionProblem.reset();m->historyLease.reset();}
  impl_->historyConsumerLease.reset();
  impl_->magnetCompletionHook={};impl_->magnetCompletionLease.reset();
  impl_->sink={};impl_->operationSink={};impl_->operationBaseline.reset();
  std::optional<AccountingError> diagnostic;
  // Stop optional actors WITHOUT their persistence paths. No fresh source
  // observation, authentication callback, metadata import or queue evaluation.
  impl_->retireMainlinePorts();impl_->closeMainline();impl_->mainlineStatus.state=MainlineDiscoveryStatus::State::Closed;
  impl_->libraryHttp.close();impl_->libraryUdp.cancel();
  for(auto& [key,m]:impl_->managed){(void)key;if(m->mixedLifetime){m->mixedLifetime->attached=false;m->mixedLifetime->retained=false;}}
  for(auto& [key,m]:impl_->magnets){(void)key;if(m->pendingLifetime){m->pendingLifetime->authorized=false;m->pendingLifetime->retiring=true;m->pendingLifetime->retained=false;}}
  if(impl_->mixedTransport){impl_->mixedTransport->retire();impl_->mixedTransport.reset();}
  try{
    if(impl_->session){
      const auto end=std::min(deadline,Clock::now()+CertificationBudget);
      // Sole owner-thread drain, mutually exclusive with normal pop()/poll().
      // Drop older responses, then request real payload flushes. These alerts
      // are diagnostic effort only: they do NOT mint a causal saved-piece seal.
      std::vector<lt::alert*> alerts;impl_->session->pop_alerts(&alerts);
      std::map<lt::torrent_handle,bool> waiting;
      for(const auto& [key,m]:impl_->managed){(void)key;if(Clock::now()>=end){diagnostic=AccountingError::Deadline;break;}if(m->handle.is_valid()){waiting.emplace(m->handle,false);}}
      if(impl_->registered&&impl_->active.is_valid()){waiting.emplace(impl_->active,false);}
      impl_->session->pause();
      for(const auto& [handle,done]:waiting){(void)done;if(Clock::now()>=end){diagnostic=AccountingError::Deadline;break;}handle.pause();handle.save_resume_data(lt::torrent_handle::flush_disk_cache);}
      const auto pending=[&]{return std::any_of(waiting.begin(),waiting.end(),[](const auto& item){return !item.second;});};
      while(pending()&&Clock::now()<end){
        impl_->session->pop_alerts(&alerts);
        if(alerts.size()>4096){diagnostic=AccountingError::StorageFenceLost;break;}
        for(auto* alert:alerts){
          if(const auto* saved=lt::alert_cast<lt::save_resume_data_alert>(alert)){if(auto found=waiting.find(saved->handle);found!=waiting.end()){found->second=true;}}
          if(lt::alert_cast<lt::save_resume_data_failed_alert>(alert)||lt::alert_cast<lt::file_error_alert>(alert)
              ||lt::alert_cast<lt::alerts_dropped_alert>(alert)){diagnostic=AccountingError::StorageFenceLost;}
        }
        if(pending()){std::this_thread::sleep_for(20ms);}
      }
      if(pending()){diagnostic=AccountingError::Deadline;}
    }
  }catch(...){diagnostic=AccountingError::Engine;}
  // Mandatory actual abort/join even after flush failure/deadline. Do not use
  // retire/advanceManaged/joinPoisoned: all can publish profile state/settlement.
  // SDK disk shutdown retains the original disk objects through its callbacks.
  impl_->session.reset();
  impl_->active={};impl_->registered=false;impl_->outstanding=false;
  impl_->managedByHandle.clear();impl_->managed.clear();impl_->magnets.clear();
  impl_->autoStarts.clear();impl_->importPeers.clear();
  impl_->mixedBinder={};impl_->targetPublication={};impl_->positionsPublication={};
  impl_->releaseRegularTrackerLease();impl_->managedLease=nullptr;impl_->magnetLease=nullptr;
  impl_->externalSnapshotValid=false;impl_->closed=true;impl_->poisoned=true;impl_->phase=AccountingState::Closed;
  impl_->retirement=RetirementReceipt{impl_->operation,false,{},AccountingError::OwnerResetRequired};
  if(!impl_->counterFailure){impl_->counterFailure=AccountingError::OwnerResetRequired;}
  impl_->publish(); // Owning Uncertified observation only; both sinks are clear.
  impl_->call=false;
  impl_->drainReceipt=CloseWithoutCheckpointReceipt{true,true,diagnostic};return *impl_->drainReceipt;
}
CloseReceipt PersistentSession::close(Clock::time_point deadline) noexcept {
  if (!impl_->onWorker()) { return {{0, false, {}, AccountingError::WrongThread}, false}; }
  if (impl_->call) { return {{0, false, {}, AccountingError::ActiveOperation}, false}; }
  if (impl_->closed) { return {impl_->retirement.value_or(RetirementReceipt{}), true}; }
  impl_->call = true;
  impl_->closingAdmission = true;
  impl_->phase = AccountingState::Closing;
  impl_->magnetCompletionHook={};impl_->magnetCompletionLease.reset();
  const auto result = impl_->retire(std::min(deadline, Clock::now() + CertificationBudget));
  impl_->persistMainline();
  impl_->retireMainlinePorts();
  impl_->sink = {}; impl_->operationSink = {};
  impl_->closeMainline();
  impl_->mainlineStatus.state=MainlineDiscoveryStatus::State::Closed;
  for(auto& [key,magnet]:impl_->magnets){(void)key;if(magnet->pendingLifetime){magnet->pendingLifetime->attached=false;magnet->pendingLifetime->retained=false;}}
  for(auto& [key,member]:impl_->managed){(void)key;if(member->mixedLifetime){member->mixedLifetime->attached=false;member->mixedLifetime->retained=false;}}
  if(impl_->mixedTransport){impl_->mixedTransport->retire();impl_->mixedTransport.reset();}
  impl_->session.reset(); // Join may outlive the certification effort deadline.
  impl_->releaseRegularTrackerLease();
  impl_->registered = false; impl_->outstanding = false;
  impl_->closed = true; impl_->phase = AccountingState::Closed;
  impl_->publish(); // Sink is cleared: update immutable state, no shutdown callback.
  impl_->call = false;
  std::optional<storage::Error> checkpointFailure;
  for (const auto& [key, entry] : impl_->managed) {
    static_cast<void>(key);
    if (entry->status.failure) { checkpointFailure = entry->status.failure; break; }
  }
  if (!checkpointFailure) {
    for (const auto& [key, entry] : impl_->magnets) {
      static_cast<void>(key);
      if (entry->status.failure) { checkpointFailure = entry->status.failure; break; }
    }
  }
  return {result, true, std::move(checkpointFailure)};
}

namespace detail {
PersistentCall::PersistentCall(PersistentSession& owner, const storage::ProfileLease& lease) : owner_(owner) {
  auto& impl = *owner_.impl_;
  require(impl.onWorker(), "Persistent owner called from another thread", storage::ErrorCode::Conflict);
  require(!impl.call && !impl.closed && !impl.poisoned && !impl.managedActive() && !impl.magnetActive(),
      "Persistent owner is active, closed or requires reset", storage::ErrorCode::Conflict);
  require(impl.operation != std::numeric_limits<std::uint64_t>::max(),
      "Persistent operation identity exhausted", storage::ErrorCode::Conflict);
  impl.bind(lease); // Reject a foreign domain BEFORE any compatible state/cache IO.
  impl.retirement.reset(); // Never expose an old operation seal after failed prepare.
  ++impl.operation;
  impl.call = true;
}
PersistentCall::~PersistentCall() {
  auto& impl = *owner_.impl_;
  impl.operationSink = {}; impl.operationBaseline.reset();
  if (impl.registered && impl.session) { static_cast<void>(impl.retire(Clock::now() + CertificationBudget)); }
  impl.call = false;
}
PersistentOperation::PersistentOperation(PersistentSession& owner,
    const storage::ProfileLease& lease, const LocalOptions& options) : owner_(owner), options_(options) {
  try { owner_.impl_->prepare(lease, options); }
  catch (...) {
    owner_.impl_->operationSink = {}; owner_.impl_->operationBaseline.reset();
    throw;
  }
}
PersistentOperation::~PersistentOperation() {
  if (!retired_) { static_cast<void>(owner_.impl_->retire(Clock::now() + CertificationBudget)); }
}
lt::torrent_handle PersistentOperation::add(lt::add_torrent_params parameters) {
  require(!options_.stop.stop_requested(), "Local session cancelled", storage::ErrorCode::Cancelled);
  return owner_.impl_->add(std::move(parameters));
}
void PersistentOperation::request() noexcept { owner_.impl_->post(true); }
std::vector<lt::alert*> PersistentOperation::pump() {
  auto& impl = *owner_.impl_;
  auto pending = impl.pop();
  require(!impl.transferError && !impl.removalLost,
      "Persistent engine reported a transfer/storage error; original state requires recheck", storage::ErrorCode::Io);
  impl.post(false);
  return pending;
}
bool PersistentOperation::flushed(const std::vector<lt::alert*>& pending) const noexcept {
  for (auto* alert : pending) {
    if (auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert);
        saved && owner_.impl_->matches(saved->handle)) { return true; }
  }
  return false;
}
void PersistentOperation::retire() {
  if (retired_) { return; }
  const auto result = owner_.impl_->retire(Clock::now() + CertificationBudget);
  retired_ = true;
  require(result.storageReleased,
      "Persistent storage retirement was not certified; completion was not published", storage::ErrorCode::Io);
}
} // namespace detail
} // namespace biglybtc::engine

namespace biglybtc::engine {
bool PersistentSession::mixedNetworksAvailable()noexcept{return bool(detail::mixedI2pTransportRuntimePin());}
std::expected<void,storage::Error> PersistentSession::configureMixedNetworks(){
  if(!impl_->onWorker()||impl_->call||impl_->session||impl_->closed||impl_->poisoned||!mixedNetworksAvailable()){
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed Public+I2P runtime mode is unavailable"});
  }
  try{impl_->mixedOrigin=std::make_shared<detail::MixedManagedOrigin>();}
  catch(const std::bad_alloc&){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot retain mixed engine owner origin"});}
  impl_->mixedNetworks=true;return {};
}
std::expected<void,storage::Error> PersistentSession::configureMixedSourceBinding(MixedSourceBinding binding){
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||!impl_->managed.empty()){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Configure mixed source binding before managed admissions"});
  }
  impl_->mixedBinder=std::move(binding);return {};
}
std::expected<std::shared_ptr<const MixedManagedSource>,storage::Error> PersistentSession::mixedSource(const core::TorrentHash& hash)const{
  const auto found=impl_->managed.find(hash.hex());
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||found==impl_->managed.end()||!found->second->mixedSource||!found->second->mixedSource->backendCurrent()){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"No actual retained mixed admission"});
  }
  return found->second->mixedSource;
}
std::expected<std::shared_ptr<MixedI2pTransport>,storage::Error> PersistentSession::mixedTransport(){
  if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||!impl_->mixedNetworks||!impl_->session){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Mixed transport needs an actual retained Public session"});
  }
  if(!impl_->mixedTransport){
    auto pin=detail::mixedI2pTransportRuntimePin();if(!pin){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed I2P SDK cohort is unavailable"});}
    auto state=std::make_shared<MixedI2pTransport::State>();state->owner=impl_->session.get();state->pin=std::move(pin);state->origin=impl_->mixedOrigin;
    impl_->mixedTransport=std::shared_ptr<MixedI2pTransport>(new MixedI2pTransport(std::move(state)));
  }
  return impl_->mixedTransport;
}
}

namespace biglybtc::engine {
std::expected<ChatTransportFactory,storage::Error> PersistentSession::chatTransport(const storage::ProfileLease& lease){
#ifdef BIGLYBTC_ENGINE_SHARED_CHAT
 if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||!impl_->session||!impl_->networkBinding||!impl_->network||impl_->network->mode!=NetworkMode::Normal){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Public chat requires the current persistent Public session"});}
 try{impl_->bind(lease);auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}const libtorrent::session_handle session=*impl_->session;const auto port=impl_->networkBinding->effective.listenPort;
  if(port<=0||port>65535){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Public chat requires its configured UDP port"});}
  return ChatTransportFactory{std::move(*retained),[session,port]()->std::expected<std::shared_ptr<services::shared_udp::Provider>,services::azureus_udp::Failure>{auto attached=services::shared_udp::Provider::attach(session,static_cast<quint16>(port));if(!attached){return std::unexpected(attached.error());}return std::shared_ptr<services::shared_udp::Provider>(std::move(*attached));},false};
 }catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Could not retain Public chat transport"});}
#else
 (void)lease;return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"This runtime does not include Public chat transport"});
#endif
}
}
