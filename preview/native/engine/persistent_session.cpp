#include "persistent_session_support.hpp"
#include "persistent_runtime_guard.hpp"
#include "runtime_metrics_sampler.hpp"

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
  AccountingIdentity identity;
  const std::thread::id worker = std::this_thread::get_id();
  const std::int64_t origin = static_cast<std::int64_t>(Clock::now().time_since_epoch().count());
  std::unique_ptr<lt::session> session{};
  AccountingState phase = AccountingState::Starting;
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

  explicit Impl(AccountingIdentity value) : identity(value) {}
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
        counterFailure ? AccountingState::Uncertified : phase, lastStatistics, now};
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
  void prepare(const storage::ProfileLease& lease, const LocalOptions& options) {
    detail::require(call && onWorker() && !registered && !poisoned && !closed,
        "Persistent owner is not available", storage::ErrorCode::Conflict);
    bind(lease);
    // Optional callback allocation failure does not prevent safe transfer or
    // essential absolute accounting. Every operation gets a fresh accumulator.
    operationSink = {};
    try { operationSink = options.statistics; } catch (...) { operationSink = {}; }
    operationBaseline.reset(); operationAccumulator = {};
    transferError = false; storageError = false; removalLost = false; removalSeen = false;
    if (!session) {
      try {
        session = std::make_unique<lt::session>(detail::settings());
        session->pause();
        phase = AccountingState::Idle;
        mapCounters();
      } catch (...) { joinPoisoned(); throw; }
    }
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
  if (!impl_->session) { return {}; } // Statistics/offline opening creates no engine.
  impl_->call = true;
  try {
    static_cast<void>(impl_->pop());
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
  return {result, true};
}

namespace detail {
PersistentCall::PersistentCall(PersistentSession& owner, const storage::ProfileLease& lease) : owner_(owner) {
  auto& impl = *owner_.impl_;
  require(impl.onWorker(), "Persistent owner called from another thread", storage::ErrorCode::Conflict);
  require(!impl.call && !impl.closed && !impl.poisoned,
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
