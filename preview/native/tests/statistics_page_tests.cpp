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
};
QTEST_MAIN(StatisticsPageTests)
#include "statistics_page_tests.moc"
