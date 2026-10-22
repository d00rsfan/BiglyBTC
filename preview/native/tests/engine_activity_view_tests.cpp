#include "engine_activity_view.hpp"
#include "engine_activity_history.hpp"
#include "statistics_page.hpp"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QPixmap>
#include <QTabWidget>
#include <QtTest>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {
using namespace biglybtc;
using Packet = std::shared_ptr<ui::EngineStatisticsPacket>;
constexpr core::EngineRates Rates{90, 60, 60, 40, 30, 20};
Packet preparing(quint64 operation = 1, quint64 session = 1) {
  auto p = std::make_shared<ui::EngineStatisticsPacket>(); p->session = session; p->operation = operation; p->sequence = 1;
  p->target = {QStringLiteral("ENGINE-HASH"), 7}; p->name = QStringLiteral("Owned transfer"); return p;
}
Packet observed(quint64 sequence, std::int64_t elapsed, std::optional<core::EngineRates> rates,
    quint64 operation = 1, quint64 session = 1) {
  if (sequence < 2 || sequence > 10000) { throw std::runtime_error("Fixture sequence is outside its finite domain"); }
  auto p = preparing(operation, session); p->state = ui::EngineStatisticsState::Observed; p->sequence = sequence;
  const auto n = static_cast<std::int64_t>(sequence);
  p->sample = core::EngineStatistics{{n * 100, n * 80, n * 70, n * 50}, elapsed, rates}; return p;
}
void admit(ui::EngineActivityHistory& h, const Packet& p) {
  if (!h.observe(p, p->session)) { throw std::runtime_error("Generated frame input rejected by History"); }
}
void start(ui::EngineActivityHistory& h, quint64 operation = 1, quint64 session = 1) {
  admit(h, preparing(operation, session)); admit(h, observed(2, 0, {}, operation, session));
}
ui::EngineActivityPresentation known(ui::EngineActivityHistory& h, quint64 operation = 1, quint64 session = 1) {
  start(h, operation, session); admit(h, observed(3, 1000000000, Rates, operation, session)); return h.snapshot();
}
QLabel* label(QWidget& parent, const char* id) {
  auto* found = parent.findChild<QLabel*>(QString::fromLatin1(id));
  if (!found) { throw std::runtime_error("Missing semantic Activity label"); } return found;
}
QWidget* chart(ui::EngineActivityView& view, bool sending = false) {
  auto* found = view.findChild<QWidget*>(sending ? QStringLiteral("engine-activity-send-chart") : QStringLiteral("engine-activity-receive-chart"));
  if (!found) { throw std::runtime_error("Missing engine Activity chart"); } return found;
}
void show(ui::EngineActivityView& view) { view.resize(1100, 850); view.show(); QCoreApplication::processEvents(); }
QTabWidget& activityTabs(ui::StatisticsPage& page) {
  auto* tabs = page.findChild<QTabWidget*>(QStringLiteral("statistics-activity-views"));
  if (!tabs) { throw std::runtime_error("Missing nested Activity tabs"); } return *tabs;
}
ui::EngineActivityView& activityView(ui::StatisticsPage& page) {
  auto* view = page.findChild<ui::EngineActivityView*>(QStringLiteral("statistics-engine-activity-view"));
  if (!view) { throw std::runtime_error("Missing typed engine Activity view"); } return *view;
}
void showPage(ui::StatisticsPage& page) { page.resize(1100, 900); page.show(); QCoreApplication::processEvents(); }
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
int seriesPixelsInMiddle(const QImage& image, const QColor& expected) {
  int result = 0;
  for (int x = image.width() / 3; x < image.width() * 2 / 3; ++x) {
    for (int y = 0; y < image.height(); ++y) {
      const auto actual = image.pixelColor(x, y);
      if (std::abs(actual.red() - expected.red()) <= 30 && std::abs(actual.green() - expected.green()) <= 30
          && std::abs(actual.blue() - expected.blue()) <= 30) { ++result; }
    }
  }
  return result;
}
} // namespace

class EngineActivityViewTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void nestedActivityPreservesPayloadDefaultAndExistingIdentities() {
    ui::StatisticsPage page; showPage(page);
    QCOMPARE(page.tabs().count(), 6); QCOMPARE(page.tabs().tabText(5), QStringLiteral("Tags")); QCOMPARE(page.tabs().currentIndex(), 0);
    QCOMPARE(page.tabs().tabText(0), QStringLiteral("Activity")); QCOMPARE(page.tabs().tabText(1), QStringLiteral("Transfers"));
    auto& tabs = activityTabs(page); QCOMPARE(tabs.count(), 3); QCOMPARE(tabs.currentIndex(), 2);
    QCOMPARE(tabs.tabText(0), QStringLiteral("Payload")); QCOMPARE(tabs.tabText(1), QStringLiteral("Engine traffic"));
    QCOMPARE(tabs.tabText(2), QStringLiteral("Session traffic"));
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-activity")));
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-activity-plot")));
    QCOMPARE(page.sampleCount(), std::size_t{0}); QVERIFY(!page.engineActivitySnapshot());
    auto& view = activityView(page); QVERIFY(!view.presentation()); QCOMPARE(view.sampleCount(), std::size_t{0});
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("Unavailable"));
  }
  void firstCounterObservationNeverInventsRateOrPoint() {
    ui::EngineActivityHistory h; start(h); ui::EngineActivityView view; show(view); view.setPresentation(h.snapshot());
    QCOMPARE(view.sampleCount(), std::size_t{0}); QCOMPARE(view.presentation()->latest()->sample->elapsedNanoseconds, std::int64_t{0});
    QCOMPARE(label(view, "engine-activity-samples")->text(), QStringLiteral("No measured engine rate samples yet"));
    QVERIFY(label(view, "engine-activity-status")->text().contains(QStringLiteral("unavailable")));
    for (const auto* id : {"received", "sent", "payload-received", "payload-sent", "protocol-received", "protocol-sent"}) {
      QCOMPARE(label(view, (QByteArray("engine-activity-rate-") + id).constData())->text(), QStringLiteral("Unavailable"));
    }
    QVERIFY(!chart(view)->accessibleName().isEmpty()); QVERIFY(!chart(view, true)->accessibleName().isEmpty());
  }
  void sixExactLatestRatesUseOwnedComponentsAndPlainText() {
    ui::EngineActivityHistory h; start(h); auto value = observed(3, 1000000000, core::EngineRates{3, 5, 1, 2, 1, 2});
    value->name = QStringLiteral("<b>literal transfer</b>"); admit(h, value);
    ui::EngineActivityView view; show(view); view.setPresentation(h.snapshot());
    const std::array<const char*, 6> ids{"received", "sent", "payload-received", "payload-sent", "protocol-received", "protocol-sent"};
    const std::array<qint64, 6> expected{3, 5, 1, 2, 1, 2};
    for (std::size_t i = 0; i < ids.size(); ++i) {
      auto* field = label(view, (QByteArray("engine-activity-rate-") + ids[i]).constData());
      QCOMPARE(field->text(), QStringLiteral("%1 bytes/s").arg(expected[i]));
      QCOMPARE(field->toolTip(), QStringLiteral("%1 measured bytes per second; no smoothing").arg(expected[i]));
      QCOMPARE(field->textFormat(), Qt::PlainText);
    }
    // Independent floor components intentionally differ from combined-payload.
    QCOMPARE(label(view, "engine-activity-rate-protocol-received")->text(), QStringLiteral("1 bytes/s"));
    QCOMPARE(label(view, "engine-activity-name")->text(), QStringLiteral("Transfer: <b>literal transfer</b>"));
    QCOMPARE(label(view, "engine-activity-name")->textFormat(), Qt::PlainText);
    QVERIFY(label(view, "engine-activity-scope")->text().contains(QStringLiteral("not IP/tracker/DHT or application-wide")));
    QCOMPARE(view.sampleCount(), std::size_t{1});
  }
  void genuineZeroRatesAreKnownObservations() {
    ui::EngineActivityHistory h; const auto initial = known(h); ui::EngineActivityView view; show(view); view.setPresentation(initial);
    admit(h, observed(4, 2000000000, core::EngineRates{})); view.setPresentation(h.snapshot());
    QCOMPARE(view.sampleCount(), std::size_t{2});
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("0 bytes/s"));
    QCOMPARE(label(view, "engine-activity-rate-protocol-sent")->text(), QStringLiteral("0 bytes/s"));
    QCOMPARE(view.presentation()->points().back().rates, core::EngineRates{});
  }
  void latestUnknownRateKeepsPriorPlotButNeverStaleRateLabels_data() {
    QTest::addColumn<bool>("terminal"); QTest::newRow("observed-unknown") << false; QTest::newRow("ended-unknown") << true;
  }
  void latestUnknownRateKeepsPriorPlotButNeverStaleRateLabels() {
    QFETCH(bool, terminal); ui::EngineActivityHistory h; const auto initial = known(h);
    ui::EngineActivityView view; show(view); view.setPresentation(initial);
    auto unknown = observed(4, 2000000000, {}); admit(h, unknown);
    if (terminal) { auto ended = std::make_shared<ui::EngineStatisticsPacket>(*unknown); ended->sequence = 5; ended->state = ui::EngineStatisticsState::Ended; admit(h, ended); }
    view.setPresentation(h.snapshot()); QCOMPARE(view.sampleCount(), std::size_t{1});
    QCOMPARE(view.presentation()->points().front().rates, Rates);
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(label(view, "engine-activity-rate-payload-sent")->text(), QStringLiteral("Unavailable"));
    QVERIFY(label(view, "engine-activity-status")->text().contains(QStringLiteral("unavailable")));
  }
  void terminalRetainsActualPointsWithoutDuplicateOrSyntheticZero() {
    ui::EngineActivityHistory h; const auto initial = known(h); ui::StatisticsPage page; page.beginRequest(1, 1);
    page.setEngineActivity(initial); auto ended = std::make_shared<ui::EngineStatisticsPacket>(*initial->latest());
    ended->sequence = 4; ended->state = ui::EngineStatisticsState::Ended; ended->sample.reset(); admit(h, ended);
    auto terminal = h.snapshot(); page.setEngineActivity(terminal);
    QVERIFY(page.engineActivitySnapshot() == terminal); QCOMPARE(page.engineActivitySnapshot()->points().size(), std::size_t{1});
    QCOMPARE(page.engineActivitySnapshot()->points().front().rates, Rates);
    showPage(page); activityTabs(page).setCurrentIndex(1);
    QCOMPARE(label(activityView(page), "engine-activity-rate-received")->text(), QStringLiteral("90 bytes/s"));
    QVERIFY(label(activityView(page), "engine-activity-status")->text().contains(QStringLiteral("last observed")));
  }
  void newPreparingClearsPointsAndEarlierRunCannotReturn() {
    ui::EngineActivityHistory h; const auto first = known(h); ui::EngineActivityView view; show(view); view.setPresentation(first);
    admit(h, preparing(2)); const auto next = h.snapshot(); view.setPresentation(next);
    QCOMPARE(view.sampleCount(), std::size_t{0}); QVERIFY(view.presentation() == next);
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("Unavailable"));
    view.setPresentation(first); QVERIFY(view.presentation() == next); QCOMPARE(view.sampleCount(), std::size_t{0});
    admit(h, observed(2, 0, {}, 2)); admit(h, observed(3, 1000000000, Rates, 2)); view.setPresentation(h.snapshot());
    QCOMPARE(view.sampleCount(), std::size_t{1}); QVERIFY(label(view, "engine-activity-name")->toolTip().endsWith(QStringLiteral("engine operation 2")));
  }
  void higherProfileRebindsAndLowerProfileIsRejected() {
    ui::EngineActivityHistory firstHistory; const auto first = known(firstHistory); ui::EngineActivityView view; show(view); view.setPresentation(first);
    ui::EngineActivityHistory secondHistory; admit(secondHistory, preparing(1, 2)); const auto second = secondHistory.snapshot(); view.setPresentation(second);
    QVERIFY(view.presentation() == second); QCOMPARE(view.sampleCount(), std::size_t{0});
    view.setPresentation(first); QVERIFY(view.presentation() == second);
    admit(secondHistory, observed(2, 0, {}, 1, 2)); admit(secondHistory, observed(3, 1000000000, Rates, 1, 2)); view.setPresentation(secondHistory.snapshot());
    QCOMPARE(view.sampleCount(), std::size_t{1}); QCOMPARE(view.presentation()->latest()->session, quint64{2});
  }
  void nullDuplicateAndOlderFrameCannotReplaceCurrent() {
    ui::EngineActivityHistory h; const auto first = known(h); admit(h, observed(4, 2000000000, core::EngineRates{180, 120, 120, 80, 60, 40}));
    const auto newer = h.snapshot(); ui::EngineActivityView view; show(view); view.setPresentation(newer);
    view.setPresentation({}); view.setPresentation(first); view.setPresentation(newer);
    QVERIFY(view.presentation() == newer); QCOMPARE(view.sampleCount(), std::size_t{2});
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("180 bytes/s"));
  }
  void factoryFramesOwnAllSixHundredPointsAfterHistoryDestruction() {
    ui::EngineActivityPresentation frame;
    {
      ui::EngineActivityHistory h; start(h);
      for (quint64 i = 0; i < 620; ++i) { admit(h, observed(i + 3, static_cast<std::int64_t>(i + 1) * 1000000000LL, Rates)); }
      frame = h.snapshot();
    }
    ui::EngineActivityView view; show(view); view.setPresentation(frame);
    QCOMPARE(view.sampleCount(), std::size_t{600}); QCOMPARE(view.presentation()->points().front().elapsedNanoseconds, std::int64_t{21000000000LL});
    QCOMPARE(view.presentation()->points().back().elapsedNanoseconds, std::int64_t{620000000000LL});
    QVERIFY(!view.presentation()->points().front().connectedToPrevious);
    QVERIFY(label(view, "engine-activity-samples")->text().contains(QStringLiteral("600 actual rate observations")));
    QVERIFY(!chart(view)->grab().isNull()); QVERIFY(!chart(view, true)->grab().isNull());
  }
  void actualChartsDrawAllSeriesAndLeaveUnknownIntervalsDisconnected() {
    ui::EngineActivityHistory connected, disconnected; start(connected); start(disconnected);
    admit(connected, observed(3, 1000000000, Rates)); admit(connected, observed(4, 3000000000, Rates));
    admit(disconnected, observed(3, 1000000000, Rates)); admit(disconnected, observed(4, 2000000000, {})); admit(disconnected, observed(5, 3000000000, Rates));
    const auto continuous = connected.snapshot(), gap = disconnected.snapshot();
    QCOMPARE(continuous->points().size(), std::size_t{2}); QCOMPARE(gap->points().size(), std::size_t{2});
    QVERIFY(continuous->points().back().connectedToPrevious); QVERIFY(!gap->points().back().connectedToPrevious);
    ui::EngineActivityView view; show(view); view.setPresentation(continuous);
    std::array<QImage, 2> lines{chart(view)->grab().toImage(), chart(view, true)->grab().toImage()};
    view.clear(); view.setPresentation(gap);
    std::array<QImage, 2> gaps{chart(view)->grab().toImage(), chart(view, true)->grab().toImage()};
    const std::array<QColor, 3> colors{QColor(30, 125, 205), QColor(38, 150, 90), QColor(190, 95, 20)};
    for (std::size_t direction = 0; direction < lines.size(); ++direction) {
      for (const auto& color : colors) {
        const int connectedPixels = seriesPixelsInMiddle(lines[direction], color);
        const int disconnectedPixels = seriesPixelsInMiddle(gaps[direction], color);
        const auto detail = QStringLiteral("direction=%1 color=%2 connected=%3 disconnected=%4 image=%5x%6 DPR=%7")
            .arg(direction).arg(color.name()).arg(connectedPixels).arg(disconnectedPixels)
            .arg(lines[direction].width()).arg(lines[direction].height()).arg(lines[direction].devicePixelRatio());
        qInfo().noquote() << detail;
        QVERIFY2(connectedPixels > 5, qPrintable(detail));
        QCOMPARE(disconnectedPixels, 0);
      }
    }
  }
  void largeObservationTimesSubtractBeforeFloatingConversion() {
    const auto maximum = std::numeric_limits<std::int64_t>::max(); ui::EngineActivityHistory h;
    admit(h, preparing()); admit(h, observed(2, maximum - 2, Rates)); admit(h, observed(3, maximum, Rates));
    ui::EngineActivityView view; show(view); view.setPresentation(h.snapshot());
    QCOMPARE(view.sampleCount(), std::size_t{2});
    QCOMPARE(view.presentation()->points().back().elapsedNanoseconds - view.presentation()->points().front().elapsedNanoseconds, std::int64_t{2});
    QVERIFY(seriesPixelsInMiddle(chart(view)->grab().toImage(), QColor(30, 125, 205)) > 5);
  }
  void readableElapsedAxesKeepExactNanosecondTooltips() {
    struct Case { std::int64_t first; std::int64_t last; QString firstText; QString spanText; };
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const std::array<Case, 5> cases{{{1, 3, QStringLiteral("1 ns"), QStringLiteral("2 ns")},
        {1500, 4500, QStringLiteral("1.5 \u00B5s"), QStringLiteral("3 \u00B5s")},
        {226979020, 4033184199LL, QStringLiteral("226.979 ms"), QStringLiteral("3.806 s")},
        {1500000000, 5500000000LL, QStringLiteral("1.5 s"), QStringLiteral("4 s")},
        {maximum - 2, maximum, QStringLiteral("9223372036.855 s"), QStringLiteral("2 ns")}}};
    ui::EngineActivityView view; show(view);
    for (const auto& item : cases) {
      ui::EngineActivityHistory h; admit(h, preparing()); admit(h, observed(2, item.first, Rates)); admit(h, observed(3, item.last, Rates));
      view.clear(); view.setPresentation(h.snapshot());
      for (const bool sending : {false, true}) {
        const auto tip = chart(view, sending)->toolTip();
        QVERIFY2(tip.startsWith(QStringLiteral("Observed elapsed: %1; span %2.").arg(item.firstText, item.spanText)), qPrintable(tip));
        QVERIFY(tip.contains(QStringLiteral("Exact plotted range: %1 to %2 ns;").arg(item.first).arg(item.last)));
        QVERIFY(tip.endsWith(QStringLiteral("span %1 ns.").arg(item.last - item.first)));
      }
    }
  }
  void hiddenUpdatesOnlyOwnedCacheWithoutLabelMutationOrPaint() {
    ui::EngineActivityHistory h; const auto first = known(h); ui::EngineActivityView view; show(view); view.setPresentation(first);
    Paints receive, send; chart(view)->installEventFilter(&receive); chart(view, true)->installEventFilter(&send);
    view.hide(); QCoreApplication::processEvents(); const auto beforeReceive = receive.count, beforeSend = send.count;
    admit(h, observed(4, 2000000000, core::EngineRates{180, 120, 120, 80, 60, 40})); const auto latest = h.snapshot(); view.setPresentation(latest);
    QCoreApplication::processEvents(); QVERIFY(view.presentation() == latest); QCOMPARE(view.sampleCount(), std::size_t{2});
    QCOMPARE(receive.count, beforeReceive); QCOMPARE(send.count, beforeSend);
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("90 bytes/s"));
    show(view); QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("180 bytes/s"));
  }
  void minimizedUpdatesWaitForActualRestore() {
    ui::EngineActivityHistory h; const auto first = known(h); ui::EngineActivityView view; show(view); view.setPresentation(first);
    Paints paints; chart(view)->installEventFilter(&paints); view.showMinimized(); QCoreApplication::processEvents(); QVERIFY(view.isMinimized()); const auto before = paints.count;
    admit(h, observed(4, 2000000000, core::EngineRates{180, 120, 120, 80, 60, 40})); view.setPresentation(h.snapshot()); QCoreApplication::processEvents();
    QCOMPARE(paints.count, before); QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("90 bytes/s"));
    view.showNormal(); QCoreApplication::processEvents(); QTRY_COMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("180 bytes/s"));
  }
  void hiddenClearDropsDataThenShowsAnEmptyGraph() {
    ui::EngineActivityHistory h; const auto frame = known(h); ui::EngineActivityView view; show(view); view.setPresentation(frame); view.hide(); QCoreApplication::processEvents();
    view.clear(); QVERIFY(!view.presentation()); QCOMPARE(view.sampleCount(), std::size_t{0});
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("90 bytes/s"));
    show(view); QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(label(view, "engine-activity-status")->text(), QStringLiteral("No engine observations yet"));
  }
  void reentrantNewOperationWinsEveryRemainingLabelAndChart() {
    ui::EngineActivityHistory h; const auto first = known(h); admit(h, preparing(2)); const auto next = h.snapshot();
    ui::EngineActivityView view; show(view); Hook hook; hook.callback = [&] { view.setPresentation(next); };
    label(view, "engine-activity-name")->installEventFilter(&hook); view.setPresentation(first);
    QVERIFY(!hook.armed); QVERIFY(view.presentation() == next); QCOMPARE(view.sampleCount(), std::size_t{0});
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("Unavailable"));
    QVERIFY(label(view, "engine-activity-name")->toolTip().endsWith(QStringLiteral("engine operation 2")));
  }
  void reentrantClearCannotBeOverwrittenByOldFrame() {
    ui::EngineActivityHistory h; const auto frame = known(h); ui::EngineActivityView view; show(view); Hook hook;
    hook.callback = [&] { view.clear(); }; label(view, "engine-activity-status")->installEventFilter(&hook); view.setPresentation(frame);
    QVERIFY(!hook.armed); QVERIFY(!view.presentation()); QCOMPARE(view.sampleCount(), std::size_t{0});
    QCOMPARE(label(view, "engine-activity-rate-received")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(label(view, "engine-activity-status")->text(), QStringLiteral("No engine observations yet"));
  }
  void directDeletionDuringViewRenderIsGuarded() {
    ui::EngineActivityHistory h; const auto frame = known(h); auto view = std::make_unique<ui::EngineActivityView>(); show(*view);
    QPointer guard(view.get()); Hook hook; hook.consume = true; hook.callback = [&] { view.reset(); };
    label(*view, "engine-activity-name")->installEventFilter(&hook); view->setPresentation(frame);
    QVERIFY(!hook.armed); QVERIFY(guard.isNull()); QCOMPARE(frame->points().size(), std::size_t{1});
  }
  void pageSavedRefreshKeepsFrameNewProfileClearsAndPayloadRemainsSeparate() {
    ui::EngineActivityHistory h; const auto frame = known(h); ui::StatisticsPage page; page.beginRequest(1, 1); showPage(page);
    activityTabs(page).setCurrentIndex(0); // Explicitly exercise retained operation Payload rendering.
    ui::Telemetry payload{{QStringLiteral("PAYLOAD-HASH"), 3}, 20, 100, 50, QStringLiteral("Downloading")}; page.recordPayload(payload, QStringLiteral("Payload"));
    page.setEngineActivity(frame); QVERIFY(page.engineActivitySnapshot() == frame); QCOMPARE(page.sampleCount(), std::size_t{1});
    page.beginRequest(1, 2); QVERIFY(page.engineActivitySnapshot() == frame);
    activityTabs(page).setCurrentIndex(1); QCOMPARE(label(activityView(page), "engine-activity-rate-received")->text(), QStringLiteral("90 bytes/s"));
    payload.down = 300; page.recordPayload(payload, QStringLiteral("Payload")); QCOMPARE(page.sampleCount(), std::size_t{2});
    QCOMPARE(label(page, "statistics-payload-download-rate")->text(), QStringLiteral("100 bytes/s"));
    activityTabs(page).setCurrentIndex(0); QCOMPARE(label(page, "statistics-payload-download-rate")->text(), QStringLiteral("300 bytes/s"));
    page.beginRequest(2, 1); QVERIFY(!page.engineActivitySnapshot()); QCOMPARE(activityView(page).sampleCount(), std::size_t{0}); QCOMPARE(page.sampleCount(), std::size_t{0});
    page.setEngineActivity(frame); QVERIFY(!page.engineActivitySnapshot());
  }
  void pageOnlyForwardsTheExactAdmittedImmutablePacket() {
    ui::EngineActivityHistory h; const auto frame = known(h); ui::StatisticsPage page; page.beginRequest(1, 1);
    page.setEngineStatistics(frame->latest()); QVERIFY(!page.engineActivitySnapshot());
    page.setEngineActivity(frame); QVERIFY(page.engineActivitySnapshot() == frame); QCOMPARE(activityView(page).sampleCount(), std::size_t{1});
    // Standalone statistics accepts no fabricated history; only a factory frame
    // with the exact admitted latest-packet binding supplies plot observations.
  }
  void pageRenderReentryCannotForwardAFrameToAnotherProfile() {
    ui::EngineActivityHistory h; const auto frame = known(h); ui::StatisticsPage page; page.beginRequest(1, 1); showPage(page); page.tabs().setCurrentIndex(1);
    Hook hook; hook.callback = [&] { page.beginRequest(2, 1); }; label(page, "statistics-engine-target")->installEventFilter(&hook);
    page.setEngineActivity(frame); QVERIFY(!hook.armed); QCOMPARE(page.session(), quint64{2}); QVERIFY(!page.engineActivitySnapshot());
  }
  void pageDeletionDuringCounterAdmissionPreventsTrailingViewAccess() {
    ui::EngineActivityHistory h; const auto frame = known(h); auto page = std::make_unique<ui::StatisticsPage>(); page->beginRequest(1, 1); showPage(*page); page->tabs().setCurrentIndex(1);
    QPointer guard(page.get()); Hook hook; hook.consume = true; hook.callback = [&] { page.reset(); };
    label(*page, "statistics-engine-status")->installEventFilter(&hook); page->setEngineActivity(frame); QVERIFY(!hook.armed); QVERIFY(guard.isNull());
  }
};
QTEST_MAIN(EngineActivityViewTests)
#include "engine_activity_view_tests.moc"
