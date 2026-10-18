#include "session_bridge.hpp"

#include "library_view.hpp"
#include "local_session.hpp"
#include "saved_service_view.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <map>
#include <set>
#include <stdexcept>

namespace biglybtc::app {
namespace {
struct Failure { storage::Error error; };
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
  quint64 revision = 0;
  std::map<std::string, Known> known;
  std::set<std::string> currentHashes;
  ui::Presentation lastSnapshot{};

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
    result->revision = ++revision;
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
      if (row.cached) {
        const auto& cached = *row.cached;
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
          .savedPosition = saved.position});
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
      busy_ = false;
      emit busyChanged(false);
      if (!guard) { return; }
      emit messageReady(result.error() == core::WorkError::Cancelled ? tr("Cancelled") : tr("Session command failed"));
    }
  });
  if (!worker_.start()) { throw std::runtime_error("Cannot start native session worker"); }
}
SessionBridge::~SessionBridge() {
  if (current_) { current_->cancel(); }
  worker_.requestShutdown();
}
void SessionBridge::ready(ui::Presentation snapshot, QString message) {
  QMetaObject::invokeMethod(this, [this, snapshot = std::move(snapshot), message = std::move(message)] {
    QPointer<SessionBridge> guard(this);
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
bool SessionBridge::dispatch(std::move_only_function<void(std::stop_token)> operation) {
  if (busy_) { return false; }
  busy_ = true;
  auto posted = std::make_shared<std::atomic_bool>(false);
  auto submitted = worker_.submit([this, posted, operation = std::move(operation)](std::stop_token stop) mutable -> core::WorkResult {
    try { checkpoint(stop); operation(stop); posted->store(true); return {}; }
    catch (const Failure& failure) {
      const bool cancelled = failure.error.code == storage::ErrorCode::Cancelled || stop.stop_requested();
      const auto message = cancelled ? tr("Cancelled") : text(failure.error.message);
      if (cancelled && host_ && host_->lastSnapshot) {
        auto snapshot = std::make_shared<ui::LibraryPresentation>(*host_->lastSnapshot);
        snapshot->revision = ++host_->revision;
        host_->lastSnapshot = snapshot;
        ready(snapshot, message);
      } else if (host_ && !cancelled) {
        try { ready(host_->query({}), message); }
        catch (...) { failed(message); }
      } else { failed(message); }
      posted->store(true);
      return std::unexpected(cancelled ? core::WorkError::Cancelled : core::WorkError::Failed);
    } catch (const std::exception& failure) {
      failed(text(failure.what()));
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
    host_ = std::make_unique<Host>(Host{.profile = profile, .peerPort = ownedPeerPort, .lease = std::move(lease),
        .known = {}, .currentHashes = {}});
    ready(host_->query(stop));
  });
}
bool SessionBridge::refresh() {
  return dispatch([this](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    ready(host_->query(stop));
  });
}
bool SessionBridge::savedServices(quint64 request) {
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
bool SessionBridge::details(ui::Target target) {
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
bool SessionBridge::continueDownload(ui::Target target) {
  return dispatch([this, target = std::move(target)](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    host_->accepts(target);
    auto options = engine::LocalOptions{.profile = host_->profile, .hash = identity(target),
        .peerPort = host_->peerPort, .stop = stop};
    options.progress = [this, target](const engine::LocalProgress& value) {
      const auto state = value.phase == engine::LocalProgress::Phase::Checking ? tr("Checking")
          : value.phase == engine::LocalProgress::Phase::Downloading ? tr("Downloading") : tr("Finishing");
      progress({target, value.completedBytes, value.downloadRate, value.uploadRate, state});
    };
    const auto report = take(engine::continueLocal(host_->lease, options));
    auto& known = host_->known[target.hash.toStdString()];
    known.bytes = report.bytes;
    if (known.name.isEmpty()) { known.name = text(report.payload.filename().string()); }
    ready(host_->query({}), tr("Complete"));
  });
}
void SessionBridge::cancel() { if (current_) { current_->cancel(); } }
} // namespace biglybtc::app
