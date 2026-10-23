#include "tracker_statistics_view.hpp"
#include "statistics_page.hpp"
#include <QApplication>
#include <QAbstractItemModel>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>
#include <functional>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace biglybtc;
using Type=core::RuntimeMetricType;using Group=core::RuntimeMetricGroup;
// Explicit synthetic immutable metric packets test consumer policy/rendering.
// Real provider availability and units come from the pinned native LT catalog;
// these fixtures do not certify tracker requests or Java activity equivalence.
auto sample(quint64 publication,quint64 response,std::int64_t time,std::optional<std::int64_t> queue=7,
    std::optional<std::int64_t> received=0,std::optional<std::int64_t> sent=0,quint64 epoch=2){
  auto data=std::make_shared<core::RuntimeMetrics>();data->sequence=response;data->ownerElapsedNanoseconds=time;
  if(queue){data->metrics.push_back({"tracker.num_queued_tracker_announces",Type::Gauge,Group::Tracker,*queue});}
  if(received){data->metrics.push_back({"net.recv_tracker_bytes",Type::Counter,Group::Net,*received});}
  if(sent){data->metrics.push_back({"net.sent_tracker_bytes",Type::Counter,Group::Net,*sent});}
  auto packet=std::make_shared<ui::RuntimeMetricsPacket>();packet->session=7;packet->epoch=epoch;packet->sequence=publication;packet->snapshot=data;return packet;
}
auto tablePacket(){
  auto data=std::make_shared<core::TrackerStatistics>();data->state=core::TrackerReadState::Ready;data->torrentHash=std::string(40,'a');
  core::TrackerEndpointFacts facts;facts.protocol=1;facts.localEndpoint="127.0.0.1:1234";facts.enabled=true;facts.updating=true;facts.failures=4;facts.seeders=0;facts.nextAnnounceSeconds=-10;
  data->rows.push_back(core::projectTrackerRow("http://user:secret@fixture.invalid/hidden?key=secret",3,true,facts));
  auto packet=std::make_shared<ui::TrackerStatisticsPacket>();packet->session=7;packet->epoch=2;packet->sequence=1;packet->snapshot=data;return packet;
}
template<class T>T& child(QObject& parent,const char* name){auto* p=parent.findChild<T*>(QString::fromLatin1(name));if(!p){throw std::runtime_error("Missing tracker history fixture widget");}return *p;}
void drain(){QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::processEvents();}
void show(ui::TrackerStatisticsView& view){auto font=view.font();font.setStyleStrategy(QFont::NoSubpixelAntialias);view.setFont(font);view.resize(1024,650);view.show();drain();}
class Hook final:public QObject {
public:std::function<void()> callback;
 bool eventFilter(QObject* watched,QEvent* event)override{QPointer<QObject> guard(watched);if(event->type()==QEvent::ToolTipChange&&callback){auto once=std::move(callback);once();}return guard.isNull();}
};
int colored(const QImage& image,QRect region,QColor color){
  region=region.intersected(image.rect());int count=0;
  for(int y=region.top();y<=region.bottom();++y){for(int x=region.left();x<=region.right();++x){const auto p=image.pixelColor(x,y);
    if(std::abs(p.red()-color.red())<45&&std::abs(p.green()-color.green())<45&&std::abs(p.blue()-color.blue())<45){++count;}}}return count;
}
}
class TrackerActivityHistoryTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
 void exactFloorOverflowUnknownAndObservedZeroAreDifferent(){
   using H=ui::TrackerActivityHistory;constexpr auto maximum=std::numeric_limits<std::int64_t>::max();
   QCOMPARE(H::rate(100,131,2000000000),std::optional<std::int64_t>{15});QCOMPARE(H::rate(0,maximum,1000000000),std::optional{maximum});
   QCOMPARE(H::rate(0,maximum,maximum),std::optional<std::int64_t>{1000000000});QCOMPARE(H::rate(0,maximum-1,maximum),std::optional<std::int64_t>{999999999});
   QVERIFY(!H::rate(0,maximum,1));QVERIFY(!H::rate(2,1,1000000000));QVERIFY(!H::rate(0,0,0));
   H history;QVERIFY(history.observe(*sample(1,1,0,0,10,20)->snapshot,true));QCOMPARE(history.frame().count,std::size_t{1});
   QCOMPARE(history.latest().queuedHttpAnnounces,std::optional<std::int64_t>{0});QVERIFY(!history.latest().receivedBytesPerSecond);
   QVERIFY(history.observe(*sample(2,2,1000000000,0,10,20)->snapshot,true));QCOMPARE(history.latest().receivedBytesPerSecond,std::optional<std::int64_t>{0});
   QCOMPARE(history.latest().sentBytesPerSecond,std::optional<std::int64_t>{0});QVERIFY(history.frame().points[1].queueConnected);QVERIFY(!history.frame().points[1].receivedConnected);
   H absent;QVERIFY(absent.observe(*sample(1,1,0,{}, {}, {})->snapshot,true));QVERIFY(!absent.latest().queuedHttpAnnounces);QVERIFY(!absent.latest().receivedBytes);
   QVERIFY(absent.observe(*sample(2,2,1000000000,0,0,0)->snapshot,true));QCOMPARE(absent.latest().queuedHttpAnnounces,std::optional<std::int64_t>{0});QVERIFY(!absent.latest().receivedBytesPerSecond);
 }
 void sourceResponseGapsRegressionRetentionAndNewEpochHaveActualFences(){
   ui::TrackerStatisticsView view;QVERIFY(view.beginSession(7));show(view);
   QVERIFY(view.setRuntimeMetrics(sample(1,1,0,7,100,200)));QVERIFY(view.setRuntimeMetrics(sample(2,2,1000000000,8,150,220)));
   QCOMPARE(view.activity().count,std::size_t{2});QVERIFY(child<QLabel>(view,"tracker-history-traffic").text().contains(QStringLiteral("50 / 20")));
   auto ended=sample(3,2,1000000000,8,150,220);ended->current=false;QVERIFY(view.setRuntimeMetrics(ended));QCOMPARE(view.activity().count,std::size_t{2});
   QVERIFY(child<QLabel>(view,"tracker-history-status").text().contains(QStringLiteral("Last observed")));
   QVERIFY(!view.setRuntimeMetrics(sample(4,2,1000000000,8,151,220)));QCOMPARE(view.activity().count,std::size_t{2}); // Same actual response cannot change.
   QVERIFY(view.setRuntimeMetrics(sample(4,4,2000000000,8,200,250)));const auto gap=view.activity();QCOMPARE(gap.count,std::size_t{3});QVERIFY(!gap.points[2].queueConnected);QVERIFY(!gap.points[2].receivedBytesPerSecond);
   QVERIFY(view.setRuntimeMetrics(sample(5,5,3000000000,9,20,260)));QVERIFY(!view.activity().points[3].queueConnected);QVERIFY(!view.activity().points[3].sentBytesPerSecond);
   QVERIFY(view.setRuntimeMetrics(sample(6,6,4000000000,9,30,270)));QCOMPARE(view.activity().points[4].receivedBytesPerSecond,std::optional<std::int64_t>{10});
   auto missing=sample(7,7,5000000000);missing->snapshot.reset();QVERIFY(view.setRuntimeMetrics(missing));QCOMPARE(view.activity().count,std::size_t{5});
   QVERIFY(child<QLabel>(view,"tracker-history-queue").text().contains(QStringLiteral("Unavailable")));
   QVERIFY(view.setRuntimeMetrics(sample(8,7,5000000000,0,30,270)));QVERIFY(!view.activity().points[5].queueConnected);QVERIFY(!view.activity().points[5].receivedBytesPerSecond);
   QVERIFY(view.setRuntimeMetrics(sample(9,8,6000000000,0,30,270)));QCOMPARE(view.activity().points[6].receivedBytesPerSecond,std::optional<std::int64_t>{0});
   QVERIFY(!view.setRuntimeMetrics(sample(10,9,1000000000)));QVERIFY(!view.setRuntimeMetrics(sample(8,8,6000000000)));
   auto bad=sample(10,9,7000000000);auto data=std::make_shared<core::RuntimeMetrics>(*bad->snapshot);data->metrics.push_back(data->metrics.front());bad->snapshot=data;QVERIFY(!view.setRuntimeMetrics(bad));
   QVERIFY(view.setRuntimeMetrics(sample(10,1,0,2,0,0,3)));QCOMPARE(view.activity().count,std::size_t{1});QVERIFY(!view.activity().points[0].receivedBytesPerSecond);
   QVERIFY(view.beginSession(8));QCOMPARE(view.activity().count,std::size_t{0});QVERIFY(!view.setRuntimeMetrics(sample(11,2,1000000000)));
 }
 void visibleRasterDrawsActualConnectionsButNoLineAcrossSkippedResponse(){
   ui::TrackerStatisticsView continuous,gap;QVERIFY(continuous.beginSession(7));QVERIFY(gap.beginSession(7));show(continuous);show(gap);
   QVERIFY(continuous.setRuntimeMetrics(sample(1,1,0,7,0,0)));QVERIFY(continuous.setRuntimeMetrics(sample(2,2,1000000000,7,50,20)));QVERIFY(continuous.setRuntimeMetrics(sample(3,3,2000000000,7,100,40)));
   QVERIFY(gap.setRuntimeMetrics(sample(1,1,0,7,0,0)));QVERIFY(gap.setRuntimeMetrics(sample(2,2,1000000000,7,50,20)));QVERIFY(gap.setRuntimeMetrics(sample(4,4,2000000000,7,100,40)));
   auto& a=child<QWidget>(continuous,"tracker-history-plot");auto& b=child<QWidget>(gap,"tracker-history-plot");drain();
   const auto good=a.grab().toImage();const auto broken=b.grab().toImage();QCOMPARE(good.size(),broken.size());
   const auto dpr=good.devicePixelRatio();const auto region=[&](double x,double y){return QRect(qRound((x-4)*dpr),qRound((y-3)*dpr),qRound(9*dpr),qRound(7*dpr));};
   // Interior of the last interval: far from markers, axes and text. Both
   // traces must be painted when connected and absent on a sequence gap.
   const double queueX=42+(a.width()/2.0-56)*0.75,trafficX=a.width()/2.0+42+(a.width()/2.0-56)*0.75;
   QVERIFY(colored(good,region(queueX,28),QColor(30,100,210))>0);QCOMPARE(colored(broken,region(queueX,28),QColor(30,100,210)),0);
   QVERIFY(colored(good,region(trafficX,28),QColor(30,150,80))>0);QCOMPARE(colored(broken,region(trafficX,28),QColor(30,150,80)),0);
   const auto directory=qEnvironmentVariable("BIGLYBTC_TRACKER_HISTORY_CAPTURE_DIRECTORY");if(!directory.isEmpty()){QVERIFY(QDir().mkpath(directory));QVERIFY(continuous.grab().save(directory+QStringLiteral("/tracker-history-policy-fixture.png")));}
 }
 void hiddenMinimizedSamplesRemainCachedAndHistoryNeverExceeds600(){
   ui::TrackerStatisticsView view;QVERIFY(view.beginSession(7));QVERIFY(view.setRuntimeMetrics(sample(1,1,0)));const auto before=child<QLabel>(view,"tracker-history-queue").text();
   QVERIFY(view.setRuntimeMetrics(sample(2,2,1000000000,12,100,20)));QCOMPARE(child<QLabel>(view,"tracker-history-queue").text(),before);show(view);
   QTRY_VERIFY(child<QLabel>(view,"tracker-history-queue").text().contains(QStringLiteral("12")));view.showMinimized();drain();QVERIFY(view.isMinimized());
   const auto previous=child<QLabel>(view,"tracker-history-queue").text();QVERIFY(view.setRuntimeMetrics(sample(3,3,2000000000,14,120,30)));QCOMPARE(child<QLabel>(view,"tracker-history-queue").text(),previous);
   view.showNormal();QTRY_VERIFY(!view.isMinimized());QTRY_VERIFY(child<QLabel>(view,"tracker-history-queue").text().contains(QStringLiteral("14")));
   view.hide();for(quint64 sequence=4;sequence<=650;++sequence){QVERIFY(view.setRuntimeMetrics(sample(sequence,sequence,static_cast<std::int64_t>(sequence)*1000000000,0,1000,2000)));}
   const auto frame=view.activity();QCOMPARE(frame.count,std::size_t{600});QCOMPARE(frame.points[0].sequence,std::uint64_t{51});QVERIFY(!frame.points[0].queueConnected);QCOMPARE(frame.points.back().sequence,std::uint64_t{650});
 }
 void tooltipReentryAndIndividualChildDeletionCannotOverwriteOrDereferenceOldFacts(){
   ui::TrackerStatisticsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setRuntimeMetrics(sample(1,1,0)));
   auto& traffic=child<QLabel>(view,"tracker-history-traffic");traffic.setToolTip({});Hook reentry;traffic.installEventFilter(&reentry);
   reentry.callback=[&]{QVERIFY(view.setRuntimeMetrics(sample(3,3,2000000000,88,100,40)));};
   QVERIFY(view.setRuntimeMetrics(sample(2,2,1000000000,22,50,20)));QTRY_VERIFY(child<QLabel>(view,"tracker-history-queue").text().contains(QStringLiteral("88")));QCOMPARE(view.activity().count,std::size_t{3});
   QPointer<QLabel> queue=&child<QLabel>(view,"tracker-history-queue");queue->setToolTip({});Hook remove;queue->installEventFilter(&remove);remove.callback=[queue]{delete queue.data();};
   QVERIFY(view.setRuntimeMetrics(sample(4,4,3000000000,33,150,60)));QVERIFY(queue.isNull());QVERIFY(view.setRuntimeMetrics(sample(5,5,4000000000,44,200,80)));
   QPointer<ui::TrackerStatisticsView> doomed=new ui::TrackerStatisticsView;QVERIFY(doomed->beginSession(7));show(*doomed);auto& label=child<QLabel>(*doomed,"tracker-history-queue");label.setToolTip({});Hook destroy;label.installEventFilter(&destroy);destroy.callback=[doomed]{delete doomed.data();};
   doomed->setRuntimeMetrics(sample(1,1,0));QVERIFY(doomed.isNull());
 }
 void tableFactsAndPageForwardingRemainIndependentAndDdbTabUsesScroll(){
   ui::TrackerStatisticsView view;QVERIFY(view.beginSession(7));show(view);const auto original=tablePacket();QVERIFY(view.setPresentation(original));
   const auto rows=child<QTableView>(view,"tracker-table").model();QCOMPARE(rows->rowCount(),1);QCOMPARE(rows->index(0,6).data().toString(),QStringLiteral("Yes"));QCOMPARE(rows->index(0,11).data(Qt::UserRole).toLongLong(),qlonglong{-10});
   QVERIFY(view.setRuntimeMetrics(sample(1,1,0)));QVERIFY(view.setRuntimeMetrics(sample(2,2,1000000000,2,100,30)));QVERIFY(view.presentation()==original);
   QCOMPARE(rows->rowCount(),1);QVERIFY(!rows->index(0,0).data().toString().contains(QStringLiteral("secret")));
   ui::StatisticsPage page;page.beginNativeSession(7);auto first=sample(1,1,0);first->tracker=original->snapshot;page.setRuntimeMetrics(first);auto second=sample(2,2,1000000000,3,100,30);second->tracker=original->snapshot;page.setRuntimeMetrics(second);
   auto& nested=child<ui::TrackerStatisticsView>(page,"tracker-statistics-view");QCOMPARE(nested.activity().count,std::size_t{2});QVERIFY(nested.presentation());
   auto& scroll=child<QScrollArea>(page,"statistics-ddb-operations-scroll");QVERIFY(scroll.widgetResizable());QCOMPARE(page.tabs().tabText(page.tabs().indexOf(&scroll)),QStringLiteral("DDB Operations"));QVERIFY(scroll.widget());
   page.tabs().setCurrentIndex(3);page.resize(1024,768);page.show();drain();
   QPointer<ui::TrackerStatisticsView> deleted=&nested;auto& label=child<QLabel>(nested,"tracker-history-queue");label.setToolTip({});Hook remove;label.installEventFilter(&remove);remove.callback=[deleted]{delete deleted.data();};
   page.setRuntimeMetrics(sample(3,3,2000000000,5,200,60));QVERIFY(deleted.isNull()); // Page survives a child deletion during forwarding.
 }
};
QTEST_MAIN(TrackerActivityHistoryTests)
#include "tracker_activity_history_tests.moc"
