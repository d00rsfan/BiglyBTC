#include "runtime_metrics_view.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& parent, const char* id) {
  auto* result = parent.findChild<T*>(QString::fromLatin1(id));
  if (!result) { throw std::runtime_error("Missing runtime metrics control"); }
  return *result;
}
auto snapshot(std::uint64_t sequence = 1) {
  auto value = std::make_shared<core::RuntimeMetrics>(); value->sequence = sequence;
  value->ownerElapsedNanoseconds = static_cast<std::int64_t>(sequence) * 1000;
  value->metrics = {
      {"disk.queued",core::RuntimeMetricType::Gauge,core::RuntimeMetricGroup::Disk,10},
      {"tracker.requests",core::RuntimeMetricType::Counter,core::RuntimeMetricGroup::Tracker,2},
      {"peer.connected",core::RuntimeMetricType::Gauge,core::RuntimeMetricGroup::Peer,0},
      {"net.received",core::RuntimeMetricType::Counter,core::RuntimeMetricGroup::Net,123},
      {"future.metric",core::RuntimeMetricType::Gauge,core::RuntimeMetricGroup::Other,-7}};
  return value;
}
auto packet(quint64 sequence = 1, quint64 session = 7, quint64 epoch = 1) {
  auto value = std::make_shared<ui::RuntimeMetricsPacket>();
  value->session=session; value->epoch=epoch; value->sequence=sequence; value->snapshot=snapshot(sequence);
  return value;
}
QTableView& table(QWidget& view) { return child<QTableView>(view,"runtime-metrics-table"); }
QLabel& status(QWidget& view) { return child<QLabel>(view,"runtime-metrics-status"); }
QLineEdit& search(QWidget& view) { return child<QLineEdit>(view,"runtime-metrics-filter"); }
void show(QWidget& view) { view.resize(1100,700); view.show(); QCoreApplication::processEvents(); }
int rowFor(QWidget& view, const QString& name) {
  auto* model=table(view).model();
  for (int row=0;row<model->rowCount();++row) { if (model->index(row,0).data().toString()==name) { return row; } }
  return -1;
}
qint64 valueFor(QWidget& view, const QString& name) {
  const int row=rowFor(view,name); if(row<0) {throw std::runtime_error("Metric not displayed");}
  return table(view).model()->index(row,1).data(Qt::UserRole).toLongLong();
}
class Hook final : public QObject {
public:
  bool armed=true, consume=false;
  std::function<void()> callback;
  bool eventFilter(QObject*,QEvent* event) override {
    if(armed && event->type()==QEvent::ToolTipChange) {armed=false;callback();return consume;}
    return false;
  }
};
void totalsOwner(ui::LibraryWindow& window) {
  QObject::connect(&window,&ui::LibraryWindow::statisticsRequested,&window,[&window](quint64 session,quint64 request) {
    window.setBusy(true);
    QTimer::singleShot(0,&window,[&window,session,request] {
      auto value=std::make_shared<ui::StatisticsPacket>();value->session=session;value->request=request;
      value->state=ui::StatisticsReadState::Missing;window.setStatistics(value);window.setBusy(false);
    });
  });
}
ui::StatisticsPage& open(ui::LibraryWindow& window) {
  child<QAction>(window,"classic-statistics").trigger();
  if(!window.statisticsPage()) {throw std::runtime_error("Statistics did not open");}
  return *window.statisticsPage();
}
}

class RuntimeMetricsViewTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void unknownIsNotRecordedZeroAndTableHasExactThreeColumns() {
    ui::RuntimeMetricsView view;show(view);QVERIFY(view.beginSession(7));
    QCOMPARE(table(view).model()->rowCount(),0);QCOMPARE(table(view).model()->columnCount(),3);
    QVERIFY(status(view).text().contains(QStringLiteral("unavailable")));
    auto value=packet();QVERIFY(view.setPresentation(value));
    QTRY_COMPARE(table(view).model()->rowCount(),5);
    QCOMPARE(valueFor(view,QStringLiteral("peer.connected")),qint64{0});
    const int row=rowFor(view,QStringLiteral("peer.connected"));
    QCOMPARE(table(view).model()->index(row,1).data().toString(),QStringLiteral("0"));
    auto missing=packet(2);missing->snapshot.reset();QVERIFY(view.setPresentation(missing));
    QCOMPARE(table(view).model()->rowCount(),0);QVERIFY(status(view).text().contains(QStringLiteral("unavailable")));
  }
  void exactSignedIntegersUnknownNamesAndTypesAreDisplayed() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);
    auto value=packet();auto facts=snapshot();facts->metrics.push_back({"other.minimum",core::RuntimeMetricType::Gauge,core::RuntimeMetricGroup::Other,std::numeric_limits<std::int64_t>::min()});
    facts->metrics.push_back({"other.maximum",core::RuntimeMetricType::Counter,core::RuntimeMetricGroup::Other,std::numeric_limits<std::int64_t>::max()});
    value->snapshot=facts;QVERIFY(view.setPresentation(value));QCOMPARE(table(view).model()->rowCount(),7);
    QCOMPARE(valueFor(view,QStringLiteral("other.minimum")),std::numeric_limits<qint64>::min());
    QCOMPARE(valueFor(view,QStringLiteral("other.maximum")),std::numeric_limits<qint64>::max());
    QCOMPARE(valueFor(view,QStringLiteral("future.metric")),qint64{-7});
    const int row=rowFor(view,QStringLiteral("other.minimum"));
    QCOMPARE(table(view).model()->index(row,1).data().toString(),QStringLiteral("-9223372036854775808"));
    QCOMPARE(table(view).model()->index(row,2).data().toString(),QStringLiteral("Gauge"));
  }
  void groupFiltersUseExactClassificationAndSearchIsCaseInsensitive() {
    for (const auto group : {core::RuntimeMetricGroup::Disk,core::RuntimeMetricGroup::Tracker,core::RuntimeMetricGroup::Peer}) {
      ui::RuntimeMetricsView view(group);QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
      QCOMPARE(table(view).model()->rowCount(),1);
      const auto name=table(view).model()->index(0,0).data().toString();
      QVERIFY(name.startsWith(group==core::RuntimeMetricGroup::Disk?QStringLiteral("disk."):
          group==core::RuntimeMetricGroup::Tracker?QStringLiteral("tracker."):QStringLiteral("peer.")));
      search(view).setText(name.toUpper());QTRY_COMPARE(table(view).model()->rowCount(),1);
      search(view).setText(QStringLiteral("not-present"));QTRY_COMPARE(table(view).model()->rowCount(),0);
      QVERIFY(status(view).text().contains(QStringLiteral("no matching")));
    }
  }
  void numericSortDoesNotUseLexicalNumberOrder() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    table(view).sortByColumn(1,Qt::AscendingOrder);
    auto* model=table(view).model();
    QCOMPARE(model->index(0,1).data(Qt::UserRole).toLongLong(),qint64{-7});
    QCOMPARE(model->index(2,1).data(Qt::UserRole).toLongLong(),qint64{2});
    QCOMPARE(model->index(3,1).data(Qt::UserRole).toLongLong(),qint64{10});
    table(view).sortByColumn(1,Qt::DescendingOrder);QCOMPARE(model->index(0,1).data(Qt::UserRole).toLongLong(),qint64{123});
  }
  void refreshPreservesQuerySortAndSelectedExactName() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    search(view).setText(QStringLiteral("e"));QCoreApplication::processEvents();
    table(view).sortByColumn(1,Qt::DescendingOrder);
    table(view).setCurrentIndex(table(view).model()->index(rowFor(view,QStringLiteral("tracker.requests")),0));
    auto changed=packet(2);auto facts=snapshot(2);facts->metrics[1].value=999;changed->snapshot=facts;
    QVERIFY(view.setPresentation(changed));
    QCOMPARE(search(view).text(),QStringLiteral("e"));QCOMPARE(table(view).horizontalHeader()->sortIndicatorSection(),1);
    QCOMPARE(table(view).horizontalHeader()->sortIndicatorOrder(),Qt::DescendingOrder);
    QCOMPARE(table(view).currentIndex().data().toString(),QStringLiteral("tracker.requests"));
    QCOMPARE(valueFor(view,QStringLiteral("tracker.requests")),qint64{999});
  }
  void sessionAndEpochChangesClearFactsAndStalePacketsCannotRestoreThem() {
    ui::RuntimeMetricsView view;QVERIFY(!view.beginSession(0));QVERIFY(view.beginSession(7));show(view);
    QVERIFY(view.setPresentation(packet(4)));table(view).setCurrentIndex(table(view).model()->index(0,0));
    QVERIFY(!view.setPresentation(packet(3)));QVERIFY(!view.setPresentation(packet(9,8)));
    QVERIFY(view.setPresentation(packet(1,7,2)));QVERIFY(!table(view).currentIndex().isValid());
    QVERIFY(!view.setPresentation(packet(99,7,1)));QVERIFY(!view.beginSession(7));
    QVERIFY(view.beginSession(8));QCOMPARE(table(view).model()->rowCount(),0);
    QVERIFY(!view.setPresentation(packet(10,7,3)));QVERIFY(!view.beginSession(6));
    QVERIFY(view.setPresentation(packet(1,8)));QCOMPARE(table(view).model()->rowCount(),5);
  }
  void snapshotResponseSequenceMayLagOwnerPublicationButCannotExceedIt() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);
    auto value=packet(10);value->snapshot=snapshot(2);QVERIFY(view.setPresentation(value));
    auto repeated=packet(11);repeated->snapshot=value->snapshot;repeated->current=false;
    QVERIFY(view.setPresentation(repeated));QVERIFY(status(view).text().startsWith(QStringLiteral("Last observed")));
    auto bad=packet(12);bad->snapshot=snapshot(13);QVERIFY(!view.setPresentation(bad));
    auto regressed=packet(12);regressed->snapshot=snapshot(1);QVERIFY(!view.setPresentation(regressed));
    auto altered=packet(12);auto facts=snapshot(2);facts->metrics[0].value=999;altered->snapshot=facts;
    QVERIFY(!view.setPresentation(altered));QCOMPARE(valueFor(view,QStringLiteral("disk.queued")),qint64{10});
  }
  void unavailableSnapshotCannotResurrectAnOldResponse() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    auto missing=packet(2);missing->snapshot.reset();QVERIFY(view.setPresentation(missing));
    auto old=packet(3);old->snapshot=snapshot(1);QVERIFY(!view.setPresentation(old));
    auto fresh=packet(3);fresh->snapshot=snapshot(2);QVERIFY(view.setPresentation(fresh));
  }
  void malformedBoundsNamesTypesGroupsAndDatesDoNotReplaceAcceptedFacts() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    for(int kind=0;kind<9;++kind) {
      auto bad=packet(2);auto facts=snapshot(2);
      switch(kind) {
        case 0:facts->metrics[0].name.clear();break;
        case 1:facts->metrics[0].name.assign(257,'x');break;
        case 2:facts->metrics[0].name=std::string("a\0b",3);break;
        case 3:facts->metrics[0].name=std::string(1,static_cast<char>(0xff));break;
        case 4:facts->metrics[1]=facts->metrics[0];break;
        case 5:facts->metrics[0].type=static_cast<core::RuntimeMetricType>(99);break;
        case 6:facts->metrics[0].group=core::RuntimeMetricGroup::Peer;break;
        case 7:facts->ownerElapsedNanoseconds=-1;break;
        case 8:facts->sequence=0;break;
      }
      bad->snapshot=facts;QVERIFY(!view.setPresentation(bad));
      QCOMPARE(table(view).model()->rowCount(),5);QCOMPARE(view.presentation()->sequence,quint64{1});
    }
  }
  void fullCapsAcceptedAndOneExtraRecordRejected() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);
    auto value=packet();auto facts=snapshot();facts->metrics.clear();
    for(std::size_t i=0;i<core::MaximumRuntimeMetrics;++i) {
      facts->metrics.push_back({"other."+std::to_string(i),core::RuntimeMetricType::Counter,core::RuntimeMetricGroup::Other,static_cast<std::int64_t>(i)});
    }
    facts->metrics[0].name.assign(256,'n');value->snapshot=facts;QVERIFY(view.setPresentation(value));
    QCOMPARE(table(view).model()->rowCount(),1024);
    auto tooMany=packet(2);auto extra=std::make_shared<core::RuntimeMetrics>(*facts);extra->sequence=2;
    extra->metrics.push_back({"other.extra",core::RuntimeMetricType::Counter,core::RuntimeMetricGroup::Other,0});tooMany->snapshot=extra;
    QVERIFY(!view.setPresentation(tooMany));QCOMPARE(table(view).model()->rowCount(),1024);
  }
  void hiddenAndMinimizedViewsCacheWithoutChangingModelsOrLabels() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));auto first=packet();QVERIFY(view.setPresentation(first));
    QCOMPARE(table(view).model()->rowCount(),0);show(view);QTRY_COMPARE(table(view).model()->rowCount(),5);
    QSignalSpy resets(table(view).model(),&QAbstractItemModel::modelReset);
    view.hide();auto second=packet(2);second->current=false;const auto oldText=status(view).text();
    QVERIFY(view.setPresentation(second));QCoreApplication::processEvents();QCOMPARE(resets.count(),0);QCOMPARE(status(view).text(),oldText);
    view.show();QTRY_VERIFY(status(view).text().startsWith(QStringLiteral("Last observed")));
    view.showMinimized();QTRY_VERIFY(view.isMinimized());QCoreApplication::processEvents();resets.clear();
    const auto before=status(view).text();QVERIFY(view.setPresentation(packet(3)));QCoreApplication::processEvents();
    QCOMPARE(resets.count(),0);QCOMPARE(status(view).text(),before);
    view.showNormal();QTRY_VERIFY(!view.isMinimized());QTRY_VERIFY(status(view).text().startsWith(QStringLiteral("Current")));
  }
  void suppliedSnapshotIsOwnedAndPlainTextIsNeverInterpretedAsMarkup() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);
    auto value=packet();auto facts=snapshot();facts->metrics[4].name="<b>actual</b>";value->snapshot=facts;
    std::weak_ptr<const core::RuntimeMetrics> weak=facts;QVERIFY(view.setPresentation(value));facts.reset();value.reset();
    QVERIFY(!weak.expired());QVERIFY(rowFor(view,QStringLiteral("<b>actual</b>"))>=0);
    QCOMPARE(status(view).textFormat(),Qt::PlainText);QCOMPARE(valueFor(view,QStringLiteral("<b>actual</b>")),qint64{-7});
  }
  void statusCallbackCanDeleteOwningView() {
    QPointer<ui::RuntimeMetricsView> view=new ui::RuntimeMetricsView;
    QVERIFY(view->beginSession(7));show(*view);Hook hook;hook.consume=true;
    hook.callback=[&]{delete view.data();};status(*view).installEventFilter(&hook);
    view->setPresentation(packet());QVERIFY(!hook.armed);QVERIFY(view.isNull());
  }
  void statusCallbackCanDeleteIndividualTableWithoutStaleAccess() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);
    QPointer<QTableView> target=&table(view);Hook hook;hook.callback=[&]{delete target.data();};
    status(view).installEventFilter(&hook);QVERIFY(view.setPresentation(packet()));QVERIFY(!hook.armed);QVERIFY(!target);
    QVERIFY(view.setPresentation(packet(2)));QVERIFY(view.presentation()->sequence==2);
  }
  void statusCallbackRebindsToNewProfileAndLatestPacketWins() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);Hook hook;
    hook.callback=[&]{view.beginSession(8);view.setPresentation(packet(2,8));};
    status(view).installEventFilter(&hook);QVERIFY(view.setPresentation(packet()));QVERIFY(!hook.armed);
    QTRY_COMPARE(view.presentation()->session,quint64{8});QTRY_COMPARE(table(view).model()->rowCount(),5);
    QTRY_VERIFY(status(view).toolTip().contains(QStringLiteral("response 2")));
    QVERIFY(!view.setPresentation(packet(9,7)));
  }
  void modelResetCallbackQueuesLatestSnapshotWithoutNestedReset() {
    ui::RuntimeMetricsView view;QVERIFY(view.beginSession(7));show(view);bool armed=true;
    QObject::connect(table(view).model(),&QAbstractItemModel::modelReset,&view,[&] {
      if(!armed){return;}armed=false;view.setPresentation(packet(2));
    });
    QVERIFY(view.setPresentation(packet()));QVERIFY(!armed);QTRY_COMPARE(view.presentation()->sequence,quint64{2});
    QTRY_VERIFY(status(view).toolTip().contains(QStringLiteral("response 2")));
  }
  void pageRoutesRealGroupsAndRestoresNewNavigationChoices() {
    ui::StatisticsPage initial;const auto defaults=initial.navigation();QVERIFY(defaults);
    for(int section=2;section<=4;++section) {
      auto navigation=*defaults;navigation.section=section;navigation.transfers=2;
      QVERIFY(ui::validStatisticsNavigation(navigation));ui::StatisticsPage page(nullptr,navigation);
      page.beginNativeSession(7);show(page);page.setRuntimeMetrics(packet());
      QCOMPARE(page.tabs().currentIndex(),section);QCOMPARE(page.navigation()->transfers,2);
      const char* id=section==2?"statistics-cache-metrics":section==3?"statistics-tracker-metrics":"statistics-all-metrics";
      auto& view=child<ui::RuntimeMetricsView>(page,id);QTRY_COMPARE(table(view).model()->rowCount(),section==4?5:1);
    }
    ui::StatisticsPage connections;connections.beginNativeSession(7);show(connections);
    connections.tabs().setCurrentIndex(1);child<QTabWidget>(connections,"statistics-transfer-tabs").setCurrentIndex(2);
    connections.setRuntimeMetrics(packet());auto& view=child<ui::RuntimeMetricsView>(connections,"statistics-connections-metrics");
    QTRY_COMPARE(table(view).model()->rowCount(),1);QCOMPARE(valueFor(view,QStringLiteral("peer.connected")),qint64{0});
  }
  void windowCachesWhileClosedReopensFreshWidgetsAndNewProfileClearsMeasurements() {
    ui::LibraryWindow window;totalsOwner(window);window.beginColumnLayoutSession(7);QCoreApplication::processEvents();show(window);
    QSignalSpy reads(&window,&ui::LibraryWindow::statisticsRequested);
    window.setRuntimeMetrics(packet());QVERIFY(!window.statisticsPage());
    auto& page=open(window);QTRY_VERIFY(!page.loading());page.tabs().setCurrentIndex(4);
    auto& first=child<ui::RuntimeMetricsView>(page,"statistics-all-metrics");QTRY_COMPARE(table(first).model()->rowCount(),5);
    QPointer<ui::StatisticsPage> old=&page;child<QAction>(window,"classic-close-tab").trigger();QTRY_VERIFY(old.isNull());
    auto updated=packet(2);auto facts=snapshot(2);facts->metrics[0].value=77;updated->snapshot=facts;window.setRuntimeMetrics(updated);
    const int previousReads=reads.count();auto& reopened=open(window);QTRY_VERIFY(!reopened.loading());
    QCOMPARE(reads.count(),previousReads+1);QCOMPARE(reopened.tabs().currentIndex(),4);
    auto& next=child<ui::RuntimeMetricsView>(reopened,"statistics-all-metrics");QTRY_COMPARE(table(next).model()->rowCount(),5);
    QCOMPARE(valueFor(next,QStringLiteral("disk.queued")),qint64{77});
    child<QAction>(window,"classic-close-tab").trigger();QTRY_VERIFY(!window.statisticsPage());
    window.beginColumnLayoutSession(8);QCoreApplication::processEvents();auto& fresh=open(window);QTRY_VERIFY(!fresh.loading());
    fresh.tabs().setCurrentIndex(4);auto& clean=child<ui::RuntimeMetricsView>(fresh,"statistics-all-metrics");
    QTRY_COMPARE(table(clean).model()->rowCount(),0);window.setRuntimeMetrics(packet(9,7));QCOMPARE(table(clean).model()->rowCount(),0);
    window.setRuntimeMetrics(packet(1,8));QTRY_COMPARE(table(clean).model()->rowCount(),5);
  }
};

QTEST_MAIN(RuntimeMetricsViewTests)
#include "runtime_metrics_view_tests.moc"
