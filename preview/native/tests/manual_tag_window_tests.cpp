#include "library_window.hpp"
#include "statistics_page.hpp"
#include "manual_tag_view.hpp"
#include "manual_tag_presentation.hpp"
#include "biglybtc/saved_download_tags.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QLabel>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStringList>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace biglybtc;
constexpr quint64 Session = 73, LibraryRevision = 11, TagRevision = 13;
template<class T> T& child(QObject& owner, const char* name) {
  auto* value=owner.findChild<T*>(QString::fromLatin1(name));
  if(!value){throw std::runtime_error("Missing manual-tag navigation control");} return *value;
}
QAction& action(ui::LibraryWindow& w,const char* name){return child<QAction>(w,name);}
void drain() {
  // Drain the finite View -> Page -> Window queued dispatch chain. No clock,
  // sleep, timer, profile reader or provider is involved in this UI fixture.
  for(int i=0;i<3;++i){QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::processEvents();}
}
void show(ui::LibraryWindow& w){w.resize(1280,900);w.show();w.activateWindow();drain();}
ui::StatisticsPage& open(ui::LibraryWindow& w){
  action(w,"classic-statistics").trigger();if(!w.statisticsPage()){throw std::runtime_error("Statistics did not open");}
  return *w.statisticsPage();
}
void totalsOwner(ui::LibraryWindow& w) {
  // Explicit synthetic owner accepts synchronously, like Bridge::dispatch.
  // Missing saved totals is unrelated to the separately tested tag packet.
  QObject::connect(&w,&ui::LibraryWindow::statisticsRequested,&w,[&w](quint64 session,quint64 request){
    w.setBusy(true);auto packet=std::make_shared<ui::StatisticsPacket>();packet->session=session;packet->request=request;
    packet->state=ui::StatisticsReadState::Missing;w.setStatistics(packet);w.setBusy(false);
  });
}
core::TorrentHash hash() {
  std::string bytes;for(int i=0;i<20;++i){bytes.push_back(static_cast<char>(i));}
  auto value=core::TorrentHash::parse(bytes);if(!value){throw std::runtime_error("Generated hash rejected");}return *value;
}
std::string bstring(const std::string& text){return std::to_string(text.size())+':'+text;}
auto membership(quint64 session=Session,quint64 library=LibraryRevision,quint64 tags=TagRevision,
    const std::string& name="Alpha") {
  // Parsed original-format tag bytes + the real sealing factory, not a
  // fabricated membership object. c/vis=1 makes this explicitly visible.
  const std::string raw="d1:3d1:1d1:cd3:visi1ee1:n"+bstring(name)
      +"1:ol32:AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTeeee";
  auto parsed=core::readSavedDownloadTags(raw);if(!parsed){throw std::runtime_error("Generated tag document rejected");}
  const std::array libraryHashes{hash()};
  auto sealed=core::resolveManualTagMembership(session,library,tags,*parsed,libraryHashes);
  if(!sealed||(*sealed)->rows().size()!=1){throw std::runtime_error("Generated tag/library join rejected");}return *sealed;
}
auto ready(quint64 sequence=1,quint64 session=Session,quint64 epoch=3,
    quint64 library=LibraryRevision,quint64 tags=TagRevision,const std::string& name="Alpha") {
  // Positive fake provider packet, labelled synthetic: no engine/worker/live
  // provider, IO or Java equality credit. Only actual Window/View admission.
  auto p=std::make_shared<ui::ManualTagStatisticsPacket>();
  p->session=session;p->epoch=epoch;p->sequence=sequence;p->libraryRevision=library;p->tagRevision=tags;
  p->readState=ui::ManualTagReadState::Ready;p->membership=membership(session,library,tags,name);
  p->traffic={core::ManualTagTrafficState::Active,core::TargetTraffic{hash(),101,23,80,20},true,true};return p;
}
auto unavailable(ui::ManualTagReadState state,quint64 sequence,quint64 session=Session,quint64 epoch=3,
    quint64 library=LibraryRevision,quint64 tags=TagRevision) {
  auto p=std::make_shared<ui::ManualTagStatisticsPacket>();p->session=session;p->epoch=epoch;p->sequence=sequence;
  p->libraryRevision=library;p->tagRevision=tags;p->readState=state;
  if(state==ui::ManualTagReadState::Failed){p->failure=QStringLiteral("Synthetic read failure");}return p;
}
void rows(ui::LibraryWindow& w) {
  auto p=std::make_shared<ui::LibraryPresentation>();p->revision=LibraryRevision;
  p->items.push_back({.key=QStringLiteral("row-1"),.hash=QString::fromStdString(hash().hex()),
      .name=QStringLiteral("Alpha torrent"),.state=QStringLiteral("Queued"),.bytes=100,.completed=0,.canContinue=true});w.setPresentation(p);
}
void bind(ui::LibraryWindow& w,quint64 session=Session){w.beginColumnLayoutSession(session);drain();}
QTableView& tagsTable(ui::StatisticsPage& p){return child<QTableView>(p,"manual-tag-table");}
QPushButton& refresh(ui::StatisticsPage& p){return child<QPushButton>(p,"manual-tag-refresh");}
class TooltipHook final : public QObject {
public:
  bool armed=true;std::function<void()> callback;
  bool eventFilter(QObject*,QEvent* event) override {
    if(armed&&event->type()==QEvent::ToolTipChange){armed=false;callback();return true;}
    return false; // Consume the event when its watched widget was deleted.
  }
};
QString name(ui::StatisticsPage& p){return tagsTable(p).model()->index(0,0).data().toString();}
}

class ManualTagWindowTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);
  }
  void appendedRouteIsLazyAndCtrlOnePreservesLibraryTarget(){
    ui::LibraryWindow w;totalsOwner(w);rows(w);show(w);bind(w);w.filter().setText(QStringLiteral("Alpha torrent"));
    w.incompleteTable().selectRow(0);w.incompleteTable().setCurrentIndex(w.incompleteTable().model()->index(0,0));
    const QPersistentModelIndex selection(w.incompleteTable().currentIndex());const auto target=w.model().targetAt(0);QVERIFY(target);
    QWidget* const torrents=w.mainTabs().currentWidget();QSignalSpy reads(&w,&ui::LibraryWindow::manualTagStatisticsRequested);
    QSignalSpy libraryRefresh(&w,&ui::LibraryWindow::refreshRequested);auto& page=open(w);drain();
    QCOMPARE(page.tabs().count(),10);QCOMPARE(page.tabs().tabText(6),QStringLiteral("Country"));const QStringList expected{QStringLiteral("Activity"),QStringLiteral("Transfers"),
      QStringLiteral("Cache"),QStringLiteral("Tracker"),QStringLiteral("Counters"),QStringLiteral("Tags")};
    for(int i=0;i<expected.size();++i){QCOMPARE(page.tabs().tabText(i),expected[i]);}
    QCOMPARE(page.tabs().currentIndex(),0);QCOMPARE(child<QTabWidget>(page,"statistics-activity-views").currentIndex(),2);
    QCOMPARE(reads.count(),0);QVERIFY(!page.manualTagsSelected());
    auto packet=ready();w.setManualTagStatistics(packet);drain();QCOMPARE(reads.count(),0);
    QVERIFY(w.manualTagStatisticsSnapshot()==packet);QVERIFY(page.manualTagStatisticsSnapshot()==packet);
    page.tabs().setCurrentIndex(5);QTRY_COMPARE(reads.count(),1);QCOMPARE(reads.at(0).at(0).toULongLong(),Session);
    QTRY_COMPARE(tagsTable(page).model()->rowCount(),1);QCOMPARE(name(page),QStringLiteral("Alpha"));
    QCOMPARE(tagsTable(page).model()->index(0,3).data(Qt::UserRole).toLongLong(),qlonglong{101});
    auto& button=refresh(page);button.setFocus();QTest::keyClick(&button,Qt::Key_1,Qt::ControlModifier);
    QTRY_COMPARE(w.mainTabs().currentWidget(),torrents);QCOMPARE(w.incompleteTable().currentIndex(),QModelIndex(selection));
    QCOMPARE(w.model().targetAt(0),target);QCOMPARE(w.filter().text(),QStringLiteral("Alpha torrent"));QCOMPARE(libraryRefresh.count(),0);
  }
  void explicitBusyHiddenAndMinimizedReadIntentsCoalesceWithoutPolling(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);drain();
    QSignalSpy reads(&w,&ui::LibraryWindow::manualTagStatisticsRequested);w.setBusy(true);page.tabs().setCurrentIndex(5);
    refresh(page).click();refresh(page).click();refresh(page).click();drain();QCOMPARE(reads.count(),0);
    w.setBusy(false);QTRY_COMPARE(reads.count(),1);drain();QCOMPARE(reads.count(),1);
    w.setBusy(true);refresh(page).click();drain();page.tabs().setCurrentIndex(0);w.setBusy(false);drain();QCOMPARE(reads.count(),1);
    page.tabs().setCurrentIndex(5);QTRY_COMPARE(reads.count(),2);
    w.showMinimized();drain();QVERIFY(w.isMinimized());refresh(page).click();refresh(page).click();drain();QCOMPARE(reads.count(),2);
    auto packet=ready();w.setManualTagStatistics(packet);QVERIFY(w.manualTagStatisticsSnapshot()==packet);drain();QCOMPARE(reads.count(),2);
    QCOMPARE(tagsTable(page).model()->rowCount(),0); // Minimized accepts C++ cache only.
    w.showNormal();w.activateWindow();QTRY_VERIFY(!w.isMinimized());QTRY_COMPARE(reads.count(),3);
    QTRY_COMPARE(name(page),QStringLiteral("Alpha"));drain();QCOMPARE(reads.count(),3);
    auto next=std::make_shared<ui::ManualTagStatisticsPacket>(*packet);next->sequence=2;next->traffic.target->downloadRate=707;
    w.setManualTagStatistics(next);drain();QCOMPARE(reads.count(),3);
    QTRY_COMPARE(tagsTable(page).model()->index(0,3).data(Qt::UserRole).toLongLong(),qlonglong{707});
    refresh(page).click();QTRY_COMPARE(reads.count(),4);drain();QCOMPARE(reads.count(),4);
    QSignalSpy totals(&w,&ui::LibraryWindow::statisticsRequested);
    action(w,"classic-refresh").trigger();QTRY_COMPARE(reads.count(),5);drain();
    QCOMPARE(totals.count(),0); // F5/menu Refresh follows the selected Tags route.
  }
  void closeReopenRestoresTagsAndCacheButOldPageCannotRequest(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);page.tabs().setCurrentIndex(5);
    QSignalSpy reads(&w,&ui::LibraryWindow::manualTagStatisticsRequested);QTRY_COMPARE(reads.count(),1);
    auto packet=ready();w.setManualTagStatistics(packet);QTRY_COMPARE(name(page),QStringLiteral("Alpha"));
    QPointer<ui::StatisticsPage> old=&page;action(w,"classic-close-tab").trigger();QVERIFY(!w.statisticsPage());
    QVERIFY(w.manualTagStatisticsSnapshot()==packet);QVERIFY(old);
    auto cached=std::make_shared<ui::ManualTagStatisticsPacket>(*packet);cached->sequence=2;cached->traffic.target->downloadRate=909;
    w.setManualTagStatistics(cached);w.setManualTagStatistics(packet);QVERIFY(w.manualTagStatisticsSnapshot()==cached);
    emit old->manualTagStatisticsRequested(Session);drain();QCOMPARE(reads.count(),1);
    auto& replacement=open(w);QVERIFY(&replacement!=old.data());QCOMPARE(replacement.tabs().currentIndex(),5);
    QVERIFY(replacement.manualTagsSelected());QTRY_COMPARE(name(replacement),QStringLiteral("Alpha"));
    QVERIFY(replacement.manualTagStatisticsSnapshot()==cached);
    QTRY_COMPARE(tagsTable(replacement).model()->index(0,3).data(Qt::UserRole).toLongLong(),qlonglong{909});QTRY_COMPARE(reads.count(),2);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(old.isNull());
    QCOMPARE(reads.count(),2);QVERIFY(w.manualTagStatisticsSnapshot()==cached);
  }
  void everyReadStateKeepsEpochSequenceAndRevisionWatermarks(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);page.tabs().setCurrentIndex(5);drain();
    auto accepted=ready(10);w.setManualTagStatistics(accepted);QTRY_COMPARE(name(page),QStringLiteral("Alpha"));
    const auto reject=[&](ui::ManualTagStatisticsPresentation bad){w.setManualTagStatistics(std::move(bad));
      QVERIFY(w.manualTagStatisticsSnapshot()==accepted);QVERIFY(page.manualTagStatisticsSnapshot()==accepted);};
    reject({});auto wrong=ready(11,Session+1);reject(wrong);
    for(auto state:{ui::ManualTagReadState::Ready,ui::ManualTagReadState::Missing,ui::ManualTagReadState::Failed}){
      const auto candidate=[&]{
        if(state!=ui::ManualTagReadState::Ready){return unavailable(state,11);}
        auto p=std::make_shared<ui::ManualTagStatisticsPacket>(*accepted);p->sequence=11;return p;
      };
      auto oldSequence=candidate();oldSequence->sequence=10;reject(oldSequence);
      auto zeroSequence=candidate();zeroSequence->sequence=0;reject(zeroSequence);
      auto oldEpoch=candidate();oldEpoch->epoch=2;reject(oldEpoch);
      auto oldLibrary=state==ui::ManualTagReadState::Ready?ready(11,Session,3,LibraryRevision-1,TagRevision):candidate();
      oldLibrary->libraryRevision=LibraryRevision-1;reject(oldLibrary);
      auto oldTags=state==ui::ManualTagReadState::Ready?ready(11,Session,3,LibraryRevision,TagRevision-1):candidate();
      oldTags->tagRevision=TagRevision-1;reject(oldTags);
      auto zeroLibrary=candidate();zeroLibrary->libraryRevision=0;reject(zeroLibrary);
      auto zeroTags=candidate();zeroTags->tagRevision=0;reject(zeroTags);
    }
    reject(ready(11)); // Equal revision Ready must reuse the sealed pointer.
    auto nextEpoch=std::make_shared<ui::ManualTagStatisticsPacket>(*accepted);nextEpoch->sequence=11;nextEpoch->epoch=4;
    w.setManualTagStatistics(nextEpoch);QVERIFY(w.manualTagStatisticsSnapshot()==nextEpoch);
    auto failed=unavailable(ui::ManualTagReadState::Failed,12,Session,4);w.setManualTagStatistics(failed);
    QVERIFY(w.manualTagStatisticsSnapshot()==failed);QTRY_COMPARE(tagsTable(page).model()->rowCount(),0);
    auto restored=std::make_shared<ui::ManualTagStatisticsPacket>(*accepted);restored->sequence=13;restored->epoch=4;w.setManualTagStatistics(restored);
    QTRY_COMPARE(name(page),QStringLiteral("Alpha"));
    auto missing=unavailable(ui::ManualTagReadState::Missing,14,Session,4,LibraryRevision+1,TagRevision+1);
    w.setManualTagStatistics(missing);QVERIFY(w.manualTagStatisticsSnapshot()==missing);QTRY_COMPARE(tagsTable(page).model()->rowCount(),0);
    auto epochZero=unavailable(ui::ManualTagReadState::Failed,15,Session,0,LibraryRevision+1,TagRevision+1);
    w.setManualTagStatistics(epochZero);QVERIFY(w.manualTagStatisticsSnapshot()==missing);
  }
  void newProfileClearsCacheAndClosedNavigationBeforeAnyNewPacket(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);page.tabs().setCurrentIndex(5);drain();
    auto old=ready();w.setManualTagStatistics(old);QTRY_COMPARE(name(page),QStringLiteral("Alpha"));
    w.beginColumnLayoutSession(0);w.beginColumnLayoutSession(Session);w.beginColumnLayoutSession(Session-1);
    QVERIFY(w.manualTagStatisticsSnapshot()==old);action(w,"classic-close-tab").trigger();QVERIFY(!w.statisticsPage());
    w.beginColumnLayoutSession(Session+1);QVERIFY(!w.manualTagStatisticsSnapshot());auto& fresh=open(w);
    QCOMPARE(fresh.tabs().currentIndex(),0);QVERIFY(!fresh.manualTagStatisticsSnapshot());
    QSignalSpy reads(&w,&ui::LibraryWindow::manualTagStatisticsRequested);drain();QCOMPARE(reads.count(),0);
    w.setManualTagStatistics(old);QVERIFY(!w.manualTagStatisticsSnapshot());fresh.tabs().setCurrentIndex(5);
    QTRY_COMPARE(reads.count(),1);QCOMPARE(reads.at(0).at(0).toULongLong(),Session+1);
    QTRY_COMPARE(tagsTable(fresh).model()->rowCount(),0);
    auto current=ready(1,Session+1,1,1,1,"Beta");w.setManualTagStatistics(current);QTRY_COMPARE(name(fresh),QStringLiteral("Beta"));
    w.setManualTagStatistics(old);drain();QCOMPARE(name(fresh),QStringLiteral("Beta"));QVERIFY(w.manualTagStatisticsSnapshot()==current);
  }
  void resultCallbackCanRebindProfileWithoutRestoringOldTagMetadata(){
    ui::LibraryWindow w;totalsOwner(w);show(w);bind(w);auto& page=open(w);page.tabs().setCurrentIndex(5);drain();
    bool rebound=false;auto* model=tagsTable(page).model();
    const auto connection=QObject::connect(model,&QAbstractItemModel::modelReset,&w,[&]{
      if(!rebound){rebound=true;w.beginColumnLayoutSession(Session+1);}
    });
    w.setManualTagStatistics(ready());QVERIFY(rebound);QObject::disconnect(connection);
    QVERIFY(!w.manualTagStatisticsSnapshot());QTRY_COMPARE(tagsTable(page).model()->rowCount(),0);
    QVERIFY(!page.manualTagStatisticsSnapshot());
    auto current=ready(1,Session+1,1,1,1,"Beta");w.setManualTagStatistics(current);
    QTRY_COMPARE(name(page),QStringLiteral("Beta"));QVERIFY(w.manualTagStatisticsSnapshot()==current);
  }
  void queuedReadOrResultCallbackMayDeleteTheWindow_data(){
    QTest::addColumn<bool>("duringRead");QTest::newRow("queued-read-signal")<<true;QTest::newRow("accepted-tooltip-change")<<false;
  }
  void queuedReadOrResultCallbackMayDeleteTheWindow(){
    QFETCH(bool,duringRead);QPointer<ui::LibraryWindow> w=new ui::LibraryWindow;totalsOwner(*w);show(*w);bind(*w);
    auto& page=open(*w);QPointer<ui::StatisticsPage> old=&page;
    if(duringRead){QObject::connect(w,&ui::LibraryWindow::manualTagStatisticsRequested,w,[w](quint64){delete w.data();});
      page.tabs().setCurrentIndex(5);QTRY_VERIFY(w.isNull());
    }else{page.tabs().setCurrentIndex(5);drain();
      TooltipHook remove;remove.callback=[w]{delete w.data();};
      auto& status=child<QLabel>(page,"manual-tag-status");status.setToolTip({});status.installEventFilter(&remove);
      w->setManualTagStatistics(ready());QVERIFY(!remove.armed);QVERIFY(w.isNull());
    }
    QVERIFY(old.isNull()); // Never dereference the former owner/page afterward.
  }
};
QTEST_MAIN(ManualTagWindowTests)
#include "manual_tag_window_tests.moc"
