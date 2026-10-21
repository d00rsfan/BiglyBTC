#include "session_bridge.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include "profile.hpp"

#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

namespace {
using namespace biglybtc;
ui::EngineStatisticsPresentation operation(const QSignalSpy& spy) {
  return qvariant_cast<ui::EngineStatisticsPresentation>(spy.last().at(0));
}
ui::Target target(const QSignalSpy& spy) {
  const auto rows = qvariant_cast<ui::Presentation>(spy.last().at(0));
  test::require(rows && rows->items.size() == 1, "Missing generated inventory");
  return {rows->items.front().hash, rows->revision};
}
} // namespace

// Default-only lane. Bound persistent operational suites remain separate and
// unchanged; they must run in the explicitly source-bound build, not skip here.
class PersistentUnboundBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    const auto result = engine::PersistentSession::create({1, 2, 3});
    QVERIFY(!result); QCOMPARE(result.error(), engine::AccountingError::DependencyUnqualified);
  }
  void actualLegacyTransferAndRepeatPreserveOperationTelemetryAndCloseBarrier_data() {
    QTest::addColumn<bool>("multifile");
    QTest::newRow("single") << false;
    QTest::newRow("multifile") << true;
  }
  void actualLegacyTransferAndRepeatPreserveOperationTelemetryAndCloseBarrier() {
    QFETCH(bool, multifile);
    test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy operations(&bridge, &app::SessionBridge::engineStatisticsReady);
    QSignalSpy owners(&bridge, &app::SessionBridge::nativeSessionReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && rows.count() > 0, 30000);
    QVERIFY(bridge.continueDownload(target(rows)));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !operations.empty()
        && operation(operations)->state == ui::EngineStatisticsState::Ended, 30000);
    const auto first = operation(operations);
    QVERIFY(first && first->successful && first->sample);
    QVERIFY(first->sample->counters.payloadReceived > 0);
    QCOMPARE(owners.count(), 0); QVERIFY(fixture.intact());
    const auto previousRows = rows.count();
    const auto previousOperations = operations.count();
    QVERIFY(bridge.continueDownload(target(rows)));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && rows.count() > previousRows
        && operations.count() > previousOperations
        && operation(operations)->state == ui::EngineStatisticsState::Ended, 30000);
    const auto second = operation(operations);
    QVERIFY(second && second->successful && second->sample);
    QVERIFY(second->operation > first->operation);
    QCOMPARE(second->sample->counters.payloadReceived, std::int64_t{0});
    QVERIFY(first->sample->counters.payloadReceived > 0); QCOMPARE(owners.count(), 0);
    QVERIFY(fixture.intact());
    QVERIFY(!storage::ProfileLease::acquire(fixture.native()));
    bool leaseReleasedAtAcknowledgement = false;
    QObject::connect(&bridge, &app::SessionBridge::sessionClosed, &bridge,
        [&](quint64, bool joined, bool released) {
          auto lease = storage::ProfileLease::acquire(fixture.native());
          leaseReleasedAtAcknowledgement = joined && released && lease.has_value();
        });
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    QVERIFY(leaseReleasedAtAcknowledgement); QCOMPARE(owners.count(), 0);
    QVERIFY(fixture.intact());
  }
};
QTEST_MAIN(PersistentUnboundBridgeTests)
#include "persistent_unbound_bridge_tests.moc"
