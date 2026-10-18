#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"

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
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-history-window-XXXXXX")};
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
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &b, &app::SessionBridge::columnLayouts);
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &b, &app::SessionBridge::statistics);
  QObject::connect(&w, &ui::LibraryWindow::historyRequested, &b, &app::SessionBridge::history);
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
} // namespace

class HistoryIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void realHistoryIsLazyRefreshableAndPreservesSavedFilesAndFavoriteView() {
    Profile p; ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), reads(&b, &app::SessionBridge::historyReady);
    QSignalSpy totals(&b, &app::SessionBridge::statisticsReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    w.filter().setText(QStringLiteral("retained filter")); w.incompleteTable().setColumnWidth(1, 247);
    action(w, "classic-statistics")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !b.busy(), 30000);
    QCOMPARE(reads.count(), 0); auto* page = select(w, First);
    QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && page->historyPresentation()->state == ui::HistoryReadState::Ready && !b.busy(), 30000);
    QCOMPARE(page->historyPresentation()->date, First); QCOMPARE(page->historyPresentation()->snapshot->observations, std::size_t{2});
    QCOMPARE((*page->historyPresentation()->snapshot->totals)[3], std::int64_t{44});
    auto* table = page->findChild<QTableView*>(QStringLiteral("saved-history-hours")); QVERIFY(table);
    QCOMPARE(table->model()->rowCount(), 24); QCOMPARE(table->model()->columnCount(), 7);
    QCOMPARE(table->model()->data(table->model()->index(9, 4), Qt::UserRole).toLongLong(), qint64{44});
    QVERIFY(!table->model()->data(table->model()->index(8, 4), Qt::UserRole).isValid());
    const auto old = page->historyPresentation();
    p.put(datePath(First), QByteArray::fromStdString(history(First, 5))); action(w, "classic-refresh")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 2 && !b.busy(), 30000);
    QCOMPARE((*page->historyPresentation()->snapshot->totals)[3], std::int64_t{55}); QCOMPARE((*old->snapshot->totals)[3], std::int64_t{44});
    QCOMPARE(totals.count(), 1); select(w, Second);
    QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && page->historyPresentation()->date == Second && !b.busy(), 30000);
    QVERIFY(page->historyPresentation()->snapshot->totals); QCOMPARE((*page->historyPresentation()->snapshot->totals)[3], std::int64_t{0});
    QCOMPARE(table->model()->data(table->model()->index(10, 4), Qt::DisplayRole).toString(), QStringLiteral("0 bytes"));
    auto* previous = page->findChild<QPushButton*>(QStringLiteral("saved-history-previous")); QVERIFY(previous); previous->click();
    QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && page->historyPresentation()->date == First && !b.busy(), 30000);
    action(w, "classic-my-torrents")->trigger(); QCOMPARE(w.filter().text(), QStringLiteral("retained filter"));
    QCOMPARE(w.incompleteTable().columnWidth(1), 247); QCOMPARE(snapshots.count(), 1); QVERIFY(p.intact());
    action(w, "classic-statistics")->trigger(); action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage());
    page = select(w, First); QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && page->historyPresentation()->date == First && !b.busy(), 30000);
    QCOMPARE((*page->historyPresentation()->snapshot->totals)[3], std::int64_t{55}); QVERIFY(p.intact());
  }
  void directBridgeBindsDateAndKeepsTheWorkerBusyThroughDelivery() {
    Profile p; app::SessionBridge b; QSignalSpy sessions(&b, &app::SessionBridge::columnLayoutSessionReady), reads(&b, &app::SessionBridge::historyReady);
    QVERIFY(!b.history(1, 1, First)); QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(sessions.count() == 1 && !b.busy(), 30000);
    const auto session = sessions.at(0).at(0).toULongLong();
    QVERIFY(!b.history(session, 0, First)); QVERIFY(!b.history(session + 1, 1, First)); QVERIFY(!b.history(session, 1, {2024, 2, 30}));
    bool held = false, rejected = false;
    QObject::connect(&b, &app::SessionBridge::historyReady, &b, [&](ui::HistoryPresentation result) {
      held = b.busy(); rejected = !b.history(session, 8, Second) && !b.statistics(session, 8) && !b.refresh();
      QVERIFY(result && result->date == First && result->request == 7 && result->state == ui::HistoryReadState::Ready); b.cancel();
    });
    QVERIFY(b.history(session, 7, First)); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !b.busy(), 30000);
    QVERIFY(held && rejected); QVERIFY(p.intact());
  }
  void missingMalformedAndUnsafeHistoryDoNotReviseTheLibrary_data() {
    QTest::addColumn<int>("kind"); QTest::newRow("missing") << 0; QTest::newRow("unsupported-version") << 1; QTest::newRow("symlink") << 2;
  }
  void missingMalformedAndUnsafeHistoryDoNotReviseTheLibrary() {
    QFETCH(int, kind); Profile p;
    if (kind == 1) { p.put(datePath(Absent), QByteArrayLiteral("s,2,28401600,0,0,0,0,0,0\n")); }
    if (kind == 2) { QVERIFY(QFile::link(p.path + datePath(First), p.path + datePath(Absent))); }
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    auto* page = select(w, Absent); QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && !b.busy(), 30000);
    QCOMPARE(page->historyPresentation()->date, Absent);
    QCOMPARE(page->historyPresentation()->state, kind == 0 ? ui::HistoryReadState::Missing : ui::HistoryReadState::Failed);
    QVERIFY(!page->historyPresentation()->snapshot); QCOMPARE(snapshots.count(), 1); QCOMPARE(w.model().rowCount(), 0); QVERIFY(p.intact());
  }
  void pendingDatesCoalesceWithoutDroppingSavedTotalsOrAcceptingWrongKinds() {
    ui::LibraryWindow w; show(w); QVERIFY(bound(w));
    QSignalSpy totals(&w, &ui::LibraryWindow::statisticsRequested), reads(&w, &ui::LibraryWindow::historyRequested);
    QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&w] { w.setBusy(true); });
    QObject::connect(&w, &ui::LibraryWindow::historyRequested, &w, [&w] { w.setBusy(true); });
    action(w, "classic-statistics")->trigger(); QTRY_COMPARE(totals.count(), 1);
    auto* page = select(w, First); select(w, Second); select(w, First); select(w, Second);
    QCoreApplication::processEvents(); QCOMPARE(reads.count(), 0);
    w.setStatistics(saved(73, totals.at(0).at(1).toULongLong())); w.setBusy(false); QTRY_COMPARE(reads.count(), 1);
    const auto id = reads.at(0).at(1).toULongLong(); QCOMPARE(qvariant_cast<core::HistoryDate>(reads.at(0).at(2)), Second);
    w.setHistory(ready(73, id, First)); QVERIFY(!page->historyPresentation());
    w.setStatistics(saved(73, id)); QVERIFY(!page->historyPresentation()); // Same numeric id, wrong read kind.
    w.setHistory(ready(73, id, Second)); w.setBusy(false); QVERIFY(page->historyPresentation());
    auto* refreshSaved = page->findChild<QPushButton*>(QStringLiteral("statistics-refresh")); QVERIFY(refreshSaved); refreshSaved->click(); QTRY_COMPARE(totals.count(), 2);
    select(w, First); QCoreApplication::processEvents(); QCOMPARE(reads.count(), 1);
    w.setStatistics(saved(73, totals.at(1).at(1).toULongLong())); w.setBusy(false); QTRY_COMPARE(reads.count(), 2);
    w.setHistory(ready(73, reads.at(1).at(1).toULongLong(), First)); w.setBusy(false);
    QCOMPARE(page->historyPresentation()->date, First); QCOMPARE(page->readState(), std::optional{ui::StatisticsReadState::Ready});
  }
  void closeReopenRejectsClosedPageAndReplaysOnlyTheNewRead() {
    ui::LibraryWindow w; show(w); QVERIFY(bound(w));
    QSignalSpy reads(&w, &ui::LibraryWindow::historyRequested), cancels(&w, &ui::LibraryWindow::cancelRequested);
    QObject::connect(&w, &ui::LibraryWindow::historyRequested, &w, [&w] { w.setBusy(true); });
    QPointer old(select(w, First)); QTRY_COMPARE(reads.count(), 1); const auto id = reads.at(0).at(1).toULongLong();
    action(w, "classic-close-tab")->trigger(); QVERIFY(!w.statisticsPage()); auto* page = select(w, Second);
    w.setHistory(ready(73, id, First)); w.setBusy(false); QTRY_COMPARE(reads.count(), 2); QTRY_VERIFY(old.isNull());
    QVERIFY(!page->historyPresentation()); w.setHistory(ready(73, reads.at(1).at(1).toULongLong(), Second)); w.setBusy(false);
    QCOMPARE(page->historyPresentation()->date, Second); QCOMPARE(cancels.count(), 0);
  }
  void newProfileClearsHistoryAndRejectsOldSession() {
    ui::LibraryWindow w; show(w); QVERIFY(bound(w)); QSignalSpy reads(&w, &ui::LibraryWindow::historyRequested);
    QObject::connect(&w, &ui::LibraryWindow::historyRequested, &w, [&w] { w.setBusy(true); });
    auto* page = select(w, First); QTRY_COMPARE(reads.count(), 1);
    auto old = ready(73, reads.at(0).at(1).toULongLong(), First); w.setHistory(old); w.setBusy(false); QVERIFY(page->historyPresentation());
    w.beginColumnLayoutSession(74); QVERIFY(!page->historyPresentation()); w.setHistory(old); QVERIFY(!page->historyPresentation());
    QTRY_COMPARE(reads.count(), 2); const auto session = reads.at(1).at(0).toULongLong(); QCOMPARE(session, quint64{74});
    w.setHistory(ready(session, reads.at(1).at(1).toULongLong(), First)); w.setBusy(false); QCOMPARE(page->historyPresentation()->session, session);
  }
  void cancellationBeforeQueuedDeliveryNeverPublishesAHistoryPacket() {
    Profile p; ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), reads(&b, &app::SessionBridge::historyReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    QObject::connect(&w, &ui::LibraryWindow::historyRequested, &b, [&b] { b.cancel(); });
    auto* page = select(w, First); auto* status = page->findChild<QLabel*>(QStringLiteral("saved-history-status")); QVERIFY(status);
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && status->text().contains(QStringLiteral("did not complete")), 30000);
    QCOMPARE(reads.count(), 0); QCOMPARE(snapshots.count(), 1); QVERIFY(p.intact());
  }
  void deletingWindowDuringHistoryDispatchIsSafe() {
    auto owner = std::make_unique<ui::LibraryWindow>(); show(*owner); QVERIFY(bound(*owner)); QPointer guard(owner.get());
    QObject::connect(owner.get(), &ui::LibraryWindow::historyRequested, qApp, [&] { owner.reset(); });
    select(*owner, First); QTRY_VERIFY(guard.isNull());
  }
};
QTEST_MAIN(HistoryIntegrationTests)
#include "history_integration_tests.moc"
