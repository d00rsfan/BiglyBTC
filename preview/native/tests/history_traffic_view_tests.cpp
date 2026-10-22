#include "history_view.hpp"
#include "history_week_view.hpp"
#include "statistics_page.hpp"
#include "statistics_navigation.hpp"
#include "saved_history_plot.hpp"
#include "saved_history_traffic.hpp"
#include "library_window.hpp"
#include "session_bridge.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
using Traffic = ui::SavedHistoryTraffic;
constexpr core::HistoryDate Date{2024, 2, 29}, End{2024, 3, 2};
constexpr core::HistoryCounters Raw{11, 22, 33, 44, 55, 66};
template<class T> T& child(QObject& parent, const char* name) {
  auto* p = parent.findChild<T*>(QString::fromLatin1(name)); if (!p) { throw std::runtime_error("Missing traffic view control"); } return *p;
}
std::shared_ptr<core::SavedHistoryDay> day() {
  auto value = std::make_shared<core::SavedHistoryDay>(); value->date = Date; value->observations = 1; value->hours[9] = Raw; value->totals = Raw; return value;
}
std::shared_ptr<core::SavedHistoryWeek> week() {
  auto value = std::make_shared<core::SavedHistoryWeek>(); value->endingDate = End; auto dates = core::savedHistoryWeekDates(End);
  if (!dates) { throw std::runtime_error("Finite week failed"); } for (std::size_t i = 0; i < 7; ++i) { value->days[i].date = (*dates)[i]; }
  value->days[4].snapshot = *day(); return value;
}
auto dailyPacket(quint64 request) {
  auto p = std::make_shared<ui::HistoryPacket>(); p->session = 1; p->request = request; p->date = Date; p->state = ui::HistoryReadState::Ready; p->snapshot = day(); return p;
}
auto weekPacket(quint64 request) {
  auto p = std::make_shared<ui::HistoryWeekPacket>(); p->session = 1; p->request = request; p->endingDate = End; p->state = ui::HistoryWeekReadState::Ready; p->snapshot = week(); return p;
}
void show(QWidget& w) { w.resize(1180, 850); w.show(); QCoreApplication::processEvents(); }
struct Browser {
  bool weekly; std::unique_ptr<ui::HistoryView> d; std::unique_ptr<ui::HistoryWeekView> w;
  explicit Browser(bool seven) : weekly(seven) {
    if (weekly) { w = std::make_unique<ui::HistoryWeekView>(nullptr, End); } else { d = std::make_unique<ui::HistoryView>(nullptr, Date); }
  }
  QWidget& widget() { return weekly ? static_cast<QWidget&>(*w) : static_cast<QWidget&>(*d); }
  QComboBox& combo() { return child<QComboBox>(widget(), weekly ? "saved-history-week-traffic" : "saved-history-traffic"); }
  ui::SavedHistoryPlot& plot() { return child<ui::SavedHistoryPlot>(widget(), weekly ? "saved-history-week-chart" : "saved-history-chart"); }
  QAbstractItemModel& model() { return *child<QTableView>(widget(), weekly ? "saved-history-week-days" : "saved-history-hours").model(); }
  void begin(quint64 request) { if (weekly) { w->beginRequest(1, request, End); } else { d->beginRequest(1, request, Date); } }
  void ready(quint64 request) { if (weekly) { w->setPresentation(weekPacket(request)); } else { d->setPresentation(dailyPacket(request)); } }
  void fail(quint64 request) { if (weekly) { w->setFailed(1, request, End, QStringLiteral("Test read failed")); } else { d->setFailed(1, request, Date, QStringLiteral("Test read failed")); } }
  void reset() { if (weekly) { w->resetSession(2); } else { d->resetSession(2); } }
  std::unique_ptr<QSignalSpy> reads() {
    return weekly ? std::make_unique<QSignalSpy>(w.get(), &ui::HistoryWeekView::readRequested) : std::make_unique<QSignalSpy>(d.get(), &ui::HistoryView::readRequested);
  }
  void destroy() { w.reset(); d.reset(); }
};
bool unknown(const ui::SavedHistoryPlotPresentation& frame) {
  if (!frame) { return false; } for (const auto& b : frame->buckets()) { if (b.downloaded || b.uploaded) { return false; } } return true;
}
class TipHook final : public QObject {
public:
  bool armed = true, consume = false; std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* e) override { if (armed && e->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; } return false; }
};
void selectHistory(ui::StatisticsPage& page, bool weekly) {
  child<QTabWidget>(page, "statistics-history-tabs").setCurrentIndex(weekly ? 1 : 0);
  child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(1); page.tabs().setCurrentIndex(1);
}
struct Profile {
  struct Saved { QByteArray bytes; dev_t dev; ino_t ino; };
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-history-traffic-XXXXXX")}; QString path; std::map<QString, Saved> files;
  Profile() {
    if (!root.isValid()) { throw std::runtime_error("Generated profile root failed"); } path = root.path() + QStringLiteral("/profile");
    put(QStringLiteral("/downloads.config"), QByteArrayLiteral("d9:downloadslee")); put(QStringLiteral("/tables.config"), QByteArrayLiteral("de"));
    put(QStringLiteral("/BiglyBT.statistics"), QByteArrayLiteral("d3:alld10:downloadedi999eee")); history(false);
  }
  void put(const QString& name, const QByteArray& bytes) {
    const auto file = path + name; if (!QDir().mkpath(QFileInfo(file).absolutePath())) { throw std::runtime_error("Generated directory failed"); }
    QFile out(file); if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size() || !out.flush()) { throw std::runtime_error("Generated file failed"); }
    struct stat s{}; if (::stat(file.toStdString().c_str(), &s)) { throw std::runtime_error("Generated identity failed"); } files[file] = {bytes, s.st_dev, s.st_ino};
  }
  void history(bool changed) {
    using namespace std::chrono; const auto minute = duration_cast<minutes>(sys_days{year{2024}/2/29}.time_since_epoch()).count() + 540;
    put(QStringLiteral("/stats/2024/02/29.dat"), QByteArray::fromStdString("s,1," + std::to_string(minute) + ",0,0,0,0,0,0\n" + (changed ? "101,202,303,404,505,606\n" : "11,22,33,44,55,66\n")));
  }
  bool intact() const {
    for (const auto& [name, expected] : files) {
      struct stat s{}; QFile f(name); if (::stat(name.toStdString().c_str(), &s) || s.st_dev != expected.dev || s.st_ino != expected.ino || !f.open(QIODevice::ReadOnly) || f.readAll() != expected.bytes
          || QFile::exists(name + QStringLiteral(".bak")) || QFile::exists(name + QStringLiteral(".saving"))) { return false; }
    } return true;
  }
};
void wire(ui::LibraryWindow& w, app::SessionBridge& b) {
  QObject::connect(&b, &app::SessionBridge::snapshotReady, &w, &ui::LibraryWindow::setPresentation); QObject::connect(&b, &app::SessionBridge::busyChanged, &w, &ui::LibraryWindow::setBusy);
  QObject::connect(&b, &app::SessionBridge::columnLayoutSessionReady, &w, &ui::LibraryWindow::beginColumnLayoutSession); QObject::connect(&b, &app::SessionBridge::columnLayoutsReady, &w, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&b, &app::SessionBridge::statisticsReady, &w, &ui::LibraryWindow::setStatistics); QObject::connect(&b, &app::SessionBridge::historyReady, &w, &ui::LibraryWindow::setHistory); QObject::connect(&b, &app::SessionBridge::historyWeekReady, &w, &ui::LibraryWindow::setHistoryWeek);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &b, &app::SessionBridge::columnLayouts); QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &b, &app::SessionBridge::statistics);
  QObject::connect(&w, &ui::LibraryWindow::historyRequested, &b, &app::SessionBridge::history); QObject::connect(&w, &ui::LibraryWindow::historyWeekRequested, &b, &app::SessionBridge::historyWeek);
}
ui::StatisticsPage& open(ui::LibraryWindow& w) { child<QAction>(w, "classic-statistics").trigger(); auto* p = w.statisticsPage(); if (!p) { throw std::runtime_error("Statistics missing"); } return *p; }
void chooseTraffic(ui::StatisticsPage& page) {
  page.tabs().setCurrentIndex(0); child<QDateEdit>(page, "saved-history-date").setDate(QDate(2024, 2, 29)); child<QDateEdit>(page, "saved-history-week-date").setDate(QDate(2024, 3, 2));
  child<QComboBox>(page, "saved-history-traffic").setCurrentIndex(2); child<QComboBox>(page, "saved-history-week-traffic").setCurrentIndex(1);
  child<QComboBox>(page, "saved-history-week-resolution").setCurrentIndex(1);
}
} // namespace

class HistoryTrafficViewTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); qRegisterMetaType<core::HistoryDate>(); }
  void defaultsAndDirectInitialKindsAreSetBeforeAnyRead() {
    for (const auto kind : {Traffic::Payload, Traffic::Protocol, Traffic::Dht}) {
      ui::HistoryView d(nullptr, Date, kind); ui::HistoryWeekView w(nullptr, End, true, kind); QCOMPARE(d.traffic(), kind); QCOMPARE(w.traffic(), kind);
      QSignalSpy days(&d, &ui::HistoryView::readRequested), weeks(&w, &ui::HistoryWeekView::readRequested); QCoreApplication::processEvents(); QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0);
    }
    Browser b(false); QCOMPARE(b.d->traffic(), Traffic::Payload); QCOMPARE(b.combo().currentIndex(), 0); QCOMPARE(b.combo().count(), 3);
    QCOMPARE(b.combo().itemText(0), QStringLiteral("Payload")); QCOMPARE(b.combo().itemText(1), QStringLiteral("Protocol")); QCOMPARE(b.combo().itemText(2), QStringLiteral("DHT")); QCOMPARE(b.combo().accessibleName(), QStringLiteral("Chart traffic"));
    bool buddy = false; for (auto* label : b.widget().findChildren<QLabel*>()) { buddy |= label->text() == QStringLiteral("Traffic") && label->buddy() == &b.combo(); } QVERIFY(buddy);
    ui::HistoryView bad(nullptr, Date, static_cast<Traffic>(99)); ui::HistoryWeekView badWeek(nullptr, End, true, static_cast<Traffic>(-1));
    QCOMPARE(bad.selectedDate(), Date); QCOMPARE(bad.traffic(), Traffic::Payload); QCOMPARE(badWeek.selectedDate(), End); QVERIFY(badWeek.hourlyResolution()); QCOMPARE(badWeek.traffic(), Traffic::Payload);
  }
  void visibleSwitchesOnlyReprojectChartAndLeaveTablesPayloadSummaryAndReadsUnchanged() {
    for (const bool weekly : {false, true}) {
      Browser b(weekly); b.begin(1); show(b.widget()); b.ready(1); auto reads = b.reads(); QSignalSpy changes(&b.model(), &QAbstractItemModel::dataChanged), resets(&b.model(), &QAbstractItemModel::modelReset);
      const int row = weekly ? 4 : 9, column = weekly ? 5 : 4; const auto payload = b.model().data(b.model().index(row, column), Qt::UserRole); QCOMPARE(payload.toLongLong(), qlonglong{44});
      const auto dailyBinding = weekly ? ui::HistoryPresentation{} : b.d->presentation(); const auto weekBinding = weekly ? b.w->presentation() : ui::HistoryWeekPresentation{};
      for (int k : {1, 2, 0}) {
        b.combo().setCurrentIndex(k); QTRY_VERIFY(b.plot().presentation() && b.plot().presentation()->traffic() == static_cast<Traffic>(k));
        const auto& point = b.plot().presentation()->buckets()[static_cast<std::size_t>(weekly ? 4 : 9)];
        QCOMPARE(point.downloaded, std::optional<std::int64_t>{k == 0 ? 44 : k == 1 ? 33 : 66}); QCOMPARE(point.uploaded, std::optional<std::int64_t>{k == 0 ? 22 : k == 1 ? 11 : 55});
        QCOMPARE(b.model().data(b.model().index(row, column), Qt::UserRole), payload); QCOMPARE(b.model().rowCount(), weekly ? 7 : 24); QCOMPARE(b.model().columnCount(), weekly ? 8 : 7);
        if (weekly) { QCOMPARE(b.w->presentation(), weekBinding); } else { QCOMPARE(b.d->presentation(), dailyBinding); QCOMPARE(child<QLabel>(b.widget(), "saved-history-payload-downloaded").text(), QStringLiteral("44 bytes")); }
      }
      QCOMPARE(reads->count(), 0); QCOMPARE(changes.count(), 0); QCOMPARE(resets.count(), 0);
    }
  }
  void hiddenAndMinimizedChangesDeferUntilVisible_data() {
    QTest::addColumn<bool>("weekly"); QTest::addColumn<bool>("minimized");
    QTest::newRow("hidden-daily") << false << false; QTest::newRow("hidden-week") << true << false; QTest::newRow("minimized-daily") << false << true; QTest::newRow("minimized-week") << true << true;
  }
  void hiddenAndMinimizedChangesDeferUntilVisible() {
    QFETCH(bool, weekly); QFETCH(bool, minimized); Browser b(weekly); b.begin(1); show(b.widget()); b.ready(1);
    if (minimized) { b.widget().showMinimized(); QCoreApplication::processEvents(); QVERIFY(b.widget().isMinimized()); } else { b.widget().hide(); }
    const auto old = b.plot().presentation(); const auto tip = b.plot().toolTip(); auto reads = b.reads(); QSignalSpy changes(&b.model(), &QAbstractItemModel::dataChanged);
    b.combo().setCurrentIndex(1); b.combo().setCurrentIndex(2); QCoreApplication::processEvents(); QCOMPARE(b.plot().presentation(), old); QCOMPARE(b.plot().toolTip(), tip); QCOMPARE(changes.count(), 0);
    if (minimized) { b.widget().showNormal(); } else { show(b.widget()); }
    QTRY_COMPARE(b.plot().presentation()->traffic(), Traffic::Dht); QCOMPARE(reads->count(), 0); QCOMPARE(changes.count(), 0);
  }
  void pendingFailureAndSessionResetClearCurrentKindWithoutAcceptingOldPacket() {
    for (const bool weekly : {false, true}) {
      Browser b(weekly); b.begin(1); show(b.widget()); b.ready(1); b.combo().setCurrentIndex(2); QTRY_COMPARE(b.plot().presentation()->traffic(), Traffic::Dht);
      b.begin(2); QVERIFY(unknown(b.plot().presentation())); b.ready(1); QVERIFY(unknown(b.plot().presentation())); b.fail(2); QVERIFY(unknown(b.plot().presentation()));
      b.begin(3); b.ready(3); QVERIFY(!unknown(b.plot().presentation())); b.reset(); QVERIFY(unknown(b.plot().presentation())); QCOMPARE(b.plot().presentation()->traffic(), Traffic::Dht);
    }
  }
  void tooltipReentryKeepsLatestTrafficAndNeverResumesOldTableWork() {
    for (const bool weekly : {false, true}) {
      Browser b(weekly); b.begin(1); show(b.widget()); b.ready(1); b.plot().setToolTip(QStringLiteral("arm")); TipHook hook;
      auto reads = b.reads(); QSignalSpy changes(&b.model(), &QAbstractItemModel::dataChanged); hook.callback = [&] { b.combo().setCurrentIndex(2); }; b.plot().installEventFilter(&hook);
      b.combo().setCurrentIndex(1); QTRY_VERIFY(!hook.armed); QTRY_COMPARE(b.plot().presentation()->traffic(), Traffic::Dht); QCOMPARE(b.combo().currentIndex(), 2); QCOMPARE(changes.count(), 0); QCOMPARE(reads->count(), 0);
    }
  }
  void tooltipCanDirectlyDeleteEachOwnerAfterItsComboSetterReturns() {
    for (const bool weekly : {false, true}) {
      Browser b(weekly); b.begin(1); show(b.widget()); b.ready(1); b.plot().setToolTip(QStringLiteral("arm")); QPointer guard(&b.widget()); TipHook hook;
      bool returned = false, afterSetter = false, destroyedInCallback = false; hook.consume = true; hook.callback = [&] { afterSetter = returned; b.destroy(); destroyedInCallback = guard.isNull(); }; b.plot().installEventFilter(&hook);
      b.combo().setCurrentIndex(1); returned = true; QTRY_VERIFY(guard.isNull()); QVERIFY(!hook.armed); QVERIFY(afterSetter); QVERIFY(destroyedInCallback);
    }
  }
  void hiddenTrafficChoicesRestoreAndInvalidAggregateUsesAllDefaults() {
    ui::StatisticsPage first; chooseTraffic(first); const auto nav = first.navigation(); QVERIFY(nav); QCOMPARE(nav->dailyTraffic, Traffic::Dht); QCOMPARE(nav->weekTraffic, Traffic::Protocol);
    ui::StatisticsPage second(nullptr, nav); QCOMPARE(child<ui::HistoryView>(second, "statistics-history-view").traffic(), Traffic::Dht); QCOMPARE(child<ui::HistoryWeekView>(second, "statistics-history-week-view").traffic(), Traffic::Protocol);
    QSignalSpy days(&second, &ui::StatisticsPage::historyRequested), weeks(&second, &ui::StatisticsPage::historyWeekRequested); show(second); QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0);
    selectHistory(second, true); QCOMPARE(weeks.count(), 1); QCOMPARE(days.count(), 0);
    for (const bool invalidDaily : {false, true}) {
      auto bad = *nav; bad.section = 1; bad.activity = 1; bad.transfers = 1; bad.history = 1; if (invalidDaily) { bad.dailyTraffic = static_cast<Traffic>(99); } else { bad.weekTraffic = static_cast<Traffic>(-1); }
      QVERIFY(!ui::validStatisticsNavigation(bad)); ui::StatisticsPage fallback(nullptr, bad); const auto defaults = fallback.navigation(); QVERIFY(defaults);
      QCOMPARE(defaults->section, 0); QCOMPARE(defaults->activity, 2); QCOMPARE(defaults->transfers, 0); QCOMPARE(defaults->history, 0); QVERIFY(!defaults->hourly);
      QCOMPARE(defaults->dailyTraffic, Traffic::Payload); QCOMPARE(defaults->weekTraffic, Traffic::Payload);
      const auto today = QDateTime::currentDateTimeUtc().date();
      const auto isCurrentUtcDate = [&](core::HistoryDate d) { const QDate date(d.year, static_cast<int>(d.month), static_cast<int>(d.day)); return date == today || date == today.addDays(-1); };
      QVERIFY(isCurrentUtcDate(defaults->dailyDate)); QVERIFY(isCurrentUtcDate(defaults->weekEnding));
    }
  }
  void realGeneratedProfileReopenRestoresIndependentTrafficButReadsFreshValues() {
    Profile profile; ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge); show(window);
    QSignalSpy columns(&bridge, &app::SessionBridge::columnLayoutsReady), weeks(&bridge, &app::SessionBridge::historyWeekReady), days(&bridge, &app::SessionBridge::historyReady);
    QVERIFY(bridge.open(profile.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !bridge.busy(), 30000);
    auto& first = open(window); chooseTraffic(first); selectHistory(first, true); QTRY_VERIFY_WITH_TIMEOUT(first.historyWeekPresentation() && !bridge.busy(), 30000);
    const auto nav = first.navigation(); QVERIFY(nav); QCOMPARE(nav->dailyTraffic, Traffic::Dht); QCOMPARE(nav->weekTraffic, Traffic::Protocol); QPointer old(&first);
    child<QAction>(window, "classic-close-tab").trigger(); QVERIFY(!window.statisticsPage()); profile.history(true); auto& restored = open(window);
    QCOMPARE(restored.navigation(), nav); QVERIFY(!restored.historyWeekPresentation()); QTRY_VERIFY_WITH_TIMEOUT(restored.historyWeekPresentation() && !bridge.busy(), 30000); QTRY_VERIFY(old.isNull());
    QCOMPARE(weeks.count(), 2); QCOMPARE(days.count(), 0); auto& graph = child<ui::SavedHistoryPlot>(restored, "saved-history-week-chart"); QTRY_COMPARE(graph.presentation()->traffic(), Traffic::Protocol);
    QCOMPARE(graph.presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(graph.presentation()->buckets()[105].downloaded, std::optional<std::int64_t>{303}); QCOMPARE(graph.presentation()->buckets()[105].uploaded, std::optional<std::int64_t>{101});
    selectHistory(restored, false); QTRY_VERIFY_WITH_TIMEOUT(restored.historyPresentation() && !bridge.busy(), 30000); QCOMPARE(days.count(), 1);
    auto& daily = child<ui::SavedHistoryPlot>(restored, "saved-history-chart"); QCOMPARE(daily.presentation()->traffic(), Traffic::Dht); QCOMPARE(daily.presentation()->buckets()[9].downloaded, std::optional<std::int64_t>{606});
    QCOMPARE(child<QLabel>(restored, "saved-history-payload-downloaded").text(), QStringLiteral("404 bytes")); child<QComboBox>(restored, "saved-history-traffic").setCurrentIndex(0); QTRY_COMPARE(daily.presentation()->traffic(), Traffic::Payload);
    QCOMPARE(days.count(), 1); QCOMPARE(weeks.count(), 2); QVERIFY(profile.intact());
  }
  void realNewProfileWhileClosedDropsBothTrafficChoices() {
    Profile firstProfile, secondProfile; ui::LibraryWindow window; auto first = std::make_unique<app::SessionBridge>(); wire(window, *first); show(window);
    QSignalSpy columns(first.get(), &app::SessionBridge::columnLayoutsReady); QVERIFY(first->open(firstProfile.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !first->busy(), 30000);
    auto& page = open(window); chooseTraffic(page); QTRY_VERIFY_WITH_TIMEOUT(!page.loading() && !first->busy(), 30000); child<QAction>(window, "classic-close-tab").trigger(); first.reset();
    app::SessionBridge next; wire(window, next); QSignalSpy nextColumns(&next, &app::SessionBridge::columnLayoutsReady); QVERIFY(next.open(secondProfile.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(nextColumns.count() == 1 && !next.busy(), 30000);
    auto& reset = open(window); QVERIFY(reset.navigation()); QCOMPARE(reset.navigation()->dailyTraffic, Traffic::Payload); QCOMPARE(reset.navigation()->weekTraffic, Traffic::Payload); QCOMPARE(reset.tabs().currentIndex(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!reset.loading() && !next.busy(), 30000); QVERIFY(!reset.historyPresentation()); QVERIFY(!reset.historyWeekPresentation()); QVERIFY(firstProfile.intact()); QVERIFY(secondProfile.intact());
  }
};
QTEST_MAIN(HistoryTrafficViewTests)
#include "history_traffic_view_tests.moc"
