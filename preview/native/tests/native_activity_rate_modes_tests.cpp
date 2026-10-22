#include "native_activity_history.hpp"
#include "native_activity_rates.hpp"
#include "native_activity_view.hpp"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QtTest>

#include <array>
#include <cstdlib>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
using namespace biglybtc;
using Mode=ui::NativeActivityRateMode;
template<class T>T& child(QObject& parent,const char* id){auto* result=parent.findChild<T*>(QString::fromLatin1(id));if(!result){throw std::runtime_error("Missing activity rate control");}return *result;}
auto packet(quint64 sequence=1,quint64 session=7){
  auto value=std::make_shared<ui::NativeSessionPacket>();value->session=session;value->applicationRun=3;value->epoch=1;value->sequence=sequence;
  value->state=ui::NativeSessionState::Active;value->ownerElapsedNanoseconds=static_cast<qint64>(sequence)*1000000000;
  value->sample=core::EngineStatistics{{static_cast<qint64>(sequence)*1000,static_cast<qint64>(sequence)*800,static_cast<qint64>(sequence)*600,static_cast<qint64>(sequence)*500},
      value->ownerElapsedNanoseconds,core::EngineRates{300,250,200,150,100,100}};
  value->sourceRates=core::SourceRateValues{60,40,30,20,400,350};return value;
}
ui::NativeActivityPresentation frame(ui::NativeSessionPresentation packet){ui::NativeActivityHistory history;if(!history.observe(packet,packet->session)){throw std::runtime_error("Invalid activity fixture");}return history.snapshot();}
void show(QWidget& view){auto font=view.font();font.setStyleStrategy(QFont::NoSubpixelAntialias);view.setFont(font);view.resize(1150,900);view.show();QCoreApplication::processEvents();}
QComboBox& choice(QWidget& view){return child<QComboBox>(view,"native-activity-rate-mode");}
QLabel& selected(QWidget& view){return child<QLabel>(view,"native-activity-selected-rates");}
class Hook final:public QObject{
public:bool armed=true;std::function<void()> callback;
  bool eventFilter(QObject*,QEvent* event)override{if(armed && event->type()==QEvent::ToolTipChange){armed=false;callback();return true;}return false;}
};
int middleColor(const QImage& image,QColor target){
  int count=0;for(int y=26;y<image.height()-35;++y){for(int x=image.width()*2/5;x<image.width()*3/5;++x){const auto actual=image.pixelColor(x,y);
    if(std::abs(actual.red()-target.red())<30 && std::abs(actual.green()-target.green())<30 && std::abs(actual.blue()-target.blue())<30){++count;}}}return count;
}
}
class NativeActivityRateModesTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void pureProjectionSeparatesSourceIntervalAndSmoothedBothDirections(){
    const auto input=packet();for(const bool sending:{false,true}){
      const auto source=ui::nativeActivityRates(input->sample->rates,input->sourceRates,Mode::Source10s,sending);
      QVERIFY(source==ui::NativeActivitySeries(sending?ui::NativeActivitySeries{50,30,20}:ui::NativeActivitySeries{100,60,40}));
      const auto raw=ui::nativeActivityRates(input->sample->rates,input->sourceRates,Mode::Interval,sending);
      QVERIFY(raw==ui::NativeActivitySeries(sending?ui::NativeActivitySeries{250,150,100}:ui::NativeActivitySeries{300,200,100}));
      const auto smooth=ui::nativeActivityRates(input->sample->rates,input->sourceRates,Mode::Smoothed,sending);
      QVERIFY(smooth[0]==(sending?350:400));QVERIFY(!smooth[1] && !smooth[2]);
    }
  }
  void missingSourceNeverFallsBackAndZeroStaysKnown(){
    const auto raw=packet()->sample->rates;const auto absent=ui::nativeActivityRates(raw,{},Mode::Source10s,false);
    QVERIFY(!absent[0] && !absent[1] && !absent[2]);const auto smooth=ui::nativeActivityRates(raw,{},Mode::Smoothed,true);QVERIFY(!smooth[0]);
    const auto zero=ui::nativeActivityRates(raw,core::SourceRateValues{},Mode::Source10s,false);for(const auto& value:zero){QVERIFY(value && *value==0);}
    const auto invalid=ui::nativeActivityRates(raw,core::SourceRateValues{},static_cast<Mode>(99),false);QVERIFY(!invalid[0] && !invalid[1] && !invalid[2]);
  }
  void checkedSumOverflowAndNegativeComponentsDoNotBecomeGraphValues(){
    auto source=core::SourceRateValues{std::numeric_limits<qint64>::max(),1,-1,20,-1,std::numeric_limits<qint64>::max()};
    const auto receive=ui::nativeActivityRates({},source,Mode::Source10s,false);QVERIFY(!receive[0]);QVERIFY(receive[1]==std::numeric_limits<qint64>::max());QVERIFY(receive[2]==1);
    const auto send=ui::nativeActivityRates({},source,Mode::Source10s,true);QVERIFY(!send[0] && !send[1]);QVERIFY(send[2]==20);
    QVERIFY(!ui::nativeActivityRates({},source,Mode::Smoothed,false)[0]);QVERIFY(ui::nativeActivityRates({},source,Mode::Smoothed,true)[0]==std::numeric_limits<qint64>::max());
  }
  void sourceHistoryCopiesValuesRetainsRawGapsAndCapsAt600(){
    ui::NativeActivityHistory history;auto first=packet();QVERIFY(history.observe(first,7));const auto frozen=history.snapshot();
    first->sourceRates->payloadReceived=999;QVERIFY(frozen->points()[0].sourceRates->payloadReceived==60);
    for(quint64 sequence=2;sequence<=605;++sequence){auto input=packet(sequence);if(sequence==604){input->sourceRates.reset();}QVERIFY(history.observe(input,7));}
    const auto latest=history.snapshot();QCOMPARE(latest->points().size(),std::size_t{600});QVERIFY(!latest->points().front().connectedToPrevious);
    QCOMPARE(latest->points().front().elapsedNanoseconds,qint64{6000000000});QVERIFY(!latest->points()[598].sourceRates);QVERIFY(latest->points()[599].sourceRates);
    QCOMPARE(latest->points()[599].rates.received,qint64{300});QVERIFY(frozen->points()[0].sourceRates->payloadReceived==60);
    auto next=packet(606);next->epoch=2;QVERIFY(history.observe(next,7));QCOMPARE(history.snapshot()->points().size(),std::size_t{1});
  }
  void freshSourceDefaultAndQueuedChoicesUseSameImmutableFrame(){
    ui::NativeActivityView view;view.beginSession(7);show(view);const auto input=frame(packet());QVERIFY(view.setPresentation(input));
    QVERIFY(view.rateMode()==Mode::Source10s);QCOMPARE(choice(view).currentIndex(),0);
    QVERIFY(selected(view).text().contains("100 / 50"));QCOMPARE(child<QLabel>(view,"native-activity-rate-received").text(),QStringLiteral("300 bytes/s"));
    choice(view).setCurrentIndex(1);QVERIFY(view.rateMode()==Mode::Interval);QTRY_VERIFY(selected(view).text().contains("300 / 250"));
    QVERIFY(view.presentation()==input);choice(view).setCurrentIndex(2);QTRY_VERIFY(selected(view).text().contains("400 / 350"));
    QVERIFY(!selected(view).text().contains("payload"));QVERIFY(selected(view).toolTip().contains("30 seconds"));QVERIFY(view.presentation()==input);
    QVERIFY(view.findChildren<QTimer*>().isEmpty());
  }
  void absentSourceClearsSelectedValuesWithoutChangingIntervalFacts(){
    ui::NativeActivityView view;view.beginSession(7);show(view);auto input=packet();input->sourceRates.reset();QVERIFY(view.setPresentation(frame(input)));
    QVERIFY(selected(view).text().contains("Unavailable / Unavailable"));QCOMPARE(child<QLabel>(view,"native-activity-rate-received").text(),QStringLiteral("300 bytes/s"));
    choice(view).setCurrentIndex(1);QTRY_VERIFY(selected(view).text().contains("300 / 250"));choice(view).setCurrentIndex(0);QTRY_VERIFY(selected(view).text().contains("Unavailable / Unavailable"));
  }
  void hiddenAndMinimizedModeChangesRenderOnlyLatestWhenRestored(){
    ui::NativeActivityView view;view.beginSession(7);show(view);ui::NativeActivityHistory history;QVERIFY(history.observe(packet(),7));QVERIFY(view.setPresentation(history.snapshot()));
    const auto text=selected(view).text();view.hide();choice(view).setCurrentIndex(2);QCoreApplication::processEvents();QCOMPARE(selected(view).text(),text);
    show(view);QTRY_VERIFY(selected(view).text().contains("400 / 350"));view.showMinimized();QTRY_VERIFY(view.isMinimized());const auto prior=selected(view).text();
    choice(view).setCurrentIndex(1);QCoreApplication::processEvents();QCOMPARE(selected(view).text(),prior);view.showNormal();QTRY_VERIFY(selected(view).text().contains("300 / 250"));
  }
  void sourceAndSmoothingRasterKeepUnknownIntervalsDisconnected(){
    ui::NativeActivityHistory connected,gapped;
    for(quint64 sequence=1;sequence<=3;++sequence){auto known=packet(sequence);QVERIFY(connected.observe(known,7));auto unknown=packet(sequence);if(sequence==2){unknown->sourceRates.reset();}QVERIFY(gapped.observe(unknown,7));}
    ui::NativeActivityView lines,gaps;lines.beginSession(7);gaps.beginSession(7);show(lines);show(gaps);QVERIFY(lines.setPresentation(connected.snapshot()));QVERIFY(gaps.setPresentation(gapped.snapshot()));
    for(const auto* id:{"native-activity-receive-chart","native-activity-send-chart"}){
      const auto line=child<QWidget>(lines,id).grab().toImage(),gap=child<QWidget>(gaps,id).grab().toImage();
      for(const auto color:std::array{QColor(30,125,205),QColor(38,150,90),QColor(190,95,20)}){QVERIFY(middleColor(line,color)>5);QCOMPARE(middleColor(gap,color),0);}
    }
    choice(lines).setCurrentIndex(2);QTRY_VERIFY(selected(lines).text().contains("400 / 350"));const auto smooth=child<QWidget>(lines,"native-activity-receive-chart").grab().toImage();
    QVERIFY(middleColor(smooth,QColor(30,125,205))>5);QCOMPARE(middleColor(smooth,QColor(38,150,90)),0);QCOMPARE(middleColor(smooth,QColor(190,95,20)),0);
  }
  void smoothingPolicyChangesBreakLinesAndReportActualWindow(){
    ui::NativeActivityHistory history;
    for(quint64 sequence=1;sequence<=2;++sequence){auto input=packet(sequence);
      input->sourceRates->smoothing=core::sourceSmoothingPolicy(sequence==1?30:120);QVERIFY(history.observe(input,7));}
    ui::NativeActivityView view;view.beginSession(7);show(view);QVERIFY(view.setPresentation(history.snapshot()));choice(view).setCurrentIndex(2);
    QTRY_VERIFY(selected(view).text().contains("400 / 350"));QVERIFY(selected(view).toolTip().contains("120 seconds"));
    QVERIFY(selected(view).toolTip().contains("2 seconds"));
    const auto image=child<QWidget>(view,"native-activity-receive-chart").grab().toImage();QCOMPARE(middleColor(image,QColor(30,125,205)),0);
  }
  void tooltipReentryDuringQueuedModeRenderUsesNewerChoice(){
    ui::NativeActivityView view;view.beginSession(7);show(view);QVERIFY(view.setPresentation(frame(packet())));selected(view).setToolTip({});Hook hook;
    hook.callback=[&]{choice(view).setCurrentIndex(2);};selected(view).installEventFilter(&hook);choice(view).setCurrentIndex(1);
    QTRY_VERIFY(!hook.armed);QTRY_VERIFY(selected(view).text().contains("400 / 350"));QVERIFY(view.rateMode()==Mode::Smoothed);
    QVERIFY(child<QLabel>(view,"native-activity-legend").text().startsWith("Combined: blue solid. Session limit"));
  }
  void tooltipDeletionOccursAfterQtComboSetterReturns(){
    QPointer<ui::NativeActivityView> view=new ui::NativeActivityView;view->beginSession(7);show(*view);QVERIFY(view->setPresentation(frame(packet())));
    selected(*view).setToolTip({});Hook remove;remove.callback=[&]{delete view.data();};selected(*view).installEventFilter(&remove);
    choice(*view).setCurrentIndex(1);QVERIFY(view);QVERIFY(remove.armed);QTRY_VERIFY(view.isNull());QVERIFY(!remove.armed);
  }
  void newProfileRejectsOldFrameAndResetsOnlyFacts(){
    ui::NativeActivityView view;view.beginSession(7);show(view);const auto old=frame(packet());QVERIFY(view.setPresentation(old));choice(view).setCurrentIndex(1);
    QTRY_VERIFY(selected(view).text().contains("300 / 250"));QVERIFY(view.beginSession(8));QVERIFY(!view.presentation());QVERIFY(view.rateMode()==Mode::Interval);
    QVERIFY(!view.setPresentation(old));QCOMPARE(view.sampleCount(),std::size_t{0});QVERIFY(selected(view).text().contains("Unavailable / Unavailable"));
    auto fresh=packet(1,8);QVERIFY(view.setPresentation(frame(fresh)));QVERIFY(selected(view).text().contains("300 / 250"));
  }
};
QTEST_MAIN(NativeActivityRateModesTests)
#include "native_activity_rate_modes_tests.moc"
