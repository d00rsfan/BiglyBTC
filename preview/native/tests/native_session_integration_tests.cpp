#include "session_bridge.hpp"
#include "library_window.hpp"
#include "native_session_presentation.hpp"
#include "owned_engine_fixture.hpp"
#include "profile.hpp"

#include <QAction>
#include <QApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

namespace {
using namespace biglybtc;
ui::NativeSessionPresentation nativeAt(const QSignalSpy& spy, int row = -1) {
  return qvariant_cast<ui::NativeSessionPresentation>(spy.at(row < 0 ? spy.count() - 1 : row).at(0));
}
void wire(ui::LibraryWindow& w, app::SessionBridge& b) {
  QObject::connect(&b, &app::SessionBridge::snapshotReady, &w, &ui::LibraryWindow::setPresentation);
  QObject::connect(&b, &app::SessionBridge::columnLayoutSessionReady, &w, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&b, &app::SessionBridge::columnLayoutsReady, &w, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&b, &app::SessionBridge::busyChanged, &w, &ui::LibraryWindow::setBusy);
  QObject::connect(&b, &app::SessionBridge::nativeSessionReady, &w, &ui::LibraryWindow::setNativeSession);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &b, &app::SessionBridge::columnLayouts);
  QObject::connect(&w, &ui::LibraryWindow::cancelRequested, &b, &app::SessionBridge::cancel);
}
auto packet() {
  auto p = std::make_shared<ui::NativeSessionPacket>();
  p->session = 73; p->applicationRun = 1; p->epoch = 1; p->sequence = 1;
  p->state = ui::NativeSessionState::Idle;
  p->ownerElapsedNanoseconds = 1000000000;
  p->sample = core::EngineStatistics{{1000,200,800,100},500000000,{}};
  return p;
}
} // namespace

class NativeSessionIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void immutableAdmissionRejectsWrongDomainRegressionsAndTerminalReplays() {
    const auto first = packet();
    QVERIFY(ui::acceptNativeSession({}, first, 73));
    QVERIFY(!ui::acceptNativeSession({}, first, 74));
    auto next = std::make_shared<ui::NativeSessionPacket>(*first); next->sequence = 2;
    QVERIFY(ui::acceptNativeSession(first, next, 73));
    QVERIFY(!ui::acceptNativeSession(next, next, 73));
    for (int mutation = 0; mutation < 9; ++mutation) {
      auto bad = std::make_shared<ui::NativeSessionPacket>(*next);
      switch (mutation) {
        case 0: bad->applicationRun = 2; break;
        case 1: bad->epoch = 0; break;
        case 2: bad->sequence = 0; break;
        case 3: bad->sample.reset(); break;
        case 4: --bad->sample->counters.received; break;
        case 5: --bad->ownerElapsedNanoseconds; break;
        case 6: bad->state = static_cast<ui::NativeSessionState>(999); break;
        case 7: bad->sample->elapsedNanoseconds = bad->ownerElapsedNanoseconds + 1; break;
        case 8: ++bad->sample->counters.payloadReceived; break;
      }
      QVERIFY2(!ui::acceptNativeSession(first, bad, 73), qPrintable(QString::number(mutation)));
    }
    auto closed = std::make_shared<ui::NativeSessionPacket>(*next); closed->state = ui::NativeSessionState::Closed;
    QVERIFY(ui::acceptNativeSession(first, closed, 73));
    ++next->sequence; QVERIFY(!ui::acceptNativeSession(closed, next, 73));
    ++next->epoch; QVERIFY(ui::acceptNativeSession(closed, next, 73));
    QCOMPARE(first->sample->counters.received, std::int64_t{1000});
  }
  void oneOwnerSurvivesRepeatedOperationAndIdle_data() {
    QTest::addColumn<bool>("multifile"); QTest::newRow("single") << false; QTest::newRow("multifile") << true;
  }
  void oneOwnerSurvivesRepeatedOperationAndIdle() {
    QFETCH(bool, multifile); test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    ui::LibraryWindow w; app::SessionBridge b; wire(w,b);
    w.resize(1280,900); w.show();
    QSignalSpy snapshots(&b,&app::SessionBridge::snapshotReady);
    QSignalSpy native(&b,&app::SessionBridge::nativeSessionReady);
    QSignalSpy operations(&b,&app::SessionBridge::engineStatisticsReady);
    QVERIFY(b.open(fixture.native(),fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && snapshots.count() >= 1,30000);
    const auto target = w.model().targetAt(0); QVERIFY(target);
    QVERIFY(b.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && native.count() > 1 && nativeAt(native)->sample,30000);
    const auto first = nativeAt(native);
    QVERIFY(first->sample->counters.payloadReceived > 0);
    QCOMPARE(first->state,ui::NativeSessionState::Idle);
    QVERIFY(w.nativeSessionSnapshot());
    const auto firstCounters = first->sample->counters;
    const auto firstRevision = w.model().targetAt(0)->generation;
    QTRY_VERIFY_WITH_TIMEOUT(nativeAt(native)->sequence > first->sequence
        && nativeAt(native)->sample && nativeAt(native)->sample->rates,5000);
    const auto idle = nativeAt(native);
    QCOMPARE(idle->epoch,first->epoch); QCOMPARE(idle->applicationRun,first->applicationRun);
    QCOMPARE(idle->sample->counters,firstCounters);
    QCOMPARE(idle->sample->rates->received,std::int64_t{0});
    QCOMPARE(w.model().targetAt(0)->generation,firstRevision);
    QVERIFY(!b.busy());
    const auto before = operations.count();
    const auto again = w.model().targetAt(0); QVERIFY(again); QVERIFY(b.continueDownload(*again));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && operations.count() > before,30000);
    const auto terminal = qvariant_cast<ui::EngineStatisticsPresentation>(operations.last().at(0));
    QVERIFY(terminal && terminal->sample && terminal->successful);
    QCOMPARE(terminal->sample->counters.payloadReceived,std::int64_t{0});
    QTRY_VERIFY(nativeAt(native)->state == ui::NativeSessionState::Idle);
    QCOMPARE(nativeAt(native)->epoch,first->epoch);
    QCOMPARE(nativeAt(native)->sample->counters.payloadReceived,firstCounters.payloadReceived);
    QCOMPARE(first->sample->counters,firstCounters); // Immutable copied first observation.
    QVERIFY(fixture.intact());
    QSignalSpy closed(&b,&app::SessionBridge::sessionClosed);
    QVERIFY(b.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QTRY_VERIFY(nativeAt(native)->state == ui::NativeSessionState::Closed);
    auto released = storage::ProfileLease::acquire(fixture.native()); QVERIFY(released);
    QVERIFY(!b.refresh());
  }
  void cancellationReturnsToIdleWithoutLosingAbsoluteOwnerCounters() {
    test::OwnedEngineFixture fixture(false,true); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    ui::LibraryWindow w; app::SessionBridge b; wire(w,b);
    QSignalSpy snapshots(&b,&app::SessionBridge::snapshotReady), native(&b,&app::SessionBridge::nativeSessionReady);
    QVERIFY(b.open(fixture.native(),fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && snapshots.count() > 0,30000);
    bool cancelled = false;
    QObject::connect(&b,&app::SessionBridge::nativeSessionReady,&b,[&](ui::NativeSessionPresentation p) {
      if (!cancelled && p->sample && p->sample->counters.payloadReceived > 0) { cancelled = true; b.cancel(); }
    });
    const auto target = w.model().targetAt(0); QVERIFY(target); QVERIFY(b.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(cancelled && !b.busy() && nativeAt(native)->state == ui::NativeSessionState::Idle,30000);
    const auto idle = nativeAt(native); QVERIFY(idle->sample && idle->sample->counters.payloadReceived > 0);
    QTRY_VERIFY_WITH_TIMEOUT(nativeAt(native)->sequence > idle->sequence,5000);
    QCOMPARE(nativeAt(native)->epoch,idle->epoch);
    QCOMPARE(nativeAt(native)->sample->counters,idle->sample->counters);
    QVERIFY(fixture.intact(false));
  }
  void normalCloseAcknowledgesOnlyAfterWorkerAndLeaseRelease_data() {
    QTest::addColumn<bool>("active"); QTest::newRow("offline") << false; QTest::newRow("active-cancel") << true;
  }
  void normalCloseAcknowledgesOnlyAfterWorkerAndLeaseRelease() {
    QFETCH(bool,active); test::OwnedEngineFixture fixture(false,true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    ui::LibraryWindow w; app::SessionBridge b; wire(w,b);
    w.enableSessionCloseHandshake(); w.resize(1000,700); w.show();
    QObject::connect(&w,&ui::LibraryWindow::sessionCloseRequested,&b,[&] { QVERIFY(b.closeSession()); });
    QSignalSpy snapshots(&b,&app::SessionBridge::snapshotReady), native(&b,&app::SessionBridge::nativeSessionReady);
    QSignalSpy closed(&b,&app::SessionBridge::sessionClosed);
    bool releasedBeforeAck = false;
    QObject::connect(&b,&app::SessionBridge::sessionClosed,&w,[&](quint64,bool joined,bool released) {
      auto lease = storage::ProfileLease::acquire(fixture.native());
      releasedBeforeAck = lease.has_value() && joined && released;
      w.completeSessionClose();
    });
    QVERIFY(b.open(fixture.native(),active ? fixture.port() : 0));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && snapshots.count() > 0,30000);
    QVERIFY(!storage::ProfileLease::acquire(fixture.native()));
    if (active) {
      const auto target = w.model().targetAt(0); QVERIFY(target); QVERIFY(b.continueDownload(*target));
      QTRY_VERIFY_WITH_TIMEOUT(native.count() > 0 && nativeAt(native)->sample
          && nativeAt(native)->sample->counters.payloadReceived > 0,30000);
    } else { QCOMPARE(native.count(),0); }
    QVERIFY(!w.close()); QVERIFY(w.isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(closed.count() == 1 && releasedBeforeAck && !w.isVisible(),15000);
    QVERIFY(active ? fixture.accountingIntact(false) : fixture.intact(false));
  }
  void destructionDuringActiveObservationReleasesWorkerOwnedLease() {
    test::OwnedEngineFixture fixture(false,true); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    auto* b = new app::SessionBridge; QPointer<app::SessionBridge> guard(b);
    QSignalSpy snapshots(b,&app::SessionBridge::snapshotReady);
    QVERIFY(b->open(fixture.native(),fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!b->busy() && snapshots.count() > 0,30000);
    const auto rows = qvariant_cast<ui::Presentation>(snapshots.first().at(0));
    QVERIFY(rows && rows->items.size() == 1);
    QObject::connect(b,&app::SessionBridge::nativeSessionReady,b,[b](ui::NativeSessionPresentation p) {
      if (p->sample && p->sample->counters.payloadReceived > 0) { delete b; }
    });
    const ui::Target target{rows->items.front().hash,rows->revision};
    QVERIFY(b->continueDownload(target)); QTRY_VERIFY_WITH_TIMEOUT(!guard,30000);
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    QVERIFY(fixture.intact(false));
  }
};
QTEST_MAIN(NativeSessionIntegrationTests)
#include "native_session_integration_tests.moc"
