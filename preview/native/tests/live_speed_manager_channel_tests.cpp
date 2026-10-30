#include "biglybtc/passive_speed_mapper.hpp"
#include "library_window.hpp"
#include "live_speed_manager_view.hpp"
#include "saved_speed_manager_view.hpp"
#include "statistics_page.hpp"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QTabWidget>
#include <QtTest>
#include <memory>
#include <stdexcept>

namespace {
using namespace biglybtc;
constexpr quint64 Session=73;
template<class T>T& child(QObject& owner,const char* name){
  auto* value=owner.findChild<T*>(QString::fromLatin1(name));
  if(!value){throw std::runtime_error("Missing live SpeedManager channel control");}
  return *value;
}
void drain(){
  for(int i=0;i<3;++i){QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::processEvents();}
}
void show(QWidget& widget){widget.resize(1024,768);widget.show();drain();}
void savedTotalsOwner(ui::LibraryWindow& window){
  QObject::connect(&window,&ui::LibraryWindow::statisticsRequested,&window,[&window](quint64 session,quint64 request){
    auto packet=std::make_shared<ui::StatisticsPacket>();packet->session=session;packet->request=request;
    packet->state=ui::StatisticsReadState::Missing;window.setStatistics(packet);
  });
}
ui::StatisticsPage& open(ui::LibraryWindow& window){
  child<QAction>(window,"classic-statistics").trigger();
  if(!window.statisticsPage()){throw std::runtime_error("Statistics did not open");}
  return *window.statisticsPage();
}
struct Samples{
  core::PassiveSpeedMapper mapper;
  Samples(){
    if(!mapper.observe({1000,4096,8192,15,false})||!mapper.observe({4000,8192,16384,20,false})){
      throw std::runtime_error("Qualified mapper fixture failed");
    }
  }
  std::shared_ptr<ui::LiveSpeedManagerPacket> packet(quint64 sequence=1,quint64 session=Session,quint64 epoch=1){
    auto actual=mapper.snapshot();if(!actual){throw std::runtime_error("Mapper snapshot failed");}
    auto value=std::make_shared<ui::LiveSpeedManagerPacket>();
    value->session=session;value->epoch=epoch;value->sequence=sequence;value->current=true;
    value->snapshot=std::make_shared<const core::PassiveSpeedSnapshot>(std::move(*actual));return value;
  }
};
}

class LiveSpeedManagerChannelTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void ownedMapperFactsSurviveClosedStatisticsAndProfileRebinding(){
    Samples samples;const auto first=samples.packet();
    ui::LibraryWindow window;savedTotalsOwner(window);show(window);
    window.setLiveSpeedManager(first);QVERIFY(!window.liveSpeedManagerSnapshot());
    window.beginColumnLayoutSession(Session);window.setLiveSpeedManager(first);
    QVERIFY(!window.statisticsPage());QVERIFY(window.liveSpeedManagerSnapshot()==first);
    auto& page=open(window);QVERIFY(page.liveSpeedManagerSnapshot()==first);
    QCOMPARE(page.tabs().count(),11);QCOMPARE(page.tabs().tabText(10),QStringLiteral("I2P"));page.tabs().setCurrentIndex(1);
    child<QTabWidget>(page,"statistics-transfer-tabs").setCurrentIndex(3);drain();
    auto& live=child<ui::LiveSpeedManagerView>(page,"live-speed-manager-view");
    QVERIFY(live.presentation()==first);
    page.setRuntimeMetrics({});QVERIFY(page.liveSpeedManagerSnapshot()==first);
    QPointer<ui::StatisticsPage> previous=&page;
    child<QAction>(window,"classic-close-tab").trigger();QVERIFY(!window.statisticsPage());
    auto retired=samples.packet(2);retired->current=false;window.setLiveSpeedManager(retired);
    window.setLiveSpeedManager(first);QVERIFY(window.liveSpeedManagerSnapshot()==retired);
    auto& reopened=open(window);QVERIFY(&reopened!=previous.data());
    QVERIFY(reopened.liveSpeedManagerSnapshot()==retired);
    QCOMPARE(reopened.tabs().currentIndex(),1);
    QCOMPARE(child<QTabWidget>(reopened,"statistics-transfer-tabs").currentIndex(),3);
    window.beginColumnLayoutSession(Session+1);drain();
    QVERIFY(!window.liveSpeedManagerSnapshot());QVERIFY(!reopened.liveSpeedManagerSnapshot());
    QVERIFY(!child<ui::LiveSpeedManagerView>(reopened,"live-speed-manager-view").presentation());
    window.setLiveSpeedManager(retired);QVERIFY(!window.liveSpeedManagerSnapshot());
    const auto next=samples.packet(1,Session+1);window.setLiveSpeedManager(next);
    QVERIFY(reopened.liveSpeedManagerSnapshot()==next);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(previous.isNull());
  }
  void staleOwnerEpochSequenceAndSessionCannotOverwriteCurrentMap(){
    Samples samples;ui::LibraryWindow window;savedTotalsOwner(window);
    window.beginColumnLayoutSession(Session);
    const auto first=samples.packet(5,Session,8);window.setLiveSpeedManager(first);
    const auto newer=samples.packet(6,Session,8);newer->current=false;
    window.setLiveSpeedManager(newer);QVERIFY(window.liveSpeedManagerSnapshot()==newer);
    window.setLiveSpeedManager(first);window.setLiveSpeedManager(samples.packet(7,Session,7));
    window.setLiveSpeedManager(samples.packet(7,Session+1,8));
    window.setLiveSpeedManager(samples.packet(0,Session,8));
    QVERIFY(window.liveSpeedManagerSnapshot()==newer);
    show(window);auto& page=open(window);QVERIFY(page.liveSpeedManagerSnapshot()==newer);
    page.setLiveSpeedManager(first);QVERIFY(page.liveSpeedManagerSnapshot()==newer);
    const auto nextOwner=samples.packet(7,Session,9);window.setLiveSpeedManager(nextOwner);
    QVERIFY(window.liveSpeedManagerSnapshot()==nextOwner);QVERIFY(page.liveSpeedManagerSnapshot()==nextOwner);
  }
};
QTEST_MAIN(LiveSpeedManagerChannelTests)
#include "live_speed_manager_channel_tests.moc"
