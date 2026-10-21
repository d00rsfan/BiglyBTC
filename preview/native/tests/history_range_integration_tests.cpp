#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"
#include "history_range_view.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>
#include <QtTest>

#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& o, const char* name) {
  auto* p = o.findChild<T*>(QString::fromLatin1(name));
  if (!p) { throw std::runtime_error(std::string("Missing control ")+name); } return *p;
}
void show(ui::LibraryWindow& w) { w.resize(1280, 900); w.show(); QCoreApplication::processEvents(); }
ui::StatisticsPage& open(ui::LibraryWindow& w) {
  child<QAction>(w, "classic-statistics").trigger(); if (!w.statisticsPage()) { throw std::runtime_error("Missing Statistics"); } return *w.statisticsPage();
}
ui::StatisticsPage& select(ui::LibraryWindow& w) {
  auto& p=open(w); child<QTabWidget>(p,"statistics-history-tabs").setCurrentIndex(3);
  child<QTabWidget>(p,"statistics-transfer-tabs").setCurrentIndex(1); p.tabs().setCurrentIndex(1); return p;
}
void wire(ui::LibraryWindow& w, app::SessionBridge& b) {
  QObject::connect(&b,&app::SessionBridge::snapshotReady,&w,&ui::LibraryWindow::setPresentation);
  QObject::connect(&b,&app::SessionBridge::busyChanged,&w,&ui::LibraryWindow::setBusy);
  QObject::connect(&b,&app::SessionBridge::columnLayoutSessionReady,&w,&ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&b,&app::SessionBridge::columnLayoutsReady,&w,&ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&b,&app::SessionBridge::statisticsReady,&w,&ui::LibraryWindow::setStatistics);
  QObject::connect(&b,&app::SessionBridge::historyRangeReady,&w,&ui::LibraryWindow::setHistoryRange);
  QObject::connect(&w,&ui::LibraryWindow::columnLayoutsRequested,&b,&app::SessionBridge::columnLayouts);
  QObject::connect(&w,&ui::LibraryWindow::statisticsRequested,&b,&app::SessionBridge::statistics);
  QObject::connect(&w,&ui::LibraryWindow::historyRangeRequested,&b,&app::SessionBridge::historyRange);
  QObject::connect(&w,&ui::LibraryWindow::cancelRequested,&b,&app::SessionBridge::cancel);
}
std::shared_ptr<ui::StatisticsPacket> totals(quint64 session,quint64 request) {
  auto p=std::make_shared<ui::StatisticsPacket>();p->session=session;p->request=request;
  p->state=ui::StatisticsReadState::Ready;p->source=ui::StatisticsReadSource::Primary;
  p->snapshot=std::make_shared<const core::SavedStatistics>(*core::readSavedStatistics("de"));return p;
}
void totalsOwner(ui::LibraryWindow& w) {
  QObject::connect(&w,&ui::LibraryWindow::statisticsRequested,&w,[&w](quint64 s,quint64 r){
    w.setBusy(true); QTimer::singleShot(0,&w,[&w,s,r]{w.setStatistics(totals(s,r));w.setBusy(false);});
  });
}
std::shared_ptr<ui::HistoryRangePacket> packet(quint64 session,quint64 request,core::HistoryRangeRequest query) {
  auto plan=core::planSavedHistoryRange(query); if (!plan) { throw std::runtime_error("Invalid fixture range"); }
  auto accumulator=core::SavedHistoryRangeAccumulator::create(*plan);if(!accumulator){throw std::runtime_error("Invalid accumulator");}
  using namespace std::chrono;
  auto d=sys_days{year{plan->firstFile.year}/month{plan->firstFile.month}/day{plan->firstFile.day}};
  const auto last=sys_days{year{plan->lastFile.year}/month{plan->lastFile.month}/day{plan->lastFile.day}};
  for(;d<=last;d+=days{1}){const year_month_day date{d};if(!accumulator->append({int(date.year()),unsigned(date.month()),unsigned(date.day())},{})){throw std::runtime_error("Append failed");}}
  auto range=std::move(*accumulator).finish();if(!range){throw std::runtime_error("Finish failed");}
  auto p=std::make_shared<ui::HistoryRangePacket>();p->session=session;p->request=request;p->query=query;p->state=ui::HistoryRangeReadState::Ready;
  p->snapshot=std::make_shared<const core::SavedHistoryRange>(std::move(*range));return p;
}
struct Profile {
  struct Saved {QByteArray bytes;dev_t device;ino_t inode;};
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-rolling-window-XXXXXX")};QString path;std::map<QString,Saved> files;
  explicit Profile(bool malformed=false) {
    if(!root.isValid()){throw std::runtime_error("Owned temporary profile failed");}path=root.path()+QStringLiteral("/profile");
    put("/downloads.config",QByteArrayLiteral("d9:downloadslee"));put("/tables.config",QByteArrayLiteral("de"));put("/BiglyBT.statistics",QByteArrayLiteral("de"));
    const auto now=QDateTime::currentMSecsSinceEpoch();const auto time=QDateTime::fromMSecsSinceEpoch(now,QTimeZone::utc()).date();
    const auto suffix=QStringLiteral("/stats/%1/%2/%3.dat").arg(time.year(),4,10,QLatin1Char('0')).arg(time.month(),2,10,QLatin1Char('0')).arg(time.day(),2,10,QLatin1Char('0'));
    // The first observation is at the current minute. A separate future anchor keeps later records in the
    // future and must not enter current totals, even though they exist on disk.
    put(suffix,malformed?QByteArrayLiteral("malformed fixture\n"):
        QByteArray::fromStdString("s,1,"+std::to_string(now/60000-1)+",100,200,300,400,500,600\n1,2,3,4,5,6\ns,1,"+std::to_string(now/60000+1440)+",0,0,0,0,0,0\n100,200,300,400,500,600\n"));
    put(QStringLiteral("/stats/%1/%2/cache.dat").arg(time.year(),4,10,QLatin1Char('0')).arg(time.month(),2,10,QLatin1Char('0')),QByteArrayLiteral("CACHE_UNTOUCHED"));
  }
  void put(const QString& suffix,const QByteArray& bytes){const auto p=path+suffix;QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.flush()){throw std::runtime_error("Fixture write failed");}struct stat s{};if(::stat(p.toStdString().c_str(),&s)){throw std::runtime_error("Fixture stat failed");}files[p]={bytes,s.st_dev,s.st_ino};}
  bool intact()const{for(const auto&[p,v]:files){QFile f(p);struct stat s{};if(::stat(p.toStdString().c_str(),&s)||s.st_dev!=v.device||s.st_ino!=v.inode||!f.open(QIODevice::ReadOnly)||f.readAll()!=v.bytes||QFile::exists(p+".bak")||QFile::exists(p+".saving")){return false;}}return true;}
};
} // namespace
class HistoryRangeIntegrationTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);qRegisterMetaType<core::HistoryRangeRequest>();}
  void actualWorkerReadsRollingRangeAndPreservesAllProfileBytes(){
    Profile p;ui::LibraryWindow w;app::SessionBridge b;wire(w,b);show(w);
    QSignalSpy columns(&b,&app::SessionBridge::columnLayoutsReady),reads(&b,&app::SessionBridge::historyRangeReady),snapshots(&b,&app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(columns.count()==1&&!b.busy(),30000);
    auto& page=select(w);QTRY_VERIFY_WITH_TIMEOUT(page.historyRangePresentation()&&!b.busy(),30000);
    auto frame=page.historyRangePresentation();QCOMPARE(frame->state,ui::HistoryRangeReadState::Ready);QVERIFY(frame->snapshot);QCOMPARE(frame->snapshot->buckets.size(),std::size_t{168});
    QVERIFY(frame->snapshot->totals);QCOMPARE((*frame->snapshot->totals)[3],std::int64_t{4});QCOMPARE(frame->snapshot->observations,std::size_t{1});QCOMPARE(reads.count(),1);QCOMPARE(snapshots.count(),1);QVERIFY(p.intact());
    auto& traffic=child<QComboBox>(page,"saved-history-range-traffic");traffic.setCurrentIndex(1);QCoreApplication::processEvents();QCOMPARE(reads.count(),1);traffic.setCurrentIndex(2);QCoreApplication::processEvents();QCOMPARE(reads.count(),1);
    const auto captures=qEnvironmentVariable("BIGLYBTC_HISTORY_RANGE_CAPTURE_DIRECTORY");if(!captures.isEmpty()){QVERIFY(QDir().mkpath(captures));QVERIFY(w.grab().save(captures+"/rolling-week-dht.png"));}
    auto& period=child<QComboBox>(page,"saved-history-range-period");period.setCurrentIndex(3);QTRY_VERIFY_WITH_TIMEOUT(page.historyRangePresentation()&&page.historyRangePresentation()!=frame&&!b.busy(),30000);
    frame=page.historyRangePresentation();QCOMPARE(frame->state,ui::HistoryRangeReadState::Ready);QCOMPARE(frame->snapshot->buckets.size(),std::size_t{720});QCOMPARE((*frame->snapshot->totals)[3],std::int64_t{4});
    if(!captures.isEmpty()){QVERIFY(w.grab().save(captures+"/rolling-month-dht.png"));}
    period.setCurrentIndex(5);QTRY_VERIFY_WITH_TIMEOUT(page.historyRangePresentation()&&page.historyRangePresentation()!=frame&&!b.busy(),30000);
    QCOMPARE(page.historyRangePresentation()->state,ui::HistoryRangeReadState::Ready);QVERIFY(page.historyRangePresentation()->snapshot->totals);QVERIFY(p.intact());QCOMPARE(snapshots.count(),1);
  }
  void malformedOwnedFileIsOptionalFailureWithoutLibraryRefresh(){
    Profile p(true);ui::LibraryWindow w;app::SessionBridge b;wire(w,b);show(w);QSignalSpy columns(&b,&app::SessionBridge::columnLayoutsReady),snapshots(&b,&app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(columns.count()==1&&!b.busy(),30000);auto& page=select(w);QTRY_VERIFY_WITH_TIMEOUT(page.historyRangePresentation()&&!b.busy(),30000);
    QCOMPARE(page.historyRangePresentation()->state,ui::HistoryRangeReadState::Failed);QVERIFY(!page.historyRangePresentation()->snapshot);QCOMPARE(snapshots.count(),1);QVERIFY(p.intact());
  }
  void exactQueryAndModeGatesRejectStaleSameNumberCompletions(){
    ui::LibraryWindow w;show(w);w.beginColumnLayoutSession(73);totalsOwner(w);QSignalSpy reads(&w,&ui::LibraryWindow::historyRangeRequested);
    connect(&w,&ui::LibraryWindow::historyRangeRequested,&w,[&](quint64,quint64,core::HistoryRangeRequest){w.setBusy(true);});auto& page=select(w);QTRY_COMPARE(reads.count(),1);
    const auto request=reads.at(0).at(1).toULongLong();const auto query=qvariant_cast<core::HistoryRangeRequest>(reads.at(0).at(2));
    w.setStatistics(totals(73,request));auto wrong=query;--wrong.nowMillis;w.setHistoryRange(packet(73,request,wrong));w.setHistoryRange(packet(72,request,query));QVERIFY(!page.historyRangePresentation());
    w.setHistoryRange(packet(73,request,query));QVERIFY(page.historyRangePresentation());w.setBusy(false);
  }
  void freshRefreshCoalescesLatestQueryWhileAnotherOptionalReadOwnsWorker(){
    ui::LibraryWindow w;show(w);w.beginColumnLayoutSession(73);totalsOwner(w);QSignalSpy reads(&w,&ui::LibraryWindow::historyRangeRequested);
    connect(&w,&ui::LibraryWindow::historyRangeRequested,&w,[&](quint64,quint64,core::HistoryRangeRequest){w.setBusy(true);});auto& page=select(w);QTRY_COMPARE(reads.count(),1);
    const auto oldRequest=reads.at(0).at(1).toULongLong();const auto oldQuery=qvariant_cast<core::HistoryRangeRequest>(reads.at(0).at(2));
    auto& period=child<QComboBox>(page,"saved-history-range-period");period.setCurrentIndex(0);period.setCurrentIndex(3);QCOMPARE(reads.count(),1);
    w.setHistoryRange(packet(73,oldRequest,oldQuery));QVERIFY(!page.historyRangePresentation());w.setBusy(false);QTRY_COMPARE(reads.count(),2);
    const auto query=qvariant_cast<core::HistoryRangeRequest>(reads.at(1).at(2));QCOMPARE(query.period,core::HistoryPeriod::Month);
    const auto request=reads.at(1).at(1).toULongLong();QVERIFY(request>oldRequest);w.setHistoryRange(packet(73,request,query));w.setBusy(false);QVERIFY(page.historyRangePresentation());
    const auto before=QDateTime::currentMSecsSinceEpoch();child<QAction>(w,"classic-refresh").trigger();QTRY_COMPARE(reads.count(),3);const auto after=QDateTime::currentMSecsSinceEpoch();const auto fresh=qvariant_cast<core::HistoryRangeRequest>(reads.at(2).at(2));QVERIFY(reads.at(2).at(1).toULongLong()>request);QVERIFY(fresh.nowMillis>=std::min(before,after)&&fresh.nowMillis<=std::max(before,after));
    w.setHistoryRange(packet(73,reads.at(2).at(1).toULongLong(),fresh));w.setBusy(false);
  }
  void closingAndReopeningRemembersChoicesAndRereadsFreshResults(){
    ui::LibraryWindow w;show(w);w.beginColumnLayoutSession(73);totalsOwner(w);QSignalSpy reads(&w,&ui::LibraryWindow::historyRangeRequested);
    connect(&w,&ui::LibraryWindow::historyRangeRequested,&w,[&](quint64,quint64,core::HistoryRangeRequest){w.setBusy(true);});auto& page=select(w);QTRY_COMPARE(reads.count(),1);
    child<QComboBox>(page,"saved-history-range-period").setCurrentIndex(3);
    child<QPushButton>(page,"saved-history-range-previous").click();
    child<QComboBox>(page,"saved-history-range-traffic").setCurrentIndex(2);QPointer old(&page);child<QAction>(w,"classic-close-tab").trigger();QVERIFY(!w.statisticsPage());auto& next=open(w);QVERIFY(next.historyRangeSelected());QCOMPARE(child<QComboBox>(next,"saved-history-range-traffic").currentIndex(),2);QVERIFY(next.navigation());QCOMPARE(next.navigation()->rangePeriod,core::HistoryPeriod::Month);QCOMPARE(next.navigation()->rangeOffset,std::int64_t{-1});QVERIFY(!next.historyRangePresentation());
    const auto query=qvariant_cast<core::HistoryRangeRequest>(reads.at(0).at(2));w.setHistoryRange(packet(73,reads.at(0).at(1).toULongLong(),query));w.setBusy(false);QTRY_VERIFY(old.isNull());QTRY_COMPARE(reads.count(),2);QVERIFY(!next.historyRangePresentation());
    const auto fresh=qvariant_cast<core::HistoryRangeRequest>(reads.at(1).at(2));w.setHistoryRange(packet(73,reads.at(1).at(1).toULongLong(),fresh));w.setBusy(false);QVERIFY(next.historyRangePresentation());
  }
  void missingCompletionFailsOnlyItsExactQueryAndPreservesPendingNewerQuery(){
    ui::LibraryWindow w;show(w);w.beginColumnLayoutSession(73);totalsOwner(w);QSignalSpy reads(&w,&ui::LibraryWindow::historyRangeRequested);
    connect(&w,&ui::LibraryWindow::historyRangeRequested,&w,[&](quint64,quint64,core::HistoryRangeRequest){w.setBusy(true);});auto& page=select(w);QTRY_COMPARE(reads.count(),1);
    w.setBusy(false);QVERIFY(page.historyRangePresentation());QCOMPARE(page.historyRangePresentation()->state,ui::HistoryRangeReadState::Failed);QVERIFY(!page.historyRangePresentation()->snapshot);
    QCOMPARE(page.readState(),std::optional{ui::StatisticsReadState::Ready});
    child<QPushButton>(page,"saved-history-range-refresh").click();QTRY_COMPARE(reads.count(),2);
    child<QComboBox>(page,"saved-history-range-period").setCurrentIndex(3);QVERIFY(!page.historyRangePresentation());w.setBusy(false);QTRY_COMPARE(reads.count(),3);QVERIFY(!page.historyRangePresentation());
    const auto fresh=qvariant_cast<core::HistoryRangeRequest>(reads.at(2).at(2));QCOMPARE(fresh.period,core::HistoryPeriod::Month);
    w.setHistoryRange(packet(73,reads.at(2).at(1).toULongLong(),fresh));w.setBusy(false);QVERIFY(page.historyRangePresentation());QCOMPARE(page.historyRangePresentation()->state,ui::HistoryRangeReadState::Ready);
  }
};
QTEST_MAIN(HistoryRangeIntegrationTests)
#include "history_range_integration_tests.moc"
