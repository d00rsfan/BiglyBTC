#include "statistics_page.hpp"
#include "statistics_navigation.hpp"
#include "history_view.hpp"
#include "history_week_view.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QPointer>
#include <QTabWidget>
#include <QtTest>

#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate Daily{2024, 2, 29}, Ending{2024, 3, 2}, MonthEnding{2024, 3, 30};
template<class T> T& child(QWidget& parent, const char* name) {
  auto* value = parent.findChild<T*>(QString::fromLatin1(name)); if (!value) { throw std::runtime_error("Missing navigation control"); } return *value;
}
QDate qtDate(core::HistoryDate d) { return {d.year, static_cast<int>(d.month), static_cast<int>(d.day)}; }
ui::StatisticsNavigation choices(unsigned bits = 15, bool hourly = true) {
  return {.section = static_cast<int>(bits & 1), .activity = static_cast<int>((bits >> 1) & 1),
      .transfers = static_cast<int>((bits >> 2) & 1), .history = static_cast<int>((bits >> 3) & 1),
      .dailyDate = Daily, .weekEnding = Ending, .hourly = hourly,
      .monthEnding = MonthEnding, .monthTraffic = ui::SavedHistoryTraffic::Dht};
}
void visible(ui::StatisticsPage& page) { page.resize(1180, 850); page.show(); QCoreApplication::processEvents(); }
void verifyControls(ui::StatisticsPage& page, const ui::StatisticsNavigation& value) {
  QCOMPARE(page.tabs().currentIndex(), value.section);
  QCOMPARE(child<QTabWidget>(page, "statistics-activity-views").currentIndex(), value.activity);
  QCOMPARE(child<QTabWidget>(page, "statistics-transfer-tabs").currentIndex(), value.transfers);
  QCOMPARE(child<QTabWidget>(page, "statistics-history-tabs").currentIndex(), value.history);
  QCOMPARE(child<QDateEdit>(page, "saved-history-date").date(), qtDate(value.dailyDate));
  QCOMPARE(child<QDateEdit>(page, "saved-history-week-date").date(), qtDate(value.weekEnding));
  QCOMPARE(child<QDateEdit>(page, "saved-history-month-date").date(), qtDate(value.monthEnding));
  QCOMPARE(child<QComboBox>(page, "saved-history-month-traffic").currentIndex(), static_cast<int>(value.monthTraffic));
  QCOMPARE(child<QComboBox>(page, "saved-history-week-resolution").currentIndex(), value.hourly ? 1 : 0);
}
bool today(core::HistoryDate date, QDate first, QDate last) { return qtDate(date) == first || qtDate(date) == last; }
} // namespace

class StatisticsNavigationPageTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); qRegisterMetaType<core::HistoryDate>();
    static_assert(std::is_trivially_copyable_v<ui::StatisticsNavigation>);
  }
  void all32ChoicesInitializeActualControlsIncludingHiddenSubviews() {
    for (unsigned bits = 0; bits < 16; ++bits) { for (const bool hourly : {false, true}) {
      const auto expected = choices(bits, hourly); QVERIFY(ui::validStatisticsNavigation(expected)); ui::StatisticsPage page(nullptr, expected);
      QSignalSpy days(&page, &ui::StatisticsPage::historyRequested), weeks(&page, &ui::StatisticsPage::historyWeekRequested);
      QCoreApplication::processEvents(); verifyControls(page, expected); QVERIFY(page.navigation()); QCOMPARE(*page.navigation(), expected);
      QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QVERIFY(!page.historyPresentation()); QVERIFY(!page.historyWeekPresentation());
      QCOMPARE(page.sampleCount(), std::size_t{0}); QVERIFY(!page.engineActivitySnapshot());
    } }
  }
  void invalidStateDiscardsAllChoicesInsteadOfPartiallyRestoring() {
    for (int kind = 0; kind < 8; ++kind) {
      auto invalid = choices();
      switch (kind) {
        case 0: invalid.section = -1; break; case 1: invalid.activity = 2; break;
        case 2: invalid.transfers = -1; break; case 3: invalid.history = 4; break;
        case 4: invalid.dailyDate = {2023, 2, 29}; break; case 5: invalid.dailyDate = {10000, 1, 1}; break;
        case 6: invalid.weekEnding = {1970, 1, 6}; break; default: invalid.weekEnding = {2024, 4, 31}; break;
      }
      QVERIFY(!ui::validStatisticsNavigation(invalid)); const auto first = QDateTime::currentDateTimeUtc().date();
      ui::StatisticsPage page(nullptr, invalid); const auto last = QDateTime::currentDateTimeUtc().date(); const auto result = page.navigation(); QVERIFY(result);
      QCOMPARE(result->section, 0); QCOMPARE(result->activity, 0); QCOMPARE(result->transfers, 0); QCOMPARE(result->history, 0); QVERIFY(!result->hourly);
      QVERIFY(today(result->dailyDate, first, last)); QVERIFY(today(result->weekEnding, first, last));
      QVERIFY(today(result->monthEnding, first, last)); QCOMPARE(result->monthTraffic, ui::SavedHistoryTraffic::Payload); verifyControls(page, *result);
    }
  }
  void firstShowReadsOnlyTheSelectedHistorySubview_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("activity-payload") << 0; QTest::newRow("activity-engine") << 1;
    QTest::newRow("transfers-current") << 2; QTest::newRow("daily") << 3; QTest::newRow("hourly-week") << 4;
  }
  void firstShowReadsOnlyTheSelectedHistorySubview() {
    QFETCH(int, kind); auto value = choices(); value.section = kind < 2 ? 0 : 1;
    value.activity = kind == 1 ? 1 : 0; value.transfers = kind < 3 ? 0 : 1; value.history = kind == 3 ? 0 : 1;
    ui::StatisticsPage page(nullptr, value); page.resetHistorySession(31);
    QSignalSpy days(&page, &ui::StatisticsPage::historyRequested), weeks(&page, &ui::StatisticsPage::historyWeekRequested);
    visible(page); QCOMPARE(days.count(), kind == 3 ? 1 : 0); QCOMPARE(weeks.count(), kind == 4 ? 1 : 0);
    if (kind == 3) { QCOMPARE(qvariant_cast<core::HistoryDate>(days.at(0).at(0)), Daily); }
    if (kind == 4) { QCOMPARE(qvariant_cast<core::HistoryDate>(weeks.at(0).at(0)), Ending); }
    page.hide(); visible(page); QCOMPARE(days.count(), kind == 3 ? 1 : 0); QCOMPARE(weeks.count(), kind == 4 ? 1 : 0);
    verifyControls(page, value); QVERIFY(!page.historyPresentation()); QVERIFY(!page.historyWeekPresentation());
  }
  void capturesEditedHiddenChoicesAndRestoresBeforeAnyFirstShow() {
    ui::StatisticsPage first; child<QDateEdit>(first, "saved-history-date").setDate(qtDate(Daily));
    child<QDateEdit>(first, "saved-history-week-date").setDate(qtDate(Ending));
    child<QDateEdit>(first, "saved-history-month-date").setDate(qtDate(MonthEnding));
    child<QComboBox>(first, "saved-history-month-traffic").setCurrentIndex(2);
    child<QTabWidget>(first, "statistics-activity-views").setCurrentIndex(1);
    child<QTabWidget>(first, "statistics-transfer-tabs").setCurrentIndex(1);
    child<QTabWidget>(first, "statistics-history-tabs").setCurrentIndex(1);
    child<QComboBox>(first, "saved-history-week-resolution").setCurrentIndex(1);
    first.tabs().setCurrentIndex(0); const auto value = first.navigation(); QVERIFY(value); QCOMPARE(*value, choices(14, true));
    ui::StatisticsPage restored(nullptr, value); verifyControls(restored, *value);
    QSignalSpy reads(&restored, &ui::StatisticsPage::historyWeekRequested); visible(restored); QCOMPARE(reads.count(), 0);
    restored.tabs().setCurrentIndex(1); QCOMPARE(reads.count(), 1); QCOMPARE(qvariant_cast<core::HistoryDate>(reads.at(0).at(0)), Ending);
  }
  void directHistoryConstructorsUseValidDateOrUtcDefaultWithoutPartialPagePolicy() {
    ui::HistoryView day(nullptr, Daily); QCOMPARE(day.selectedDate(), Daily);
    ui::HistoryWeekView week(nullptr, Ending, true); QCOMPARE(week.selectedDate(), Ending); QVERIFY(week.hourlyResolution());
    const auto first = QDateTime::currentDateTimeUtc().date(); ui::HistoryView badDay(nullptr, core::HistoryDate{2024, 2, 30});
    ui::HistoryWeekView badWeek(nullptr, core::HistoryDate{1970, 1, 6}, true); const auto last = QDateTime::currentDateTimeUtc().date();
    QVERIFY(today(badDay.selectedDate(), first, last)); QVERIFY(today(badWeek.selectedDate(), first, last)); QVERIFY(badWeek.hourlyResolution());
    QVERIFY(!day.presentation()); QVERIFY(!week.presentation()); QSignalSpy reads(&badWeek, &ui::HistoryWeekView::readRequested);
    QCoreApplication::processEvents(); QCOMPARE(reads.count(), 0);
  }
  void scalarNavigationOutlivesDestroyedPageWithoutKeepingItsSavedResult() {
    auto owner = std::make_unique<ui::StatisticsPage>(nullptr, choices()); owner->resetHistorySession(9);
    auto source = std::make_shared<core::SavedHistoryDay>(); source->date = Daily; source->observations = 1;
    source->hours[0] = core::HistoryCounters{1, 2, 3, 4, 5, 6}; source->totals = core::HistoryCounters{1, 2, 3, 4, 5, 6};
    std::weak_ptr<core::SavedHistoryDay> weak = source; owner->beginHistoryRequest(9, 1, Daily);
    auto result = std::make_shared<ui::HistoryPacket>(); result->session = 9; result->request = 1; result->date = Daily;
    result->state = ui::HistoryReadState::Ready; result->snapshot = source; owner->setHistory(result); source.reset(); result.reset();
    QVERIFY(!weak.expired()); const auto navigation = owner->navigation(); QVERIFY(navigation); QPointer guard(owner.get()); owner.reset();
    QVERIFY(guard.isNull()); QVERIFY(weak.expired()); ui::StatisticsPage reopened(nullptr, navigation); verifyControls(reopened, *navigation); QVERIFY(!reopened.historyPresentation());
  }
  void firstReadCanReenterNavigationWithoutLaterRestoringOldSelection() {
    auto value = choices(); ui::StatisticsPage page(nullptr, value); int reads = 0;
    connect(&page, &ui::StatisticsPage::historyWeekRequested, &page, [&](core::HistoryDate date) { QCOMPARE(date, Ending); ++reads; page.tabs().setCurrentIndex(0); });
    visible(page); QCOMPARE(reads, 1); QCOMPARE(page.tabs().currentIndex(), 0); QVERIFY(page.navigation()); QCOMPARE(page.navigation()->section, 0);
    QCOMPARE(page.navigation()->weekEnding, Ending); QVERIFY(page.navigation()->hourly); QVERIFY(!page.historyWeekPresentation());
  }
};
QTEST_MAIN(StatisticsNavigationPageTests)
#include "statistics_navigation_page_tests.moc"
