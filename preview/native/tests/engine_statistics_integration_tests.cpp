#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"
#include "owned_engine_fixture.hpp"
#include "local_session.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

namespace {
using namespace biglybtc;
QAction* action(ui::LibraryWindow& w, const char* id) { return w.findChild<QAction*>(QString::fromLatin1(id)); }
QLabel* field(ui::StatisticsPage& page, const char* id) {
  auto* label = page.findChild<QLabel*>(QString::fromLatin1(id)); test::require(label, "Missing engine statistics label"); return label;
}
void show(ui::LibraryWindow& w) { w.resize(1280, 900); w.show(); w.activateWindow(); QCoreApplication::processEvents(); }
void wire(ui::LibraryWindow& w, app::SessionBridge& b) {
  QObject::connect(&b, &app::SessionBridge::snapshotReady, &w, &ui::LibraryWindow::setPresentation);
  QObject::connect(&b, &app::SessionBridge::progressReady, &w, &ui::LibraryWindow::setTelemetry);
  QObject::connect(&b, &app::SessionBridge::busyChanged, &w, &ui::LibraryWindow::setBusy);
  QObject::connect(&b, &app::SessionBridge::columnLayoutSessionReady, &w, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&b, &app::SessionBridge::columnLayoutsReady, &w, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&b, &app::SessionBridge::statisticsReady, &w, &ui::LibraryWindow::setStatistics);
  QObject::connect(&b, &app::SessionBridge::engineStatisticsReady, &w, &ui::LibraryWindow::setEngineStatistics);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &b, &app::SessionBridge::columnLayouts);
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &b, &app::SessionBridge::statistics);
  QObject::connect(&w, &ui::LibraryWindow::continueRequested, &b, &app::SessionBridge::continueDownload);
  QObject::connect(&w, &ui::LibraryWindow::cancelRequested, &b, &app::SessionBridge::cancel);
}
void fakeRows(ui::LibraryWindow& w) {
  auto rows = std::make_shared<ui::LibraryPresentation>(); rows->revision = 9;
  rows->items.push_back({.key = QStringLiteral("A"), .hash = QString(40, QLatin1Char('A')), .name = QStringLiteral("Alpha"),
      .state = QStringLiteral("Queued"), .bytes = 100, .completed = 0, .canContinue = true});
  w.setPresentation(rows);
}
bool fakeSession(ui::LibraryWindow& w, quint64 session = 73) {
  QSignalSpy reads(&w, &ui::LibraryWindow::columnLayoutsRequested); w.beginColumnLayoutSession(session);
  return QTest::qWaitFor([&] { return reads.count() == 1; }, 3000);
}
auto preparing(ui::LibraryWindow& w, quint64 operation = 1) {
  auto p = std::make_shared<ui::EngineStatisticsPacket>(); p->session = 73; p->operation = operation; p->sequence = 1;
  p->target = *w.model().targetAt(0); p->name = QStringLiteral("Alpha"); return p;
}
auto observed(ui::LibraryWindow& w, quint64 operation = 1) {
  auto p = preparing(w, operation); p->sequence = 2; p->state = ui::EngineStatisticsState::Observed;
  p->sample = core::EngineStatistics{{1000, 200, 800, 100}, 500000000, core::EngineRates{2000, 400, 1600, 200, 400, 200}};
  return p;
}
ui::StatisticsPage* statistics(ui::LibraryWindow& w) {
  action(w, "classic-statistics")->trigger(); auto* page = w.statisticsPage();
  test::require(page, "Statistics did not open"); page->tabs().setCurrentIndex(1); QCoreApplication::processEvents(); return page;
}
ui::EngineStatisticsPresentation packetAt(const QSignalSpy& spy, int index) {
  return qvariant_cast<ui::EngineStatisticsPresentation>(spy.at(index).at(0));
}
} // namespace

class EngineStatisticsIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void windowCachesActualFactsAcrossCloseReopenAndRejectsUnboundTargets() {
    ui::LibraryWindow w; show(w); fakeRows(w); QVERIFY(fakeSession(w));
    auto bad = preparing(w); ++bad->target.generation; w.setEngineStatistics(bad);
    auto* page = statistics(w);
    QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("Unavailable"));
    w.setEngineStatistics(preparing(w)); w.setEngineStatistics(observed(w));
    QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    const auto target = w.filesModel().target(); w.filter().setText(QStringLiteral("Alpha")); w.incompleteTable().setColumnWidth(1, 247);
    auto ended = observed(w); ended->sequence = 3; ended->state = ui::EngineStatisticsState::Ended; ended->sample.reset();
    w.setEngineStatistics(ended);
    action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage());
    page = statistics(w);
    QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    QVERIFY(field(*page, "statistics-engine-status")->text().contains(QStringLiteral("last observed"), Qt::CaseInsensitive));
    auto late = observed(w); late->sequence = 99; late->sample->counters.received = 2000;
    w.setEngineStatistics(late); QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    action(w, "classic-my-torrents")->trigger(); QCOMPARE(w.filter().text(), QStringLiteral("Alpha"));
    QCOMPARE(w.incompleteTable().columnWidth(1), 247); QVERIFY(w.filesModel().target() == target);
  }
  void newOperationAndProfileNeverInheritPreviousEngineFacts() {
    ui::LibraryWindow w; show(w); fakeRows(w); QVERIFY(fakeSession(w));
    auto* page = statistics(w); w.setEngineStatistics(preparing(w)); w.setEngineStatistics(observed(w));
    w.setEngineStatistics(preparing(w, 2));
    QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("Unavailable"));
    w.setEngineStatistics(observed(w)); QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("Unavailable"));
    auto unknown = preparing(w, 3); unknown->target.hash = QString(40, QLatin1Char('B')); w.setEngineStatistics(unknown);
    w.setEngineStatistics(observed(w, 2)); QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    QVERIFY(fakeSession(w, 74));
    QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("Unavailable"));
    action(w, "classic-close-tab")->trigger(); page = statistics(w);
    w.setEngineStatistics(observed(w, 3)); QCOMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("Unavailable"));
  }
  void realMeasurementsReachWindowAndSurviveTabReopening_data() {
    QTest::addColumn<bool>("multifile"); QTest::newRow("single-file") << false; QTest::newRow("multifile") << true;
  }
  void realMeasurementsReachWindowAndSurviveTabReopening() {
    QFETCH(bool, multifile); test::OwnedEngineFixture f(multifile); QTRY_VERIFY_WITH_TIMEOUT(f.ready(), 30000);
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), updates(&b, &app::SessionBridge::engineStatisticsReady);
    QVERIFY(b.open(f.native(), f.port())); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    QCOMPARE(w.model().rowCount(), 1); QVERIFY(w.continueAction().isEnabled());
    const auto target = w.model().targetAt(0); QVERIFY(target);
    w.incompleteTable().setColumnWidth(1, 247); w.continueAction().trigger(); QVERIFY(b.busy());
    auto* page = statistics(w); QVERIFY(!w.continueAction().isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && updates.count() >= 3
        && packetAt(updates, updates.count() - 1)->state == ui::EngineStatisticsState::Ended, 30000);
    const auto last = packetAt(updates, updates.count() - 1); QVERIFY(last->successful); QVERIFY(last->sample);
    QCOMPARE(last->name, multifile ? QStringLiteral("bundle") : QStringLiteral("sample.bin"));
    QCOMPARE(last->target, *target); QVERIFY(last->sample->counters.payloadReceived > 0);
    QVERIFY(last->sample->counters.payloadReceived <= test::OwnedEngineFixture::PayloadBytes);
    QVERIFY(last->sample->counters.received > last->sample->counters.payloadReceived);
    bool measuredRate = false; quint64 sequence = 0;
    QCOMPARE(packetAt(updates, 0)->state, ui::EngineStatisticsState::Preparing);
    for (int i = 0; i < updates.count(); ++i) {
      const auto p = packetAt(updates, i); QCOMPARE(p->session, last->session); QCOMPARE(p->operation, last->operation);
      QVERIFY(p->sequence > sequence); sequence = p->sequence;
      if (p->sample) { QVERIFY(core::validEngineStatistics(*p->sample)); measuredRate |= p->sample->rates && p->sample->rates->received > 0; }
    }
    QVERIFY(measuredRate); QVERIFY(f.intact());
    const auto shown = QStringLiteral("%1 bytes").arg(last->sample->counters.received);
    QTRY_COMPARE(field(*page, "statistics-engine-received")->text(), shown);
    const auto capture = qEnvironmentVariable("BIGLYBTC_ENGINE_CAPTURE_DIRECTORY");
    if (!capture.isEmpty()) {
      const QDir dir(capture); QVERIFY(dir.exists());
      const auto file = dir.filePath(multifile ? QStringLiteral("engine-multifile.png") : QStringLiteral("engine-single.png"));
      QVERIFY(!QFile::exists(file)); QVERIFY(w.grab().save(file));
    }
    action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage()); page = statistics(w);
    QTRY_COMPARE(field(*page, "statistics-engine-received")->text(), shown);
    action(w, "classic-my-torrents")->trigger(); QCOMPARE(w.incompleteTable().columnWidth(1), 247);
    QVERIFY(f.intact());
    // A fresh operation rechecking already-complete files has its own zero
    // payload counter; it must not inherit the previous engine's download.
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(), 30000);
    const auto before = updates.count(); const auto nextTarget = w.model().targetAt(0); QVERIFY(nextTarget);
    QVERIFY(b.continueDownload(*nextTarget));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && updates.count() > before
        && packetAt(updates, updates.count() - 1)->state == ui::EngineStatisticsState::Ended, 30000);
    const auto next = packetAt(updates, updates.count() - 1); QVERIFY(next->successful && next->sample);
    QCOMPARE(next->operation, last->operation + 1); QCOMPARE(next->sample->counters.payloadReceived, std::int64_t{0});
    page = statistics(w); QTRY_COMPARE(field(*page, "statistics-engine-payload-received")->text(), QStringLiteral("0 bytes"));
    QVERIFY(f.intact());
  }
  void realCancellationRetainsLastObservationWithoutInventingCompletion() {
    test::OwnedEngineFixture f(false, true); QTRY_VERIFY_WITH_TIMEOUT(f.ready(), 30000);
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), updates(&b, &app::SessionBridge::engineStatisticsReady);
    QVERIFY(b.open(f.native(), f.port())); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    bool cancelled = false;
    QObject::connect(&b, &app::SessionBridge::engineStatisticsReady, &b, [&](ui::EngineStatisticsPresentation p) {
      if (!cancelled && p->state == ui::EngineStatisticsState::Observed && p->sample && p->sample->counters.payloadReceived > 0) {
        cancelled = true; b.cancel();
      }
    });
    w.continueAction().trigger(); auto* page = statistics(w);
    QTRY_VERIFY_WITH_TIMEOUT(cancelled && !b.busy() && updates.count() >= 3
        && packetAt(updates, updates.count() - 1)->state == ui::EngineStatisticsState::Ended, 30000);
    const auto last = packetAt(updates, updates.count() - 1); QVERIFY(!last->successful); QVERIFY(last->sample);
    QTRY_COMPARE(field(*page, "statistics-engine-received")->text(), QStringLiteral("%1 bytes").arg(last->sample->counters.received));
    QVERIFY(f.intact(false));
  }
  void deletingBridgeWithUnfinishedTransferReleasesItsOwnedEngine() {
    test::OwnedEngineFixture f(false, true); QTRY_VERIFY_WITH_TIMEOUT(f.ready(), 30000);
    QObject bridgeOwner;
    auto* b = new app::SessionBridge(&bridgeOwner); QPointer<app::SessionBridge> guard(b);
    QSignalSpy snapshots(b, &app::SessionBridge::snapshotReady);
    QVERIFY(b->open(f.native(), f.port())); QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 1 && !b->busy(), 30000);
    const auto rows = qvariant_cast<ui::Presentation>(snapshots.at(0).at(0)); QVERIFY(rows && rows->items.size() == 1);
    QObject::connect(b, &app::SessionBridge::engineStatisticsReady, b, [b](ui::EngineStatisticsPresentation p) {
      if (p->state == ui::EngineStatisticsState::Observed && p->sample && p->sample->counters.payloadReceived > 0) { delete b; }
    });
    QVERIFY(b->continueDownload({rows->items[0].hash, rows->revision})); QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 30000);
    QVERIFY(f.intact(false)); auto lease = storage::ProfileLease::acquire(f.native()); QVERIFY(lease);
  }
  void absentAndThrowingObserversNeverChangeTransferSuccess_data() {
    QTest::addColumn<bool>("multifile"); QTest::addColumn<bool>("throwing");
    QTest::newRow("single-opted-out") << false << false; QTest::newRow("multi-opted-out") << true << false;
    QTest::newRow("single-throwing") << false << true; QTest::newRow("multi-throwing") << true << true;
  }
  void absentAndThrowingObserversNeverChangeTransferSuccess() {
    QFETCH(bool, multifile); QFETCH(bool, throwing); test::OwnedEngineFixture f(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(f.ready(), 30000); auto lease = storage::ProfileLease::acquire(f.native()); QVERIFY(lease);
    engine::LocalOptions options{.profile = f.native(), .hash = *f.hash, .peerPort = f.port(), .timeout = std::chrono::seconds{30}};
    int calls = 0;
    if (throwing) { options.statistics = [&](const core::EngineStatistics&) { ++calls; throw std::runtime_error("Owned optional observer failure"); }; }
    const auto result = engine::continueLocal(*lease, options); QVERIFY2(result.has_value(), result ? "" : result.error().message.c_str());
    QCOMPARE(calls, throwing ? 1 : 0); QCOMPARE(result->bytes, std::int64_t{test::OwnedEngineFixture::PayloadBytes}); QVERIFY(f.intact());
    auto state = core::Document::parse(test::read(f.stateFile).toStdString()); QVERIFY(state);
    QVERIFY(state->find("opaque.fixture")); QCOMPARE(std::string(state->find("opaque.fixture")->bytes), std::string("\0\xffQ", 3));
  }
};

QTEST_MAIN(EngineStatisticsIntegrationTests)
#include "engine_statistics_integration_tests.moc"
