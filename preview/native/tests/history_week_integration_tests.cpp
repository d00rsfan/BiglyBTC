#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"
#include "history_week_presentation.hpp"

#include <QAction>
#include <QApplication>
#include <QDateEdit>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <chrono>
#include <map>
#include <stdexcept>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate First{2024, 1, 2}, Second{2024, 1, 3}, Absent{2024, 1, 4};
QString datePath(core::HistoryDate d) {
  return QStringLiteral("/stats/%1/%2/%3.dat").arg(d.year, 4, 10, QLatin1Char('0'))
      .arg(d.month, 2, 10, QLatin1Char('0')).arg(d.day, 2, 10, QLatin1Char('0'));
}
std::string history(core::HistoryDate d = First, int payload = 4) {
  using namespace std::chrono;
  const auto start = duration_cast<minutes>(sys_days{year{d.year}/month{d.month}/day{d.day}}.time_since_epoch()).count() + 540;
  return "s,1," + std::to_string(start) + ",100,200,300,400,500,600\n1,2,3," + std::to_string(payload)
      + ",5,6\ne,1," + std::to_string(start + 99) + ",10,20,30," + std::to_string(payload * 10) + ",50,60\n";
}
QByteArray read(const QString& p) {
  QFile f(p); if (!f.open(QIODevice::ReadOnly)) { throw std::runtime_error("Missing owned fixture"); } return f.readAll();
}
void write(const QString& p, const QByteArray& data) {
  if (!QDir().mkpath(QFileInfo(p).absolutePath())) { throw std::runtime_error("Owned fixture directory failed"); }
  QFile f(p); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size() || !f.flush()) {
    throw std::runtime_error("Owned fixture write failed");
  }
}
struct Profile {
  struct File { QByteArray bytes; dev_t device; ino_t inode; };
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-history-week-window-XXXXXX")};
  QString path;
  std::map<QString, File> files;
  Profile() {
    if (!root.isValid()) { throw std::runtime_error("Owned temporary profile failed"); }
    path = root.path() + QStringLiteral("/profile");
    put(QStringLiteral("/downloads.config"), QByteArrayLiteral("d9:downloadslee"));
    put(QStringLiteral("/tables.config"), QByteArrayLiteral("de"));
    put(QStringLiteral("/BiglyBT.statistics"), QByteArrayLiteral("d3:alld10:downloadedi999eee"));
    put(datePath(First), QByteArray::fromStdString(history()));
    using namespace std::chrono;
    const auto anchor = duration_cast<minutes>(sys_days{year{2024}/1/3}.time_since_epoch()).count() + 600;
    put(datePath(Second), QByteArray::fromStdString("s,1," + std::to_string(anchor) + ",9,8,7,6,5,4\n0,0,0,0,0,0\n"));
    put(QStringLiteral("/stats/2024/01/cache.dat"), QByteArrayLiteral("opaque cache must not be read or rewritten"));
  }
  void put(const QString& suffix, const QByteArray& data) {
    const auto p = path + suffix; write(p, data); struct stat s{};
    if (::stat(p.toStdString().c_str(), &s) != 0) { throw std::runtime_error("Owned fixture identity failed"); }
    files[p] = {data, s.st_dev, s.st_ino};
  }
  bool intact() const {
    for (const auto& [p, f] : files) {
      struct stat s{}; if (::stat(p.toStdString().c_str(), &s) != 0 || s.st_dev != f.device || s.st_ino != f.inode || read(p) != f.bytes) { return false; }
    }
    return !QFile::exists(path + datePath(First) + QStringLiteral(".saving"))
        && !QFile::exists(path + datePath(First) + QStringLiteral(".bak"));
  }
};
QAction* action(ui::LibraryWindow& w, const char* id) {
  auto* a = w.findChild<QAction*>(QString::fromLatin1(id)); if (!a) { throw std::runtime_error("Missing classic action"); } return a;
}
void show(ui::LibraryWindow& w) { w.resize(1280, 900); w.show(); w.activateWindow(); QCoreApplication::processEvents(); }
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
ui::StatisticsPage* select(ui::LibraryWindow& w, core::HistoryDate date) {
  action(w, "classic-statistics")->trigger(); auto* p = w.statisticsPage();
  if (!p) { throw std::runtime_error("Missing Statistics page"); }
  auto* tabs = p->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
  auto* edit = p->findChild<QDateEdit*>(QStringLiteral("saved-history-date"));
  if (!tabs || !edit) { throw std::runtime_error("Missing History controls"); }
  edit->setDate(QDate(date.year, static_cast<int>(date.month), static_cast<int>(date.day)));
  p->tabs().setCurrentIndex(1); tabs->setCurrentIndex(1); return p;
}
bool bound(ui::LibraryWindow& w) {
  QSignalSpy columns(&w, &ui::LibraryWindow::columnLayoutsRequested); w.beginColumnLayoutSession(73);
  return QTest::qWaitFor([&] { return columns.count() == 1; }, 3000);
}
auto ready(quint64 session, quint64 request, core::HistoryDate date) {
  auto parsed = core::readSavedHistoryDay(history(date), date); if (!parsed) { throw std::runtime_error("Invalid owned raw History fixture"); }
  auto p = std::make_shared<ui::HistoryPacket>(); p->session = session; p->request = request; p->date = date;
  p->state = ui::HistoryReadState::Ready; p->snapshot = std::make_shared<const core::SavedHistoryDay>(*parsed); return p;
}
auto saved(quint64 session, quint64 request) {
  auto parsed = core::readSavedStatistics("d3:alld10:downloadedi999eee"); if (!parsed) { throw std::runtime_error("Invalid owned saved totals"); }
  auto p = std::make_shared<ui::StatisticsPacket>(); p->session = session; p->request = request;
  p->state = ui::StatisticsReadState::Ready; p->source = ui::StatisticsReadSource::Primary;
  p->snapshot = std::make_shared<const core::SavedStatistics>(std::move(*parsed)); return p;
}
ui::StatisticsPage* selectWeek(ui::LibraryWindow& w, core::HistoryDate date) {
  action(w, "classic-statistics")->trigger(); auto* p = w.statisticsPage();
  if (!p) { throw std::runtime_error("Missing Statistics page"); }
  auto* transfers = p->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
  auto* periods = p->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs"));
  auto* edit = p->findChild<QDateEdit*>(QStringLiteral("saved-history-week-date"));
  if (!transfers || !periods || !edit) { throw std::runtime_error("Missing seven-day controls"); }
  edit->setDate(QDate(date.year, static_cast<int>(date.month), static_cast<int>(date.day)));
  periods->setCurrentIndex(1); p->tabs().setCurrentIndex(1); transfers->setCurrentIndex(1); return p;
}
auto weekReady(quint64 session, quint64 request, core::HistoryDate end) {
  auto dates = core::savedHistoryWeekDates(end); if (!dates) { throw std::runtime_error("Bad owned seven-day date"); }
  auto value = std::make_shared<core::SavedHistoryWeek>(); value->endingDate = end;
  for (std::size_t i = 0; i < dates->size(); ++i) {
    value->days[i].date = (*dates)[i];
    auto parsed = core::readSavedHistoryDay(history((*dates)[i]), (*dates)[i]);
    if (!parsed) { throw std::runtime_error("Bad owned seven-day records"); }
    value->days[i].snapshot.emplace(std::move(*parsed));
  }
  auto packet = std::make_shared<ui::HistoryWeekPacket>(); packet->session = session; packet->request = request;
  packet->endingDate = end; packet->state = ui::HistoryWeekReadState::Ready; packet->snapshot = std::move(value); return packet;
}
} // namespace

class HistoryWeekIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void realWeekLoadsLazilyAndF5KeepsDailyAndFavoriteStateIndependent() {
    Profile p; p.put(QStringLiteral("/stats/2023/12/30.dat"), QByteArrayLiteral(""));
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), totals(&b, &app::SessionBridge::statisticsReady);
    QSignalSpy weeks(&b, &app::SessionBridge::historyWeekReady), days(&b, &app::SessionBridge::historyReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    w.filter().setText(QStringLiteral("retained week filter")); w.incompleteTable().setColumnWidth(1, 247);
    action(w, "classic-statistics")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !b.busy(), 30000);
    QCOMPARE(weeks.count(), 0); QCOMPARE(days.count(), 0); auto* page = selectWeek(w, Second);
    QTRY_VERIFY_WITH_TIMEOUT(page->historyWeekPresentation() && page->historyWeekPresentation()->state == ui::HistoryWeekReadState::Ready && !b.busy(), 30000);
    QCOMPARE(weeks.count(), 1); QCOMPARE(days.count(), 0); const auto frame = page->historyWeekPresentation();
    QCOMPARE(frame->snapshot->days.front().date, (core::HistoryDate{2023, 12, 28})); QCOMPARE(frame->snapshot->days.back().date, Second);
    QVERIFY(!frame->snapshot->days[0].snapshot); QVERIFY(frame->snapshot->days[2].snapshot); QVERIFY(!frame->snapshot->days[2].snapshot->totals);
    QCOMPARE((*frame->snapshot->days[5].snapshot->totals)[3], std::int64_t{44}); QCOMPARE((*frame->snapshot->days[6].snapshot->totals)[3], std::int64_t{0});
    auto* table = page->findChild<QTableView*>(QStringLiteral("saved-history-week-days")); QVERIFY(table);
    QCOMPARE(table->model()->rowCount(), 7); QCOMPARE(table->model()->columnCount(), 8);
    QCOMPARE(table->model()->data(table->model()->index(5, 5), Qt::UserRole).toLongLong(), qint64{44});
    QVERIFY(!table->model()->data(table->model()->index(0, 5), Qt::UserRole).isValid());
    QCOMPARE(table->model()->data(table->model()->index(6, 5), Qt::DisplayRole).toString(), QStringLiteral("0 bytes"));
    p.put(datePath(First), QByteArray::fromStdString(history(First, 5))); action(w, "classic-refresh")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(weeks.count() == 2 && !b.busy(), 30000);
    QCOMPARE((*page->historyWeekPresentation()->snapshot->days[5].snapshot->totals)[3], std::int64_t{55});
    QCOMPARE((*frame->snapshot->days[5].snapshot->totals)[3], std::int64_t{44}); QCOMPARE(totals.count(), 1);
    auto* dailyDate = page->findChild<QDateEdit*>(QStringLiteral("saved-history-date")); QVERIFY(dailyDate);
    dailyDate->setDate(QDate(First.year, static_cast<int>(First.month), static_cast<int>(First.day)));
    auto* modes = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs")); QVERIFY(modes); modes->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && page->historyPresentation()->state == ui::HistoryReadState::Ready && !b.busy(), 30000);
    QCOMPARE(days.count(), 1); QCOMPARE(weeks.count(), 2); action(w, "classic-refresh")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(days.count() == 2 && !b.busy(), 30000); QCOMPARE(weeks.count(), 2);
    modes->setCurrentIndex(1); QCoreApplication::processEvents(); QCOMPARE(weeks.count(), 2);
    action(w, "classic-my-torrents")->trigger(); QCOMPARE(w.filter().text(), QStringLiteral("retained week filter"));
    QCOMPARE(w.incompleteTable().columnWidth(1), 247); QCOMPARE(snapshots.count(), 1); QVERIFY(p.intact());
    action(w, "classic-statistics")->trigger(); action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage());
    page = selectWeek(w, Second); QTRY_VERIFY_WITH_TIMEOUT(weeks.count() == 3 && page->historyWeekPresentation() && !b.busy(), 30000);
    QCOMPARE((*page->historyWeekPresentation()->snapshot->days[5].snapshot->totals)[3], std::int64_t{55}); QVERIFY(p.intact());
  }
  void directBridgeValidatesRangeAndKeepsTheWorkerBusyThroughDelivery() {
    Profile p; app::SessionBridge b; QSignalSpy sessions(&b, &app::SessionBridge::columnLayoutSessionReady), reads(&b, &app::SessionBridge::historyWeekReady);
    QVERIFY(!b.historyWeek(1, 1, Second)); QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(sessions.count() == 1 && !b.busy(), 30000);
    const auto session = sessions.at(0).at(0).toULongLong();
    QVERIFY(!b.historyWeek(session, 0, Second)); QVERIFY(!b.historyWeek(session + 1, 1, Second));
    QVERIFY(!b.historyWeek(session, 1, {1970, 1, 6})); QVERIFY(!b.historyWeek(session, 1, {2024, 2, 30}));
    bool held = false, rejected = false;
    QObject::connect(&b, &app::SessionBridge::historyWeekReady, &b, [&](ui::HistoryWeekPresentation packet) {
      held = b.busy(); rejected = !b.historyWeek(session, 8, First) && !b.history(session, 8, First) && !b.statistics(session, 8) && !b.refresh();
      QVERIFY(packet && packet->request == 7 && packet->endingDate == Second && packet->state == ui::HistoryWeekReadState::Ready); b.cancel();
    });
    QVERIFY(b.historyWeek(session, 7, Second)); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !b.busy(), 30000);
    QVERIFY(held && rejected); QVERIFY(p.intact());
  }
  void missingMiddleFailureAndUnsafePathDoNotReviseLibrary_data() {
    QTest::addColumn<int>("kind"); QTest::newRow("all-missing-ready") << 0; QTest::newRow("middle-malformed") << 1; QTest::newRow("middle-symlink") << 2;
  }
  void missingMiddleFailureAndUnsafePathDoNotReviseLibrary() {
    QFETCH(int, kind); Profile p; p.put(QStringLiteral("/stats/2023/12/30.dat"), QByteArrayLiteral(""));
    if (kind == 1) { p.put(QStringLiteral("/stats/2023/12/31.dat"), QByteArrayLiteral("s,9,1,0,0,0,0,0,0\n")); }
    if (kind == 2) {
      p.put(QStringLiteral("/outside.dat"), QByteArrayLiteral("s,1,1,0,0,0,0,0,0\n"));
      QVERIFY(QFile::link(p.path + QStringLiteral("/outside.dat"), p.path + QStringLiteral("/stats/2023/12/31.dat")));
    }
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    auto* page = selectWeek(w, kind == 0 ? core::HistoryDate{Absent.year, Absent.month + 1, Absent.day} : Second);
    QTRY_VERIFY_WITH_TIMEOUT(page->historyWeekPresentation() && !b.busy(), 30000); const auto packet = page->historyWeekPresentation();
    if (kind == 0) {
      QCOMPARE(packet->state, ui::HistoryWeekReadState::Ready); QVERIFY(packet->snapshot);
      for (const auto& row : packet->snapshot->days) { QVERIFY(!row.snapshot); }
    } else { QCOMPARE(packet->state, ui::HistoryWeekReadState::Failed); QVERIFY(!packet->snapshot); QVERIFY(!packet->failure.isEmpty()); }
    QCOMPARE(snapshots.count(), 1); QVERIFY(p.intact());
  }
  void pendingDateAndModeCoalesceWithoutDroppingSavedTotalsOrAcceptingWrongKinds() {
    ui::LibraryWindow w; show(w); QVERIFY(bound(w));
    QSignalSpy totals(&w, &ui::LibraryWindow::statisticsRequested), weeks(&w, &ui::LibraryWindow::historyWeekRequested), days(&w, &ui::LibraryWindow::historyRequested);
    QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&](quint64, quint64) { w.setBusy(true); });
    QObject::connect(&w, &ui::LibraryWindow::historyWeekRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    QObject::connect(&w, &ui::LibraryWindow::historyRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(totals.count(), 1); auto* page = selectWeek(w, Second);
    auto* edit = page->findChild<QDateEdit*>(QStringLiteral("saved-history-week-date")); QVERIFY(edit); edit->setDate(QDate(2024, 1, 2));
    select(w, First); auto* modes = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs")); QVERIFY(modes); modes->setCurrentIndex(0);
    selectWeek(w, Second); QCOMPARE(weeks.count(), 0); QCOMPARE(days.count(), 0);
    w.setStatistics(saved(73, totals.at(0).at(1).toULongLong())); w.setBusy(false); QTRY_COMPARE(weeks.count(), 1);
    const auto id = weeks.at(0).at(1).toULongLong();
    w.setHistory(ready(73, id, Second)); w.setHistoryWeek(weekReady(73, id, First));
    QVERIFY(!page->historyWeekPresentation()); QCOMPARE(weeks.count(), 1);
    auto* savedRefresh = page->findChild<QPushButton*>(QStringLiteral("statistics-refresh")); QVERIFY(savedRefresh); savedRefresh->click();
    edit->setDate(QDate(2024, 1, 2)); w.setHistoryWeek(weekReady(73, id, Second)); w.setBusy(false);
    QTRY_COMPARE(totals.count(), 2); QCOMPARE(weeks.count(), 1); QCOMPARE(days.count(), 0);
    w.setHistoryWeek(weekReady(73, totals.at(1).at(1).toULongLong(), First));
    w.setStatistics(saved(73, totals.at(1).at(1).toULongLong())); w.setBusy(false); QTRY_COMPARE(weeks.count(), 2);
    QCOMPARE(qvariant_cast<core::HistoryDate>(weeks.at(1).at(2)), First);
    w.setHistoryWeek(weekReady(73, weeks.at(1).at(1).toULongLong(), First)); w.setBusy(false);
    QVERIFY(page->historyWeekPresentation()); QCOMPARE(page->historyWeekPresentation()->endingDate, First);
  }
  void closeReopenRejectsOldPageWithoutCancelingTheOwnedRead() {
    ui::LibraryWindow w; show(w); QVERIFY(bound(w)); QSignalSpy reads(&w, &ui::LibraryWindow::historyWeekRequested), cancels(&w, &ui::LibraryWindow::cancelRequested);
    QObject::connect(&w, &ui::LibraryWindow::historyWeekRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    QPointer old(selectWeek(w, Second)); QTRY_COMPARE(reads.count(), 1); const auto id = reads.at(0).at(1).toULongLong();
    action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage()); auto* page = selectWeek(w, Second);
    w.setHistoryWeek(weekReady(73, id, Second)); w.setBusy(false); QTRY_COMPARE(reads.count(), 2); QTRY_VERIFY(old.isNull());
    QVERIFY(!page->historyWeekPresentation()); QCOMPARE(cancels.count(), 0);
    w.setHistoryWeek(weekReady(73, reads.at(1).at(1).toULongLong(), Second)); w.setBusy(false); QVERIFY(page->historyWeekPresentation());
  }
  void profileChangeClearsBothViewsAndRejectsTheOldWeekSession() {
    ui::LibraryWindow w; show(w); QVERIFY(bound(w)); QSignalSpy reads(&w, &ui::LibraryWindow::historyWeekRequested);
    QObject::connect(&w, &ui::LibraryWindow::historyWeekRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    auto* page = selectWeek(w, Second); QTRY_COMPARE(reads.count(), 1); const auto id = reads.at(0).at(1).toULongLong();
    w.beginColumnLayoutSession(74); QVERIFY(!page->historyWeekPresentation()); QVERIFY(!page->historyPresentation());
    w.setHistoryWeek(weekReady(73, id, Second)); w.setBusy(false); QTRY_COMPARE(reads.count(), 2);
    QCOMPARE(reads.at(1).at(0).toULongLong(), quint64{74}); QVERIFY(!page->historyWeekPresentation());
    w.setHistoryWeek(weekReady(74, reads.at(1).at(1).toULongLong(), Second)); w.setBusy(false); QVERIFY(page->historyWeekPresentation());
  }
  void canceledDeliveryNeverPublishesAWeekOrRefreshesTheLibrary() {
    Profile p; ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), reads(&b, &app::SessionBridge::historyWeekReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    action(w, "classic-statistics")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(!b.busy(), 30000);
    QObject::connect(&w, &ui::LibraryWindow::historyWeekRequested, &w, [&](quint64, quint64, core::HistoryDate) { b.cancel(); });
    auto* page = selectWeek(w, Second); auto* status = page->findChild<QLabel*>(QStringLiteral("saved-history-week-status")); QVERIFY(status);
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && status->text().contains(QStringLiteral("did not complete")), 30000);
    QCOMPARE(reads.count(), 0); QCOMPARE(snapshots.count(), 1); QVERIFY(p.intact());
  }
  void deletingTheWindowDuringWeekDispatchIsSafe() {
    auto owner = std::make_unique<ui::LibraryWindow>(); show(*owner); QVERIFY(bound(*owner)); QPointer guard(owner.get());
    QObject::connect(owner.get(), &ui::LibraryWindow::historyWeekRequested, owner.get(), [&](quint64, quint64, core::HistoryDate) { owner.reset(); });
    selectWeek(*owner, Second); QTRY_VERIFY(guard.isNull());
  }
};
QTEST_MAIN(HistoryWeekIntegrationTests)
#include "history_week_integration_tests.moc"
