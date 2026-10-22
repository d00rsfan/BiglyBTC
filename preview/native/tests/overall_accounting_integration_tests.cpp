#include "session_bridge.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "overall_presentation.hpp"
#include "effective_rate_limit_text.hpp"
#include "runtime_metrics_view.hpp"
#include "owned_engine_fixture.hpp"
#include "accounting_handoff_capture.hpp"
#include "persistent_session.hpp"
#include "../adapters/history_view.hpp"
#include "biglybtc/saved_history_range.hpp"
#include "biglybtc/saved_statistics.hpp"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDateEdit>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>

#include <array>
#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
using Field = core::SavedStatistic;
template<class T> T& child(QObject& owner, const char* id) {
  auto* result = owner.findChild<T*>(QString::fromLatin1(id));
  if (!result) { throw std::runtime_error("Missing actual accounting integration widget"); }
  return *result;
}
void wire(ui::LibraryWindow& window, app::SessionBridge& bridge) {
  QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &window, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&bridge, &app::SessionBridge::columnLayoutsReady, &window, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&bridge, &app::SessionBridge::statisticsReady, &window, &ui::LibraryWindow::setStatistics);
  QObject::connect(&bridge, &app::SessionBridge::historyChanged, &window, &ui::LibraryWindow::setHistoryRevision);
  QObject::connect(&bridge, &app::SessionBridge::historyReady, &window, &ui::LibraryWindow::setHistory);
  QObject::connect(&bridge, &app::SessionBridge::historyWeekReady, &window, &ui::LibraryWindow::setHistoryWeek);
  QObject::connect(&bridge, &app::SessionBridge::nativeSessionReady, &window, &ui::LibraryWindow::setNativeSession);
  QObject::connect(&bridge, &app::SessionBridge::overallReady, &window, &ui::LibraryWindow::setOverall);
  QObject::connect(&bridge, &app::SessionBridge::runtimeMetricsReady, &window, &ui::LibraryWindow::setRuntimeMetrics);
  QObject::connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
  QObject::connect(&window, &ui::LibraryWindow::columnLayoutsRequested, &bridge, &app::SessionBridge::columnLayouts);
  QObject::connect(&window, &ui::LibraryWindow::statisticsRequested, &bridge, &app::SessionBridge::statistics);
  QObject::connect(&window, &ui::LibraryWindow::historyRequested, &bridge, &app::SessionBridge::history);
  QObject::connect(&window, &ui::LibraryWindow::historyWeekRequested, &bridge, &app::SessionBridge::historyWeek);
  QObject::connect(&window, &ui::LibraryWindow::markRequested, &bridge, &app::SessionBridge::markStatistics);
}
ui::StatisticsPage& openStatistics(ui::LibraryWindow& window) {
  child<QAction>(window, "classic-statistics").trigger();
  if (!window.statisticsPage()) { throw std::runtime_error("Statistics page did not open"); }
  auto& page = *window.statisticsPage();
  page.tabs().setCurrentIndex(1);
  child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(0);
  return page;
}
ui::OverallPresentation latest(const QSignalSpy& spy) {
  return spy.empty() ? ui::OverallPresentation{} : qvariant_cast<ui::OverallPresentation>(spy.last().at(0));
}
ui::RuntimeMetricsPresentation runtimeAt(const QSignalSpy& spy) {
  return spy.empty() ? ui::RuntimeMetricsPresentation{} : qvariant_cast<ui::RuntimeMetricsPresentation>(spy.last().at(0));
}
bool runtimeMatchesPeer(const ui::RuntimeMetricsPresentation& packet, const ui::NativeSessionPresentation& peer) {
  if (!packet || !packet->snapshot || !peer || !peer->sample) { return false; }
  const auto value = [&](std::string_view name) -> std::optional<std::int64_t> {
    for (const auto& metric : packet->snapshot->metrics) {
      if (metric.name == name && metric.type == core::RuntimeMetricType::Counter) { return metric.value; }
    }
    return {};
  };
  const auto& counters = peer->sample->counters;
  return value("net.recv_bytes") == counters.received && value("net.sent_bytes") == counters.sent
      && value("net.recv_payload_bytes") == counters.payloadReceived && value("net.sent_payload_bytes") == counters.payloadSent;
}
std::int64_t field(const core::OverallValues& values, Field key) { return values[static_cast<std::size_t>(key)]; }
core::SavedStatistics saved(const QByteArray& bytes) {
  auto result = core::readSavedStatistics(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  if (!result) { throw std::runtime_error("Actual saved accounting file did not parse"); }
  return std::move(*result);
}
core::OverallValues values(const QByteArray& bytes) {
  auto snapshot = saved(bytes); core::OverallValues result{};
  if (!snapshot.hasAll) { throw std::runtime_error("Saved accounting all dictionary missing"); }
  for (std::size_t i = 0; i < result.size(); ++i) {
    if (!snapshot.values[i].value || snapshot.values[i].invalidType) { throw std::runtime_error("Saved accounting field lost"); }
    result[i] = *snapshot.values[i].value;
  }
  return result;
}
std::string encodedNode(const core::Document& document, const core::Node* node) {
  if (!node || node->end < node->begin || node->end > document.originalBytes().size()) {
    throw std::runtime_error("Missing opaque accounting subtree");
  }
  return document.originalBytes().substr(node->begin, node->end - node->begin);
}
bool opaquePreserved(const QByteArray& before, const QByteArray& after) {
  const auto a = saved(before), b = saved(after);
  const auto* aa = a.source->find("all"); const auto* ba = b.source->find("all");
  return aa && ba && encodedNode(*a.source, a.source->find("opaque.root")) == encodedNode(*b.source, b.source->find("opaque.root"))
      && encodedNode(*a.source, aa->find("opaque.all")) == encodedNode(*b.source, ba->find("opaque.all"));
}
constexpr std::int64_t InitialDown = 5000, InitialUp = 2000;
constexpr core::HistoryCounters InitialParts{11, 13, 17, 19, 29, 23};
QByteArray initialDocument() {
  libtorrent::entry root(libtorrent::entry::dictionary_type{});
  auto& all = root["all"];
  all["downloaded"] = InitialDown; all["uploaded"] = InitialUp; all["uptime"] = 40;
  all["p_uploaded"] = InitialParts[0]; all["d_uploaded"] = InitialParts[1];
  all["p_downloaded"] = InitialParts[2]; all["d_downloaded"] = InitialParts[3];
  all["dht_up"] = InitialParts[4]; all["dht_down"] = InitialParts[5];
  all["opaque.all"] = std::string("\0\xffuntouched", 11);
  root["opaque.root"] = libtorrent::entry::list_type{libtorrent::entry(std::string("\xff\0raw", 5)), libtorrent::entry(-71)};
  return QByteArray::fromStdString(test::encoded(root));
}
bool payloadIntact(const test::OwnedEngineFixture& fixture) {
  for (const auto& file : fixture.files) {
    const auto name = fixture.payload + '/' + file.relative; struct stat current{};
    if (::stat(name.toStdString().c_str(), &current) || current.st_dev != file.before.st_dev
        || current.st_ino != file.before.st_ino || test::read(name) != file.expected) { return false; }
  }
  return true;
}
core::HistoryDate date(QDate value) {
  return {value.year(), static_cast<unsigned>(value.month()), static_cast<unsigned>(value.day())};
}
QString dayFile(const QString& profile, QDate value) {
  return profile + QStringLiteral("/stats/") + value.toString(QStringLiteral("yyyy/MM/dd")) + QStringLiteral(".dat");
}
} // namespace

// Actual source-bound owner + held generated profile + real worker/GUI signals.
// No synthetic owner packets, dependency fault API, timers, sleeps or new Java
// equality. Numeric history records are compared to actual absolute peer facts;
// reconstructed record minutes are deliberately not equated with wall arrival.
class OverallAccountingIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    auto owner = engine::PersistentSession::create({1, 2, 3});
    QVERIFY2(owner.has_value(), "Explicit source-bound accounting cohort is required; no skip");
    const auto closed = (*owner)->close(std::chrono::steady_clock::now() + std::chrono::seconds{5});
    QVERIFY(closed.sessionJoined);
  }
  void actualPeerTotalsMarksHistoryAndJoinedReopen_data() {
    QTest::addColumn<bool>("multifile");
    QTest::newRow("single") << false; QTest::newRow("multifile") << true;
  }
  void actualPeerTotalsMarksHistoryAndJoinedReopen() {
    QFETCH(bool, multifile); test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    QVERIFY(fixture.intact(false));
    const QByteArray original = initialDocument();
    const auto statisticsPath = fixture.profile + QStringLiteral("/BiglyBT.statistics");
    test::write(statisticsPath, original);
    const auto initial = saved(original);
    QVERIFY(!initial.get(Field::MarkTime).value); QVERIFY(!initial.get(Field::MarkDownloaded).value);
    const auto stateBefore = test::read(fixture.stateFile);
    const auto inventoryBefore = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.resize(1280, 1000); window.show();
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), packets(&bridge, &app::SessionBridge::overallReady);
    QSignalSpy runtime(&bridge, &app::SessionBridge::runtimeMetricsReady);
    QSignalSpy marks(&window, &ui::LibraryWindow::markRequested), closed(&bridge, &app::SessionBridge::sessionClosed);
    QSignalSpy historyChanges(&bridge, &app::SessionBridge::historyChanged);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 30000);
    auto& page = openStatistics(window);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && page.readState().has_value(), 30000);
    QTRY_VERIFY_WITH_TIMEOUT(latest(packets) && latest(packets)->available && latest(packets)->writable
        && window.nativeSessionSnapshot() && window.nativeSessionSnapshot()->sample
        && !historyChanges.empty(), 10000);
    const auto startupOwner = window.nativeSessionSnapshot();
    QCOMPARE(startupOwner->sample->counters, core::EngineCounters{});
    QCOMPARE(latest(packets)->values[static_cast<std::size_t>(Field::Downloaded)], InitialDown);
    QCOMPARE(latest(packets)->values[static_cast<std::size_t>(Field::Uploaded)], InitialUp);
    QVERIFY(child<QPushButton>(page, "statistics-set-mark").isEnabled());
    QVERIFY(historyChanges.last().at(1).toULongLong() > 0);
    const auto beforeTransferDate = QDateTime::currentDateTimeUtc().date();
    QSignalSpy historyReads(&window, &ui::LibraryWindow::historyWeekRequested);
    const auto readsBeforeStartupSelection = historyReads.count();
    child<QDateEdit>(page, "saved-history-week-date").setDate(beforeTransferDate.addDays(1));
    child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(1);
    child<QTabWidget>(page, "statistics-history-tabs").setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && page.historyWeekPresentation()
        && page.historyWeekPresentation()->snapshot, 10000);
    QVERIFY(std::any_of(page.historyWeekPresentation()->snapshot->days.begin(),
        page.historyWeekPresentation()->snapshot->days.end(), [](const auto& day) { return day.snapshot.has_value(); }));
    // Startup wrote a real Start header from actual idle counters. A header
    // alone has unknown totals; any completed idle minute has measured zeros.
    for (const auto& day : page.historyWeekPresentation()->snapshot->days) {
      if (day.snapshot && day.snapshot->totals) { QCOMPARE(*day.snapshot->totals, core::HistoryCounters{}); }
    }
    const auto target = window.model().targetAt(0); QVERIFY(target);
    QVERIFY(bridge.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(packets) && latest(packets)->available
        && window.nativeSessionSnapshot() && window.nativeSessionSnapshot()->sample
        && window.nativeSessionSnapshot()->sample->counters.payloadReceived > 0, 30000);
    // Actual bound owner's one get_settings snapshot, delivered by the real
    // Bridge. Isolated defaults are session-unlimited, not unknown or inferred
    // from torrent policy; the connection/choker/proxy facts must also survive.
    const auto actualOwnerSettings = window.nativeSessionSnapshot()->rateLimits;
    QVERIFY(core::validSessionRateLimits(actualOwnerSettings));
    QVERIFY(actualOwnerSettings.download.state == core::RateLimitState::Unlimited);
    QVERIFY(actualOwnerSettings.upload.state == core::RateLimitState::Unlimited);
    QCOMPARE(actualOwnerSettings.download.bytesPerSecond, std::int64_t{0});
    QCOMPARE(actualOwnerSettings.upload.bytesPerSecond, std::int64_t{0});
    QVERIFY(actualOwnerSettings.connections.connectionLimit.has_value());
    QVERIFY(*actualOwnerSettings.connections.connectionLimit > 0);
    QVERIFY(actualOwnerSettings.connections.uploadSlots.state != core::UploadSlotState::Unavailable);
    QVERIFY(actualOwnerSettings.connections.proxy == core::SessionProxyMode::Disabled);
    QCOMPARE(window.nativeSessionSnapshot()->epoch, startupOwner->epoch);
    QCOMPARE(window.nativeSessionSnapshot()->applicationRun, startupOwner->applicationRun);
    // Start is now an application-startup event while My Torrents is open.
    // Selecting Week reads that actual saved file; transfer does not recreate
    // the owner/baseline. Dedicated invalidation tests retain live-refresh proof.
    QTRY_VERIFY_WITH_TIMEOUT(!historyChanges.empty() && historyReads.count() > readsBeforeStartupSelection
        && !bridge.busy() && page.historyWeekPresentation() && page.historyWeekPresentation()->snapshot
        && std::any_of(page.historyWeekPresentation()->snapshot->days.begin(),
            page.historyWeekPresentation()->snapshot->days.end(), [](const auto& day) { return day.snapshot.has_value(); }), 10000);
    QCOMPARE(child<QDateEdit>(page, "saved-history-week-date").date(), beforeTransferDate.addDays(1));
    QVERIFY(payloadIntact(fixture));
    // Compare actual independent response tuples once the isolated owner is
    // idle; do not equate two publication sequences or elapsed clocks.
    QTRY_VERIFY_WITH_TIMEOUT(runtimeMatchesPeer(runtimeAt(runtime), window.nativeSessionSnapshot()), 10000);
    page.tabs().setCurrentIndex(4);
    auto& metricsView = child<ui::RuntimeMetricsView>(page, "statistics-all-metrics");
    auto& metricsTable = child<QTableView>(metricsView, "runtime-metrics-table");
    QTRY_VERIFY_WITH_TIMEOUT(metricsView.presentation() && metricsView.presentation()->snapshot
        && metricsTable.model()->rowCount() > 0, 5000);
    const auto displayed = metricsView.presentation();
    QVERIFY(displayed == runtimeAt(runtime)); QVERIFY(displayed->snapshot); QVERIFY(!displayed->snapshot->metrics.empty());
    QCOMPARE(metricsTable.model()->rowCount(), static_cast<int>(displayed->snapshot->metrics.size()));
    QCOMPARE(metricsTable.model()->columnCount(), 3);
    for (int row = 0; row < metricsTable.model()->rowCount(); ++row) {
      const auto nameIndex = metricsTable.model()->index(row, 0);
      const auto rawName = nameIndex.data(Qt::UserRole + 1).toByteArray();
      const std::string_view name(rawName.constData(), static_cast<std::size_t>(rawName.size()));
      const auto& actualMetrics = displayed->snapshot->metrics;
      const auto found = std::find_if(actualMetrics.begin(), actualMetrics.end(), [&](const auto& metric) { return metric.name == name; });
      QVERIFY(found != actualMetrics.end());
      const auto valueIndex = metricsTable.model()->index(row, 1);
      QCOMPARE(valueIndex.data(Qt::UserRole).metaType(), QMetaType::fromType<qlonglong>());
      QCOMPARE(valueIndex.data(Qt::UserRole).toLongLong(), static_cast<qlonglong>(found->value));
      QCOMPARE(valueIndex.data().toString(), QString::number(found->value));
      QCOMPARE(nameIndex.data(Qt::UserRole + 2).toInt(), static_cast<int>(found->group));
    }
    page.tabs().setCurrentIndex(1);
    child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(child<QPushButton>(page, "statistics-set-mark").isEnabled(), 5000);
    QCOMPARE(child<QLabel>(page, "native-session-download-limit").text(), QStringLiteral("Session download limit: Unlimited"));
    QCOMPARE(child<QLabel>(page, "native-session-upload-limit").text(), QStringLiteral("Session upload limit: Unlimited"));
    QCOMPARE(child<QLabel>(page, "native-session-connection-settings").text(),
        QStringLiteral("Session settings: %1").arg(ui::sessionConnectionSettingsText(actualOwnerSettings.connections)));
    QTest::mouseClick(&child<QPushButton>(page, "statistics-set-mark"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(marks.count() == 1 && !bridge.busy() && latest(packets)->saved
        && field(*latest(packets)->saved, Field::MarkTime) > 0, 10000);
    QCOMPARE(marks.first().at(1).toBool(), false);
    const auto markedBytes = test::read(statisticsPath); const auto marked = values(markedBytes);
    QVERIFY(opaquePreserved(original, markedBytes));
    QCOMPARE(field(marked, Field::MarkDownloaded), field(marked, Field::Downloaded));
    QCOMPARE(field(marked, Field::MarkUploaded), field(marked, Field::Uploaded));
    QCOMPARE(field(marked, Field::MarkUptime), field(marked, Field::Uptime));
    QVERIFY(field(marked, Field::DataDownloaded) - InitialParts[3] >= test::OwnedEngineFixture::PayloadBytes);
    QCOMPARE(field(marked, Field::Downloaded) - InitialDown,
        field(marked, Field::ProtocolDownloaded) - InitialParts[2] + field(marked, Field::DataDownloaded) - InitialParts[3]);
    QCOMPARE(field(marked, Field::Uploaded) - InitialUp,
        field(marked, Field::ProtocolUploaded) - InitialParts[0] + field(marked, Field::DataUploaded) - InitialParts[1]);
    QCOMPARE(field(marked, Field::DhtDownloaded), InitialParts[5]);
    QCOMPARE(field(marked, Field::DhtUploaded), InitialParts[4]); // Actual fixture disables DHT.
    QCOMPARE(test::read(statisticsPath + QStringLiteral(".bak")), original);
    QTRY_COMPARE(child<QLabel>(page, "overall-live-value-4").text(), QStringLiteral("0"));
    QTRY_VERIFY(child<QPushButton>(page, "statistics-clear-mark").isEnabled());
    QTest::mouseClick(&child<QPushButton>(page, "statistics-clear-mark"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(marks.count() == 2 && !bridge.busy() && latest(packets)->saved
        && field(*latest(packets)->saved, Field::MarkTime) == 0, 10000);
    QCOMPARE(marks.last().at(1).toBool(), true);
    const auto clearedBytes = test::read(statisticsPath); const auto cleared = values(clearedBytes);
    for (const auto key : {Field::MarkTime, Field::MarkDownloaded, Field::MarkUploaded, Field::MarkUptime}) {
      QCOMPARE(field(cleared, key), std::int64_t{0});
    }
    QVERIFY(field(cleared, Field::Downloaded) >= field(marked, Field::Downloaded));
    QVERIFY(opaquePreserved(original, clearedBytes));
    QCOMPARE(test::read(statisticsPath + QStringLiteral(".bak")), markedBytes);
    const auto historyRevisionBeforeClose = historyChanges.last().at(1).toULongLong();
    bool releasedAtAcknowledgement = false, endRevisionBeforeAcknowledgement = false;
    QObject::connect(&bridge, &app::SessionBridge::sessionClosed, &window, [&](quint64, bool joined, bool released) {
      auto lease = storage::ProfileLease::acquire(fixture.native());
      releasedAtAcknowledgement = joined && released && lease.has_value();
      endRevisionBeforeAcknowledgement = !historyChanges.empty()
          && historyChanges.last().at(1).toULongLong() > historyRevisionBeforeClose;
    });
    QVERIFY(bridge.closeSession());
    QTRY_VERIFY_WITH_TIMEOUT(closed.count() == 1 && releasedAtAcknowledgement && latest(packets)->closed, 15000);
    QVERIFY(endRevisionBeforeAcknowledgement);
    QVERIFY(latest(packets)->finalCounters); QVERIFY(latest(packets)->saved);
    QVERIFY(!latest(packets)->writable); QVERIFY(latest(packets)->failure.isEmpty());
    QVERIFY(latest(packets)->historyFailure.isEmpty());
    QVERIFY(!child<QPushButton>(page, "statistics-set-mark").isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(window.nativeSessionSnapshot() && window.nativeSessionSnapshot()->state == ui::NativeSessionState::Closed, 5000);
    const auto final = window.nativeSessionSnapshot(); QVERIFY(final->sample);
    const auto actual = final->sample->counters;
    const auto finalBytes = test::read(statisticsPath); const auto persisted = values(finalBytes);
    QCOMPARE(field(persisted, Field::Downloaded), InitialDown + actual.received);
    QCOMPARE(field(persisted, Field::Uploaded), InitialUp + actual.sent);
    QCOMPARE(field(persisted, Field::ProtocolDownloaded), InitialParts[2] + actual.received - actual.payloadReceived);
    QCOMPARE(field(persisted, Field::DataDownloaded), InitialParts[3] + actual.payloadReceived);
    QCOMPARE(field(persisted, Field::ProtocolUploaded), InitialParts[0] + actual.sent - actual.payloadSent);
    QCOMPARE(field(persisted, Field::DataUploaded), InitialParts[1] + actual.payloadSent);
    QCOMPARE(persisted, *latest(packets)->saved);
    QVERIFY(opaquePreserved(original, finalBytes));
    QVERIFY(!QFile::exists(statisticsPath + QStringLiteral(".saving")));
    const core::HistoryCounters peerFinal{actual.sent - actual.payloadSent, actual.payloadSent,
        actual.received - actual.payloadReceived, actual.payloadReceived, 0, 0};
    core::HistoryCounters recorded{}, firstAbsolute{}; bool firstHeader = true; std::size_t records = 0;
    const auto afterCloseDate = QDateTime::currentDateTimeUtc().date();
    const std::array<QDate, 2> dates{beforeTransferDate, afterCloseDate};
    {
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    for (std::size_t index = 0; index < dates.size(); ++index) {
      if (index && dates[index] == dates[0]) { continue; }
      const auto path = dayFile(fixture.profile, dates[index]);
      if (!QFile::exists(path)) { continue; }
      const auto bytes = test::read(path); QVERIFY(bytes.endsWith('\n'));
      const auto raw = std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
      const auto all = core::readSavedHistoryRecords(raw); QVERIFY(all);
      const auto projected = core::readSavedHistoryDay(raw, date(dates[index])); QVERIFY(projected);
      auto loaded = storage::readSavedHistoryDayView(*lease, date(dates[index]));
      QVERIFY(loaded && loaded->snapshot); QCOMPARE(loaded->snapshot->hours, projected->hours);
      QCOMPARE(loaded->snapshot->totals, projected->totals);
      if (firstHeader) {
        const auto header = bytes.split('\n').first().split(','); QCOMPARE(header.size(), qsizetype{9});
        QCOMPARE(header[0], QByteArray("s")); QCOMPARE(header[1], QByteArray("1"));
        for (std::size_t i = 0; i < firstAbsolute.size(); ++i) {
          bool ok = false; firstAbsolute[i] = header[static_cast<qsizetype>(i) + 3].toLongLong(&ok); QVERIFY(ok);
        }
        firstHeader = false;
      }
      for (const auto& record : *all) {
        ++records;
        for (std::size_t i = 0; i < recorded.size(); ++i) { recorded[i] += record.counters[i]; }
        QCOMPARE(record.counters[4], std::int64_t{0}); QCOMPARE(record.counters[5], std::int64_t{0});
      }
    }
    } // Borrowed read lease is released before the actual second Bridge opens.
    QVERIFY(!firstHeader && records >= 1);
    for (std::size_t i = 0; i < recorded.size(); ++i) {
      const auto firstActual = firstAbsolute[i] - InitialParts[i];
      QVERIFY(firstActual >= 0); QCOMPARE(recorded[i], peerFinal[i] - firstActual);
    }
    if (!qEnvironmentVariable("BIGLYBTC_ACCOUNTING_HANDOFF_DIRECTORY").isEmpty()) {
      // Optional owned capture only. Select the actual Current page and reveal
      // the Overall group after its joined-close packet has rendered.
      page.tabs().setCurrentIndex(1);
      child<QTabWidget>(page, "statistics-transfer-tabs").setCurrentIndex(0);
      child<QScrollArea>(page, "statistics-transfers-current").ensureWidgetVisible(
          &child<QWidget>(page, "statistics-overall-live"), 0, 20);
      QTRY_COMPARE(child<QLabel>(page, "overall-live-value-0").text(),
          QString::number(latest(packets)->values[0]));
      test::captureAccountingHandoff(multifile, fixture.profile, original, persisted, dates, final,
          closed.last().at(1).toBool(), closed.last().at(2).toBool(), window);
    }
    ui::LibraryWindow reopened; app::SessionBridge reopenedBridge; wire(reopened, reopenedBridge);
    reopened.resize(1280, 1000); reopened.show();
    QSignalSpy reopenRows(&reopenedBridge, &app::SessionBridge::snapshotReady);
    QSignalSpy reopenSaved(&reopenedBridge, &app::SessionBridge::statisticsReady);
    QVERIFY(reopenedBridge.open(fixture.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(!reopenedBridge.busy() && !reopenRows.empty(), 30000);
    auto& reopenedPage = openStatistics(reopened);
    QTRY_VERIFY_WITH_TIMEOUT(!reopenedBridge.busy() && !reopenSaved.empty() && reopenedPage.readState() == ui::StatisticsReadState::Ready, 10000);
    const auto readback = qvariant_cast<ui::StatisticsPresentation>(reopenSaved.last().at(0));
    QVERIFY(readback && readback->snapshot);
    for (std::size_t i = 0; i < persisted.size(); ++i) { QCOMPARE(readback->snapshot->values[i].value, std::optional{persisted[i]}); }
    QCOMPARE(test::read(statisticsPath), finalBytes);
    QVERIFY(!reopened.nativeSessionSnapshot()); // Offline re-open reads persisted facts only.
    QSignalSpy reopenedClosed(&reopenedBridge, &app::SessionBridge::sessionClosed);
    QVERIFY(reopenedBridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(reopenedClosed.count(), 1, 10000);
    QVERIFY(reopenedClosed.first().at(1).toBool()); QVERIFY(reopenedClosed.first().at(2).toBool());
    // Successful continuation deliberately commits original-compatible resume
    // and completion fields. Preserve every other encoded subtree verbatim.
    auto oldState = core::Document::parse(stateBefore.toStdString()); QVERIFY(oldState);
    auto newState = core::Document::parse(test::read(fixture.stateFile).toStdString()); QVERIFY(newState);
    const auto unchangedExcept = [&](const core::Document& beforeDoc, const core::Node& beforeNode,
        const core::Document& afterDoc, const core::Node& afterNode,
        std::initializer_list<std::string_view> mutableKeys) {
      if (beforeNode.type != core::Node::Type::Dictionary || afterNode.type != core::Node::Type::Dictionary) { return false; }
      const auto mutableKey = [&](std::string_view key) { return std::find(mutableKeys.begin(), mutableKeys.end(), key) != mutableKeys.end(); };
      for (std::size_t i = 0; i < beforeNode.children.size(); i += 2) {
        const auto key = beforeNode.children[i].bytes;
        if (mutableKey(key)) { continue; }
        const auto* after = afterNode.find(key);
        if (!after || encodedNode(beforeDoc, &beforeNode.children[i + 1]) != encodedNode(afterDoc, after)) { return false; }
      }
      for (std::size_t i = 0; i < afterNode.children.size(); i += 2) {
        const auto key = afterNode.children[i].bytes;
        if (!mutableKey(key) && !beforeNode.find(key)) { return false; }
      }
      return true;
    };
    QVERIFY(unchangedExcept(*oldState, oldState->root(), *newState, newState->root(), {"attributes", "resume"}));
    const auto* oldAttributes = oldState->find("attributes"); QVERIFY(oldAttributes);
    const auto* attributes = newState->find("attributes"); QVERIFY(attributes);
    QVERIFY(unchangedExcept(*oldState, *oldAttributes, *newState, *attributes, {"resumecomplete", "filedownloaded", "filelinks2"}));
    const auto* complete = attributes->find("resumecomplete"); QVERIFY(complete);
    QCOMPARE(complete->type, core::Node::Type::Integer); QCOMPARE(complete->integer, std::int64_t{2});
    const auto* downloaded = attributes->find("filedownloaded"); QVERIFY(downloaded);
    QCOMPARE(downloaded->type, core::Node::Type::Dictionary); QCOMPARE(downloaded->children.size(), std::size_t{2});
    const auto* counts = downloaded->find("downloaded"); QVERIFY(counts); QCOMPARE(counts->type, core::Node::Type::List);
    QCOMPARE(counts->children.size(), fixture.files.size());
    for (std::size_t i = 0; i < fixture.files.size(); ++i) {
      QCOMPARE(counts->children[i].type, core::Node::Type::Integer);
      QCOMPARE(counts->children[i].integer, static_cast<std::int64_t>(fixture.files[i].expected.size()));
    }
    const auto* links = attributes->find("filelinks2"); QVERIFY(links); QCOMPARE(links->type, core::Node::Type::List);
    QVERIFY(links->children.empty()); // These owned fixtures have no suffix or physical relocation.
    const auto* resume = newState->find("resume"); QVERIFY(resume); QCOMPARE(resume->type, core::Node::Type::Dictionary);
    QCOMPARE(resume->children.size(), std::size_t{2});
    const auto* data = resume->find("data"); QVERIFY(data); QCOMPARE(data->type, core::Node::Type::Dictionary);
    QCOMPARE(data->children.size(), std::size_t{6});
    const auto* pieceFlags = data->find("resume data"); QVERIFY(pieceFlags); QCOMPARE(pieceFlags->type, core::Node::Type::Bytes);
    QCOMPARE(std::string(pieceFlags->bytes), std::string(test::OwnedEngineFixture::PayloadBytes / 16384, '\1'));
    const auto* valid = data->find("valid"); QVERIFY(valid); QCOMPARE(valid->type, core::Node::Type::Integer);
    QCOMPARE(valid->integer, std::int64_t{1});
    const auto* blocks = data->find("blocks"); QVERIFY(blocks); QCOMPARE(blocks->type, core::Node::Type::Dictionary);
    QVERIFY(blocks->children.empty());
    auto oldInventory = core::Document::parse(inventoryBefore.toStdString()); QVERIFY(oldInventory);
    auto newInventory = core::Document::parse(test::read(fixture.profile + QStringLiteral("/downloads.config")).toStdString()); QVERIFY(newInventory);
    QVERIFY(unchangedExcept(*oldInventory, oldInventory->root(), *newInventory, newInventory->root(), {"downloads"}));
    const auto* oldDownloads = oldInventory->find("downloads"); const auto* downloads = newInventory->find("downloads");
    QVERIFY(oldDownloads && downloads); QCOMPARE(oldDownloads->type, core::Node::Type::List); QCOMPARE(downloads->type, core::Node::Type::List);
    QCOMPARE(oldDownloads->children.size(), std::size_t{1}); QCOMPARE(downloads->children.size(), std::size_t{1});
    const auto& oldEntry = oldDownloads->children.front(); const auto& entry = downloads->children.front();
    QVERIFY(unchangedExcept(*oldInventory, oldEntry, *newInventory, entry, {"downloaded", "completedbytes", "allocated"}));
    const auto* received = entry.find("downloaded"); QVERIFY(received); QCOMPARE(received->type, core::Node::Type::Integer);
    const auto* previous = oldEntry.find("downloaded"); QVERIFY(previous); QCOMPARE(previous->type, core::Node::Type::Integer);
    QVERIFY(received->integer - previous->integer >= test::OwnedEngineFixture::PayloadBytes);
    QVERIFY(received->integer - previous->integer <= actual.payloadReceived);
    const auto* completedBytes = entry.find("completedbytes"); QVERIFY(completedBytes); QCOMPARE(completedBytes->type, core::Node::Type::Integer);
    QCOMPARE(completedBytes->integer, static_cast<std::int64_t>(test::OwnedEngineFixture::PayloadBytes));
    const auto* allocated = entry.find("allocated"); QVERIFY(allocated); QCOMPARE(allocated->type, core::Node::Type::Integer);
    QCOMPARE(allocated->integer, std::int64_t{1});
    QVERIFY(payloadIntact(fixture)); QVERIFY(fixture.accountingIntact());
  }
  void optionalFileFailuresPreserveTransferAndRelease_data() {
    QTest::addColumn<bool>("overallFailure");
    QTest::newRow("overall-saving-fifo") << true; QTest::newRow("history-directory-symlink") << false;
  }
  void optionalFileFailuresPreserveTransferAndRelease() {
    QFETCH(bool, overallFailure); test::OwnedEngineFixture fixture(false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000); QVERIFY(fixture.intact(false));
    const auto statisticsPath = fixture.profile + QStringLiteral("/BiglyBT.statistics");
    const auto sentinel = fixture.root.path() + QStringLiteral("/external-sentinel");
    test::write(sentinel, QByteArray("untouched opaque sentinel")); const auto sentinelBytes = test::read(sentinel);
    if (overallFailure) {
      QVERIFY(::mkfifo((statisticsPath + QStringLiteral(".saving")).toStdString().c_str(), 0600) == 0);
    } else {
      const auto external = fixture.root.path() + QStringLiteral("/external-directory"); QVERIFY(QDir().mkpath(external));
      QVERIFY(::symlink(external.toStdString().c_str(), (fixture.profile + QStringLiteral("/stats")).toStdString().c_str()) == 0);
    }
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge); window.resize(1280, 1000); window.show();
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), overall(&bridge, &app::SessionBridge::overallReady);
    QSignalSpy operations(&bridge, &app::SessionBridge::engineStatisticsReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port())); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 30000);
    auto& page = openStatistics(window); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && page.readState().has_value(), 10000);
    const auto target = window.model().targetAt(0); QVERIFY(target); QVERIFY(bridge.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(overall) && latest(overall)->available && payloadIntact(fixture), 30000);
    QTRY_VERIFY(!operations.empty());
    const auto completed = qvariant_cast<ui::EngineStatisticsPresentation>(operations.last().at(0));
    QVERIFY(completed && completed->state == ui::EngineStatisticsState::Ended && completed->successful);
    if (overallFailure) {
      QTRY_VERIFY(child<QPushButton>(page, "statistics-set-mark").isEnabled());
      QTest::mouseClick(&child<QPushButton>(page, "statistics-set-mark"), Qt::LeftButton);
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(overall) && !latest(overall)->failure.isEmpty() && !latest(overall)->writable, 10000);
      QCOMPARE(test::read(statisticsPath), fixture.statistics);
      struct stat saving{}; QVERIFY(::lstat((statisticsPath + QStringLiteral(".saving")).toStdString().c_str(), &saving) == 0);
      QVERIFY(S_ISFIFO(saving.st_mode));
    } else { QTRY_VERIFY_WITH_TIMEOUT(!latest(overall)->historyFailure.isEmpty(), 5000); }
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QTRY_VERIFY(latest(overall)->closed);
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    QVERIFY(payloadIntact(fixture)); QCOMPARE(test::read(sentinel), sentinelBytes);
    if (overallFailure) { QCOMPARE(test::read(statisticsPath), fixture.statistics); }
    else { QVERIFY(!latest(overall)->historyFailure.isEmpty()); }
  }
  void disabledMinuteRecordingStillPersistsActualOverallOnJoinedClose() {
    test::OwnedEngineFixture fixture(false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000); QVERIFY(fixture.intact(false));
    const auto configPath = fixture.profile + QStringLiteral("/biglybt.config");
    const QByteArray config("d22:long.term.stats.enablei0ee");
    test::write(configPath, config);
    struct stat configBefore{};
    QVERIFY(::stat(configPath.toStdString().c_str(), &configBefore) == 0);
    const auto noHistory = [&] {
      const QFileInfo info(fixture.profile + QStringLiteral("/stats"));
      return !info.exists() && !info.isSymLink();
    };
    QVERIFY(noHistory());
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.resize(1280, 1000); window.show();
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), overall(&bridge, &app::SessionBridge::overallReady);
    QSignalSpy operations(&bridge, &app::SessionBridge::engineStatisticsReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 30000);
    const auto target = window.model().targetAt(0); QVERIFY(target); QVERIFY(bridge.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(overall) && latest(overall)->available
        && window.nativeSessionSnapshot() && window.nativeSessionSnapshot()->sample
        && window.nativeSessionSnapshot()->sample->counters.payloadReceived > 0, 30000);
    QVERIFY(!operations.empty());
    const auto completed = qvariant_cast<ui::EngineStatisticsPresentation>(operations.last().at(0));
    QVERIFY(completed && completed->state == ui::EngineStatisticsState::Ended && completed->successful);
    QVERIFY(payloadIntact(fixture)); QVERIFY(noHistory()); QCOMPARE(test::read(configPath), config);
    QVERIFY(bridge.closeSession());
    QTRY_VERIFY_WITH_TIMEOUT(closed.count() == 1 && latest(overall)->closed, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QVERIFY(latest(overall)->finalCounters); QVERIFY(latest(overall)->saved);
    QVERIFY(latest(overall)->failure.isEmpty()); QVERIFY(latest(overall)->historyFailure.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(window.nativeSessionSnapshot()
        && window.nativeSessionSnapshot()->state == ui::NativeSessionState::Closed, 5000);
    const auto final = window.nativeSessionSnapshot(); QVERIFY(final->sample);
    const auto persisted = values(test::read(fixture.profile + QStringLiteral("/BiglyBT.statistics")));
    QCOMPARE(field(persisted, Field::Downloaded), std::int64_t{999} + final->sample->counters.received);
    QCOMPARE(field(persisted, Field::Uploaded), final->sample->counters.sent);
    QCOMPARE(persisted, *latest(overall)->saved);
    QVERIFY(noHistory()); QCOMPARE(test::read(configPath), config);
    struct stat configAfter{}; QVERIFY(::stat(configPath.toStdString().c_str(), &configAfter) == 0);
    QCOMPARE(configAfter.st_dev, configBefore.st_dev); QCOMPARE(configAfter.st_ino, configBefore.st_ino);
    QCOMPARE(configAfter.st_mode, configBefore.st_mode); QCOMPARE(configAfter.st_size, configBefore.st_size);
    QCOMPARE(configAfter.st_mtim.tv_sec, configBefore.st_mtim.tv_sec);
    QCOMPARE(configAfter.st_mtim.tv_nsec, configBefore.st_mtim.tv_nsec);
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    QVERIFY(fixture.accountingIntact());
  }
};
QTEST_MAIN(OverallAccountingIntegrationTests)
#include "overall_accounting_integration_tests.moc"
