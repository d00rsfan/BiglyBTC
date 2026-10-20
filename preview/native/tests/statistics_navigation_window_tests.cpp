#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"
#include "statistics_navigation.hpp"
#include "saved_history_plot.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QPointer>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate Daily{2024, 2, 29}, Ending{2024, 3, 2};
template<class T> T& child(QObject& owner, const char* name) {
  auto* value = owner.findChild<T*>(QString::fromLatin1(name)); if (!value) { throw std::runtime_error("Missing navigation control"); } return *value;
}
QAction& action(ui::LibraryWindow& w, const char* name) { return child<QAction>(w, name); }
QDate qtDate(core::HistoryDate d) { return {d.year, static_cast<int>(d.month), static_cast<int>(d.day)}; }
ui::StatisticsNavigation choices(bool selectedWeek = true) {
  return {.section = selectedWeek ? 1 : 0, .activity = 1, .transfers = 1, .history = 1,
      .dailyDate = Daily, .weekEnding = Ending, .hourly = true};
}
void choose(ui::StatisticsPage& page, const ui::StatisticsNavigation& value) {
  page.tabs().setCurrentIndex(0); // Edit History while hidden, then activate only the chosen branch.
  child<QDateEdit>(page, "saved-history-date").setDate(qtDate(value.dailyDate));
  child<QDateEdit>(page, "saved-history-week-date").setDate(qtDate(value.weekEnding));
  child<QTabWidget>(page, "statistics-activity-views").setCurrentIndex(value.activity);
  child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(value.transfers);
  child<QTabWidget>(page, "statistics-history-tabs").setCurrentIndex(value.history);
  child<QComboBox>(page, "saved-history-week-resolution").setCurrentIndex(value.hourly ? 1 : 0);
  page.tabs().setCurrentIndex(value.section);
}
void show(ui::LibraryWindow& w) { w.resize(1280, 900); w.show(); w.activateWindow(); QCoreApplication::processEvents(); }
ui::StatisticsPage& open(ui::LibraryWindow& w) {
  action(w, "classic-statistics").trigger(); auto* page = w.statisticsPage(); if (!page) { throw std::runtime_error("Statistics did not open"); } return *page;
}
std::string history(core::HistoryDate date = Daily, int payload = 4) {
  using namespace std::chrono;
  const auto minute = duration_cast<minutes>(sys_days{year{date.year}/month{date.month}/day{date.day}}.time_since_epoch()).count() + 540;
  return "s,1," + std::to_string(minute) + ",100,200,300,400,500,600\n1,2,3," + std::to_string(payload) + ",5,6\n";
}
QString datePath(core::HistoryDate d) {
  return QStringLiteral("/stats/%1/%2/%3.dat").arg(d.year, 4, 10, QLatin1Char('0')).arg(d.month, 2, 10, QLatin1Char('0')).arg(d.day, 2, 10, QLatin1Char('0'));
}
struct Profile {
  struct File { QByteArray bytes; dev_t device; ino_t inode; };
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-statistics-navigation-XXXXXX")}; QString path; std::map<QString, File> files;
  explicit Profile(int saved = 999) {
    if (!root.isValid()) { throw std::runtime_error("Generated profile root failed"); } path = root.path() + QStringLiteral("/profile");
    put(QStringLiteral("/downloads.config"), QByteArrayLiteral("d9:downloadslee")); put(QStringLiteral("/tables.config"), QByteArrayLiteral("de"));
    put(QStringLiteral("/BiglyBT.statistics"), QByteArray::fromStdString("d3:alld10:downloadedi" + std::to_string(saved) + "eee"));
    put(datePath(Daily), QByteArray::fromStdString(history()));
  }
  void put(const QString& suffix, const QByteArray& bytes) {
    const auto file = path + suffix; if (!QDir().mkpath(QFileInfo(file).absolutePath())) { throw std::runtime_error("Generated directory failed"); }
    QFile out(file); if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size() || !out.flush()) { throw std::runtime_error("Generated fixture write failed"); }
    struct stat s{}; if (::stat(file.toStdString().c_str(), &s) != 0) { throw std::runtime_error("Generated fixture metadata failed"); } files[file] = {bytes, s.st_dev, s.st_ino};
  }
  bool intact() const {
    for (const auto& [name, expected] : files) {
      struct stat s{}; QFile file(name);
      if (::stat(name.toStdString().c_str(), &s) != 0 || s.st_dev != expected.device || s.st_ino != expected.inode || !file.open(QIODevice::ReadOnly) || file.readAll() != expected.bytes
          || QFile::exists(name + QStringLiteral(".saving")) || QFile::exists(name + QStringLiteral(".bak"))) { return false; }
    } return true;
  }
};
void wire(ui::LibraryWindow& w, app::SessionBridge& b) {
  QObject::connect(&b, &app::SessionBridge::snapshotReady, &w, &ui::LibraryWindow::setPresentation);
  QObject::connect(&b, &app::SessionBridge::busyChanged, &w, &ui::LibraryWindow::setBusy);
  QObject::connect(&b, &app::SessionBridge::columnLayoutSessionReady, &w, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&b, &app::SessionBridge::columnLayoutsReady, &w, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&b, &app::SessionBridge::statisticsReady, &w, &ui::LibraryWindow::setStatistics);
  QObject::connect(&b, &app::SessionBridge::historyReady, &w, &ui::LibraryWindow::setHistory);
  QObject::connect(&b, &app::SessionBridge::historyWeekReady, &w, &ui::LibraryWindow::setHistoryWeek);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &b, &app::SessionBridge::columnLayouts);
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &b, &app::SessionBridge::statistics);
  QObject::connect(&w, &ui::LibraryWindow::historyRequested, &b, &app::SessionBridge::history);
  QObject::connect(&w, &ui::LibraryWindow::historyWeekRequested, &b, &app::SessionBridge::historyWeek);
  QObject::connect(&w, &ui::LibraryWindow::cancelRequested, &b, &app::SessionBridge::cancel);
}
auto saved(quint64 session, quint64 request) {
  auto parsed = core::readSavedStatistics("d3:alld10:downloadedi77eee"); if (!parsed) { throw std::runtime_error("Invalid saved fixture"); }
  auto value = std::make_shared<ui::StatisticsPacket>(); value->session = session; value->request = request; value->state = ui::StatisticsReadState::Ready;
  value->source = ui::StatisticsReadSource::Primary; value->snapshot = std::make_shared<const core::SavedStatistics>(std::move(*parsed)); return value;
}
void syntheticTotalsOwner(ui::LibraryWindow& w) {
  // Only the stale-request tests use this deterministic optional-command owner;
  // it executes real Window dispatch/admission, without worker or IO credit.
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&w](quint64 session, quint64 request) {
    w.setBusy(true); QTimer::singleShot(0, &w, [&w, session, request] { w.setStatistics(saved(session, request)); w.setBusy(false); });
  });
}
bool bind(ui::LibraryWindow& w, quint64 session = 73) {
  QSignalSpy columns(&w, &ui::LibraryWindow::columnLayoutsRequested); w.beginColumnLayoutSession(session);
  return QTest::qWaitFor([&] { return columns.count() == 1; }, 3000);
}
auto weekPacket(quint64 session, quint64 request, int payload = 44) {
  const auto dates = core::savedHistoryWeekDates(Ending); if (!dates) { throw std::runtime_error("Invalid finite range"); }
  auto source = std::make_shared<core::SavedHistoryWeek>(); source->endingDate = Ending;
  for (std::size_t i = 0; i < dates->size(); ++i) { source->days[i].date = (*dates)[i]; }
  auto day = core::readSavedHistoryDay(history(Daily, payload), Daily); if (!day) { throw std::runtime_error("Invalid finite day"); }
  source->days[4].snapshot = *day; // Feb29 is member4 in Feb25..Mar2.
  auto value = std::make_shared<ui::HistoryWeekPacket>(); value->session = session; value->request = request; value->endingDate = Ending;
  value->state = ui::HistoryWeekReadState::Ready; value->snapshot = std::move(source); return value;
}
void defaults(ui::StatisticsPage& page) {
  const auto state = page.navigation(); QVERIFY(state); QCOMPARE(state->section, 0); QCOMPARE(state->activity, 0);
  QCOMPARE(state->transfers, 0); QCOMPARE(state->history, 0); QVERIFY(!state->hourly);
  const auto now = QDateTime::currentDateTimeUtc().date();
  QVERIFY(qtDate(state->dailyDate) == now || qtDate(state->dailyDate) == now.addDays(-1));
  QVERIFY(qtDate(state->weekEnding) == now || qtDate(state->weekEnding) == now.addDays(-1));
}
} // namespace

class StatisticsNavigationWindowTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); qRegisterMetaType<core::HistoryDate>();
  }
  void actualHeldProfileReopenRestoresHourlyWeekButReadsFreshBytes() {
    Profile profile; ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy columns(&bridge, &app::SessionBridge::columnLayoutsReady), reads(&bridge, &app::SessionBridge::historyWeekReady), daily(&bridge, &app::SessionBridge::historyReady);
    QVERIFY(bridge.open(profile.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !bridge.busy(), 30000);
    auto& first = open(w); choose(first, choices()); QTRY_VERIFY_WITH_TIMEOUT(first.historyWeekPresentation() && !bridge.busy(), 30000);
    QCOMPARE(reads.count(), 1); QCOMPARE(daily.count(), 0); const auto original = first.historyWeekPresentation();
    QCOMPARE((*original->snapshot->days[4].snapshot->totals)[3], std::int64_t{4}); QPointer old(&first);
    action(w, "classic-close-tab").trigger(); QVERIFY(!w.statisticsPage());
    profile.put(datePath(Daily), QByteArray::fromStdString(history(Daily, 17)));
    auto& next = open(w); QCOMPARE(next.navigation(), std::optional<ui::StatisticsNavigation>{choices()}); QVERIFY(!next.historyWeekPresentation());
    QTRY_VERIFY_WITH_TIMEOUT(next.historyWeekPresentation() && !bridge.busy(), 30000); QTRY_VERIFY(old.isNull()); QCOMPARE(reads.count(), 2); QCOMPARE(daily.count(), 0);
    QVERIFY(next.historyWeekPresentation() != original); QCOMPARE((*next.historyWeekPresentation()->snapshot->days[4].snapshot->totals)[3], std::int64_t{17});
    QCOMPARE((*original->snapshot->days[4].snapshot->totals)[3], std::int64_t{4});
    QTRY_VERIFY(child<ui::SavedHistoryPlot>(next, "saved-history-week-chart").presentation());
    QCOMPARE(child<ui::SavedHistoryPlot>(next, "saved-history-week-chart").presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QVERIFY(profile.intact());
  }
  void realProfileChangeWhileClosedClearsMemoryAndLoadsNewProfileTotals() {
    Profile a(111), b(333); ui::LibraryWindow w; auto first = std::make_unique<app::SessionBridge>(); wire(w, *first); show(w);
    QSignalSpy firstColumns(first.get(), &app::SessionBridge::columnLayoutsReady); QVERIFY(first->open(a.path.toStdString(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(firstColumns.count() == 1 && !first->busy(), 30000);
    auto& page = open(w); choose(page, choices(false)); QTRY_VERIFY_WITH_TIMEOUT(!first->busy() && !page.loading(), 30000);
    action(w, "classic-close-tab").trigger(); QVERIFY(!w.statisticsPage()); first.reset();
    app::SessionBridge second; wire(w, second); QSignalSpy columns(&second, &app::SessionBridge::columnLayoutsReady);
    QVERIFY(second.open(b.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !second.busy(), 30000);
    auto& reopened = open(w); defaults(reopened); QTRY_VERIFY_WITH_TIMEOUT(!reopened.loading() && !second.busy(), 30000);
    QCOMPARE(child<QLabel>(reopened, "statistics-saved-downloaded").text(), QStringLiteral("333 bytes")); QVERIFY(!reopened.historyPresentation()); QVERIFY(!reopened.historyWeekPresentation());
    QVERIFY(a.intact()); QVERIFY(b.intact());
  }
  void existingPageAndLibraryShortcutsKeepIdentityAndHiddenChoices() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); syntheticTotalsOwner(w);
    auto library = std::make_shared<ui::LibraryPresentation>(); library->revision = 7;
    library->items.push_back({.key = QStringLiteral("A"), .hash = QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), .name = QStringLiteral("Alpha.bin"), .state = QStringLiteral("Stopped"), .bytes = 100, .completed = 50, .canContinue = true}); w.setPresentation(library);
    w.filter().setText(QStringLiteral("Alpha")); w.incompleteTable().setColumnWidth(1, 247); w.incompleteTable().setCurrentIndex(w.incompleteTable().model()->index(0, 0));
    QVERIFY(w.incompleteTable().currentIndex().isValid()); const auto key = w.incompleteTable().model()->data(w.incompleteTable().currentIndex(), ui::LibraryModel::KeyRole); QCOMPARE(key.toString(), QStringLiteral("A"));
    const auto target = w.filesModel().target(); QVERIFY(target); QVERIFY(w.continueAction().isEnabled());
    auto& page = open(w); choose(page, choices(false)); QTRY_VERIFY(!page.loading()); const QPointer identity(&page); QSignalSpy totals(&w, &ui::LibraryWindow::statisticsRequested);
    action(w, "classic-my-torrents").trigger(); QCOMPARE(w.filter().text(), QStringLiteral("Alpha")); QCOMPARE(w.incompleteTable().columnWidth(1), 247);
    QCOMPARE(w.incompleteTable().model()->data(w.incompleteTable().currentIndex(), ui::LibraryModel::KeyRole), key);
    QVERIFY(w.filesModel().target() == target); QVERIFY(w.continueAction().isEnabled());
    QCOMPARE(action(w, "classic-statistics").shortcut(), QKeySequence(QStringLiteral("Ctrl+5"))); w.incompleteTable().setFocus(); QTest::keyClick(&w.incompleteTable(), Qt::Key_5, Qt::ControlModifier);
    QTRY_VERIFY(w.statisticsPage() == identity.data()); QCOMPARE(page.navigation(), std::optional<ui::StatisticsNavigation>{choices(false)});
    const int afterFirst = totals.count(); action(w, "classic-statistics").trigger(); QCoreApplication::processEvents(); QCOMPARE(totals.count(), afterFirst);
    page.tabs().setFocus(); QTest::keyClick(&page.tabs(), Qt::Key_W, Qt::ControlModifier); QTRY_VERIFY(!w.statisticsPage());
    QTest::keyClick(&w, Qt::Key_5, Qt::ControlModifier); QTRY_VERIFY(w.statisticsPage()); QCOMPARE(w.statisticsPage()->navigation(), std::optional<ui::StatisticsNavigation>{choices(false)});
    QTRY_VERIFY(identity.isNull()); action(w, "classic-my-torrents").trigger(); QCOMPARE(w.filter().text(), QStringLiteral("Alpha"));
  }
  void zeroSessionAndAnotherWindowNeverInheritClosedNavigation() {
    ui::LibraryWindow zero; show(zero); choose(open(zero), choices(false)); action(zero, "classic-close-tab").trigger(); defaults(open(zero));
    ui::LibraryWindow first; show(first); QVERIFY(bind(first)); choose(open(first), choices(false)); action(first, "classic-close-tab").trigger();
    ui::LibraryWindow other; show(other); QVERIFY(bind(other)); defaults(open(other));
  }
  void zeroDuplicateAndOlderSessionDoNotClearClosedChoices() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); choose(open(w), choices(false)); action(w, "classic-close-tab").trigger();
    QSignalSpy columns(&w, &ui::LibraryWindow::columnLayoutsRequested); w.beginColumnLayoutSession(0); w.beginColumnLayoutSession(73); w.beginColumnLayoutSession(72);
    QCoreApplication::processEvents(); QCOMPARE(columns.count(), 0); auto& reopened = open(w); QCOMPARE(reopened.navigation(), std::optional<ui::StatisticsNavigation>{choices(false)});
  }
  void acceptedSessionClearsClosedMemoryBeforeItsOutwardCallbacks() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); choose(open(w), choices(false)); action(w, "classic-close-tab").trigger(); bool called = false;
    connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &w, [&](quint64 session, quint64) {
      QCOMPARE(session, quint64{74}); called = true; defaults(open(w));
    });
    w.beginColumnLayoutSession(74); QTRY_VERIFY(called); QVERIFY(w.statisticsPage()); defaults(*w.statisticsPage());
  }
  void visiblePageSessionResetKeepsChoicesAndLaterCloseCapturesNewSession() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); auto& page = open(w); choose(page, choices(false)); QPointer identity(&page);
    w.beginColumnLayoutSession(74); QCOMPARE(w.statisticsPage(), identity.data()); QCOMPARE(page.navigation(), std::optional<ui::StatisticsNavigation>{choices(false)});
    action(w, "classic-close-tab").trigger(); auto& reopened = open(w); QCOMPARE(reopened.navigation(), std::optional<ui::StatisticsNavigation>{choices(false)}); QTRY_VERIFY(identity.isNull());
  }
  void pendingOldPageAndOldPacketsCannotSettleRestoredPageOrCancelWorker() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); syntheticTotalsOwner(w);
    QSignalSpy weeks(&w, &ui::LibraryWindow::historyWeekRequested), cancels(&w, &ui::LibraryWindow::cancelRequested);
    connect(&w, &ui::LibraryWindow::historyWeekRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    auto& first = open(w); choose(first, choices()); QTRY_COMPARE(weeks.count(), 1); const auto oldRequest = weeks.at(0).at(1).toULongLong(); QPointer old(&first);
    action(w, "classic-close-tab").trigger(); auto& reopened = open(w); QCOMPARE(reopened.navigation(), std::optional<ui::StatisticsNavigation>{choices()});
    w.setHistoryWeek(weekPacket(73, oldRequest)); w.setBusy(false); QTRY_COMPARE(weeks.count(), 2); QTRY_VERIFY(old.isNull());
    QVERIFY(!reopened.historyWeekPresentation()); QCOMPARE(cancels.count(), 0); const auto request = weeks.at(1).at(1).toULongLong(); QVERIFY(request > oldRequest);
    w.setHistoryWeek(weekPacket(72, request)); w.setHistoryWeek(weekPacket(73, oldRequest)); QVERIFY(!reopened.historyWeekPresentation());
    w.setHistoryWeek(weekPacket(73, request, 77)); w.setBusy(false); QVERIFY(reopened.historyWeekPresentation());
    const auto accepted = reopened.historyWeekPresentation(); QCOMPARE((*accepted->snapshot->days[4].snapshot->totals)[3], std::int64_t{77});
    w.setHistoryWeek(weekPacket(73, oldRequest)); QCOMPARE(reopened.historyWeekPresentation(), accepted); QCOMPARE(cancels.count(), 0);
  }
  void restoredDispatchCanDeleteWindowWithoutRetainingOldPageOrCallback() {
    auto owner = std::make_unique<ui::LibraryWindow>(); show(*owner); QVERIFY(bind(*owner)); syntheticTotalsOwner(*owner);
    auto& first = open(*owner); choose(first, choices()); QTRY_VERIFY(first.historyWeekPresentation());
    action(*owner, "classic-close-tab").trigger(); QPointer guard(owner.get());
    connect(owner.get(), &ui::LibraryWindow::historyWeekRequested, owner.get(), [&](quint64, quint64, core::HistoryDate) { owner.reset(); });
    open(*owner); QTRY_VERIFY(guard.isNull());
  }
};
QTEST_MAIN(StatisticsNavigationWindowTests)
#include "statistics_navigation_window_tests.moc"
