#include "session_bridge.hpp"
#include "saved_services_presentation.hpp"
#include "profile.hpp"
#include "biglybtc/saved_services.hpp"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <cerrno>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>

namespace {
using namespace biglybtc;
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string output = "d";
  for (const auto& [key, value] : fields) { output += bytes(key) + value; }
  return output + 'e';
}
QByteArray tracker(std::string_view marker, bool empty = false) {
  const auto record = map({{"hash", bytes(std::string(20, 'H'))}, {"status", "i2e"}, {"persistent", "i1e"}});
  return QByteArray::fromStdString(map({{"torrents", empty ? "le" : "l" + record + 'e'},
      {"marker", bytes(marker)}, {"unknown", bytes(std::string("\0\xff", 2))}}));
}
QByteArray sharing(std::string_view marker, bool empty = false) {
  const auto record = map({{"type", "i1e"}, {"file", bytes("/not-opened/bridge-share")},
      {"ihash", bytes(std::string(16, 'F'))}, {"ifile", bytes("/not-opened/cache.torrent")},
      {"per_key", bytes("SYNTHETIC-PRIVATE-DATA-NOT-REAL")}});
  return QByteArray::fromStdString(map({{"resources", empty ? "le" : "l" + record + 'e'},
      {"marker", bytes(marker)}, {"unknown", bytes(std::string("\0\xff", 2))}}));
}
struct Stamp {
  dev_t device;
  ino_t inode;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  nlink_t links;
  off_t size;
  timespec modified;
  timespec changed;
  QByteArray content;
};
std::optional<Stamp> stamp(const QString& path) {
  struct stat info{};
  if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
    if (errno == ENOENT) { return std::nullopt; }
    throw std::runtime_error("Unable to inspect explicit generated fixture pathname");
  }
  QByteArray content;
  if (S_ISREG(info.st_mode)) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { throw std::runtime_error("Unable to read explicit generated fixture"); }
    content = file.readAll();
    if (file.error() != QFileDevice::NoError) { throw std::runtime_error("Generated fixture read failed"); }
  }
  return Stamp{info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_gid,
      info.st_nlink, info.st_size, info.st_mtim, info.st_ctim, std::move(content)};
}
bool sameTime(const timespec& left, const timespec& right) { return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec; }
bool same(const std::optional<Stamp>& left, const std::optional<Stamp>& right) {
  if (left.has_value() != right.has_value()) { return false; }
  if (!left) { return true; }
  // Config reads may update atime. No persistent byte, inode, link, mode,
  // ownership, mtime or ctime change is permitted during this read-only work.
  return left->device == right->device && left->inode == right->inode && left->mode == right->mode
      && left->uid == right->uid && left->gid == right->gid && left->links == right->links && left->size == right->size
      && sameTime(left->modified, right->modified) && sameTime(left->changed, right->changed) && left->content == right->content;
}
struct Observed { QString path; std::optional<Stamp> before; };
struct Profile {
  QTemporaryDir temporary{QStringLiteral("/tmp/biglybtc-saved-services-bridge-XXXXXX")};
  QString path;
  Profile() {
    if (!temporary.isValid() || !QDir(temporary.path()).mkdir(QStringLiteral("profile"))) {
      throw std::runtime_error("Unable to create owned generated profile");
    }
    path = temporary.path() + QStringLiteral("/profile");
    write(QStringLiteral("downloads.config"), "d9:downloadslee");
  }
  void write(const QString& name, const QByteArray& input) {
    QFile file(path + QChar('/') + name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(input) != input.size() || !file.flush()) {
      throw std::runtime_error("Unable to create exclusive generated fixture");
    }
  }
  std::filesystem::path native() const { return std::filesystem::path(path.toStdString()); }
  std::vector<Observed> observe() const {
    std::vector<Observed> result;
    for (const auto& name : {QStringLiteral("downloads.config"), QStringLiteral("tracker.config"), QStringLiteral("sharing.config")}) {
      for (const auto* suffix : {"", ".bak", ".saving"}) {
        const auto full = path + QChar('/') + name + QString::fromLatin1(suffix);
        result.push_back({full, stamp(full)});
      }
    }
    for (const auto& name : {QStringLiteral(".azlock"), QStringLiteral("shares"), QStringLiteral("active")}) {
      const auto full = path + QChar('/') + name;
      result.push_back({full, stamp(full)});
    }
    result.push_back({path, stamp(path)});
    return result;
  }
};
void unchanged(const std::vector<Observed>& before) {
  for (const auto& file : before) { QVERIFY2(same(file.before, stamp(file.path)), "Read-only bridge work changed generated persistent bytes or identity"); }
}
ui::SavedServicesPresentation packet(const QSignalSpy& spy, int row = 0) {
  return qvariant_cast<ui::SavedServicesPresentation>(spy.at(row).at(0));
}
template<class Result>
void validResult(const Result& result, ui::SavedPageState state, const std::optional<ui::SavedPageSource>& source,
    const QByteArray& raw = {}) {
  QCOMPARE(result.state, state);
  QCOMPARE(result.source.has_value(), source.has_value());
  if (source) { QVERIFY(result.source == source); }
  if (state == ui::SavedPageState::Ready) {
    QVERIFY(result.snapshot);
    QVERIFY(result.snapshot->source);
    QCOMPARE(QByteArray::fromStdString(result.snapshot->source->serialize()), raw);
    QVERIFY(result.failure.isEmpty());
  } else {
    QVERIFY(!result.snapshot);
    QVERIFY(!result.source);
    if (state == ui::SavedPageState::Failed) { QVERIFY(!result.failure.isEmpty()); }
    else { QVERIFY(result.failure.isEmpty()); }
  }
}
} // namespace

class SavedServicesBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void independentFileResultsAndGuiDelivery_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("both-primary") << 0;
    QTest::newRow("hosting-backup-sharing-saving") << 1;
    QTest::newRow("both-missing") << 2;
    QTest::newRow("hosting-bad-sharing-good") << 3;
    QTest::newRow("hosting-good-sharing-bad") << 4;
    QTest::newRow("lists-absent") << 5;
    QTest::newRow("valid-empty-lists") << 6;
  }
  void independentFileResultsAndGuiDelivery() {
    QFETCH(int, kind);
    Profile profile;
    QByteArray expectedTracker;
    QByteArray expectedSharing;
    auto hostingState = ui::SavedPageState::Ready;
    auto sharingState = ui::SavedPageState::Ready;
    std::optional<ui::SavedPageSource> hostingSource = ui::SavedPageSource::Primary;
    std::optional<ui::SavedPageSource> sharingSource = ui::SavedPageSource::Primary;
    if (kind == 1) {
      expectedTracker = tracker("backup");
      expectedSharing = sharing("saving");
      profile.write(QStringLiteral("tracker.config"), "invalid synthetic tracker");
      profile.write(QStringLiteral("tracker.config.bak"), expectedTracker);
      profile.write(QStringLiteral("sharing.config.saving"), expectedSharing);
      hostingSource = ui::SavedPageSource::Backup;
      sharingSource = ui::SavedPageSource::Saving;
    } else if (kind == 2) {
      hostingState = sharingState = ui::SavedPageState::Missing;
      hostingSource.reset(); sharingSource.reset();
    } else {
      expectedTracker = kind == 5 ? QByteArray{"de"} : tracker("primary", kind == 6);
      expectedSharing = kind == 5 ? QByteArray{"de"} : sharing("primary", kind == 6);
      profile.write(QStringLiteral("tracker.config"), kind == 3 ? QByteArray{"DO-NOT-EXPOSE-PERSONAL-INPUT"} : expectedTracker);
      profile.write(QStringLiteral("sharing.config"), kind == 4 ? QByteArray{"DO-NOT-EXPOSE-PERSONAL-INPUT"} : expectedSharing);
      if (kind == 3) { hostingState = ui::SavedPageState::Failed; hostingSource.reset(); }
      if (kind == 4) { sharingState = ui::SavedPageState::Failed; sharingSource.reset(); }
    }
    app::SessionBridge bridge;
    QSignalSpy library(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy results(&bridge, &app::SessionBridge::savedServicesReady);
    QVERIFY(bridge.open(profile.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(library.count() == 1 && !bridge.busy(), 30000);
    const auto before = profile.observe(); // Lease creation completed before baseline.
    bool guiThread = false;
    bool busyDuring = false;
    bool rejectedReentry = false;
    QObject::connect(&bridge, &app::SessionBridge::savedServicesReady, &bridge,
        [&](const ui::SavedServicesPresentation&) {
          guiThread = QThread::currentThread() == qApp->thread() && QThread::currentThread() == bridge.thread();
          if (guiThread) { busyDuring = bridge.busy(); rejectedReentry = !bridge.savedServices(99) && !bridge.refresh(); }
        }, Qt::DirectConnection);
    QVERIFY(bridge.savedServices(41));
    QVERIFY(bridge.busy());
    QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1 && !bridge.busy(), 30000);
    QVERIFY(guiThread);
    QVERIFY(busyDuring);
    QVERIFY(rejectedReentry);
    const auto value = packet(results);
    QVERIFY(value);
    QCOMPARE(value->request, quint64{41});
    validResult(value->hosting, hostingState, hostingSource, expectedTracker);
    validResult(value->sharing, sharingState, sharingSource, expectedSharing);
    QVERIFY(!value->hosting.failure.contains(QStringLiteral("DO-NOT-EXPOSE")));
    QVERIFY(!value->sharing.failure.contains(QStringLiteral("DO-NOT-EXPOSE")));
    if (kind == 5 || kind == 6) {
      QVERIFY(value->hosting.snapshot && value->sharing.snapshot);
      QCOMPARE(value->hosting.snapshot->listPresent, kind == 6);
      QCOMPARE(value->sharing.snapshot->listPresent, kind == 6);
      QVERIFY(value->hosting.snapshot->records.empty() && value->sharing.snapshot->records.empty());
    }
    unchanged(before);
  }

  void zeroAndConcurrentRequestsAreRejectedSynchronously() {
    Profile profile;
    app::SessionBridge bridge;
    QSignalSpy busy(&bridge, &app::SessionBridge::busyChanged);
    QSignalSpy results(&bridge, &app::SessionBridge::savedServicesReady);
    QVERIFY(!bridge.savedServices(0));
    QVERIFY(!bridge.busy());
    QCOMPARE(busy.count(), 0);
    QVERIFY(bridge.open(profile.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 30000);
    const auto before = profile.observe();
    busy.clear();
    QVERIFY(bridge.savedServices(1));
    QVERIFY(bridge.busy());
    QVERIFY(!bridge.savedServices(2));
    QVERIFY(!bridge.savedServices(0));
    QVERIFY(!bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1 && !bridge.busy(), 30000);
    const auto value = packet(results);
    QVERIFY(value);
    QCOMPARE(value->request, quint64{1});
    QCOMPARE(busy.count(), 2);
    QCOMPARE(busy.first().at(0).toBool(), true);
    QCOMPARE(busy.last().at(0).toBool(), false);
    unchanged(before);
  }

  void requestIdsAreEchoedAndRemainCallerOwned() {
    Profile profile;
    app::SessionBridge bridge;
    QSignalSpy results(&bridge, &app::SessionBridge::savedServicesReady);
    QVERIFY(bridge.open(profile.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 30000);
    const auto before = profile.observe();
    // The page/owner rejects stale generations. Bridge admission only rejects
    // zero and concurrent work; it must not invent its own generation counter.
    int expected = 0;
    for (const quint64 request : {quint64{50}, quint64{1}, quint64{1}}) {
      QVERIFY(bridge.savedServices(request));
      ++expected;
      QTRY_VERIFY_WITH_TIMEOUT(results.count() == expected && !bridge.busy(), 30000);
      const auto result = packet(results, expected - 1);
      QVERIFY(result);
      QCOMPARE(result->request, request);
    }
    unchanged(before);
  }

  void immediateCancelSuppressesPacketBeforeGuiProcessing() {
    Profile profile;
    profile.write(QStringLiteral("tracker.config"), tracker("cancel"));
    profile.write(QStringLiteral("sharing.config"), sharing("cancel"));
    app::SessionBridge bridge;
    QSignalSpy results(&bridge, &app::SessionBridge::savedServicesReady);
    QVERIFY(bridge.open(profile.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 30000);
    const auto before = profile.observe();
    QVERIFY(bridge.savedServices(1));
    // No event processing occurs between accepted admission and cancellation.
    // This covers even a fast worker that has already queued GUI publication.
    bridge.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 30000);
    QCOMPARE(results.count(), 0);
    // The existing cancellation handler may refresh the library revision. Only
    // persistent bytes and saved-service publication are invariant here.
    unchanged(before);
    QVERIFY(bridge.savedServices(2));
    QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1 && !bridge.busy(), 30000);
    const auto result = packet(results);
    QVERIFY(result);
    QCOMPARE(result->request, quint64{2});
    QCOMPARE(result->hosting.state, ui::SavedPageState::Ready);
    QCOMPARE(result->sharing.state, ui::SavedPageState::Ready);
    unchanged(before);
  }

  void deletionInsideResultSlotIsGuarded() {
    Profile profile;
    profile.write(QStringLiteral("tracker.config"), tracker("delete"));
    auto owner = std::make_unique<app::SessionBridge>();
    auto* bridge = owner.get();
    QPointer guard(bridge);
    QVERIFY(bridge->open(profile.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge->busy(), 30000);
    const auto before = profile.observe();
    ui::SavedServicesPresentation retained;
    bool busyDuring = false;
    int deliveries = 0;
    QObject::connect(bridge, &app::SessionBridge::savedServicesReady, qApp,
        [&](ui::SavedServicesPresentation value) {
          busyDuring = bridge->busy();
          retained = std::move(value);
          ++deliveries;
          owner.reset();
        }, Qt::DirectConnection);
    QVERIFY(bridge->savedServices(7));
    QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 30000);
    QCOMPARE(deliveries, 1);
    QVERIFY(busyDuring);
    QVERIFY(retained);
    QCOMPARE(retained->request, quint64{7});
    QCOMPARE(retained->hosting.state, ui::SavedPageState::Ready);
    unchanged(before);
    auto released = storage::ProfileLease::acquire(profile.native());
    QVERIFY(released.has_value());
    QCoreApplication::processEvents();
  }

  void deletionOnFailureBusyReleaseSuppressesTrailingDelivery() {
    auto owner = std::make_unique<app::SessionBridge>();
    auto* bridge = owner.get();
    QPointer guard(bridge);
    QSignalSpy snapshots(bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy results(bridge, &app::SessionBridge::savedServicesReady);
    QSignalSpy messages(bridge, &app::SessionBridge::messageReady);
    int busyReleases = 0;
    QObject::connect(bridge, &app::SessionBridge::busyChanged, qApp,
        [&](bool busy) {
          if (busy) { return; }
          ++busyReleases;
          owner.reset();
        }, Qt::DirectConnection);
    // This accepted operation has no Host, so the actual worker routes the
    // failure through failed(). Its busy release is a deletion boundary.
    QVERIFY(bridge->savedServices(11));
    QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 30000);
    QCOMPARE(busyReleases, 1);
    QCOMPARE(snapshots.count(), 0);
    QCOMPARE(results.count(), 0);
    QCOMPARE(messages.count(), 0);
    QCoreApplication::processEvents();
    QCOMPARE(messages.count(), 0);
  }

  void deletionOnOpenBusyReleaseSuppressesSnapshot() {
    Profile profile;
    profile.write(QStringLiteral("tracker.config"), tracker("open-delete"));
    auto owner = std::make_unique<app::SessionBridge>();
    auto* bridge = owner.get();
    QPointer guard(bridge);
    QSignalSpy snapshots(bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy results(bridge, &app::SessionBridge::savedServicesReady);
    QSignalSpy messages(bridge, &app::SessionBridge::messageReady);
    std::vector<Observed> before;
    int busyReleases = 0;
    QObject::connect(bridge, &app::SessionBridge::busyChanged, qApp,
        [&](bool busy) {
          if (busy) { return; }
          ++busyReleases;
          // open() acquired its lease before ready() releases busy. Baseline
          // here excludes legitimate lock creation from read-only checks.
          before = profile.observe();
          owner.reset();
        }, Qt::DirectConnection);
    QVERIFY(bridge->open(profile.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 30000);
    QCOMPARE(busyReleases, 1);
    QVERIFY(!before.empty());
    QCOMPARE(snapshots.count(), 0);
    QCOMPARE(results.count(), 0);
    QCOMPARE(messages.count(), 0);
    unchanged(before);
    auto released = storage::ProfileLease::acquire(profile.native());
    QVERIFY(released.has_value());
    QCoreApplication::processEvents();
    QCOMPARE(snapshots.count(), 0);
    QCOMPARE(messages.count(), 0);
  }

  void destructionAfterAcceptedWorkDropsGuiDelivery() {
    Profile profile;
    profile.write(QStringLiteral("tracker.config"), tracker("teardown"));
    profile.write(QStringLiteral("sharing.config"), sharing("teardown"));
    for (int turn = 0; turn < 4; ++turn) {
      auto bridge = std::make_unique<app::SessionBridge>();
      QPointer guard(bridge.get());
      QSignalSpy results(bridge.get(), &app::SessionBridge::savedServicesReady);
      QVERIFY(bridge->open(profile.native(), 0));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge->busy(), 30000);
      const auto before = profile.observe();
      QVERIFY(bridge->savedServices(static_cast<quint64>(turn + 1)));
      // Immediate owner destruction excludes GUI delivery, whether the real
      // worker is still queued, reading, or has posted its receiver-bound event.
      bridge.reset();
      QVERIFY(guard.isNull());
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      QCOMPARE(results.count(), 0);
      unchanged(before);
      auto released = storage::ProfileLease::acquire(profile.native());
      QVERIFY(released.has_value());
    }
  }

  void packetSnapshotsOutliveBridgeAndTemporaryFiles() {
    ui::SavedServicesPresentation retained;
    const auto hostingRaw = tracker("owned");
    const auto sharingRaw = sharing("owned");
    {
      Profile profile;
      profile.write(QStringLiteral("tracker.config"), hostingRaw);
      profile.write(QStringLiteral("sharing.config"), sharingRaw);
      app::SessionBridge bridge;
      QSignalSpy results(&bridge, &app::SessionBridge::savedServicesReady);
      QVERIFY(bridge.open(profile.native(), 0));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 30000);
      const auto before = profile.observe();
      QVERIFY(bridge.savedServices(9));
      QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1 && !bridge.busy(), 30000);
      retained = packet(results);
      QVERIFY(retained);
      unchanged(before);
    }
    QVERIFY(retained);
    validResult(retained->hosting, ui::SavedPageState::Ready, ui::SavedPageSource::Primary, hostingRaw);
    validResult(retained->sharing, ui::SavedPageState::Ready, ui::SavedPageSource::Primary, sharingRaw);
    QVERIFY(retained->hosting.snapshot);
    QVERIFY(retained->sharing.snapshot);
    QCOMPARE(retained->hosting.snapshot->records.size(), std::size_t{1});
    QCOMPARE(retained->sharing.snapshot->records.size(), std::size_t{1});
  }
};

QTEST_MAIN(SavedServicesBridgeTests)
#include "saved_services_bridge_tests.moc"
