#include "profile.hpp"
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "manual_tag_view.hpp"
#include <QBuffer>
#include <QFile>
#include <QPixmap>
#include <QTableView>
#include <cerrno>
#include <fcntl.h>

#include "owned_engine_fixture.hpp"
#include "biglybtc/manual_tag_statistics.hpp"

#include <QApplication>
#include <QPointer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
std::atomic<bool> watched{false};
std::atomic<std::uintmax_t> watchedDevice{0}, watchedInode{0};
std::atomic<unsigned> tagReads{0};
struct Watch {
  explicit Watch(const QString& filename) {
    struct stat info{}; test::require(::stat(filename.toStdString().c_str(), &info) == 0, "Cannot watch owned tag file");
    watchedDevice = static_cast<std::uintmax_t>(info.st_dev); watchedInode = static_cast<std::uintmax_t>(info.st_ino);
    tagReads = 0; watched = true;
  }
  ~Watch() { watched = false; }
};
struct stat tagStamp(const QString& path) {
  struct stat value{}; test::require(::stat(path.toStdString().c_str(), &value) == 0, "Cannot stamp owned tag file");
  return value;
}
bool sameTag(const struct stat& before, const QString& path) {
  const auto after = tagStamp(path);
  return before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size
      && before.st_mode == after.st_mode && before.st_nlink == after.st_nlink
      && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
      && before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
}
std::string base32(const core::TorrentHash& hash) {
  constexpr auto alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::uint32_t pending = 0; unsigned bits = 0; std::string result;
  for (const auto byte : hash.bytes()) {
    pending = (pending << 8) | std::to_integer<unsigned char>(byte); bits += 8;
    while (bits >= 5) { bits -= 5; result += alphabet[(pending >> bits) & 31]; }
  }
  if (bits) { result += alphabet[(pending << (5 - bits)) & 31]; }
  return result;
}
QByteArray tags(const core::TorrentHash& target) {
  libtorrent::entry config(libtorrent::entry::dictionary_type{});
  config["3"]["1"]["n"] = "Actual";
  config["3"]["1"]["o"] = libtorrent::entry::list_type{libtorrent::entry(base32(target))};
  config["3"]["2"]["n"] = "Empty";
  config["3"]["2"]["o"] = libtorrent::entry::list_type{};
  const auto unknown = core::TorrentHash::parse(std::string(20, 'z'));
  test::require(unknown.has_value(), "Cannot prepare unresolved member");
  config["3"]["3"]["n"] = "Unresolved";
  config["3"]["3"]["o"] = libtorrent::entry::list_type{libtorrent::entry(base32(*unknown))};
  config["3"]["c"]["opaque"] = std::string("\0\xffQ", 3);
  config["77"]["1"]["n"] = "Unadopted other tag type";
  config["unknown-root"] = std::string("\0\xffR", 3);
  return QByteArray::fromStdString(test::encoded(config));
}
ui::ManualTagStatisticsPresentation latest(const QSignalSpy& spy) {
  return spy.empty() ? ui::ManualTagStatisticsPresentation{}
      : qvariant_cast<ui::ManualTagStatisticsPresentation>(spy.last().at(0));
}
ui::NativeSessionPresentation native(const QSignalSpy& spy) {
  return spy.empty() ? ui::NativeSessionPresentation{}
      : qvariant_cast<ui::NativeSessionPresentation>(spy.last().at(0));
}
ui::Target target(const QSignalSpy& spy) {
  const auto rows = qvariant_cast<ui::Presentation>(spy.last().at(0));
  test::require(rows && rows->items.size() == 1, "Expected one generated library row");
  return {.hash = rows->items.front().hash, .generation = rows->revision};
}
bool activePositive(const ui::ManualTagStatisticsPresentation& p) {
  return p && p->readState == ui::ManualTagReadState::Ready && p->traffic.current
      && p->traffic.state == core::ManualTagTrafficState::Active && p->traffic.target
      && p->traffic.target->payloadDownloadRate > 0;
}
bool sawPositive(const QSignalSpy& spy) {
  return std::any_of(spy.begin(), spy.end(), [](const auto& row) {
    return activePositive(qvariant_cast<ui::ManualTagStatisticsPresentation>(row.at(0)));
  });
}
// Optional owned output only. A directory descriptor anchors exclusive0600
// writes; canonical path/owner/mode/identity checks reject aliases and links.
class CaptureDirectory final {
public:
  explicit CaptureDirectory(QString path) : path_(std::move(path)) {
    const QFileInfo info(path_);
    if (!info.isAbsolute() || !info.isDir() || info.isSymLink() || info.canonicalFilePath() != path_) {
      throw std::runtime_error("Manual Tag capture directory must be canonical and private");
    }
    const auto encoded = QFile::encodeName(path_);
    descriptor_ = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (!valid()) {
      if (descriptor_ >= 0) { static_cast<void>(::close(descriptor_)); descriptor_ = -1; }
      throw std::runtime_error("Manual Tag capture directory must be owned0700");
    }
  }
  ~CaptureDirectory() { if (descriptor_ >= 0) { static_cast<void>(::close(descriptor_)); } }
  CaptureDirectory(const CaptureDirectory&) = delete;
  CaptureDirectory& operator=(const CaptureDirectory&) = delete;
  bool save(ui::ManualTagStatisticsView& view, const ui::ManualTagStatisticsPresentation& expected,
      const char* basename) const {
    if (view.presentation() != expected) { return false; }
    QByteArray bytes; QBuffer stream(&bytes);
    if (!stream.open(QIODevice::WriteOnly) || !view.grab().save(&stream, "PNG") || !valid()
        || view.presentation() != expected || !view.isVisible() || view.isMinimized()) { return false; }
    const int output = ::openat(descriptor_, basename, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (output < 0) { return false; }
    bool success = true; qsizetype offset = 0;
    while (offset < bytes.size()) {
      const auto written = ::write(output, bytes.constData() + offset, static_cast<std::size_t>(bytes.size() - offset));
      if (written < 0 && errno == EINTR) { continue; }
      if (written <= 0) { success = false; break; }
      offset += written;
    }
    if (success && ::fsync(output) != 0) { success = false; }
    if (::close(output) != 0) { success = false; }
    if (success && ::fsync(descriptor_) != 0) { success = false; }
    return success;
  }
private:
  bool valid() const {
    struct stat held{}, named{}; const auto encoded = QFile::encodeName(path_);
    return descriptor_ >= 0 && ::fstat(descriptor_, &held) == 0 && ::lstat(encoded.constData(), &named) == 0
        && S_ISDIR(held.st_mode) && S_ISDIR(named.st_mode) && held.st_uid == ::geteuid()
        && (held.st_mode & 0777) == 0700 && held.st_dev == named.st_dev && held.st_ino == named.st_ino;
  }
  QString path_;
  int descriptor_ = -1;
};
bool positiveTagRendered(ui::ManualTagStatisticsView& view, const ui::ManualTagStatisticsPresentation& packet) {
  if (!activePositive(packet) || view.presentation() != packet || !view.isVisible() || view.isMinimized()) { return false; }
  const auto* table = view.findChild<QTableView*>(QStringLiteral("manual-tag-table"));
  if (!table || !table->model()) { return false; }
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    const auto name = table->model()->index(row, 0);
    const auto down = table->model()->index(row, 3);
    if (name.data().toString() == QStringLiteral("Actual")
        && down.data(Qt::UserRole).isValid() && down.data(Qt::UserRole).toLongLong() == packet->traffic.target->downloadRate
        && down.data(Qt::UserRole).toLongLong() > 0) { return true; }
  }
  return false;
}
} // namespace

extern "C" ssize_t __real_read(int, void*, size_t);
extern "C" ssize_t __wrap_read(int descriptor, void* bytes, size_t size) {
  if (watched.load()) {
    struct stat info{};
    if (::fstat(descriptor, &info) == 0
        && static_cast<std::uintmax_t>(info.st_dev) == watchedDevice.load()
        && static_cast<std::uintmax_t>(info.st_ino) == watchedInode.load()) { ++tagReads; }
  }
  return __real_read(descriptor, bytes, size);
}

class ManualTagBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void offlineReadsAreLazyRefreshesAreExplicitAndFailuresInvalidateReady() {
    test::OwnedEngineFixture fixture(false);
    const auto path = fixture.profile + QStringLiteral("/tag.config");
    const auto raw = tags(*fixture.hash); test::write(path, raw); Watch watch(path);
    app::SessionBridge bridge; QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady), packets(&bridge, &app::SessionBridge::manualTagStatisticsReady);
    QVERIFY(bridge.open(fixture.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty() && !sessions.empty(), 10000);
    QCOMPARE(tagReads.load(), 0U); QCOMPARE(packets.count(), 0);
    const auto session = sessions.last().at(0).toULongLong();
    QVERIFY(!bridge.manualTagStatistics(0)); QVERIFY(!bridge.manualTagStatistics(session + 1));
    bool busyDuringPacket = false, reentryRejected = false;
    connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, &bridge, [&](auto) {
      if (bridge.busy()) { busyDuringPacket = true; reentryRejected = !bridge.manualTagStatistics(session); }
    });
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets), 10000);
    const auto first = latest(packets); QCOMPARE(first->readState, ui::ManualTagReadState::Ready);
    QVERIFY(first->membership); QVERIFY(tagReads.load() > 0); QVERIFY(busyDuringPacket && reentryRejected);
    QCOMPARE(first->epoch, quint64{0}); QCOMPARE(first->traffic.state, core::ManualTagTrafficState::Unavailable);
    auto rates = core::projectManualTagRates(*first->membership, first->traffic); QVERIFY(rates);
    QVERIFY(!rates->at(0).downloadRate); QVERIFY(!rates->at(1).downloadRate);
    const auto oldReads = tagReads.load();
    QVERIFY(bridge.refresh()); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && rows.count() >= 2, 10000);
    QTRY_VERIFY(latest(packets)->libraryRevision > first->libraryRevision);
    QCOMPARE(tagReads.load(), oldReads); // Library join uses cached bytes, no tag read.
    QVERIFY(latest(packets)->membership != first->membership);
    QCOMPARE(latest(packets)->tagRevision, first->tagRevision);
    test::write(path, QByteArray("malformed"));
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets)->tagRevision > first->tagRevision, 10000);
    const auto failed = latest(packets); QCOMPARE(failed->readState, ui::ManualTagReadState::Failed);
    QVERIFY(!failed->membership); QVERIFY(!failed->failure.isEmpty());
    QCOMPARE(failed->traffic.state, core::ManualTagTrafficState::Unavailable);
    QVERIFY(failed->sequence > first->sequence);
    QCOMPARE(test::read(path), QByteArray("malformed"));
    test::write(path, raw); const auto beforeRetry = failed->tagRevision;
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets)->tagRevision > beforeRetry, 10000);
    QCOMPARE(latest(packets)->readState, ui::ManualTagReadState::Ready); QCOMPARE(test::read(path), raw);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak"))); QVERIFY(!QFileInfo::exists(path + QStringLiteral(".saving")));
    QVERIFY(fixture.intact(false));
  }
  void actualSingleAndMultifileRatesAndJoinedClose_data() {
    QTest::addColumn<bool>("multifile"); QTest::newRow("single") << false; QTest::newRow("multifile") << true;
  }
  void actualSingleAndMultifileRatesAndJoinedClose() {
    QFETCH(bool, multifile); test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto path = fixture.profile + QStringLiteral("/tag.config");
    const auto raw = tags(*fixture.hash); test::write(path, raw); const auto before = tagStamp(path); Watch watch(path);
    app::SessionBridge bridge; QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady), packets(&bridge, &app::SessionBridge::manualTagStatisticsReady);
    QSignalSpy owner(&bridge, &app::SessionBridge::nativeSessionReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty() && !sessions.empty(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(native(owner) && native(owner)->state == ui::NativeSessionState::Idle && native(owner)->sample, 10000);
    QCOMPARE(tagReads.load(), 0U); QCOMPARE(packets.count(), 0);
    const auto session = sessions.last().at(0).toULongLong();
    const auto capturePath = qEnvironmentVariable("BIGLYBTC_MANUAL_TAG_CAPTURE_DIRECTORY");
    std::unique_ptr<CaptureDirectory> captureDirectory;
    std::unique_ptr<ui::ManualTagStatisticsView> captureView;
    bool captured = false; QString captureFailure;
    if (!capturePath.isEmpty()) {
      captureDirectory = std::make_unique<CaptureDirectory>(capturePath);
      captureView = std::make_unique<ui::ManualTagStatisticsView>();
      QVERIFY(captureView->beginSession(session)); captureView->resize(1180, 650); captureView->show();
      // No readRequested connection: the existing explicit read remains the only
      // tag IO. The module renders actual packets from the real Bridge signal.
      connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, captureView.get(),
          &ui::ManualTagStatisticsView::setPresentation);
      connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, captureView.get(), [&](auto packet) {
        if (captured || !captureFailure.isEmpty() || !bridge.busy()) { return; }
        try {
          if (!positiveTagRendered(*captureView, packet)) { return; }
          captured = captureDirectory->save(*captureView, packet,
              multifile ? "actual-manual-tags-multi.png" : "actual-manual-tags-single.png");
          if (!captured) { captureFailure = QStringLiteral("Cannot save owned real Manual Tag capture"); }
        } catch (...) { captureFailure = QStringLiteral("Cannot render owned real Manual Tag capture"); }
      });
    }
    QVERIFY(bridge.manualTagStatistics(session));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets) && latest(packets)->traffic.state == core::ManualTagTrafficState::Idle, 10000);
    const auto idle = latest(packets); QVERIFY(idle->epoch); QVERIFY(idle->traffic.completeOwnedCoverage);
    auto zero = core::projectManualTagRates(*idle->membership, idle->traffic); QVERIFY(zero);
    QCOMPARE(zero->at(0).downloadRate, std::optional<std::int64_t>{0});
    const auto readsAfterRequest = tagReads.load(); QVERIFY(readsAfterRequest > 0);
    QVERIFY(bridge.continueDownload(target(rows)));
    QTRY_VERIFY_WITH_TIMEOUT(sawPositive(packets), 30000);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && rows.count() >= 2, 30000);
    bool checkedPositive = false;
    quint64 previous = 0;
    for (const auto& row : packets) {
      const auto p = qvariant_cast<ui::ManualTagStatisticsPresentation>(row.at(0));
      QCOMPARE(p->session, session); QVERIFY(p->sequence > previous); previous = p->sequence;
      if (p->readState == ui::ManualTagReadState::Ready && p->libraryRevision == idle->libraryRevision
          && p->tagRevision == idle->tagRevision) { QVERIFY(p->membership == idle->membership); }
      if (!activePositive(p)) { continue; }
      QVERIFY(p->traffic.target->hash == *fixture.hash); QVERIFY(p->traffic.completeOwnedCoverage);
      auto rates = core::projectManualTagRates(*p->membership, p->traffic); QVERIFY(rates);
      QCOMPARE(rates->at(0).state, core::ManualTagRateState::ObservedActive);
      QCOMPARE(rates->at(0).downloadRate, std::optional{p->traffic.target->downloadRate});
      QCOMPARE(rates->at(1).state, core::ManualTagRateState::ObservedOtherTagZero);
      QCOMPARE(rates->at(1).downloadRate, std::optional<std::int64_t>{0});
      QCOMPARE(rates->at(2).state, core::ManualTagRateState::UnresolvedMembership);
      QVERIFY(!rates->at(2).downloadRate); checkedPositive = true;
    }
    QVERIFY(checkedPositive); QCOMPARE(tagReads.load(), readsAfterRequest);
    if (captureDirectory) { QVERIFY2(captured, qPrintable(captureFailure.isEmpty()
        ? QStringLiteral("No actual positive Manual Tag frame was captured") : captureFailure)); }
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QTRY_VERIFY(latest(packets) && !latest(packets)->traffic.current);
    QVERIFY(!bridge.manualTagStatistics(session)); // Closed worker cannot reread.
    QCOMPARE(tagReads.load(), readsAfterRequest); QCOMPARE(test::read(path), raw); QVERIFY(sameTag(before, path));
    QVERIFY(fixture.accountingIntact());
    QVERIFY(idle->traffic.current); QCOMPARE(idle->traffic.state, core::ManualTagTrafficState::Idle);
  }
  void cancellationRetiresActualHandleWithoutTagWrites() {
    test::OwnedEngineFixture fixture(false, true); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto path = fixture.profile + QStringLiteral("/tag.config"); const auto raw = tags(*fixture.hash); test::write(path, raw);
    app::SessionBridge bridge; QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy packets(&bridge, &app::SessionBridge::manualTagStatisticsReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port())); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !sessions.empty(), 10000);
    const auto session = sessions.last().at(0).toULongLong();
    QVERIFY(bridge.manualTagStatistics(session)); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets), 10000);
    bool cancelled = false;
    connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, &bridge, [&](auto packet) {
      if (!cancelled && bridge.busy() && activePositive(packet)) { cancelled = true; bridge.cancel(); }
    });
    QVERIFY(bridge.continueDownload(target(rows)));
    QTRY_VERIFY_WITH_TIMEOUT(cancelled && !bridge.busy(), 30000);
    QCOMPARE(test::read(path), raw); QVERIFY(fixture.accountingIntact(false));
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QTRY_VERIFY(latest(packets) && !latest(packets)->traffic.current);
  }
  void closeThenNewProfilePublishesANewSessionAndRejectsOldRequests() {
    test::OwnedEngineFixture first(false), second(false); app::SessionBridge bridge; ui::LibraryWindow window;
    QTRY_VERIFY_WITH_TIMEOUT(second.ready(), 30000);
    auto inventory = test::read(second.profile + QStringLiteral("/downloads.config"));
    QCOMPARE(inventory.count("10:downloadedi0e"), 1);
    inventory.replace("10:downloadedi0e", "10:downloadedi42e");
    test::write(second.profile + QStringLiteral("/downloads.config"), inventory);
    QVERIFY(*first.hash == *second.hash); // Same identity; generation must carry profile fencing.
    connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
    connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &window, &ui::LibraryWindow::beginColumnLayoutSession);
    connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
    QSignalSpy operations(&bridge, &app::SessionBridge::engineStatisticsReady), messages(&bridge, &app::SessionBridge::messageReady);
    QSignalSpy owner(&bridge, &app::SessionBridge::nativeSessionReady);
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady), packets(&bridge, &app::SessionBridge::manualTagStatisticsReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(first.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && sessions.count() == 1, 10000);
    const auto oldSession = sessions.first().at(0).toULongLong();
    QVERIFY(window.model().targetAt(0)); const auto oldTarget = *window.model().targetAt(0);
    QVERIFY(bridge.manualTagStatistics(oldSession)); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets), 10000);
    QCOMPARE(latest(packets)->readState, ui::ManualTagReadState::Missing);
    bool attemptedEarly = false, openedEarly = false, reopenedInAck = false, closedCommandsRejected = false;
    connect(&bridge, &app::SessionBridge::busyChanged, &bridge, [&](bool busy) {
      if (!busy && !attemptedEarly) { attemptedEarly = true; openedEarly = bridge.open(second.native(), second.port()); }
    });
    connect(&bridge, &app::SessionBridge::sessionClosed, &bridge, [&](quint64 session, bool, bool) {
      if (session == oldSession) {
        closedCommandsRejected = !bridge.refresh() && !bridge.savedServices(1)
            && !bridge.details(oldTarget) && !bridge.continueDownload(oldTarget);
        reopenedInAck = bridge.open(second.native(), second.port());
      }
    });
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(attemptedEarly); QVERIFY(!openedEarly); QVERIFY(closedCommandsRejected); QVERIFY(reopenedInAck);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && sessions.count() == 2, 10000);
    const auto nextSession = sessions.last().at(0).toULongLong(); QVERIFY(nextSession > oldSession);
    QTRY_VERIFY_WITH_TIMEOUT(native(owner) && native(owner)->session == nextSession
        && native(owner)->state == ui::NativeSessionState::Idle && native(owner)->sample, 10000);
    // The existing idle monitor was stopped for close and must resume for the new owner.
    QVERIFY(native(owner)->epoch);
    QVERIFY(window.model().targetAt(0)); const auto nextTarget = *window.model().targetAt(0);
    QCOMPARE(nextTarget.hash, oldTarget.hash); QVERIFY(nextTarget.generation > oldTarget.generation);
    QCOMPARE(window.model().data(window.model().index(0, 6), Qt::UserRole).toLongLong(), qlonglong{42});
    QVERIFY(bridge.continueDownload(oldTarget));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !messages.empty(), 10000);
    QCOMPARE(operations.count(), 0); QVERIFY(messages.last().at(0).toString().contains(QStringLiteral("stale")));
    QVERIFY(!bridge.manualTagStatistics(oldSession)); QVERIFY(bridge.manualTagStatistics(nextSession));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets)->session == nextSession, 10000);
    QCOMPARE(latest(packets)->readState, ui::ManualTagReadState::Missing);
    QCOMPARE(latest(packets)->epoch, native(owner)->epoch); QVERIFY(latest(packets)->tagRevision);
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 2, 15000);
  }
  void directPacketReceiverMayDeleteBridgeWithoutTrailingBusySignal() {
    test::OwnedEngineFixture fixture(false); const auto raw = tags(*fixture.hash);
    test::write(fixture.profile + QStringLiteral("/tag.config"), raw);
    QObject cleanup; auto* bridge = new app::SessionBridge(&cleanup); QPointer<app::SessionBridge> alive(bridge);
    QSignalSpy sessions(bridge, &app::SessionBridge::columnLayoutSessionReady);
    QVERIFY(bridge->open(fixture.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(!bridge->busy() && !sessions.empty(), 10000);
    const auto session = sessions.first().at(0).toULongLong();
    bool deleted = false; int trailingIdle = 0;
    connect(bridge, &app::SessionBridge::busyChanged, &cleanup, [&](bool busy) { if (!busy) { ++trailingIdle; } });
    connect(bridge, &app::SessionBridge::manualTagStatisticsReady, &cleanup, [&](auto packet) {
      if (packet->session == session) { QVERIFY(bridge->busy()); delete bridge; deleted = true; }
    });
    QVERIFY(bridge->manualTagStatistics(session)); QTRY_VERIFY(deleted && alive.isNull()); QCOMPARE(trailingIdle, 0);
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
  }
};
QTEST_MAIN(ManualTagBridgeTests)
#include "manual_tag_bridge_tests.moc"
