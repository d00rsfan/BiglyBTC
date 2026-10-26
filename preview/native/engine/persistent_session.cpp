#include "persistent_session_support.hpp"
#include "managed_payload_counters.hpp"
#include "tracker_statistics_sampler.hpp"
#include "persistent_runtime_guard.hpp"
#include "runtime_metrics_sampler.hpp"
#include "effective_rate_limits.hpp"
#include "peer_payload_observer.hpp"
#include "managed_storage.hpp"
#include "library_commands.hpp"
#include "queue_positions.hpp"
#include "magnet_intake.hpp"
#include "torrent_intake.hpp"
#include "metadata_disk_router.hpp"
#include "original_disk_io.hpp"
#include <libtorrent/session_params.hpp>
#include <libtorrent/write_resume_data.hpp>
#include "network_policy.hpp"
#include "network_binding.hpp"
#include "biglybtc/queue_rate_providers.hpp"
#include <libtorrent/torrent_status.hpp>
#include <map>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
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

struct PersistentSession::Impl {
  struct OperationToken { std::uint64_t number = 0; } token;
  struct Managed {
    std::unique_ptr<OperationToken> token;
    detail::ManagedStorage storage;
    std::shared_ptr<detail::ManagedPayloadCounters> payloadCounters = std::make_shared<detail::ManagedPayloadCounters>();
    lt::torrent_handle handle;
    ManagedDownloadStatus status;
    bool manualPaused = false, forceStart = false, stop = false;
    bool policyHeld = false, completedTransition = false, completionHandled = false;
    core::QueueActivityMemory activity;
    std::optional<core::QueueFilePriorityStats> filePriority;
    std::optional<std::int64_t> queueObservedMillis;
    std::optional<core::QueueProbeScope> probeScope;
    std::unique_ptr<core::QueueTargetEta> targetEta;
    std::uint64_t targetStatisticsSequence=0;
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
    std::optional<core::QueueSwarm> swarm;
    int connectedLeechers = 0;
    std::optional<std::uint32_t> queuePosition;
    std::optional<bool> positionTransition;
    std::uint64_t positionTransitionOrder=0;
    bool positionTransitionLost=false;
    std::int64_t remainingBytes = 0, badPayloadBytes = 0;
    bool flushed = false, removing = false, removed = false;
    Clock::time_point deadline{};
    std::vector<std::uint8_t> verified;
    Managed(std::uint64_t id, detail::ManagedStorage files, const core::TorrentHash& hash)
        : token(std::make_unique<OperationToken>(OperationToken{id})), storage(std::move(files)),
          status{.hash = hash, .admission = id} {}
  };
  std::map<std::string, std::unique_ptr<Managed>> managed;
  std::map<lt::torrent_handle, Managed*> managedByHandle;
  const storage::ProfileLease* managedLease = nullptr; // Same bound lease, owned by Host.
  std::optional<NetworkPolicy> network;
  std::optional<NetworkBinding> networkBinding;
  std::optional<LiveQueueLimits> liveQueueLimits;
  std::optional<core::QueuePolicy> queuePolicy;
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
  bool appliedSeedingOnly = false;
  struct Magnet {
    std::unique_ptr<OperationToken> token;
    PendingMagnet intent;
    lt::torrent_handle handle;
    PendingMagnetStatus status;
    Clock::time_point expires{}, deadline{};
    bool removing = false, removed = false, cancel = false, shutdown = false;
    std::string metadata;
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
  std::optional<ApplicationPeerObservation> observation{};
  ApplicationObservationSink sink{};
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
    } else {
      queueGlobalAverage = {};
      queueGlobalReceiveKiB.reset(); queueGlobalObservedMillis.reset();
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
    session->pop_alerts(&pending); // The ONLY pop_alerts consumer for this owner.
    if (pending.size() > 4096) {
      removalLost = true; transferError = true;
      counterError(AccountingError::CounterResponseLost);
      return pending;
    }
    // Record losses/errors before publishing any optional callback. No borrowed
    // alert storage escapes this turn. A same-hash predecessor cannot match the
    // active weak ownership identity or its operation token.
    for (auto* alert : pending) {
      if (!alert) {
        removalLost = true; transferError = true;
        counterError(AccountingError::CounterResponseLost); continue;
      }
      if (auto* dropped = lt::alert_cast<lt::alerts_dropped_alert>(alert)) {
        if (dropped->dropped_alerts.test(lt::session_stats_alert::alert_type)) {
          counterError(AccountingError::CounterResponseLost);
        }
        if (dropped->dropped_alerts.test(lt::session_error_alert::alert_type)) {
          // An unattributed async engine exception may have affected removal or
          // storage, not just stats. No optional-request attribution is safe.
          transferError = true; removalLost = true;
          counterError(AccountingError::Engine);
        }
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
      if (lt::alert_cast<lt::listen_failed_alert>(alert)) { transferError = true; }
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
      if (const auto* state = lt::alert_cast<lt::state_changed_alert>(alert)) {
        value.advanceTimeline(state->timestamp()); value.timelineState=state->state;
      }
      if (const auto* paused = lt::alert_cast<lt::torrent_paused_alert>(alert)) {
        value.advanceTimeline(paused->timestamp()); value.timelinePaused=true;
      }
      if (const auto* resumed = lt::alert_cast<lt::torrent_resumed_alert>(alert)) {
        value.advanceTimeline(resumed->timestamp()); value.timelinePaused=false;
      }
      if (queuePolicy) {
        if (const auto* scrape = lt::alert_cast<lt::scrape_reply_alert>(alert);
            scrape && scrape->complete>=0 && scrape->incomplete>=0) {
          int seeds=scrape->complete, peers=scrape->incomplete;
          if(value.status.phase==ManagedPhase::Seeding&&value.trackerSeedingSince
              &&scrape->timestamp()>*value.trackerSeedingSince&&seeds>0){--seeds;}
          if(value.status.phase==ManagedPhase::Downloading&&value.trackerRunningSince
              &&scrape->timestamp()>*value.trackerRunningSince&&peers>0){--peers;}
          value.swarm=core::QueueSwarm{seeds,peers,true};
        }
        if (const auto* announce = lt::alert_cast<lt::tracker_reply_alert>(alert);
            announce && value.handle.is_valid() && !value.stop) {
          const auto observed=value.handle.status(lt::status_flags_t{});
          if(observed.num_complete>=0&&observed.num_incomplete>=0){
            // Announce fallback in the original does not subtract self. These
            // are observed tracker totals; connected peers are a separate input.
            value.swarm=core::QueueSwarm{observed.num_complete,observed.num_incomplete,true};
          }
        }
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
    operationSink = {};
    poisoned = true;
    phase = AccountingState::Uncertified;
    // Not bounded by the certification deadline. No callback is made by LT
    // destruction, and no joined tail is relabeled as a final counter sample.
    session.reset();
    for (auto& [key, entry] : managed) {
      static_cast<void>(key); entry->status.retained = false;
      entry->status.diskReadLatencyMillis.reset();entry->status.diskWriteLatencyMillis.reset();
      entry->status.staleRankOffset.reset();
      entry->status.phase = ManagedPhase::Failed;
      entry->status.queueTargetStatistics.reset(); entry->targetEta.reset();
      entry->status.failure = storage::Error{storage::ErrorCode::Io,
          "Engine owner joined after failure; original pieces require recheck"};
    }
    for (auto& [key, entry] : magnets) {
      static_cast<void>(key); entry->status.retained = false; entry->status.phase = MagnetPhase::Failed;
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
    detail::require(bindingHealthy(), "Explicit network binding was lost", storage::ErrorCode::Io);
  }
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
        if (queuePolicy) {
          settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::tracker
              | lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage));
        } else settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error
            | lt::alert_category::status | lt::alert_category::storage));
        lt::session_params parameters(settings);
        parameters.disk_io_constructor = detail::metadataDiskConstructor(diskPolicy);
        session = std::make_unique<lt::session>(std::move(parameters));
        session->pause();
        rateLimits = detail::readEffectiveRateLimits(*session); // Optional read, never applies settings.
        phase = AccountingState::Idle;
        mapCounters();
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
      if (entry->handle.is_valid() || (entry->removing && !entry->removed)) { return true; }
    }
    return false;
  }
  void sampleManaged(Managed& value, bool pieces = false) {
    value.status.diskReadLatencyMillis.reset();value.status.diskWriteLatencyMillis.reset();
    value.status.staleRankOffset.reset();
    if (!value.handle.is_valid() || value.removing) {
      value.status.queueTargetStatistics.reset(); value.targetEta.reset(); return;
    }
    const bool originalProgress=value.storage.originalProgressRequiresPieces();
    const bool priorityObservation=queuePolicy&&queuePolicy->downloadOrder==core::QueueDownloadOrder::FilePriority;
    const bool needPieces=pieces||originalProgress||priorityObservation;
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
      actual = value.payloadCounters->combine(*epoch, sample.total_payload_download, sample.total_payload_upload, protocol);
      if (actual.state != detail::ManagedPayloadCounters::Read::Retry) { break; }
    }
    if (actual.state != detail::ManagedPayloadCounters::Read::Accepted) {
      value.status.failure = storage::Error{storage::ErrorCode::Io,
          "Exact managed transfer counters are unavailable; saved state requires recheck"};
      requestStop(value);
      value.status.phase = ManagedPhase::Failed;
      return; // Never checkpoint guessed/zero/tick-lagged transfer totals.
    }
    value.status.retained = true;
    value.status.completed = sample.total_done;
    value.status.bytes = value.storage.originalBytes();
    value.status.received = actual.received;
    value.status.sent = actual.sent;
    value.status.combinedReceived=actual.combinedReceived;value.status.combinedSent=actual.combinedSent;
    if(!value.probeScope){value.probeScope=actual.combinedReceived?core::QueueProbeScope::NativeCombined:core::QueueProbeScope::NativePayload;}
    value.status.downloadRate = sample.download_payload_rate;
    value.status.uploadRate = sample.upload_payload_rate;
    value.status.pieces = sample.num_pieces;
    value.remainingBytes = std::max<std::int64_t>(0, sample.total_wanted - sample.total_wanted_done);
    value.badPayloadBytes = std::max<std::int64_t>(0, sample.total_failed_bytes)
        + std::max<std::int64_t>(0, sample.total_redundant_bytes);
    const auto now = Clock::now();
    value.queueObservedMillis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if(!value.stop&&!value.status.failure){
      if(const auto mapped=value.storage.mappedFiles()){
        // Same accepted target-status pipeline, matching retained descriptor
        // owner. A later completion race is unavailable until a future sample.
        const auto latency=mapped->diskLatency().snapshot(*value.queueObservedMillis);
        value.status.diskReadLatencyMillis=latency.readMillis;
        value.status.diskWriteLatencyMillis=latency.writeMillis;
      }
    }
    const bool transferring = !(sample.flags & lt::torrent_flags::paused)
        && sample.state != lt::torrent_status::checking_files
        && sample.state != lt::torrent_status::checking_resume_data && !value.stop;
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
    value.projectTimeline(lt::clock_type::now());
    if (sample.last_upload.time_since_epoch().count() > 0) {
      value.idleUploadSeconds = std::max<std::int64_t>(0,
          std::chrono::duration_cast<std::chrono::seconds>(lt::clock_type::now() - sample.last_upload).count());
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
    value.status.checked = sample.state != lt::torrent_status::checking_files
        && sample.state != lt::torrent_status::checking_resume_data
        && sample.state != lt::torrent_status::downloading_metadata;
    value.status.selectedComplete = sample.is_finished;
    if (needPieces) {
      value.verified.assign(static_cast<std::size_t>(value.status.totalPieces), 0);
      for (int i = 0; i < sample.pieces.size() && i < value.status.totalPieces; ++i) {
        value.verified[static_cast<std::size_t>(i)] = sample.pieces[lt::piece_index_t{i}] ? 1 : 0;
      }
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
    value.status.queueTargetStatistics.reset(); value.targetEta.reset();
    value.status.diskReadLatencyMillis.reset();value.status.diskWriteLatencyMillis.reset();
    value.status.staleRankOffset.reset();
    if (value.stop || !value.handle.is_valid()) { return; }
    value.stop = true;
    value.status.phase = ManagedPhase::Stopping;
    value.deadline = Clock::now() + CertificationBudget;
    value.handle.unset_flags(lt::torrent_flags::auto_managed);
    value.handle.pause();
    value.handle.save_resume_data(lt::torrent_handle::flush_disk_cache);
  }
  void normalizeManagedPositions() noexcept {
    if(queuePositionsBlocked||!queuePositionsPending||!queuePolicy||!managedLease||managed.empty()){return;}
    // Deferred checking is not an unknown=complete guess and performs no IO.
    for(const auto& [key,value]:managed){
      static_cast<void>(key);
      if(!value->handle.is_valid()||!value->status.retained||!value->status.checked
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
      const auto normalized=storage::normalizeLibraryPositions(*managedLease,observations,transitions,queuePolicy->newSeedsMoveTop,&saveAttempted);
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
  void applyQueue() {
    normalizeManagedPositions();
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
      if (!value.handle.is_valid() || value.removing || value.stop || value.status.failure) { continue; }
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
      if (value.status.received <= std::numeric_limits<std::int64_t>::max() - saved.downloaded
          && value.status.sent <= std::numeric_limits<std::int64_t>::max() - saved.uploaded) {
        auto received = saved.downloaded + value.status.received;
        received -= std::min(received, saved.discarded);
        received -= std::min(received, saved.hashFailed);
        received -= std::min(received, value.badPayloadBytes);
        const auto sent = saved.uploaded + value.status.sent;
        if (received <= 0) { item.shareRatio = -1; }
        else {
          const auto ratio = static_cast<long double>(sent) * 1000 / received;
          item.shareRatio = static_cast<std::int32_t>(std::min<long double>(ratio,
              std::numeric_limits<std::int32_t>::max()));
        }
      }
      if (value.secondsDownloading <= std::numeric_limits<std::int64_t>::max() - saved.secondsDownloading) {
        item.secondsDownloading = saved.secondsDownloading + value.secondsDownloading;
      }
      if (value.secondsSeeding <= std::numeric_limits<std::int64_t>::max() - saved.secondsSeeding) {
        item.secondsSeeding = saved.secondsSeeding + value.secondsSeeding;
      }
      item.secondsSinceLastUpload = value.idleUploadSeconds;
      item.runningMillis = value.runningSince ? millis(now - *value.runningSince) : 0;
      item.seedingMillis = value.seedingSince ? millis(now - *value.seedingSince) : 0;
      item.swarm = value.swarm;
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
    auto plan = core::evaluateQueue(*queuePolicy, facts, context);
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
        if (!value.handle.is_valid() || value.stop || value.removing || value.status.failure) { continue; }
        value.activity = decision.activity;
        value.status.staleRankOffset=decision.staleRankOffset;
        value.status.queueReason = decision.reason;
        value.status.seedingRank = decision.seedingRank;
        value.status.firstPriority = decision.firstPriority;
        value.status.unavailableQueueInputs = plan->unavailableInputs;
        if(queuePositionsUnavailable){value.status.unavailableQueueInputs|=1U<<5;}
        value.status.queuePositionFailure=queuePositionFailure;
        if (decision.clearForceStart && value.forceStart && managedLease) {
          auto saved = storage::clearLibraryForceStart(*managedLease, value.status.hash);
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

  void advanceManaged() {
    for (auto& [key, entry] : managed) {
      static_cast<void>(key);
      auto& value = *entry;
      sampleManaged(value);
      if (!value.stop || (!value.handle.is_valid() && !value.removing)) { continue; }
      if (value.flushed && !value.removing) {
        sampleManaged(value, true);
        session->remove_torrent(value.handle); // Retain token/storage until exact weak expiration.
        value.removing = true;
      }
      if (value.removing && value.removed && !value.handle.is_valid()) {
        if(value.timelineLost && !value.status.failure) {
          value.status.failure=storage::Error{storage::ErrorCode::Io,"Original active-time observations were lost; saved state requires recheck"};
        }
        if (!value.status.failure && managedLease) {
          value.projectTimeline(lt::clock_type::now());
          auto saved = value.storage.checkpoint(*managedLease, value.verified, value.status.complete,
              value.status.received, value.status.sent, value.secondsDownloading, value.secondsSeeding);
          if (!saved) { value.status.failure = saved.error(); }
        }
        value.status.retained = false;
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
    if (queuePolicy && !closingAdmission && !registered && !poisoned) { applyQueue(); }
    if (!registered && !managedActive() && !magnetActive() && !closed && !poisoned) { phase = AccountingState::Idle; }
    if (network && session && !poisoned) {
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
              | (queuePolicy ? lt::alert_category::tracker : lt::alert_category_t{})));
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
        static_cast<void>(pop());
        advanceManaged();
        if (managedActive()) { std::this_thread::sleep_for(20ms); }
      }
      if (managedActive() || removalLost) { return false; }
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
            const auto hash = detail::take(completePendingMagnet(*magnetLease, value.intent, magnetProfile / filename, *network));
            value.status.phase = MagnetPhase::Imported;
            // These numeric hints were admitted under this owner's immutable
            // bind/proxy policy. Retain them across metadata and payload handles
            // (and a later same-owner Stop/Start), without bypassing LT routing.
            importPeers.insert_or_assign(hash.hex(), value.intent.peers);
            if (!value.intent.stopped && !closingAdmission) { autoStarts.push_back({hash, 0, value.intent.forceStart, false}); }
            value.metadata.clear(); value.metadata.shrink_to_fit();
          } else { value.status.phase = MagnetPhase::Resolving; } // Normal close preserves unresolved original intent.
        } catch (const detail::Failure& failure) { value.status.failure = failure.error; value.status.phase = MagnetPhase::Failed; }
        catch (...) {
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
  impl_->sink = {}; impl_->operationSink = {};
  impl_->session.reset();
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
  if (!impl_->onWorker() || impl_->call || impl_->session || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Configure networking before owner startup"});
  }
  auto settings = validateNetworkPolicy(policy);
  if (!settings) { return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,
      "Unsupported network setting: " + settings.error().field}); }
  impl_->network = policy;
  return {};
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
  impl_->call = true;
  struct Reset { Impl& value; ~Reset() { value.call = false; } } reset{*impl_};
  try {
    const LiveSettingsResult result{plan->restartFields, bool(impl_->session)};
    // Only the narrow pack reaches LT, and only after Host's successful save.
    // get_settings below is the same network-thread FIFO barrier after apply.
    if (impl_->session) { impl_->session->apply_settings(*settings); }
    impl_->network = std::move(plan->effectivePolicy);
    impl_->liveQueueLimits = plan->queues;
    if (!impl_->session) { return result; }
    const auto actual = liveSettings();
    const auto expectedUpload = core::effectiveSessionRateLimit(settings->get_int(lt::settings_pack::upload_rate_limit));
    const auto expectedDownload = core::effectiveSessionRateLimit(settings->get_int(lt::settings_pack::download_rate_limit));
    const bool confirmed = actual && actual->rateLimits.upload == expectedUpload && actual->rateLimits.download == expectedDownload
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
  impl_->queuePolicy = policy;
  impl_->queuePositionsPending=true;impl_->queuePositionsUnavailable=true;
  if (policy.downloadOrder != core::QueueDownloadOrder::Eta) {
    for (auto& [key, value] : impl_->managed) {
      static_cast<void>(key); value->status.queueTargetStatistics.reset(); value->targetEta.reset();
    }
  }
  impl_->targetSmoothingSeconds=core::sourceSmoothingPolicy(configuredSmoothingSeconds).windowSeconds;
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

std::expected<void, storage::Error> PersistentSession::startManaged(
    const storage::ProfileLease& lease, const ManagedDownloadOptions& options) {
  if (!impl_->onWorker() || impl_->call || impl_->registered || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed admission is unavailable"});
  }
  if (impl_->queuePolicy && static_cast<std::size_t>(std::ranges::count_if(impl_->managed,
      [](const auto& row){return row.second->status.retained||row.second->removing;})) >= core::MaximumQueueTorrents) {
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Original-rule queue capacity exceeded"});
  }
  const auto key = options.hash.hex();
  const auto previous=impl_->managed.find(key);
  if(previous!=impl_->managed.end()) {
    const auto& row=*previous->second;
    if(row.handle.is_valid()||row.removing||row.status.retained||row.status.phase!=ManagedPhase::Stopped
        ||!row.flushed||!row.removed||row.status.failure) {
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Torrent is already admitted or retirement is uncertified"});
    }
  }
  // Required raw counter support is admitted before any ManagedStorage
  // beginUpdate/create/truncate. Stock/old approved providers stay unsupported.
  const auto payloadBridge = detail::managedPayloadCounterBridge();
  if (!payloadBridge) {
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,
        "Managed counter/selected-swarm provider ABI is unavailable"});
  }
  impl_->call = true;
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
    auto storage = detail::take(detail::ManagedStorage::prepare(lease, options.hash, *impl_->network,claimed,retainedIdentities,payloadBridge));
    // A certified same-hash restart replaces its own old claim only after the
    // new read-only collision gate and complete preparation succeeded.
    if(previous!=impl_->managed.end()){impl_->managedByHandle.erase(previous->second->handle);impl_->managed.erase(previous);}
    impl_->ensureSession();
    auto value = std::make_unique<Impl::Managed>(++impl_->operation, std::move(storage), options.hash);
    value->manualPaused = options.manuallyPaused; value->status.manuallyPaused = options.manuallyPaused;
    auto parameters = value->storage.parameters();
    if(const auto mapped=value->storage.mappedFiles()) {
      const auto virtualPath=std::filesystem::path(parameters.save_path)/".BiglyBTC-original-storage"/(options.hash.hex()+"-"+std::to_string(impl_->operation));
      detail::done(impl_->diskPolicy->registerOriginalPath(virtualPath,mapped));parameters.save_path=virtualPath.string();
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
    parameters.userdata = value->token.get();
    detail::require(detail::attachManagedPayloadCounters(parameters, value->payloadCounters, payloadBridge),
        "Exact managed transfer counters require the approved source ABI", storage::ErrorCode::Unsupported);
    detail::attachPeerPayloadObserver(parameters, impl_->peerPayloadLedger, impl_->operation);
    parameters.flags |= lt::torrent_flags::paused | lt::torrent_flags::duplicate_is_error;
    if (impl_->queuePolicy || options.manuallyPaused) {
      parameters.flags &= ~lt::torrent_flags::auto_managed;
      parameters.flags |= lt::torrent_flags::stop_when_ready;
    }
    else if (options.forceStart) { parameters.flags &= ~lt::torrent_flags::auto_managed; }
    else { parameters.flags |= lt::torrent_flags::auto_managed; }
    value->forceStart = options.forceStart;
    auto [slot, inserted] = impl_->managed.emplace(key, std::move(value));
    detail::require(inserted, "Duplicate managed admission", storage::ErrorCode::Conflict);
    lt::error_code error;
    try { slot->second->handle = impl_->session->add_torrent(std::move(parameters), error); }
    catch (...) { impl_->joinPoisoned(); throw; }
    if (error || !slot->second->handle.is_valid()) {
      impl_->managed.erase(slot);
      throw detail::Failure{{storage::ErrorCode::Io, "Cannot add original torrent to the managed session"}};
    }
    try { impl_->managedByHandle.emplace(slot->second->handle,slot->second.get()); }
    catch (...) { impl_->joinPoisoned(); throw; }
    slot->second->status.retained = true;
    impl_->managedLease = &lease;
    impl_->queuePositionsPending=true;impl_->queuePositionsUnavailable=true;
    impl_->phase = AccountingState::Active;
    if (impl_->queuePolicy || options.manuallyPaused || !options.queued || options.forceStart) { slot->second->handle.resume(); }
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
std::expected<void, storage::Error> PersistentSession::pauseManaged(const core::TorrentHash& hash, bool paused) {
  if (!impl_->onWorker() || impl_->call || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed control is unavailable"});
  }
  if (!impl_->bindingHealthy()) { return std::unexpected(storage::Error{storage::ErrorCode::Io, "Explicit network binding was lost; restart the owner"}); }
  auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end() || !found->second->handle.is_valid() || found->second->stop) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Torrent is not active"});
  }
  auto& value = *found->second;
  value.manualPaused = paused;
  if (paused) {
    value.handle.unset_flags(lt::torrent_flags::auto_managed); value.handle.pause();
    if (impl_->queuePolicy) { impl_->sampleManaged(value); }
  }
  else {
    if (!impl_->queuePolicy) { value.handle.unset_flags(lt::torrent_flags::stop_when_ready); }
    if (!value.forceStart && !impl_->queuePolicy) { value.handle.set_flags(lt::torrent_flags::auto_managed); }
    if (impl_->queuePolicy) {
      value.handle.unset_flags(lt::torrent_flags::auto_managed);
      value.handle.pause(); value.policyHeld = true;
      impl_->sampleManaged(value); impl_->applyQueue();
    } else { value.handle.resume(); value.policyHeld = false; }
  }
  return {};
}
std::expected<void, storage::Error> PersistentSession::queueManaged(const core::TorrentHash& hash, bool forceStart, bool queued) {
  if (!managedAvailable() || impl_->call) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Managed queue control is unavailable"});
  }
  if (!impl_->bindingHealthy()) { return std::unexpected(storage::Error{storage::ErrorCode::Io, "Explicit network binding was lost; restart the owner"}); }
  const auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end() || !found->second->handle.is_valid() || found->second->stop) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Torrent is not active"});
  }
  auto& value = *found->second;
  value.manualPaused = false; value.forceStart = forceStart;
  if (!impl_->queuePolicy) { value.handle.unset_flags(lt::torrent_flags::stop_when_ready); }
  if (forceStart) { value.handle.unset_flags(lt::torrent_flags::auto_managed); value.handle.resume(); }
  else if (impl_->queuePolicy) {
    value.handle.unset_flags(lt::torrent_flags::auto_managed);
    value.handle.pause(); value.policyHeld = true;
  } else { value.handle.set_flags(lt::torrent_flags::auto_managed); if (queued) { value.handle.pause(); } else { value.handle.resume(); } }
  if (impl_->queuePolicy) { impl_->sampleManaged(value); impl_->applyQueue(); }
  return {};
}
std::expected<void, storage::Error> PersistentSession::stopManaged(const core::TorrentHash& hash) {
  if (!impl_->onWorker() || impl_->call || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed control is unavailable"});
  }
  const auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end()) { return {}; }
  try { impl_->requestStop(*found->second); return {}; }
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
  impl_->managedByHandle.erase(value.handle);
  impl_->managed.erase(found); // Releases ManagedStorage's path/file descriptors.
  impl_->queuePositionsPending=true;impl_->queuePositionsUnavailable=true;
  if (removePeerHints) { impl_->importPeers.erase(hash.hex()); }
  return {};
}
std::expected<core::TorrentHash,storage::Error> PersistentSession::createHybridCompanion(
    storage::ProfileLease& lease,const core::TorrentHash& source,storage::OriginalSelectedHash target,std::stop_token stop) {
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
    return storage::importHybridAlias(lease,source,target,retained,nullptr,stop);
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
  if (!managedAvailable() || impl_->call || impl_->closingAdmission) {
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
    parameters.userdata = value->token.get();
    detail::attachPeerPayloadObserver(parameters, impl_->peerPayloadLedger, admission);
    parameters.flags |= lt::torrent_flags::paused | lt::torrent_flags::duplicate_is_error;
    parameters.flags &= ~lt::torrent_flags::auto_managed;
    impl_->ensureSession();
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
std::expected<void, storage::Error> PersistentSession::cancelMagnet(const core::TorrentHash& hash) {
  if (!managedAvailable() || impl_->call) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Magnet cancellation is unavailable"});
  }
  const auto found = impl_->magnets.find(hash.hex());
  if (found == impl_->magnets.end()) { return std::unexpected(storage::Error{storage::ErrorCode::Missing, "Magnet is not admitted"}); }
  auto& value = *found->second;
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
std::vector<ManagedDownloadStatus> PersistentSession::managedDownloads() const {
  std::vector<ManagedDownloadStatus> result;
  if (!impl_->onWorker()) { return result; }
  result.reserve(impl_->managed.size());
  for (const auto& [key, value] : impl_->managed) { static_cast<void>(key); result.push_back(value->status); }
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
std::optional<RetirementReceipt> PersistentSession::lastRetirement() const {
  if (!impl_->onWorker()) { return {}; }
  return impl_->retirement;
}
std::expected<void, AccountingError> PersistentSession::poll() {
  if (!impl_->onWorker()) { return std::unexpected(AccountingError::WrongThread); }
  if (impl_->call) { return std::unexpected(AccountingError::ActiveOperation); }
  if (impl_->closed) { return std::unexpected(AccountingError::Closing); }
  if (impl_->poisoned) { return std::unexpected(AccountingError::OwnerResetRequired); }
  if (!impl_->session) { return {}; } // create() alone and offline opening stay lazy.
  impl_->call = true;
  try {
    static_cast<void>(impl_->pop());
    impl_->advanceManaged();
    impl_->advanceMagnets();
    if (impl_->transferError || impl_->removalLost) {
      impl_->counterError(AccountingError::Engine);
      impl_->joinPoisoned();
    } else { impl_->post(false); }
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
CloseReceipt PersistentSession::close(Clock::time_point deadline) noexcept {
  if (!impl_->onWorker()) { return {{0, false, {}, AccountingError::WrongThread}, false}; }
  if (impl_->call) { return {{0, false, {}, AccountingError::ActiveOperation}, false}; }
  if (impl_->closed) { return {impl_->retirement.value_or(RetirementReceipt{}), true}; }
  impl_->call = true;
  impl_->closingAdmission = true;
  impl_->phase = AccountingState::Closing;
  const auto result = impl_->retire(std::min(deadline, Clock::now() + CertificationBudget));
  impl_->sink = {}; impl_->operationSink = {};
  impl_->session.reset(); // Join may outlive the certification effort deadline.
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
