#include "history_month_view.hpp"
#include "saved_history_plot.hpp"
#include "statistics_page.hpp"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTabWidget>
#include <QTimeZone>
#include <QtTest>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate End{2024, 3, 2};
template<class T> T& child(QObject& root, const char* id) {
  auto* found = root.findChild<T*>(QString::fromLatin1(id)); if (!found) { throw std::runtime_error("Missing thirty-day control"); } return *found;
}
QDate qtDate(core::HistoryDate date) { return {date.year, static_cast<int>(date.month), static_cast<int>(date.day)}; }
void show(QWidget& view) { view.resize(1180, 850); view.show(); QCoreApplication::processEvents(); }
QAbstractItemModel& model(QWidget& view) { return *child<QTableView>(view, "saved-history-month-days").model(); }
QString status(QWidget& view) { return child<QLabel>(view, "saved-history-month-status").text(); }
std::string anchor(core::HistoryDate date) {
  const auto minute = QDateTime(qtDate(date), QTime(9, 0), QTimeZone::utc()).toMSecsSinceEpoch() / 60000;
  return "s,1," + std::to_string(minute) + ",100,200,300,400,500,600\n";
}
core::SavedHistoryDay parsed(core::HistoryDate date, const std::string& records) {
  auto value = core::readSavedHistoryDay(records, date); if (!value) { throw std::runtime_error("Invalid generated original-format day"); } return std::move(*value);
}
std::shared_ptr<core::SavedHistoryMonth> month(core::HistoryDate end = End, bool observed = true) {
  const auto dates = core::savedHistoryMonthDates(end); if (!dates) { throw std::runtime_error("Invalid generated thirty-day range"); }
  auto source = std::make_shared<core::SavedHistoryMonth>(); source->endingDate = end;
  for (std::size_t i = 0; i < dates->size(); ++i) { source->days[i].date = (*dates)[i]; if (observed) { source->days[i].snapshot = parsed((*dates)[i], anchor((*dates)[i]) + "1,2,3,4,5,6\n"); } }
  return source;
}
auto packet(std::shared_ptr<core::SavedHistoryMonth> source = month(), quint64 request = 1, quint64 session = 1, core::HistoryDate end = End) {
  auto result = std::make_shared<ui::HistoryMonthPacket>(); result->session = session; result->request = request; result->endingDate = end;
  result->state = ui::HistoryMonthReadState::Ready; result->snapshot = std::move(source); return result;
}
void begin(ui::HistoryMonthView& view, quint64 request = 1) { child<QDateEdit>(view, "saved-history-month-date").setDate(qtDate(End)); view.beginRequest(1, request, End); }
void choose(ui::StatisticsPage& page, int index) {
  child<QTabWidget>(page, "statistics-history-tabs").setCurrentIndex(index); page.tabs().setCurrentIndex(1);
  child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(1); QCoreApplication::processEvents();
}
class TipHook final : public QObject {
public:
  bool armed = true, consume = false; std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; } return false;
  }
};
} // namespace

class HistoryMonthViewUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); qRegisterMetaType<core::HistoryDate>(); }
  void initialUtcBoundsAndThirtyByEightReadOnlyModel() {
    const auto first = QDateTime::currentDateTimeUtc().date(); ui::HistoryMonthView view; const auto last = QDateTime::currentDateTimeUtc().date();
    QVERIFY(qtDate(view.selectedDate()) == first || qtDate(view.selectedDate()) == last);
    auto& date = child<QDateEdit>(view, "saved-history-month-date"); QCOMPARE(date.minimumDate(), QDate(1970, 1, 30)); QCOMPARE(date.maximumDate(), QDate(9999, 12, 31)); QVERIFY(date.calendarPopup());
    QAbstractItemModelTester tester(&model(view), QAbstractItemModelTester::FailureReportingMode::QtTest); QCOMPARE(model(view).rowCount(), 30); QCOMPARE(model(view).columnCount(), 8);
    QCOMPARE(model(view).rowCount(model(view).index(0, 0)), 0); QVERIFY(!child<QTableView>(view, "saved-history-month-days").isSortingEnabled());
    for (int row = 0; row < 30; ++row) { QCOMPARE(model(view).data(model(view).index(row, 1)).toString(), QStringLiteral("Not loaded")); for (int col = 2; col < 8; ++col) { QVERIFY(!model(view).data(model(view).index(row, col), Qt::UserRole).isValid()); QVERIFY(!(model(view).flags(model(view).index(row, col)) & Qt::ItemIsEditable)); } }
    QVERIFY(!view.presentation());
  }
  void dailyDefaultAndThreeIndependentLazyDatesArePreserved() {
    ui::StatisticsPage page; page.resetHistorySession(1); QSignalSpy days(&page, &ui::StatisticsPage::historyRequested), weeks(&page, &ui::StatisticsPage::historyWeekRequested), months(&page, &ui::StatisticsPage::historyMonthRequested);
    child<QDateEdit>(page, "saved-history-month-date").setDate(qtDate(End)); show(page);
    QCOMPARE(page.tabs().count(), 2); auto& periods = child<QTabWidget>(page, "statistics-history-tabs"); QCOMPARE(periods.count(), 4); QCOMPARE(periods.currentIndex(), 0); QCOMPARE(periods.tabText(2), QStringLiteral("30 days"));
    QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QCOMPARE(months.count(), 0);
    choose(page, 2); QVERIFY(page.historyMonthSelected()); QVERIFY(!page.historySelected()); QVERIFY(!page.historyWeekSelected()); QCOMPARE(months.count(), 1); QCOMPARE(qvariant_cast<core::HistoryDate>(months.at(0).at(0)), End);
    QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); choose(page, 0); QCOMPARE(days.count(), 1); choose(page, 1); QCOMPARE(weeks.count(), 1); choose(page, 2); QCOMPARE(months.count(), 1);
    child<QPushButton>(page, "saved-history-month-refresh").click(); QCOMPARE(months.count(), 2);
    QSignalSpy totals(&page, &ui::StatisticsPage::refreshRequested); child<QPushButton>(page, "statistics-refresh").click(); QCOMPARE(totals.count(), 1); QCOMPARE(months.count(), 2);
  }
  void hiddenDateEditsReadOnlyTheLatestDateWhenTheMonthTabReturns() {
    ui::StatisticsPage page; show(page); QSignalSpy reads(&page, &ui::StatisticsPage::historyMonthRequested);
    child<QDateEdit>(page, "saved-history-month-date").setDate(qtDate(End)); QCOMPARE(reads.count(), 0); choose(page, 2); QCOMPARE(reads.count(), 1);
    choose(page, 0); child<QDateEdit>(page, "saved-history-month-date").setDate(QDate(2024, 4, 1)); child<QDateEdit>(page, "saved-history-month-date").setDate(QDate(2024, 5, 1));
    QCOMPARE(reads.count(), 1); choose(page, 2); QCOMPARE(reads.count(), 2); QCOMPARE(qvariant_cast<core::HistoryDate>(reads.at(1).at(0)), (core::HistoryDate{2024, 5, 1}));
  }
  void previousNextAreFixedThirtyDatesAndNeverClampToShortRanges() {
    ui::HistoryMonthView view(nullptr, End); show(view); QSignalSpy reads(&view, &ui::HistoryMonthView::readRequested);
    child<QPushButton>(view, "saved-history-month-previous").click(); QCOMPARE(view.selectedDate(), (core::HistoryDate{2024, 2, 1}));
    child<QPushButton>(view, "saved-history-month-next").click(); QCOMPARE(view.selectedDate(), End); QCOMPARE(reads.count(), 2);
    auto& date = child<QDateEdit>(view, "saved-history-month-date"); date.setDate(QDate(1970, 1, 30)); QVERIFY(!child<QPushButton>(view, "saved-history-month-previous").isEnabled());
    date.setDate(QDate(1970, 2, 28)); QVERIFY(!child<QPushButton>(view, "saved-history-month-previous").isEnabled()); date.setDate(QDate(1970, 3, 1)); QVERIFY(child<QPushButton>(view, "saved-history-month-previous").isEnabled());
    date.setDate(QDate(9999, 12, 31)); QVERIFY(!child<QPushButton>(view, "saved-history-month-next").isEnabled());
  }
  void missingEmptyHeaderZeroAndAllSixSignedExtremesRemainDistinct() {
    auto source = month(End, false); source->days[1].snapshot = parsed(source->days[1].date, "");
    source->days[2].snapshot = parsed(source->days[2].date, anchor(source->days[2].date)); source->days[3].snapshot = parsed(source->days[3].date, anchor(source->days[3].date) + "0,0,0,0,0,0\n");
    source->days[29].snapshot = parsed(End, anchor(End) + "-9223372036854775808,-2,3,9223372036854775807,-5,6\n");
    ui::HistoryMonthView view; begin(view); show(view); view.setPresentation(packet(source));
    QCOMPARE(model(view).data(model(view).index(0, 0)).toString(), QStringLiteral("2024-02-02 UTC")); QCOMPARE(model(view).data(model(view).index(27, 0)).toString(), QStringLiteral("2024-02-29 UTC"));
    QCOMPARE(model(view).data(model(view).index(0, 1)).toString(), QStringLiteral("Missing")); QCOMPARE(model(view).data(model(view).index(1, 1)).toString(), QStringLiteral("No observations")); QCOMPARE(model(view).data(model(view).index(2, 1)).toString(), QStringLiteral("No observations"));
    const std::array<qlonglong, 6> expected{std::numeric_limits<qlonglong>::min(), -2, 3, std::numeric_limits<qlonglong>::max(), -5, 6};
    for (int col = 2; col < 8; ++col) {
      const auto cell = model(view).index(29, col); QCOMPARE(model(view).data(cell, Qt::UserRole).toLongLong(), expected[static_cast<std::size_t>(col - 2)]);
      QCOMPARE(model(view).data(cell).toString(), QStringLiteral("%1 bytes").arg(expected[static_cast<std::size_t>(col - 2)]));
      QVERIFY(model(view).data(cell, Qt::ToolTipRole).toString().contains(QString::number(expected[static_cast<std::size_t>(col - 2)])));
      QCOMPARE(model(view).data(model(view).index(3, col)).toString(), QStringLiteral("0 bytes")); QVERIFY(!model(view).data(model(view).index(2, col), Qt::UserRole).isValid());
    }
    QCOMPARE(child<QLabel>(view, "saved-history-month-status").textFormat(), Qt::PlainText); QVERIFY(child<QLabel>(view, "saved-history-month-scope").text().contains(QStringLiteral("No 30-day total")));
  }
  void thirtyMaximumValuesAreDisplayedIndividuallyWithoutAWholePeriodTotal() {
    auto source = month(); for (auto& row : source->days) { row.snapshot = parsed(row.date, anchor(row.date) + "0,0,0,9223372036854775807,0,0\n"); }
    ui::HistoryMonthView view; begin(view); show(view); view.setPresentation(packet(source));
    QCOMPARE(view.presentation()->state, ui::HistoryMonthReadState::Ready); for (int row = 0; row < 30; ++row) { QCOMPARE(model(view).data(model(view).index(row, 5), Qt::UserRole).toLongLong(), std::numeric_limits<qlonglong>::max()); }
    QVERIFY(!view.findChild<QLabel*>(QStringLiteral("saved-history-month-total")));
  }
  void requestSessionDateAndOnceOnlyGuardsCannotSettleANewerRead() {
    ui::HistoryMonthView view; begin(view, 4); show(view); view.setPresentation(packet(month(), 3)); view.setPresentation(packet(month(), 4, 2)); view.setPresentation(packet(month(), 4, 1, {2024, 3, 3})); QVERIFY(!view.presentation());
    const auto accepted = packet(month(), 4); view.setPresentation(accepted); QCOMPARE(view.presentation(), accepted); view.setPresentation(packet(month(End, false), 4)); QCOMPARE(view.presentation(), accepted);
    view.beginRequest(0, 5, End); view.beginRequest(1, 0, End); view.beginRequest(1, 4, End); QCOMPARE(view.presentation(), accepted);
    view.resetSession(2); QVERIFY(!view.presentation()); view.beginRequest(1, 5, End); view.setPresentation(accepted); QVERIFY(!view.presentation()); view.beginRequest(2, 1, End); view.setPresentation(packet(month(), 1, 2)); QVERIFY(view.presentation());
  }
  void malformedReadyBatchIsAnExplicitWholeReadFailure_data() { QTest::addColumn<int>("kind"); for (int i = 0; i < 9; ++i) { QTest::newRow(qPrintable(QStringLiteral("binding-%1").arg(i))) << i; } }
  void malformedReadyBatchIsAnExplicitWholeReadFailure() {
    QFETCH(int, kind); auto source = month(); auto result = packet(source); ui::HistoryMonthView view; begin(view); show(view);
    switch (kind) {
      case 0: result->snapshot.reset(); break; case 1: source->endingDate.day = 3; break; case 2: std::swap(source->days[0], source->days[1]); break;
      case 3: source->days[0].snapshot->date = End; break; case 4: source->days[0].snapshot->totals.reset(); break;
      case 5: source->days[0].snapshot->hours = {}; break; case 6: source->days[0].snapshot->observations = core::SavedHistoryMaximumRecords + 1; break;
      case 7: result->failure = QStringLiteral("contradictory Ready"); break; default: result->state = static_cast<ui::HistoryMonthReadState>(55); break;
    }
    view.setPresentation(result); QVERIFY(view.presentation()); QCOMPARE(view.presentation()->state, ui::HistoryMonthReadState::Failed);
    for (int row = 0; row < 30; ++row) { QVERIFY(!model(view).data(model(view).index(row, 5), Qt::UserRole).isValid()); }
  }
  void failedReadBoundsPlainTextAndClearsPreviouslyObservedValues() {
    ui::HistoryMonthView view; begin(view); show(view); view.setPresentation(packet()); view.beginRequest(1, 2, End);
    const auto error = QStringLiteral("<b>literal</b>") + QString(10000, QLatin1Char('x')); view.setFailed(1, 2, End, error); QCOMPARE(status(view), error.left(8192));
    QCOMPARE(child<QLabel>(view, "saved-history-month-status").textFormat(), Qt::PlainText); QCOMPARE(model(view).data(model(view).index(0, 1)).toString(), QStringLiteral("Unavailable")); QVERIFY(!model(view).data(model(view).index(0, 5), Qt::UserRole).isValid());
  }
  void trafficChangesCoalesceRenderWithoutReadsOrTableNotifications() {
    ui::HistoryMonthView view; begin(view); show(view); const auto result = packet(); view.setPresentation(result);
    auto& plot = child<ui::SavedHistoryPlot>(view, "saved-history-month-chart"); QSignalSpy reads(&view, &ui::HistoryMonthView::readRequested), changes(&model(view), &QAbstractItemModel::dataChanged), resets(&model(view), &QAbstractItemModel::modelReset);
    const auto old = plot.presentation(); auto& choice = child<QComboBox>(view, "saved-history-month-traffic"); choice.setCurrentIndex(1); choice.setCurrentIndex(2); QCOMPARE(plot.presentation(), old);
    QTRY_COMPARE(plot.presentation()->traffic(), ui::SavedHistoryTraffic::Dht); QCOMPARE(plot.presentation()->buckets().back().downloaded, std::optional<std::int64_t>{6}); QCOMPARE(plot.presentation()->buckets().back().uploaded, std::optional<std::int64_t>{5});
    QCOMPARE(view.presentation(), result); QCOMPARE(reads.count(), 0); QCOMPARE(changes.count(), 0); QCOMPARE(resets.count(), 0);
  }
  void hiddenAndMinimizedCacheWithoutVisibleMutationsThenRenderLatest() {
    ui::HistoryMonthView view; begin(view); show(view); view.setPresentation(packet()); const auto prior = status(view); QSignalSpy changes(&model(view), &QAbstractItemModel::dataChanged); view.hide();
    view.beginRequest(1, 2, End); view.setPresentation(packet(month(End, false), 2)); QCoreApplication::processEvents(); QCOMPARE(changes.count(), 0); QCOMPARE(status(view), prior);
    QCOMPARE(model(view).data(model(view).index(0, 1)).toString(), QStringLiteral("Observed")); show(view); QCOMPARE(changes.count(), 1); QCOMPARE(model(view).data(model(view).index(0, 1)).toString(), QStringLiteral("Missing"));
    view.showMinimized(); QCoreApplication::processEvents(); QVERIFY(view.isMinimized()); const auto hiddenStatus = status(view); changes.clear(); view.beginRequest(1, 3, End); view.setFailed(1, 3, End, QStringLiteral("newest failure")); QCoreApplication::processEvents(); QCOMPARE(changes.count(), 0); QCOMPARE(status(view), hiddenStatus);
    view.showNormal(); QTRY_COMPARE(status(view), QStringLiteral("newest failure")); QCOMPARE(changes.count(), 1);
  }
  void aggregateNavigationInvalidatesAllChoicesAndHiddenMonthChoicesRestore() {
    ui::StatisticsNavigation value{.section = 0, .activity = 1, .transfers = 1, .history = 2, .dailyDate = {2024, 2, 29}, .weekEnding = End, .hourly = true, .monthEnding = End, .monthTraffic = ui::SavedHistoryTraffic::Dht};
    ui::StatisticsPage restored(nullptr, value); QCOMPARE(restored.navigation(), std::optional<ui::StatisticsNavigation>{value}); QCOMPARE(child<QDateEdit>(restored, "saved-history-month-date").date(), qtDate(End)); QCOMPARE(child<QComboBox>(restored, "saved-history-month-traffic").currentIndex(), 2);
    QSignalSpy reads(&restored, &ui::StatisticsPage::historyMonthRequested); show(restored); QCOMPARE(reads.count(), 0); restored.tabs().setCurrentIndex(1); QCOMPARE(reads.count(), 1);
    const auto before = QDateTime::currentDateTimeUtc().date();
    for (int kind = 0; kind < 3; ++kind) { auto bad = value; if (kind == 0) { bad.history = 4; } else if (kind == 1) { bad.monthEnding = {1970, 1, 29}; } else { bad.monthTraffic = static_cast<ui::SavedHistoryTraffic>(88); } QVERIFY(!ui::validStatisticsNavigation(bad)); ui::StatisticsPage defaults(nullptr, bad); QVERIFY(defaults.navigation()); QCOMPARE(defaults.navigation()->section, 0); QCOMPARE(defaults.navigation()->activity, 0); QCOMPARE(defaults.navigation()->history, 0); QCOMPARE(defaults.navigation()->monthTraffic, ui::SavedHistoryTraffic::Payload); const auto current = QDateTime::currentDateTimeUtc().date(); QVERIFY(qtDate(defaults.navigation()->monthEnding) == before || qtDate(defaults.navigation()->monthEnding) == current); }
  }
  void pageSessionResetClearsAllThreeResultsWhileSavedTotalsRefreshKeepsThem() {
    ui::StatisticsPage page; page.resetHistorySession(1);
    child<QDateEdit>(page, "saved-history-date").setDate(qtDate(End)); child<QDateEdit>(page, "saved-history-week-date").setDate(qtDate(End)); child<QDateEdit>(page, "saved-history-month-date").setDate(qtDate(End)); show(page);
    choose(page, 0); page.beginHistoryRequest(1, 1, End); auto day = std::make_shared<ui::HistoryPacket>(); day->session = 1; day->request = 1; day->date = End; day->state = ui::HistoryReadState::Ready; day->snapshot = std::make_shared<const core::SavedHistoryDay>(parsed(End, anchor(End) + "1,2,3,4,5,6\n")); page.setHistory(day);
    auto weekSource = std::make_shared<core::SavedHistoryWeek>(); weekSource->endingDate = End; const auto dates = core::savedHistoryWeekDates(End); QVERIFY(dates); for (std::size_t i = 0; i < dates->size(); ++i) { weekSource->days[i].date = (*dates)[i]; }
    choose(page, 1); page.beginHistoryWeekRequest(1, 2, End); auto week = std::make_shared<ui::HistoryWeekPacket>(); week->session = 1; week->request = 2; week->endingDate = End; week->state = ui::HistoryWeekReadState::Ready; week->snapshot = weekSource; page.setHistoryWeek(week);
    choose(page, 2); page.beginHistoryMonthRequest(1, 3, End); const auto monthly = packet(month(), 3); page.setHistoryMonth(monthly);
    QCOMPARE(page.historyPresentation(), day); QCOMPARE(page.historyWeekPresentation(), week); QCOMPARE(page.historyMonthPresentation(), monthly);
    page.beginRequest(1, 10); page.setFailed(1, 10, QStringLiteral("independent saved totals failure")); QCOMPARE(page.historyPresentation(), day); QCOMPARE(page.historyWeekPresentation(), week); QCOMPARE(page.historyMonthPresentation(), monthly);
    QSignalSpy days(&page, &ui::StatisticsPage::historyRequested), weeks(&page, &ui::StatisticsPage::historyWeekRequested), months(&page, &ui::StatisticsPage::historyMonthRequested);
    page.resetHistorySession(2); QVERIFY(!page.historyPresentation()); QVERIFY(!page.historyWeekPresentation()); QVERIFY(!page.historyMonthPresentation()); QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QCOMPARE(months.count(), 0); QCOMPARE(page.selectedHistoryMonthDate(), End);
  }
  void modelNotificationReentryKeepsNewerFailureAndCanDeleteOwner() {
    ui::HistoryMonthView view; begin(view); show(view); bool fired = false; connect(&model(view), &QAbstractItemModel::dataChanged, &view, [&] { if (!fired) { fired = true; view.beginRequest(1, 2, End); view.setFailed(1, 2, End, QStringLiteral("newer result")); } });
    view.setPresentation(packet()); QVERIFY(fired); QCOMPARE(view.presentation()->request, quint64{2}); QCOMPARE(status(view), QStringLiteral("newer result")); QVERIFY(!model(view).data(model(view).index(0, 5), Qt::UserRole).isValid());
    auto* owner = new ui::HistoryMonthView; begin(*owner); show(*owner); QPointer guard(owner); connect(&model(*owner), &QAbstractItemModel::dataChanged, qApp, [owner] { delete owner; }); owner->setPresentation(packet()); QVERIFY(guard.isNull());
  }
  void queuedTrafficTooltipMayDeleteViewAfterComboSetterHasReturned() {
    auto* owner = new ui::HistoryMonthView; begin(*owner); show(*owner); owner->setPresentation(packet()); QPointer guard(owner); TipHook hook; hook.consume = true; hook.callback = [owner] { delete owner; };
    child<ui::SavedHistoryPlot>(*owner, "saved-history-month-chart").installEventFilter(&hook); child<QComboBox>(*owner, "saved-history-month-traffic").setCurrentIndex(1); QVERIFY(guard); QTRY_VERIFY(guard.isNull()); QVERIFY(!hook.armed);
  }
};
QTEST_MAIN(HistoryMonthViewUiTests)
#include "history_month_view_ui_tests.moc"
