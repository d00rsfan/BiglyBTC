#include "session_bridge.hpp"

#include "library_view.hpp"
#include "column_layout_view.hpp"
#include "biglybtc/saved_dnd.hpp"
#include "local_session.hpp"
#include "persistent_session.hpp"
#include "overall_statistics_service.hpp"
#include "source_rate_projection.hpp"
#include "minute_history_writer.hpp"
#include "saved_service_view.hpp"
#include "statistics_view.hpp"
#include "saved_download_tags_view.hpp"
#include "../adapters/history_view.hpp"
#include "../adapters/history_week_view.hpp"
#include "../adapters/history_month_view.hpp"
#include "../adapters/history_range_view.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <limits>
#include <bit>
#include <chrono>
#include <map>
#include <set>
#include <stdexcept>

namespace biglybtc::app {
namespace {
struct Failure { storage::Error error; };
std::int64_t wallMillis() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
OverallOwnerIdentity ownerIdentity(const engine::AccountingIdentity& value) noexcept {
  return {value.applicationRun, value.profileSession, value.engineEpoch};
}
core::OverallPeerCounters peerCounters(const core::EngineCounters& value) noexcept {
  return {value.payloadReceived, value.received - value.payloadReceived,
      value.payloadSent, value.sent - value.payloadSent};
}
core::HistoryCounters historyPeer(const core::EngineCounters& value) noexcept {
  return {value.sent - value.payloadSent, value.payloadSent, value.received - value.payloadReceived,
      value.payloadReceived, 0, 0}; // Actual isolatedSettings explicitly disables DHT.
}
core::MinuteHistoryInput historyInput(const core::EngineCounters& value) noexcept {
  const auto v = historyPeer(value);
  return {std::array{v[0], v[1], v[2], v[3]}, std::array<std::int64_t, 2>{0, 0}};
}
ui::NativeSessionPresentation nativePacket(const engine::ApplicationPeerObservation& value,
    std::optional<core::SourceRateValues> rates = {}) {
  ui::NativeSessionState state;
  switch (value.state) {
    case engine::AccountingState::Starting: state = ui::NativeSessionState::Starting; break;
    case engine::AccountingState::Idle: state = ui::NativeSessionState::Idle; break;
    case engine::AccountingState::Active: state = ui::NativeSessionState::Active; break;
    case engine::AccountingState::Retiring: state = ui::NativeSessionState::Retiring; break;
    case engine::AccountingState::Closing: state = ui::NativeSessionState::Closing; break;
    case engine::AccountingState::Closed: state = ui::NativeSessionState::Closed; break;
    case engine::AccountingState::Uncertified: state = ui::NativeSessionState::Uncertified; break;
    default: return {};
  }
  return std::make_shared<const ui::NativeSessionPacket>(ui::NativeSessionPacket{
      value.identity.profileSession, value.identity.applicationRun, value.identity.engineEpoch,
      value.sequence, state, value.observed, value.ownerElapsedNanoseconds, std::move(rates), value.rateLimits});
}
quint64 reserveColumnLayoutSession() {
  static std::atomic<quint64> last{0};
  auto previous = last.load(std::memory_order_relaxed);
  for (;;) {
    if (previous == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Column layout session identities are exhausted"}};
    }
    if (last.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) { return previous + 1; }
  }
}
quint64 reserveLibraryRevision() {
  static std::atomic<quint64> last{0};
  auto previous = last.load(std::memory_order_relaxed);
  for (;;) {
    if (previous == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Library revision identities are exhausted"}};
    }
    if (last.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) { return previous + 1; }
  }
}
template<typename T> T take(std::expected<T, storage::Error> value) {
  if (!value) { throw Failure{value.error()}; }
  return std::move(*value);
}
void checkpoint(std::stop_token stop) {
  if (stop.stop_requested()) { throw Failure{{storage::ErrorCode::Cancelled, "Cancelled"}}; }
}
QString text(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(std::min(value.size(), std::size_t{8192}))); }
std::optional<ui::SavedPageSource> savedSource(storage::RecoverySource source) {
  switch (source) {
    case storage::RecoverySource::Primary: return ui::SavedPageSource::Primary;
    case storage::RecoverySource::Backup: return ui::SavedPageSource::Backup;
    case storage::RecoverySource::Saving: return ui::SavedPageSource::Saving;
  }
  return std::nullopt;
}
std::optional<ui::ColumnLayoutReadSource> columnLayoutSource(storage::RecoverySource source) {
  switch (source) {
    case storage::RecoverySource::Primary: return ui::ColumnLayoutReadSource::Primary;
    case storage::RecoverySource::Backup: return ui::ColumnLayoutReadSource::Backup;
    case storage::RecoverySource::Saving: return ui::ColumnLayoutReadSource::Saving;
  }
  return std::nullopt;
}
template<typename Snapshot, typename View>
ui::SavedServiceResult<Snapshot> savedResult(std::expected<View, storage::Error> view) {
  ui::SavedServiceResult<Snapshot> result;
  if (!view) {
    if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
    result.failure = text(view.error().message);
    return result;
  }
  if (view->missing()) {
    result.state = ui::SavedPageState::Missing;
    return result;
  }
  if (!view->recoverySource || !(result.source = savedSource(*view->recoverySource))) {
    result.failure = SessionBridge::tr("Saved configuration source is unavailable");
    return result;
  }
  result.snapshot = std::make_shared<const Snapshot>(std::move(*view->snapshot));
  result.state = ui::SavedPageState::Ready;
  return result;
}
core::TorrentHash identity(const ui::Target& target) {
  const auto hex = target.hash.toLatin1();
  const auto bytes = QByteArray::fromHex(hex);
  if (bytes.toHex().toUpper() != hex) { throw Failure{{storage::ErrorCode::InvalidFormat, "Invalid download identity"}}; }
  const auto value = core::TorrentHash::parse(std::string_view(bytes.data(), static_cast<std::size_t>(bytes.size())));
  if (!value) { throw Failure{{storage::ErrorCode::InvalidFormat, "Invalid download identity"}}; }
  return *value;
}
}

struct SessionBridge::Host {
  struct Known { QString name; std::optional<qint64> bytes{}; };
  std::filesystem::path profile;
  unsigned short peerPort;
  storage::ProfileLease lease;
  // Declared after lease: affine engine destruction/join precedes lease release.
  std::unique_ptr<engine::PersistentSession> engine{};
  const quint64 columnLayoutSession;
  // All services are worker-owned; the held lease outlives their destruction.
  std::unique_ptr<OverallStatisticsService> overall{};
  std::unique_ptr<SourceRateProjection> sourceRates{};
  std::optional<storage::Error> overallOpenFailure{};
  core::MinuteHistoryRecorder minuteHistory{};
  std::optional<storage::Error> historyFailure{};
  bool historyEnabled = true;
  quint64 historyRevision = 0;
  std::optional<core::EngineStatistics> lastCounterSample{};
  std::chrono::steady_clock::time_point lastCounterArrival{};
  std::chrono::steady_clock::time_point lastAccountingTick = std::chrono::steady_clock::now();
  std::optional<core::SourceRateValues> latestSourceRates{};
  quint64 overallSequence = 0;
  std::chrono::steady_clock::time_point lastOverallPublication{};
  quint64 revision = 0;
  quint64 engineOperation = 0;
  std::map<std::string, Known> known;
  std::set<std::string> currentHashes;
  // Optional manual-tag cache is worker-only and never loaded by startup.
  bool manualTagsEnabled = false, manualTagCommandActive = false, manualTagEnded = false;
  quint64 manualTagRevision = 0, manualTagSequence = 0;
  quint64 manualResolvedLibrary = 0, manualResolvedTag = 0;
  ui::ManualTagReadState manualTagReadState = ui::ManualTagReadState::Failed;
  QString manualTagFailure, manualJoinFailure;
  std::shared_ptr<const core::SavedDownloadTags> manualTagSource;
  std::shared_ptr<const core::ManualTagMembership> manualMembership;
  quint64 manualOwnerEpoch = 0, manualOwnerSequence = 0;
  engine::AccountingState manualOwnerState = engine::AccountingState::Starting;
  bool manualOwnerSampled = false, manualIdleAuthority = false;
  std::optional<core::TargetTraffic> manualTarget;
  core::ManualTagTraffic manualLastTraffic{};
  ui::Presentation lastSnapshot{};
  std::shared_ptr<const core::Document> columnBaseline{};
  std::optional<storage::RecoverySource> columnBaselineSource{};
  bool columnBaselineMissing = false;
  std::stop_token columnBaselineReadStop{};

  Host(std::filesystem::path p, unsigned short port, storage::ProfileLease held, quint64 session)
      : profile(std::move(p)), peerPort(port), lease(std::move(held)), columnLayoutSession(session) {}
  void accepts(const ui::Target& target) const {
    if (target.generation != revision || !currentHashes.contains(target.hash.toStdString())) {
      throw Failure{{storage::ErrorCode::Conflict, "This download selection is stale; refresh the library"}};
    }
  }
  ui::Presentation query(std::stop_token stop) {
    checkpoint(stop);
    auto source = take(storage::readLibraryView(lease));
    checkpoint(stop);
    auto result = std::make_shared<ui::LibraryPresentation>();
    result->revision = revision = reserveLibraryRevision();
    std::map<std::string, int> counts;
    std::set<std::string> pauses;
    for (const auto& pause : source.pauses) { pauses.insert(pause.hash.hex()); }
    for (const auto& item : source.items) { if (item.saved.hash) { ++counts[item.saved.hash->hex()]; } }
    currentHashes.clear();
    result->items.reserve(source.items.size());
    for (const auto& row : source.items) {
      const auto& saved = row.saved;
      const auto hash = saved.hash ? saved.hash->hex() : std::string{};
      QString name = text(saved.saveFile.value_or(std::filesystem::path(saved.saveDirectory).filename().string()));
      std::optional<qint64> size;
      std::optional<ui::SavedSelectedValues> savedSelected;
      if (row.cached) {
        const auto& cached = *row.cached;
        const auto selected = core::projectSavedDndProgress(cached.bytes, saved.completedBytes, cached.savedDnd);
        savedSelected = ui::SavedSelectedValues{.bytes = selected.selectedBytes, .remaining = selected.selectedRemaining,
            .percentThousandths = selected.percentThousandths, .defaults = selected.defaults, .diagnostics = selected.diagnostics};
        if (cached.displayName && !cached.displayName->empty()) { name = text(*cached.displayName); }
        else if (cached.utf8Name && !cached.utf8Name->empty()) { name = text(*cached.utf8Name); }
        else if (name.isEmpty() && cached.name) { name = text(*cached.name); }
        size = cached.bytes;
        known.insert_or_assign(hash, Known{name, size});
      } else if (const auto found = known.find(hash); found != known.end()) {
        name = found->second.name;
        size = found->second.bytes;
      }
      const bool paused = pauses.contains(hash);
      const bool stopped = saved.startup == core::StartupState::Stopped;
      QString state = paused ? SessionBridge::tr("Paused") : stopped ? SessionBridge::tr("Stopped")
          : size && saved.completedBytes && *saved.completedBytes >= *size ? SessionBridge::tr("Complete")
          : saved.startup == core::StartupState::Queued ? SessionBridge::tr("Queued") : SessionBridge::tr("Waiting");
      const bool unique = !hash.empty() && counts[hash] == 1;
      if (unique) { currentHashes.insert(hash); }
      const auto key = unique ? hash : hash + ":" + std::to_string(saved.sourceIndex);
      result->items.push_back({.key = text(key), .hash = text(hash), .name = name, .state = state,
          .bytes = size, .completed = saved.completedBytes && *saved.completedBytes >= 0 ? saved.completedBytes : std::nullopt,
          .received = saved.downloaded, .sent = saved.uploaded,
          .canContinue = peerPort != 0 && unique && saved.persistent && !stopped && !paused,
          .savedPosition = saved.position, .savedSelected = std::move(savedSelected)});
    }
    lastSnapshot = result;
    return result;
  }
};

SessionBridge::SessionBridge(QObject* parent) : QObject(parent) {
  qRegisterMetaType<ui::Target>();
  qRegisterMetaType<ui::Presentation>();
  qRegisterMetaType<ui::Telemetry>();
  qRegisterMetaType<ui::Details>();
  qRegisterMetaType<ui::SavedServicesPresentation>();
  qRegisterMetaType<ui::StatisticsPresentation>();
  qRegisterMetaType<ui::ManualTagStatisticsPresentation>();
  qRegisterMetaType<core::HistoryDate>();
  qRegisterMetaType<ui::HistoryPresentation>();
  qRegisterMetaType<ui::HistoryMonthPresentation>();
  qRegisterMetaType<ui::HistoryRangePresentation>();
  qRegisterMetaType<core::HistoryRangeRequest>();
  qRegisterMetaType<ui::EngineStatisticsPresentation>();
  qRegisterMetaType<ui::NativeSessionPresentation>();
  qRegisterMetaType<ui::OverallPresentation>();
  qRegisterMetaType<ui::RuntimeMetricsPresentation>();
  qRegisterMetaType<ui::ColumnLayoutsPresentation>();
  qRegisterMetaType<ui::ColumnLayoutSaveSubmission>();
  qRegisterMetaType<ui::ColumnLayoutSavePresentation>();
  completionMonitor_ = new QTimer(this);
  completionMonitor_->setInterval(20);
  connect(completionMonitor_, &QTimer::timeout, this, [this] {
    if (!current_ || !current_->result().valid()
        || current_->result().wait_for(std::chrono::seconds{0}) != std::future_status::ready) { return; }
    const auto result = current_->result().get();
    completionMonitor_->stop();
    // Executor can cancel a queued operation before its wrapper runs. Such a
    // future must also release the GUI's busy state and deferred window close.
    if (!result && !completionPosted_->load()) {
      QPointer<SessionBridge> guard(this);
      if (closingSession_) {
        closingSession_ = false;
        idleMonitor_->start();
        emit sessionCloseFailed();
        if (!guard) { return; }
      }
      busy_ = false;
      emit busyChanged(false);
      if (!guard) { return; }
      emit messageReady(result.error() == core::WorkError::Cancelled ? tr("Cancelled") : tr("Session command failed"));
    }
  });
  if (!worker_.start([this]() noexcept {
        if (host_ && host_->engine) { host_->engine->clearObservationSink(); }
        host_.reset(); // Worker-affine fallback abort/join, then held lease.
      })) { throw std::runtime_error("Cannot start native session worker"); }
  idleMonitor_ = new QTimer(this);
  idleMonitor_->setInterval(1000);
  connect(idleMonitor_, &QTimer::timeout, this, [this] {
    if (busy_ || closingSession_) { return; }
    if (idlePoll_) {
      if (idlePoll_->result().wait_for(std::chrono::seconds{0}) != std::future_status::ready) { return; }
      static_cast<void>(idlePoll_->result().get());
      idlePoll_.reset();
    }
    auto poll = worker_.submit([this](std::stop_token stop) -> core::WorkResult {
      if (!stop.stop_requested() && host_ && host_->engine) {
        static_cast<void>(host_->engine->poll()); tickAccounting();
      }
      return {};
    });
    if (poll) { idlePoll_.emplace(std::move(*poll)); }
  });
  idleMonitor_->start();
}
SessionBridge::~SessionBridge() {
  idleMonitor_->stop();
  if (current_) { current_->cancel(); }
  worker_.requestShutdown();
}
void SessionBridge::ready(ui::Presentation snapshot, QString message) {
  publishManualTags(); // Rejoin cached tags after an actual library revision change; no IO.
  // host_ is worker-owned. Publish its immutable identity by value on the GUI.
  const auto session = host_ ? host_->columnLayoutSession : quint64{0};
  QMetaObject::invokeMethod(this, [this, snapshot = std::move(snapshot), message = std::move(message), session] {
    QPointer<SessionBridge> guard(this);
    if (session != 0 && publishedColumnLayoutSession_ == 0) {
      publishedColumnLayoutSession_ = session;
      profileClosed_ = false;
      // The initial optional read is queued by the UI; direct reentry remains busy.
      emit columnLayoutSessionReady(session);
      if (!guard) { return; }
    }
    completionMonitor_->stop();
    busy_ = false;
    emit busyChanged(false);
    if (!guard) { return; }
    emit snapshotReady(snapshot);
    if (!guard) { return; }
    if (!message.isEmpty()) { emit messageReady(message); }
  }, Qt::QueuedConnection);
}
void SessionBridge::failed(QString message) {
  QMetaObject::invokeMethod(this, [this, message = std::move(message)] {
    QPointer<SessionBridge> guard(this);
    completionMonitor_->stop();
    busy_ = false;
    emit busyChanged(false);
    if (!guard) { return; }
    emit messageReady(message);
  }, Qt::QueuedConnection);
}
bool SessionBridge::dispatch(std::move_only_function<void(std::stop_token)> operation, bool recoverLibrarySnapshot, bool closingOperation) {
  if (busy_ || (closingSession_ && !closingOperation)) { return false; }
  busy_ = true;
  auto posted = std::make_shared<std::atomic_bool>(false);
  auto submitted = worker_.submit([this, posted, operation = std::move(operation), recoverLibrarySnapshot, closingOperation](std::stop_token stop) mutable -> core::WorkResult {
    try { checkpoint(stop); operation(stop); posted->store(true); return {}; }
    catch (const Failure& failure) {
      const bool cancelled = failure.error.code == storage::ErrorCode::Cancelled || stop.stop_requested();
      const auto message = cancelled ? tr("Cancelled") : text(failure.error.message);
      if (closingOperation) { closeFailed(message); }
      else if (!recoverLibrarySnapshot) {
        // Optional configuration reads must not refresh or revise the library.
        failed(message);
      } else if (cancelled && host_ && host_->lastSnapshot) {
        auto snapshot = std::make_shared<ui::LibraryPresentation>(*host_->lastSnapshot);
        snapshot->revision = host_->revision = reserveLibraryRevision();
        host_->lastSnapshot = snapshot;
        ready(snapshot, message);
      } else if (host_ && !cancelled) {
        try { ready(host_->query({}), message); }
        catch (...) { failed(message); }
      } else { failed(message); }
      posted->store(true);
      return std::unexpected(cancelled ? core::WorkError::Cancelled : core::WorkError::Failed);
    } catch (const std::exception& failure) {
      if (closingOperation) { closeFailed(text(failure.what())); }
      else { failed(text(failure.what())); }
      posted->store(true);
      return std::unexpected(core::WorkError::Failed);
    }
  });
  if (!submitted) {
    QPointer<SessionBridge> guard(this);
    busy_ = false;
    emit busyChanged(false);
    if (!guard) { return false; }
    emit messageReady(tr("The session command queue is unavailable"));
    return false;
  }
  current_.emplace(std::move(*submitted));
  completionPosted_ = std::move(posted);
  completionMonitor_->start();
  emit busyChanged(true);
  return true;
}
bool SessionBridge::open(std::filesystem::path profile, unsigned short ownedPeerPort) {
  return dispatch([this, profile = std::move(profile), ownedPeerPort](std::stop_token stop) {
    if (host_) { throw Failure{{storage::ErrorCode::Conflict, "A profile is already open"}}; }
    auto lease = take(storage::ProfileLease::acquire(profile));
    const auto session = reserveColumnLayoutSession();
    host_ = std::make_unique<Host>(profile, ownedPeerPort, std::move(lease), session);
    const auto snapshot = host_->query(stop); // Validate the profile before optional engine startup.
    QString startupFailure;
    try { initializePersistentOwner(stop); }
    catch (const Failure& failure) {
      if (failure.error.code == storage::ErrorCode::Cancelled) { throw; }
      startupFailure = tr("Live statistics unavailable: %1").arg(text(failure.error.message));
    }
    ready(snapshot, std::move(startupFailure));
  });
}
bool SessionBridge::refresh() {
  if (profileClosed_) { return false; }
  return dispatch([this](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    ready(host_->query(stop));
  });
}
bool SessionBridge::savedServices(quint64 request) {
  if (profileClosed_) { return false; }
  if (request == 0) { return false; }
  return dispatch([this, request](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    auto packet = std::make_shared<ui::SavedServicesPacket>();
    packet->request = request;
    checkpoint(stop);
    packet->hosting = savedResult<core::SavedHosting>(storage::readSavedHostingView(host_->lease, stop));
    checkpoint(stop);
    packet->sharing = savedResult<core::SavedShares>(storage::readSavedSharesView(host_->lease, stop));
    checkpoint(stop);
    ui::SavedServicesPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested()) {
        completionMonitor_->stop();
        busy_ = false;
        emit busyChanged(false);
        if (guard) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Keep busy while delivering the packet: a direct result slot must not
      // replace current_ or dispatch another command during this completion.
      emit savedServicesReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  });
}
bool SessionBridge::statistics(quint64 session, quint64 request) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session, request](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This statistics session is no longer active"}};
    }
    auto packet = std::make_shared<ui::StatisticsPacket>();
    packet->session = session;
    packet->request = request;
    checkpoint(stop);
    auto view = storage::readSavedStatisticsView(host_->lease, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else if (view->missing()) {
      packet->state = ui::StatisticsReadState::Missing;
    } else if (!view->recoverySource) {
      packet->failure = tr("Saved statistics source is unavailable");
    } else {
      switch (*view->recoverySource) {
        case storage::RecoverySource::Primary: packet->source = ui::StatisticsReadSource::Primary; break;
        case storage::RecoverySource::Backup: packet->source = ui::StatisticsReadSource::Backup; break;
        case storage::RecoverySource::Saving: packet->source = ui::StatisticsReadSource::Saving; break;
      }
      if (packet->source) {
        packet->snapshot = std::make_shared<const core::SavedStatistics>(std::move(*view->snapshot));
        packet->state = ui::StatisticsReadState::Ready;
      }
    }
    checkpoint(stop);
    ui::StatisticsPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop();
        busy_ = false;
        emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Keep the shared command slot busy through direct result callbacks.
      emit statisticsReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional statistics failures never query/revise the library.
}
ui::ManualTagStatisticsPresentation SessionBridge::manualTagPacket() {
  if (!host_ || !host_->manualTagsEnabled || !host_->revision || !host_->manualTagRevision
      || host_->manualTagSequence == std::numeric_limits<quint64>::max()) { return {}; }
  auto& host = *host_;
  if (host.manualTagReadState == ui::ManualTagReadState::Ready
      && (host.manualResolvedLibrary != host.revision || host.manualResolvedTag != host.manualTagRevision)) {
    host.manualMembership.reset(); host.manualJoinFailure.clear();
    host.manualResolvedLibrary = host.revision; host.manualResolvedTag = host.manualTagRevision;
    if (!host.lastSnapshot || host.lastSnapshot->revision != host.revision
        || host.currentHashes.size() > core::MaximumManualTagLibraryHashes || !host.manualTagSource) {
      host.manualJoinFailure = tr("The current library is unavailable for manual tags");
    } else {
      std::vector<core::TorrentHash> hashes; hashes.reserve(host.currentHashes.size());
      for (const auto& key : host.currentHashes) {
        const auto raw = QByteArray::fromHex(QByteArray::fromStdString(key));
        const auto hash = core::TorrentHash::parse(std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));
        if (!hash) { host.manualJoinFailure = tr("Invalid current library identity"); break; }
        hashes.push_back(*hash);
      }
      if (host.manualJoinFailure.isEmpty()) {
        auto joined = core::resolveManualTagMembership(host.columnLayoutSession, host.revision,
            host.manualTagRevision, *host.manualTagSource, hashes);
        if (joined) { host.manualMembership = std::move(*joined); }
        else { host.manualJoinFailure = tr("Cannot resolve bounded manual tag membership"); }
      }
    }
  }
  auto packet = std::make_shared<ui::ManualTagStatisticsPacket>();
  packet->session = host.columnLayoutSession; packet->epoch = host.manualOwnerEpoch;
  packet->libraryRevision = host.revision; packet->tagRevision = host.manualTagRevision;
  packet->readState = host.manualTagReadState; packet->failure = host.manualTagFailure;
  if (packet->readState == ui::ManualTagReadState::Ready) {
    if (!host.manualMembership) {
      packet->readState = ui::ManualTagReadState::Failed;
      packet->failure = host.manualJoinFailure.isEmpty() ? tr("Manual tag membership is unavailable") : host.manualJoinFailure;
    } else {
      packet->membership = host.manualMembership;
      if (host.manualTagEnded) {
        packet->traffic = host.manualLastTraffic; packet->traffic.current = false;
      } else if (host.engine && host.manualOwnerSampled
          && host.manualOwnerState == engine::AccountingState::Idle && host.manualIdleAuthority && !host.manualTagCommandActive) {
        // Actual owner Idle means no registered handle in the one-owner source.
        packet->traffic = {core::ManualTagTrafficState::Idle, {}, true, true};
      } else if (host.engine && host.manualTagCommandActive && host.manualTarget
          && host.manualOwnerState == engine::AccountingState::Active && host.manualOwnerSampled) {
        // One admitted owner handle, and this exact handle's same-status rates.
        packet->traffic = {core::ManualTagTrafficState::Active, host.manualTarget, true, true};
      } else if (host.manualOwnerState == engine::AccountingState::Retiring
          || host.manualOwnerState == engine::AccountingState::Closing
          || host.manualOwnerState == engine::AccountingState::Closed
          || host.manualOwnerState == engine::AccountingState::Uncertified) {
        packet->traffic = host.manualLastTraffic; packet->traffic.current = false;
      }
      if (packet->traffic.target && !host.manualMembership->contains(packet->traffic.target->hash)) {
        packet->traffic = {}; // Changed membership never adopts an old target.
      }
      if (packet->traffic.current && packet->traffic.state != core::ManualTagTrafficState::Unavailable) {
        host.manualLastTraffic = packet->traffic;
      }
    }
  }
  if (host.manualTagEnded) { packet->traffic.current = false; }
  packet->sequence = ++host.manualTagSequence;
  return packet;
}
void SessionBridge::deliverManualTags(ui::ManualTagStatisticsPresentation packet) {
  const auto previous = deliveredManualTags_ && packet && deliveredManualTags_->session == packet->session
      ? deliveredManualTags_ : ui::ManualTagStatisticsPresentation{};
  if (!ui::acceptManualTagStatistics(previous, packet, publishedColumnLayoutSession_)) { return; }
  deliveredManualTags_ = packet;
  emit manualTagStatisticsReady(std::move(packet)); // Last access: direct receiver may delete Bridge.
}
void SessionBridge::publishManualTags() noexcept {
  try {
    auto packet = manualTagPacket(); if (!packet) { return; }
    {
      const std::lock_guard lock(progressMutex_);
      latestManualTags_ = std::move(packet);
      if (manualTagDeliveryQueued_) { return; }
      manualTagDeliveryQueued_ = true;
    }
    if (QMetaObject::invokeMethod(this, [this] {
      std::optional<ui::ManualTagStatisticsPresentation> packet;
      {
        const std::lock_guard lock(progressMutex_);
        packet.swap(latestManualTags_); manualTagDeliveryQueued_ = false;
      }
      if (packet) { deliverManualTags(std::move(*packet)); }
    }, Qt::QueuedConnection)) { return; }
    const std::lock_guard lock(progressMutex_); manualTagDeliveryQueued_ = false;
  } catch (...) {
    // All parser/join/UI allocation failures are optional, never transfer failures.
    const std::lock_guard lock(progressMutex_); manualTagDeliveryQueued_ = false;
  }
}
bool SessionBridge::manualTagStatistics(quint64 session) {
  if (!session || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This manual tag session is no longer active"}};
    }
    auto& host = *host_;
    if (host.manualTagRevision == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Manual tag revisions are exhausted"}};
    }
    checkpoint(stop);
    // Every explicit Refresh reads anew; samples only reuse this result.
    host.manualTagsEnabled = true; ++host.manualTagRevision;
    host.manualTagReadState = ui::ManualTagReadState::Failed;
    host.manualTagSource.reset(); host.manualMembership.reset();
    host.manualTagFailure.clear(); host.manualJoinFailure.clear();
    auto view = storage::readSavedDownloadTagsView(host.lease, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{view.error()}; }
      host.manualTagFailure = text(view.error().message);
    } else if (view->missing()) {
      host.manualTagReadState = ui::ManualTagReadState::Missing;
    } else if (!view->snapshot || !view->recoverySource) {
      host.manualTagFailure = tr("Manual tag source is unavailable");
    } else {
      host.manualTagSource = std::make_shared<const core::SavedDownloadTags>(std::move(*view->snapshot));
      host.manualTagReadState = ui::ManualTagReadState::Ready;
    }
    auto packet = manualTagPacket(); checkpoint(stop);
    QMetaObject::invokeMethod(this, [this, packet = std::move(packet), stop] {
      QPointer<SessionBridge> guard(this);
      if (!stop.stop_requested()) { deliverManualTags(packet); }
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
      if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
    }, Qt::QueuedConnection);
  }, false);
}
bool SessionBridge::history(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::validHistoryDate(date)) { return false; }
  return dispatch([this, session, request, date](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryPacket>();
    packet->session = session; packet->request = request; packet->date = date;
    checkpoint(stop);
    auto view = storage::readSavedHistoryDayView(host_->lease, date, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else if (view->missing()) {
      packet->state = ui::HistoryReadState::Missing;
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryDay>(std::move(*view->snapshot));
      packet->state = ui::HistoryReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Retain the shared command slot through all direct result callbacks.
      emit historyReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // A read-only history failure cannot revise the saved library.
}
bool SessionBridge::historyWeek(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::savedHistoryWeekDates(date)) { return false; }
  return dispatch([this, session, request, date](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryWeekPacket>();
    packet->session = session; packet->request = request; packet->endingDate = date;
    checkpoint(stop);
    auto view = storage::readSavedHistoryWeekView(host_->lease, date, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryWeek>(std::move(*view));
      packet->state = ui::HistoryWeekReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryWeekPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      emit historyWeekReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional history never queries or revises the saved Library.
}
bool SessionBridge::historyMonth(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::savedHistoryMonthDates(date)) { return false; }
  return dispatch([this, session, request, date](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryMonthPacket>();
    packet->session = session; packet->request = request; packet->endingDate = date;
    checkpoint(stop);
    auto view = storage::readSavedHistoryMonthView(host_->lease, date, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryMonth>(std::move(*view));
      packet->state = ui::HistoryMonthReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryMonthPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      emit historyMonthReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional history never queries or revises the saved Library.
}
bool SessionBridge::historyRange(quint64 session, quint64 request, core::HistoryRangeRequest query) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::planSavedHistoryRange(query)) { return false; }
  return dispatch([this, session, request, query](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryRangePacket>();
    packet->session = session; packet->request = request; packet->query = query;
    checkpoint(stop);
    auto view = storage::readSavedHistoryRangeView(host_->lease, query, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryRange>(std::move(*view));
      packet->state = ui::HistoryRangeReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryRangePresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      emit historyRangeReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional history never queries or revises the saved Library.
}
bool SessionBridge::columnLayouts(quint64 session, quint64 request) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session, request](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This column layout session is no longer active"}};
    }
    auto packet = std::make_shared<ui::ColumnLayoutsPacket>();
    packet->session = session;
    packet->request = request;
    checkpoint(stop);
    host_->columnBaseline.reset();
    host_->columnBaselineSource.reset();
    host_->columnBaselineMissing = false;
    host_->columnBaselineReadStop = stop;
    auto view = storage::readColumnLayoutsView(host_->lease, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else if (view->missing()) {
      host_->columnBaselineMissing = true;
      packet->state = ui::ColumnLayoutReadState::Missing;
    } else if (!view->recoverySource || !(packet->source = columnLayoutSource(*view->recoverySource))) {
      packet->failure = tr("Saved column layout source is unavailable");
    } else {
      packet->snapshot = std::make_shared<const core::ColumnLayouts>(std::move(*view->snapshot));
      packet->nativePreferences = std::move(view->nativePreferences);
      host_->columnBaseline = packet->snapshot->source;
      host_->columnBaselineSource = view->recoverySource;
      packet->state = ui::ColumnLayoutReadState::Ready;
    }
    checkpoint(stop);
    ui::ColumnLayoutsPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop();
        busy_ = false;
        emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Hold busy through direct result slots, including cancellation/deletion.
      emit columnLayoutsReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}
bool SessionBridge::saveColumnLayout(ui::ColumnLayoutSaveSubmission submission) {
  if (!submission || submission->session == 0 || submission->request == 0
      || submission->session != publishedColumnLayoutSession_ || !submission->preferences
      || !core::validateClassicPreferences(*submission->preferences)) { return false; }
  // Copy bounded input before worker dispatch; the worker never borrows GUI data.
  const auto session = submission->session;
  const auto request = submission->request;
  auto preferences = std::make_shared<const core::ClassicPreferences>(*submission->preferences);
  return dispatch([this, session, request, preferences = std::move(preferences)](std::stop_token stop) {
    auto packet = std::make_shared<ui::ColumnLayoutSavePacket>();
    packet->session = session;
    packet->request = request;
    checkpoint(stop);
    if (!host_ || host_->columnLayoutSession != session || host_->columnBaselineReadStop.stop_requested()
        || (!host_->columnBaselineMissing && (!host_->columnBaseline || !host_->columnBaselineSource))) {
      packet->failure = tr("Reload the saved column layout before saving");
    } else {
      const auto result = host_->columnBaselineMissing
          ? storage::createClassicPreferencesView(host_->lease, *preferences, stop)
          : storage::saveClassicPreferencesView(host_->lease,
              *host_->columnBaseline, *host_->columnBaselineSource, *preferences, stop);
      // No cancellation checkpoint here: save may already have replaced primary.
      if (result) {
        host_->columnBaseline = *result;
        host_->columnBaselineSource = storage::RecoverySource::Primary;
        host_->columnBaselineMissing = false;
        host_->columnBaselineReadStop = {};
        packet->saved = true;
      } else {
        host_->columnBaseline.reset();
        host_->columnBaselineSource.reset();
        host_->columnBaselineMissing = false;
        packet->failure = text(result.error().message);
      }
    }
    ui::ColumnLayoutSavePresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation)] {
      QPointer<SessionBridge> guard(this);
      // Always deliver the actual write result, including after cancellation.
      emit columnLayoutSaved(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}
bool SessionBridge::details(ui::Target target) {
  if (profileClosed_) { return false; }
  return dispatch([this, target = std::move(target)](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    host_->accepts(target);
    const auto value = take(engine::queryTorrent(host_->lease, identity(target)));
    checkpoint(stop);
    auto name = text(value.displayName && !value.displayName->empty() ? *value.displayName : value.name);
    if (const auto known = host_->known.find(target.hash.toStdString()); known != host_->known.end() && !known->second.name.isEmpty()) {
      name = known->second.name; // The original cache can own a newer display name.
    }
    host_->known.insert_or_assign(target.hash.toStdString(), Host::Known{name, value.bytes});
    auto files = std::make_shared<ui::FileDetails>();
    files->target = target;
    files->items.reserve(value.entries.size());
    for (const auto& file : value.entries) {
      files->items.push_back({file.index, text(file.path), file.bytes, file.priority, file.skipped, file.padding});
    }
    QMetaObject::invokeMethod(this, [this, result = ui::Details{target, name, value.bytes, std::move(files)}] {
      QPointer<SessionBridge> guard(this);
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
      if (!guard) { return; }
      emit detailsReady(result);
    }, Qt::QueuedConnection);
  });
}
void SessionBridge::progress(ui::Telemetry update) {
  {
    const std::lock_guard lock(progressMutex_);
    latestProgress_ = std::move(update);
    if (deliveryQueued_) { return; }
    deliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::Telemetry> update;
    {
      const std::lock_guard lock(progressMutex_);
      update.swap(latestProgress_);
      deliveryQueued_ = false;
    }
    if (update) { emit progressReady(*update); }
  }, Qt::QueuedConnection);
}
void SessionBridge::engineStatistics(ui::EngineStatisticsPresentation packet) {
  if (packet->state != ui::EngineStatisticsState::Observed) {
    // Lifecycle boundaries are never coalesced with measurements.
    QMetaObject::invokeMethod(this, [this, packet = std::move(packet)] {
      if (packet->session == publishedColumnLayoutSession_) { emit engineStatisticsReady(packet); }
    }, Qt::QueuedConnection);
    return;
  }
  {
    const std::lock_guard lock(progressMutex_);
    latestEngineStatistics_ = std::move(packet);
    if (engineDeliveryQueued_) { return; }
    engineDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::EngineStatisticsPresentation> packet;
    {
      const std::lock_guard lock(progressMutex_);
      packet.swap(latestEngineStatistics_);
      engineDeliveryQueued_ = false;
    }
    if (packet && (*packet)->session == publishedColumnLayoutSession_) { emit engineStatisticsReady(*packet); }
  }, Qt::QueuedConnection);
}
bool SessionBridge::continueDownload(ui::Target target) {
  if (profileClosed_) { return false; }
  return dispatch([this, target = std::move(target)](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    host_->accepts(target);
    if (host_->engineOperation == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Engine operation identities are exhausted"}};
    }
    // The accepted snapshot also names downloads without a torrent-cache entry.
    // Reuse that owned label; do not read torrent files just for telemetry.
    QString name;
    if (host_->lastSnapshot) {
      for (const auto& row : host_->lastSnapshot->items) {
        if (row.hash == target.hash) { name = row.name; break; }
      }
    }
    ui::EngineStatisticsPacket packet{.session = host_->columnLayoutSession,
        .operation = ++host_->engineOperation, .sequence = 1, .target = target,
        .name = std::move(name)};
    engineStatistics(std::make_shared<const ui::EngineStatisticsPacket>(packet));
    const auto ended = [&](bool successful) {
      ++packet.sequence;
      packet.state = ui::EngineStatisticsState::Ended;
      packet.successful = successful;
      engineStatistics(std::make_shared<const ui::EngineStatisticsPacket>(packet));
    };
    if (host_->peerPort == 0) {
      ended(false);
      throw Failure{{storage::ErrorCode::Unsupported, "Transfers are disabled in this offline profile"}};
    }
    try { initializePersistentOwner(stop); }
    catch (...) { ended(false); throw; }
    host_->manualTagCommandActive = true; host_->manualTarget.reset(); host_->manualIdleAuthority = false;
    publishManualTags();
    const auto finishTags = [this](Host* host) noexcept {
      if (host_.get() != host) { return; }
      host->manualTagCommandActive = false; host->manualTarget.reset(); publishManualTags();
    };
    const std::unique_ptr<Host, decltype(finishTags)> tagCommand(host_.get(), finishTags);
    auto options = engine::LocalOptions{.profile = host_->profile, .hash = identity(target),
        .peerPort = host_->peerPort, .stop = stop};
    options.progress = [this, target, commandHash = options.hash](const engine::LocalProgress& value) {
      if (host_ && host_->manualTagCommandActive) {
        host_->manualTarget = value.targetTraffic && value.targetTraffic->hash == commandHash
            && core::validTargetTraffic(*value.targetTraffic) ? value.targetTraffic : std::nullopt;
        // Fixed owned primitives are retained before optional UI allocation.
        publishManualTags();
      }
      const auto state = value.phase == engine::LocalProgress::Phase::Checking ? tr("Checking")
          : value.phase == engine::LocalProgress::Phase::Downloading ? tr("Downloading") : tr("Finishing");
      progress({target, value.completedBytes, value.downloadRate, value.uploadRate, state});
    };
    options.statistics = [&](const core::EngineStatistics& value) {
      ++packet.sequence;
      packet.state = ui::EngineStatisticsState::Observed;
      packet.sample = value;
      engineStatistics(std::make_shared<const ui::EngineStatisticsPacket>(packet));
    };
    const auto report = [&] {
      try {
        return host_->engine ? take(engine::continueLocal(*host_->engine, host_->lease, options))
            : take(engine::continueLocal(host_->lease, options));
      }
      catch (...) { ended(false); throw; }
    }();
    ended(true);
    auto& known = host_->known[target.hash.toStdString()];
    known.bytes = report.bytes;
    if (known.name.isEmpty()) { known.name = text(report.payload.filename().string()); }
    ready(host_->query({}), tr("Complete"));
  });
}
void SessionBridge::initializePersistentOwner(std::stop_token stop) {
  if (!host_ || host_->peerPort == 0 || host_->engine) { return; }
  checkpoint(stop);
  // Preserve the exact source/runtime guard and default unbound fallback.
  auto owner = engine::PersistentSession::create({1, host_->columnLayoutSession, 1});
  if (!owner) {
    if (owner.error() == engine::AccountingError::DependencyUnqualified) { return; }
    throw Failure{{storage::ErrorCode::Io, "Cannot create the native engine owner"}};
  }
  if (!(*owner)->startIdle(host_->lease)) {
    throw Failure{{storage::ErrorCode::Io, "Cannot initialize the native idle session"}};
  }
  checkpoint(stop); // Local owning pointer aborts/joins before a cancelled publication.
  host_->engine = std::move(*owner);
  // Optional accounting setup cannot change transfer/storage success.
  try {
    const auto started = wallMillis();
    host_->lastAccountingTick = std::chrono::steady_clock::now();
    auto settings = storage::ConfigFile::load(host_->lease, "biglybt.config");
    if (settings) {
      if (const auto* enabled = settings->document().find("long.term.stats.enable")) {
        if (enabled->type == core::Node::Type::Integer) { host_->historyEnabled = enabled->integer != 0; }
        else {
          host_->historyEnabled = false;
          host_->historyFailure = storage::Error{storage::ErrorCode::InvalidFormat, "Invalid long.term.stats.enable setting"};
        }
      }
    } else if (settings.error().code != storage::ErrorCode::Missing) {
      host_->historyEnabled = false; host_->historyFailure = std::move(settings.error());
    }
    auto service = OverallStatisticsService::open(host_->lease, {1, host_->columnLayoutSession, 1}, started, wallMillis());
    if (service) { host_->overall = std::move(*service); }
    else { host_->overallOpenFailure = std::move(service.error()); }
    const auto smoothing = configuredSourceSmoothing(settings ? &settings->document() : nullptr);
    host_->sourceRates = std::make_unique<SourceRateProjection>(smoothing.windowSeconds);
  } catch (...) {} // Owner retention is independent of optional allocations.
  static_cast<void>(host_->engine->setObservationSink([this](engine::ApplicationPeerObservation value) {
    observeNative(value);
  }));
  // The existing idle monitor is the only startup poller. Availability and
  // zeros are published only by actual session_stats responses after GUI open.
}
void SessionBridge::observeNative(const engine::ApplicationPeerObservation& value) noexcept {
  if (!host_) { return; }
  // Fixed essential retention precedes every optional allocation/publication.
  if (value.observed && host_->overall) {
    static_cast<void>(host_->overall->capture({ownerIdentity(value.identity), value.sequence,
        peerCounters(value.observed->counters), OverallSampleQuality::Observed}));
  }
  if (value.observed && (!host_->lastCounterSample
      || value.observed->elapsedNanoseconds > host_->lastCounterSample->elapsedNanoseconds)) {
    host_->lastCounterSample = value.observed;
    host_->lastCounterArrival = std::chrono::steady_clock::now();
  }
  if (host_->sourceRates) {
    host_->latestSourceRates = host_->sourceRates->observe(value.observed, value.ownerElapsedNanoseconds, value.sequence);
  }
  if (value.state == engine::AccountingState::Uncertified && host_->overall && !host_->overall->uncertified()) {
    // Save the retained observed prefix before latching the missing-tail state.
    tickAccounting(true);
    static_cast<void>(host_->overall->invalidate(ownerIdentity(value.identity)));
  }
  tickAccounting();
  try {
    const auto now = std::chrono::steady_clock::now();
    if (now - host_->lastOverallPublication >= std::chrono::seconds{1}) {
      host_->lastOverallPublication = now; publishOverall(); publishRuntimeMetrics(value);
    }
  } catch (...) {} // Optional UI cannot disconnect the essential sink.
  if (value.identity.profileSession == host_->columnLayoutSession && value.identity.engineEpoch
      && value.identity.engineEpoch >= host_->manualOwnerEpoch && value.sequence > host_->manualOwnerSequence) {
    host_->manualOwnerEpoch = value.identity.engineEpoch; host_->manualOwnerSequence = value.sequence;
    host_->manualOwnerState = value.state; host_->manualOwnerSampled = value.observed.has_value();
    host_->manualIdleAuthority = value.state == engine::AccountingState::Idle && value.observed.has_value();
    publishManualTags();
  }
  try { nativeSession(nativePacket(value, host_->latestSourceRates)); } catch (...) {}
}
void SessionBridge::tickAccounting(bool force, bool ending) noexcept {
  if (!host_ || !host_->overall) { return; }
  const auto steadyNow = std::chrono::steady_clock::now();
  const bool due = steadyNow - host_->lastAccountingTick >= std::chrono::seconds{60};
  const auto wallNow = wallMillis();
  // History starts on the first actual counter sample, independently of the
  // sixty-second Overall update. Its absolute baseline is the original twelve-
  // entry snapshot plus each actual source difference (Java long arithmetic).
  try {
    if (!ending && host_->historyEnabled && !host_->historyFailure && host_->lastCounterSample) {
      const auto id = core::MinuteHistoryIdentity{1, host_->columnLayoutSession, 1};
      const bool fresh = steadyNow - host_->lastCounterArrival <= std::chrono::seconds{3};
      const auto state = host_->minuteHistory.state();
      std::optional<core::MinuteHistoryRequest> request;
      if (!fresh || host_->overall->uncertified()) {
        if (state.active) { request = core::MinuteHistoryRequest{core::MinuteHistoryEvent::Suspend, id, wallNow, {}, {}}; }
      } else if (!state.active) {
        const auto source = historyPeer(host_->lastCounterSample->counters);
        const auto snap = host_->overall->observation().snapshot;
        core::HistoryCounters baseline{};
        for (std::size_t i = 0; i < baseline.size(); ++i) {
          baseline[i] = std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(snap[i])
              + static_cast<std::uint64_t>(source[i]) - static_cast<std::uint64_t>(snap[i + 6]));
        }
        request = core::MinuteHistoryRequest{core::MinuteHistoryEvent::Start, id, wallNow,
            historyInput(host_->lastCounterSample->counters), baseline};
      } else if (due) {
        request = core::MinuteHistoryRequest{core::MinuteHistoryEvent::Tick, id, wallNow,
            historyInput(host_->lastCounterSample->counters), {}};
      }
      if (request) {
        storage::MinuteHistoryWriteReceipt receipt;
        auto written = storage::writeMinuteHistory(host_->lease, host_->minuteHistory, *request, {}, &receipt);
        if (!written) { host_->historyFailure = std::move(written.error()); }
        else if (receipt.appendedActions) { publishHistoryChange(); }
      }
    }
    if (!due && !force) { return; }
    if (due) { host_->lastAccountingTick = steadyNow; }
    auto updated = host_->overall->tick(wallNow, {core::OverallDhtResolution::Empty, {}}, force);
    if (updated) {
      // Explicit close also saves a ClockGap prefix; this does not reinterpret
      // Java's early-returning force branch as a save intention.
      if (force && updated->disposition == core::OverallUpdateDisposition::ClockGap) {
        static_cast<void>(host_->overall->saveCurrent());
      } else { static_cast<void>(host_->overall->flush()); }
    }
    try { publishOverall(); } catch (...) {}
  } catch (...) {} // Accounting diagnostics must not fail successful storage.
}
void SessionBridge::publishRuntimeMetrics(const engine::ApplicationPeerObservation& value) {
  if (!host_ || !host_->engine) { return; }
  auto packet = std::make_shared<ui::RuntimeMetricsPacket>();
  packet->session = host_->columnLayoutSession; packet->epoch = value.identity.engineEpoch;
  packet->sequence = value.sequence; packet->snapshot = host_->engine->diagnostics();
  packet->current = value.state == engine::AccountingState::Idle || value.state == engine::AccountingState::Active;
  ui::RuntimeMetricsPresentation result = std::move(packet);
  {
    const std::lock_guard lock(progressMutex_);
    latestRuntime_ = std::move(result);
    if (runtimeDeliveryQueued_) { return; }
    runtimeDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::RuntimeMetricsPresentation> result;
    {
      const std::lock_guard lock(progressMutex_);
      result.swap(latestRuntime_); runtimeDeliveryQueued_ = false;
    }
    if (result && (*result)->session == publishedColumnLayoutSession_) { emit runtimeMetricsReady(*result); }
  }, Qt::QueuedConnection);
}
void SessionBridge::publishHistoryChange() noexcept {
  if (!host_ || host_->historyRevision == std::numeric_limits<quint64>::max()) { return; }
  const HistoryChange change{host_->columnLayoutSession, ++host_->historyRevision};
  try {
    {
      const std::lock_guard lock(progressMutex_);
      latestHistoryChange_ = change;
      if (historyChangeDeliveryQueued_) { return; }
      historyChangeDeliveryQueued_ = true;
    }
    const bool queued = QMetaObject::invokeMethod(this, [this] {
      std::optional<HistoryChange> change;
      {
        const std::lock_guard lock(progressMutex_);
        change.swap(latestHistoryChange_); historyChangeDeliveryQueued_ = false;
      }
      if (change && change->session == publishedColumnLayoutSession_) {
        emit historyChanged(change->session, change->revision); // Last access: receiver may delete Bridge.
      }
    }, Qt::QueuedConnection);
    if (!queued) {
      const std::lock_guard lock(progressMutex_); historyChangeDeliveryQueued_ = false;
    }
  } catch (...) {
    // Optional delivery cannot fail accounting or a successful disk transaction.
    // Retain the latest scalar; the next actual mutation can retry publication.
    const std::lock_guard lock(progressMutex_); historyChangeDeliveryQueued_ = false;
  }
}
void SessionBridge::publishOverall(bool closed) {
  if (!host_ || (!host_->overall && !host_->overallOpenFailure)
      || host_->overallSequence == std::numeric_limits<quint64>::max()) { return; }
  auto packet = std::make_shared<ui::OverallPacket>();
  packet->session = host_->columnLayoutSession; packet->sequence = ++host_->overallSequence;
  packet->closed = closed;
  if (host_->overall) {
    const auto& service = *host_->overall;
    packet->available = true; packet->values = service.values();
    packet->values = service.projectedValues();
    if (service.baseline().snapshot) { packet->saved = service.baseline().snapshot->values; }
    packet->dirty = service.dirty() || !packet->saved || *packet->saved != packet->values;
    packet->writable = service.latestSample().has_value() && !service.writesBlocked() && !service.uncertified() && !closed;
    packet->finalCounters = service.finalCountersApplied();
    if (const auto* failure = service.lastStorageError()) { packet->failure = text(failure->message); }
    else if (service.writesBlocked()) { packet->failure = tr("Statistics saving is blocked"); }
    else if (service.uncertified()) { packet->failure = tr("The final traffic interval is unavailable; last observed counters retained"); }
  } else { packet->failure = text(host_->overallOpenFailure->message); }
  if (host_->historyFailure) { packet->historyFailure = text(host_->historyFailure->message); }
  ui::OverallPresentation result = std::move(packet);
  {
    const std::lock_guard lock(progressMutex_);
    latestOverall_ = std::move(result);
    if (overallDeliveryQueued_) { return; }
    overallDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::OverallPresentation> result;
    {
      const std::lock_guard lock(progressMutex_);
      result.swap(latestOverall_); overallDeliveryQueued_ = false;
    }
    if (result && (*result)->session == publishedColumnLayoutSession_) { emit overallReady(*result); }
  }, Qt::QueuedConnection);
}
bool SessionBridge::markStatistics(quint64 session, bool clear) {
  if (!session || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session, clear](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session || !host_->overall) {
      throw Failure{{storage::ErrorCode::Unsupported, "Live statistics accounting is not active"}};
    }
    checkpoint(stop);
    auto& service = *host_->overall;
    if (service.writesBlocked() || service.uncertified()) {
      throw Failure{{storage::ErrorCode::Conflict, "Statistics saving is blocked; current values are retained"}};
    }
    // Update/stage once, then publish only the complete marked values.
    auto updated = service.tick(wallMillis(), {core::OverallDhtResolution::Empty, {}}, true);
    if (!updated) { throw Failure{{storage::ErrorCode::Conflict, "Current statistics are unavailable"}}; }
    const auto saved = clear ? service.clearMark(stop) : service.setMark(wallMillis(), stop);
    publishOverall();
    if (!saved) {
      if (const auto* failure = service.lastStorageError()) { throw Failure{*failure}; }
      throw Failure{{storage::ErrorCode::Io, "Cannot save the statistics mark"}};
    }
    QMetaObject::invokeMethod(this, [this, clear] {
      QPointer<SessionBridge> guard(this);
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
      if (guard) { emit messageReady(clear ? tr("Statistics mark cleared") : tr("Statistics mark saved")); }
    }, Qt::QueuedConnection);
  }, false);
}
void SessionBridge::nativeSession(ui::NativeSessionPresentation packet) {
  if (!packet) { return; }
  {
    const std::lock_guard lock(progressMutex_);
    latestNativeSession_ = std::move(packet);
    if (nativeDeliveryQueued_) { return; }
    nativeDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::NativeSessionPresentation> packet;
    {
      const std::lock_guard lock(progressMutex_);
      packet.swap(latestNativeSession_);
      nativeDeliveryQueued_ = false;
    }
    if (packet && (*packet)->session == publishedColumnLayoutSession_) { emit nativeSessionReady(*packet); }
  }, Qt::QueuedConnection);
}
bool SessionBridge::closeSession() {
  if (busy_ || closingSession_) { return false; }
  closingSession_ = true;
  idleMonitor_->stop();
  QPointer<SessionBridge> guard(this);
  const bool accepted = dispatch([this](std::stop_token) {
    const auto session = host_ ? host_->columnLayoutSession : quint64{0};
    bool joined = true;
    bool released = true;
    ui::NativeSessionPresentation finalObservation;
    if (host_ && host_->engine) {
      host_->engine->clearObservationSink();
      const auto receipt = host_->engine->close(std::chrono::steady_clock::now() + std::chrono::seconds{5});
      joined = receipt.sessionJoined;
      released = receipt.retirement.storageReleased;
      if (host_->overall && receipt.sessionJoined && released && receipt.retirement.peerCounters) {
        const auto& final = *receipt.retirement.peerCounters;
        static_cast<void>(host_->overall->capture({ownerIdentity(final.identity()), final.sequence(),
            peerCounters(final.counters()), OverallSampleQuality::ClosedFinal}));
        if (host_->minuteHistory.state().active) {
          try {
            storage::MinuteHistoryWriteReceipt historyReceipt;
            auto ended = storage::writeMinuteHistory(host_->lease, host_->minuteHistory,
                {core::MinuteHistoryEvent::End, {1, host_->columnLayoutSession, 1}, wallMillis(), historyInput(final.counters()), {}}, {}, &historyReceipt);
            if (!ended && !host_->historyFailure) { host_->historyFailure = std::move(ended.error()); }
            else if (ended && historyReceipt.appendedActions) { publishHistoryChange(); }
          } catch (...) {}
        }
      } else if (host_->minuteHistory.state().active) {
        try { static_cast<void>(storage::writeMinuteHistory(host_->lease, host_->minuteHistory,
            {core::MinuteHistoryEvent::Suspend, {1, host_->columnLayoutSession, 1}, wallMillis(), {}, {}})); } catch (...) {}
      }
      tickAccounting(true, true);
      try { publishOverall(true); } catch (...) {}
      try {
        if (const auto last = host_->engine->latest()) {
          finalObservation = nativePacket(*last); publishRuntimeMetrics(*last);
        }
      } catch (...) {} // Optional immutable telemetry cannot retain Host/lease.
    }
    if (host_) { host_->manualTagEnded = true; publishManualTags(); }
    host_.reset(); // Required lifetime barrier before GUI close acknowledgement.
    try { nativeSession(std::move(finalObservation)); } catch (...) {}
    QMetaObject::invokeMethod(this, [this, session, joined, released] {
      QPointer<SessionBridge> guard(this);
      completionMonitor_->stop();
      publishedColumnLayoutSession_ = 0; // Reject old-session requests before releasing busy.
      profileClosed_ = true;
      busy_ = false;
      emit busyChanged(false); // Keep closingSession_ true through this callback.
      if (!guard) { return; }
      closingSession_ = false;
      idleMonitor_->start();
      emit sessionClosed(session, joined, released); // Final access: receiver may reopen or delete Bridge.
    }, Qt::QueuedConnection);
  }, false, true);
  if (!guard) { return accepted; }
  if (!accepted) { closingSession_ = false; idleMonitor_->start(); }
  return accepted;
}
void SessionBridge::closeFailed(QString message) {
  QMetaObject::invokeMethod(this, [this, message = std::move(message)] {
    QPointer<SessionBridge> guard(this);
    closingSession_ = false;
    idleMonitor_->start();
    emit sessionCloseFailed();
    if (!guard) { return; }
    completionMonitor_->stop();
    busy_ = false;
    emit busyChanged(false);
    if (guard) { emit messageReady(message); }
  }, Qt::QueuedConnection);
}
void SessionBridge::cancel() { if (current_ && !closingSession_) { current_->cancel(); } }
} // namespace biglybtc::app
