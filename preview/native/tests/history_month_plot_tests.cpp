#include "saved_history_plot.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QPointer>
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
using Traffic = ui::SavedHistoryTraffic;
constexpr core::HistoryDate End{2024, 3, 2};
constexpr std::array<Traffic, 3> Kinds{Traffic::Payload, Traffic::Protocol, Traffic::Dht};
constexpr std::array<std::size_t, 3> Down{3, 2, 5}, Up{1, 0, 4};
std::shared_ptr<core::SavedHistoryMonth> month(core::HistoryDate end = End) {
  const auto dates = core::savedHistoryMonthDates(end); if (!dates) { throw std::runtime_error("Invalid generated thirty-day range"); }
  auto source = std::make_shared<core::SavedHistoryMonth>(); source->endingDate = end;
  for (std::size_t i = 0; i < dates->size(); ++i) { source->days[i].date = (*dates)[i]; }
  core::SavedHistoryDay empty; empty.date = (*dates)[1]; source->days[1].snapshot = empty;
  core::SavedHistoryDay zero; zero.date = (*dates)[2]; zero.observations = 1; zero.hours[0] = core::HistoryCounters{}; zero.totals = core::HistoryCounters{}; source->days[2].snapshot = zero;
  core::SavedHistoryDay signedDay; signedDay.date = dates->back(); signedDay.observations = 1;
  signedDay.hours[9] = core::HistoryCounters{-11, -22, 33, -44, 55, -66}; signedDay.totals = *signedDay.hours[9]; source->days.back().snapshot = signedDay;
  return source;
}
void show(ui::SavedHistoryPlot& plot, int width = 1100) {
  auto font = plot.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); plot.setFont(font);
  plot.resize(width, 250); plot.show(); QCoreApplication::processEvents(); plot.activateWindow(); plot.setFocus(); QCoreApplication::processEvents();
}
void press(ui::SavedHistoryPlot* plot, int key) { QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier); QCoreApplication::sendEvent(plot, &event); }
QAction& copy(ui::SavedHistoryPlot& plot) {
  auto* a = plot.findChild<QAction*>(QStringLiteral("saved-history-copy-selection")); if (!a) { throw std::runtime_error("Missing Copy action"); } return *a;
}
class Clipboard final {
public:
  QClipboard* const value = QGuiApplication::clipboard();
  Clipboard() : original_(std::make_unique<QMimeData>()) {
    if (const auto* data = value->mimeData(QClipboard::Clipboard)) { for (const auto& f : data->formats()) { original_->setData(f, data->data(f)); } }
    value->setText(QStringLiteral("month offscreen sentinel"), QClipboard::Clipboard);
  }
  ~Clipboard() { value->setMimeData(original_.release(), QClipboard::Clipboard); }
  Clipboard(const Clipboard&) = delete; Clipboard& operator=(const Clipboard&) = delete;
private: std::unique_ptr<QMimeData> original_;
};
class TipHook final : public QObject {
public:
  bool armed = true, consume = false; std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; } return false;
  }
};
} // namespace

class HistoryMonthPlotTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); }
  void thirtyOldestFirstDatesCrossLeapDayWithoutCalendarMonthTruncation() {
    const auto frame = ui::savedHistoryMonthPlot(End, month()); QVERIFY(frame); QCOMPARE(frame->mode(), ui::SavedHistoryPlotMode::Month);
    QCOMPARE(frame->buckets().size(), std::size_t{30}); QCOMPARE(frame->endingDate(), End);
    QCOMPARE(frame->bucketLabel(0), QStringLiteral("2024-02-02 UTC")); QCOMPARE(frame->bucketLabel(27), QStringLiteral("2024-02-29 UTC")); QCOMPARE(frame->bucketLabel(29), QStringLiteral("2024-03-02 UTC"));
    for (const auto& bucket : frame->buckets()) { QVERIFY(!bucket.hour); }
    const auto epoch = ui::savedHistoryMonthPlot({1970, 1, 30}, {}); QVERIFY(epoch); QCOMPARE(epoch->bucketLabel(0), QStringLiteral("1970-01-01 UTC"));
    const auto high = ui::savedHistoryMonthPlot({9999, 12, 31}, {}); QVERIFY(high); QCOMPARE(high->bucketLabel(0), QStringLiteral("9999-12-02 UTC"));
  }
  void allThreeTrafficChoicesCopyExactlyTwoOfTheSixSignedCounters() {
    const core::HistoryCounters expected{-11, -22, 33, -44, 55, -66};
    for (std::size_t k = 0; k < Kinds.size(); ++k) {
      const auto frame = ui::savedHistoryMonthPlot(End, month(), Kinds[k]); QVERIFY(frame); QCOMPARE(frame->traffic(), Kinds[k]);
      QCOMPARE(frame->buckets().back().downloaded, std::optional<std::int64_t>{expected[Down[k]]});
      QCOMPARE(frame->buckets().back().uploaded, std::optional<std::int64_t>{expected[Up[k]]});
      QVERIFY(frame->bucketToolTip(29).contains(QString::number(static_cast<qlonglong>(expected[Down[k]])) + QStringLiteral(" bytes")));
      QVERIFY(!frame->bucketToolTip(29).contains(QStringLiteral("bytes/s")));
    }
  }
  void missingAndPresentEmptyAreUnknownButObservedZeroIsAnActualPoint() {
    const auto frame = ui::savedHistoryMonthPlot(End, month()); QVERIFY(frame);
    for (std::size_t i : {std::size_t{0}, std::size_t{1}}) { QVERIFY(!frame->buckets()[i].downloaded); QVERIFY(!frame->buckets()[i].uploaded); QVERIFY(!frame->point(i, false, {0, 0, 580, 180})); QVERIFY(frame->bucketToolTip(i).contains(QStringLiteral("Unknown"))); }
    QCOMPARE(frame->buckets()[2].downloaded, std::optional<std::int64_t>{0}); QVERIFY(frame->point(2, false, {0, 0, 580, 180}));
    QVERIFY(frame->bucketToolTip(2).contains(QStringLiteral("0 bytes")));
  }
  void nullAndEveryDateBindingMismatchProduceThirtyUnknownFacts() {
    for (int kind = 0; kind < 4; ++kind) {
      auto source = month(); if (kind == 0) { source.reset(); } else if (kind == 1) { source->endingDate.day = 3; }
      else if (kind == 2) { std::swap(source->days[0], source->days[1]); } else { source->days.back().snapshot->date.day = 1; }
      const auto frame = ui::savedHistoryMonthPlot(End, source); QVERIFY(frame); QCOMPARE(frame->buckets().size(), std::size_t{30});
      for (const auto& bucket : frame->buckets()) { QVERIFY(!bucket.downloaded); QVERIFY(!bucket.uploaded); }
    }
  }
  void invalidRangesAndTrafficFailWithoutReplacingAnAcceptedWidgetFrame() {
    ui::SavedHistoryPlot plot; plot.setMonth(End, month()); const auto frame = plot.presentation(); QVERIFY(frame);
    for (const auto date : {core::HistoryDate{1970, 1, 29}, core::HistoryDate{2023, 2, 29}, core::HistoryDate{10000, 1, 1}}) {
      QVERIFY(!ui::savedHistoryMonthPlot(date, {})); plot.setMonth(date, {}); QCOMPARE(plot.presentation(), frame);
    }
    QVERIFY(!ui::savedHistoryMonthPlot(End, {}, static_cast<Traffic>(-1))); plot.setMonth(End, {}, static_cast<Traffic>(99)); QCOMPARE(plot.presentation(), frame);
    QVERIFY(frame->bucketLabel(30).isEmpty()); QVERIFY(frame->bucketToolTip(30).isEmpty()); QVERIFY(!frame->point(30, false, {0, 0, 1, 1}));
  }
  void copiedFramesOutliveAndIgnoreAllMutableSourceAliases() {
    auto source = month(); std::weak_ptr<core::SavedHistoryMonth> weak = source; const auto frame = ui::savedHistoryMonthPlot(End, source, Traffic::Dht); QVERIFY(frame);
    source->days.back().snapshot->totals = core::HistoryCounters{}; source->days.back().date = {1980, 1, 1}; source.reset(); QVERIFY(weak.expired());
    QCOMPARE(frame->buckets().back().date, End); QCOMPARE(frame->buckets().back().downloaded, std::optional<std::int64_t>{-66});
  }
  void signedExtremesStayExactAndFiniteWithoutThirtyDaySummation() {
    auto source = month(); const auto low = std::numeric_limits<std::int64_t>::min(), high = std::numeric_limits<std::int64_t>::max();
    for (auto& row : source->days) { core::SavedHistoryDay day; day.date = row.date; day.totals = core::HistoryCounters{high, high, low, low, high, low}; row.snapshot = day; }
    for (const auto traffic : Kinds) {
      const auto frame = ui::savedHistoryMonthPlot(End, source, traffic); QVERIFY(frame); QCOMPARE(frame->minimum(), low); QCOMPARE(frame->maximum(), high);
      QCOMPARE(frame->bucketToolTip(29).count(QStringLiteral("922337203685477580")), 2);
      for (std::size_t i = 0; i < 30; ++i) { for (bool upload : {false, true}) { const auto point = frame->point(i, upload, {20, 5, 870, 150}); QVERIFY(point); QVERIFY(std::isfinite(point->x())); QVERIFY(std::isfinite(point->y())); QVERIFY(point->x() >= 20 && point->x() <= 890); QVERIFY(point->y() >= 5 && point->y() <= 155); } }
      QVERIFY(!frame->point(0, false, {0, 0, -1, 1}));
    }
  }
  void keyboardTraversesAllThirtyIncludingUnknownAndCopyIsExplicitAndExact() {
    Clipboard clipboard;
    for (const auto traffic : Kinds) {
      ui::SavedHistoryPlot plot; plot.setMonth(End, month(), traffic); show(plot); QTRY_VERIFY(plot.hasFocus()); QVERIFY(!copy(plot).isEnabled());
      QTest::keyClick(&plot, Qt::Key_Right); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{0});
      for (std::size_t i = 1; i < 30; ++i) { QTest::keyClick(&plot, Qt::Key_Right); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{i}); }
      QTest::keyClick(&plot, Qt::Key_Right); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{29});
      QCOMPARE(clipboard.value->text(), QStringLiteral("month offscreen sentinel")); copy(plot).trigger(); QCOMPARE(clipboard.value->text(), plot.presentation()->bucketToolTip(29));
      clipboard.value->setText(QStringLiteral("month offscreen sentinel")); QTest::keyClick(&plot, Qt::Key_Home); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{0});
      const QKeySequence sequence(QKeySequence::Copy); QTest::keyClick(&plot, sequence[0].key(), sequence[0].keyboardModifiers()); QTRY_COMPARE(clipboard.value->text(), plot.presentation()->bucketToolTip(0));
      QVERIFY(clipboard.value->text().contains(QStringLiteral("Unknown"))); clipboard.value->setText(QStringLiteral("month offscreen sentinel"));
    }
  }
  void acceptedReplacementClearsCursorAndDisablesCopyButFocusLossRetainsIt() {
    ui::SavedHistoryPlot plot; plot.setMonth(End, month()); show(plot); QTRY_VERIFY(plot.hasFocus()); QTest::keyClick(&plot, Qt::Key_End);
    plot.clearFocus(); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{29}); QVERIFY(copy(plot).isEnabled());
    plot.setMonth(End, month(), Traffic::Protocol); QVERIFY(!plot.selectedBucket()); QVERIFY(!copy(plot).isEnabled());
  }
  void nestedTooltipReplacementKeepsNewestMonthAndNoStaleCursor() {
    ui::SavedHistoryPlot plot; plot.setMonth(End, month()); show(plot); QTRY_VERIFY(plot.hasFocus()); TipHook hook;
    hook.callback = [&] { plot.setMonth(End, {}, Traffic::Dht); }; plot.installEventFilter(&hook); press(&plot, Qt::Key_End);
    QVERIFY(!hook.armed); QCOMPARE(plot.presentation()->traffic(), Traffic::Dht); QVERIFY(!plot.selectedBucket()); QVERIFY(!copy(plot).isEnabled());
  }
  void tooltipCallbackMayDirectlyDeletePlotWithoutTrailingKeyRelease() {
    auto owner = std::make_unique<ui::SavedHistoryPlot>(); owner->setMonth(End, month()); show(*owner); QTRY_VERIFY(owner->hasFocus()); QPointer guard(owner.get()); TipHook hook;
    hook.consume = true; hook.callback = [&] { owner.reset(); }; owner->installEventFilter(&hook); press(owner.get(), Qt::Key_End); QVERIFY(!hook.armed); QVERIFY(guard.isNull());
  }
};
QTEST_MAIN(HistoryMonthPlotTests)
#include "history_month_plot_tests.moc"
