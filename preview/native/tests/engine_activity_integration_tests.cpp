#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"
#include "engine_activity_history.hpp"
#include "engine_activity_view.hpp"
#include "owned_engine_fixture.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

namespace {
using namespace biglybtc;
QAction* action(ui::LibraryWindow& w, const char* id) {
  auto* a = w.findChild<QAction*>(QString::fromLatin1(id)); test::require(a, "Missing classic action"); return a;
}
void show(ui::LibraryWindow& w) {
  w.resize(1280, 900); w.show(); w.activateWindow(); QCoreApplication::processEvents();
}
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
ui::EngineStatisticsPresentation packetAt(const QSignalSpy& spy, int index) {
  return qvariant_cast<ui::EngineStatisticsPresentation>(spy.at(index).at(0));
}
ui::StatisticsPage* graph(ui::LibraryWindow& w) {
  action(w, "classic-statistics")->trigger(); auto* page = w.statisticsPage();
  test::require(page, "Statistics did not open"); page->tabs().setCurrentIndex(0);
  auto* views = page->findChild<QTabWidget*>(QStringLiteral("statistics-activity-views"));
  test::require(views, "Missing Activity views"); views->setCurrentIndex(1);
  QCoreApplication::processEvents(); return page;
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
auto observed(ui::LibraryWindow& w, std::int64_t index, quint64 operation = 1) {
  auto p = preparing(w, operation); p->sequence = static_cast<quint64>(index + 1); p->state = ui::EngineStatisticsState::Observed;
  p->sample = core::EngineStatistics{{1000 * index, 200 * index, 800 * index, 100 * index},
      500000000 * index, core::EngineRates{2000, 400, 1600, 200, 400, 200}}; return p;
}
bool samePoints(const ui::EngineActivityPresentation& a, const ui::EngineActivityPresentation& b) {
  if (!a || !b || a->points().size() != b->points().size()) { return false; }
  for (std::size_t i = 0; i < a->points().size(); ++i) {
    const auto& x = a->points()[i]; const auto& y = b->points()[i];
    if (x.elapsedNanoseconds != y.elapsedNanoseconds || x.rates != y.rates || x.connectedToPrevious != y.connectedToPrevious) { return false; }
  }
  return true;
}
} // namespace

class EngineActivityIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void windowCollectsWithoutPageAndRestoresImmutableHistory() {
    ui::LibraryWindow w; show(w); fakeRows(w); QVERIFY(fakeSession(w));
    w.filter().setText(QStringLiteral("Alpha")); w.incompleteTable().setColumnWidth(1, 247);
    w.setEngineStatistics(preparing(w));
    for (std::int64_t i = 1; i <= 3; ++i) { w.setEngineStatistics(observed(w, i)); }
    QVERIFY(!w.statisticsPage()); auto* page = graph(w); auto first = page->engineActivitySnapshot();
    QVERIFY(first); QCOMPARE(first->points().size(), std::size_t{3});
    QVERIFY(!first->points()[0].connectedToPrevious); QVERIFY(first->points()[1].connectedToPrevious);
    action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage());
    w.setEngineStatistics(observed(w, 4)); QCOMPARE(first->points().size(), std::size_t{3});
    page = graph(w); auto reopened = page->engineActivitySnapshot(); QVERIFY(reopened);
    QCOMPARE(reopened->points().size(), std::size_t{4});
    page->refreshRequested(); QCoreApplication::processEvents();
    QVERIFY(page->engineActivitySnapshot() == reopened);
    action(w, "classic-my-torrents")->trigger(); QCOMPARE(w.filter().text(), QStringLiteral("Alpha"));
    QCOMPARE(w.incompleteTable().columnWidth(1), 247);
    page = graph(w); QVERIFY(samePoints(page->engineActivitySnapshot(), reopened));
    QVERIFY(fakeSession(w, 74)); QVERIFY(!page->engineActivitySnapshot());
    w.setEngineStatistics(observed(w, 5)); QVERIFY(!page->engineActivitySnapshot());
  }
  void windowRejectsUnboundStaleRepliesAndResetsAtEveryOperation() {
    ui::LibraryWindow w; show(w); fakeRows(w); QVERIFY(fakeSession(w));
    auto bad = preparing(w); ++bad->target.generation; w.setEngineStatistics(bad);
    auto* page = graph(w); QVERIFY(!page->engineActivitySnapshot());
    w.setEngineStatistics(preparing(w));
    for (std::int64_t i = 1; i <= 610; ++i) { w.setEngineStatistics(observed(w, i)); }
    auto full = page->engineActivitySnapshot(); QVERIFY(full); QCOMPARE(full->points().size(), std::size_t{600});
    QCOMPARE(full->points()[0].elapsedNanoseconds, std::int64_t{5500000000});
    QVERIFY(!full->points()[0].connectedToPrevious);
    w.setEngineStatistics(observed(w, 609)); QVERIFY(page->engineActivitySnapshot() == full);
    auto ended = observed(w, 610); ++ended->sequence; ended->state = ui::EngineStatisticsState::Ended;
    ended->sample.reset(); w.setEngineStatistics(ended);
    auto terminal = page->engineActivitySnapshot(); QVERIFY(terminal); QVERIFY(samePoints(full, terminal));
    w.setEngineStatistics(observed(w, 620)); QVERIFY(page->engineActivitySnapshot() == terminal);
    bad = preparing(w, 2); ++bad->target.generation; w.setEngineStatistics(bad);
    QVERIFY(page->engineActivitySnapshot() == terminal);
    w.setEngineStatistics(preparing(w, 2)); QVERIFY(page->engineActivitySnapshot());
    QVERIFY(page->engineActivitySnapshot()->points().empty());
    w.setEngineStatistics(observed(w, 630)); QVERIFY(page->engineActivitySnapshot()->points().empty());
    w.setEngineStatistics(observed(w, 1, 2)); auto next = page->engineActivitySnapshot(); QVERIFY(next);
    QCOMPARE(next->points().size(), std::size_t{1}); QVERIFY(!next->points()[0].connectedToPrevious);
    QVERIFY(samePoints(full, terminal));
  }
  void realTrafficCollectedWithClosedTabAndReopenedAfterCompletion_data() {
    QTest::addColumn<bool>("multifile"); QTest::newRow("single-file") << false; QTest::newRow("multifile") << true;
  }
  void realTrafficCollectedWithClosedTabAndReopenedAfterCompletion() {
    QFETCH(bool, multifile); test::OwnedEngineFixture f(multifile); QTRY_VERIFY_WITH_TIMEOUT(f.ready(), 30000);
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), updates(&b, &app::SessionBridge::engineStatisticsReady);
    QVERIFY(b.open(f.native(), f.port())); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    QCOMPARE(w.model().rowCount(), 1); w.incompleteTable().setColumnWidth(1, 247);
    w.continueAction().trigger(); QVERIFY(b.busy()); QVERIFY(!w.statisticsPage());
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && updates.count() >= 3
        && packetAt(updates, updates.count()-1)->state == ui::EngineStatisticsState::Ended, 30000);
    const auto last = packetAt(updates, updates.count()-1); QVERIFY(last->successful && last->sample);
    auto* page = graph(w); auto frame = page->engineActivitySnapshot(); QVERIFY(frame);
    QVERIFY(frame->latest() == last); QVERIFY(!frame->points().empty());
    bool payloadRate = false, protocolRate = false;
    std::int64_t elapsed = -1;
    for (const auto& point : frame->points()) {
      QVERIFY(point.elapsedNanoseconds > elapsed); elapsed = point.elapsedNanoseconds;
      payloadRate |= point.rates.payloadReceived > 0; protocolRate |= point.rates.protocolReceived > 0;
      QVERIFY(point.rates.received >= point.rates.payloadReceived);
    }
    QVERIFY(payloadRate && protocolRate); QVERIFY(f.intact());
    auto* initialSaved = page->findChild<QLabel*>(QStringLiteral("statistics-saved-downloaded")); QVERIFY(initialSaved);
    QTRY_COMPARE(initialSaved->text(), QStringLiteral("999 bytes")); QTRY_VERIFY_WITH_TIMEOUT(!b.busy(), 30000);
    const auto directory = qEnvironmentVariable("BIGLYBTC_ENGINE_ACTIVITY_CAPTURE_DIRECTORY");
    if (!directory.isEmpty()) {
      const QDir dir(directory); QVERIFY(dir.exists());
      const auto file = dir.filePath(multifile ? QStringLiteral("activity-multifile.png") : QStringLiteral("activity-single.png"));
      QVERIFY(!QFile::exists(file)); QVERIFY(w.grab().save(file));
    }
    action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage());
    page = graph(w); QVERIFY(samePoints(frame, page->engineActivitySnapshot()));
    page->refreshRequested(); QTRY_VERIFY_WITH_TIMEOUT(!b.busy(), 30000);
    QVERIFY(samePoints(frame, page->engineActivitySnapshot()));
    auto* saved = page->findChild<QLabel*>(QStringLiteral("statistics-saved-downloaded")); QVERIFY(saved);
    QTRY_COMPARE(saved->text(), QStringLiteral("999 bytes"));
    action(w, "classic-my-torrents")->trigger(); QCOMPARE(w.incompleteTable().columnWidth(1), 247);
    action(w, "classic-statistics")->trigger(); action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage());
    const auto before = updates.count(); auto nextTarget = w.model().targetAt(0); QVERIFY(nextTarget);
    QVERIFY(b.continueDownload(*nextTarget));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && updates.count() > before
        && packetAt(updates, updates.count()-1)->state == ui::EngineStatisticsState::Ended, 30000);
    page = graph(w); auto next = page->engineActivitySnapshot(); QVERIFY(next);
    QCOMPARE(next->latest()->operation, last->operation + 1); QVERIFY(next->latest()->successful && next->latest()->sample);
    QCOMPARE(next->latest()->sample->counters.payloadReceived, std::int64_t{0});
    for (const auto& point : next->points()) { QCOMPARE(point.rates.payloadReceived, std::int64_t{0}); }
    QVERIFY(f.intact());
  }
  void cancellationRetainsActualHistoryFromAnUnfinishedTransfer() {
    test::OwnedEngineFixture f(false, true); QTRY_VERIFY_WITH_TIMEOUT(f.ready(), 30000);
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), updates(&b, &app::SessionBridge::engineStatisticsReady);
    QVERIFY(b.open(f.native(), f.port())); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    bool cancelled = false;
    QObject::connect(&b, &app::SessionBridge::engineStatisticsReady, &b, [&](ui::EngineStatisticsPresentation packet) {
      if (!cancelled && packet->state == ui::EngineStatisticsState::Observed && b.busy()
          && packet->sample && packet->sample->rates && packet->sample->rates->payloadReceived > 0) {
        cancelled = true; b.cancel();
      }
    });
    w.continueAction().trigger();
    QTRY_VERIFY_WITH_TIMEOUT(cancelled && !b.busy() && updates.count() >= 3
        && packetAt(updates, updates.count()-1)->state == ui::EngineStatisticsState::Ended, 30000);
    auto* page = graph(w); auto frame = page->engineActivitySnapshot(); QVERIFY(frame && !frame->points().empty());
    QVERIFY(frame->latest()->state == ui::EngineStatisticsState::Ended && !frame->latest()->successful);
    QVERIFY(frame->latest()->sample); QVERIFY(f.intact(false));
    action(w, "classic-close-tab")->trigger(); page = graph(w);
    QVERIFY(samePoints(frame, page->engineActivitySnapshot()));
  }
};

QTEST_MAIN(EngineActivityIntegrationTests)
#include "engine_activity_integration_tests.moc"
