#include "cache_statistics_view.hpp"
#include <QAbstractItemModel>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
using namespace biglybtc;using I=std::int64_t;using F=core::DiskStatistic;
core::RuntimeMetrics metrics(std::uint64_t sequence=1,I time=0,I read=10,I write=20){
  core::RuntimeMetrics result;result.sequence=sequence;result.ownerElapsedNanoseconds=time;
  const auto add=[&](const char* name,core::RuntimeMetricType type,I value){result.metrics.push_back({name,type,core::RuntimeMetricGroup::Disk,value});};
  using T=core::RuntimeMetricType;
  add("disk.queued_disk_jobs",T::Gauge,2);add("disk.num_running_disk_jobs",T::Gauge,1);add("disk.blocked_disk_jobs",T::Gauge,0);
  add("disk.disk_blocks_in_use",T::Gauge,4);add("disk.file_pool_size",T::Gauge,3);add("disk.queued_write_bytes",T::Gauge,12345);
  add("disk.num_read_ops",T::Counter,read);add("disk.num_write_ops",T::Counter,write);
  add("disk.num_blocks_read",T::Counter,17);add("disk.num_blocks_written",T::Counter,29);
  add("disk.file_pool_hits",T::Counter,100);add("disk.file_pool_misses",T::Counter,5);
  result.metrics.push_back({"net.recv_bytes",T::Counter,core::RuntimeMetricGroup::Net,99999});return result;
}
auto packet(quint64 sequence=1,I time=0,I read=10,I write=20){
  auto p=std::make_shared<ui::RuntimeMetricsPacket>();p->session=7;p->epoch=3;p->sequence=sequence;
  p->snapshot=std::make_shared<const core::RuntimeMetrics>(metrics(sequence,time,read,write));return p;
}
template<class T>T& child(QObject& object,const char* id){auto* p=object.findChild<T*>(QString::fromLatin1(id));if(!p){throw std::runtime_error("Missing Cache view child");}return *p;}
void drain(){for(int i=0;i<3;++i){QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::processEvents();}}
void show(ui::CacheStatisticsView& view){view.resize(1250,780);view.show();drain();}
class TooltipHook final:public QObject {
public:std::function<void()> callback;
  bool eventFilter(QObject* object,QEvent* event)override{QPointer<QObject> watched(object);if(event->type()==QEvent::ToolTipChange&&callback){auto once=std::move(callback);once();}return watched.isNull();}
};
}
class CacheStatisticsViewTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void pureReducerUsesExactIntervalsAndDoesNotInventBytesOrInitialZero(){
    core::DiskStatisticsReducer reducer;auto a=reducer.observe(metrics());QVERIFY(a);QVERIFY(!a->readOperationsPerSecond);QCOMPARE(a->get(F::QueuedWriteBytes),std::optional<I>{12345});
    auto b=reducer.observe(metrics(2,500000000,13,21));QVERIFY(b);QCOMPARE(b->readOperationsPerSecond,std::optional<I>{6});QCOMPARE(b->writeOperationsPerSecond,std::optional<I>{2});
    QCOMPARE(b->get(F::ReadBlocks),std::optional<I>{17});QCOMPARE(b->get(F::WriteBlocks),std::optional<I>{29});
    auto zero=reducer.observe(metrics(3,1500000000,13,21));QVERIFY(zero);QCOMPARE(zero->readOperationsPerSecond,std::optional<I>{0});QCOMPARE(zero->writeOperationsPerSecond,std::optional<I>{0});
    auto repeated=reducer.observe(metrics(3,1500000000,13,21));QVERIFY(repeated);QVERIFY(!repeated->fresh);
  }
  void pureUnknownWrongTypeAndRegressionRebaselineIndependently(){
    core::DiskStatisticsReducer reducer;QVERIFY(reducer.observe(metrics()));auto source=metrics(2,1000000000,9,22); // Actual read counter regressed.
    auto value=reducer.observe(source);QVERIFY(value);QVERIFY(!value->readOperationsPerSecond);QCOMPARE(value->writeOperationsPerSecond,std::optional<I>{2});
    source=metrics(3,2000000000,12,23);source.metrics[6].type=core::RuntimeMetricType::Gauge;source.metrics[5].value=-1;
    value=reducer.observe(source);QVERIFY(value);QVERIFY(!value->get(F::ReadOperations));QVERIFY(!value->get(F::QueuedWriteBytes));QVERIFY(!value->readOperationsPerSecond);
    value=reducer.observe(metrics(4,3000000000,15,24));QVERIFY(value);QVERIFY(!value->readOperationsPerSecond);
    value=reducer.observe(metrics(5,4000000000,17,25));QVERIFY(value);QCOMPARE(value->readOperationsPerSecond,std::optional<I>{2});
  }
  void pureExtremeFloorOverflowAndFailureDoNotCorruptBaseline(){
    core::DiskStatisticsReducer reducer;QVERIFY(reducer.observe(metrics(1,0,0,0)));
    auto value=reducer.observe(metrics(2,3,1,0));QVERIFY(value);QCOMPARE(value->readOperationsPerSecond,std::optional<I>{333333333});
    auto duplicate=metrics(3,4,2,0);duplicate.metrics.push_back(duplicate.metrics[0]);QVERIFY(!reducer.observe(duplicate));
    value=reducer.observe(metrics(3,1000000003,2,0));QVERIFY(value);QCOMPARE(value->readOperationsPerSecond,std::optional<I>{1});
    reducer={};QVERIFY(reducer.observe(metrics(1,0,0,0)));value=reducer.observe(metrics(2,1,std::numeric_limits<I>::max(),0));
    QVERIFY(value);QVERIFY(!value->readOperationsPerSecond);QCOMPARE(value->get(F::ReadOperations),std::optional<I>{std::numeric_limits<I>::max()});
    reducer={};QVERIFY(reducer.observe(metrics(1,0,0,0)));value=reducer.observe(metrics(2,std::numeric_limits<I>::max(),std::numeric_limits<I>::max(),0));
    QVERIFY(value);QCOMPARE(value->readOperationsPerSecond,std::optional<I>{1000000000});
    reducer.gap();QVERIFY(!reducer.observe(metrics(1,0,0,0))); // Gap does not erase stale watermark.
  }
  void pureHistoryBoundsGapsDedupAndKnownZero(){
    core::DiskStatisticsReducer reducer;core::DiskActivityHistory history;
    auto first=reducer.observe(metrics(1,0,0,0));QVERIFY(first);history.observe(*first);QCOMPARE(history.snapshot().count,std::size_t{0});
    for(std::uint64_t i=2;i<605;++i){auto value=reducer.observe(metrics(i,static_cast<I>(i)*1000000000,0,0));QVERIFY(value);history.observe(*value);}
    auto frame=history.snapshot();QCOMPARE(frame.count,std::size_t{600});QVERIFY(!frame.points[0].connectedToPrevious);
    QCOMPARE(frame.points[599].readOperationsPerSecond,std::optional<I>{0});QVERIFY(frame.points[599].connectedToPrevious);
    const auto duplicate=reducer.observe(metrics(604,604000000000,0,0));QVERIFY(duplicate);history.observe(*duplicate);QCOMPARE(history.snapshot().count,std::size_t{600});
    reducer.gap();history.gap();auto after=reducer.observe(metrics(605,605000000000,0,0));QVERIFY(after);history.observe(*after);
    after=reducer.observe(metrics(606,606000000000,1,1));QVERIFY(after);history.observe(*after);frame=history.snapshot();QVERIFY(!frame.points[599].connectedToPrevious);
  }
  void summaryRatesAndRawNativeCountersAreBothAvailable(){
    ui::CacheStatisticsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    QCOMPARE(view.activity().count,std::size_t{0});QVERIFY(child<QLabel>(view,"cache-read-operations-rate").text().contains(QStringLiteral("Unavailable")));
    QVERIFY(view.setPresentation(packet(2,500000000,13,21)));QCOMPARE(child<QLabel>(view,"cache-read-operations-rate").text(),QStringLiteral("Read operations/s: 6"));
    QCOMPARE(child<QLabel>(view,"cache-write-operations-rate").text(),QStringLiteral("Write operations/s: 2"));QCOMPARE(view.activity().count,std::size_t{1});
    QCOMPARE(child<QLabel>(view,"cache-queued-write-bytes").text(),QStringLiteral("Queued write bytes: 12345"));
    QVERIFY(child<QLabel>(view,"cache-scope").text().contains(QStringLiteral("This engine does not report cache capacity or cache-hit ratios.")));
    auto& tabs=child<QTabWidget>(view,"cache-tabs");QCOMPARE(tabs.count(),2);QCOMPARE(tabs.tabText(1),QStringLiteral("Native counters"));tabs.setCurrentIndex(1);
    auto& raw=child<QTableView>(view,"runtime-metrics-table");QTRY_COMPARE(raw.model()->rowCount(),12);
    bool exact=false;for(int row=0;row<raw.model()->rowCount();++row){if(raw.model()->index(row,0).data().toString()==QStringLiteral("disk.num_read_ops")){
      QCOMPARE(raw.model()->index(row,1).data(Qt::UserRole).toLongLong(),qlonglong{13});exact=true;}}QVERIFY(exact);
  }
  void hiddenMinimizedAndTerminalDuplicateDoNotInventObservations(){
    ui::CacheStatisticsView view;QVERIFY(view.beginSession(7));QVERIFY(view.setPresentation(packet()));QVERIFY(view.setPresentation(packet(2,1000000000,12,22)));
    QCOMPARE(view.activity().count,std::size_t{1});QVERIFY(child<QLabel>(view,"cache-read-operations-rate").text().contains(QStringLiteral("Unavailable")));show(view);
    QTRY_COMPARE(child<QLabel>(view,"cache-read-operations-rate").text(),QStringLiteral("Read operations/s: 2"));
    view.showMinimized();drain();QVERIFY(view.isMinimized());auto third=packet(3,2000000000,17,23);QVERIFY(view.setPresentation(third));
    QCOMPARE(view.activity().count,std::size_t{2});QCOMPARE(child<QLabel>(view,"cache-read-operations-rate").text(),QStringLiteral("Read operations/s: 2"));
    view.showNormal();QTRY_VERIFY(!view.isMinimized());QTRY_COMPARE(child<QLabel>(view,"cache-read-operations-rate").text(),QStringLiteral("Read operations/s: 5"));
    auto ended=std::make_shared<ui::RuntimeMetricsPacket>(*third);ended->sequence=4;ended->current=false;QVERIFY(view.setPresentation(ended));
    QCOMPARE(view.activity().count,std::size_t{2});QVERIFY(child<QLabel>(view,"cache-status").text().contains(QStringLiteral("Last observed")));
    QVERIFY(view.beginSession(8));QCOMPARE(view.activity().count,std::size_t{0});QVERIFY(!view.setPresentation(packet(5,3000000000,20,24)));
  }
  void rawModelRebindCannotRestoreOldSummary(){
    ui::CacheStatisticsView view;QVERIFY(view.beginSession(7));show(view);auto& tabs=child<QTabWidget>(view,"cache-tabs");tabs.setCurrentIndex(1);drain();
    auto& table=child<QTableView>(view,"runtime-metrics-table");bool once=false;
    const auto c=QObject::connect(table.model(),&QAbstractItemModel::modelReset,&view,[&]{if(!once){once=true;view.beginSession(8);}});
    QVERIFY(view.setPresentation(packet()));QVERIFY(once);QObject::disconnect(c);QVERIFY(!view.presentation());QCOMPARE(view.activity().count,std::size_t{0});
    tabs.setCurrentIndex(0);QTRY_VERIFY(child<QLabel>(view,"cache-status").text().contains(QStringLiteral("unavailable")));
  }
  void individualFactDeletionAndOwnerDeletionAreGuarded_data(){QTest::addColumn<bool>("owner");QTest::newRow("individual-label")<<false;QTest::newRow("owner")<<true;}
  void individualFactDeletionAndOwnerDeletionAreGuarded(){
    QFETCH(bool,owner);QPointer<ui::CacheStatisticsView> view=new ui::CacheStatisticsView;QVERIFY(view->beginSession(7));show(*view);
    QPointer<QLabel> target=&child<QLabel>(*view,"cache-write-operations-rate");TooltipHook hook;target->setToolTip({});
    hook.callback=[view,target,owner]{if(owner){delete view.data();}else{delete target.data();}};target->installEventFilter(&hook);
    view->setPresentation(packet());QVERIFY(target.isNull());
    if(owner){QVERIFY(view.isNull());}else{QVERIFY(view);QVERIFY(view->setPresentation(packet(2,1000000000,12,23)));delete view.data();}
  }
};
QTEST_MAIN(CacheStatisticsViewTests)
#include "cache_statistics_view_tests.moc"
