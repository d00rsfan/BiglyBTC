#include "history_week_view.hpp"
#include "history_view.hpp"
#include "statistics_page.hpp"

#include <QAbstractItemModelTester>
#include <QApplication>
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
template<class T> T& child(QWidget& owner, const char* name) {
  auto* result = owner.findChild<T*>(QString::fromLatin1(name));
  if (!result) { throw std::runtime_error("Missing semantic seven-day control"); } return *result;
}
QDate qtDate(core::HistoryDate date) { return {date.year, static_cast<int>(date.month), static_cast<int>(date.day)}; }
core::HistoryDate savedDate(QDate date) { return {date.year(), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day())}; }
void show(QWidget& view) { view.resize(1100, 850); view.show(); QCoreApplication::processEvents(); }
void select(ui::HistoryWeekView& view, core::HistoryDate date = End) { child<QDateEdit>(view, "saved-history-week-date").setDate(qtDate(date)); }
QAbstractItemModel& model(QWidget& view) { return *child<QTableView>(view, "saved-history-week-days").model(); }
QString field(QWidget& view, const char* id) { return child<QLabel>(view, id).text(); }
std::string anchor(core::HistoryDate date) {
  const auto minute = QDateTime(qtDate(date), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch() / 60;
  return "s,1," + std::to_string(minute) + ",1000,2000,3000,4000,5000,6000\n";
}
core::SavedHistoryDay parsed(core::HistoryDate date, const std::string& input) {
  auto value = core::readSavedHistoryDay(input, date); if (!value) { throw std::runtime_error("Finite original-format day fixture rejected"); } return *value;
}
std::shared_ptr<core::SavedHistoryWeek> week(core::HistoryDate end = End, bool observed = true) {
  const auto dates = core::savedHistoryWeekDates(end); if (!dates) { throw std::runtime_error("Finite seven-day fixture has invalid ending date"); }
  auto value = std::make_shared<core::SavedHistoryWeek>(); value->endingDate = end;
  for (std::size_t i = 0; i < dates->size(); ++i) {
    value->days[i].date = (*dates)[i];
    if (observed) { value->days[i].snapshot = parsed((*dates)[i], anchor((*dates)[i]) + "1,2,3,4,5,6\n"); }
  }
  return value;
}
std::shared_ptr<ui::HistoryWeekPacket> packet(std::shared_ptr<const core::SavedHistoryWeek> value = week(),
    quint64 request = 1, quint64 session = 1, core::HistoryDate end = End) {
  auto p = std::make_shared<ui::HistoryWeekPacket>(); p->session = session; p->request = request; p->endingDate = end;
  p->state = ui::HistoryWeekReadState::Ready; p->snapshot = std::move(value); return p;
}
void begin(ui::HistoryWeekView& view, quint64 request = 1) { select(view); view.beginRequest(1, request, End); }
void history(ui::StatisticsPage& page, bool weekly = false) {
  page.tabs().setCurrentIndex(1); child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(1);
  child<QTabWidget>(page, "statistics-history-tabs").setCurrentIndex(weekly ? 1 : 0); QCoreApplication::processEvents();
}
class TipHook final : public QObject {
public:
  bool armed = true, consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; } return false;
  }
};
} // namespace

class HistoryWeekViewUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<core::HistoryDate>(); qRegisterMetaType<ui::HistoryWeekPresentation>();
  }
  void initialUtcDateAndSevenByEightReadOnlyModel() {
    const auto before = QDateTime::currentDateTimeUtc().date(); ui::HistoryWeekView view; const auto after = QDateTime::currentDateTimeUtc().date();
    QVERIFY(qtDate(view.selectedDate()) == before || qtDate(view.selectedDate()) == after);
    auto& editor = child<QDateEdit>(view, "saved-history-week-date"); QCOMPARE(editor.minimumDate(), QDate(1970, 1, 7));
    QCOMPARE(editor.maximumDate(), QDate(9999, 12, 31)); QVERIFY(editor.calendarPopup());
    auto& rows = model(view); QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(rows.rowCount(), 7); QCOMPARE(rows.columnCount(), 8); QCOMPARE(rows.rowCount(rows.index(0, 0)), 0);
    for (int row = 0; row < 7; ++row) {
      QCOMPARE(rows.data(rows.index(row, 1)).toString(), QStringLiteral("Not loaded"));
      for (int col = 2; col < 8; ++col) {
        QCOMPARE(rows.data(rows.index(row, col)).toString(), QStringLiteral("—")); QVERIFY(!rows.data(rows.index(row, col), Qt::UserRole).isValid());
        QVERIFY(!(rows.flags(rows.index(row, col)) & Qt::ItemIsEditable));
      }
    }
    QVERIFY(!view.presentation()); QVERIFY(!child<QTableView>(view, "saved-history-week-days").isSortingEnabled());
  }
  void dailyRemainsDefaultAndWeeklyReadsAreLazyAndIndependent() {
    ui::StatisticsPage page; QSignalSpy daily(&page, &ui::StatisticsPage::historyRequested), weekly(&page, &ui::StatisticsPage::historyWeekRequested); show(page);
    QCOMPARE(page.tabs().count(), 10); QCOMPARE(page.tabs().tabText(5), QStringLiteral("Tags")); QCOMPARE(page.tabs().tabText(6), QStringLiteral("Country")); QCOMPARE(page.tabs().currentIndex(), 0); QCOMPARE(daily.size(), 0); QCOMPARE(weekly.size(), 0);
    auto& nested = child<QTabWidget>(page, "statistics-history-tabs"); QCOMPARE(nested.count(), 4); QCOMPARE(nested.tabText(3), QStringLiteral("Rolling history")); QCOMPARE(nested.currentIndex(), 0);
    QCOMPARE(nested.tabText(0), QStringLiteral("Daily")); QCOMPARE(nested.tabText(1), QStringLiteral("7 days")); QCOMPARE(nested.tabText(2), QStringLiteral("30 days"));
    history(page); QVERIFY(page.historySelected()); QVERIFY(!page.historyWeekSelected()); QCOMPARE(daily.size(), 1); QCOMPARE(weekly.size(), 0);
    QVERIFY(page.findChild<QTableView*>(QStringLiteral("saved-history-hours"))); QCOMPARE(model(page).rowCount(), 7);
    history(page, true); QVERIFY(!page.historySelected()); QVERIFY(page.historyWeekSelected()); QCOMPARE(daily.size(), 1); QCOMPARE(weekly.size(), 1);
    history(page); history(page, true); QCOMPARE(daily.size(), 1); QCOMPARE(weekly.size(), 1);
    child<QPushButton>(page, "saved-history-week-refresh").click(); QCOMPARE(weekly.size(), 2); QCOMPARE(daily.size(), 1);
    QSignalSpy saved(&page, &ui::StatisticsPage::refreshRequested); child<QPushButton>(page, "statistics-refresh").click();
    QCOMPARE(saved.size(), 1); QCOMPARE(daily.size(), 1); QCOMPARE(weekly.size(), 2);
  }
  void dateSelectedBeforeFirstWeeklyActivationIsNotLost() {
    ui::StatisticsPage page; QSignalSpy weekly(&page, &ui::StatisticsPage::historyWeekRequested); show(page);
    select(child<ui::HistoryWeekView>(page, "statistics-history-week-view")); QCOMPARE(weekly.size(), 0);
    history(page, true); QCOMPARE(weekly.size(), 1); QVERIFY(qvariant_cast<core::HistoryDate>(weekly.at(0).at(0)) == End);
  }
  void hiddenDateChangeRequestsOnlyWhenReturning() {
    ui::StatisticsPage page; QSignalSpy reads(&page, &ui::StatisticsPage::historyWeekRequested); show(page); history(page, true); QCOMPARE(reads.size(), 1);
    const auto prior = qtDate(page.selectedHistoryWeekDate()); const auto chosen = prior.addDays(prior == QDate(1970, 1, 7) ? 1 : -1); QVERIFY(chosen != prior);
    history(page); child<QDateEdit>(page, "saved-history-week-date").setDate(chosen); QCOMPARE(reads.size(), 1);
    history(page, true); QCOMPARE(reads.size(), 2); QVERIFY(qvariant_cast<core::HistoryDate>(reads.at(1).at(0)) == savedDate(chosen));
  }
  void previousAndNextMoveExactlySevenUtcDates() {
    ui::HistoryWeekView view; select(view); show(view); QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested);
    child<QPushButton>(view, "saved-history-week-previous").click(); QVERIFY(view.selectedDate() == (core::HistoryDate{2024, 2, 24})); QCOMPARE(reads.size(), 1);
    child<QPushButton>(view, "saved-history-week-next").click(); QVERIFY(view.selectedDate() == End); QCOMPARE(reads.size(), 2);
    child<QDateEdit>(view, "saved-history-week-date").setDate(QDate(2025, 1, 2)); QCOMPARE(reads.size(), 3);
    child<QPushButton>(view, "saved-history-week-refresh").click(); QCOMPARE(reads.size(), 4);
    QVERIFY(qvariant_cast<core::HistoryDate>(reads.at(3).at(0)) == (core::HistoryDate{2025, 1, 2}));
  }
  void boundaryButtonsNeverClampOrPublishPartialPeriods() {
    ui::HistoryWeekView view; select(view, {1970, 1, 7}); show(view);
    QVERIFY(!child<QPushButton>(view, "saved-history-week-previous").isEnabled());
    select(view, {1970, 1, 13}); QVERIFY(!child<QPushButton>(view, "saved-history-week-previous").isEnabled());
    select(view, {1970, 1, 14}); QVERIFY(child<QPushButton>(view, "saved-history-week-previous").isEnabled());
    child<QPushButton>(view, "saved-history-week-previous").click(); QVERIFY(view.selectedDate() == (core::HistoryDate{1970, 1, 7}));
    select(view, {9999, 12, 31}); QVERIFY(!child<QPushButton>(view, "saved-history-week-next").isEnabled());
    select(view, {9999, 12, 25}); QVERIFY(!child<QPushButton>(view, "saved-history-week-next").isEnabled());
  }
  void exactChronologicalUtcDatesIncludeLeapDay() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet());
    const std::array<const char*, 7> expected{"2024-02-25 UTC", "2024-02-26 UTC", "2024-02-27 UTC", "2024-02-28 UTC", "2024-02-29 UTC", "2024-03-01 UTC", "2024-03-02 UTC"};
    for (int row = 0; row < 7; ++row) { QCOMPARE(model(view).data(model(view).index(row, 0)).toString(), QString::fromLatin1(expected[static_cast<std::size_t>(row)])); }
    QVERIFY(field(view, "saved-history-week-range").contains(QStringLiteral("2024-02-25 — 2024-03-02 UTC")));
    QCOMPARE(child<QLabel>(view, "saved-history-week-range").textFormat(), Qt::PlainText);
  }
  void sevenMissingRowsAreSuccessfulUnknownNotBatchMissingOrZero() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet(week(End, false)));
    QCOMPARE(view.presentation()->state, ui::HistoryWeekReadState::Ready);
    for (int row = 0; row < 7; ++row) {
      QCOMPARE(model(view).data(model(view).index(row, 1)).toString(), QStringLiteral("Missing"));
      for (int col = 2; col < 8; ++col) { QVERIFY(!model(view).data(model(view).index(row, col), Qt::UserRole).isValid()); }
    }
  }
  void missingEmptyHeaderZeroAndSignedRowsRemainDistinct() {
    auto value = week(End, false);
    value->days[1].snapshot = parsed(value->days[1].date, "");
    value->days[2].snapshot = parsed(value->days[2].date, anchor(value->days[2].date) + "0,0,0,0,0,0\n");
    value->days[3].snapshot = parsed(value->days[3].date, anchor(value->days[3].date) + "-1,-2,-3,-4,-5,-6\n");
    value->days[4].snapshot = parsed(value->days[4].date, anchor(value->days[4].date) + "1,2,3,4,5,6\n");
    value->days[5].snapshot = parsed(value->days[5].date, anchor(value->days[5].date));
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet(value));
    const std::array<const char*, 7> states{"Missing", "No observations", "Observed", "Observed", "Observed", "No observations", "Missing"};
    for (int row = 0; row < 7; ++row) { QCOMPARE(model(view).data(model(view).index(row, 1)).toString(), QString::fromLatin1(states[static_cast<std::size_t>(row)])); }
    for (int col = 2; col < 8; ++col) {
      QCOMPARE(model(view).data(model(view).index(2, col)).toString(), QStringLiteral("0 bytes"));
      QCOMPARE(model(view).data(model(view).index(2, col), Qt::UserRole).toLongLong(), qlonglong{0});
      QCOMPARE(model(view).data(model(view).index(3, col), Qt::UserRole).toLongLong(), static_cast<qlonglong>(-(col - 1)));
      QCOMPARE(model(view).data(model(view).index(4, col), Qt::UserRole).toLongLong(), static_cast<qlonglong>(col - 1));
      QVERIFY(!model(view).data(model(view).index(1, col), Qt::UserRole).isValid());
    }
  }
  void allSixDomainsHaveExactSignedInt64RolesAndTooltips() {
    auto value = week(); const auto date = value->days[0].date;
    value->days[0].snapshot = parsed(date, anchor(date) + "-9223372036854775808,-2,3,9223372036854775807,-5,6\n");
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet(value));
    const std::array<qlonglong, 6> expected{std::numeric_limits<qlonglong>::min(), -2, 3, std::numeric_limits<qlonglong>::max(), -5, 6};
    const std::array<const char*, 6> titles{"Protocol upload", "Payload upload", "Protocol download", "Payload download", "DHT upload", "DHT download"};
    for (int col = 2; col < 8; ++col) {
      const auto index = model(view).index(0, col); const auto at = static_cast<std::size_t>(col - 2);
      QCOMPARE(model(view).headerData(col, Qt::Horizontal).toString(), QString::fromLatin1(titles[at]));
      QCOMPARE(model(view).data(index, Qt::UserRole).toLongLong(), expected[at]);
      QCOMPARE(model(view).data(index).toString(), QStringLiteral("%1 bytes").arg(expected[at]));
      QVERIFY(model(view).data(index, Qt::ToolTipRole).toString().contains(QString::number(expected[at])));
    }
  }
  void sevenMaximumDailyValuesDoNotCreateOrOverflowAWeeklySum() {
    auto value = week();
    for (auto& day : value->days) { day.snapshot = parsed(day.date, anchor(day.date) + "0,0,0,9223372036854775807,0,0\n"); }
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet(value));
    QCOMPARE(view.presentation()->state, ui::HistoryWeekReadState::Ready);
    for (int row = 0; row < 7; ++row) { QCOMPARE(model(view).data(model(view).index(row, 5), Qt::UserRole).toLongLong(), std::numeric_limits<qlonglong>::max()); }
    QVERIFY(field(view, "saved-history-week-scope").contains(QStringLiteral("No weekly total is shown")));
    QVERIFY(!view.findChild<QLabel*>(QStringLiteral("saved-history-week-total")));
  }
  void exactSessionRequestAndEndingDateSettleOnlyOnce() {
    ui::HistoryWeekView view; begin(view, 4); show(view);
    view.setPresentation(packet(week(), 3)); QVERIFY(!view.presentation());
    view.setPresentation(packet(week(), 4, 2)); QVERIFY(!view.presentation());
    auto wrongDate = packet(week(), 4); wrongDate->endingDate.day = 3; view.setPresentation(wrongDate); QVERIFY(!view.presentation());
    auto good = packet(week(), 4); view.setPresentation(good); QVERIFY(view.presentation() == good);
    view.setPresentation(packet(week(End, false), 4)); QVERIFY(view.presentation() == good);
    view.beginRequest(0, 5, End); view.beginRequest(1, 0, End); view.beginRequest(1, 4, End); QVERIFY(view.presentation() == good);
  }
  void dateChangeRejectsOldResultAndClearsVisibleFacts() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); select(view, {2024, 3, 3}); view.setPresentation(packet());
    QVERIFY(!view.presentation()); QVERIFY(!model(view).data(model(view).index(0, 5), Qt::UserRole).isValid());
    view.beginRequest(1, 2, {2024, 3, 3}); auto latest = packet(week({2024, 3, 3}), 2, 1, {2024, 3, 3}); view.setPresentation(latest); QVERIFY(view.presentation() == latest);
  }
  void malformedBatchMetadataFailsWholeResult_data() {
    QTest::addColumn<int>("kind"); for (int i = 0; i < 11; ++i) { QTest::newRow(qPrintable(QStringLiteral("invalid-%1").arg(i))) << i; }
  }
  void malformedBatchMetadataFailsWholeResult() {
    QFETCH(int, kind); auto value = week(); auto p = packet(value); ui::HistoryWeekView view; begin(view); show(view);
    switch (kind) {
      case 0: p->snapshot.reset(); break;
      case 1: value->endingDate.day = 3; break;
      case 2: std::swap(value->days[0], value->days[1]); break;
      case 3: value->days[1].date = value->days[0].date; break;
      case 4: value->days[0].snapshot->date.day = 1; break;
      case 5: value->days[0].snapshot->totals.reset(); break;
      case 6: value->days[0].snapshot->observations = 0; break;
      case 7: value->days[0].snapshot->hours = {}; break;
      case 8: value->days[0].snapshot->observations = core::SavedHistoryMaximumRecords + 1; break;
      case 9: value->days[0].snapshot->outsideDayRecords = core::SavedHistoryMaximumRecords; break;
      case 10: p->failure = QStringLiteral("inconsistent Ready error"); break;
    }
    view.setPresentation(p); QVERIFY(view.presentation()); QCOMPARE(view.presentation()->state, ui::HistoryWeekReadState::Failed);
    QVERIFY(field(view, "saved-history-week-status").contains(QStringLiteral("incomplete")));
    for (int row = 0; row < 7; ++row) { QVERIFY(!model(view).data(model(view).index(row, 5), Qt::UserRole).isValid()); }
  }
  void failedBatchClearsAllRowsAndBoundsPlainText() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); view.beginRequest(1, 2, End);
    const auto failure = QStringLiteral("<b>literal error</b>") + QString(10000, QLatin1Char('x')); view.setFailed(1, 2, End, failure);
    QCOMPARE(view.presentation()->state, ui::HistoryWeekReadState::Failed); QCOMPARE(field(view, "saved-history-week-status"), failure.left(8192));
    QCOMPARE(child<QLabel>(view, "saved-history-week-status").textFormat(), Qt::PlainText);
    for (int row = 0; row < 7; ++row) {
      QCOMPARE(model(view).data(model(view).index(row, 1)).toString(), QStringLiteral("Unavailable"));
      for (int col = 2; col < 8; ++col) { QVERIFY(!model(view).data(model(view).index(row, col), Qt::UserRole).isValid()); }
    }
  }
  void resetSessionRejectsOldPacketsWithoutEmittingRead() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested);
    view.resetSession(2); QCOMPARE(reads.size(), 0); QVERIFY(!view.presentation()); QVERIFY(view.selectedDate() == End);
    view.setPresentation(packet(week(), 2)); view.beginRequest(1, 3, End); QVERIFY(!view.presentation());
    view.beginRequest(2, 1, End); auto newest = packet(week(), 1, 2); view.setPresentation(newest); QVERIFY(view.presentation() == newest);
    view.resetSession(0); view.resetSession(1); QVERIFY(view.presentation() == newest);
  }
  void hiddenCachesWithoutLabelsOrModelMutationThenRendersLatest() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); auto& rows = model(view);
    QSignalSpy changes(&rows, &QAbstractItemModel::dataChanged); const auto status = field(view, "saved-history-week-status"); view.hide();
    view.beginRequest(1, 2, End); auto newest = packet(week(End, false), 2); view.setPresentation(newest); QCoreApplication::processEvents();
    QCOMPARE(changes.size(), 0); QCOMPARE(field(view, "saved-history-week-status"), status);
    QCOMPARE(rows.data(rows.index(0, 1)).toString(), QStringLiteral("Observed")); QVERIFY(view.presentation() == newest);
    show(view); QCOMPARE(changes.size(), 1); QCOMPARE(rows.data(rows.index(0, 1)).toString(), QStringLiteral("Missing"));
  }
  void minimizedCachesAndRestoreRendersFailedWithoutStaleValues() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); view.showMinimized(); QCoreApplication::processEvents(); QVERIFY(view.isMinimized());
    QSignalSpy changes(&model(view), &QAbstractItemModel::dataChanged); const auto status = field(view, "saved-history-week-status");
    view.beginRequest(1, 2, End); view.setFailed(1, 2, End, QStringLiteral("new failure")); QCoreApplication::processEvents();
    QCOMPARE(changes.size(), 0); QCOMPARE(field(view, "saved-history-week-status"), status);
    view.showNormal(); QTRY_COMPARE(field(view, "saved-history-week-status"), QStringLiteral("new failure")); QCOMPARE(changes.size(), 1);
    QVERIFY(!model(view).data(model(view).index(0, 5), Qt::UserRole).isValid());
  }
  void pageResetClearsBothViewsButSavedTotalsRefreshDoesNot() {
    ui::StatisticsPage page; show(page); history(page); page.resetHistorySession(1);
    const auto day = page.selectedHistoryDate(); page.beginHistoryRequest(1, 1, day);
    auto daily = std::make_shared<ui::HistoryPacket>(); daily->session = 1; daily->request = 1; daily->date = day; daily->state = ui::HistoryReadState::Ready;
    daily->snapshot = std::make_shared<const core::SavedHistoryDay>(parsed(day, anchor(day) + "1,2,3,4,5,6\n")); page.setHistory(daily);
    select(child<ui::HistoryWeekView>(page, "statistics-history-week-view")); history(page, true); page.beginHistoryWeekRequest(1, 2, End);
    auto weekly = packet(week(), 2); page.setHistoryWeek(weekly); QVERIFY(page.historyPresentation() == daily); QVERIFY(page.historyWeekPresentation() == weekly);
    page.beginRequest(1, 8); page.setFailed(1, 8, QStringLiteral("independent saved totals failure"));
    QVERIFY(page.historyPresentation() == daily); QVERIFY(page.historyWeekPresentation() == weekly);
    QSignalSpy reads(&page, &ui::StatisticsPage::historyRequested), weekReads(&page, &ui::StatisticsPage::historyWeekRequested);
    page.resetHistorySession(2); QCOMPARE(reads.size(), 0); QCOMPARE(weekReads.size(), 0); QVERIFY(!page.historyPresentation()); QVERIFY(!page.historyWeekPresentation());
  }
  void modelNotificationReentryWinsOverOlderBatch() {
    ui::HistoryWeekView view; begin(view); show(view); bool fired = false;
    connect(&model(view), &QAbstractItemModel::dataChanged, &view, [&] {
      if (!fired) { fired = true; view.beginRequest(1, 2, End); view.setFailed(1, 2, End, QStringLiteral("newer request won")); }
    });
    view.setPresentation(packet()); QVERIFY(fired); QCOMPARE(view.presentation()->request, quint64{2});
    QCOMPARE(field(view, "saved-history-week-status"), QStringLiteral("newer request won")); QVERIFY(!model(view).data(model(view).index(0, 5), Qt::UserRole).isValid());
  }
  void modelNotificationMayDeleteView() {
    auto* view = new ui::HistoryWeekView; begin(*view); show(*view); QPointer<ui::HistoryWeekView> guard(view);
    connect(&model(*view), &QAbstractItemModel::dataChanged, qApp, [view] { delete view; }); view->setPresentation(packet()); QVERIFY(!guard);
  }
  void labelMutationReentryStopsOldFrame() {
    ui::HistoryWeekView view; begin(view); show(view); TipHook hook; child<QLabel>(view, "saved-history-week-status").setToolTip(QStringLiteral("before hook"));
    hook.callback = [&] { view.resetSession(2); }; child<QLabel>(view, "saved-history-week-status").installEventFilter(&hook);
    view.setPresentation(packet()); QVERIFY(!hook.armed); QVERIFY(!view.presentation()); QVERIFY(!model(view).data(model(view).index(0, 5), Qt::UserRole).isValid());
  }
  void labelMutationMayDeleteView() {
    auto* view = new ui::HistoryWeekView; begin(*view); show(*view); QPointer<ui::HistoryWeekView> guard(view); TipHook hook;
    child<QLabel>(*view, "saved-history-week-status").setToolTip(QStringLiteral("before hook")); hook.consume = true;
    hook.callback = [view] { delete view; }; child<QLabel>(*view, "saved-history-week-status").installEventFilter(&hook);
    view->setPresentation(packet()); QVERIFY(!guard);
  }
  void pageDeletionDuringFirstChildResetCannotReachSecondChild() {
    auto* page = new ui::StatisticsPage; show(*page); history(*page); page->resetHistorySession(1);
    const auto date = page->selectedHistoryDate(); page->beginHistoryRequest(1, 1, date);
    auto daily = std::make_shared<ui::HistoryPacket>(); daily->session = 1; daily->request = 1; daily->date = date; daily->state = ui::HistoryReadState::Ready;
    daily->snapshot = std::make_shared<const core::SavedHistoryDay>(parsed(date, anchor(date) + "1,2,3,4,5,6\n")); page->setHistory(daily);
    auto* rows = child<QTableView>(*page, "saved-history-hours").model(); QPointer<ui::StatisticsPage> guard(page);
    connect(rows, &QAbstractItemModel::dataChanged, qApp, [page] { delete page; }); page->resetHistorySession(2); QVERIFY(!guard);
  }
  void nestedNewerProfileResetWinsForBothChildren() {
    ui::StatisticsPage page; show(page); history(page); page.resetHistorySession(1);
    const auto date = page.selectedHistoryDate(); page.beginHistoryRequest(1, 1, date);
    auto daily = std::make_shared<ui::HistoryPacket>(); daily->session = 1; daily->request = 1; daily->date = date; daily->state = ui::HistoryReadState::Ready;
    daily->snapshot = std::make_shared<const core::SavedHistoryDay>(parsed(date, anchor(date) + "1,2,3,4,5,6\n")); page.setHistory(daily);
    select(child<ui::HistoryWeekView>(page, "statistics-history-week-view"));
    bool fired = false; auto* rows = child<QTableView>(page, "saved-history-hours").model();
    connect(rows, &QAbstractItemModel::dataChanged, &page, [&] { if (!fired) { fired = true; page.resetHistorySession(3); } });
    page.resetHistorySession(2); QVERIFY(fired); history(page, true);
    page.beginHistoryWeekRequest(2, 1, End); page.setHistoryWeek(packet(week(), 1, 2)); QVERIFY(!page.historyWeekPresentation());
    page.beginHistoryWeekRequest(3, 1, End); auto newest = packet(week(), 1, 3); page.setHistoryWeek(newest); QVERIFY(page.historyWeekPresentation() == newest);
    page.beginHistoryRequest(2, 1, date); page.setHistory(daily); QVERIFY(!page.historyPresentation());
  }
  void refreshSignalMayDeleteViewWithoutTrailingAccess() {
    auto* view = new ui::HistoryWeekView; select(*view); show(*view); QPointer<ui::HistoryWeekView> guard(view);
    connect(view, &ui::HistoryWeekView::readRequested, qApp, [view](core::HistoryDate) { delete view; });
    child<QPushButton>(*view, "saved-history-week-refresh").click(); QVERIFY(!guard);
  }
};

QTEST_MAIN(HistoryWeekViewUiTests)
#include "history_week_view_ui_tests.moc"
