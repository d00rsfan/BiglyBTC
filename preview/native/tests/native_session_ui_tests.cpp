#include "native_activity_history.hpp"
#include "native_activity_view.hpp"
#include "native_session_view.hpp"

#include <QAccessible>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QtTest>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
using State = ui::NativeSessionState;
constexpr std::array<const char*, 6> Ids{
    "received", "sent", "payload-received", "payload-sent", "protocol-received", "protocol-sent"};
std::shared_ptr<ui::NativeSessionPacket> packet(quint64 sequence, qint64 elapsed, bool rates = true,
    State state = State::Idle) {
  auto p = std::make_shared<ui::NativeSessionPacket>();
  p->session = 7; p->applicationRun = 3; p->epoch = 1; p->sequence = sequence; p->state = state;
  p->ownerElapsedNanoseconds = elapsed;
  const auto n = static_cast<std::int64_t>(sequence);
  p->sample = core::EngineStatistics{{1000 + n * 100, 800 + n * 80, 600 + n * 60, 500 + n * 50}, elapsed, {}};
  if (rates) { p->sample->rates = core::EngineRates{100, 80, 60, 50, 40, 30}; }
  return p;
}
QLabel& field(QWidget& widget, const QString& name) {
  auto* value = widget.findChild<QLabel*>(name);
  if (!value) { throw std::runtime_error("Missing native-session label"); }
  return *value;
}
QWidget& chart(ui::NativeActivityView& widget, bool sending = false) {
  auto* value = widget.findChild<QWidget*>(sending ? QStringLiteral("native-activity-send-chart") : QStringLiteral("native-activity-receive-chart"));
  if (!value) { throw std::runtime_error("Missing native-session chart"); }
  return *value;
}
void show(QWidget& widget) {
  auto font = widget.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); widget.setFont(font);
  widget.resize(1100, 900); widget.show(); QCoreApplication::processEvents();
}
class Hook final : public QObject {
public:
  bool armed = true, consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; }
    return false;
  }
};
class Paints final : public QObject {
public:
  int count = 0;
  bool eventFilter(QObject*, QEvent* event) override { if (event->type() == QEvent::Paint) { ++count; } return false; }
};
struct AccessArm { QObject* object; std::function<void()> callback; bool fired = false; };
thread_local AccessArm* activeAccess = nullptr;
void accessibility(QAccessibleEvent* event) {
  auto* arm = activeAccess;
  if (arm && event->object() == arm->object && event->type() == QAccessible::DescriptionChanged) {
    activeAccess = nullptr; arm->fired = true; arm->callback();
  }
}
class AccessScope final {
public:
  AccessArm arm;
  AccessScope(QObject* object, std::function<void()> callback)
      : arm{object, std::move(callback)}, wasActive_(QAccessible::isActive()),
        previous_(QAccessible::installUpdateHandler(accessibility)) {
    QAccessible::setActive(true); (void)QAccessible::queryAccessibleInterface(object); activeAccess = &arm;
  }
  ~AccessScope() { activeAccess = nullptr; QAccessible::installUpdateHandler(previous_); QAccessible::setActive(wasActive_); }
private:
  bool wasActive_;
  QAccessible::UpdateHandler previous_;
};
int coloredMiddle(const QImage& image, const QColor& expected) {
  int result = 0;
  for (int x = image.width() / 3; x < image.width() * 2 / 3; ++x) {
    for (int y = 0; y < image.height(); ++y) {
      const auto actual = image.pixelColor(x, y);
      if (std::abs(actual.red() - expected.red()) <= 25 && std::abs(actual.green() - expected.green()) <= 25
          && std::abs(actual.blue() - expected.blue()) <= 25) { ++result; }
    }
  }
  return result;
}
} // namespace

class NativeSessionUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void unstartedHasNoFabricatedCountersRatesHistoryOrTimer() {
    ui::NativeSessionView totals; ui::NativeActivityView activity;
    show(totals); show(activity);
    QVERIFY(!totals.setPresentation(packet(1, 1000000000)));
    QVERIFY(!totals.beginSession(0)); QVERIFY(totals.beginSession(7));
    QVERIFY(activity.beginSession(7)); QVERIFY(!activity.beginSession(7));
    QVERIFY(!totals.presentation()); QVERIFY(!activity.presentation());
    QCOMPARE(activity.sampleCount(), std::size_t{0});
    for (const auto* id : Ids) {
      QCOMPARE(field(totals, QStringLiteral("native-session-counter-") + id).text(), QStringLiteral("Unavailable"));
      QCOMPARE(field(totals, QStringLiteral("native-session-rate-") + id).text(), QStringLiteral("Unavailable"));
    }
    QVERIFY(totals.findChildren<QTimer*>().isEmpty()); QVERIFY(activity.findChildren<QTimer*>().isEmpty());
  }
  void exactAbsoluteCountersAndIndependentFlooredRatesArePlainText() {
    auto p = packet(1, 1000000000);
    p->sample->counters = {1111, 777, 800, 600};
    // Independent floors deliberately differ from combined-minus-payload.
    p->sample->rates = core::EngineRates{100, 60, 79, 39, 20, 20};
    ui::NativeSessionView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(p));
    const std::array<qint64, 6> counters{1111, 777, 800, 600, 311, 177};
    const std::array<qint64, 6> rates{100, 60, 79, 39, 20, 20};
    for (std::size_t i = 0; i < Ids.size(); ++i) {
      QCOMPARE(field(view, QStringLiteral("native-session-counter-") + Ids[i]).text(), QString::number(counters[i]));
      QCOMPARE(field(view, QStringLiteral("native-session-rate-") + Ids[i]).text(), QString::number(rates[i]));
    }
    for (auto* label : view.findChildren<QLabel*>()) { QCOMPARE(label->textFormat(), Qt::PlainText); }
    QVERIFY(field(view, QStringLiteral("native-session-uptime")).toolTip().startsWith(QStringLiteral("1000000000 steady nanoseconds")));
    QVERIFY(field(view, QStringLiteral("native-session-scope")).text().contains(QStringLiteral("not operation sums")));
    ui::NativeActivityHistory history; QVERIFY(history.observe(p, 7));
    QCOMPARE(history.snapshot()->points().front().rates.protocolReceived, qint64{20});
    QCOMPARE(history.snapshot()->points().front().rates.protocolSent, qint64{20});
    ui::NativeActivityView graph; QVERIFY(graph.beginSession(7)); show(graph); QVERIFY(graph.setPresentation(history.snapshot()));
    QCOMPARE(field(graph, QStringLiteral("native-activity-rate-protocol-received")).text(), QStringLiteral("20 bytes/s"));
  }
  void everyOwnerStateSeparatesCurrentRatesFromLastObservedCounters_data() {
    QTest::addColumn<int>("state"); QTest::addColumn<bool>("knownRates");
    QTest::newRow("starting") << static_cast<int>(State::Starting) << false;
    QTest::newRow("idle") << static_cast<int>(State::Idle) << true;
    QTest::newRow("active") << static_cast<int>(State::Active) << true;
    QTest::newRow("retiring") << static_cast<int>(State::Retiring) << true;
    QTest::newRow("closing") << static_cast<int>(State::Closing) << false;
    QTest::newRow("closed") << static_cast<int>(State::Closed) << false;
    QTest::newRow("uncertified") << static_cast<int>(State::Uncertified) << false;
  }
  void everyOwnerStateSeparatesCurrentRatesFromLastObservedCounters() {
    QFETCH(int, state); QFETCH(bool, knownRates);
    auto p = packet(1, 1000000000, true, static_cast<State>(state));
    if (p->state == State::Starting) { p->sample.reset(); }
    ui::NativeSessionView totals; QVERIFY(totals.beginSession(7)); show(totals); QVERIFY(totals.setPresentation(p));
    ui::NativeActivityHistory history; QVERIFY(history.observe(p, 7));
    ui::NativeActivityView activity; QVERIFY(activity.beginSession(7)); show(activity); QVERIFY(activity.setPresentation(history.snapshot()));
    QCOMPARE(field(totals, QStringLiteral("native-session-counter-received")).text(), p->sample ? QStringLiteral("1100") : QStringLiteral("Unavailable"));
    QCOMPARE(field(totals, QStringLiteral("native-session-rate-received")).text(), knownRates ? QStringLiteral("100") : QStringLiteral("Unavailable"));
    QCOMPARE(field(activity, QStringLiteral("native-activity-rate-received")).text(), knownRates ? QStringLiteral("100 bytes/s") : QStringLiteral("Unavailable"));
    if (p->state == State::Uncertified) { QCOMPARE(activity.sampleCount(), std::size_t{0}); }
  }
  void malformedAndStalePacketsCannotMutateAcceptedFacts() {
    const auto first = packet(2, 2000000000); ui::NativeActivityHistory history;
    QVERIFY(history.observe(first, 7)); const auto frozen = history.snapshot();
    ui::NativeSessionView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(first));
    for (int mutation = 0; mutation < 13; ++mutation) {
      auto bad = packet(3, 3000000000);
      switch (mutation) {
        case 0: bad->session = 6; break;
        case 1: bad->applicationRun = 0; break;
        case 2: bad->applicationRun = 4; break;
        case 3: bad->epoch = 0; break;
        case 4: bad->sequence = 2; break;
        case 5: bad->ownerElapsedNanoseconds = -1; break;
        case 6: bad->sample->elapsedNanoseconds = bad->ownerElapsedNanoseconds + 1; break;
        case 7: bad->sample->counters.received = -1; break;
        case 8: bad->sample->counters.payloadReceived = bad->sample->counters.received + 1; break;
        case 9: bad->sample->counters = first->sample->counters; --bad->sample->counters.received; break;
        case 10: bad->sample.reset(); break;
        case 11: bad->state = static_cast<State>(999); break;
        case 12: bad->sample->rates->protocolSent = -1; break;
      }
      QVERIFY2(!history.observe(bad, 7), qPrintable(QString::number(mutation)));
      QVERIFY2(!view.setPresentation(bad), qPrintable(QString::number(mutation)));
      QVERIFY(view.presentation() == first); QVERIFY(history.snapshot()->latest() == first);
    }
    QCOMPARE(frozen->points().size(), std::size_t{1});
  }
  void firstSampleUnknownRateDoesNotPlotButActualIdleZeroDoes() {
    ui::NativeActivityHistory history;
    auto first = packet(1, 0, false); QVERIFY(history.observe(first, 7));
    QCOMPARE(history.snapshot()->points().size(), std::size_t{0});
    auto zero = packet(2, 1000000000); zero->sample->counters = first->sample->counters;
    zero->sample->rates = core::EngineRates{}; QVERIFY(history.observe(zero, 7));
    const auto frame = history.snapshot(); QCOMPARE(frame->points().size(), std::size_t{1});
    QCOMPARE(frame->points()[0].rates, core::EngineRates{}); QVERIFY(!frame->points()[0].connectedToPrevious);
    ui::NativeActivityView activity; QVERIFY(activity.beginSession(7)); show(activity); QVERIFY(activity.setPresentation(frame));
    QCOMPARE(field(activity, QStringLiteral("native-activity-rate-received")).text(), QStringLiteral("0 bytes/s"));
  }
  void activeRetiringIdleAndNextTransferShareOneAbsoluteHistory() {
    ui::NativeActivityHistory history;
    QVERIFY(history.observe(packet(1, 1000000000, true, State::Active), 7));
    QVERIFY(history.observe(packet(2, 2000000000, true, State::Retiring), 7));
    auto idle = packet(3, 3000000000); idle->sample->counters = packet(2, 2000000000)->sample->counters;
    idle->sample->rates = core::EngineRates{}; QVERIFY(history.observe(idle, 7));
    QVERIFY(history.observe(packet(4, 4000000000, true, State::Active), 7));
    const auto frame = history.snapshot(); QCOMPARE(frame->points().size(), std::size_t{4});
    QVERIFY(!frame->points()[0].connectedToPrevious);
    for (std::size_t i = 1; i < 4; ++i) { QVERIFY(frame->points()[i].connectedToPrevious); }
    QCOMPARE(frame->points()[2].rates.received, qint64{0});
    QCOMPARE(frame->latest()->sample->counters.received, qint64{1400});
  }
  void unknownFailureAndSequenceGapsStayDisconnectedEvenThroughDuplicateSamples() {
    ui::NativeActivityHistory history;
    QVERIFY(history.observe(packet(1, 1000000000), 7));
    QVERIFY(history.observe(packet(2, 2000000000, false), 7));
    QVERIFY(history.observe(packet(3, 3000000000), 7));
    auto failure = std::make_shared<ui::NativeSessionPacket>(*history.snapshot()->latest());
    failure->sequence = 4; failure->state = State::Uncertified; QVERIFY(history.observe(failure, 7));
    QVERIFY(history.observe(packet(5, 5000000000), 7));
    auto duplicate = std::make_shared<ui::NativeSessionPacket>(*history.snapshot()->latest());
    duplicate->sequence = 9; QVERIFY(history.observe(duplicate, 7));
    duplicate = std::make_shared<ui::NativeSessionPacket>(*duplicate); duplicate->sequence = 10;
    QVERIFY(history.observe(duplicate, 7));
    QVERIFY(history.observe(packet(11, 11000000000LL), 7));
    const auto frame = history.snapshot(); QCOMPARE(frame->points().size(), std::size_t{4});
    for (const auto& point : frame->points()) { QVERIFY(!point.connectedToPrevious); }
  }
  void closeDeduplicatesSameSampleAndLaterCloseSampleStaysStandalone() {
    ui::NativeActivityHistory history; const auto actual = packet(1, 1000000000);
    QVERIFY(history.observe(actual, 7));
    ui::NativeActivityHistory repeated; QVERIFY(repeated.observe(actual, 7));
    auto equalClose = std::make_shared<ui::NativeSessionPacket>(*actual); equalClose->sequence = 2; equalClose->state = State::Closed;
    QVERIFY(repeated.observe(equalClose, 7)); QCOMPARE(repeated.snapshot()->points().size(), std::size_t{1});
    auto closing = std::make_shared<ui::NativeSessionPacket>(*actual); closing->sequence = 2; closing->state = State::Closing;
    QVERIFY(history.observe(closing, 7)); QCOMPARE(history.snapshot()->points().size(), std::size_t{1});
    auto closed = packet(3, 3000000000, true, State::Closed); QVERIFY(history.observe(closed, 7));
    const auto frame = history.snapshot(); QCOMPARE(frame->points().size(), std::size_t{2});
    QVERIFY(!frame->points().back().connectedToPrevious); QVERIFY(!history.observe(packet(4, 4000000000), 7));
    ui::NativeSessionView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(closed));
    QCOMPARE(field(view, QStringLiteral("native-session-rate-received")).text(), QStringLiteral("Unavailable"));
    QCOMPARE(field(view, QStringLiteral("native-session-counter-received")).text(), QStringLiteral("1300"));
    QVERIFY(field(view, QStringLiteral("native-session-status")).text().contains(QStringLiteral("not exhaustive final")));
  }
  void newEpochAndProfileClearWhileCopiedFramesRemainOwned() {
    ui::NativeActivityHistory history; QVERIFY(history.observe(packet(1, 1000000000), 7));
    const auto old = history.snapshot();
    auto next = packet(1, 1000000000); next->epoch = 2; next->sample->counters = {0,0,0,0};
    QVERIFY(history.observe(next, 7)); QCOMPARE(history.snapshot()->points().size(), std::size_t{1});
    QVERIFY(!history.observe(packet(2, 2000000000), 7));
    auto profile = packet(1, 1000000000); profile->session = 8; QVERIFY(history.observe(profile, 8));
    QVERIFY(!history.observe(next, 7));
    auto oldRun = packet(2, 2000000000); oldRun->session = 9; oldRun->applicationRun = 2;
    QVERIFY(!history.observe(oldRun, 9));
    QCOMPARE(old->latest()->session, quint64{7}); QCOMPARE(old->latest()->epoch, quint64{1});
    QCOMPARE(old->points().front().rates.received, qint64{100});
    history.clear(); QVERIFY(!history.snapshot()); QCOMPARE(old->points().size(), std::size_t{1});
  }
  void sixHundredActualPointsRolloverWithoutGrowingCopiedFrames() {
    ui::NativeActivityHistory history;
    QVERIFY(history.observe(packet(1, 1000000000), 7)); const auto early = history.snapshot();
    for (quint64 i = 2; i <= 605; ++i) { QVERIFY(history.observe(packet(i, static_cast<qint64>(i) * 1000000000), 7)); }
    const auto latest = history.snapshot(); QCOMPARE(latest->points().size(), std::size_t{600});
    QCOMPARE(latest->points().front().elapsedNanoseconds, qint64{6000000000LL});
    QCOMPARE(latest->points().back().elapsedNanoseconds, qint64{605000000000LL});
    QVERIFY(!latest->points().front().connectedToPrevious); QVERIFY(latest->points().back().connectedToPrevious);
    QCOMPARE(early->points().size(), std::size_t{1}); QCOMPARE(early->latest()->sequence, quint64{1});
  }
  void int64ExtremesKeepExactCountersRatesUptimeAndFiniteChart() {
    const auto max = std::numeric_limits<qint64>::max(); auto p = packet(1, max - 2);
    p->sample->counters = {max,max,max,0}; p->sample->rates = core::EngineRates{max,max,max,0,0,max};
    ui::NativeSessionView totals; QVERIFY(totals.beginSession(7)); show(totals); QVERIFY(totals.setPresentation(p));
    QCOMPARE(field(totals, QStringLiteral("native-session-counter-protocol-sent")).text(), QString::number(max));
    QCOMPARE(field(totals, QStringLiteral("native-session-rate-received")).text(), QString::number(max));
    QVERIFY(field(totals, QStringLiteral("native-session-uptime")).toolTip().startsWith(QString::number(max - 2)));
    ui::NativeActivityHistory history; QVERIFY(history.observe(p, 7));
    auto next = std::make_shared<ui::NativeSessionPacket>(*p); next->sequence = 2;
    next->ownerElapsedNanoseconds = max; next->sample->elapsedNanoseconds = max;
    next->sample->rates = core::EngineRates{}; QVERIFY(history.observe(next, 7));
    ui::NativeActivityView graph; QVERIFY(graph.beginSession(7)); show(graph); QVERIFY(graph.setPresentation(history.snapshot()));
    graph.findChild<QComboBox*>(QStringLiteral("native-activity-rate-mode"))->setCurrentIndex(1);QCoreApplication::processEvents();
    QCOMPARE(graph.presentation()->points().back().elapsedNanoseconds - graph.presentation()->points().front().elapsedNanoseconds, qint64{2});
    QVERIFY(coloredMiddle(chart(graph).grab().toImage(), QColor(38,150,90)) > 5);
    QVERIFY(chart(graph).toolTip().contains(QString::number(max)));
  }
  void hiddenAndMinimizedWidgetsRetainDataWithoutRenderingUntilRestore() {
    ui::NativeActivityHistory history; QVERIFY(history.observe(packet(1, 1000000000), 7));
    ui::NativeActivityView graph; QVERIFY(graph.beginSession(7)); show(graph); QVERIFY(graph.setPresentation(history.snapshot()));
    Paints paints; chart(graph).installEventFilter(&paints);
    graph.hide(); QCoreApplication::processEvents(); const int before = paints.count;
    auto newer = packet(2, 2000000000); newer->sample->rates = core::EngineRates{200,160,120,100,80,60};
    QVERIFY(history.observe(newer, 7)); QVERIFY(graph.setPresentation(history.snapshot())); QCoreApplication::processEvents();
    QCOMPARE(paints.count, before); QCOMPARE(graph.sampleCount(), std::size_t{2});
    QCOMPARE(field(graph, QStringLiteral("native-activity-rate-received")).text(), QStringLiteral("100 bytes/s"));
    show(graph); QCOMPARE(field(graph, QStringLiteral("native-activity-rate-received")).text(), QStringLiteral("200 bytes/s"));
    graph.showMinimized(); QCoreApplication::processEvents(); QVERIFY(graph.isMinimized()); const int minimized = paints.count;
    QVERIFY(history.observe(packet(3, 3000000000), 7)); QVERIFY(graph.setPresentation(history.snapshot())); QCoreApplication::processEvents();
    QCOMPARE(paints.count, minimized); QCOMPARE(field(graph, QStringLiteral("native-activity-rate-received")).text(), QStringLiteral("200 bytes/s"));
    graph.showNormal(); QTRY_COMPARE(field(graph, QStringLiteral("native-activity-rate-received")).text(), QStringLiteral("100 bytes/s"));
    ui::NativeSessionView totals; QVERIFY(totals.beginSession(7)); QVERIFY(totals.setPresentation(newer));
    QCOMPARE(field(totals, QStringLiteral("native-session-counter-received")).text(), QStringLiteral("Unavailable"));
    show(totals); QCOMPARE(field(totals, QStringLiteral("native-session-counter-received")).text(), QStringLiteral("1200"));
  }
  void reopenedWidgetsUseRetainedImmutableFrameAndNewSessionRejectsOldDelivery() {
    ui::NativeActivityHistory history; QVERIFY(history.observe(packet(1, 1000000000), 7));
    const auto frame = history.snapshot(); auto first = std::make_unique<ui::NativeActivityView>();
    QVERIFY(first->beginSession(7)); show(*first); QVERIFY(first->setPresentation(frame)); QPointer old(first.get()); first.reset(); QVERIFY(!old);
    ui::NativeActivityView reopened; QVERIFY(reopened.beginSession(7)); show(reopened); QVERIFY(reopened.setPresentation(frame));
    QCOMPARE(reopened.sampleCount(), std::size_t{1}); QVERIFY(reopened.beginSession(8));
    QVERIFY(!reopened.setPresentation(frame)); QCOMPARE(reopened.sampleCount(), std::size_t{0});
    QVERIFY(!reopened.beginSession(0)); QVERIFY(!reopened.beginSession(7));
    ui::NativeSessionView totals; QVERIFY(totals.beginSession(7)); QVERIFY(totals.setPresentation(frame->latest()));
    QVERIFY(totals.beginSession(8)); QVERIFY(!totals.setPresentation(frame->latest())); QVERIFY(!totals.presentation());
    auto lowerRun = packet(2, 2000000000); lowerRun->session = 8; lowerRun->applicationRun = 2;
    QVERIFY(!totals.setPresentation(lowerRun));
    ui::NativeActivityHistory separate; QVERIFY(separate.observe(lowerRun, 8));
    QVERIFY(!reopened.setPresentation(separate.snapshot()));
  }
  void graphRasterHasAllSeriesAndDoesNotBridgeMissingSequence() {
    ui::NativeActivityHistory continuous, gap;
    auto a = packet(1, 1000000000); a->sample->rates = core::EngineRates{100,80,60,50,40,30};
    auto b = packet(2, 2000000000); b->sample->rates = core::EngineRates{200,160,120,100,80,60};
    QVERIFY(continuous.observe(a, 7)); QVERIFY(continuous.observe(b, 7));
    QVERIFY(gap.observe(a, 7)); auto skipped = std::make_shared<ui::NativeSessionPacket>(*b); skipped->sequence = 3;
    QVERIFY(gap.observe(skipped, 7));
    ui::NativeActivityView lines, separated; QVERIFY(lines.beginSession(7)); QVERIFY(separated.beginSession(7)); show(lines); show(separated);
    lines.findChild<QComboBox*>(QStringLiteral("native-activity-rate-mode"))->setCurrentIndex(1);
    separated.findChild<QComboBox*>(QStringLiteral("native-activity-rate-mode"))->setCurrentIndex(1);QCoreApplication::processEvents();
    QVERIFY(lines.setPresentation(continuous.snapshot())); QVERIFY(separated.setPresentation(gap.snapshot()));
    for (const bool send : {false,true}) {
      const auto line = chart(lines, send).grab().toImage(), dots = chart(separated, send).grab().toImage();
      for (const auto& color : std::array{QColor(30,125,205), QColor(38,150,90), QColor(190,95,20)}) {
        QVERIFY(coloredMiddle(line, color) > 5); QCOMPARE(coloredMiddle(dots, color), 0);
      }
    }
  }
  void finalCounterTooltipReentryCannotOverwriteNewerSession() {
    ui::NativeSessionView view; QVERIFY(view.beginSession(7)); show(view);
    Hook hook; hook.callback = [&] { QVERIFY(view.beginSession(8)); auto p = packet(2, 2000000000); p->session = 8; QVERIFY(view.setPresentation(p)); };
    field(view, QStringLiteral("native-session-counter-protocol-sent")).installEventFilter(&hook);
    QVERIFY(view.setPresentation(packet(1, 1000000000))); QVERIFY(!hook.armed);
    QCOMPARE(view.presentation()->session, quint64{8});
    QCOMPARE(field(view, QStringLiteral("native-session-counter-received")).text(), QStringLiteral("1200"));
    QVERIFY(field(view, QStringLiteral("native-session-status")).toolTip().startsWith(QStringLiteral("Profile 8;")));
  }
  void deletionFromFinalCounterTooltipAndChartTooltipIsGuarded() {
    auto view = std::make_unique<ui::NativeSessionView>(); QVERIFY(view->beginSession(7)); show(*view);
    QPointer guard(view.get()); Hook hook; hook.consume = true; hook.callback = [&] { view.reset(); };
    field(*view, QStringLiteral("native-session-counter-protocol-sent")).installEventFilter(&hook);
    view->setPresentation(packet(1, 1000000000)); QVERIFY(!hook.armed); QVERIFY(!guard);
    ui::NativeActivityHistory history; QVERIFY(history.observe(packet(1, 1000000000), 7));
    auto graph = std::make_unique<ui::NativeActivityView>(); QVERIFY(graph->beginSession(7)); show(*graph);
    QPointer graphGuard(graph.get()); Hook chartHook; chartHook.consume = true; chartHook.callback = [&] { graph.reset(); };
    chart(*graph).installEventFilter(&chartHook); graph->setPresentation(history.snapshot()); QVERIFY(!chartHook.armed); QVERIFY(!graphGuard);
  }
  void actualAccessibilityDescriptionCallbackSupportsReentryAndDeletion() {
    ui::NativeSessionView view; QVERIFY(view.beginSession(7)); show(view);
    auto newer = packet(2, 2000000000);
    AccessScope replace(&field(view, QStringLiteral("native-session-counter-protocol-sent")), [&] { QVERIFY(view.setPresentation(newer)); });
    QVERIFY(view.setPresentation(packet(1, 1000000000))); QVERIFY(replace.arm.fired); QVERIFY(view.presentation() == newer);
    QCOMPARE(field(view, QStringLiteral("native-session-counter-received")).text(), QStringLiteral("1200"));
    auto graph = std::make_unique<ui::NativeActivityView>(); QVERIFY(graph->beginSession(7)); show(*graph);
    QPointer guard(graph.get()); ui::NativeActivityHistory history; QVERIFY(history.observe(packet(1, 1000000000), 7));
    AccessScope remove(&field(*graph, QStringLiteral("native-activity-rate-protocol-sent")), [&] { graph.reset(); });
    graph->setPresentation(history.snapshot()); QVERIFY(remove.arm.fired); QVERIFY(!guard);
  }
  void individualLabelDeletionDuringTooltipLeavesLaterSettersSafe() {
    ui::NativeSessionView totals; QVERIFY(totals.beginSession(7)); show(totals);
    QPointer<QLabel> removed(&field(totals, QStringLiteral("native-session-counter-protocol-sent")));
    Hook hook; hook.consume = true; hook.callback = [&] { delete removed.data(); };
    removed->installEventFilter(&hook);
    QVERIFY(totals.setPresentation(packet(1, 1000000000))); QVERIFY(!hook.armed); QVERIFY(!removed);
    auto next = packet(2, 2000000000); QVERIFY(totals.setPresentation(next)); QVERIFY(totals.presentation() == next);
    QVERIFY(totals.beginSession(8)); QVERIFY(!totals.presentation());
    ui::NativeActivityHistory history; QVERIFY(history.observe(packet(1, 1000000000), 7));
    ui::NativeActivityView activity; QVERIFY(activity.beginSession(7)); show(activity);
    QPointer<QLabel> rate(&field(activity, QStringLiteral("native-activity-rate-protocol-sent")));
    Hook rateHook; rateHook.consume = true; rateHook.callback = [&] { delete rate.data(); };
    rate->installEventFilter(&rateHook);
    QVERIFY(activity.setPresentation(history.snapshot())); QVERIFY(!rateHook.armed); QVERIFY(!rate);
    QVERIFY(history.observe(next, 7)); const auto updated = history.snapshot();
    QVERIFY(activity.setPresentation(updated)); QVERIFY(activity.presentation() == updated);
    QVERIFY(activity.beginSession(8)); QVERIFY(!activity.presentation());
  }
  void individualChartDeletionDuringTooltipGuardsCacheAndLaterRefresh() {
    ui::NativeActivityHistory history; QVERIFY(history.observe(packet(1, 1000000000), 7));
    ui::NativeActivityView activity; QVERIFY(activity.beginSession(7)); show(activity);
    QPointer<QWidget> removed(&chart(activity));
    Hook hook; hook.consume = true; hook.callback = [&] { delete removed.data(); };
    removed->installEventFilter(&hook);
    QVERIFY(activity.setPresentation(history.snapshot())); QVERIFY(!hook.armed); QVERIFY(!removed);
    QVERIFY(history.observe(packet(2, 2000000000), 7)); const auto latest = history.snapshot();
    QVERIFY(activity.setPresentation(latest)); QVERIFY(activity.presentation() == latest);
    QCOMPARE(activity.sampleCount(), std::size_t{2}); QVERIFY(activity.beginSession(8));
    QCOMPARE(activity.sampleCount(), std::size_t{0});
  }
};
QTEST_MAIN(NativeSessionUiTests)
#include "native_session_ui_tests.moc"
