#include "history_view.hpp"
#include "history_week_view.hpp"
#include "saved_history_plot.hpp"
#include "statistics_page.hpp"

#include <QApplication>
#include <QDateEdit>
#include <QDateTime>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QTableView>
#include <QTabWidget>
#include <QTimeZone>
#include <QtTest>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate Day{2024, 2, 29}, End{2024, 3, 2};
template<class T> T& child(QWidget& owner, const char* name) {
  auto* value = owner.findChild<T*>(QString::fromLatin1(name));
  if (!value) { throw std::runtime_error("Missing saved chart or retained control"); } return *value;
}
QDate qtDate(core::HistoryDate value) { return {value.year, static_cast<int>(value.month), static_cast<int>(value.day)}; }
std::string anchor(core::HistoryDate date, int offset = 0) {
  const auto minute = QDateTime(qtDate(date), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch() / 60 + offset;
  return "s,1," + std::to_string(minute) + ",100,200,300,400,500,600\n";
}
std::shared_ptr<const core::SavedHistoryDay> parsed(core::HistoryDate date, const std::string& text) {
  auto result = core::readSavedHistoryDay(text, date);
  if (!result) { throw std::runtime_error("Finite parser-backed chart fixture rejected"); }
  return std::make_shared<const core::SavedHistoryDay>(*result);
}
std::shared_ptr<const core::SavedHistoryDay> single(core::HistoryDate date = Day, std::int64_t down = 11, std::int64_t up = 7) {
  return parsed(date, anchor(date) + "1000," + std::to_string(up) + ",3000," + std::to_string(down) + ",5000,6000\n");
}
auto dayPacket(std::shared_ptr<const core::SavedHistoryDay> value = single(), quint64 request = 1,
    quint64 session = 1, core::HistoryDate date = Day) {
  auto p = std::make_shared<ui::HistoryPacket>(); p->session = session; p->request = request; p->date = date;
  p->state = ui::HistoryReadState::Ready; p->snapshot = std::move(value); return p;
}
std::shared_ptr<core::SavedHistoryWeek> week(bool known = true) {
  const auto dates = core::savedHistoryWeekDates(End); if (!dates) { throw std::runtime_error("Finite week invalid"); }
  auto result = std::make_shared<core::SavedHistoryWeek>(); result->endingDate = End;
  for (std::size_t i = 0; i < dates->size(); ++i) {
    result->days[i].date = (*dates)[i]; if (known) { result->days[i].snapshot = *single((*dates)[i]); }
  }
  return result;
}
auto weekPacket(std::shared_ptr<const core::SavedHistoryWeek> value = week(), quint64 request = 1, quint64 session = 1) {
  auto p = std::make_shared<ui::HistoryWeekPacket>(); p->session = session; p->request = request; p->endingDate = End;
  p->state = ui::HistoryWeekReadState::Ready; p->snapshot = std::move(value); return p;
}
void show(QWidget& value, int width = 1100) { value.resize(width, 850); value.show(); QCoreApplication::processEvents(); }
void begin(ui::HistoryView& view, quint64 request = 1) {
  child<QDateEdit>(view, "saved-history-date").setDate(qtDate(Day)); view.beginRequest(1, request, Day);
}
void begin(ui::HistoryWeekView& view, quint64 request = 1) {
  child<QDateEdit>(view, "saved-history-week-date").setDate(qtDate(End)); view.beginRequest(1, request, End);
}
ui::SavedHistoryPlot& chart(QWidget& owner, bool weekly = false) {
  return child<ui::SavedHistoryPlot>(owner, weekly ? "saved-history-week-chart" : "saved-history-chart");
}
QAbstractItemModel& model(QWidget& owner, bool weekly = false) {
  return *child<QTableView>(owner, weekly ? "saved-history-week-days" : "saved-history-hours").model();
}
bool unknown(const ui::SavedHistoryPlotPresentation& frame, std::size_t count) {
  if (!frame || frame->buckets().size() != count) { return false; }
  for (const auto& bucket : frame->buckets()) { if (bucket.downloaded || bucket.uploaded) { return false; } }
  return true;
}
class TipHook final : public QObject {
public:
  bool armed = true, consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; } return false;
  }
};
void activate(ui::StatisticsPage& page, bool weekly) {
  page.tabs().setCurrentIndex(1); child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(1);
  child<QTabWidget>(page, "statistics-history-tabs").setCurrentIndex(weekly ? 1 : 0); QCoreApplication::processEvents();
}
} // namespace

class SavedHistoryChartViewTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<core::HistoryDate>();
  }
  void chartsPrecedeRetainedTablesAndAreInitiallyUnknown() {
    ui::HistoryView day; begin(day); show(day); auto& daily = chart(day); auto& table = child<QTableView>(day, "saved-history-hours");
    QVERIFY(daily.geometry().bottom() < table.geometry().top()); QVERIFY(unknown(daily.presentation(), 24));
    QCOMPARE(daily.presentation()->mode(), ui::SavedHistoryPlotMode::Day); QVERIFY(daily.presentation()->endingDate() == Day);
    QCOMPARE(model(day).rowCount(), 24); QCOMPARE(model(day).columnCount(), 7);
    ui::HistoryWeekView seven; begin(seven); show(seven); auto& weekly = chart(seven, true);
    QVERIFY(weekly.geometry().bottom() < child<QTableView>(seven, "saved-history-week-days").geometry().top());
    QVERIFY(unknown(weekly.presentation(), 7)); QCOMPARE(weekly.presentation()->mode(), ui::SavedHistoryPlotMode::Week);
    QCOMPARE(model(seven, true).rowCount(), 7); QCOMPARE(model(seven, true).columnCount(), 8);
  }
  void dailyChartCopiesOnlyPayloadHoursAndKeepsZeroGapsSignedValues() {
    const auto source = parsed(Day, anchor(Day) + "1000,7,3000,11,5000,6000\n" + anchor(Day, 119)
        + "0,0,0,0,0,0\n" + anchor(Day, 180) + "0,-5,0,-9,0,0\n");
    ui::HistoryView view; begin(view); show(view); QSignalSpy reads(&view, &ui::HistoryView::readRequested);
    view.setPresentation(dayPacket(source)); const auto frame = chart(view).presentation(); QVERIFY(frame); QCOMPARE(frame->buckets().size(), std::size_t{24});
    const auto buckets = frame->buckets(); QVERIFY(buckets[0].hour == 0); QVERIFY(buckets[0].date == Day);
    QCOMPARE(buckets[0].downloaded, std::optional<std::int64_t>{11}); QCOMPARE(buckets[0].uploaded, std::optional<std::int64_t>{7});
    QVERIFY(!buckets[1].downloaded && !buckets[1].uploaded);
    QCOMPARE(buckets[2].downloaded, std::optional<std::int64_t>{0}); QCOMPARE(buckets[2].uploaded, std::optional<std::int64_t>{0});
    QCOMPARE(buckets[3].downloaded, std::optional<std::int64_t>{-9}); QCOMPARE(buckets[3].uploaded, std::optional<std::int64_t>{-5});
    QCOMPARE(frame->minimum(), std::int64_t{-9}); QCOMPARE(frame->maximum(), std::int64_t{11}); // Protocol/DHT values are excluded.
    QCOMPARE(model(view).data(model(view).index(0, 4), Qt::UserRole).toLongLong(), qlonglong{11});
    QCOMPARE(model(view).data(model(view).index(0, 2), Qt::UserRole).toLongLong(), qlonglong{7}); QCOMPARE(reads.size(), 0);
  }
  void weekChartMatchesDailyRowsWithoutAnyAggregate() {
    auto source = week(false); source->days[1].snapshot = *parsed(source->days[1].date, "");
    source->days[2].snapshot = *single(source->days[2].date, 0, 0);
    source->days[3].snapshot = *single(source->days[3].date, -4, -2);
    source->days[4].snapshot = *single(source->days[4].date, 44, 22);
    source->days[5].snapshot = *parsed(source->days[5].date, anchor(source->days[5].date));
    source->days[6].snapshot = *single(source->days[6].date, 66, 33);
    ui::HistoryWeekView view; begin(view); show(view); QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested);
    view.setPresentation(weekPacket(source)); const auto frame = chart(view, true).presentation(); QVERIFY(frame); QCOMPARE(frame->buckets().size(), std::size_t{7});
    for (std::size_t i = 0; i < source->days.size(); ++i) {
      const auto& bucket = frame->buckets()[i]; QVERIFY(bucket.date == source->days[i].date); QVERIFY(!bucket.hour);
      if (i == 0 || i == 1 || i == 5) { QVERIFY(!bucket.downloaded && !bucket.uploaded); }
      else {
        QCOMPARE(bucket.downloaded.value(), model(view, true).data(model(view, true).index(static_cast<int>(i), 5), Qt::UserRole).toLongLong());
        QCOMPARE(bucket.uploaded.value(), model(view, true).data(model(view, true).index(static_cast<int>(i), 3), Qt::UserRole).toLongLong());
      }
    }
    QCOMPARE(frame->buckets()[2].downloaded, std::optional<std::int64_t>{0});
    QCOMPARE(frame->buckets()[3].downloaded, std::optional<std::int64_t>{-4}); QCOMPARE(reads.size(), 0);
  }
  void chartIsCurrentWhenDailyModelNotifiesObservers() {
    ui::HistoryView view; begin(view); show(view); bool notified = false, consistent = true;
    connect(&model(view), &QAbstractItemModel::dataChanged, &view, [&] {
      notified = true; const auto frame = chart(view).presentation();
      consistent = consistent && frame && frame->buckets()[0].downloaded == 11 && frame->buckets()[0].uploaded == 7;
    });
    view.setPresentation(dayPacket()); QVERIFY(notified); QVERIFY(consistent);
  }
  void chartIsCurrentWhenWeeklyModelNotifiesObservers() {
    ui::HistoryWeekView view; begin(view); show(view); bool notified = false, consistent = true;
    connect(&model(view, true), &QAbstractItemModel::dataChanged, &view, [&] {
      notified = true; const auto frame = chart(view, true).presentation();
      consistent = consistent && frame && frame->buckets()[0].downloaded == 11 && frame->buckets()[0].uploaded == 7;
    });
    view.setPresentation(weekPacket()); QVERIFY(notified); QVERIFY(consistent);
  }
  void pendingMissingFailedAndProfileResetClearDailyChart() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(dayPacket()); view.beginRequest(1, 2, Day);
    QVERIFY(unknown(chart(view).presentation(), 24)); auto missing = dayPacket({}, 2); missing->state = ui::HistoryReadState::Missing;
    view.setPresentation(missing); QVERIFY(unknown(chart(view).presentation(), 24));
    view.beginRequest(1, 3, Day); view.setPresentation(dayPacket(single(), 3)); QVERIFY(chart(view).presentation()->buckets()[0].downloaded.has_value());
    view.beginRequest(1, 4, Day); view.setFailed(1, 4, Day, QStringLiteral("Read failed")); QVERIFY(unknown(chart(view).presentation(), 24));
    view.beginRequest(1, 5, Day); view.setPresentation(dayPacket(single(), 5)); view.resetSession(2); QVERIFY(unknown(chart(view).presentation(), 24));
    QVERIFY(!model(view).data(model(view).index(0, 4), Qt::UserRole).isValid());
  }
  void missingEmptyAndFailedWeekNeverBecomeZeroLines() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(weekPacket(week(false))); QVERIFY(unknown(chart(view, true).presentation(), 7));
    auto empty = week(false); for (auto& day : empty->days) { day.snapshot = *parsed(day.date, ""); }
    view.beginRequest(1, 2, End); view.setPresentation(weekPacket(empty, 2)); QVERIFY(unknown(chart(view, true).presentation(), 7));
    view.beginRequest(1, 3, End); view.setPresentation(weekPacket(week(), 3)); QVERIFY(chart(view, true).presentation()->buckets()[0].downloaded.has_value());
    view.beginRequest(1, 4, End); view.setFailed(1, 4, End, QStringLiteral("Read failed")); QVERIFY(unknown(chart(view, true).presentation(), 7));
    view.beginRequest(1, 5, End); view.setPresentation(weekPacket(week(), 5)); view.resetSession(2); QVERIFY(unknown(chart(view, true).presentation(), 7));
  }
  void newSelectedDatesReplacePlotIdentityWithUnknownBuckets() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(dayPacket());
    child<QDateEdit>(view, "saved-history-date").setDate(QDate(2024, 3, 1)); QVERIFY(unknown(chart(view).presentation(), 24));
    QVERIFY(chart(view).presentation()->endingDate() == (core::HistoryDate{2024, 3, 1}));
    ui::HistoryWeekView seven; begin(seven); show(seven); seven.setPresentation(weekPacket());
    child<QDateEdit>(seven, "saved-history-week-date").setDate(QDate(2024, 3, 3)); QVERIFY(unknown(chart(seven, true).presentation(), 7));
    QVERIFY(chart(seven, true).presentation()->endingDate() == (core::HistoryDate{2024, 3, 3}));
  }
  void staleSessionRequestAndDateCannotReplaceEitherPlot() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(dayPacket()); const auto original = chart(view).presentation();
    view.setPresentation(dayPacket(single(Day, 99, 88), 1, 2)); view.setPresentation(dayPacket(single(Day, 99, 88), 0));
    auto wrongDate = dayPacket(single(Day, 99, 88)); wrongDate->date.day = 28; view.setPresentation(wrongDate); QVERIFY(chart(view).presentation() == original);
    ui::HistoryWeekView seven; begin(seven); show(seven); seven.setPresentation(weekPacket()); const auto weekly = chart(seven, true).presentation();
    seven.setPresentation(weekPacket(week(false), 1, 2)); seven.setPresentation(weekPacket(week(false), 0));
    auto wrongEnd = weekPacket(week(false)); wrongEnd->endingDate.day = 1; seven.setPresentation(wrongEnd); QVERIFY(chart(seven, true).presentation() == weekly);
  }
  void hiddenDailyDefersPlotFactsTooltipAndTableUpdates() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(dayPacket()); const auto original = chart(view).presentation(); const auto tip = chart(view).toolTip();
    view.hide(); QSignalSpy changes(&model(view), &QAbstractItemModel::dataChanged); view.beginRequest(1, 2, Day);
    view.setPresentation(dayPacket(single(Day, 99, 88), 2)); QCoreApplication::processEvents();
    QVERIFY(chart(view).presentation() == original); QCOMPARE(chart(view).toolTip(), tip); QCOMPARE(changes.size(), 0);
    show(view); QCOMPARE(chart(view).presentation()->buckets()[0].downloaded, std::optional<std::int64_t>{99}); QCOMPARE(changes.size(), 1);
  }
  void hiddenWeekDefersPlotFactsTooltipAndTableUpdates() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(weekPacket()); const auto original = chart(view, true).presentation(); const auto tip = chart(view, true).toolTip();
    view.hide(); QSignalSpy changes(&model(view, true), &QAbstractItemModel::dataChanged); view.beginRequest(1, 2, End); view.setPresentation(weekPacket(week(false), 2)); QCoreApplication::processEvents();
    QVERIFY(chart(view, true).presentation() == original); QCOMPARE(chart(view, true).toolTip(), tip); QCOMPARE(changes.size(), 0);
    show(view); QVERIFY(unknown(chart(view, true).presentation(), 7)); QCOMPARE(changes.size(), 1);
  }
  void minimizedDailyDefersPlotThenRestoreShowsLatestUnknown() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(dayPacket()); view.showMinimized(); QCoreApplication::processEvents(); QVERIFY(view.isMinimized());
    const auto original = chart(view).presentation(); const auto tip = chart(view).toolTip(); view.beginRequest(1, 2, Day); view.setFailed(1, 2, Day, QStringLiteral("latest failure"));
    QCoreApplication::processEvents(); QVERIFY(chart(view).presentation() == original); QCOMPARE(chart(view).toolTip(), tip);
    view.showNormal(); QTRY_VERIFY(unknown(chart(view).presentation(), 24)); QVERIFY(chart(view).presentation() != original);
  }
  void minimizedWeekDefersPlotThenRestoreShowsLatestUnknown() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(weekPacket()); view.showMinimized(); QCoreApplication::processEvents(); QVERIFY(view.isMinimized());
    const auto original = chart(view, true).presentation(); const auto tip = chart(view, true).toolTip(); view.beginRequest(1, 2, End); view.setFailed(1, 2, End, QStringLiteral("latest failure"));
    QCoreApplication::processEvents(); QVERIFY(chart(view, true).presentation() == original); QCOMPARE(chart(view, true).toolTip(), tip);
    view.showNormal(); QTRY_VERIFY(unknown(chart(view, true).presentation(), 7)); QVERIFY(chart(view, true).presentation() != original);
  }
  void plotTooltipReentryPreservesNewDailyFrameAndStopsOldModelWrite() {
    ui::HistoryView view; begin(view); show(view); auto& plot = chart(view); plot.setToolTip(QStringLiteral("before hook")); TipHook hook;
    std::vector<qlonglong> seen; connect(&model(view), &QAbstractItemModel::dataChanged, &view, [&] {
      seen.push_back(model(view).data(model(view).index(0, 4), Qt::UserRole).toLongLong());
    });
    hook.callback = [&] { view.beginRequest(1, 2, Day); view.setPresentation(dayPacket(single(Day, 99, 88), 2)); }; plot.installEventFilter(&hook);
    view.setPresentation(dayPacket()); QVERIFY(!hook.armed); QCOMPARE(view.presentation()->request, quint64{2});
    QCOMPARE(plot.presentation()->buckets()[0].downloaded, std::optional<std::int64_t>{99}); QCOMPARE(plot.presentation()->buckets()[0].uploaded, std::optional<std::int64_t>{88});
    QCOMPARE(seen.size(), std::size_t{1}); QCOMPARE(seen.front(), qlonglong{99});
  }
  void plotTooltipReentryPreservesNewWeekFrameAndStopsOldModelWrite() {
    ui::HistoryWeekView view; begin(view); show(view); auto& plot = chart(view, true); plot.setToolTip(QStringLiteral("before hook")); TipHook hook;
    auto latest = week(); latest->days[0].snapshot = *single(latest->days[0].date, 77, 66); std::vector<qlonglong> seen;
    connect(&model(view, true), &QAbstractItemModel::dataChanged, &view, [&] { seen.push_back(model(view, true).data(model(view, true).index(0, 5), Qt::UserRole).toLongLong()); });
    hook.callback = [&] { view.beginRequest(1, 2, End); view.setPresentation(weekPacket(latest, 2)); }; plot.installEventFilter(&hook);
    view.setPresentation(weekPacket()); QVERIFY(!hook.armed); QCOMPARE(view.presentation()->request, quint64{2});
    QCOMPARE(plot.presentation()->buckets()[0].downloaded, std::optional<std::int64_t>{77}); QCOMPARE(plot.presentation()->buckets()[0].uploaded, std::optional<std::int64_t>{66});
    QCOMPARE(seen.size(), std::size_t{1}); QCOMPARE(seen.front(), qlonglong{77});
  }
  void plotTooltipCanDeleteDailyOwnerBeforeTableUpdate() {
    auto* view = new ui::HistoryView; begin(*view); show(*view); QPointer<ui::HistoryView> guard(view); auto& plot = chart(*view); plot.setToolTip(QStringLiteral("before hook")); TipHook hook;
    hook.consume = true; hook.callback = [view] { delete view; }; plot.installEventFilter(&hook); view->setPresentation(dayPacket()); QVERIFY(!guard);
  }
  void plotTooltipCanDeleteWeeklyOwnerBeforeTableUpdate() {
    auto* view = new ui::HistoryWeekView; begin(*view); show(*view); QPointer<ui::HistoryWeekView> guard(view); auto& plot = chart(*view, true); plot.setToolTip(QStringLiteral("before hook")); TipHook hook;
    hook.consume = true; hook.callback = [view] { delete view; }; plot.installEventFilter(&hook); view->setPresentation(weekPacket()); QVERIFY(!guard);
  }
  void hiddenDailyAndVisibleWeekStayIndependentWithoutExtraRequests() {
    ui::StatisticsPage page; show(page); page.resetHistorySession(1);
    auto& day = child<ui::HistoryView>(page, "statistics-history-view"); auto& seven = child<ui::HistoryWeekView>(page, "statistics-history-week-view");
    begin(day); begin(seven); QSignalSpy daily(&page, &ui::StatisticsPage::historyRequested), weekly(&page, &ui::StatisticsPage::historyWeekRequested);
    activate(page, false); page.setHistory(dayPacket()); const auto original = chart(day).presentation(); QCOMPARE(daily.size(), 0); QCOMPARE(weekly.size(), 0);
    activate(page, true); page.setHistoryWeek(weekPacket()); QCOMPARE(weekly.size(), 0); QVERIFY(chart(day).presentation() == original);
    const auto weekFrame = chart(seven, true).presentation(); QVERIFY(weekFrame->buckets()[0].downloaded.has_value());
    day.beginRequest(1, 2, Day); day.setPresentation(dayPacket(single(Day, 99, 88), 2)); QVERIFY(chart(day).presentation() == original);
    QVERIFY(chart(seven, true).presentation() == weekFrame); activate(page, false);
    QCOMPARE(chart(day).presentation()->buckets()[0].downloaded, std::optional<std::int64_t>{99}); QVERIFY(chart(seven, true).presentation() == weekFrame);
    QCOMPARE(daily.size(), 0); QCOMPARE(weekly.size(), 0);
  }
  void plotTooltipCanDeleteWholeStatisticsPage() {
    auto* page = new ui::StatisticsPage; show(*page); page->resetHistorySession(1); auto& day = child<ui::HistoryView>(*page, "statistics-history-view"); begin(day); activate(*page, false);
    auto& plot = chart(day); plot.setToolTip(QStringLiteral("before hook")); TipHook hook; QPointer<ui::StatisticsPage> guard(page);
    hook.consume = true; hook.callback = [page] { delete page; }; plot.installEventFilter(&hook); page->setHistory(dayPacket()); QVERIFY(!guard);
  }
  void narrowViewRetainsTablesAndFiniteFullSignedGeometry() {
    ui::HistoryView view; begin(view); show(view, 480);
    view.setPresentation(dayPacket(single(Day, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max())));
    auto& plot = chart(view); const auto frame = plot.presentation(); QVERIFY(frame);
    QCOMPARE(frame->minimum(), std::numeric_limits<std::int64_t>::min()); QCOMPARE(frame->maximum(), std::numeric_limits<std::int64_t>::max());
    QVERIFY(plot.height() >= 180 && plot.height() <= 260); QVERIFY(plot.width() > 0); const auto area = plot.plotRect(); QVERIFY(area.isValid());
    for (const bool uploaded : {false, true}) {
      const auto point = frame->point(0, uploaded, area); QVERIFY(point); QVERIFY(std::isfinite(point->x()) && std::isfinite(point->y()));
      QVERIFY(area.adjusted(-0.1, -0.1, 0.1, 0.1).contains(*point));
    }
    QVERIFY(child<QTableView>(view, "saved-history-hours").height() >= 150); QCOMPARE(model(view).rowCount(), 24);
  }
};

QTEST_MAIN(SavedHistoryChartViewTests)
#include "saved_history_chart_view_tests.moc"
