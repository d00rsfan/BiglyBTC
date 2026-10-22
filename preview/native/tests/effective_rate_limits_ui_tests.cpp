#include "biglybtc/effective_rate_limits.hpp"
#include "effective_rate_limit_text.hpp"
#include "native_activity_history.hpp"
#include "native_activity_view.hpp"
#include "native_session_view.hpp"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QtTest>

#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
using namespace biglybtc;
using State = core::RateLimitState;
auto packet(quint64 sequence,core::SessionRateLimits limits={}) {
  auto value=std::make_shared<ui::NativeSessionPacket>();
  value->session=7;value->applicationRun=1;value->epoch=1;value->sequence=sequence;value->state=ui::NativeSessionState::Active;
  value->ownerElapsedNanoseconds=static_cast<qint64>(sequence)*1000000000;
  const auto n=static_cast<std::int64_t>(sequence);
  value->sample=core::EngineStatistics{{n*100,n*50,n*80,n*40},value->ownerElapsedNanoseconds,core::EngineRates{100,50,80,40,20,10}};
  value->rateLimits=limits;return value;
}
template<class T> T& child(QObject& owner,const char* id) {
  auto* value=owner.findChild<T*>(QString::fromLatin1(id));if(!value){throw std::runtime_error("Missing rate-limit control");}return *value;
}
void show(QWidget& view) {
  auto font=view.font();font.setStyleStrategy(QFont::NoSubpixelAntialias);view.setFont(font);
  view.resize(1100,850);view.show();QCoreApplication::processEvents();
}
class Hook final : public QObject {
public:
  bool armed=true,consume=false;std::function<void()> callback;
  bool eventFilter(QObject*,QEvent* event) override {
    if(armed&&event->type()==QEvent::ToolTipChange){armed=false;callback();return consume;}return false;
  }
};
int purple(const QImage& image,int left,int right) {
  int count=0;const QColor expected(130,70,180);
  for(int y=0;y<image.height();++y)for(int x=left;x<right;++x){
    const QColor color=image.pixelColor(x,y);
    if(std::abs(color.red()-expected.red())<30&&std::abs(color.green()-expected.green())<30&&std::abs(color.blue()-expected.blue())<30){++count;}
  }
  return count;
}
}
class EffectiveRateLimitsUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void connectionSlotAndProxyModesHaveDistinctText() {
    using Slots=core::UploadSlotState;using Proxy=core::SessionProxyMode;
    QCOMPARE(ui::effectiveUploadSlotsText({}),QStringLiteral("Unavailable"));
    QCOMPARE(ui::effectiveUploadSlotsText({Slots::Disabled,0}),QStringLiteral("Disabled (0 fixed slots)"));
    QCOMPARE(ui::effectiveUploadSlotsText({Slots::Unlimited,0}),QStringLiteral("Unlimited"));
    QCOMPARE(ui::effectiveUploadSlotsText({Slots::Automatic,0}),QStringLiteral("Automatic (rate-based)"));
    QCOMPARE(ui::effectiveUploadSlotsText({Slots::Limited,8}),QStringLiteral("8"));
    QCOMPARE(ui::effectiveUploadSlotsText({Slots::Automatic,8}),QStringLiteral("Unavailable"));
    QCOMPARE(ui::sessionProxyModeText(Proxy::Disabled),QStringLiteral("Disabled"));
    QCOMPARE(ui::sessionProxyModeText(Proxy::Socks4),QStringLiteral("Enabled (SOCKS4)"));
    QCOMPARE(ui::sessionProxyModeText(Proxy::Socks5),QStringLiteral("Enabled (SOCKS5)"));
    QCOMPARE(ui::sessionProxyModeText(Proxy::Http),QStringLiteral("Enabled (HTTP)"));
    QCOMPARE(ui::sessionProxyModeText(static_cast<Proxy>(99)),QStringLiteral("Unavailable"));
    QCOMPARE(ui::sessionConnectionSettingsText({}),QStringLiteral("Connections Unavailable; upload slots Unavailable; proxy Unavailable"));
    QCOMPARE(ui::sessionConnectionSettingsText({200,{Slots::Limited,8},Proxy::Disabled}),
        QStringLiteral("Connections 200; upload slots 8; proxy Disabled"));
    QVERIFY(ui::sessionConnectionSettingsScope().contains(QStringLiteral("not current peers")));
    QVERIFY(ui::sessionConnectionSettingsScope().contains(QStringLiteral("does not prove")));
  }
  void transfersShowsCompactSettingsAndClosedSnapshotWithoutChangingRates() {
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);
    auto& text=child<QLabel>(view,"native-session-connection-settings");
    QVERIFY(text.text().contains(QStringLiteral("Connections Unavailable")));
    auto first=packet(1,{{State::Limited,4096},{State::Unlimited,0}});
    first->rateLimits.connections={200,{core::UploadSlotState::Limited,8},core::SessionProxyMode::Disabled};
    const auto settings=first->rateLimits;const auto raw=*first->sample;
    QVERIFY(view.setPresentation(first));
    QCOMPARE(text.text(),QStringLiteral("Session settings: Connections 200; upload slots 8; proxy Disabled"));
    QCOMPARE(text.textFormat(),Qt::PlainText);
    QVERIFY(first->rateLimits==settings);QVERIFY(*first->sample==raw);
    QCOMPARE(child<QLabel>(view,"native-session-rate-received").text(),QStringLiteral("100"));
    QCOMPARE(child<QLabel>(view,"native-session-download-limit").text(),QStringLiteral("Session download limit: 4096 bytes/s"));
    auto closed=packet(2,settings);closed->state=ui::NativeSessionState::Closed;
    QVERIFY(view.setPresentation(closed));
    QCOMPARE(text.text(),QStringLiteral("Last observed session settings: Connections 200; upload slots 8; proxy Disabled"));
  }
  void malformedDiagnosticsRejectedAndNewSessionClearsSettings() {
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);
    auto first=packet(1);first->rateLimits.connections={200,{core::UploadSlotState::Limited,8},core::SessionProxyMode::Socks5};
    QVERIFY(view.setPresentation(first));
    auto badConnection=packet(2);badConnection->rateLimits.connections.connectionLimit=0;
    QVERIFY(!view.setPresentation(badConnection));
    auto badSlots=packet(2);badSlots->rateLimits.connections.uploadSlots={core::UploadSlotState::Automatic,9};
    QVERIFY(!view.setPresentation(badSlots));
    auto badProxy=packet(2);badProxy->rateLimits.connections.proxy=static_cast<core::SessionProxyMode>(99);
    QVERIFY(!view.setPresentation(badProxy));
    QCOMPARE(view.presentation()->sequence,quint64{1});
    QVERIFY(view.beginSession(8));
    QCOMPARE(child<QLabel>(view,"native-session-connection-settings").text(),
        QStringLiteral("Session settings: Connections Unavailable; upload slots Unavailable; proxy Unavailable"));
  }
  void compactSettingsDeferWhileHiddenAndMinimized() {
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);
    auto first=packet(1);first->rateLimits.connections={200,{core::UploadSlotState::Disabled,0},core::SessionProxyMode::Disabled};
    QVERIFY(view.setPresentation(first));
    auto& text=child<QLabel>(view,"native-session-connection-settings");
    view.hide();auto next=packet(2);next->rateLimits.connections={300,{core::UploadSlotState::Automatic,0},core::SessionProxyMode::Http};
    QVERIFY(view.setPresentation(next));QCoreApplication::processEvents();
    QCOMPARE(text.text(),QStringLiteral("Session settings: Connections 200; upload slots Disabled (0 fixed slots); proxy Disabled"));
    view.show();QTRY_COMPARE(text.text(),QStringLiteral("Session settings: Connections 300; upload slots Automatic (rate-based); proxy Enabled (HTTP)"));
    view.showMinimized();QTRY_VERIFY(view.isMinimized());
    auto last=packet(3);last->rateLimits.connections={400,{core::UploadSlotState::Unlimited,0},core::SessionProxyMode::Socks4};
    QVERIFY(view.setPresentation(last));QCoreApplication::processEvents();
    QCOMPARE(text.text(),QStringLiteral("Session settings: Connections 300; upload slots Automatic (rate-based); proxy Enabled (HTTP)"));
    view.showNormal();QTRY_COMPARE(text.text(),QStringLiteral("Session settings: Connections 400; upload slots Unlimited; proxy Enabled (SOCKS4)"));
  }
  void settingsTooltipReplacementCannotResumeAnOldPacket() {
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);
    auto& text=child<QLabel>(view,"native-session-connection-settings");text.setToolTip({});
    auto newer=packet(2);newer->rateLimits.connections={300,{core::UploadSlotState::Automatic,0},core::SessionProxyMode::Http};
    Hook hook;bool accepted=false;hook.callback=[&]{accepted=view.setPresentation(newer);};text.installEventFilter(&hook);
    auto older=packet(1);older->rateLimits.connections={200,{core::UploadSlotState::Limited,8},core::SessionProxyMode::Disabled};
    QVERIFY(view.setPresentation(older));QVERIFY(!hook.armed);QVERIFY(accepted);
    QCOMPARE(view.presentation()->sequence,quint64{2});
    QCOMPARE(text.text(),QStringLiteral("Session settings: Connections 300; upload slots Automatic (rate-based); proxy Enabled (HTTP)"));
    QVERIFY(text.accessibleDescription().contains(QStringLiteral("Connections 300")));
  }
  void settingsTooltipCallbackCanDeleteOwner() {
    QPointer<ui::NativeSessionView> view=new ui::NativeSessionView;QVERIFY(view->beginSession(7));show(*view);
    auto& text=child<QLabel>(*view,"native-session-connection-settings");text.setToolTip({});
    Hook hook;hook.consume=true;hook.callback=[&]{delete view.data();};text.installEventFilter(&hook);
    auto first=packet(1);first->rateLimits.connections={200,{core::UploadSlotState::Limited,8},core::SessionProxyMode::Disabled};
    QVERIFY(view->setPresentation(first));QVERIFY(!hook.armed);QVERIFY(!view);
  }
  void unavailableUnlimitedAndFiniteHaveDistinctText() {
    QCOMPARE(ui::effectiveRateLimitText({}),QStringLiteral("Unavailable"));
    QCOMPARE(ui::effectiveRateLimitText({State::Unlimited,0}),QStringLiteral("Unlimited"));
    QCOMPARE(ui::effectiveRateLimitText({State::Limited,4096}),QStringLiteral("4096 bytes/s"));
    QCOMPARE(ui::effectiveRateLimitText({State::Limited,2147483646}),QStringLiteral("2147483646 bytes/s"));
    QCOMPARE(ui::effectiveRateLimitText({State::Unlimited,12}),QStringLiteral("Unavailable"));
    QVERIFY(ui::effectiveRateLimitScope().contains(QStringLiteral("Local peers may bypass")));
  }
  void transfersShowsActualSessionLimitsWithoutChangingRawRates() {
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);
    auto value=packet(1,{{State::Limited,4096},{State::Unlimited,0}});const auto raw=*value->sample;
    QVERIFY(view.setPresentation(value));
    QCOMPARE(child<QLabel>(view,"native-session-download-limit").text(),QStringLiteral("Session download limit: 4096 bytes/s"));
    QCOMPARE(child<QLabel>(view,"native-session-upload-limit").text(),QStringLiteral("Session upload limit: Unlimited"));
    QCOMPARE(child<QLabel>(view,"native-session-rate-received").text(),QStringLiteral("100"));QVERIFY(*value->sample==raw);
    auto closed=packet(2,value->rateLimits);closed->state=ui::NativeSessionState::Closed;
    QVERIFY(view.setPresentation(closed));QVERIFY(child<QLabel>(view,"native-session-upload-limit").text().startsWith(QStringLiteral("Last observed")));
  }
  void invalidLimitPacketRejectedAndNewProfileClearsOldFacts() {
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet(1,{{State::Limited,1000},{State::Limited,2000}})));
    auto invalid=packet(2);invalid->rateLimits.upload={State::Limited,0};QVERIFY(!view.setPresentation(invalid));
    QVERIFY(view.beginSession(8));QVERIFY(child<QLabel>(view,"native-session-download-limit").text().contains(QStringLiteral("Unavailable")));
  }
  void historyCopiesOnlyCompactLimitsAndKeepsItsExistingBound() {
    ui::NativeActivityHistory history;
    for(quint64 n=1;n<=605;++n){QVERIFY(history.observe(packet(n,{{State::Limited,static_cast<qint64>(1000+n)},{State::Unlimited,0}}),7));}
    const auto frame=history.snapshot();QCOMPARE(frame->points().size(),std::size_t{600});
    QCOMPARE(frame->points().front().rateLimits.download.bytesPerSecond,qint64{1006});
    QCOMPARE(frame->points().back().rateLimits.download.bytesPerSecond,qint64{1605});
    QVERIFY(frame->points().back().rateLimits.upload.state==State::Unlimited);
  }
  void activityLabelsDeferWhileHiddenAndShowLatestLimit() {
    ui::NativeActivityView view;QVERIFY(view.beginSession(7));ui::NativeActivityHistory history;
    QVERIFY(history.observe(packet(1,{{State::Limited,1000},{State::Unlimited,0}}),7));QVERIFY(view.setPresentation(history.snapshot()));
    QCOMPARE(child<QLabel>(view,"native-activity-download-limit").text(),QStringLiteral("Unavailable"));
    show(view);QCOMPARE(child<QLabel>(view,"native-activity-download-limit").text(),QStringLiteral("1000 bytes/s"));
    view.hide();QVERIFY(history.observe(packet(2,{{State::Limited,2000},{State::Limited,3000}}),7));QVERIFY(view.setPresentation(history.snapshot()));
    QCOMPARE(child<QLabel>(view,"native-activity-download-limit").text(),QStringLiteral("1000 bytes/s"));
    view.show();QTRY_COMPARE(child<QLabel>(view,"native-activity-download-limit").text(),QStringLiteral("2000 bytes/s"));
  }
  void finiteLimitSeriesHasInteriorStrokeAndUnlimitedCreatesAGap() {
    ui::NativeActivityHistory history;const core::SessionRateLimits finite{{State::Limited,1000},{State::Limited,1000}};
    for(quint64 n=1;n<=5;++n){QVERIFY(history.observe(packet(n,n==3?core::SessionRateLimits{{State::Unlimited,0},{State::Unlimited,0}}:finite),7));}
    ui::NativeActivityView view;QVERIFY(view.beginSession(7));QVERIFY(view.setPresentation(history.snapshot()));show(view);
    for(const auto* id:{"native-activity-receive-chart","native-activity-send-chart"}){
      auto& chart=child<QWidget>(view,id);const auto image=chart.grab().toImage();
      const int left=chart.fontMetrics().horizontalAdvance(QStringLiteral("1000 bytes/s"))+16;
      const int span=chart.width()-left-16;
      QVERIFY(purple(image,left+span/12,left+span/5)>5);
      QCOMPARE(purple(image,left+span*2/5,left+span*3/5),0);
    }
  }
  void unlimitedAndUnavailableNeverDrawAZeroLimitLine() {
    for(const auto limits:{core::SessionRateLimits{},core::SessionRateLimits{{State::Unlimited,0},{State::Unlimited,0}}}){
      ui::NativeActivityHistory history;for(quint64 n=1;n<=3;++n){QVERIFY(history.observe(packet(n,limits),7));}
      ui::NativeActivityView view;QVERIFY(view.beginSession(7));QVERIFY(view.setPresentation(history.snapshot()));show(view);
      auto& chart=child<QWidget>(view,"native-activity-receive-chart");const auto image=chart.grab().toImage();
      QCOMPARE(purple(image,0,image.width()),0);
    }
  }
  void configuredPolicyAdmissionRejectsInvalidWindowOrInterval() {
    ui::NativeSessionView view;QVERIFY(view.beginSession(7));show(view);
    auto valid=packet(1);valid->sourceRates=core::SourceRateValues{};
    valid->sourceRates->smoothing=core::sourceSmoothingPolicy(120);QVERIFY(view.setPresentation(valid));
    for(const auto badPolicy:{core::SourceSmoothingPolicy{9,1},core::SourceSmoothingPolicy{30,2},core::SourceSmoothingPolicy{1801,20}}){
      auto invalid=packet(2);invalid->sourceRates=core::SourceRateValues{};invalid->sourceRates->smoothing=badPolicy;
      QVERIFY(!view.setPresentation(invalid));QCOMPARE(view.presentation()->sequence,quint64{1});
    }
    auto next=packet(2);next->sourceRates=core::SourceRateValues{};
    next->sourceRates->smoothing=core::sourceSmoothingPolicy(119);QVERIFY(view.setPresentation(next));
    QCOMPARE(view.presentation()->sourceRates->smoothing.intervalSeconds,1);
  }
  void newLimitLabelCallbackMayDeleteOwner() {
    QPointer<ui::NativeSessionView> view=new ui::NativeSessionView;QVERIFY(view->beginSession(7));show(*view);
    child<QLabel>(*view,"native-session-download-limit").setToolTip({});
    Hook hook;hook.consume=true;hook.callback=[&]{delete view.data();};
    child<QLabel>(*view,"native-session-download-limit").installEventFilter(&hook);
    view->setPresentation(packet(1,{{State::Limited,1000},{State::Unlimited,0}}));QVERIFY(!hook.armed);QVERIFY(!view);
  }
};
QTEST_MAIN(EffectiveRateLimitsUiTests)
#include "effective_rate_limits_ui_tests.moc"
