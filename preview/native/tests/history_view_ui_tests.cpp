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

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate Day{2026, 10, 18};
template<class T> T& child(QWidget& owner, const char* name) {
  auto* result = owner.findChild<T*>(QString::fromLatin1(name));
  if (!result) { throw std::runtime_error("Missing semantic history control"); } return *result;
}
QDate qtDate(core::HistoryDate date) { return {date.year, static_cast<int>(date.month), static_cast<int>(date.day)}; }
void select(ui::HistoryView& view, core::HistoryDate day = Day) { child<QDateEdit>(view, "saved-history-date").setDate(qtDate(day)); }
void show(QWidget& view) { view.resize(1100, 850); view.show(); QCoreApplication::processEvents(); }
QAbstractItemModel& model(QWidget& view) { return *child<QTableView>(view, "saved-history-hours").model(); }
QString field(QWidget& view, const char* id) { return child<QLabel>(view, id).text(); }
std::string anchor(core::HistoryDate date = Day, int minute = 0) {
  const auto time = QDateTime(qtDate(date), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch() / 60 + minute;
  return "s,1," + std::to_string(time) + ",10,20,30,40,50,60\n";
}
std::shared_ptr<const core::SavedHistoryDay> saved(std::string input, core::HistoryDate date = Day) {
  auto result = core::readSavedHistoryDay(input, date);
  if (!result) { throw std::runtime_error("Finite generated history fixture rejected"); }
  return std::make_shared<const core::SavedHistoryDay>(*result);
}
std::shared_ptr<ui::HistoryPacket> packet(std::shared_ptr<const core::SavedHistoryDay> value,
    quint64 request = 1, quint64 session = 1, core::HistoryDate date = Day) {
  auto p = std::make_shared<ui::HistoryPacket>(); p->session = session; p->request = request; p->date = date;
  p->state = ui::HistoryReadState::Ready; p->snapshot = std::move(value); return p;
}
auto observed(quint64 request = 1) { return packet(saved(anchor() + "1,2,3,4,5,6\n"), request); }
void begin(ui::HistoryView& view, quint64 request = 1) { select(view); view.beginRequest(1, request, Day); }
class TipHook final : public QObject {
public:
  bool armed = true, consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; }
    return false;
  }
};
void activateHistory(ui::StatisticsPage& page) {
  page.tabs().setCurrentIndex(1); child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(1);
  QCoreApplication::processEvents();
}
} // namespace

class HistoryViewUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<core::HistoryDate>(); qRegisterMetaType<ui::HistoryPresentation>();
  }
  void initialUtcDateAndFixedUnknownReadOnlyModel() {
    const auto before = QDateTime::currentDateTimeUtc().date(); ui::HistoryView view;
    const auto after = QDateTime::currentDateTimeUtc().date(); const auto selected = qtDate(view.selectedDate());
    QVERIFY(selected == before || selected == after);
    auto& editor = child<QDateEdit>(view, "saved-history-date");
    QCOMPARE(editor.minimumDate(), QDate(1970, 1, 1)); QCOMPARE(editor.maximumDate(), QDate(9999, 12, 31)); QVERIFY(editor.calendarPopup());
    auto& rows = model(view); QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(rows.rowCount(), 24); QCOMPARE(rows.columnCount(), 7); QCOMPARE(rows.rowCount(rows.index(0, 0)), 0);
    QCOMPARE(rows.data(rows.index(0, 0)).toString(), QStringLiteral("00:00–00:59 UTC"));
    QCOMPARE(rows.data(rows.index(23, 0)).toString(), QStringLiteral("23:00–23:59 UTC"));
    for (int row = 0; row < 24; ++row) {
      for (int column = 1; column < 7; ++column) {
        QCOMPARE(rows.data(rows.index(row, column)).toString(), QStringLiteral("—")); QVERIFY(!rows.data(rows.index(row, column), Qt::UserRole).isValid());
        QVERIFY(!(rows.flags(rows.index(row, column)) & Qt::ItemIsEditable));
      }
    }
    QVERIFY(!view.presentation()); QVERIFY(!child<QTableView>(view, "saved-history-hours").isSortingEnabled());
  }
  void pageDefaultPreservesActivityAndLazyHistoryActivation() {
    ui::StatisticsPage page; QSignalSpy reads(&page, &ui::StatisticsPage::historyRequested); show(page);
    QCOMPARE(page.tabs().count(), 2); QCOMPARE(page.tabs().currentIndex(), 0); QCOMPARE(reads.size(), 0);
    QVERIFY(!page.historySelected()); QVERIFY(!page.historyPresentation());
    QCOMPARE(child<QTabWidget>(page, "statistics-activity-views").currentIndex(), 0);
    QVERIFY(field(page, "statistics-history-unavailable").contains(QStringLiteral("History unavailable")));
    activateHistory(page); QCOMPARE(reads.size(), 1); QVERIFY(page.historySelected());
    page.tabs().setCurrentIndex(0); activateHistory(page); QCOMPARE(reads.size(), 1);
    child<QPushButton>(page, "saved-history-refresh").click(); QCOMPARE(reads.size(), 2);
    QCOMPARE(qvariant_cast<core::HistoryDate>(reads.at(1).at(0)), page.selectedHistoryDate());
  }
  void hiddenDateSelectionStillRequestsOnFirstActivation() {
    ui::StatisticsPage page; QSignalSpy reads(&page, &ui::StatisticsPage::historyRequested); show(page);
    auto& view = child<ui::HistoryView>(page, "statistics-history-view"); select(view); QCOMPARE(reads.size(), 0);
    activateHistory(page); QCOMPARE(reads.size(), 1); QVERIFY(qvariant_cast<core::HistoryDate>(reads.at(0).at(0)) == Day);
  }
  void hiddenDateChangeRequestsNewSelectionWhenReturning() {
    ui::StatisticsPage page; QSignalSpy reads(&page, &ui::StatisticsPage::historyRequested); show(page); activateHistory(page);
    QCOMPARE(reads.size(), 1); const auto prior = qtDate(page.selectedHistoryDate());
    const auto chosen = prior.addDays(prior == QDate(1970, 1, 1) ? 1 : -1); QVERIFY(chosen != prior);
    page.tabs().setCurrentIndex(0);
    child<QDateEdit>(page, "saved-history-date").setDate(chosen); QCOMPARE(reads.size(), 1);
    activateHistory(page); QCOMPARE(reads.size(), 2);
    QVERIFY(qvariant_cast<core::HistoryDate>(reads.at(1).at(0)) == (core::HistoryDate{chosen.year(),
        static_cast<unsigned>(chosen.month()), static_cast<unsigned>(chosen.day())}));
  }
  void previousNextCalendarAndExplicitRefreshKeepUtcDates() {
    ui::HistoryView view; select(view, {2024, 3, 1}); show(view); QSignalSpy reads(&view, &ui::HistoryView::readRequested);
    child<QPushButton>(view, "saved-history-previous").click(); QCOMPARE(reads.size(), 1); QVERIFY(view.selectedDate() == core::HistoryDate(2024, 2, 29));
    child<QPushButton>(view, "saved-history-next").click(); QCOMPARE(reads.size(), 2); QVERIFY(view.selectedDate() == core::HistoryDate(2024, 3, 1));
    child<QDateEdit>(view, "saved-history-date").setDate(QDate(2026, 10, 25)); QCOMPARE(reads.size(), 3);
    child<QPushButton>(view, "saved-history-refresh").click(); QCOMPARE(reads.size(), 4);
    QVERIFY(qvariant_cast<core::HistoryDate>(reads.at(3).at(0)) == core::HistoryDate(2026, 10, 25));
  }
  void dateRangeNavigationDoesNotWrap() {
    ui::HistoryView view; select(view, {1970, 1, 1}); show(view);
    QVERIFY(!child<QPushButton>(view, "saved-history-previous").isEnabled());
    select(view, {9999, 12, 31}); QVERIFY(!child<QPushButton>(view, "saved-history-next").isEnabled());
    QVERIFY(child<QPushButton>(view, "saved-history-previous").isEnabled());
  }
  void sameNonzeroSessionRequestAndDateMustMatchOnce() {
    ui::HistoryView view; begin(view, 3); show(view);
    auto stale = observed(2); view.setPresentation(stale); QVERIFY(!view.presentation());
    auto wrongSession = observed(3); wrongSession->session = 2; view.setPresentation(wrongSession); QVERIFY(!view.presentation());
    auto wrongDate = observed(3); wrongDate->date.day = 19; view.setPresentation(wrongDate); QVERIFY(!view.presentation());
    auto valid = observed(3); view.setPresentation(valid); QVERIFY(view.presentation() == valid);
    auto duplicate = packet(saved(anchor() + "9,9,9,9,9,9\n"), 3); view.setPresentation(duplicate); QVERIFY(view.presentation() == valid);
    view.beginRequest(0, 4, Day); view.beginRequest(1, 0, Day); view.beginRequest(1, 3, Day); QVERIFY(view.presentation() == valid);
  }
  void oldDateCannotSettleAfterSelectionChange() {
    ui::HistoryView view; begin(view); show(view); select(view, {2026, 10, 19}); view.setPresentation(observed());
    QVERIFY(!view.presentation()); QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("Unavailable"));
    view.beginRequest(1, 2, {2026, 10, 19}); auto fresh = packet(saved(anchor({2026, 10, 19}) + "1,2,3,4,5,6\n", {2026, 10, 19}), 2, 1, {2026, 10, 19});
    view.setPresentation(fresh); QVERIFY(view.presentation() == fresh);
  }
  void newRequestClearsFactsWithoutInferringZero() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(observed()); view.beginRequest(1, 2, Day);
    QVERIFY(!view.presentation()); QVERIFY(field(view, "saved-history-status").contains(QStringLiteral("Loading")));
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("Unavailable")); QVERIFY(!model(view).data(model(view).index(0, 4), Qt::UserRole).isValid());
  }
  void missingAndExistingWithoutObservationsStayDistinct() {
    ui::HistoryView view; begin(view); show(view); auto missing = packet({}); missing->state = ui::HistoryReadState::Missing;
    view.setPresentation(missing); QCOMPARE(view.presentation()->state, ui::HistoryReadState::Missing);
    QVERIFY(field(view, "saved-history-status").contains(QStringLiteral("not found")));
    view.beginRequest(1, 2, Day); view.setPresentation(packet(saved(""), 2));
    QCOMPARE(view.presentation()->state, ui::HistoryReadState::Ready); QVERIFY(field(view, "saved-history-status").contains(QStringLiteral("no observations")));
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("Unavailable"));
  }
  void actualObservedZeroIsKnownAcrossAllSixDomains() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(packet(saved(anchor() + "0,0,0,0,0,0\n")));
    for (int column = 1; column < 7; ++column) {
      const auto index = model(view).index(0, column); QCOMPARE(model(view).data(index).toString(), QStringLiteral("0 bytes"));
      QVERIFY(model(view).data(index, Qt::UserRole).isValid()); QCOMPARE(model(view).data(index, Qt::UserRole).toLongLong(), qlonglong{0});
    }
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("0 bytes"));
    QCOMPARE(field(view, "saved-history-payload-uploaded"), QStringLiteral("0 bytes"));
    QVERIFY(!model(view).data(model(view).index(1, 1), Qt::UserRole).isValid());
  }
  void sixCounterOrderAndSignedInt64BoundariesAreExact() {
    ui::HistoryView view; begin(view); show(view);
    view.setPresentation(packet(saved(anchor() + "-9223372036854775808,-2,3,9223372036854775807,-5,6\n")));
    const std::array<qlonglong, 6> expected{std::numeric_limits<qlonglong>::min(), -2, 3, std::numeric_limits<qlonglong>::max(), -5, 6};
    const std::array<QString, 6> headers{QStringLiteral("Protocol upload"), QStringLiteral("Payload upload"), QStringLiteral("Protocol download"),
        QStringLiteral("Payload download"), QStringLiteral("DHT upload"), QStringLiteral("DHT download")};
    for (int column = 1; column < 7; ++column) {
      const auto i = static_cast<std::size_t>(column - 1); const auto index = model(view).index(0, column);
      QCOMPARE(model(view).headerData(column, Qt::Horizontal).toString(), headers[i]);
      QCOMPARE(model(view).data(index, Qt::UserRole).toLongLong(), expected[i]);
      QCOMPARE(model(view).data(index).toString(), QStringLiteral("%1 bytes").arg(expected[i]));
      QVERIFY(model(view).data(index, Qt::ToolTipRole).toString().contains(QString::number(expected[i])));
    }
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("9223372036854775807 bytes"));
    QCOMPARE(field(view, "saved-history-payload-uploaded"), QStringLiteral("-2 bytes"));
  }
  void actualParserAggregatesHoursAndExcludesSessionBaseline() {
    ui::HistoryView view; begin(view); show(view);
    view.setPresentation(packet(saved(anchor(Day, 59) + "1,2,3,4,5,6\n-1,3,-3,5,-5,7\n")));
    QVERIFY(!model(view).data(model(view).index(0, 1), Qt::UserRole).isValid());
    QCOMPARE(model(view).data(model(view).index(1, 2), Qt::UserRole).toLongLong(), qlonglong{5});
    QCOMPARE(model(view).data(model(view).index(1, 4), Qt::UserRole).toLongLong(), qlonglong{9});
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("9 bytes"));
    QCOMPARE(field(view, "saved-history-payload-uploaded"), QStringLiteral("5 bytes"));
    QVERIFY(field(view, "saved-history-observations").startsWith(QStringLiteral("2 saved observations")));
  }
  void outsideDayRecordsAreReportedWithoutSelectedTotals() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(packet(saved(anchor(Day, 1439) + "1,2,3,4,5,6\n")));
    QVERIFY(view.presentation()->snapshot->observations == 0); QCOMPARE(view.presentation()->snapshot->outsideDayRecords, std::size_t{1});
    QVERIFY(field(view, "saved-history-observations").contains(QStringLiteral("1 outside-day")));
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("Unavailable"));
  }
  void incompletePacketsFailWholeRead_data() {
    QTest::addColumn<int>("kind");
    for (int i = 0; i < 8; ++i) { QTest::newRow(qPrintable(QStringLiteral("incomplete-%1").arg(i))) << i; }
  }
  void incompletePacketsFailWholeRead() {
    QFETCH(int, kind); ui::HistoryView view; begin(view); show(view); auto p = observed();
    auto changed = std::make_shared<core::SavedHistoryDay>(*p->snapshot);
    switch (kind) {
      case 0: p->snapshot.reset(); break;
      case 1: changed->date.day = 19; p->snapshot = changed; break;
      case 2: changed->totals.reset(); p->snapshot = changed; break;
      case 3: changed->observations = 0; p->snapshot = changed; break;
      case 4: changed->hours = {}; p->snapshot = changed; break;
      case 5: changed->outsideDayRecords = core::SavedHistoryMaximumRecords; p->snapshot = changed; break;
      case 6: p->state = ui::HistoryReadState::Missing; break;
      case 7: p->failure = QStringLiteral("unexpected data"); break;
    }
    view.setPresentation(p); QVERIFY(view.presentation()); QCOMPARE(view.presentation()->state, ui::HistoryReadState::Failed);
    QVERIFY(field(view, "saved-history-status").contains(QStringLiteral("incomplete")));
    QVERIFY(!model(view).data(model(view).index(0, 1), Qt::UserRole).isValid());
  }
  void failedReadClearsOldFactsAndBoundsPlainText() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(observed()); view.beginRequest(1, 2, Day);
    const auto error = QStringLiteral("<b>literal failure</b>") + QString(10000, QLatin1Char('x'));
    view.setFailed(1, 2, Day, error); QCOMPARE(view.presentation()->state, ui::HistoryReadState::Failed);
    auto& status = child<QLabel>(view, "saved-history-status"); QCOMPARE(status.text(), error.left(8192)); QCOMPARE(status.textFormat(), Qt::PlainText);
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("Unavailable"));
  }
  void resetSessionClearsFactsAndDoesNotEmitReadReentrantly() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(observed()); QSignalSpy reads(&view, &ui::HistoryView::readRequested);
    view.resetSession(2); QCOMPARE(reads.size(), 0); QVERIFY(!view.presentation()); QVERIFY(view.selectedDate() == Day);
    view.setPresentation(observed(9)); view.beginRequest(1, 9, Day); QVERIFY(!view.presentation());
    view.beginRequest(2, 1, Day); auto p = observed(); p->session = 2; view.setPresentation(p); QVERIFY(view.presentation() == p);
    view.resetSession(1); view.resetSession(0); QVERIFY(view.presentation() == p);
  }
  void hiddenCachesLatestWithoutModelOrLabelMutation() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(observed()); auto& rows = model(view);
    QSignalSpy changes(&rows, &QAbstractItemModel::dataChanged); const auto status = field(view, "saved-history-status"); view.hide();
    view.beginRequest(1, 2, Day); auto latest = packet(saved(anchor() + "6,5,4,3,2,1\n"), 2); view.setPresentation(latest);
    QCoreApplication::processEvents(); QCOMPARE(changes.size(), 0); QCOMPARE(field(view, "saved-history-status"), status);
    QCOMPARE(rows.data(rows.index(0, 4), Qt::UserRole).toLongLong(), qlonglong{4}); QVERIFY(view.presentation() == latest);
    show(view); QCOMPARE(changes.size(), 1); QCOMPARE(rows.data(rows.index(0, 4), Qt::UserRole).toLongLong(), qlonglong{3});
  }
  void minimizedCachesWithoutMutatingThenRestoresLatest() {
    ui::HistoryView view; begin(view); show(view); view.setPresentation(observed()); view.showMinimized(); QCoreApplication::processEvents();
    QVERIFY(view.isMinimized()); auto& rows = model(view); QSignalSpy changes(&rows, &QAbstractItemModel::dataChanged);
    const auto status = field(view, "saved-history-status"); view.beginRequest(1, 2, Day); view.setFailed(1, 2, Day, QStringLiteral("Latest saved read failed"));
    QCoreApplication::processEvents(); QCOMPARE(changes.size(), 0); QCOMPARE(field(view, "saved-history-status"), status);
    view.showNormal(); QTRY_COMPARE(field(view, "saved-history-status"), QStringLiteral("Latest saved read failed"));
    QCOMPARE(changes.size(), 1); QVERIFY(!rows.data(rows.index(0, 4), Qt::UserRole).isValid());
  }
  void pageSavedTotalsRefreshRetainsHistoryAndExplicitSessionResetClearsIt() {
    ui::StatisticsPage page; show(page); auto& view = child<ui::HistoryView>(page, "statistics-history-view"); select(view); activateHistory(page);
    page.resetHistorySession(1); page.beginHistoryRequest(1, 1, Day); auto p = observed(); page.setHistory(p); QVERIFY(page.historyPresentation() == p);
    page.beginRequest(1, 7); page.setFailed(1, 7, QStringLiteral("saved totals fail independently")); QVERIFY(page.historyPresentation() == p);
    QSignalSpy reads(&page, &ui::StatisticsPage::historyRequested); page.resetHistorySession(2); QCOMPARE(reads.size(), 0); QVERIFY(!page.historyPresentation());
  }
  void modelNotificationReentryWinsOverOlderResult() {
    ui::HistoryView view; begin(view); show(view); bool fired = false;
    connect(&model(view), &QAbstractItemModel::dataChanged, &view, [&] {
      if (!fired) { fired = true; view.beginRequest(1, 2, Day); view.setFailed(1, 2, Day, QStringLiteral("newer request won")); }
    });
    view.setPresentation(observed()); QVERIFY(fired); QCOMPARE(view.presentation()->request, quint64{2});
    QCOMPARE(field(view, "saved-history-status"), QStringLiteral("newer request won"));
    QVERIFY(!model(view).data(model(view).index(0, 4), Qt::UserRole).isValid());
  }
  void modelNotificationMayDeleteView() {
    auto* view = new ui::HistoryView; begin(*view); show(*view); QPointer<ui::HistoryView> guard(view);
    connect(&model(*view), &QAbstractItemModel::dataChanged, qApp, [view] { delete view; });
    view->setPresentation(observed()); QVERIFY(!guard);
  }
  void labelMutationReentryStopsOlderRender() {
    ui::HistoryView view; begin(view); show(view); TipHook hook;
    // Force a genuine tooltip change during the next render without altering the packet.
    child<QLabel>(view, "saved-history-status").setToolTip(QStringLiteral("before hook"));
    hook.callback = [&] { view.resetSession(2); }; child<QLabel>(view, "saved-history-status").installEventFilter(&hook);
    view.setPresentation(observed()); QVERIFY(!hook.armed); QVERIFY(!view.presentation());
    QCOMPARE(field(view, "saved-history-payload-downloaded"), QStringLiteral("Unavailable"));
  }
  void labelMutationMayDeleteView() {
    auto* view = new ui::HistoryView; begin(*view); show(*view); QPointer<ui::HistoryView> guard(view); TipHook hook;
    child<QLabel>(*view, "saved-history-status").setToolTip(QStringLiteral("before hook"));
    hook.consume = true; hook.callback = [view] { delete view; }; child<QLabel>(*view, "saved-history-status").installEventFilter(&hook);
    view->setPresentation(observed()); QVERIFY(!guard);
  }
  void explicitRefreshMayDeleteViewWithoutTrailingAccess() {
    auto* view = new ui::HistoryView; select(*view); show(*view); QPointer<ui::HistoryView> guard(view);
    connect(view, &ui::HistoryView::readRequested, qApp, [view](core::HistoryDate) { delete view; });
    child<QPushButton>(*view, "saved-history-refresh").click(); QVERIFY(!guard);
  }
};

QTEST_MAIN(HistoryViewUiTests)
#include "history_view_ui_tests.moc"
