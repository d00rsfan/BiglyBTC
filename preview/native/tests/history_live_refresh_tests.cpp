#include "library_window.hpp"
#include "statistics_page.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>

#include <optional>
#include <stdexcept>
#include <vector>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& owner, const char* id) {
  auto* result = owner.findChild<T*>(QString::fromLatin1(id));
  if (!result) { throw std::runtime_error("Missing live-history fixture control"); }
  return *result;
}
struct Read {
  int kind;
  quint64 session, request;
  core::HistoryDate date{};
  core::HistoryRangeRequest query{};
};
// Synthetic optional-reader owner only. All disk-success and disk-error claims
// are tested by the real writer/owned-engine suites, not these GUI packets.
struct Owner {
  QPointer<ui::LibraryWindow> window;
  QObject context;
  std::vector<Read> reads;
  std::optional<Read> active;
  bool hold = false;
  explicit Owner(ui::LibraryWindow& value) : window(&value) {
    QObject::connect(&value, &ui::LibraryWindow::statisticsRequested, &context,
        [this](quint64 session, quint64 request) {
      if (!window) { return; }
      window->setBusy(true);
      QTimer::singleShot(0, &context, [this, session, request] {
        if (!window) { return; }
        auto packet = std::make_shared<ui::StatisticsPacket>();
        packet->session = session; packet->request = request; packet->state = ui::StatisticsReadState::Missing;
        window->setStatistics(packet);
        if (window) { window->setBusy(false); }
      });
    });
    QObject::connect(&value, &ui::LibraryWindow::historyRequested, &context,
        [this](quint64 s, quint64 r, core::HistoryDate d) { accept({0,s,r,d,{}}); });
    QObject::connect(&value, &ui::LibraryWindow::historyWeekRequested, &context,
        [this](quint64 s, quint64 r, core::HistoryDate d) { accept({1,s,r,d,{}}); });
    QObject::connect(&value, &ui::LibraryWindow::historyMonthRequested, &context,
        [this](quint64 s, quint64 r, core::HistoryDate d) { accept({2,s,r,d,{}}); });
    QObject::connect(&value, &ui::LibraryWindow::historyRangeRequested, &context,
        [this](quint64 s, quint64 r, core::HistoryRangeRequest q) { accept({3,s,r,{},q}); });
  }
  void accept(Read read) {
    if (!window) { return; }
    reads.push_back(read); active = read; window->setBusy(true);
    if (!hold) { QTimer::singleShot(0, &context, [this] { finish(); }); }
  }
  void finish(bool packet = true) {
    if (!window || !active) { return; }
    const auto read = *active; active.reset();
    if (packet) {
      if (read.kind == 0) {
        auto result = std::make_shared<ui::HistoryPacket>();
        result->session = read.session; result->request = read.request; result->date = read.date;
        result->state = ui::HistoryReadState::Missing; window->setHistory(result);
      } else if (read.kind == 1) {
        auto result = std::make_shared<ui::HistoryWeekPacket>();
        result->session = read.session; result->request = read.request; result->endingDate = read.date;
        result->failure = QStringLiteral("Synthetic optional read failure"); window->setHistoryWeek(result);
      } else if (read.kind == 2) {
        auto result = std::make_shared<ui::HistoryMonthPacket>();
        result->session = read.session; result->request = read.request; result->endingDate = read.date;
        result->failure = QStringLiteral("Synthetic optional read failure"); window->setHistoryMonth(result);
      } else {
        auto result = std::make_shared<ui::HistoryRangePacket>();
        result->session = read.session; result->request = read.request; result->query = read.query;
        result->failure = QStringLiteral("Synthetic optional read failure"); window->setHistoryRange(result);
      }
    }
    if (window) { window->setBusy(false); }
  }
};
void show(ui::LibraryWindow& window) {
  window.resize(1280, 1000); window.show(); window.activateWindow(); QCoreApplication::processEvents();
}
ui::StatisticsPage& select(ui::LibraryWindow& window, int kind) {
  child<QAction>(window, "classic-statistics").trigger();
  auto* page = window.statisticsPage();
  if (!page) { throw std::runtime_error("Statistics page did not open"); }
  page->tabs().setCurrentIndex(1);
  child<QTabWidget>(*page, "statistics-transfer-tabs").setCurrentIndex(1);
  child<QTabWidget>(*page, "statistics-history-tabs").setCurrentIndex(kind);
  return *page;
}
bool completed(const ui::StatisticsPage& page, int kind) {
  return kind == 0 ? bool(page.historyPresentation()) : kind == 1 ? bool(page.historyWeekPresentation())
      : kind == 2 ? bool(page.historyMonthPresentation()) : bool(page.historyRangePresentation());
}
void turn() { QCoreApplication::processEvents(); QCoreApplication::processEvents(); }
} // namespace

class HistoryLiveRefreshTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void revisionsAreSessionBoundLatestOnlyAndDoNotPollFailedReads() {
    ui::LibraryWindow window; Owner owner(window); show(window); window.beginColumnLayoutSession(7);
    auto& page = select(window, 1);
    QTRY_VERIFY(completed(page, 1) && !owner.active);
    const auto before = owner.reads.size();
    window.setHistoryRevision(0, 1); window.setHistoryRevision(8, 1); window.setHistoryRevision(7, 0);
    turn(); QCOMPARE(owner.reads.size(), before);
    window.setHistoryRevision(7, 1); window.setHistoryRevision(7, 3); window.setHistoryRevision(7, 2);
    QTRY_VERIFY(owner.reads.size() == before + 1 && !owner.active);
    QCOMPARE(owner.reads.back().kind, 1);
    const auto request = owner.reads.back().request;
    QVERIFY(page.historyWeekPresentation()->failure.contains(QStringLiteral("Synthetic")));
    window.setHistoryRevision(7, 3); window.setHistoryRevision(7, 1); turn();
    QCOMPARE(owner.reads.size(), before + 1); QCOMPARE(page.historyWeekPresentation()->request, request);
    window.beginColumnLayoutSession(8);
    QTRY_VERIFY(!owner.active && completed(page, 1) && owner.reads.back().session == 8);
    const auto changedProfile = owner.reads.size();
    window.setHistoryRevision(7, 99); turn(); QCOMPARE(owner.reads.size(), changedProfile);
    window.setHistoryRevision(8, 1);
    QTRY_VERIFY(owner.reads.size() == changedProfile + 1 && !owner.active);
  }
  void hiddenModesDeferAndPreserveDateTrafficAndHourlyChoices() {
    ui::LibraryWindow window; Owner owner(window); show(window); window.beginColumnLayoutSession(7);
    auto& page = select(window, 0); QTRY_VERIFY(completed(page, 0) && !owner.active);
    child<QDateEdit>(page, "saved-history-date").setDate(QDate(2024, 2, 29));
    QTRY_VERIFY(!owner.active && owner.reads.back().date == (core::HistoryDate{2024,2,29}));
    page.tabs().setCurrentIndex(0); const auto before = owner.reads.size();
    window.setHistoryRevision(7, 1); window.setHistoryRevision(7, 2); turn();
    QCOMPARE(owner.reads.size(), before);
    select(window, 0); QTRY_VERIFY(owner.reads.size() == before + 1 && !owner.active);
    QCOMPARE(owner.reads.back().date, (core::HistoryDate{2024,2,29}));
    select(window, 1); QTRY_VERIFY(completed(page, 1) && !owner.active);
    child<QComboBox>(page, "saved-history-week-resolution").setCurrentIndex(1);
    child<QComboBox>(page, "saved-history-week-traffic").setCurrentIndex(2); turn();
    const auto choices = page.navigation(); QVERIFY(choices);
    const auto weekCount = owner.reads.size(); window.setHistoryRevision(7, 3);
    QTRY_VERIFY(owner.reads.size() == weekCount + 1 && !owner.active);
    QVERIFY(page.navigation() == choices);
    select(window, 0); QTRY_VERIFY(owner.reads.size() == weekCount + 2 && !owner.active);
    QCOMPARE(owner.reads.back().kind, 0); QCOMPARE(owner.reads.back().date, (core::HistoryDate{2024,2,29}));
  }
  void busyAndCancelledReadsReleaseExactlyOneLatestRevision() {
    ui::LibraryWindow window; Owner owner(window); show(window); window.beginColumnLayoutSession(7);
    auto& page = select(window, 0); QTRY_VERIFY(completed(page, 0) && !owner.active);
    owner.hold = true; const auto before = owner.reads.size(); window.setHistoryRevision(7, 1);
    QTRY_VERIFY(owner.active && owner.reads.size() == before + 1);
    const auto old = owner.active->request;
    window.setHistoryRevision(7, 2); window.setHistoryRevision(7, 5); window.setHistoryRevision(7, 4); turn();
    QCOMPARE(owner.reads.size(), before + 1);
    owner.hold = false; owner.finish(false); // Exact active request gets no packet, as on cancellation.
    QTRY_VERIFY(owner.reads.size() == before + 2 && !owner.active);
    QVERIFY(owner.reads.back().request > old); QCOMPARE(page.historyPresentation()->request, owner.reads.back().request);
    turn(); QCOMPARE(owner.reads.size(), before + 2);
  }
  void minimizedViewsAndRollingControlsKeepOnlyCurrentQuery() {
    ui::LibraryWindow window; Owner owner(window); show(window); window.beginColumnLayoutSession(7);
    auto& page = select(window, 3); QTRY_VERIFY(completed(page, 3) && !owner.active);
    child<QComboBox>(page, "saved-history-range-period").setCurrentIndex(static_cast<int>(core::HistoryPeriod::Day));
    child<QPushButton>(page, "saved-history-range-previous").click();
    child<QComboBox>(page, "saved-history-range-traffic").setCurrentIndex(2);
    QTRY_VERIFY(!owner.active && owner.reads.back().query.period == core::HistoryPeriod::Day
        && owner.reads.back().query.offset == -1);
    const auto choices = page.navigation(); const auto before = owner.reads.size();
    window.showMinimized(); turn(); window.setHistoryRevision(7, 1); window.setHistoryRevision(7, 2); turn();
    QCOMPARE(owner.reads.size(), before);
    const auto started = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    window.showNormal(); window.activateWindow();
    QTRY_VERIFY(owner.reads.size() == before + 1 && !owner.active);
    QCOMPARE(owner.reads.back().kind, 3); QCOMPARE(owner.reads.back().query.offset, std::int64_t{-1});
    QVERIFY(owner.reads.back().query.nowMillis >= started);
    QVERIFY(owner.reads.back().query.nowMillis <= QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    QVERIFY(page.navigation() == choices);
  }
  void closedTabRetainsRevisionButClosedOwnerDoesNotReread() {
    ui::LibraryWindow window; Owner owner(window); show(window); window.beginColumnLayoutSession(7);
    auto& page = select(window, 2); QTRY_VERIFY(completed(page, 2) && !owner.active);
    const auto date = page.selectedHistoryMonthDate();
    const auto before = owner.reads.size(); child<QAction>(window, "classic-close-tab").trigger();
    QVERIFY(!window.statisticsPage()); window.setHistoryRevision(7, 1); turn(); QCOMPARE(owner.reads.size(), before);
    auto& fresh = select(window, 2); QTRY_VERIFY(completed(fresh, 2) && !owner.active);
    QCOMPARE(fresh.selectedHistoryMonthDate(), date);
    auto closed = std::make_shared<ui::NativeSessionPacket>(); closed->session = 7;
    closed->applicationRun = 1; closed->epoch = 1; closed->sequence = 1;
    closed->state = ui::NativeSessionState::Closed; window.setNativeSession(closed);
    const auto closedCount = owner.reads.size(); window.setHistoryRevision(7, 2); turn();
    QCOMPARE(owner.reads.size(), closedCount);
  }
  void directRequestReceiverCanDeleteWindowWithoutResumingRefresh() {
    QPointer<ui::LibraryWindow> window = new ui::LibraryWindow;
    Owner owner(*window); show(*window); window->beginColumnLayoutSession(7);
    auto& page = select(*window, 0); QTRY_VERIFY(completed(page, 0) && !owner.active);
    QObject::connect(window, &ui::LibraryWindow::historyRequested, &owner.context,
        [&window](quint64, quint64, core::HistoryDate) { delete window.data(); });
    window->setHistoryRevision(7, 1); QTRY_VERIFY(!window); turn();
  }
};

QTEST_MAIN(HistoryLiveRefreshTests)
#include "history_live_refresh_tests.moc"
