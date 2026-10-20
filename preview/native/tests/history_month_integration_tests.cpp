#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"
#include "saved_history_plot.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate End{2024, 3, 2}, Leap{2024, 2, 29};
template<class T> T& child(QObject& root, const char* id) {
  auto* found = root.findChild<T*>(QString::fromLatin1(id)); if (!found) { throw std::runtime_error("Missing generated-profile UI control"); } return *found;
}
QAction& action(ui::LibraryWindow& w, const char* id) { return child<QAction>(w, id); }
QString datePath(core::HistoryDate d) { return QStringLiteral("/stats/%1/%2/%3.dat").arg(d.year, 4, 10, QLatin1Char('0')).arg(d.month, 2, 10, QLatin1Char('0')).arg(d.day, 2, 10, QLatin1Char('0')); }
std::string history(core::HistoryDate date, int payload = 4) {
  using namespace std::chrono;
  const auto minute = duration_cast<minutes>(sys_days{year{date.year}/month{date.month}/day{date.day}}.time_since_epoch()).count() + 540;
  return "s,1," + std::to_string(minute) + ",100,200,300,400,500,600\n1,2,3," + std::to_string(payload) + ",5,6\ne,1," + std::to_string(minute + 99) + ",10,20,30," + std::to_string(payload * 10) + ",50,60\n";
}
struct Profile {
  struct File { QByteArray bytes; dev_t device; ino_t inode; };
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-history-month-window-XXXXXX")}; QString path; std::map<QString, File> files;
  explicit Profile(int saved = 999) {
    if (!root.isValid()) { throw std::runtime_error("Generated profile root failed"); } path = root.path() + QStringLiteral("/profile");
    put(QStringLiteral("/downloads.config"), QByteArrayLiteral("d9:downloadslee")); put(QStringLiteral("/tables.config"), QByteArrayLiteral("de"));
    put(QStringLiteral("/BiglyBT.statistics"), QByteArray::fromStdString("d3:alld10:downloadedi" + std::to_string(saved) + "eee"));
    put(datePath(Leap), QByteArray::fromStdString(history(Leap))); put(datePath({2024, 2, 3}), QByteArray{});
    put(datePath({2024, 3, 1}), QByteArray::fromStdString(history({2024, 3, 1}, 0)));
    put(datePath(End), QByteArray::fromStdString(history(End, -4)));
    put(QStringLiteral("/stats/2024/02/cache.dat"), QByteArrayLiteral("opaque cache is not a daily record"));
  }
  void put(const QString& suffix, const QByteArray& bytes) {
    const auto p = path + suffix; if (!QDir().mkpath(QFileInfo(p).absolutePath())) { throw std::runtime_error("Generated directory failed"); }
    QFile out(p); if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size() || !out.flush()) { throw std::runtime_error("Generated fixture write failed"); }
    struct stat s{}; if (::stat(p.toStdString().c_str(), &s) != 0) { throw std::runtime_error("Generated fixture identity failed"); } files[p] = {bytes, s.st_dev, s.st_ino};
  }
  bool intact() const {
    for (const auto& [p, file] : files) { struct stat s{}; QFile input(p); if (::stat(p.toStdString().c_str(), &s) != 0 || s.st_dev != file.device || s.st_ino != file.inode || !input.open(QIODevice::ReadOnly) || input.readAll() != file.bytes || QFile::exists(p + QStringLiteral(".bak")) || QFile::exists(p + QStringLiteral(".saving"))) { return false; } } return true;
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
  QObject::connect(&b, &app::SessionBridge::historyMonthReady, &w, &ui::LibraryWindow::setHistoryMonth);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &b, &app::SessionBridge::columnLayouts);
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &b, &app::SessionBridge::statistics);
  QObject::connect(&w, &ui::LibraryWindow::historyRequested, &b, &app::SessionBridge::history);
  QObject::connect(&w, &ui::LibraryWindow::historyWeekRequested, &b, &app::SessionBridge::historyWeek);
  QObject::connect(&w, &ui::LibraryWindow::historyMonthRequested, &b, &app::SessionBridge::historyMonth);
  QObject::connect(&w, &ui::LibraryWindow::cancelRequested, &b, &app::SessionBridge::cancel);
}
void show(ui::LibraryWindow& w) { w.resize(1280, 900); w.show(); w.activateWindow(); QCoreApplication::processEvents(); }
ui::StatisticsPage& open(ui::LibraryWindow& w) { action(w, "classic-statistics").trigger(); if (!w.statisticsPage()) { throw std::runtime_error("Statistics did not open"); } return *w.statisticsPage(); }
ui::StatisticsPage& select(ui::LibraryWindow& w, int mode = 2, core::HistoryDate end = End) {
  auto& page = open(w); page.tabs().setCurrentIndex(0);
  const char* id = mode == 2 ? "saved-history-month-date" : mode == 1 ? "saved-history-week-date" : "saved-history-date";
  child<QDateEdit>(page, id).setDate(QDate(end.year, static_cast<int>(end.month), static_cast<int>(end.day)));
  child<QTabWidget>(page, "statistics-history-tabs").setCurrentIndex(mode); child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(1); page.tabs().setCurrentIndex(1); return page;
}
bool bind(ui::LibraryWindow& w, quint64 session = 73) { QSignalSpy columns(&w, &ui::LibraryWindow::columnLayoutsRequested); w.beginColumnLayoutSession(session); return QTest::qWaitFor([&] { return columns.count() == 1; }, 3000); }
auto saved(quint64 session, quint64 request) {
  const auto parsed = core::readSavedStatistics("d3:alld10:downloadedi999eee"); if (!parsed) { throw std::runtime_error("Invalid saved totals fixture"); }
  auto value = std::make_shared<ui::StatisticsPacket>(); value->session = session; value->request = request; value->state = ui::StatisticsReadState::Ready; value->source = ui::StatisticsReadSource::Primary; value->snapshot = std::make_shared<const core::SavedStatistics>(*parsed); return value;
}
void totalsOwner(ui::LibraryWindow& w) {
  QObject::connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&w](quint64 session, quint64 request) { w.setBusy(true); QTimer::singleShot(0, &w, [&w, session, request] { w.setStatistics(saved(session, request)); w.setBusy(false); }); });
}
auto monthPacket(quint64 session, quint64 request, core::HistoryDate end = End) {
  const auto dates = core::savedHistoryMonthDates(end); if (!dates) { throw std::runtime_error("Invalid generated month"); }
  auto source = std::make_shared<core::SavedHistoryMonth>(); source->endingDate = end;
  for (std::size_t i = 0; i < dates->size(); ++i) { source->days[i].date = (*dates)[i]; }
  auto parsed = core::readSavedHistoryDay(history(end), end); if (!parsed) { throw std::runtime_error("Invalid generated day"); } source->days.back().snapshot = std::move(*parsed);
  auto value = std::make_shared<ui::HistoryMonthPacket>(); value->session = session; value->request = request; value->endingDate = end; value->state = ui::HistoryMonthReadState::Ready; value->snapshot = std::move(source); return value;
}
auto dayPacket(quint64 session, quint64 request, core::HistoryDate date = End) {
  auto parsed = core::readSavedHistoryDay(history(date), date); if (!parsed) { throw std::runtime_error("Invalid generated day"); }
  auto value = std::make_shared<ui::HistoryPacket>(); value->session = session; value->request = request; value->date = date; value->state = ui::HistoryReadState::Ready; value->snapshot = std::make_shared<const core::SavedHistoryDay>(std::move(*parsed)); return value;
}
auto weekPacket(quint64 session, quint64 request, core::HistoryDate end = End) {
  const auto dates = core::savedHistoryWeekDates(end); if (!dates) { throw std::runtime_error("Invalid generated week"); } auto source = std::make_shared<core::SavedHistoryWeek>(); source->endingDate = end;
  for (std::size_t i = 0; i < dates->size(); ++i) { source->days[i].date = (*dates)[i]; }
  auto value = std::make_shared<ui::HistoryWeekPacket>(); value->session = session; value->request = request; value->endingDate = end; value->state = ui::HistoryWeekReadState::Ready; value->snapshot = source; return value;
}
class Clipboard final {
public:
  QClipboard* const value = QGuiApplication::clipboard();
  Clipboard() : original_(std::make_unique<QMimeData>()) { if (const auto* data = value->mimeData(QClipboard::Clipboard)) { for (const auto& f : data->formats()) { original_->setData(f, data->data(f)); } } }
  ~Clipboard() { value->setMimeData(original_.release(), QClipboard::Clipboard); }
  Clipboard(const Clipboard&) = delete; Clipboard& operator=(const Clipboard&) = delete;
private: std::unique_ptr<QMimeData> original_;
};
} // namespace

class HistoryMonthIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); qRegisterMetaType<core::HistoryDate>(); }
  void actualHeldProfileLoadsThirtyStatesAndTrafficKeyboardCopyNeverReread() {
    Profile p; ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w);
    QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), totals(&b, &app::SessionBridge::statisticsReady), reads(&b, &app::SessionBridge::historyMonthReady), days(&b, &app::SessionBridge::historyReady), weeks(&b, &app::SessionBridge::historyWeekReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000);
    w.filter().setText(QStringLiteral("retained month filter")); w.incompleteTable().setColumnWidth(1, 247); open(w); QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !b.busy(), 30000); QCOMPARE(reads.count(), 0);
    auto& page = select(w); QTRY_VERIFY_WITH_TIMEOUT(page.historyMonthPresentation() && !b.busy(), 30000); const auto frame = page.historyMonthPresentation(); QCOMPARE(frame->state, ui::HistoryMonthReadState::Ready); QCOMPARE(frame->snapshot->days.front().date, (core::HistoryDate{2024, 2, 2})); QCOMPARE(frame->snapshot->days.back().date, End);
    auto& rows = *child<QTableView>(page, "saved-history-month-days").model(); QCOMPARE(rows.rowCount(), 30); QCOMPARE(rows.columnCount(), 8);
    QCOMPARE(rows.data(rows.index(0, 1)).toString(), QStringLiteral("Missing")); QCOMPARE(rows.data(rows.index(1, 1)).toString(), QStringLiteral("No observations"));
    QCOMPARE(rows.data(rows.index(27, 5), Qt::UserRole).toLongLong(), qint64{44}); QCOMPARE(rows.data(rows.index(28, 5)).toString(), QStringLiteral("0 bytes")); QCOMPARE(rows.data(rows.index(29, 5), Qt::UserRole).toLongLong(), qint64{-44});
    const int originalSnapshots = snapshots.count(); QSignalSpy changes(&rows, &QAbstractItemModel::dataChanged); Clipboard clipboard;
    auto& plot = child<ui::SavedHistoryPlot>(page, "saved-history-month-chart"); auto& traffic = child<QComboBox>(page, "saved-history-month-traffic");
    const std::array<qint64, 3> expectedDown{-44, 33, 66}, expectedUp{22, 11, 55};
    for (int choice = 0; choice < 3; ++choice) {
      traffic.setCurrentIndex(choice); QTRY_COMPARE(plot.presentation()->traffic(), static_cast<ui::SavedHistoryTraffic>(choice)); QCOMPARE(plot.presentation()->buckets().back().downloaded, std::optional<std::int64_t>{expectedDown[static_cast<std::size_t>(choice)]}); QCOMPARE(plot.presentation()->buckets().back().uploaded, std::optional<std::int64_t>{expectedUp[static_cast<std::size_t>(choice)]});
      plot.setFocus(); QTRY_VERIFY(plot.hasFocus()); QTest::keyClick(&plot, Qt::Key_End); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{29}); child<QAction>(plot, "saved-history-copy-selection").trigger(); QCOMPARE(clipboard.value->text(), plot.presentation()->bucketToolTip(29));
    }
    QCOMPARE(reads.count(), 1); QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QCOMPARE(snapshots.count(), originalSnapshots); QCOMPARE(changes.count(), 0); QCOMPARE(page.historyMonthPresentation(), frame);
    action(w, "classic-my-torrents").trigger(); QCOMPARE(w.filter().text(), QStringLiteral("retained month filter")); QCOMPARE(w.incompleteTable().columnWidth(1), 247); QVERIFY(p.intact());
  }
  void realF5AndReopenReadFreshBytesButRetainOnlyScalarChoices() {
    Profile p; ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w); QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), reads(&b, &app::SessionBridge::historyMonthReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000); auto& first = select(w); QTRY_VERIFY_WITH_TIMEOUT(first.historyMonthPresentation() && !b.busy(), 30000);
    child<QComboBox>(first, "saved-history-month-traffic").setCurrentIndex(2); const auto original = first.historyMonthPresentation(); p.put(datePath(Leap), QByteArray::fromStdString(history(Leap, 5)));
    action(w, "classic-refresh").trigger(); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 2 && !b.busy(), 30000); QCOMPARE((*first.historyMonthPresentation()->snapshot->days[27].snapshot->totals)[3], std::int64_t{55}); QCOMPARE((*original->snapshot->days[27].snapshot->totals)[3], std::int64_t{44});
    QPointer old(&first); action(w, "classic-close-tab").trigger(); QVERIFY(!w.statisticsPage()); p.put(datePath(Leap), QByteArray::fromStdString(history(Leap, 7)));
    auto& next = open(w); QCOMPARE(next.selectedHistoryMonthDate(), End); QCOMPARE(child<QComboBox>(next, "saved-history-month-traffic").currentIndex(), 2); QVERIFY(!next.historyMonthPresentation());
    QTRY_VERIFY_WITH_TIMEOUT(next.historyMonthPresentation() && !b.busy(), 30000); QTRY_VERIFY(old.isNull()); QCOMPARE(reads.count(), 3); QCOMPARE((*next.historyMonthPresentation()->snapshot->days[27].snapshot->totals)[3], std::int64_t{77}); QVERIFY(next.historyMonthPresentation() != original); QVERIFY(p.intact());
  }
  void everyDailyFailureProducesOneFailedMonthWithoutLibraryRecovery_data() { QTest::addColumn<int>("kind"); QTest::newRow("malformed-middle") << 0; QTest::newRow("unsafe-last-directory") << 1; }
  void everyDailyFailureProducesOneFailedMonthWithoutLibraryRecovery() {
    QFETCH(int, kind); Profile p; if (kind == 0) { p.put(datePath({2024, 2, 16}), QByteArrayLiteral("not a history record\n")); }
    else { const auto path = p.path + datePath(End); QVERIFY(QFile::remove(path)); p.files.erase(path); QVERIFY(QDir().mkpath(path)); }
    ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w); QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), reads(&b, &app::SessionBridge::historyMonthReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000); auto& page = select(w); QTRY_VERIFY_WITH_TIMEOUT(page.historyMonthPresentation() && !b.busy(), 30000);
    QCOMPARE(reads.count(), 1); QCOMPARE(page.historyMonthPresentation()->state, ui::HistoryMonthReadState::Failed); QVERIFY(!page.historyMonthPresentation()->snapshot); QCOMPARE(snapshots.count(), 1); QVERIFY(p.intact());
  }
  void queuedCancellationPublishesNoMonthAndDoesNotRefreshLibrary() {
    Profile p; ui::LibraryWindow w; app::SessionBridge b; wire(w, b); show(w); QSignalSpy columns(&b, &app::SessionBridge::columnLayoutsReady), reads(&b, &app::SessionBridge::historyMonthReady), snapshots(&b, &app::SessionBridge::snapshotReady);
    QVERIFY(b.open(p.path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !b.busy(), 30000); open(w); QTRY_VERIFY_WITH_TIMEOUT(!b.busy(), 30000);
    connect(&w, &ui::LibraryWindow::historyMonthRequested, &w, [&](quint64, quint64, core::HistoryDate) { b.cancel(); }); auto& page = select(w);
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy() && child<QLabel>(page, "saved-history-month-status").text().contains(QStringLiteral("did not complete")), 30000); QCOMPARE(reads.count(), 0); QCOMPARE(snapshots.count(), 1); QVERIFY(p.intact());
  }
  void threeKindsCoalesceLatestAndTotalsKeepTheirPriorityWithoutWrongKindSettlement() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); QSignalSpy totals(&w, &ui::LibraryWindow::statisticsRequested), months(&w, &ui::LibraryWindow::historyMonthRequested), weeks(&w, &ui::LibraryWindow::historyWeekRequested), days(&w, &ui::LibraryWindow::historyRequested);
    connect(&w, &ui::LibraryWindow::statisticsRequested, &w, [&](quint64, quint64) { w.setBusy(true); });
    connect(&w, &ui::LibraryWindow::historyMonthRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    connect(&w, &ui::LibraryWindow::historyWeekRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    connect(&w, &ui::LibraryWindow::historyRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); });
    open(w); QTRY_COMPARE(totals.count(), 1); select(w, 0); select(w, 1); auto& page = select(w, 2); QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QCOMPARE(months.count(), 0);
    w.setStatistics(saved(73, totals.at(0).at(1).toULongLong())); w.setBusy(false); QTRY_COMPARE(months.count(), 1); const auto request = months.at(0).at(1).toULongLong();
    w.setHistory(dayPacket(73, request)); w.setHistoryWeek(weekPacket(73, request)); w.setHistoryMonth(monthPacket(72, request)); w.setHistoryMonth(monthPacket(73, request, {2024, 3, 3})); QVERIFY(!page.historyMonthPresentation());
    child<QPushButton>(page, "statistics-refresh").click(); select(w, 0, Leap); w.setHistoryMonth(monthPacket(73, request)); w.setBusy(false); QTRY_COMPARE(totals.count(), 2); QCOMPARE(days.count(), 0);
    w.setStatistics(saved(73, totals.at(1).at(1).toULongLong())); w.setBusy(false); QTRY_COMPARE(days.count(), 1); QCOMPARE(months.count(), 1); QCOMPARE(weeks.count(), 0);
    const auto dayRequest = days.at(0).at(1).toULongLong(); w.setHistoryMonth(monthPacket(73, dayRequest, Leap)); QVERIFY(!page.historyPresentation()); w.setHistory(dayPacket(73, dayRequest, Leap)); w.setBusy(false); QVERIFY(page.historyPresentation());
  }
  void closingBusyMonthDoesNotCancelAndOldPageCannotPopulateItsReplacement() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); totalsOwner(w); QSignalSpy reads(&w, &ui::LibraryWindow::historyMonthRequested), cancels(&w, &ui::LibraryWindow::cancelRequested);
    connect(&w, &ui::LibraryWindow::historyMonthRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); }); auto& first = select(w); QTRY_COMPARE(reads.count(), 1); const auto oldRequest = reads.at(0).at(1).toULongLong(); QPointer old(&first);
    action(w, "classic-close-tab").trigger(); QVERIFY(!w.statisticsPage()); auto& next = open(w); QCOMPARE(next.selectedHistoryMonthDate(), End); QVERIFY(!next.historyMonthPresentation());
    w.setHistoryMonth(monthPacket(73, oldRequest)); w.setBusy(false); QTRY_COMPARE(reads.count(), 2); QTRY_VERIFY(old.isNull()); QVERIFY(!next.historyMonthPresentation()); QCOMPARE(cancels.count(), 0);
    const auto request = reads.at(1).at(1).toULongLong(); QVERIFY(request > oldRequest); w.setHistoryMonth(monthPacket(73, oldRequest)); QVERIFY(!next.historyMonthPresentation()); w.setHistoryMonth(monthPacket(73, request)); w.setBusy(false); QVERIFY(next.historyMonthPresentation());
  }
  void newSessionRejectsOldMonthWhileVisibleChoicesRemainAndClosedMemoryResets() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); totalsOwner(w); QSignalSpy reads(&w, &ui::LibraryWindow::historyMonthRequested);
    connect(&w, &ui::LibraryWindow::historyMonthRequested, &w, [&](quint64, quint64, core::HistoryDate) { w.setBusy(true); }); auto& page = select(w); QTRY_COMPARE(reads.count(), 1); const auto oldRequest = reads.at(0).at(1).toULongLong(); child<QComboBox>(page, "saved-history-month-traffic").setCurrentIndex(2);
    w.beginColumnLayoutSession(74); QCOMPARE(page.selectedHistoryMonthDate(), End); QCOMPARE(child<QComboBox>(page, "saved-history-month-traffic").currentIndex(), 2); QVERIFY(!page.historyMonthPresentation());
    w.setHistoryMonth(monthPacket(73, oldRequest)); w.setBusy(false); QTRY_COMPARE(reads.count(), 2); QCOMPARE(reads.at(1).at(0).toULongLong(), quint64{74});
    w.setHistoryMonth(monthPacket(74, reads.at(1).at(1).toULongLong())); w.setBusy(false); QVERIFY(page.historyMonthPresentation()); action(w, "classic-close-tab").trigger();
    w.beginColumnLayoutSession(0); w.beginColumnLayoutSession(73); w.beginColumnLayoutSession(74); auto& same = open(w); QCOMPARE(same.selectedHistoryMonthDate(), End); QCOMPARE(child<QComboBox>(same, "saved-history-month-traffic").currentIndex(), 2);
    action(w, "classic-close-tab").trigger(); w.beginColumnLayoutSession(75); auto& fresh = open(w); QVERIFY(fresh.navigation()); QCOMPARE(fresh.navigation()->section, 0); QCOMPARE(fresh.navigation()->history, 0); QCOMPARE(fresh.navigation()->monthTraffic, ui::SavedHistoryTraffic::Payload);
  }
  void rejectedMonthDispatchSettlesFailureAndAllowsAnExplicitFreshRetry() {
    ui::LibraryWindow w; show(w); QVERIFY(bind(w)); totalsOwner(w); QSignalSpy reads(&w, &ui::LibraryWindow::historyMonthRequested); auto& page = select(w);
    QTRY_VERIFY(reads.count() == 1 && page.historyMonthPresentation()); QCOMPARE(page.historyMonthPresentation()->state, ui::HistoryMonthReadState::Failed);
    child<QPushButton>(page, "saved-history-month-refresh").click(); QTRY_COMPARE(reads.count(), 2); QVERIFY(page.historyMonthPresentation()); QCOMPARE(page.historyMonthPresentation()->state, ui::HistoryMonthReadState::Failed);
  }
  void directDeletionDuringMonthDispatchCannotResumeAnOlderOptionalCallback() {
    auto owner = std::make_unique<ui::LibraryWindow>(); show(*owner); QVERIFY(bind(*owner)); totalsOwner(*owner); QPointer guard(owner.get());
    connect(owner.get(), &ui::LibraryWindow::historyMonthRequested, owner.get(), [&](quint64, quint64, core::HistoryDate) { owner.reset(); }); select(*owner); QTRY_VERIFY(guard.isNull());
  }
};
QTEST_MAIN(HistoryMonthIntegrationTests)
#include "history_month_integration_tests.moc"
