#include "native_session_view.hpp"
#include "protocol_share.hpp"
#include <QApplication>
#include <QPixmap>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QtTest>
#include <array>
#include <functional>
#include <limits>
#include <stdexcept>
namespace {
using namespace biglybtc;using State=ui::NativeSessionState;
QLabel& field(QObject& widget,const char* id){auto* result=widget.findChild<QLabel*>(QString::fromLatin1(id));if(!result){throw std::runtime_error("Missing protocol share fact");}return *result;}
void show(QWidget& widget){widget.resize(1024,768);widget.show();QCoreApplication::processEvents();}
auto packet(quint64 sequence=1,quint64 session=7,State state=State::Active){
  core::EngineStatisticsAccumulator source;const auto first=source.observe({},0);const auto actual=source.observe({200,120,159,79},2000000000);if(!first||!actual){throw std::runtime_error("Actual accumulator rejected generated observations");}
  auto result=std::make_shared<ui::NativeSessionPacket>();result->session=session;result->applicationRun=3;result->epoch=1;result->sequence=sequence;result->state=state;
  result->ownerElapsedNanoseconds=2000000000;result->sample=*actual;
  result->sourceRates=core::SourceRateValues{30,10,2,1,40,3,core::sourceSmoothingPolicy(60)};return result;
}
class Hook final:public QObject{public:std::function<void()> callback;bool eventFilter(QObject* object,QEvent* event)override{QPointer<QObject> guard(object);if(event->type()==QEvent::ToolTipChange&&callback){auto once=std::move(callback);once();}return guard.isNull();}};
}
class ProtocolShareUiTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void exactRationalTenthsAndHalfEvenTiesNeverUseOverflowingProducts(){
    struct Vector{std::int64_t payload,protocol;std::uint16_t expected;};const auto maximum=std::numeric_limits<std::int64_t>::max();
    const std::array values{Vector{0,0,0},Vector{9,1,100},Vector{3,1,250},Vector{1,2,667},Vector{79,20,202},Vector{39,20,339},
      Vector{1999,1,0},Vector{1997,3,2},Vector{1995,5,2},Vector{1993,7,4},Vector{1,1999,1000},
      Vector{maximum,0,0},Vector{0,maximum,1000},Vector{maximum/2,maximum-maximum/2,500},Vector{maximum-1,1,0},Vector{1,maximum-1,1000}};
    for(const auto& value:values){const auto actual=ui::protocolShareTenths(value.payload,value.protocol);QVERIFY(actual);QCOMPARE(*actual,value.expected);}
  }
  void missingNegativeAndUnrepresentableSumsRemainUnknownRatherThanZero(){
    QVERIFY(!ui::protocolShareTenths({},0));QVERIFY(!ui::protocolShareTenths(0,{}));QVERIFY(!ui::protocolShareTenths({},{}));
    QVERIFY(!ui::protocolShareTenths(-1,0));QVERIFY(!ui::protocolShareTenths(0,-1));
    const auto maximum=std::numeric_limits<std::int64_t>::max();QVERIFY(!ui::protocolShareTenths(maximum,1));QVERIFY(!ui::protocolShareTenths(1,maximum));
  }
  void acceptedAccumulatorRatesKeepEpochIntervalAndSourceWindowDistinct(){
    const auto actual=packet();QVERIFY(actual->sample->rates);QCOMPARE(actual->sample->rates->received,std::int64_t{100});QCOMPARE(actual->sample->rates->payloadReceived,std::int64_t{79});QCOMPARE(actual->sample->rates->protocolReceived,std::int64_t{20});
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(actual));
    QCOMPARE(field(view,"native-session-protocol-share-counter-received").text(),QStringLiteral("20.5%"));QCOMPARE(field(view,"native-session-protocol-share-counter-sent").text(),QStringLiteral("34.2%"));
    QCOMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 20.2%; 10 s 25%"));QCOMPARE(field(view,"native-session-protocol-share-rate-sent").text(),QStringLiteral("Interval 33.9%; 10 s 33.3%"));
    QCOMPARE(field(view,"native-session-counter-received").text(),QStringLiteral("200"));QCOMPARE(field(view,"native-session-rate-received").text(),QStringLiteral("100"));QCOMPARE(field(view,"native-session-rate-protocol-received").text(),QStringLiteral("20"));
    QCOMPARE(field(view,"native-session-share-ratio").text(),QStringLiteral("Session share ratio: 0.600"));
    QVERIFY(field(view,"native-session-protocol-share-rate-received").toolTip().contains(QStringLiteral("independently floored")));
    for(const auto* id:{"native-session-protocol-share-counter-received","native-session-protocol-share-counter-sent","native-session-protocol-share-rate-received","native-session-protocol-share-rate-sent"}){
      auto& value=field(view,id);QCOMPARE(value.textFormat(),Qt::PlainText);QVERIFY(!value.accessibleDescription().isEmpty());QVERIFY(view.rect().contains(QRect(value.mapTo(&view,QPoint{}),value.size())));}
    QVERIFY(view.findChildren<QTimer*>().isEmpty());
    const auto capture=qEnvironmentVariable("BIGLYBTC_PROTOCOL_CAPTURE_DIRECTORY");
    if(!capture.isEmpty()){QVERIFY(view.grab().save(capture+QStringLiteral("/protocol-share-policy-fixture.png")));}
  }
  void unobservedFirstKnownZeroAndClosedLastObservationRemainDifferent(){
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);QCOMPARE(field(view,"native-session-protocol-share-counter-received").text(),QStringLiteral("Unavailable"));
    core::EngineStatisticsAccumulator source;const auto initial=source.observe({},0);QVERIFY(initial);auto first=packet();first->ownerElapsedNanoseconds=0;first->sample=*initial;first->sourceRates.reset();QVERIFY(view.setPresentation(first));
    QCOMPARE(field(view,"native-session-protocol-share-counter-received").text(),QStringLiteral("0%"));QCOMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Unavailable"));
    const auto zero=source.observe({},1000000000);QVERIFY(zero);auto next=std::make_shared<ui::NativeSessionPacket>(*first);next->sequence=2;next->ownerElapsedNanoseconds=1000000000;next->sample=*zero;next->sourceRates=core::SourceRateValues{};next->sourceRates->smoothing=core::sourceSmoothingPolicy(60);QVERIFY(view.setPresentation(next));
    QCOMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 0%; 10 s 0%"));
    for(const auto state:{State::Closing,State::Closed,State::Uncertified}){ui::NativeSessionView retained;QVERIFY(retained.beginSession(7));show(retained);QVERIFY(retained.setPresentation(packet(1,7,state)));
      QCOMPARE(field(retained,"native-session-protocol-share-counter-received").text(),QStringLiteral("20.5% (last observed)"));QCOMPARE(field(retained,"native-session-protocol-share-rate-received").text(),QStringLiteral("Unavailable"));}
  }
  void hiddenProfilesAndUnsafeSourceWindowInputsCannotInventAShare(){
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);const auto first=packet();QVERIFY(view.setPresentation(first));view.hide();
    auto latest=std::make_shared<ui::NativeSessionPacket>(*first);latest->sequence=2;latest->sourceRates->payloadReceived=1;latest->sourceRates->protocolReceived=1;QVERIFY(view.setPresentation(latest));
    QCOMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 20.2%; 10 s 25%"));show(view);QTRY_COMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 20.2%; 10 s 50%"));
    view.showMinimized();QTRY_VERIFY(view.isMinimized());auto invalid=std::make_shared<ui::NativeSessionPacket>(*latest);invalid->sequence=3;invalid->sourceRates->payloadReceived=-1;QVERIFY(view.setPresentation(invalid));
    QCOMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 20.2%; 10 s 50%"));view.showNormal();QTRY_COMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 20.2%; 10 s Unavailable"));
    auto overflow=std::make_shared<ui::NativeSessionPacket>(*invalid);overflow->sequence=4;overflow->sourceRates->payloadReceived=std::numeric_limits<std::int64_t>::max();overflow->sourceRates->protocolReceived=1;QVERIFY(view.setPresentation(overflow));
    QCOMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 20.2%; 10 s Unavailable"));QVERIFY(!view.setPresentation(latest));
    view.hide();QVERIFY(view.beginSession(8));QVERIFY(!view.presentation());QVERIFY(!view.setPresentation(first));show(view);QTRY_COMPARE(field(view,"native-session-protocol-share-counter-received").text(),QStringLiteral("Unavailable"));
  }
  void percentagePropertyCallbacksCanReplaceTheProfileOrDeleteTheView(){
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);Hook replace;replace.callback=[&]{QVERIFY(view.beginSession(8));QVERIFY(view.setPresentation(packet(1,8)));};
    field(view,"native-session-protocol-share-counter-received").installEventFilter(&replace);QVERIFY(view.setPresentation(packet()));QVERIFY(!replace.callback);QVERIFY(view.presentation()&&view.presentation()->session==8);
    QCOMPARE(field(view,"native-session-protocol-share-rate-received").text(),QStringLiteral("Interval 20.2%; 10 s 25%"));
    QPointer<ui::NativeSessionView> victim=new ui::NativeSessionView;QVERIFY(victim->beginSession(7));show(*victim);Hook remove;remove.callback=[&]{delete victim.data();};
    field(*victim,"native-session-protocol-share-counter-received").installEventFilter(&remove);victim->setPresentation(packet());QVERIFY(victim.isNull());QVERIFY(!remove.callback);
  }
};
QTEST_MAIN(ProtocolShareUiTests)
#include "protocol_share_ui_tests.moc"
