#include "statistics_page.hpp"

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QtTest>

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace biglybtc;
std::string b(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string d(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string result = "d";
  for (const auto& [key, value] : fields) { result += b(key) + value; }
  return result + 'e';
}
std::shared_ptr<const core::SavedStatistics> snapshot(std::string raw) {
  auto result = core::readSavedStatistics(raw);
  if (!result) { throw std::runtime_error("Generated statistics fixture failed"); }
  return std::make_shared<const core::SavedStatistics>(std::move(*result));
}
std::shared_ptr<const core::SavedStatistics> complete() {
  return snapshot(d({{"all", d({{"downloaded", "i1024e"}, {"uploaded", "i2048e"}, {"uptime", "i90061e"},
      {"mark_time", "i0e"}, {"mark_downloaded", "i100e"}, {"mark_uploaded", "i4096e"}, {"mark_uptime", "i61e"},
      {"dht_down", "i7e"}, {"dht_up", "i8e"}, {"p_uploaded", "i9e"}, {"d_uploaded", "i10e"}, {"p_downloaded", "i11e"}, {"d_downloaded", "i12e"}})},
      {"private-unrendered", b("SECRET_MUST_NOT_RENDER")}}));
}
std::shared_ptr<ui::StatisticsPacket> packet(quint64 session = 1, quint64 request = 1) {
  auto value = std::make_shared<ui::StatisticsPacket>(); value->session = session; value->request = request;
  value->state = ui::StatisticsReadState::Ready; value->source = ui::StatisticsReadSource::Primary; value->snapshot = complete(); return value;
}
QLabel* field(ui::StatisticsPage& page, const char* id) {
  auto* value = page.findChild<QLabel*>(QString::fromLatin1(id));
  if (!value) { throw std::runtime_error(std::string("Missing semantic field: ") + id); }
  return value;
}
void show(ui::StatisticsPage& page) { page.resize(1000, 800); page.show(); QCoreApplication::processEvents(); }
ui::Telemetry payload(qint64 down = 100, qint64 up = 50, quint64 generation = 1, const QString& hash = QStringLiteral("HASH-A")) {
  return {{hash, generation}, 2048, down, up, QStringLiteral("Downloading")};
}
std::shared_ptr<ui::EngineStatisticsPacket> enginePreparing(quint64 operation = 1) {
  auto value = std::make_shared<ui::EngineStatisticsPacket>(); value->session = 1; value->operation = operation; value->sequence = 1;
  value->target = {QStringLiteral("ENGINE-HASH"), 3}; value->name = QStringLiteral("Engine transfer"); return value;
}
std::shared_ptr<ui::EngineStatisticsPacket> engineObserved() {
  auto value = enginePreparing(); value->sequence = 2; value->state = ui::EngineStatisticsState::Observed;
  value->sample = core::EngineStatistics{{1000, 700, 800, 500}, 2000000000LL, core::EngineRates{100, 80, 70, 50, 30, 30}};
  return value;
}
void showEngine(ui::StatisticsPage& page) {
  page.tabs().setCurrentIndex(1);
  auto* inner = page.findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
  if (!inner) { throw std::runtime_error("Missing statistics Current/History tabs"); }
  inner->setCurrentIndex(0); show(page);
}
class Hook final : public QObject {
public:
  bool armed = true;
  bool consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; }
    return false;
  }
};
class Paints final : public QObject {
public:
  int count = 0;
  bool eventFilter(QObject*, QEvent* event) override { if (event->type() == QEvent::Paint) { ++count; } return false; }
};
} // namespace

class StatisticsPageTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void defaultsNavigationAndRealRefreshSignal() {
    ui::StatisticsPage page;
    QCOMPARE(page.objectName(), QStringLiteral("classic-statistics-page"));
    QCOMPARE(page.tabs().objectName(), QStringLiteral("statistics-tabs"));
    QCOMPARE(page.tabs().count(), 2); QCOMPARE(page.tabs().currentIndex(), 0);
    QCOMPARE(page.tabs().tabText(0), QStringLiteral("Activity")); QCOMPARE(page.tabs().tabText(1), QStringLiteral("Transfers"));
    auto* inner = page.findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs")); QVERIFY(inner);
    QCOMPARE(inner->tabText(0), QStringLiteral("Current")); QCOMPARE(inner->tabText(1), QStringLiteral("History")); QCOMPARE(inner->currentIndex(), 0);
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-activity")));
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-transfers")));
    QCOMPARE(page.sampleCount(), std::size_t{0}); QVERIFY(!page.readState()); QVERIFY(!page.loading());
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(field(page, "statistics-activity-observations")->text(), QStringLiteral("No activity samples yet"));
    QVERIFY(field(page, "statistics-current-unavailable")->text().contains(QStringLiteral("unavailable")));
    QVERIFY(field(page, "statistics-history-unavailable")->text().contains(QStringLiteral("History unavailable")));
    auto* refresh = page.findChild<QPushButton*>(QStringLiteral("statistics-refresh")); QVERIFY(refresh);
    QSignalSpy requests(&page, &ui::StatisticsPage::refreshRequested); refresh->click(); QCOMPARE(requests.count(), 1);
    QCOMPARE(page.session(), quint64{0}); QVERIFY(!page.loading());
  }
  void exactSavedFactsUnitsAndIndependentClampedSinceMark_data() {
    QTest::addColumn<int>("source");
    QTest::newRow("primary") << 0; QTest::newRow("backup") << 1; QTest::newRow("saving") << 2;
  }
  void exactSavedFactsUnitsAndIndependentClampedSinceMark() {
    QFETCH(int, source);
    ui::StatisticsPage page; page.beginRequest(1, 1); auto value = packet();
    value->source = static_cast<ui::StatisticsReadSource>(source); page.setPresentation(value);
    QCOMPARE(page.readState(), std::optional{ui::StatisticsReadState::Ready}); QVERIFY(!page.loading());
    const std::array<const char*, 13> ids{"downloaded", "uploaded", "uptime", "mark-time", "mark-downloaded", "mark-uploaded", "mark-uptime",
        "dht-downloaded", "dht-uploaded", "protocol-uploaded", "data-uploaded", "protocol-downloaded", "data-downloaded"};
    const std::array<qint64, 13> expected{1024, 2048, 90061, 0, 100, 4096, 61, 7, 8, 9, 10, 11, 12};
    for (std::size_t i = 0; i < ids.size(); ++i) {
      const auto id = QByteArray("statistics-saved-") + ids[i]; auto* scalar = field(page, id.constData());
      QVERIFY(scalar->toolTip().startsWith(QString::number(expected[i]) + QLatin1Char(' ')));
      QCOMPARE(scalar->textFormat(), Qt::PlainText);
      if (i == 2 || i == 6) { QVERIFY(scalar->toolTip().contains(QStringLiteral("seconds"))); }
      else if (i != 3) { QCOMPARE(scalar->text(), QStringLiteral("%1 bytes").arg(expected[i])); }
    }
    QCOMPARE(field(page, "statistics-saved-uptime")->text(), QStringLiteral("1d 1h 1m 1s"));
    QCOMPARE(field(page, "statistics-saved-mark-time")->text(), QStringLiteral("No mark date recorded"));
    QVERIFY(field(page, "statistics-saved-mark-time")->toolTip().contains(QStringLiteral("not a save timestamp")));
    QCOMPARE(field(page, "statistics-since-mark-downloaded")->text(), QStringLiteral("924 bytes")); // mark_time0 does not suppress baselines.
    QCOMPARE(field(page, "statistics-since-mark-uploaded")->text(), QStringLiteral("0 bytes"));
    QVERIFY(field(page, "statistics-since-mark-uploaded")->toolTip().contains(QStringLiteral("clamped for display only")));
    QCOMPARE(field(page, "statistics-since-mark-uptime")->toolTip(), QStringLiteral("90000 seconds"));
    const QStringList names{QStringLiteral("primary"), QStringLiteral("backup (.bak)"), QStringLiteral("saving (.saving)")};
    QVERIFY(field(page, "statistics-source")->text().contains(names[source]));
    QVERIFY(field(page, "statistics-source")->text().contains(QStringLiteral("save time unknown")));
    for (auto* label : page.findChildren<QLabel*>()) { QVERIFY(!label->text().contains(QStringLiteral("SECRET_MUST_NOT_RENDER"))); }
    QCOMPARE(value->snapshot->get(core::SavedStatistic::MarkUploaded).value, std::optional<std::int64_t>{4096});
  }
  void absentAllEmptyAllWrongTypeAndNegativeRemainDistinct() {
    ui::StatisticsPage page; page.beginRequest(1, 1); auto value = packet(); value->snapshot = snapshot("de"); page.setPresentation(value);
    QVERIFY(field(page, "statistics-read-status")->text().contains(QStringLiteral("no overall")));
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("Unavailable"));
    page.beginRequest(1, 2); value = packet(1, 2); value->snapshot = snapshot("d3:alldee"); page.setPresentation(value);
    QCOMPARE(field(page, "statistics-read-status")->text(), QStringLiteral("Last-saved statistics loaded"));
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("Unavailable"));
    page.beginRequest(1, 3); value = packet(1, 3); value->snapshot = snapshot(d({{"all", d({{"downloaded", "3:bad"}, {"uploaded", "i-1e"}, {"uptime", "i0e"}})}})); page.setPresentation(value);
    QVERIFY(field(page, "statistics-saved-downloaded")->toolTip().contains(QStringLiteral("invalid type")));
    QVERIFY(field(page, "statistics-saved-uploaded")->toolTip().startsWith(QStringLiteral("-1 bytes")));
    QCOMPARE(field(page, "statistics-saved-uploaded")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(field(page, "statistics-saved-uptime")->text(), QStringLiteral("0d 0h 0m 0s"));
    QCOMPARE(field(page, "statistics-since-mark-downloaded")->text(), QStringLiteral("Unavailable"));
  }
  void signedLongBoundariesNeverOverflowOrRoundIntegerTooltips() {
    ui::StatisticsPage page; page.beginRequest(1, 1); auto value = packet();
    value->snapshot = snapshot(d({{"all", d({{"downloaded", "i9223372036854775807e"}, {"mark_downloaded", "i0e"},
        {"uploaded", "i-9223372036854775808e"}, {"uptime", "i9223372036854775807e"}, {"mark_uptime", "i0e"}, {"mark_time", "i9223372036854775807e"}})}}));
    page.setPresentation(value);
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("9223372036854775807 bytes"));
    QCOMPARE(field(page, "statistics-since-mark-downloaded")->text(), QStringLiteral("9223372036854775807 bytes"));
    QVERIFY(field(page, "statistics-saved-uploaded")->toolTip().startsWith(QStringLiteral("-9223372036854775808 bytes")));
    QCOMPARE(field(page, "statistics-saved-uploaded")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(field(page, "statistics-since-mark-uptime")->toolTip(), QStringLiteral("9223372036854775807 seconds"));
    QVERIFY(field(page, "statistics-saved-mark-time")->toolTip().startsWith(QStringLiteral("9223372036854775807 epoch milliseconds")));
    QVERIFY(field(page, "statistics-saved-mark-time")->text() != QStringLiteral("Unavailable"));
  }
  void sessionRequestOnceOnlyStaleAndFailureGates() {
    ui::StatisticsPage page; page.beginRequest(0, 1); page.beginRequest(1, 0); QVERIFY(!page.loading());
    page.beginRequest(4, 10); QVERIFY(page.loading());
    page.setPresentation(packet(3, 100)); page.setPresentation(packet(4, 9)); QVERIFY(page.loading());
    page.setPresentation(packet(4, 10)); QVERIFY(!page.loading());
    page.setFailed(4, 10, QStringLiteral("duplicate")); QCOMPARE(page.readState(), std::optional{ui::StatisticsReadState::Ready});
    page.beginRequest(3, 100); page.beginRequest(4, 10); QVERIFY(!page.loading());
    page.beginRequest(4, 11); QVERIFY(page.loading()); QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("Unavailable"));
    page.setPresentation(packet(4, 10)); page.setFailed(4, 10, QStringLiteral("stale")); QVERIFY(page.loading());
    page.setFailed(4, 11, QStringLiteral("<b>failed</b>")); QCOMPARE(page.readState(), std::optional{ui::StatisticsReadState::Failed});
    QCOMPARE(field(page, "statistics-read-status")->text(), QStringLiteral("<b>failed</b>")); QCOMPARE(field(page, "statistics-read-status")->textFormat(), Qt::PlainText);
    page.beginRequest(5, 1); QVERIFY(page.loading()); QCOMPARE(page.session(), quint64{5}); QCOMPARE(page.request(), quint64{1});
  }
  void incompletePacketsClearPriorSavedFacts_data() {
    QTest::addColumn<int>("kind");
    for (int i = 0; i < 10; ++i) { QTest::newRow(qPrintable(QStringLiteral("incomplete-%1").arg(i))) << i; }
  }
  void incompletePacketsClearPriorSavedFacts() {
    QFETCH(int, kind);
    ui::StatisticsPage page; page.beginRequest(1, 1); page.setPresentation(packet());
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("1024 bytes"));
    page.beginRequest(1, 2); auto bad = packet(1, 2);
    if (kind == 0) { bad->source.reset(); }
    if (kind == 1) { bad->snapshot.reset(); }
    if (kind == 2) { auto copy = std::make_shared<core::SavedStatistics>(*bad->snapshot); copy->source.reset(); bad->snapshot = copy; }
    if (kind == 3) { bad->source = static_cast<ui::StatisticsReadSource>(99); }
    if (kind == 4) { bad->failure = QStringLiteral("conflicting error"); }
    if (kind == 5) { auto copy = std::make_shared<core::SavedStatistics>(*bad->snapshot); copy->values[0].invalidType = true; bad->snapshot = copy; }
    if (kind == 6) { auto copy = std::make_shared<core::SavedStatistics>(*bad->snapshot); copy->hasAll = false; bad->snapshot = copy; }
    if (kind == 7) {
      auto copy = std::make_shared<core::SavedStatistics>(*bad->snapshot);
      auto raw = d({{"opaque", b(std::string(core::Limits{}.inputBytes, 'x'))}}); auto limits = core::Limits{}; limits.inputBytes = raw.size();
      auto document = core::Document::parse(std::move(raw), limits); QVERIFY(document);
      copy->source = std::make_shared<const core::Document>(std::move(*document)); bad->snapshot = copy;
    }
    if (kind == 8) { bad->state = static_cast<ui::StatisticsReadState>(99); }
    if (kind == 9) { bad->state = ui::StatisticsReadState::Missing; }
    page.setPresentation(bad);
    QCOMPARE(page.readState(), std::optional{ui::StatisticsReadState::Failed});
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("Unavailable"));
    QVERIFY(field(page, "statistics-source")->text().contains(QStringLiteral("unavailable")));
  }
  void genuineMissingDoesNotFabricateZeroAndErrorTextIsBounded() {
    ui::StatisticsPage page; page.beginRequest(1, 1); auto value = std::make_shared<ui::StatisticsPacket>();
    value->session = 1; value->request = 1; value->state = ui::StatisticsReadState::Missing; page.setPresentation(value);
    QCOMPARE(page.readState(), std::optional{ui::StatisticsReadState::Missing});
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("Unavailable"));
    page.beginRequest(1, 2); page.setFailed(1, 2, QString(9000, QLatin1Char('x')));
    QCOMPARE(field(page, "statistics-read-status")->text().size(), qsizetype{8192});
  }
  void payloadRatesAreActualTargetBoundAndNoTimeFillsAreAdded() {
    ui::StatisticsPage page; show(page); page.recordPayload(payload(), QStringLiteral("<b>literal name</b>"));
    QCOMPARE(page.sampleCount(), std::size_t{1}); QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("100 bytes/s"));
    QCOMPARE(field(page, "statistics-payload-upload-rate")->text(), QStringLiteral("50 bytes/s"));
    QCOMPARE(field(page, "statistics-transfer-name")->text(), QStringLiteral("Transfer: <b>literal name</b>"));
    QCOMPARE(field(page, "statistics-transfer-name")->textFormat(), Qt::PlainText);
    page.recordPayload(payload(0, 0), QStringLiteral("renamed")); QCOMPARE(page.sampleCount(), std::size_t{2});
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("0 bytes/s"));
    QCoreApplication::processEvents(); QCOMPARE(page.sampleCount(), std::size_t{2});
    page.recordPayload(payload(10, 20, 2), QStringLiteral("new generation")); QCOMPARE(page.sampleCount(), std::size_t{1});
    page.recordPayload(payload(30, 40, 2, QStringLiteral("HASH-B")), QStringLiteral("another transfer")); QCOMPARE(page.sampleCount(), std::size_t{1});
    QVERIFY(field(page, "statistics-transfer-name")->toolTip().contains(QStringLiteral("HASH-B")));
  }
  void malformedTelemetryDoesNotClearOrReplaceGoodSeries_data() {
    QTest::addColumn<int>("kind");
    for (int i = 0; i < 6; ++i) { QTest::newRow(qPrintable(QStringLiteral("rejected-%1").arg(i))) << i; }
  }
  void malformedTelemetryDoesNotClearOrReplaceGoodSeries() {
    QFETCH(int, kind);
    ui::StatisticsPage page; show(page); page.recordPayload(payload(), QStringLiteral("kept")); auto bad = payload(900, 800, 2, QStringLiteral("OTHER"));
    if (kind == 0) { bad.completed = -1; } if (kind == 1) { bad.down = -1; } if (kind == 2) { bad.up = -1; }
    if (kind == 3) { bad.target.generation = 0; } if (kind == 4) { bad.target.hash.clear(); } if (kind == 5) { bad.target.hash = QString(257, QLatin1Char('A')); }
    page.recordPayload(bad, QStringLiteral("must not replace")); QCOMPARE(page.sampleCount(), std::size_t{1});
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("100 bytes/s"));
    QCOMPARE(field(page, "statistics-transfer-name")->text(), QStringLiteral("Transfer: kept"));
  }
  void sampleRingHasHard600BoundAndRealMonotonicInterval() {
    ui::StatisticsPage page;
    for (int i = 0; i < 607; ++i) { page.recordPayload(payload(i, i + 10), QStringLiteral("bounded")); }
    QCOMPARE(page.sampleCount(), std::size_t{600}); show(page);
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("606 bytes/s"));
    QCOMPARE(field(page, "statistics-payload-upload-rate")->text(), QStringLiteral("616 bytes/s"));
    const auto tooltip = field(page, "statistics-activity-observations")->toolTip();
    QVERIFY(tooltip.startsWith(QStringLiteral("Monotonic arrival interval: ")));
    bool valid = false; const auto interval = tooltip.section(QLatin1Char(' '), 3, 3).toLongLong(&valid); QVERIFY(valid && interval >= 0);
    QCoreApplication::processEvents(); QCOMPARE(page.sampleCount(), std::size_t{600});
  }
  void savedRefreshPreservesActivityNewSessionClearsIt() {
    ui::StatisticsPage page; show(page); page.beginRequest(1, 1); page.recordPayload(payload(), QStringLiteral("kept")); page.setPresentation(packet());
    page.beginRequest(1, 2); page.setFailed(1, 2, QStringLiteral("saved read failed")); QCOMPARE(page.sampleCount(), std::size_t{1});
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("100 bytes/s"));
    page.beginRequest(2, 1); QCOMPARE(page.sampleCount(), std::size_t{0}); QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("Unavailable"));
  }
  void hiddenAndMinimizedPagesRecordWithoutRepaintThenShowLatest() {
    ui::StatisticsPage page; show(page); auto* plot = page.findChild<QWidget*>(QStringLiteral("statistics-activity-plot")); QVERIFY(plot);
    Paints paints; plot->installEventFilter(&paints); page.hide(); QCoreApplication::processEvents(); const int hidden = paints.count;
    page.recordPayload(payload(111, 222), QStringLiteral("hidden")); QCoreApplication::processEvents(); QCOMPARE(paints.count, hidden);
    QCOMPARE(page.sampleCount(), std::size_t{1}); show(page); QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("111 bytes/s"));
    page.showMinimized(); QCoreApplication::processEvents(); QVERIFY(page.isMinimized()); const int minimized = paints.count;
    page.recordPayload(payload(333, 444), QStringLiteral("minimized")); QCoreApplication::processEvents(); QCOMPARE(paints.count, minimized);
    QCOMPARE(page.sampleCount(), std::size_t{2}); page.showNormal(); QCoreApplication::processEvents();
    QTRY_COMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("333 bytes/s"));
  }
  void savedRenderReentryCannotOverwriteNewRequest() {
    ui::StatisticsPage page; page.beginRequest(1, 1); Hook hook;
    hook.callback = [&] { page.beginRequest(1, 2); }; field(page, "statistics-saved-downloaded")->installEventFilter(&hook);
    page.setPresentation(packet()); QVERIFY(!hook.armed); QVERIFY(page.loading()); QCOMPARE(page.request(), quint64{2});
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(field(page, "statistics-saved-uploaded")->text(), QStringLiteral("Unavailable"));
  }
  void payloadRenderReentryCannotOverwriteNewTarget() {
    ui::StatisticsPage page; show(page); Hook hook;
    hook.callback = [&] { page.recordPayload(payload(700, 800, 3, QStringLiteral("NEW")), QStringLiteral("new")); };
    field(page, "statistics-transfer-name")->installEventFilter(&hook); page.recordPayload(payload(), QStringLiteral("old"));
    QVERIFY(!hook.armed); QCOMPARE(page.sampleCount(), std::size_t{1});
    QCOMPARE(field(page, "statistics-transfer-name")->text(), QStringLiteral("Transfer: new"));
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("700 bytes/s"));
  }
  void directDeletionDuringSavedRenderIsGuarded() {
    auto page = std::make_unique<ui::StatisticsPage>(); page->beginRequest(1, 1); QPointer guard(page.get()); Hook hook; hook.consume = true;
    hook.callback = [&] { page.reset(); }; field(*page, "statistics-saved-downloaded")->installEventFilter(&hook);
    page->setPresentation(packet()); QVERIFY(guard.isNull()); QVERIFY(!hook.armed);
  }
  void directDeletionDuringPayloadRenderIsGuarded() {
    auto page = std::make_unique<ui::StatisticsPage>(); show(*page); QPointer guard(page.get()); Hook hook; hook.consume = true;
    hook.callback = [&] { page.reset(); }; field(*page, "statistics-transfer-name")->installEventFilter(&hook);
    page->recordPayload(payload(), QStringLiteral("deleted")); QVERIFY(guard.isNull()); QVERIFY(!hook.armed);
  }
  void refreshSlotMayDeletePage() {
    auto page = std::make_unique<ui::StatisticsPage>(); QPointer guard(page.get());
    QObject::connect(page.get(), &ui::StatisticsPage::refreshRequested, qApp, [&] { page.reset(); }, Qt::DirectConnection);
    page->findChild<QPushButton*>(QStringLiteral("statistics-refresh"))->click(); QVERIFY(guard.isNull());
  }
  void engineGroupIsCompactUntilAnActualObservation() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page);
    auto* group = page.findChild<QWidget*>(QStringLiteral("statistics-engine-session")); QVERIFY(group);
    auto* counters = page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters")); QVERIFY(counters);
    QVERIFY(group->isVisible()); QVERIFY(counters->isHidden());
    QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Unavailable")));
    QVERIFY(field(page, "statistics-engine-target")->isHidden());
    QCOMPARE(page.sampleCount(), std::size_t{0});
    auto preparing = enginePreparing(); page.setEngineStatistics(preparing);
    QVERIFY(counters->isHidden()); QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Preparing")));
    QVERIFY(field(page, "statistics-engine-scope")->text().contains(QStringLiteral("One experimental transfer engine only")));
    QVERIFY(field(page, "statistics-engine-scope")->text().contains(QStringLiteral("not an exhaustive final accounting")));
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("Unavailable"));
  }
  void enginePacketGateRejectsMalformed_data() {
    QTest::addColumn<int>("kind");
    const std::array<const char*, 26> names{"null", "zero-expected-session", "zero-session", "wrong-session", "zero-operation", "zero-sequence",
        "zero-target-generation", "empty-target", "oversized-target", "oversized-name", "unknown-state", "preparing-wrong-sequence",
        "preparing-with-sample", "preparing-success", "observed-sequence-one", "observed-without-sample", "observed-success",
        "negative-counter", "payload-exceeds-total", "negative-elapsed", "negative-rate", "inconsistent-rate", "rate-at-zero-elapsed",
        "ended-sequence-one", "ended-invalid-sample", "overflowing-rate-components"};
    for (std::size_t i = 0; i < names.size(); ++i) { QTest::newRow(names[i]) << static_cast<int>(i); }
  }
  void enginePacketGateRejectsMalformed() {
    QFETCH(int, kind); auto value = engineObserved(); quint64 expectedSession = 1;
    if (kind == 1) { expectedSession = 0; } if (kind == 2) { value->session = 0; }
    if (kind == 3) { value->session = 2; } if (kind == 4) { value->operation = 0; } if (kind == 5) { value->sequence = 0; }
    if (kind == 6) { value->target.generation = 0; } if (kind == 7) { value->target.hash.clear(); }
    if (kind == 8) { value->target.hash = QString(257, QLatin1Char('A')); } if (kind == 9) { value->name = QString(8193, QLatin1Char('x')); }
    if (kind == 10) { value->state = static_cast<ui::EngineStatisticsState>(99); }
    if (kind == 11) { value->state = ui::EngineStatisticsState::Preparing; value->sample.reset(); }
    if (kind == 12) { value->state = ui::EngineStatisticsState::Preparing; value->sequence = 1; }
    if (kind == 13) { value = enginePreparing(); value->successful = true; }
    if (kind == 14) { value->sequence = 1; } if (kind == 15) { value->sample.reset(); } if (kind == 16) { value->successful = true; }
    if (kind == 17) { value->sample->counters.received = -1; } if (kind == 18) { value->sample->counters.payloadSent = 701; }
    if (kind == 19) { value->sample->elapsedNanoseconds = -1; } if (kind == 20) { value->sample->rates->sent = -1; }
    if (kind == 21) { value->sample->rates->protocolReceived = 31; } if (kind == 22) { value->sample->elapsedNanoseconds = 0; }
    if (kind == 23) { value->state = ui::EngineStatisticsState::Ended; value->sequence = 1; }
    if (kind == 24) { value->state = ui::EngineStatisticsState::Ended; value->sample->counters.sent = -1; }
    if (kind == 25) {
      value->sample->rates->received = std::numeric_limits<std::int64_t>::max();
      value->sample->rates->payloadReceived = std::numeric_limits<std::int64_t>::max();
      value->sample->rates->protocolReceived = std::numeric_limits<std::int64_t>::max();
    }
    ui::EngineStatisticsPresentation incoming = kind == 0 ? ui::EngineStatisticsPresentation{} : value;
    QVERIFY(!ui::acceptEngineStatistics({}, incoming, expectedSession));
    ui::StatisticsPage page;
    if (kind != 1) { page.beginRequest(1, 1); } // Case1 keeps the real Page session uninitialised.
    showEngine(page); page.setEngineStatistics(incoming);
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
  }
  void engineOrderingRejectsStaleAndRegressedPackets_data() {
    QTest::addColumn<int>("kind");
    const std::array<const char*, 15> names{"older-operation", "duplicate-sequence", "older-sequence", "changed-hash", "changed-generation",
        "received-regression", "sent-regression", "payload-receive-regression", "payload-send-regression", "protocol-receive-regression",
        "protocol-send-regression", "backward-elapsed", "equal-observation-elapsed", "after-terminal", "current-other-session"};
    for (std::size_t i = 0; i < names.size(); ++i) { QTest::newRow(names[i]) << static_cast<int>(i); }
  }
  void engineOrderingRejectsStaleAndRegressedPackets() {
    QFETCH(int, kind); auto previous = engineObserved(); previous->operation = 5;
    auto incoming = std::make_shared<ui::EngineStatisticsPacket>(*previous);
    incoming->sequence = 3; incoming->sample->elapsedNanoseconds = 3000000000LL;
    incoming->sample->counters = {1100, 800, 900, 600};
    if (kind == 0) { incoming->operation = 4; } if (kind == 1) { incoming->sequence = 2; } if (kind == 2) { incoming->sequence = 1; }
    if (kind == 3) { incoming->target.hash = QStringLiteral("OTHER"); } if (kind == 4) { ++incoming->target.generation; }
    if (kind == 5) { incoming->sample->counters.received = 999; incoming->sample->counters.payloadReceived = 800; }
    if (kind == 6) { incoming->sample->counters.sent = 699; incoming->sample->counters.payloadSent = 500; }
    if (kind == 7) { incoming->sample->counters.payloadReceived = 799; } if (kind == 8) { incoming->sample->counters.payloadSent = 499; }
    if (kind == 9) { incoming->sample->counters.payloadReceived = 950; } if (kind == 10) { incoming->sample->counters.payloadSent = 650; }
    if (kind == 11) { incoming->sample->elapsedNanoseconds = 1999999999LL; }
    if (kind == 12) { incoming->sample->elapsedNanoseconds = previous->sample->elapsedNanoseconds; }
    if (kind == 13) { previous->state = ui::EngineStatisticsState::Ended; } if (kind == 14) { previous->session = 2; }
    QVERIFY(!ui::acceptEngineStatistics(previous, incoming, 1));
    QCOMPARE(previous->sample->counters.received, std::int64_t{1000});
  }
  void engineMeasuredCountersAndCombinedIntervalRatesStaySeparate() {
    ui::StatisticsPage page; page.beginRequest(1, 1); page.setPresentation(packet()); showEngine(page);
    page.setEngineStatistics(enginePreparing()); auto observation = engineObserved(); page.setEngineStatistics(observation);
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isVisible());
    const std::array<const char*, 6> ids{"received", "sent", "payload-received", "payload-sent", "protocol-received", "protocol-sent"};
    const std::array<qint64, 6> totals{1000, 700, 800, 500, 200, 200};
    for (std::size_t i = 0; i < ids.size(); ++i) {
      auto* value = field(page, (QByteArray("statistics-engine-") + ids[i]).constData());
      QCOMPARE(value->text(), QStringLiteral("%1 bytes").arg(totals[i]));
      QCOMPARE(value->toolTip(), QStringLiteral("%1 measured bytes in this engine operation").arg(totals[i]));
      QCOMPARE(value->textFormat(), Qt::PlainText);
    }
    QCOMPARE(field(page, "statistics-engine-receive-rate")->text(), QStringLiteral("100 bytes/s"));
    QCOMPARE(field(page, "statistics-engine-send-rate")->text(), QStringLiteral("80 bytes/s"));
    QCOMPARE(field(page, "statistics-engine-status")->toolTip(), QStringLiteral("Last actual observation: 2000000000 nanoseconds since the first accepted observation"));
    QCOMPARE(field(page, "statistics-saved-downloaded")->text(), QStringLiteral("1024 bytes"));
    QCOMPARE(page.sampleCount(), std::size_t{0}); // Engine packets never add Activity samples.
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(field(page, "statistics-current-unavailable")->text(), QStringLiteral("Current global rates and this-session totals: unavailable"));
  }
  void engineFirstCounterSampleDoesNotInventRateButMeasuredZeroIsShown() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page);
    auto first = engineObserved(); first->sample->elapsedNanoseconds = 0; first->sample->rates.reset(); page.setEngineStatistics(first);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    QCOMPARE(field(page, "statistics-engine-receive-rate")->text(), QStringLiteral("Unavailable"));
    QVERIFY(field(page, "statistics-engine-receive-rate")->toolTip().contains(QStringLiteral("no rate is inferred")));
    auto interval = std::make_shared<ui::EngineStatisticsPacket>(*first); interval->sequence = 3;
    interval->sample->elapsedNanoseconds = 1000000000; interval->sample->rates = core::EngineRates{}; page.setEngineStatistics(interval);
    QCOMPARE(field(page, "statistics-engine-receive-rate")->text(), QStringLiteral("0 bytes/s"));
    QCOMPARE(field(page, "statistics-engine-send-rate")->text(), QStringLiteral("0 bytes/s"));
  }
  void engineTerminalRetainsMeasuredFactsAndRefusesLateSameRun() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); auto observed = engineObserved(); page.setEngineStatistics(observed);
    auto ended = std::make_shared<ui::EngineStatisticsPacket>(*observed); ended->sequence = 3;
    ended->state = ui::EngineStatisticsState::Ended; ended->successful = true; ended->sample.reset(); page.setEngineStatistics(ended);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Ended successfully; last observed")));
    QCOMPARE(field(page, "statistics-engine-receive-rate")->text(), QStringLiteral("100 bytes/s"));
    auto late = std::make_shared<ui::EngineStatisticsPacket>(*observed); late->sequence = 4;
    late->sample->counters = {2000, 1400, 1600, 1000}; late->sample->elapsedNanoseconds = 4000000000LL; page.setEngineStatistics(late);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    page.setEngineStatistics(ended); QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    auto next = enginePreparing(2); page.setEngineStatistics(next);
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
    QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Preparing")));
  }
  void engineHigherOperationTerminalNeverInheritsPreviousRunCounters() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); page.setEngineStatistics(engineObserved());
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    auto unseenRun = enginePreparing(2); unseenRun->sequence = 2; unseenRun->state = ui::EngineStatisticsState::Ended;
    page.setEngineStatistics(unseenRun); // No Preparing packet was delivered for this newer run.
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
    QCOMPARE(field(page, "statistics-engine-status")->text(), QStringLiteral("Ended; no engine counters were observed"));
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("Unavailable"));
    QCOMPARE(field(page, "statistics-engine-receive-rate")->text(), QStringLiteral("Unavailable"));
    QVERIFY(field(page, "statistics-engine-target")->toolTip().endsWith(QStringLiteral("engine operation 2")));
  }
  void engineEqualTimeTerminalMustRepeatTheExactObservation_data() {
    QTest::addColumn<bool>("changeRates");
    QTest::newRow("changed-counters") << false;
    QTest::newRow("changed-rates") << true;
  }
  void engineEqualTimeTerminalMustRepeatTheExactObservation() {
    QFETCH(bool, changeRates); auto current = engineObserved();
    auto terminal = std::make_shared<ui::EngineStatisticsPacket>(*current);
    terminal->state = ui::EngineStatisticsState::Ended; terminal->sequence = 3;
    if (changeRates) { terminal->sample->rates = core::EngineRates{101, 80, 71, 50, 30, 30}; }
    else { terminal->sample->counters = {1100, 800, 900, 600}; }
    QVERIFY(core::validEngineStatistics(*terminal->sample));
    QVERIFY(!ui::acceptEngineStatistics(current, terminal, 1));
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); page.setEngineStatistics(current); page.setEngineStatistics(terminal);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    QCOMPARE(field(page, "statistics-engine-receive-rate")->text(), QStringLiteral("100 bytes/s"));
    QVERIFY(!field(page, "statistics-engine-status")->text().contains(QStringLiteral("Ended")));
    terminal->sample->elapsedNanoseconds = 3000000000LL; // A genuinely later coalesced observation is admissible.
    QVERIFY(ui::acceptEngineStatistics(current, terminal, 1)); page.setEngineStatistics(terminal);
    QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Ended")));
  }
  void engineTerminalMayRepeatLastSampleOrHaveNoObservation() {
    auto observed = engineObserved(); auto ended = std::make_shared<ui::EngineStatisticsPacket>(*observed);
    ended->state = ui::EngineStatisticsState::Ended; ended->sequence = 3;
    QVERIFY(ui::acceptEngineStatistics(observed, ended, 1));
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); page.setEngineStatistics(observed); page.setEngineStatistics(ended);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    auto noObservation = enginePreparing(2); noObservation->state = ui::EngineStatisticsState::Ended; noObservation->sequence = 2;
    page.setEngineStatistics(noObservation);
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
    QCOMPARE(field(page, "statistics-engine-status")->text(), QStringLiteral("Ended; no engine counters were observed"));
  }
  void engineSavedRefreshKeepsFactsAndNewProfileClearsThem() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); page.setEngineStatistics(engineObserved());
    page.beginRequest(1, 2); page.setFailed(1, 2, QStringLiteral("saved totals unavailable"));
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    page.beginRequest(2, 1); QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
    QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Unavailable")));
    page.setEngineStatistics(engineObserved()); QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
    auto current = engineObserved(); current->session = 2; page.setEngineStatistics(current);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
  }
  void engineActualCachedObservationOrTerminalCanRestoreReopenedPage() {
    auto cached = engineObserved();
    { ui::StatisticsPage closed; closed.beginRequest(1, 1); closed.setEngineStatistics(cached); }
    ui::StatisticsPage reopened; reopened.beginRequest(1, 8); showEngine(reopened); reopened.setEngineStatistics(cached);
    QCOMPARE(field(reopened, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    auto terminalCache = std::make_shared<ui::EngineStatisticsPacket>(*cached); terminalCache->sequence = 3;
    terminalCache->state = ui::EngineStatisticsState::Ended;
    ui::StatisticsPage reopenedEnded; reopenedEnded.beginRequest(1, 9); showEngine(reopenedEnded); reopenedEnded.setEngineStatistics(terminalCache);
    QCOMPARE(field(reopenedEnded, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    QVERIFY(field(reopenedEnded, "statistics-engine-status")->text().contains(QStringLiteral("last observed")));
  }
  void engineHiddenMinimizedAndOtherTabsDeferOnlyRendering() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); page.hide(); QCoreApplication::processEvents();
    auto observed = engineObserved(); page.setEngineStatistics(observed);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("Unavailable"));
    showEngine(page); QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    page.showMinimized(); QCoreApplication::processEvents(); QVERIFY(page.isMinimized());
    auto later = std::make_shared<ui::EngineStatisticsPacket>(*observed); later->sequence = 3;
    later->sample->counters = {1100, 800, 900, 600}; later->sample->elapsedNanoseconds = 3000000000LL; page.setEngineStatistics(later);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1000 bytes"));
    page.showNormal(); QCoreApplication::processEvents(); QTRY_COMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1100 bytes"));
    page.tabs().setCurrentIndex(0); auto newest = std::make_shared<ui::EngineStatisticsPacket>(*later); newest->sequence = 4;
    newest->sample->counters = {1200, 900, 1000, 700}; newest->sample->elapsedNanoseconds = 4000000000LL; page.setEngineStatistics(newest);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1100 bytes"));
    page.tabs().setCurrentIndex(1); QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("1200 bytes"));
    auto* inner = page.findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs")); QVERIFY(inner); inner->setCurrentIndex(1);
    auto ended = std::make_shared<ui::EngineStatisticsPacket>(*newest); ended->sequence = 5; ended->state = ui::EngineStatisticsState::Ended;
    page.setEngineStatistics(ended); QVERIFY(!field(page, "statistics-engine-status")->text().contains(QStringLiteral("Ended")));
    inner->setCurrentIndex(0); QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Ended")));
  }
  void engineRenderReentryCannotOverwriteNewOperation() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); Hook hook;
    hook.callback = [&] { page.setEngineStatistics(enginePreparing(2)); };
    field(page, "statistics-engine-target")->installEventFilter(&hook); page.setEngineStatistics(engineObserved());
    QVERIFY(!hook.armed); QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Preparing")));
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
    auto next = engineObserved(); next->operation = 2; next->sample->counters = {50, 30, 20, 10}; page.setEngineStatistics(next);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("50 bytes"));
  }
  void engineRenderReentryCannotOverwriteNewProfile() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); Hook hook;
    hook.callback = [&] { page.beginRequest(2, 1); };
    field(page, "statistics-engine-received")->installEventFilter(&hook); page.setEngineStatistics(engineObserved());
    QVERIFY(!hook.armed); QCOMPARE(page.session(), quint64{2}); QVERIFY(page.loading());
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
    QVERIFY(field(page, "statistics-engine-status")->text().contains(QStringLiteral("Unavailable")));
  }
  void engineRenderDeletionIsGuarded() {
    auto page = std::make_unique<ui::StatisticsPage>(); page->beginRequest(1, 1); showEngine(*page); QPointer guard(page.get()); Hook hook; hook.consume = true;
    hook.callback = [&] { page.reset(); }; field(*page, "statistics-engine-status")->installEventFilter(&hook);
    page->setEngineStatistics(engineObserved()); QVERIFY(!hook.armed); QVERIFY(guard.isNull());
  }
  void engineNamesArePlainTextAndIntegerExtremesStayExact() {
    ui::StatisticsPage page; page.beginRequest(1, 1); showEngine(page); auto observed = engineObserved();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    observed->name = QStringLiteral("<b>literal transfer</b>"); observed->sample->counters = {maximum, maximum, maximum - 1, maximum - 2};
    observed->sample->elapsedNanoseconds = maximum; observed->sample->rates.reset(); page.setEngineStatistics(observed);
    QCOMPARE(field(page, "statistics-engine-received")->text(), QStringLiteral("9223372036854775807 bytes"));
    QCOMPARE(field(page, "statistics-engine-protocol-received")->text(), QStringLiteral("1 bytes"));
    QCOMPARE(field(page, "statistics-engine-protocol-sent")->text(), QStringLiteral("2 bytes"));
    QCOMPARE(field(page, "statistics-engine-target")->text(), QStringLiteral("Transfer: <b>literal transfer</b>"));
    QCOMPARE(field(page, "statistics-engine-target")->textFormat(), Qt::PlainText);
    QCOMPARE(field(page, "statistics-engine-status")->toolTip(), QStringLiteral("Last actual observation: 9223372036854775807 nanoseconds since the first accepted observation"));
    auto bounded = enginePreparing(2); bounded->name = QString(8192, QLatin1Char('x')); bounded->target.hash = QString(256, QLatin1Char('A'));
    page.setEngineStatistics(bounded); QVERIFY(field(page, "statistics-engine-target")->text().endsWith(bounded->name));
    QVERIFY(field(page, "statistics-engine-target")->toolTip().startsWith(bounded->target.hash));
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("statistics-engine-counters"))->isHidden());
  }
  void engineCounterPacketsNeverChangeExistingActivityTargetOrSamples() {
    ui::StatisticsPage page; page.beginRequest(1, 1); show(page); page.recordPayload(payload(), QStringLiteral("Activity target"));
    const auto identity = field(page, "statistics-transfer-name")->toolTip();
    page.tabs().setCurrentIndex(1); page.setEngineStatistics(engineObserved());
    QCOMPARE(page.sampleCount(), std::size_t{1}); page.tabs().setCurrentIndex(0);
    QCOMPARE(field(page, "statistics-transfer-name")->text(), QStringLiteral("Transfer: Activity target"));
    QCOMPARE(field(page, "statistics-transfer-name")->toolTip(), identity);
    QCOMPARE(field(page, "statistics-payload-download-rate")->text(), QStringLiteral("100 bytes/s"));
  }
};
QTEST_MAIN(StatisticsPageTests)
#include "statistics_page_tests.moc"
