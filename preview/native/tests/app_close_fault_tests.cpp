#include "app_close_faults.hpp"
#include "persistent_api_faults.hpp"
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "owned_engine_fixture.hpp"
#include "profile.hpp"
#include <QApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

namespace {
using namespace biglybtc;
void wire(ui::LibraryWindow& w,app::SessionBridge& b) {
  QObject::connect(&b,&app::SessionBridge::snapshotReady,&w,&ui::LibraryWindow::setPresentation);
  QObject::connect(&b,&app::SessionBridge::columnLayoutSessionReady,&w,&ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&b,&app::SessionBridge::busyChanged,&w,&ui::LibraryWindow::setBusy);
  QObject::connect(&w,&ui::LibraryWindow::cancelRequested,&b,&app::SessionBridge::cancel);
  QObject::connect(&w,&ui::LibraryWindow::sessionCloseRequested,&b,[&] { QVERIFY(b.closeSession()); });
  QObject::connect(&b,&app::SessionBridge::sessionClosed,&w,[&](quint64,bool,bool) { w.completeSessionClose(); });
  QObject::connect(&b,&app::SessionBridge::sessionCloseFailed,&w,&ui::LibraryWindow::retrySessionClose);
  w.enableSessionCloseHandshake();
}
}
class AppCloseFaultTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QApplication::setQuitOnLastWindowClosed(false); }
  void queuedCloseIgnoresRepeatedCancellation_data() {
    QTest::addColumn<bool>("post"); QTest::newRow("idle-pop") << false; QTest::newRow("idle-post") << true;
  }
  void queuedCloseIgnoresRepeatedCancellation() {
    QFETCH(bool,post); test::OwnedEngineFixture f(false); QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    ui::LibraryWindow w; app::SessionBridge b; test::accounting_fault::CallGate gate; wire(w,b);
    w.resize(1000,700); w.show();
    QSignalSpy snapshots(&b,&app::SessionBridge::snapshotReady), native(&b,&app::SessionBridge::nativeSessionReady);
    QSignalSpy closed(&b,&app::SessionBridge::sessionClosed);
    QVERIFY(b.open(f.native(),f.port())); QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && snapshots.count() > 0,30000);
    const auto target = w.model().targetAt(0); QVERIFY(target); QVERIFY(b.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && native.count() > 0 && gate.bound(),30000);
    QVERIFY(gate.arm(post ? test::accounting_fault::CallGate::Point::Post : test::accounting_fault::CallGate::Point::Pop));
    QTRY_VERIFY_WITH_TIMEOUT(gate.entered(),5000); // Actual worker is held; no guessed sleep/race.
    QVERIFY(!b.busy()); QVERIFY(!w.close()); QVERIFY(b.busy());
    QVERIFY(!w.close()); b.cancel(); b.cancel();
    QCOMPARE(closed.count(),0); QVERIFY(w.isVisible());
    gate.release(); // Before all subsequent assertions/destructors can join Bridge.
    QTRY_VERIFY_WITH_TIMEOUT(closed.count() == 1 && !w.isVisible(),15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    auto lease = storage::ProfileLease::acquire(f.native()); QVERIFY(lease); QVERIFY(f.accountingIntact());
  }
  void optionalAllocationFailureCannotRetainHostOrSuppressCloseAcknowledgement() {
    test::OwnedEngineFixture f(false); QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    app::SessionBridge b; test::app_close_fault::Scope fault;
    QSignalSpy snapshots(&b,&app::SessionBridge::snapshotReady), native(&b,&app::SessionBridge::nativeSessionReady);
    QSignalSpy closed(&b,&app::SessionBridge::sessionClosed), failed(&b,&app::SessionBridge::sessionCloseFailed);
    QVERIFY(b.open(f.native(),f.port())); QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && snapshots.count() > 0,30000);
    const auto rows = qvariant_cast<ui::Presentation>(snapshots.first().at(0)); QVERIFY(rows && rows->items.size() == 1);
    QVERIFY(b.continueDownload({rows->items.front().hash,rows->revision}));
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && native.count() > 0,30000);
    fault.failFirstAllocationAfterJoinedSession(); QVERIFY(b.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
    QCOMPARE(fault.joinedSessions(),std::size_t{1}); QCOMPARE(fault.allocationFailures(),std::size_t{1});
    QCOMPARE(failed.count(),0); QVERIFY(closed.first().at(1).toBool());
    auto lease = storage::ProfileLease::acquire(f.native()); QVERIFY(lease); QVERIFY(f.accountingIntact());
  }
  void rejectedCloseDispatchMayDeleteBridgeBeforeReturn() {
    test::OwnedEngineFixture f(false); QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto* b = new app::SessionBridge; QPointer<app::SessionBridge> guard(b);
    test::app_close_fault::Scope fault;
    QSignalSpy snapshots(b,&app::SessionBridge::snapshotReady);
    QVERIFY(b->open(f.native(),0)); QTRY_VERIFY_WITH_TIMEOUT(!b->busy() && snapshots.count() > 0,30000);
    QObject::connect(b,&app::SessionBridge::busyChanged,b,[b](bool busy) { if (!busy) { delete b; } });
    fault.rejectNextSubmit(); const bool accepted = b->closeSession();
    QVERIFY(!accepted); QVERIFY(!guard); QCOMPARE(fault.rejectedSubmissions(),std::size_t{1});
    auto lease = storage::ProfileLease::acquire(f.native()); QVERIFY(lease); QVERIFY(f.intact(false));
  }
};
QTEST_MAIN(AppCloseFaultTests)
#include "app_close_fault_tests.moc"
