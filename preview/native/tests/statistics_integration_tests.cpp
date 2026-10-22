#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace biglybtc;
std::string raw(qint64 downloaded = 1048576) {
  return "d3:alld10:downloadedi" + std::to_string(downloaded)
      + "e8:uploadedi2097152e6:uptimei90061e9:mark_timei0e15:mark_downloadedi1000e"
        "13:mark_uploadedi4194304e11:mark_uptimei61e8:dht_downi7e6:dht_upi8e"
        "10:p_uploadedi9e10:d_uploadedi10e12:p_downloadedi11e12:d_downloadedi12eee";
}
void write(const QString& path, std::string_view value) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
      || file.write(value.data(), static_cast<qint64>(value.size())) != static_cast<qint64>(value.size()) || !file.flush()) {
    throw std::runtime_error("Unable to write generated statistics fixture");
  }
}
std::string read(const QString& path) {
  QFile file(path); if (!file.open(QIODevice::ReadOnly)) { throw std::runtime_error("Generated statistics fixture missing"); }
  return file.readAll().toStdString();
}
struct Profile {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-statistics-integration-XXXXXX")};
  QString path;
  explicit Profile(bool statistics = true) {
    if (!root.isValid() || !QDir(root.path()).mkdir(QStringLiteral("profile"))) { throw std::runtime_error("Generated profile failed"); }
    path = root.path() + QStringLiteral("/profile");
    write(path + QStringLiteral("/downloads.config"), "d9:downloadslee");
    write(path + QStringLiteral("/tables.config"), "de");
    if (statistics) { write(file(), raw()); }
  }
  QString file() const { return path + QStringLiteral("/BiglyBT.statistics"); }
  std::filesystem::path native() const { return path.toStdString(); }
};
QAction* action(ui::LibraryWindow& window, const char* id) { return window.findChild<QAction*>(QString::fromLatin1(id)); }
QLabel* field(ui::StatisticsPage& page, const char* id) {
  auto* value = page.findChild<QLabel*>(QString::fromLatin1(id));
  if (!value) { throw std::runtime_error("Missing semantic statistics label"); } return value;
}
void show(ui::LibraryWindow& w) { w.resize(1400, 900); w.show(); w.activateWindow(); QCoreApplication::processEvents(); }
std::shared_ptr<ui::StatisticsPacket> ready(quint64 session, quint64 request, qint64 value = 1048576) {
  auto parsed = core::readSavedStatistics(raw(value)); if (!parsed) { throw std::runtime_error("Generated raw statistics did not parse"); }
  auto result = std::make_shared<ui::StatisticsPacket>(); result->session = session; result->request = request;
  result->state = ui::StatisticsReadState::Ready; result->source = ui::StatisticsReadSource::Primary;
  result->snapshot = std::make_shared<const core::SavedStatistics>(std::move(*parsed)); return result;
}
void rows(ui::LibraryWindow& w, quint64 revision = 7) {
  auto value = std::make_shared<ui::LibraryPresentation>(); value->revision = revision;
  value->items.push_back({.key = QStringLiteral("A"), .hash = QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"),
      .name = QStringLiteral("Alpha.bin"), .state = QStringLiteral("Stopped"), .bytes = 100, .completed = 50, .received = 51, .sent = 52, .canContinue = true});
  value->items.push_back({.key = QStringLiteral("B"), .hash = QStringLiteral("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"),
      .name = QStringLiteral("Beta.bin"), .state = QStringLiteral("Stopped"), .bytes = 100, .completed = 75, .received = 76, .sent = 77});
  w.setPresentation(value);
}
bool bindSession(ui::LibraryWindow& w) {
  QSignalSpy requests(&w, &ui::LibraryWindow::columnLayoutsRequested); w.beginColumnLayoutSession(73);
  return QTest::qWaitFor([&] { return requests.count() == 1; }, 3000);
}
void syntheticOwner(ui::LibraryWindow& w) {
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&w](quint64 session, quint64 request) {
    w.setBusy(true); QTimer::singleShot(0, &w, [&w, session, request] { w.setStatistics(ready(session, request)); w.setBusy(false); });
  });
}
void wire(ui::LibraryWindow& w, app::SessionBridge& b) {
  QObject::connect(&b, &app::SessionBridge::snapshotReady, &w, &ui::LibraryWindow::setPresentation);
  QObject::connect(&b, &app::SessionBridge::busyChanged, &w, &ui::LibraryWindow::setBusy);
  QObject::connect(&b, &app::SessionBridge::columnLayoutSessionReady, &w, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&b, &app::SessionBridge::columnLayoutsReady, &w, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&b, &app::SessionBridge::columnLayoutSaved, &w, &ui::LibraryWindow::setColumnLayoutSaved);
  QObject::connect(&b, &app::SessionBridge::statisticsReady, &w, &ui::LibraryWindow::setStatistics);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &b, &app::SessionBridge::columnLayouts);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutSaveRequested, &b, &app::SessionBridge::saveColumnLayout);
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &b, &app::SessionBridge::statistics);
  QObject::connect(&w, &ui::LibraryWindow::cancelRequested, &b, &app::SessionBridge::cancel);
}
} // namespace

class StatisticsIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void toolsShortcutReusesPageAndPreservesFavoriteLibraryView() {
    ui::LibraryWindow w; show(w); rows(w); QVERIFY(bindSession(w)); syntheticOwner(w);
    auto* stats = action(w, "classic-statistics"); QVERIFY(stats && stats->isEnabled());
    QCOMPARE(stats->shortcut(), QKeySequence(QStringLiteral("Ctrl+5"))); QCOMPARE(stats->shortcutContext(), Qt::WindowShortcut);
    bool tools = false;
    for (auto* menu : w.findChildren<QMenu*>()) { tools |= menu->title() == QStringLiteral("&Tools") && menu->actions().contains(stats); }
    QVERIFY(tools); w.filter().setText(QStringLiteral("Alpha")); w.incompleteTable().setColumnWidth(1, 247);
    const auto target = w.filesModel().target(); QVERIFY(target);
    QVERIFY(w.continueAction().isEnabled());
    QSignalSpy requests(&w, &ui::LibraryWindow::statisticsRequested), refresh(&w, &ui::LibraryWindow::refreshRequested), transfers(&w, &ui::LibraryWindow::continueRequested);
    w.filter().setFocus(); QTRY_VERIFY(w.filter().hasFocus()); QTest::keyClick(&w.filter(), Qt::Key_5, Qt::ControlModifier);
    QTRY_VERIFY(w.statisticsPage() && w.statisticsPage()->readState() == ui::StatisticsReadState::Ready);
    auto* page = w.statisticsPage(); QCOMPARE(w.mainTabs().currentWidget(), page); QCOMPARE(page->tabs().currentIndex(), 0);
    QCOMPARE(page->sampleCount(), std::size_t{0}); QCOMPARE(requests.count(), 1);
    QCOMPARE(field(*page, "statistics-saved-downloaded")->toolTip(), QStringLiteral("1048576 bytes"));
    stats->trigger(); QCoreApplication::processEvents(); QCOMPARE(w.statisticsPage(), page); QCOMPARE(requests.count(), 1);
    QVERIFY(!w.filesAction().isEnabled()); QVERIFY(!w.continueAction().isEnabled());
    QTest::keyClick(&page->tabs(), Qt::Key_R, Qt::ControlModifier); QCOMPARE(transfers.count(), 0);
    QTest::keyClick(&page->tabs(), Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(w.mainTabs().currentWidget()->objectName(), QStringLiteral("classic-my-torrents-page"));
    QCOMPARE(w.filter().text(), QStringLiteral("Alpha")); QCOMPARE(w.incompleteTable().columnWidth(1), 247);
    QVERIFY(w.filesModel().target() == target); QCOMPARE(refresh.count(), 0); QCOMPARE(w.model().rowCount(), 2);
    QVERIFY(w.continueAction().isEnabled());
  }
  void opensBeforeSessionAndLoadsOnceSessionIsReady() {
    ui::LibraryWindow w; show(w); syntheticOwner(w); QSignalSpy reads(&w, &ui::LibraryWindow::statisticsRequested);
    action(w, "classic-statistics")->trigger(); QVERIFY(w.statisticsPage()); QVERIFY(!w.statisticsPage()->loading()); QCOMPARE(reads.count(), 0);
    QVERIFY(bindSession(w)); QTRY_COMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Ready}); QCOMPARE(reads.count(), 1);
  }
  void closeReopenDuringReadRejectsOldPageAndOldPacket() {
    ui::LibraryWindow w; show(w); QVERIFY(bindSession(w));
    QSignalSpy reads(&w, &ui::LibraryWindow::statisticsRequested), cancels(&w, &ui::LibraryWindow::cancelRequested);
    QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&](quint64, quint64) { w.setBusy(true); });
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(reads.count(), 1);
    QPointer old(w.statisticsPage()); const auto oldRequest = reads.at(0).at(1).toULongLong();
    w.statisticsPage()->tabs().setFocus(); QTRY_VERIFY(w.statisticsPage()->tabs().hasFocus());
    QTest::keyClick(&w.statisticsPage()->tabs(), Qt::Key_W, Qt::ControlModifier);
    QVERIFY(!w.statisticsPage()); QCOMPARE(cancels.count(), 0);
    action(w, "classic-statistics")->trigger(); auto* replacement = w.statisticsPage(); QVERIFY(replacement); QVERIFY(replacement->loading());
    w.setStatistics(ready(73, oldRequest, 111)); w.setBusy(false);
    QTRY_COMPARE(reads.count(), 2); QVERIFY(replacement->loading()); QTRY_VERIFY(old.isNull());
    const auto next = reads.at(1).at(1).toULongLong(); QVERIFY(next > oldRequest);
    w.setStatistics(ready(73, next, 777)); w.setBusy(false);
    QCOMPARE(replacement->readState(), std::optional{ui::StatisticsReadState::Ready});
    w.setStatistics(ready(73, oldRequest, 222)); QCOMPARE(field(*replacement, "statistics-saved-downloaded")->text(), QStringLiteral("777 bytes"));
    QCOMPARE(replacement->tabs().currentIndex(), 0);
  }
  void rejectedStatisticsDrainsReentrantColumnReload() {
    ui::LibraryWindow w; show(w); QVERIFY(bindSession(w)); QSignalSpy reads(&w, &ui::LibraryWindow::statisticsRequested), columns(&w, &ui::LibraryWindow::columnLayoutsRequested);
    QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&](quint64, quint64) { action(w, "classic-reload-column-layout")->trigger(); });
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(reads.count(), 1); QTRY_COMPARE(columns.count(), 1);
    QCOMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Failed});
    action(w, "classic-refresh")->trigger(); QTRY_COMPARE(reads.count(), 2); QTRY_COMPARE(columns.count(), 2);
  }
  void busyRefreshesCoalesceAndOldResultCannotSettleLatestRead() {
    ui::LibraryWindow w; show(w); QVERIFY(bindSession(w));
    QSignalSpy reads(&w, &ui::LibraryWindow::statisticsRequested);
    QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&](quint64, quint64) { w.setBusy(true); });
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(reads.count(), 1);
    auto* page = w.statisticsPage(); const auto first = reads.at(0).at(1).toULongLong();
    // The page refresh remains available while another optional read owns the worker.
    for (int i = 0; i < 3; ++i) { emit page->refreshRequested(); }
    const auto latest = page->request(); QVERIFY(latest == first + 3); QCOMPARE(reads.count(), 1);
    w.setStatistics(ready(73, first, 111)); QVERIFY(page->loading());
    w.setBusy(false); QTRY_COMPARE(reads.count(), 2);
    QCOMPARE(reads.at(1).at(1).toULongLong(), latest);
    w.setStatistics(ready(73, first + 1, 222)); QVERIFY(page->loading());
    w.setStatistics(ready(73, latest, 777)); w.setBusy(false);
    QCOMPARE(page->readState(), std::optional{ui::StatisticsReadState::Ready});
    QCOMPARE(field(*page, "statistics-saved-downloaded")->text(), QStringLiteral("777 bytes"));
    QCoreApplication::processEvents(); QCOMPARE(reads.count(), 2);
  }
  void rejectedSavedServiceDrainsReentrantStatistics() {
    ui::LibraryWindow w; show(w); QVERIFY(bindSession(w)); QSignalSpy reads(&w, &ui::LibraryWindow::statisticsRequested), services(&w, &ui::LibraryWindow::savedServicesRequested);
    QObject::connect(&w, &ui::LibraryWindow::savedServicesRequested, &w, [&](quint64) { action(w, "classic-statistics")->trigger(); });
    action(w, "classic-my-tracker")->trigger(); QTRY_COMPARE(services.count(), 1); QTRY_COMPARE(reads.count(), 1);
    QCOMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Failed});
  }
  void columnReloadWaitsForStatisticsCompletion() {
    ui::LibraryWindow w; show(w); QVERIFY(bindSession(w)); QSignalSpy reads(&w, &ui::LibraryWindow::statisticsRequested), columns(&w, &ui::LibraryWindow::columnLayoutsRequested);
    QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&](quint64, quint64) { w.setBusy(true); });
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(reads.count(), 1);
    action(w, "classic-reload-column-layout")->trigger(); QCoreApplication::processEvents(); QCOMPARE(columns.count(), 0);
    w.setStatistics(ready(73, reads.at(0).at(1).toULongLong())); w.setBusy(false);
    QTRY_COMPARE(columns.count(), 1); QCOMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Ready});
  }
  void realStatisticsWaitsForColumnSaveAndDoesNotRefreshLibrary() {
    Profile p; ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy columns(&bridge, &app::SessionBridge::columnLayoutsReady), writes(&bridge, &app::SessionBridge::columnLayoutSaved);
    QSignalSpy reads(&bridge, &app::SessionBridge::statisticsReady), snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !bridge.busy(), 30000);
    w.incompleteTable().setColumnWidth(1, 321); action(w, "classic-save-column-layout")->trigger(); QVERIFY(bridge.busy());
    action(w, "classic-statistics")->trigger(); QVERIFY(w.statisticsPage()->loading());
    QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && reads.count() == 1 && !bridge.busy(), 30000);
    QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
    QCOMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Ready}); QCOMPARE(snapshots.count(), 1);
    QCOMPARE(read(p.file()), raw()); QCOMPARE(read(p.path + QStringLiteral("/tables.config.bak")), std::string("de"));
    QVERIFY(!QFile::exists(p.file() + QStringLiteral(".bak"))); QVERIFY(!QFile::exists(p.file() + QStringLiteral(".saving")));
  }
  void missingMalformedAndBackupReadNeverBlockLibrary_data() {
    QTest::addColumn<int>("kind"); QTest::newRow("missing") << 0; QTest::newRow("invalid-all") << 1; QTest::newRow("backup") << 2;
  }
  void missingMalformedAndBackupReadNeverBlockLibrary() {
    QFETCH(int, kind); Profile p(false);
    if (kind == 1) { write(p.file(), "d3:alli8ee"); } else if (kind == 2) { write(p.file() + QStringLiteral(".bak"), raw()); }
    ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy columns(&bridge, &app::SessionBridge::columnLayoutsReady), reads(&bridge, &app::SessionBridge::statisticsReady), snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !bridge.busy(), 30000);
    rows(w); w.filter().setText(QStringLiteral("Alpha")); const auto target = w.filesModel().target();
    action(w, "classic-statistics")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !bridge.busy(), 30000);
    const auto expected = kind == 0 ? ui::StatisticsReadState::Missing : kind == 1 ? ui::StatisticsReadState::Failed : ui::StatisticsReadState::Ready;
    QCOMPARE(w.statisticsPage()->readState(), std::optional{expected}); QCOMPARE(snapshots.count(), 1); QCOMPARE(w.model().rowCount(), 2);
    QCOMPARE(w.filter().text(), QStringLiteral("Alpha")); QVERIFY(w.filesModel().target() == target);
    if (kind != 1) { QVERIFY(!QFile::exists(p.file())); }
    else { QCOMPARE(read(p.file()), std::string("d3:alli8ee")); }
    if (kind == 2) { QCOMPARE(read(p.file() + QStringLiteral(".bak")), raw()); }
    QVERIFY(!QFile::exists(p.file() + QStringLiteral(".saving")));
  }
  void realRefreshAndReopenReadNewSavedCountersWithoutWrites() {
    Profile p;
    for (int cycle = 0; cycle < 2; ++cycle) {
      ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
      QSignalSpy columns(&bridge, &app::SessionBridge::columnLayoutsReady), reads(&bridge, &app::SessionBridge::statisticsReady), snapshots(&bridge, &app::SessionBridge::snapshotReady);
      QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !bridge.busy(), 30000);
      action(w, "classic-statistics")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(field(*w.statisticsPage(), "statistics-saved-downloaded")->text(), cycle == 0 ? QStringLiteral("1048576 bytes") : QStringLiteral("777 bytes"));
      if (cycle == 0) {
        write(p.file(), raw(777)); action(w, "classic-refresh")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 2 && !bridge.busy(), 30000);
        QCOMPARE(field(*w.statisticsPage(), "statistics-saved-downloaded")->text(), QStringLiteral("777 bytes"));
      }
      QCOMPARE(snapshots.count(), 1); QCOMPARE(read(p.file()), raw(777));
    }
  }
  void directBridgeBindsSessionAndHoldsBusyThroughResult() {
    Profile p; app::SessionBridge b; QSignalSpy sessions(&b, &app::SessionBridge::columnLayoutSessionReady), reads(&b, &app::SessionBridge::statisticsReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(!b.statistics(1, 1)); QVERIFY(b.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(sessions.count() == 1 && !b.busy(), 30000);
    const auto session = sessions.at(0).at(0).toULongLong(); QVERIFY(!b.statistics(session, 0)); QVERIFY(!b.statistics(session + 1, 1));
    bool held = false, rejected = false;
    QObject::connect(&b, &app::SessionBridge::statisticsReady, &b, [&](ui::StatisticsPresentation result) {
      held = b.busy(); rejected = !b.statistics(session, 8) && !b.refresh(); QVERIFY(result && result->state == ui::StatisticsReadState::Ready); b.cancel();
    });
    QVERIFY(b.statistics(session, 7)); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !b.busy(), 30000);
    QVERIFY(held && rejected); QCOMPARE(snapshots.count(), 1); QCOMPARE(read(p.file()), raw());
  }
  void cancellationBeforeQueuedDeliveryDoesNotPublishOrRefresh() {
    Profile p; ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), reads(&b, &app::SessionBridge::statisticsReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &b, [&](quint64, quint64) { b.cancel(); });
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Failed});
    QTRY_VERIFY(!b.busy()); QCOMPARE(reads.count(), 0); QCOMPARE(snapshots.count(), 1); QCOMPARE(read(p.file()), raw());
  }
  void acceptedPayloadTargetsOnlyAndBoundedHistoryWithoutRetargetingSelection() {
    ui::LibraryWindow w; show(w); rows(w); QVERIFY(bindSession(w)); syntheticOwner(w);
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Ready});
    auto* page = w.statisticsPage(); const auto target = *w.model().targetAt(0); const auto selected = w.filesModel().target();
    auto* activity = page->findChild<QTabWidget*>(QStringLiteral("statistics-activity-views"));
    QVERIFY(activity); activity->setCurrentIndex(0); // This fixture exercises the retained Payload view.
    w.setTelemetry({target, 50, 8192, 1024, QStringLiteral("Downloading")}); QCOMPARE(page->sampleCount(), std::size_t{1});
    auto stale = target; ++stale.generation; w.setTelemetry({stale, 51, 999, 999, QStringLiteral("Stale")});
    w.setTelemetry({target, 50, -1, 1024, QStringLiteral("Invalid")}); QCOMPARE(page->sampleCount(), std::size_t{1});
    action(w, "classic-my-torrents")->trigger();
    for (int i = 0; i < 625; ++i) { w.setTelemetry({target, 50, i, i + 1, QStringLiteral("Downloading")}); }
    QCOMPARE(page->sampleCount(), ui::StatisticsPage::MaximumSamples); QVERIFY(w.filesModel().target() == selected);
    const auto other = *w.model().targetAt(1); w.setTelemetry({other, 75, 12, 13, QStringLiteral("Downloading")});
    QCOMPARE(page->sampleCount(), std::size_t{1}); QVERIFY(w.filesModel().target() == selected);
    action(w, "classic-statistics")->trigger(); QTRY_VERIFY(field(*page, "statistics-transfer-name")->text().contains(QStringLiteral("Beta.bin")));
  }
  void modelReplacementDuringTelemetryDoesNotRecordObsoleteGeneration() {
    ui::LibraryWindow w; show(w); rows(w); QVERIFY(bindSession(w)); syntheticOwner(w);
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(w.statisticsPage()->readState(), std::optional{ui::StatisticsReadState::Ready});
    const auto target = *w.model().targetAt(0); bool once = true;
    QObject::connect(&w.model(), &QAbstractItemModel::dataChanged, &w, [&] { if (once) { once = false; rows(w, 8); } });
    w.setTelemetry({target, 50, 1, 2, QStringLiteral("Old generation")}); QCOMPARE(w.statisticsPage()->sampleCount(), std::size_t{0});
  }
  void deletingWindowInsideStatisticsDispatchIsSafe() {
    auto owner = std::make_unique<ui::LibraryWindow>(); show(*owner); QVERIFY(bindSession(*owner)); QPointer guard(owner.get());
    QObject::connect(owner.get(), &ui::LibraryWindow::statisticsRequested, qApp, [&](quint64, quint64) { owner.reset(); });
    action(*owner, "classic-statistics")->trigger(); QTRY_VERIFY(guard.isNull());
  }
  void deletingBridgeInsideStatisticsResultIsSafe() {
    Profile p; auto b = std::make_unique<app::SessionBridge>(); QPointer guard(b.get());
    QSignalSpy sessions(b.get(), &app::SessionBridge::columnLayoutSessionReady);
    QVERIFY(b->open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(sessions.count() == 1 && !b->busy(), 30000);
    QObject::connect(b.get(), &app::SessionBridge::statisticsReady, qApp, [&](ui::StatisticsPresentation) { b.reset(); });
    QVERIFY(b->statistics(sessions.at(0).at(0).toULongLong(), 1)); QTRY_VERIFY(guard.isNull());
  }
};
QTEST_MAIN(StatisticsIntegrationTests)
#include "statistics_integration_tests.moc"
