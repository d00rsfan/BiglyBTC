#include "library_window.hpp"
#include "statistics_page.hpp"
#include "country_statistics_view.hpp"
#include "biglybtc/country_statistics.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
constexpr quint64 Session=73;
template<class T>T& child(QObject& owner,const char* name){
  auto* result=owner.findChild<T*>(QString::fromLatin1(name));
  if(!result){throw std::runtime_error("Missing Country navigation control");}return *result;
}
QAction& action(ui::LibraryWindow& w,const char* name){return child<QAction>(w,name);}
void drain(){for(int i=0;i<3;++i){QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::processEvents();}}
void show(ui::LibraryWindow& w){w.resize(1280,900);w.show();w.activateWindow();drain();}
ui::StatisticsPage& open(ui::LibraryWindow& w){
  action(w,"classic-statistics").trigger();if(!w.statisticsPage()){throw std::runtime_error("Statistics did not open");}return *w.statisticsPage();
}
void bind(ui::LibraryWindow& w,quint64 session=Session){w.beginColumnLayoutSession(session);drain();}
void totalsOwner(ui::LibraryWindow& w){
  // Explicit synthetic saved-total owner accepts synchronously like Bridge.
  // Country packets below have no worker/provider/runtime qualification credit.
  QObject::connect(&w,&ui::LibraryWindow::statisticsRequested,&w,[&w](quint64 session,quint64 request){
    w.setBusy(true);auto p=std::make_shared<ui::StatisticsPacket>();p->session=session;p->request=request;
    p->state=ui::StatisticsReadState::Missing;w.setStatistics(p);w.setBusy(false);
  });
}
auto packet(quint64 sequence=1,quint64 session=Session,quint64 epoch=3,
    qint32 download=101,bool available=true,quint64 ledgerSequence=7){
  core::PeerPayloadSnapshot ledger;ledger.identity={41,session,epoch};ledger.sequence=ledgerSequence;ledger.count=2;
  for(std::size_t i=0;i<ledger.count;++i){auto& p=ledger.records[i];p.connection=i+1;p.operation=1;p.sequence=i+1;
    p.phase=core::PeerPayloadPhase::Connected;p.network=core::PeerNetworkClass::NonLocal;
    p.remote.family=core::PeerAddressFamily::V4;p.remote.address={81,2,69,160};p.remote.port=1234;
    p.latestSampleAvailable=true;p.lastObserved=core::PeerPayloadSample{9999999,8888888,download,23};}
  ledger.records[0].network=core::PeerNetworkClass::Local;ledger.records[0].lastObserved=core::PeerPayloadSample{};
  core::CountryResolvedPeer country;country.connection=2;country.location.kind=core::CountryLookupKind::Country;
  country.location.code={'U','S',0}; // Explicit synthetic geography, no real provider claim.
  const auto projected=core::projectCountryStatistics(ledger,{&country,1},available);
  if(!projected){throw std::runtime_error("Actual Country projection rejected generated peer fixture");}
  auto p=std::make_shared<ui::CountryStatisticsPacket>();p->session=session;p->epoch=epoch;p->sequence=sequence;
  p->snapshot=std::make_shared<const core::CountryStatistics>(*projected);return p;
}
QTableView& table(ui::StatisticsPage& p){return child<QTableView>(p,"country-table");}
QAbstractItemModel& source(ui::StatisticsPage& p){
  auto* proxy=qobject_cast<QSortFilterProxyModel*>(table(p).model());
  if(!proxy||!proxy->sourceModel()){throw std::runtime_error("Missing Country source model");}return *proxy->sourceModel();
}
QVariant rate(ui::StatisticsPage& p,const QString& name){
  auto* model=table(p).model();for(int row=0;row<model->rowCount();++row){
    if(model->index(row,0).data().toString()==name){return model->index(row,2).data(Qt::UserRole);}}
  return {};
}
void rows(ui::LibraryWindow& w){
  auto p=std::make_shared<ui::LibraryPresentation>();p->revision=11;
  p->items.push_back({.key=QStringLiteral("row-1"),.hash=QStringLiteral("000102030405060708090a0b0c0d0e0f10111213"),
      .name=QStringLiteral("Alpha torrent"),.state=QStringLiteral("Queued"),.bytes=100,.completed=0,.canContinue=true});w.setPresentation(p);
}
class TooltipHook final:public QObject{
public:std::function<void()> callback;
  bool eventFilter(QObject* object,QEvent* event)override{
    QPointer<QObject> watched(object);if(event->type()==QEvent::ToolTipChange&&callback){auto once=std::move(callback);once();}
    return watched.isNull();
  }
};
}

class CountryStatisticsWindowTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);
  }
  void appendedRouteIsLazyAndCtrlOneRetainsLibraryTarget(){
    ui::LibraryWindow w;totalsOwner(w);rows(w);show(w);bind(w);w.filter().setText(QStringLiteral("Alpha torrent"));
    w.incompleteTable().selectRow(0);w.incompleteTable().setCurrentIndex(w.incompleteTable().model()->index(0,0));
    const QPersistentModelIndex selection(w.incompleteTable().currentIndex());const auto target=w.model().targetAt(0);QVERIFY(target);
    QWidget* const torrents=w.mainTabs().currentWidget();QSignalSpy reads(&w,&ui::LibraryWindow::countryStatisticsRequested);
    QSignalSpy libraryRefresh(&w,&ui::LibraryWindow::refreshRequested);auto& page=open(w);drain();
    const QStringList expected{QStringLiteral("Activity"),QStringLiteral("Transfers"),QStringLiteral("Cache"),
      QStringLiteral("Tracker"),QStringLiteral("Counters"),QStringLiteral("Tags"),QStringLiteral("Country")};
    QCOMPARE(page.tabs().count(),11);QCOMPARE(page.tabs().tabText(10),QStringLiteral("I2P"));for(int i=0;i<expected.size();++i){QCOMPARE(page.tabs().tabText(i),expected[i]);}
    QCOMPARE(page.tabs().currentIndex(),0);QCOMPARE(child<QTabWidget>(page,"statistics-activity-views").currentIndex(),2);
    QCOMPARE(reads.count(),0);auto value=packet();w.setCountryStatistics(value);drain();QCOMPARE(reads.count(),0);
    QVERIFY(w.countryStatisticsSnapshot()==value);QVERIFY(page.countryStatisticsSnapshot()==value);
    page.tabs().setCurrentIndex(6);QTRY_COMPARE(reads.count(),1);QCOMPARE(reads.at(0).at(0).toULongLong(),Session);
    QVERIFY(page.countrySelected());QTRY_COMPARE(table(page).model()->rowCount(),2);
    QCOMPARE(rate(page,QStringLiteral("US")).toLongLong(),qlonglong{101});QVERIFY(rate(page,QStringLiteral("Local")).isValid());
    QCOMPARE(rate(page,QStringLiteral("Local")).toLongLong(),qlonglong{0});
    auto& search=child<QLineEdit>(page,"country-filter");search.setFocus();QTest::keyClick(&search,Qt::Key_1,Qt::ControlModifier);
    QTRY_COMPARE(w.mainTabs().currentWidget(),torrents);QCOMPARE(w.incompleteTable().currentIndex(),QModelIndex(selection));
    QCOMPARE(w.model().targetAt(0),target);QCOMPARE(w.filter().text(),QStringLiteral("Alpha torrent"));QCOMPARE(libraryRefresh.count(),0);
  }
  void busyHiddenAndMinimizedRefreshIntentsCoalesceAndF5AvoidsSavedTotals(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);drain();
    QSignalSpy reads(&w,&ui::LibraryWindow::countryStatisticsRequested);QSignalSpy totals(&w,&ui::LibraryWindow::statisticsRequested);
    w.setBusy(true);page.tabs().setCurrentIndex(6);action(w,"classic-refresh").trigger();action(w,"classic-refresh").trigger();drain();
    QCOMPARE(reads.count(),0);w.setBusy(false);QTRY_COMPARE(reads.count(),1);drain();QCOMPARE(reads.count(),1);QCOMPARE(totals.count(),0);
    w.setBusy(true);action(w,"classic-refresh").trigger();page.tabs().setCurrentIndex(0);w.setBusy(false);drain();QCOMPARE(reads.count(),1);
    page.tabs().setCurrentIndex(6);QTRY_COMPARE(reads.count(),2);
    w.hide();drain();action(w,"classic-refresh").trigger();action(w,"classic-refresh").trigger();drain();QCOMPARE(reads.count(),2);
    show(w);QTRY_COMPARE(reads.count(),3);
    w.showMinimized();drain();QVERIFY(w.isMinimized());action(w,"classic-refresh").trigger();action(w,"classic-refresh").trigger();drain();
    QCOMPARE(reads.count(),3);const auto value=packet();w.setCountryStatistics(value);drain();QCOMPARE(reads.count(),3);
    QVERIFY(w.countryStatisticsSnapshot()==value);QCOMPARE(table(page).model()->rowCount(),0);
    w.showNormal();w.activateWindow();QTRY_VERIFY(!w.isMinimized());QTRY_COMPARE(reads.count(),4);
    QTRY_COMPARE(rate(page,QStringLiteral("US")).toLongLong(),qlonglong{101});drain();QCOMPARE(reads.count(),4);
    action(w,"classic-refresh").trigger();QTRY_COMPARE(reads.count(),5);drain();QCOMPARE(totals.count(),0);
  }
  void closeReopenRestoresCountryCacheAndNewProfileClearsClosedMemory(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);drain();
    QSignalSpy reads(&w,&ui::LibraryWindow::countryStatisticsRequested);page.tabs().setCurrentIndex(6);QTRY_COMPARE(reads.count(),1);
    auto first=packet();w.setCountryStatistics(first);QTRY_COMPARE(rate(page,QStringLiteral("US")).toLongLong(),qlonglong{101});
    QPointer<ui::StatisticsPage> old=&page;action(w,"classic-close-tab").trigger();QVERIFY(!w.statisticsPage());QVERIFY(old);
    auto cached=packet(2,Session,3,909);cached->current=false;w.setCountryStatistics(cached);w.setCountryStatistics(first);
    QVERIFY(w.countryStatisticsSnapshot()==cached);emit old->countryVisible();drain();QCOMPARE(reads.count(),1);
    auto& fresh=open(w);QVERIFY(&fresh!=old.data());QCOMPARE(fresh.tabs().currentIndex(),6);QTRY_COMPARE(reads.count(),2);
    QTRY_COMPARE(rate(fresh,QStringLiteral("US")).toLongLong(),qlonglong{909});QVERIFY(fresh.countryStatisticsSnapshot()==cached);
    QVERIFY(child<QLabel>(fresh,"country-status").text().contains(QStringLiteral("Last observed")));
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(old.isNull());
    action(w,"classic-close-tab").trigger();w.beginColumnLayoutSession(0);w.beginColumnLayoutSession(Session-1);
    QVERIFY(w.countryStatisticsSnapshot()==cached);w.beginColumnLayoutSession(Session+1);QVERIFY(!w.countryStatisticsSnapshot());
    auto& next=open(w);QCOMPARE(next.tabs().currentIndex(),0);QVERIFY(!next.countryStatisticsSnapshot());drain();QCOMPARE(reads.count(),2);
    w.setCountryStatistics(cached);QVERIFY(!w.countryStatisticsSnapshot());next.tabs().setCurrentIndex(6);QTRY_COMPARE(reads.count(),3);
    QCOMPARE(reads.at(2).at(0).toULongLong(),Session+1);auto current=packet(1,Session+1,1,707);w.setCountryStatistics(current);
    QTRY_COMPARE(rate(next,QStringLiteral("US")).toLongLong(),qlonglong{707});w.setCountryStatistics(cached);
    QVERIFY(w.countryStatisticsSnapshot()==current);
  }
  void visibleProfileChangeStartsExactlyOneNewCountryIntent(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);drain();
    QSignalSpy reads(&w,&ui::LibraryWindow::countryStatisticsRequested);page.tabs().setCurrentIndex(6);QTRY_COMPARE(reads.count(),1);
    const auto old=packet();w.setCountryStatistics(old);QTRY_COMPARE(table(page).model()->rowCount(),2);
    w.beginColumnLayoutSession(Session+1);QCOMPARE(page.tabs().currentIndex(),6);QVERIFY(!w.countryStatisticsSnapshot());
    QTRY_COMPARE(reads.count(),2);QCOMPARE(reads.at(1).at(0).toULongLong(),Session+1);drain();QCOMPARE(reads.count(),2);
    QTRY_COMPARE(table(page).model()->rowCount(),0);QVERIFY(!page.countryStatisticsSnapshot());
    w.setCountryStatistics(old);QVERIFY(!w.countryStatisticsSnapshot());auto current=packet(1,Session+1,1,303);w.setCountryStatistics(current);
    QTRY_COMPARE(rate(page,QStringLiteral("US")).toLongLong(),qlonglong{303});
    w.beginColumnLayoutSession(Session+1);w.beginColumnLayoutSession(Session);w.beginColumnLayoutSession(0);drain();
    QCOMPARE(reads.count(),2);QVERIFY(w.countryStatisticsSnapshot()==current);
  }
  void modelCallbackProfileRebindCannotRestoreOldMetadata(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);page.tabs().setCurrentIndex(6);drain();
    bool rebound=false;const auto connection=QObject::connect(&source(page),&QAbstractItemModel::modelReset,&w,[&]{
      if(!rebound){rebound=true;w.beginColumnLayoutSession(Session+1);}
    });
    w.setCountryStatistics(packet());QVERIFY(rebound);QObject::disconnect(connection);QVERIFY(!w.countryStatisticsSnapshot());
    QTRY_COMPARE(table(page).model()->rowCount(),0);QVERIFY(!page.countryStatisticsSnapshot());
    auto current=packet(1,Session+1,1,404);w.setCountryStatistics(current);QTRY_COMPARE(rate(page,QStringLiteral("US")).toLongLong(),qlonglong{404});
    QVERIFY(w.countryStatisticsSnapshot()==current);
  }
  void newerPacketDuringModelResetWinsWithoutAnotherRead(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);page.tabs().setCurrentIndex(6);drain();
    QSignalSpy reads(&w,&ui::LibraryWindow::countryStatisticsRequested);auto first=packet();auto replacement=packet(2,Session,3,505,false);
    bool replaced=false;const auto connection=QObject::connect(&source(page),&QAbstractItemModel::modelReset,&w,[&]{
      if(!replaced){replaced=true;w.setCountryStatistics(replacement);}
    });
    w.setCountryStatistics(first);QVERIFY(replaced);QObject::disconnect(connection);QVERIFY(w.countryStatisticsSnapshot()==replacement);
    QTRY_COMPARE(rate(page,QStringLiteral("Country unavailable")).toLongLong(),qlonglong{505});
    QVERIFY(!rate(page,QStringLiteral("US")).isValid());QVERIFY(page.countryStatisticsSnapshot()==replacement);drain();QCOMPARE(reads.count(),0);
  }
  void readOrResultCallbackMayDeleteWindow_data(){
    QTest::addColumn<bool>("duringRead");QTest::newRow("queued-read")<<true;QTest::newRow("accepted-result-tooltip")<<false;
  }
  void readOrResultCallbackMayDeleteWindow(){
    QFETCH(bool,duringRead);QPointer<ui::LibraryWindow> w=new ui::LibraryWindow;totalsOwner(*w);show(*w);bind(*w);
    auto& page=open(*w);QPointer<ui::StatisticsPage> old=&page;
    if(duringRead){QObject::connect(w,&ui::LibraryWindow::countryStatisticsRequested,w,[w](quint64){delete w.data();});
      page.tabs().setCurrentIndex(6);QTRY_VERIFY(w.isNull());
    }else{page.tabs().setCurrentIndex(6);drain();TooltipHook remove;remove.callback=[w]{delete w.data();};
      auto& status=child<QLabel>(page,"country-status");status.setToolTip({});status.installEventFilter(&remove);
      w->setCountryStatistics(packet());QVERIFY(!remove.callback);QVERIFY(w.isNull());
    }
    QVERIFY(old.isNull());
  }
};
QTEST_MAIN(CountryStatisticsWindowTests)
#include "country_statistics_window_tests.moc"
