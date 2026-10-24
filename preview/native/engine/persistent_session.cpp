#include "persistent_session_support.hpp"
#include "tracker_statistics_sampler.hpp"
#include "persistent_runtime_guard.hpp"
#include "runtime_metrics_sampler.hpp"
#include "effective_rate_limits.hpp"
#include "peer_payload_observer.hpp"
#include "managed_storage.hpp"
#include "network_policy.hpp"
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
    lt::torrent_handle handle;
    ManagedDownloadStatus status;
    bool manualPaused = false, forceStart = false, stop = false;
    bool flushed = false, removing = false, removed = false;
    Clock::time_point deadline{};
    std::vector<std::uint8_t> verified;
    Managed(std::uint64_t id, detail::ManagedStorage files, const core::TorrentHash& hash)
        : token(std::make_unique<OperationToken>(OperationToken{id})), storage(std::move(files)),
          status{.hash = hash, .admission = id} {}
  };
  std::map<std::string, std::unique_ptr<Managed>> managed;
  const storage::ProfileLease* managedLease = nullptr; // Same bound lease, owned by Host.
  std::optional<NetworkPolicy> network;
  bool appliedSeedingOnly = false;
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
      for (auto& [key, entry] : managed) {
        static_cast<void>(key);
        auto& value = *entry;
        if (auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert);
            removed && removed->handle == value.handle
            && removed->userdata.get<OperationToken>() == value.token.get()) { value.removed = true; }
        if (auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert);
            saved && saved->handle == value.handle && value.stop) { value.flushed = true; }
        auto fail = [&](const lt::torrent_handle& handle) {
          if (handle == value.handle) {
            value.status.failure = storage::Error{storage::ErrorCode::Io,
                "Torrent storage/flush failed; saved pieces require recheck"};
            value.status.phase = ManagedPhase::Failed;
            if (!value.stop) { requestStop(value); }
          }
        };
        if (auto* failed = lt::alert_cast<lt::file_error_alert>(alert)) { fail(failed->handle); }
        if (auto* failed = lt::alert_cast<lt::torrent_error_alert>(alert)) { fail(failed->handle); }
        if (auto* failed = lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) { fail(failed->handle); }
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
      entry->status.phase = ManagedPhase::Failed;
      entry->status.failure = storage::Error{storage::ErrorCode::Io,
          "Engine owner joined after failure; original pieces require recheck"};
    }
    outstanding = false;
    counterError(AccountingError::OwnerResetRequired);
    publish(); // Explicit uncertified state; retained values are last-observed.
  }
  void bind(const storage::ProfileLease& lease) {
    struct stat profile{};
    detail::require(lease.valid() && ::fstat(lease.directoryHandle(), &profile) == 0 && S_ISDIR(profile.st_mode),
        "Persistent owner requires its held profile lease", storage::ErrorCode::UnsafePath);
    if (profileBound) {
      detail::require(profile.st_dev == profileDevice && profile.st_ino == profileInode,
          "Persistent owner cannot cross profile identities", storage::ErrorCode::Conflict);
    } else { profileDevice = profile.st_dev; profileInode = profile.st_ino; profileBound = true; }
  }
  void ensureSession() {
    if (!session) {
      try {
        auto settings = detail::settings();
        if (network) {
          auto translated = makeNetworkSettings(*network);
          detail::require(translated.has_value(), "Network policy cannot be applied");
          settings = std::move(*translated);
        }
        settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error
            | lt::alert_category::status | lt::alert_category::storage));
        session = std::make_unique<lt::session>(settings);
        session->pause();
        rateLimits = detail::readEffectiveRateLimits(*session); // Optional read, never applies settings.
        phase = AccountingState::Idle;
        mapCounters();
      } catch (...) { joinPoisoned(); throw; }
    }
  }
  void prepare(const storage::ProfileLease& lease, const LocalOptions& options) {
    detail::require(call && onWorker() && !registered && !managedActive() && !poisoned && !closed,
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
    if (!value.handle.is_valid() || value.removing) { return; }
    const auto sample = value.handle.status(pieces ? lt::torrent_handle::query_pieces : lt::status_flags_t{});
    value.status.retained = true;
    value.status.completed = sample.total_done;
    value.status.bytes = sample.total;
    value.status.received = sample.total_payload_download;
    value.status.sent = sample.total_payload_upload;
    value.status.downloadRate = sample.download_payload_rate;
    value.status.uploadRate = sample.upload_payload_rate;
    value.status.pieces = sample.num_pieces;

    value.status.complete = sample.is_seeding;
    value.status.checked = sample.state != lt::torrent_status::checking_files
        && sample.state != lt::torrent_status::checking_resume_data
        && sample.state != lt::torrent_status::downloading_metadata;
    value.status.selectedComplete = sample.is_finished;
    if (pieces) {
      value.verified.assign(static_cast<std::size_t>(value.status.totalPieces), 0);
      for (int i = 0; i < sample.pieces.size() && i < value.status.totalPieces; ++i) {
        value.verified[static_cast<std::size_t>(i)] = sample.pieces[lt::piece_index_t{i}] ? 1 : 0;
      }
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
    if (value.stop || !value.handle.is_valid()) { return; }
    value.stop = true;
    value.status.phase = ManagedPhase::Stopping;
    value.deadline = Clock::now() + CertificationBudget;
    value.handle.unset_flags(lt::torrent_flags::auto_managed);
    value.handle.pause();
    value.handle.save_resume_data(lt::torrent_handle::flush_disk_cache);
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
        if (!value.status.failure && managedLease) {
          auto saved = value.storage.checkpoint(*managedLease, value.verified, value.status.complete,
              value.status.received, value.status.sent);
          if (!saved) { value.status.failure = saved.error(); }
        }
        value.status.retained = false;
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
    if (!registered && !managedActive() && !closed && !poisoned) { phase = AccountingState::Idle; }
    if (network && session && !poisoned) {
      bool any = false, onlySeeds = true;
      for (const auto& [key, entry] : managed) {
        static_cast<void>(key);
        if (!entry->handle.is_valid() || entry->stop) { continue; }
        any = true; onlySeeds = onlySeeds && entry->status.selectedComplete;
      }
      const bool seedingOnly = any && onlySeeds;
      if (seedingOnly != appliedSeedingOnly) {
        auto settings = makeNetworkSettings(*network, seedingOnly);
        if (settings) {
          settings->set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error
              | lt::alert_category::status | lt::alert_category::storage));
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
      if (!retireManaged(deadline)) {
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
  auto settings = makeNetworkSettings(policy);
  if (!settings) { return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,
      "Unsupported network setting: " + settings.error().field}); }
  impl_->network = policy;
  return {};
}
std::expected<void, storage::Error> PersistentSession::startManaged(
    const storage::ProfileLease& lease, const ManagedDownloadOptions& options) {
  if (!impl_->onWorker() || impl_->call || impl_->registered || impl_->closed || impl_->poisoned) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed admission is unavailable"});
  }
  const auto key = options.hash.hex();
  if (const auto found = impl_->managed.find(key); found != impl_->managed.end()) {
    if (found->second->handle.is_valid() || found->second->removing) {
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Torrent is already admitted or retiring"});
    }
    impl_->managed.erase(found);
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
    auto storage = detail::take(detail::ManagedStorage::prepare(lease, options.hash, *impl_->network));
    impl_->ensureSession();
    auto value = std::make_unique<Impl::Managed>(++impl_->operation, std::move(storage), options.hash);
    auto parameters = value->storage.parameters();
    value->status.totalPieces = parameters.ti->num_pieces();
    value->status.bytes = parameters.ti->total_size();
    parameters.userdata = value->token.get();
    detail::attachPeerPayloadObserver(parameters, impl_->peerPayloadLedger, impl_->operation);
    parameters.flags |= lt::torrent_flags::paused | lt::torrent_flags::duplicate_is_error;
    if (options.forceStart) { parameters.flags &= ~lt::torrent_flags::auto_managed; }
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
    slot->second->status.retained = true;
    impl_->managedLease = &lease;
    impl_->phase = AccountingState::Active;
    if (!options.queued || options.forceStart) { slot->second->handle.resume(); }
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
  auto found = impl_->managed.find(hash.hex());
  if (found == impl_->managed.end() || !found->second->handle.is_valid() || found->second->stop) {
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Torrent is not active"});
  }
  auto& value = *found->second;
  value.manualPaused = paused;
  if (paused) { value.handle.unset_flags(lt::torrent_flags::auto_managed); value.handle.pause(); }
  else {
    if (!value.forceStart) { value.handle.set_flags(lt::torrent_flags::auto_managed); }
    value.handle.resume();
  }
  return {};
}
std::expected<void, storage::Error> PersistentSession::queueManaged(const core::TorrentHash& hash, bool forceStart, bool queued) {
  auto changed = pauseManaged(hash, false);
  if (!changed) { return changed; }
  auto& value = *impl_->managed.at(hash.hex());
  value.forceStart = forceStart;
  if (forceStart) { value.handle.unset_flags(lt::torrent_flags::auto_managed); value.handle.resume(); }
  else { value.handle.set_flags(lt::torrent_flags::auto_managed); if (queued) { value.handle.pause(); } }
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
std::expected<void, storage::Error> PersistentSession::reorderManaged(std::span<const core::TorrentHash> order) {
  if (!managedAvailable() || impl_->call) { return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Managed queue is unavailable"}); }
  int position = 0;
  for (const auto& hash : order) {
    const auto found = impl_->managed.find(hash.hex());
    if (found == impl_->managed.end() || !found->second->handle.is_valid()
        || found->second->stop || found->second->status.selectedComplete) { continue; }
    found->second->handle.queue_position_set(lt::queue_position_t{position++});
  }
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
  if (impl_->counterFailure) { return std::unexpected(*impl_->counterFailure); }
  return {};
}
CloseReceipt PersistentSession::close(Clock::time_point deadline) noexcept {
  if (!impl_->onWorker()) { return {{0, false, {}, AccountingError::WrongThread}, false}; }
  if (impl_->call) { return {{0, false, {}, AccountingError::ActiveOperation}, false}; }
  if (impl_->closed) { return {impl_->retirement.value_or(RetirementReceipt{}), true}; }
  impl_->call = true;
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
  return {result, true, std::move(checkpointFailure)};
}

namespace detail {
PersistentCall::PersistentCall(PersistentSession& owner, const storage::ProfileLease& lease) : owner_(owner) {
  auto& impl = *owner_.impl_;
  require(impl.onWorker(), "Persistent owner called from another thread", storage::ErrorCode::Conflict);
  require(!impl.call && !impl.closed && !impl.poisoned && !impl.managedActive(),
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
