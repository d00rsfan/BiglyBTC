#include "session_bridge.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "native_activity_history.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include "../adapters/history_view.hpp"
#include "biglybtc/saved_statistics.hpp"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLabel>
#include <QSignalSpy>
#include <QTabWidget>
#include <QtTest>

#include <chrono>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& owner, const char* name) {
  auto* value = owner.findChild<T*>(QString::fromLatin1(name));
  if (!value) { throw std::runtime_error("Missing startup Statistics control"); }
  return *value;
}
void wire(ui::LibraryWindow& window, app::SessionBridge& bridge) {
  QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &window, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&bridge, &app::SessionBridge::columnLayoutsReady, &window, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&bridge, &app::SessionBridge::statisticsReady, &window, &ui::LibraryWindow::setStatistics);
  QObject::connect(&bridge, &app::SessionBridge::nativeSessionReady, &window, &ui::LibraryWindow::setNativeSession);
  QObject::connect(&bridge, &app::SessionBridge::overallReady, &window, &ui::LibraryWindow::setOverall);
  QObject::connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
  QObject::connect(&window, &ui::LibraryWindow::columnLayoutsRequested, &bridge, &app::SessionBridge::columnLayouts);
  QObject::connect(&window, &ui::LibraryWindow::statisticsRequested, &bridge, &app::SessionBridge::statistics);
}
ui::OverallPresentation latest(const QSignalSpy& spy) {
  return spy.empty() ? ui::OverallPresentation{} : qvariant_cast<ui::OverallPresentation>(spy.last().at(0));
}
bool actualIdleZero(const ui::NativeSessionPresentation& packet) {
  if (!packet || packet->state != ui::NativeSessionState::Idle || !packet->sample || !packet->sample->rates) { return false; }
  const auto& counters = packet->sample->counters;
  const auto& rates = *packet->sample->rates;
  return counters == core::EngineCounters{} && rates == core::EngineRates{};
}
ui::StatisticsPage& open(ui::LibraryWindow& window) {
  child<QAction>(window, "classic-statistics").trigger();
  if (!window.statisticsPage()) { throw std::runtime_error("Statistics page did not open"); }
  auto& page = *window.statisticsPage(); page.tabs().setCurrentIndex(1);
  child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(0);
  return page;
}
core::SavedStatistics saved(const QString& filename) {
  const auto bytes = test::read(filename);
  auto value = core::readSavedStatistics(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  if (!value) { throw std::runtime_error("Actual startup accounting did not parse"); }
  return std::move(*value);
}
} // namespace

// Generated held profiles and real qualified loopback owners. No injected
// counter/clock/GUI packet, sleeps, new timer, public networking or Java run.
class StartupStatisticsTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    auto owner = engine::PersistentSession::create({1, 2, 3});
    QVERIFY2(owner.has_value(), "Startup owner tests require the qualified source/runtime cohort; no skip");
    QVERIFY(!(*owner)->latest()); // create() remains lazy for independent API callers.
    const auto closed = (*owner)->close(std::chrono::steady_clock::now() + std::chrono::seconds{5});
    QVERIFY(closed.sessionJoined); QVERIFY(!closed.retirement.peerCounters);
  }
  void actualIdlePrecedesStatisticsAndSharesFirstOperation_data() {
    QTest::addColumn<bool>("multifile");
    QTest::newRow("single") << false; QTest::newRow("multifile") << true;
  }
  void actualIdlePrecedesStatisticsAndSharesFirstOperation() {
    QFETCH(bool, multifile); test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.resize(1280, 1000); window.show();
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), overall(&bridge, &app::SessionBridge::overallReady);
    QSignalSpy operations(&bridge, &app::SessionBridge::engineStatisticsReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 30000);
    const auto target = window.model().targetAt(0); QVERIFY(target);
    QTRY_VERIFY_WITH_TIMEOUT(actualIdleZero(window.nativeSessionSnapshot()) && latest(overall)
        && latest(overall)->available && latest(overall)->writable, 10000);
    const auto idle = window.nativeSessionSnapshot();
    QVERIFY(idle->ownerElapsedNanoseconds >= 1000000000);
    QVERIFY(!window.statisticsPage()); // The application open, not UI navigation, started it.
    QCOMPARE(operations.count(), 0); QCOMPARE(rows.count(), 1);
    QCOMPARE(window.model().targetAt(0)->generation, target->generation);
    QVERIFY(window.nativeActivitySnapshot()); QVERIFY(!window.nativeActivitySnapshot()->points().empty());
    QCOMPARE(latest(overall)->values[static_cast<std::size_t>(core::SavedStatistic::Downloaded)], std::int64_t{999});
    QCOMPARE(latest(overall)->values[static_cast<std::size_t>(core::SavedStatistic::Uploaded)], std::int64_t{0});
    QVERIFY(fixture.intact(false)); // Idle history may exist; no Overall save/transfer mutation yet.
    for (const auto& file : fixture.files) {
      QCOMPARE(test::read(fixture.payload + '/' + file.relative), QByteArray(file.expected.size(), '\0'));
    }
    auto& page = open(window); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && page.readState().has_value(), 10000);
    QTRY_COMPARE(child<QLabel>(page, "native-session-counter-received").text(), QStringLiteral("0"));
    QCOMPARE(child<QLabel>(page, "native-session-counter-sent").text(), QStringLiteral("0"));
    QCOMPARE(child<QLabel>(page, "native-session-rate-received").text(), QStringLiteral("0"));
    QVERIFY(child<QLabel>(page, "native-session-uptime").text().startsWith(QStringLiteral("Observed owner uptime:")));
    QCOMPARE(window.nativeSessionSnapshot()->epoch, idle->epoch);
    QCOMPARE(operations.count(), 0);
    QVERIFY(bridge.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && window.nativeSessionSnapshot()
        && window.nativeSessionSnapshot()->sample && window.nativeSessionSnapshot()->sample->counters.payloadReceived > 0
        && !operations.empty(), 30000);
    QCOMPARE(window.nativeSessionSnapshot()->applicationRun, idle->applicationRun);
    QCOMPARE(window.nativeSessionSnapshot()->epoch, idle->epoch);
    QVERIFY(window.nativeSessionSnapshot()->ownerElapsedNanoseconds > idle->ownerElapsedNanoseconds);
    QCOMPARE(idle->sample->counters, core::EngineCounters{}); // Earlier actual snapshot remains immutable.
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    auto released = storage::ProfileLease::acquire(fixture.native()); QVERIFY(released);
    QVERIFY(fixture.accountingIntact());
  }
  void closingRealIdleOwnerWithoutTorrentPersistsObservedUptimeAndZeroTraffic() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), overall(&bridge, &app::SessionBridge::overallReady);
    QSignalSpy operations(&bridge, &app::SessionBridge::engineStatisticsReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    const auto stateBefore = test::read(fixture.stateFile);
    const auto inventoryBefore = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    const auto firstDate = QDateTime::currentDateTimeUtc().date();
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 30000);
    QTRY_VERIFY_WITH_TIMEOUT(actualIdleZero(window.nativeSessionSnapshot()) && latest(overall)
        && latest(overall)->writable && window.nativeSessionSnapshot()->ownerElapsedNanoseconds >= 2000000000, 10000);
    const auto idle = window.nativeSessionSnapshot();
    QCOMPARE(operations.count(), 0); QVERIFY(!window.statisticsPage());
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QTRY_VERIFY(window.nativeSessionSnapshot() && window.nativeSessionSnapshot()->state == ui::NativeSessionState::Closed);
    const auto final = window.nativeSessionSnapshot(); QVERIFY(final->sample);
    QCOMPARE(final->sample->counters, idle->sample->counters);
    QVERIFY(final->sample->elapsedNanoseconds >= idle->sample->elapsedNanoseconds);
    QTRY_VERIFY(latest(overall) && latest(overall)->closed);
    // This flag comes from the existing joined, source-qualified fresh-stat
    // close receipt. No zero packet or GUI state is promoted into finality.
    QVERIFY(latest(overall)->finalCounters); QVERIFY(latest(overall)->saved);
    auto held = storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
    const auto statistics = saved(fixture.profile + QStringLiteral("/BiglyBT.statistics"));
    QCOMPARE(statistics.get(core::SavedStatistic::Downloaded).value, std::optional<std::int64_t>{999});
    QCOMPARE(statistics.get(core::SavedStatistic::Uploaded).value, std::optional<std::int64_t>{0});
    QVERIFY(statistics.get(core::SavedStatistic::Uptime).value);
    QVERIFY(*statistics.get(core::SavedStatistic::Uptime).value >= 1);
    QCOMPARE(*statistics.get(core::SavedStatistic::Uptime).value,
        latest(overall)->values[static_cast<std::size_t>(core::SavedStatistic::Uptime)]);
    const auto lastDate = QDateTime::currentDateTimeUtc().date();
    bool historyFound = false;
    for (const auto& date : {firstDate, lastDate}) {
      const auto history = storage::readSavedHistoryDayView(*held,
          {date.year(), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day())});
      QVERIFY(history);
      if (history->snapshot) {
        historyFound = true; // Actual idle Start and End, including a possible UTC rollover.
        if (history->snapshot->totals) { QCOMPARE(*history->snapshot->totals, core::HistoryCounters{}); }
      }
    }
    QVERIFY(historyFound);
    QVERIFY(fixture.accountingIntact(false));
    QCOMPARE(test::read(fixture.stateFile), stateBefore);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), inventoryBefore);
    for (const auto& file : fixture.files) {
      QCOMPARE(test::read(fixture.payload + '/' + file.relative), QByteArray(file.expected.size(), '\0'));
    }
  }
  void offlineOpenAndStatisticsStayWithoutAnOwner() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge); window.show();
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), native(&bridge, &app::SessionBridge::nativeSessionReady);
    QSignalSpy overall(&bridge, &app::SessionBridge::overallReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 30000);
    auto& page = open(window); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && page.readState().has_value(), 10000);
    QVERIFY(bridge.refresh()); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && rows.count() >= 2, 10000);
    QCOMPARE(native.count(), 0); QCOMPARE(overall.count(), 0); QVERIFY(!window.nativeSessionSnapshot());
    QVERIFY(!QFileInfo::exists(fixture.profile + QStringLiteral("/stats")));
    QVERIFY(fixture.intact(false)); QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    QCOMPARE(native.count(), 0); QCOMPARE(overall.count(), 0); QVERIFY(fixture.intact(false));
  }
};
QTEST_MAIN(StartupStatisticsTests)
#include "startup_statistics_tests.moc"
