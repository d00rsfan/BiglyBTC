#include "session_bridge.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "native_activity_history.hpp"
#include "native_activity_view.hpp"
#include "persistent_session.hpp"
#include "profile.hpp"
#include "owned_engine_fixture.hpp"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QTabWidget>
#include <QtTest>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <stdexcept>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& owner,const char* id) {
  auto* value=owner.findChild<T*>(QString::fromLatin1(id));if(!value){throw std::runtime_error("Missing actual session integration control");}return *value;
}
void wire(ui::LibraryWindow& window,app::SessionBridge& bridge) {
  QObject::connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&bridge,&app::SessionBridge::columnLayoutsReady,&window,&ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&bridge,&app::SessionBridge::statisticsReady,&window,&ui::LibraryWindow::setStatistics);
  QObject::connect(&bridge,&app::SessionBridge::engineStatisticsReady,&window,&ui::LibraryWindow::setEngineStatistics);
  QObject::connect(&bridge,&app::SessionBridge::nativeSessionReady,&window,&ui::LibraryWindow::setNativeSession);
  QObject::connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
  QObject::connect(&window,&ui::LibraryWindow::columnLayoutsRequested,&bridge,&app::SessionBridge::columnLayouts);
  QObject::connect(&window,&ui::LibraryWindow::statisticsRequested,&bridge,&app::SessionBridge::statistics);
}
bool capture(ui::LibraryWindow& window, const QString& directory, const char* name) {
  const QFileInfo root(directory);
  if (!root.isAbsolute() || !root.isDir() || root.isSymLink()
      || root.canonicalFilePath() != directory || root.ownerId() != static_cast<uint>(::geteuid())) { return false; }
  const auto path = QDir(directory).filePath(QString::fromLatin1(name));
  const QFileInfo target(path);
  if (target.exists() || target.isSymLink()) { return false; }
  return window.grab().save(path, "PNG");
}
ui::StatisticsPage& open(ui::LibraryWindow& window) {
  child<QAction>(window,"classic-statistics").trigger();
  if(!window.statisticsPage()){throw std::runtime_error("Actual Statistics page did not open");}return *window.statisticsPage();
}
} // namespace

// REQUIRED in the explicit bound-runtime cohort; never skip when unbound.
// Generated owned loopback fixture only. Ordinary default fallback has its own
// unchanged transfer/telemetry/barrier tests and cannot claim these observations.
class NativeSessionStatisticsBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
    auto result=engine::PersistentSession::create({1,2,3}); QVERIFY2(result.has_value(),"Bound UI cohort requires actual reviewed runtime binding; no skip");
    const auto closed=(*result)->close(std::chrono::steady_clock::now()+std::chrono::seconds{5});
    QVERIFY(closed.sessionJoined); QVERIFY(!closed.retirement.peerCounters);
  }
  void actualOwnerIdleRepeatAndReopenStayIndependentOfOperationCounters_data() {
    QTest::addColumn<bool>("multifile");QTest::newRow("single")<<false;QTest::newRow("multifile")<<true;
  }
  void actualOwnerIdleRepeatAndReopenStayIndependentOfOperationCounters() {
    QFETCH(bool,multifile);test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    ui::LibraryWindow window;app::SessionBridge bridge;wire(window,bridge);window.resize(1280,900);window.show();
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),operations(&bridge,&app::SessionBridge::engineStatisticsReady);
    QSignalSpy closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&rows.count()>0,30000);
    auto& page=open(window);QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&page.readState().has_value(),30000);
    QCOMPARE(child<QTabWidget>(page,"statistics-activity-views").currentIndex(),2);QVERIFY(!window.nativeSessionSnapshot());
    const auto firstTarget=window.model().targetAt(0);QVERIFY(firstTarget);QVERIFY(bridge.continueDownload(*firstTarget));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&window.nativeSessionSnapshot()&&window.nativeSessionSnapshot()->sample
        &&window.nativeSessionSnapshot()->sample->counters.payloadReceived>0,30000);
    QTRY_VERIFY_WITH_TIMEOUT(window.nativeSessionSnapshot()->sample->rates
        &&window.nativeSessionSnapshot()->sample->rates->received==0,10000);
    const auto first=window.nativeSessionSnapshot();const auto retained=window.nativeActivitySnapshot();QVERIFY(retained);
    QVERIFY(!retained->points().empty());QVERIFY(page.nativeSessionSnapshot()==first);
    page.tabs().setCurrentIndex(1);
    QTRY_COMPARE(child<QLabel>(page,"native-session-counter-received").text(),QString::number(first->sample->counters.received));
    QCOMPARE(child<QLabel>(page,"native-session-counter-payload-received").text(),QString::number(first->sample->counters.payloadReceived));
    QCOMPARE(child<QLabel>(page,"native-session-rate-received").text(),QStringLiteral("0"));
    if (const auto directory = qEnvironmentVariable("BIGLYBTC_NATIVE_SESSION_CAPTURE_DIRECTORY"); !directory.isEmpty()) {
      page.tabs().setCurrentIndex(0);
      child<QTabWidget>(page,"statistics-activity-views").setCurrentIndex(2);
      QCoreApplication::processEvents();
      QVERIFY2(capture(window,directory,multifile ? "actual-native-multi-activity.png" : "actual-native-single-activity.png"),"Cannot save owned actual session Activity capture");
      page.tabs().setCurrentIndex(1);
      child<QTabWidget>(page,"statistics-transfer-tabs").setCurrentIndex(0);
      QCoreApplication::processEvents();
      QVERIFY2(capture(window,directory,multifile ? "actual-native-multi-current.png" : "actual-native-single-current.png"),"Cannot save owned actual session Current capture");
    }
    const int before=operations.count();const auto repeat=window.model().targetAt(0);QVERIFY(repeat);QVERIFY(bridge.continueDownload(*repeat));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&operations.count()>before,30000);
    const auto operation=qvariant_cast<ui::EngineStatisticsPresentation>(operations.last().at(0));
    QVERIFY(operation&&operation->state==ui::EngineStatisticsState::Ended&&operation->successful&&operation->sample);
    QCOMPARE(operation->sample->counters.payloadReceived,qint64{0});
    QTRY_VERIFY(window.nativeSessionSnapshot()->sequence>first->sequence);
    QCOMPARE(window.nativeSessionSnapshot()->epoch,first->epoch);
    QCOMPARE(window.nativeSessionSnapshot()->sample->counters.payloadReceived,first->sample->counters.payloadReceived);
    const auto beforeClose=window.nativeSessionSnapshot()->sequence;QPointer<ui::StatisticsPage> old(&page);
    child<QAction>(window,"classic-close-tab").trigger();QTRY_VERIFY(old.isNull());QVERIFY(!window.statisticsPage());
    QTRY_VERIFY_WITH_TIMEOUT(window.nativeSessionSnapshot()->sequence>beforeClose,10000);
    const auto whileClosed=window.nativeActivitySnapshot();auto& reopened=open(window);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&reopened.readState().has_value(),30000);
    QVERIFY(reopened.nativeActivitySnapshot());QVERIFY(reopened.nativeActivitySnapshot()->points().size()>=whileClosed->points().size());
    QCOMPARE(reopened.nativeSessionSnapshot()->epoch,first->epoch);
    QVERIFY(reopened.nativeSessionSnapshot()->sample->counters.payloadReceived>0);
    QVERIFY(retained->latest()==first);QVERIFY(fixture.intact());
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);
    QVERIFY(closed.first().at(1).toBool());QVERIFY(closed.first().at(2).toBool());
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);QVERIFY(fixture.accountingIntact());
  }
};
QTEST_MAIN(NativeSessionStatisticsBridgeTests)
#include "native_session_statistics_bridge_tests.moc"
