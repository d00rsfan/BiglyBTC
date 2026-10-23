#include "tracker_statistics_view.hpp"
#include <QAbstractItemModel>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
using namespace biglybtc;
auto packet(quint64 sequence=1,core::TrackerReadState state=core::TrackerReadState::Ready){
  auto data=std::make_shared<core::TrackerStatistics>();data->state=state;
  if(state==core::TrackerReadState::Ready){data->torrentHash=std::string(40,'a');core::TrackerEndpointFacts facts;
    facts.protocol=1;facts.localEndpoint="127.0.0.1:1234";facts.enabled=true;facts.failures=12;facts.seeders=0;facts.nextAnnounceSeconds=-5;
    data->rows.push_back(core::projectTrackerRow("http://user:secret@fixture.invalid/private/passkey?token=secret",3,true,facts));
    facts.failures=2;facts.seeders=9;facts.nextAnnounceSeconds=100;
    data->rows.push_back(core::projectTrackerRow("udp://other.invalid:9000/secret",1,false,facts));}
  auto result=std::make_shared<ui::TrackerStatisticsPacket>();result->session=7;result->epoch=2;result->sequence=sequence;result->snapshot=data;return result;
}
template<class T>T& child(QObject& owner,const char* id){auto* p=owner.findChild<T*>(QString::fromLatin1(id));if(!p){throw std::runtime_error("Missing tracker view fixture control");}return *p;}
QTableView& table(ui::TrackerStatisticsView& v){return child<QTableView>(v,"tracker-table");}
void drain(){for(int i=0;i<3;++i){QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::processEvents();}}
void show(ui::TrackerStatisticsView& v){v.resize(1250,500);v.show();drain();}
class TooltipHook final:public QObject {
public:std::function<void()> callback;
  bool eventFilter(QObject* object,QEvent* event)override{QPointer<QObject> watched(object);if(event->type()==QEvent::ToolTipChange&&callback){auto once=std::move(callback);once();}return watched.isNull();}
};
}
class TrackerStatisticsViewTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void pureProjectionPreservesZeroUnknownAndSignedIntervals(){
    auto absent=core::projectTrackerRow("https://user:password@fixture.invalid/a/passkey?secret=1#secret",255,false);
    QCOMPARE(absent.displayUrl,std::string("https://fixture.invalid/[path hidden]"));QVERIFY(!absent.failures);QVERIFY(!absent.seeders);QVERIFY(!absent.nextAnnounceSeconds);QCOMPARE(absent.protocol,0);
    core::TrackerEndpointFacts raw;raw.protocol=2;raw.enabled=true;raw.seeders=0;raw.leechers=-1;raw.downloaded=std::numeric_limits<int>::max();raw.nextAnnounceSeconds=std::numeric_limits<std::int64_t>::min();
    auto value=core::projectTrackerRow("udp://[::1]:9000/announce",4,true,raw);QCOMPARE(value.seeders,std::optional<int>{0});QVERIFY(!value.leechers);
    QCOMPARE(value.downloaded,std::optional<int>{std::numeric_limits<int>::max()});QCOMPARE(value.nextAnnounceSeconds,std::optional<std::int64_t>{std::numeric_limits<std::int64_t>::min()});
    raw.failures=128;raw.seeders=-2;value=core::projectTrackerRow("http://fixture.invalid",0,false,raw);QVERIFY(value.partial);QVERIFY(!value.failures);QVERIFY(!value.seeders);
    QCOMPARE(core::projectTrackerRow("http://bad\nhost/announce",0,false).displayUrl,std::string("Invalid tracker URL"));
  }
  void snapshotAdmissionRejectsForgedBoundsAndStaleBindings(){
    const auto good=packet(3);QVERIFY(core::validTrackerStatistics(*good->snapshot));QVERIFY(ui::acceptTrackerStatistics({},good,7));
    auto old=packet(2);QVERIFY(!ui::acceptTrackerStatistics(good,old,7));auto wrong=packet(4);wrong->session=8;QVERIFY(!ui::acceptTrackerStatistics(good,wrong,7));
    wrong=packet(4);wrong->epoch=1;QVERIFY(!ui::acceptTrackerStatistics(good,wrong,7));
    auto data=std::make_shared<core::TrackerStatistics>(*good->snapshot);data->rows.resize(core::MaximumTrackerRows+1);QVERIFY(!core::validTrackerStatistics(*data));
    data=std::make_shared<core::TrackerStatistics>(*good->snapshot);data->rows[0].seeders=-1;QVERIFY(!core::validTrackerStatistics(*data));
    data=std::make_shared<core::TrackerStatistics>(*good->snapshot);data->state=core::TrackerReadState::DisabledByIsolation;QVERIFY(!core::validTrackerStatistics(*data));
  }
  void actualRowsNumericSortFilterAndPlainPrivateUrlDisplay(){
    ui::TrackerStatisticsView v;QVERIFY(v.beginSession(7));show(v);QVERIFY(v.setPresentation(packet()));QCOMPARE(table(v).model()->rowCount(),2);
    table(v).sortByColumn(7,Qt::AscendingOrder);QCOMPARE(table(v).model()->index(0,7).data(Qt::UserRole).toInt(),2);
    QCOMPARE(table(v).model()->index(1,7).data(Qt::UserRole).toInt(),12);
    child<QLineEdit>(v,"tracker-filter").setText(QStringLiteral("fixture.invalid"));QTRY_COMPARE(table(v).model()->rowCount(),1);
    const auto display=table(v).model()->index(0,0).data().toString();QVERIFY(display.contains(QStringLiteral("fixture.invalid")));QVERIFY(!display.contains(QStringLiteral("secret")));QVERIFY(!display.contains(QStringLiteral("passkey")));
    QCOMPARE(table(v).model()->index(0,8).data(Qt::UserRole).toInt(),0);QVERIFY(table(v).model()->index(0,8).data(Qt::UserRole).isValid());
    QVERIFY(!table(v).model()->index(0,9).data(Qt::UserRole).isValid());QCOMPARE(table(v).model()->index(0,11).data(Qt::UserRole).toLongLong(),qlonglong{-5});
  }
  void disabledPolicyAndUnavailableStayDistinctWithoutRows(){
    ui::TrackerStatisticsView v;QVERIFY(v.beginSession(7));show(v);QVERIFY(v.setPresentation(packet(1,core::TrackerReadState::DisabledByIsolation)));
    QCOMPARE(table(v).model()->rowCount(),0);QVERIFY(child<QLabel>(v,"tracker-status").text().contains(QStringLiteral("isolation policy")));
    QVERIFY(v.setPresentation(packet(2,core::TrackerReadState::Unavailable)));QVERIFY(child<QLabel>(v,"tracker-status").text().contains(QStringLiteral("unavailable")));
  }
  void hiddenAndMinimizedCacheWithoutModelMutation(){
    ui::TrackerStatisticsView v;QVERIFY(v.beginSession(7));QVERIFY(v.setPresentation(packet()));QCOMPARE(table(v).model()->rowCount(),0);show(v);QTRY_COMPARE(table(v).model()->rowCount(),2);
    v.showMinimized();drain();QVERIFY(v.isMinimized());QVERIFY(v.setPresentation(packet(2,core::TrackerReadState::DisabledByIsolation)));QCOMPARE(table(v).model()->rowCount(),2);
    v.showNormal();QTRY_VERIFY(!v.isMinimized());QTRY_COMPARE(table(v).model()->rowCount(),0);
    QVERIFY(v.beginSession(8));QVERIFY(!v.setPresentation(packet(3)));QVERIFY(!v.presentation());
  }
  void modelResetRebindCannotRestoreOldTrackerRows(){
    ui::TrackerStatisticsView v;QVERIFY(v.beginSession(7));show(v);auto* proxy=qobject_cast<QSortFilterProxyModel*>(table(v).model());QVERIFY(proxy);
    bool once=false;const auto c=QObject::connect(proxy->sourceModel(),&QAbstractItemModel::modelReset,&v,[&]{if(!once){once=true;v.beginSession(8);}});
    QVERIFY(v.setPresentation(packet()));QVERIFY(once);QObject::disconnect(c);QTRY_COMPARE(table(v).model()->rowCount(),0);QVERIFY(!v.presentation());
  }
  void resultTooltipCallbackMayDeleteOwner(){
    QPointer<ui::TrackerStatisticsView> v=new ui::TrackerStatisticsView;QVERIFY(v->beginSession(7));show(*v);TooltipHook hook;
    hook.callback=[v]{delete v.data();};auto& status=child<QLabel>(*v,"tracker-status");status.setToolTip({});status.installEventFilter(&hook);
    v->setPresentation(packet());QVERIFY(v.isNull());
  }
};
QTEST_MAIN(TrackerStatisticsViewTests)
#include "tracker_statistics_view_tests.moc"
