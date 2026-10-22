#include "library_window.hpp"
#include "statistics_page.hpp"
#include "statistics_navigation.hpp"
#include "native_activity_history.hpp"
#include "native_activity_view.hpp"
#include "native_session_view.hpp"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>
#include <unistd.h>

#include <functional>
#include <memory>
#include <stdexcept>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& owner, const char* id) {
  auto* value = owner.findChild<T*>(QString::fromLatin1(id));
  if (!value) { throw std::runtime_error("Missing session integration control"); }
  return *value;
}
auto packet(quint64 sequence = 1, quint64 session = 7) {
  auto p = std::make_shared<ui::NativeSessionPacket>();
  p->session = session; p->applicationRun = 3; p->epoch = 1; p->sequence = sequence;
  p->state = ui::NativeSessionState::Idle;
  p->ownerElapsedNanoseconds = static_cast<qint64>(sequence) * 1000000000;
  p->sample = core::EngineStatistics{{5000,3000,4000,2500}, p->ownerElapsedNanoseconds,
      core::EngineRates{100,60,79,39,20,20}};
  return p;
}
ui::NativeActivityPresentation frame(ui::NativeSessionPresentation value) {
  ui::NativeActivityHistory history;
  if (!history.observe(value, value->session)) { throw std::runtime_error("Invalid synthetic native packet"); }
  return history.snapshot();
}
void show(QWidget& w) { w.resize(1280,900); w.show(); QCoreApplication::processEvents(); }
bool capture(ui::LibraryWindow& window, const QString& directory, const char* name) {
  const QFileInfo root(directory);
  if (!root.isAbsolute() || !root.isDir() || root.isSymLink()
      || root.canonicalFilePath() != directory || root.ownerId() != static_cast<uint>(::geteuid())) { return false; }
  const auto path = QDir(directory).filePath(QString::fromLatin1(name));
  const QFileInfo target(path);
  if (target.exists() || target.isSymLink()) { return false; }
  return window.grab().save(path, "PNG");
}
ui::StatisticsPage& open(ui::LibraryWindow& w) {
  child<QAction>(w, "classic-statistics").trigger();
  if (!w.statisticsPage()) { throw std::runtime_error("Statistics did not open"); }
  return *w.statisticsPage();
}
void rows(ui::LibraryWindow& w) {
  auto p = std::make_shared<ui::LibraryPresentation>(); p->revision = 11;
  p->items.push_back({.key=QStringLiteral("A"),.hash=QString(40,QLatin1Char('A')),
      .name=QStringLiteral("Alpha"),.state=QStringLiteral("Queued"),.bytes=100,.completed=0,.canContinue=true});
  w.setPresentation(p);
}
void totalsOwner(ui::LibraryWindow& w) {
  // Explicit synthetic saved-total owner only; no engine/worker/IO credit.
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&w](quint64 session, quint64 request) {
    w.setBusy(true);
    QTimer::singleShot(0, &w, [&w,session,request] {
      auto p = std::make_shared<ui::StatisticsPacket>(); p->session=session; p->request=request;
      p->state=ui::StatisticsReadState::Missing; w.setStatistics(p); w.setBusy(false);
    });
  });
}
bool bind(ui::LibraryWindow& w, quint64 session=7) {
  QSignalSpy columns(&w,&ui::LibraryWindow::columnLayoutsRequested); w.beginColumnLayoutSession(session);
  return QTest::qWaitFor([&]{return columns.count()==1;},3000);
}
class Hook final : public QObject {
public:
  bool armed=true, consume=false;
  std::function<void()> callback;
  bool eventFilter(QObject*,QEvent* e) override {
    if (armed && e->type()==QEvent::ToolTipChange) {armed=false;callback();return consume;}
    return false;
  }
};
} // namespace

class NativeSessionStatisticsTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void freshDefaultAndExplicitOldNavigationPreserveAllThreeIndices() {
    ui::StatisticsPage fresh;
    auto& tabs=child<QTabWidget>(fresh,"statistics-activity-views");
    QCOMPARE(tabs.count(),3); QCOMPARE(tabs.currentIndex(),2);
    QCOMPARE(tabs.tabText(0),QStringLiteral("Payload")); QCOMPARE(tabs.tabText(1),QStringLiteral("Engine traffic"));
    QCOMPARE(tabs.tabText(2),QStringLiteral("Session traffic")); QVERIFY(fresh.navigation()); QCOMPARE(fresh.navigation()->activity,2);
    QCOMPARE(ui::StatisticsNavigation{}.activity,2);
    for (int index=0;index<3;++index) {
      ui::StatisticsNavigation choice; choice.activity=index; QVERIFY(ui::validStatisticsNavigation(choice));
      ui::StatisticsPage restored(nullptr,choice); QCOMPARE(child<QTabWidget>(restored,"statistics-activity-views").currentIndex(),index);
      QVERIFY(restored.navigation()); QCOMPARE(*restored.navigation(),choice);
    }
    ui::StatisticsNavigation bad; bad.activity=3; QVERIFY(!ui::validStatisticsNavigation(bad));
    ui::StatisticsPage fallback(nullptr,bad); QCOMPARE(fallback.navigation()->activity,2);
  }
  void nativeGroupPrecedesOperationAndStreamsNeverPopulateEachOther() {
    ui::StatisticsPage page; page.beginRequest(7,1); show(page); page.tabs().setCurrentIndex(1);
    auto& group=child<QGroupBox>(page,"statistics-native-session"); auto& operation=child<QGroupBox>(page,"statistics-engine-session");
    QVERIFY(group.parentWidget()==operation.parentWidget()); QVERIFY(group.parentWidget()->layout());
    QVERIFY(group.parentWidget()->layout()->indexOf(&group)<group.parentWidget()->layout()->indexOf(&operation));
    QVERIFY(!page.findChild<QLabel*>(QStringLiteral("statistics-current-unavailable")));
    auto op=std::make_shared<ui::EngineStatisticsPacket>();op->session=7;op->operation=1;op->sequence=2;
    op->target={QStringLiteral("A"),1};op->state=ui::EngineStatisticsState::Observed;
    op->sample=core::EngineStatistics{{12,8,7,3},1000000000,core::EngineRates{12,8,7,3,5,5}};
    page.setEngineStatistics(op); QCOMPARE(child<QLabel>(page,"statistics-engine-received").text(),QStringLiteral("12 bytes"));
    QCOMPARE(child<QLabel>(page,"native-session-counter-received").text(),QStringLiteral("Unavailable"));
    page.setNativeActivity(frame(packet()));
    QCOMPARE(child<QLabel>(page,"native-session-counter-received").text(),QStringLiteral("5000"));
    QCOMPARE(child<QLabel>(page,"native-session-counter-protocol-received").text(),QStringLiteral("1000"));
    QCOMPARE(child<QLabel>(page,"native-session-rate-protocol-received").text(),QStringLiteral("20"));
    QCOMPARE(child<QLabel>(page,"statistics-engine-received").text(),QStringLiteral("12 bytes"));
    QCOMPARE(page.sampleCount(),std::size_t{0}); QVERIFY(!page.engineActivitySnapshot());
    const auto cached=page.nativeActivitySnapshot(); page.beginRequest(7,2); QVERIFY(page.nativeActivitySnapshot()==cached);
  }
  void windowRetainsAcrossCloseReopenAndPreservesLibraryWithNoNativeRead() {
    ui::LibraryWindow window; totalsOwner(window); rows(window); QVERIFY(bind(window)); show(window);
    window.filter().setText(QStringLiteral("Alpha")); const auto target=window.model().targetAt(0); QVERIFY(target);
    QSignalSpy reads(&window,&ui::LibraryWindow::statisticsRequested), columns(&window,&ui::LibraryWindow::columnLayoutsRequested);
    QSignalSpy transfer(&window,&ui::LibraryWindow::continueRequested),cancel(&window,&ui::LibraryWindow::cancelRequested);
    window.setNativeSession(packet()); window.setNativeSession(packet(2)); QVERIFY(!window.statisticsPage());
    const auto frozen=window.nativeActivitySnapshot(); QVERIFY(frozen); QCOMPARE(frozen->points().size(),std::size_t{2});
    auto& page=open(window); QTRY_VERIFY(!page.loading()); QCOMPARE(reads.count(),1);
    QVERIFY(page.nativeActivitySnapshot()==window.nativeActivitySnapshot());
    QCOMPARE(child<ui::NativeActivityView>(page,"statistics-native-activity-view").sampleCount(),std::size_t{2});
    QPointer<ui::StatisticsPage> old(&page); child<QAction>(window,"classic-close-tab").trigger();
    QTRY_VERIFY(old.isNull()); QVERIFY(!window.statisticsPage());
    window.setNativeSession(packet(3)); QCOMPARE(reads.count(),1); QCOMPARE(columns.count(),0);
    auto& reopened=open(window); QTRY_VERIFY(!reopened.loading()); QCOMPARE(reads.count(),2);
    QCOMPARE(reopened.nativeActivitySnapshot()->points().size(),std::size_t{3});
    QCOMPARE(reopened.nativeSessionSnapshot()->sequence,quint64{3});
    QCOMPARE(frozen->points().size(),std::size_t{2}); QCOMPARE(frozen->latest()->sequence,quint64{2});
    QCOMPARE(window.model().targetAt(0),target); QCOMPARE(window.filter().text(),QStringLiteral("Alpha"));
    QCOMPARE(transfer.count(),0); QCOMPARE(cancel.count(),0); QCOMPARE(columns.count(),0);
    if (const auto directory = qEnvironmentVariable("BIGLYBTC_NATIVE_SESSION_CAPTURE_DIRECTORY"); !directory.isEmpty()) {
      reopened.tabs().setCurrentIndex(0);
      child<QTabWidget>(reopened,"statistics-activity-views").setCurrentIndex(2);
      QCoreApplication::processEvents();
      QVERIFY2(capture(window,directory,"synthetic-native-session-activity.png"),"Cannot save owned synthetic session Activity capture");
      reopened.tabs().setCurrentIndex(1);
      child<QTabWidget>(reopened,"statistics-transfer-tabs").setCurrentIndex(0);
      QCoreApplication::processEvents();
      QVERIFY2(capture(window,directory,"synthetic-native-session-current.png"),"Cannot save owned synthetic session Current capture");
    }
  }
  void closedNavigationRestoresAllIndicesAndNewProfileUsesFreshDefault() {
    for (int index=0;index<3;++index) {
      ui::LibraryWindow window; totalsOwner(window); QVERIFY(bind(window)); show(window);
      auto& page=open(window); QTRY_VERIFY(!page.loading()); child<QTabWidget>(page,"statistics-activity-views").setCurrentIndex(index);
      child<QAction>(window,"classic-close-tab").trigger(); QTRY_VERIFY(!window.statisticsPage());
      auto& restored=open(window); QTRY_VERIFY(!restored.loading()); QCOMPARE(restored.navigation()->activity,index);
      child<QAction>(window,"classic-close-tab").trigger(); QTRY_VERIFY(!window.statisticsPage());
      QVERIFY(bind(window,8)); auto& fresh=open(window); QTRY_VERIFY(!fresh.loading()); QCOMPARE(fresh.navigation()->activity,2);
    }
  }
  void windowRejectsStalePacketsClearsEpochAndResetsBeforeNewProfileCallbacks() {
    ui::LibraryWindow window; totalsOwner(window); QVERIFY(bind(window)); show(window); auto& page=open(window); QTRY_VERIFY(!page.loading());
    window.setNativeSession(packet()); const auto first=window.nativeActivitySnapshot();
    auto stale=packet(); window.setNativeSession(stale); QVERIFY(window.nativeActivitySnapshot()==first);
    stale=packet(2);stale->applicationRun=4;window.setNativeSession(stale); QVERIFY(window.nativeActivitySnapshot()==first);
    auto epoch=packet(1);epoch->epoch=2;epoch->sample->counters={0,0,0,0};epoch->sample->rates=core::EngineRates{};
    window.setNativeSession(epoch); QCOMPARE(window.nativeActivitySnapshot()->points().size(),std::size_t{1});
    QCOMPARE(page.nativeSessionSnapshot()->epoch,quint64{2});
    QVERIFY(bind(window,8)); QTRY_VERIFY(!page.loading()); QVERIFY(!window.nativeSessionSnapshot()); QVERIFY(!window.nativeActivitySnapshot());
    QVERIFY(!page.nativeSessionSnapshot()); QVERIFY(!page.nativeActivitySnapshot());
    window.setNativeSession(packet(2,7)); QVERIFY(!window.nativeSessionSnapshot());
    auto oldRun=packet(1,8);oldRun->applicationRun=2;window.setNativeSession(oldRun);QVERIFY(!window.nativeSessionSnapshot());
    window.setNativeSession(packet(1,8)); QCOMPARE(page.nativeSessionSnapshot()->session,quint64{8});
    QCOMPARE(first->latest()->epoch,quint64{1});
  }
  void hiddenSubviewsKeepCppFramesAndOnlyRenderWhenActuallyVisible() {
    ui::StatisticsPage page; page.beginRequest(7,1); show(page);
    child<QTabWidget>(page,"statistics-activity-views").setCurrentIndex(0);
    const auto f=frame(packet());page.setNativeActivity(f); QVERIFY(page.nativeActivitySnapshot()==f);
    QCOMPARE(child<QLabel>(page,"native-activity-rate-received").text(),QStringLiteral("Unavailable"));
    QCOMPARE(child<QLabel>(page,"native-session-counter-received").text(),QStringLiteral("Unavailable"));
    child<QTabWidget>(page,"statistics-activity-views").setCurrentIndex(2);
    QTRY_COMPARE(child<QLabel>(page,"native-activity-rate-received").text(),QStringLiteral("100 bytes/s"));
    page.tabs().setCurrentIndex(1); QTRY_COMPARE(child<QLabel>(page,"native-session-counter-received").text(),QStringLiteral("5000"));
  }
  void pageBindingReplayHandlesNativeArrivalDuringChildReset() {
    ui::StatisticsPage page; page.beginNativeSession(7);show(page);page.tabs().setCurrentIndex(1);
    page.setNativeActivity(frame(packet()));
    const auto fresh=frame(packet(1,8)); Hook hook;
    hook.callback=[&]{page.setNativeActivity(fresh);};
    child<QLabel>(page,"native-session-counter-received").installEventFilter(&hook);
    page.beginNativeSession(8); QVERIFY(!hook.armed); QVERIFY(page.nativeActivitySnapshot()==fresh);
    QCOMPARE(child<ui::NativeActivityView>(page,"statistics-native-activity-view").sampleCount(),std::size_t{1});
    QCOMPARE(child<QLabel>(page,"native-session-counter-received").text(),QStringLiteral("5000"));
  }
  void reentrantProfileChangeAndPageDeletionCannotForwardOldFrame() {
    ui::LibraryWindow window; totalsOwner(window); QVERIFY(bind(window)); show(window); auto& page=open(window);QTRY_VERIFY(!page.loading());
    page.tabs().setCurrentIndex(1); Hook change;change.callback=[&]{window.beginColumnLayoutSession(8);};
    child<QLabel>(page,"native-session-counter-protocol-sent").installEventFilter(&change);
    window.setNativeSession(packet()); QVERIFY(!change.armed); QVERIFY(!window.nativeSessionSnapshot()); QVERIFY(!page.nativeSessionSnapshot());
    QTRY_VERIFY(!page.loading());
    QPointer<ui::StatisticsPage> old(&page); Hook remove;remove.consume=true;remove.callback=[&]{delete old.data();};
    child<QLabel>(page,"native-session-counter-protocol-sent").installEventFilter(&remove);
    window.setNativeSession(packet(1,8)); QVERIFY(!remove.armed); QVERIFY(!old); QVERIFY(!window.statisticsPage());
    QVERIFY(window.nativeActivitySnapshot()); auto& reopened=open(window);QTRY_VERIFY(!reopened.loading());
    QVERIFY(reopened.nativeActivitySnapshot()==window.nativeActivitySnapshot());
  }
};
QTEST_MAIN(NativeSessionStatisticsTests)
#include "native_session_statistics_tests.moc"
