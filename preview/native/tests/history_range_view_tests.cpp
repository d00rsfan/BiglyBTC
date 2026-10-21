#include "history_range_view.hpp"
#include "history_range_plot.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimeZone>
#include <QtTest>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {
using namespace biglybtc;
constexpr std::int64_t Now = 1709382840000; // 2024-03-02 12:34:00 UTC.
template<class T> T& child(QObject& owner, const char* id) {
  auto* value = owner.findChild<T*>(QString::fromLatin1(id));
  if (!value) { throw std::runtime_error("Missing rolling-history control"); }
  return *value;
}
void show(QWidget& view) {
  auto font = view.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); view.setFont(font);
  view.resize(1200, 850); view.show(); QCoreApplication::processEvents();
}
core::HistoryRangeRequest query(core::HistoryPeriod period = core::HistoryPeriod::Week) { return {period, 0, Now}; }
std::shared_ptr<core::SavedHistoryRange> range(core::HistoryRangeRequest request = query(), bool observed = true) {
  const auto first = request.period == core::HistoryPeriod::All ? std::optional{request.nowMillis - 7 * std::int64_t{86400000}} : std::nullopt;
  const auto plan = core::planSavedHistoryRange(request, first);
  if (!plan) { throw std::runtime_error("Invalid generated query"); }
  auto result = std::make_shared<core::SavedHistoryRange>(); result->plan = *plan;
  result->buckets.resize(plan->bucketCount);
  auto start = plan->startMillis;
  for (auto& bucket : result->buckets) { bucket.startMillis = start; bucket.endMillis = start + plan->bucketMillis; start = bucket.endMillis; }
  const auto& a = plan->firstFile; const auto& b = plan->lastFile;
  const auto days = QDate(a.year, static_cast<int>(a.month), static_cast<int>(a.day)).daysTo(QDate(b.year, static_cast<int>(b.month), static_cast<int>(b.day))) + 1;
  result->presentFiles = 1; result->missingFiles = static_cast<std::size_t>(days - 1);
  if (observed) {
    result->buckets[0].observations = 1; result->buckets[0].totals = core::HistoryCounters{0, 0, 0, 0, 0, 0};
    const auto last = result->buckets.size() - 1;
    if (last != 0) { result->buckets[last].observations = 1; result->buckets[last].totals = core::HistoryCounters{-11, -22, -33, -44, -55, -66}; }
    // Synthetic transport DTO: whole-query observations/totals are independent
    // of bucket totals. This fixture claims admission/rendering, no accounting
    // or Java equality and never derives a whole value by summing the graph.
    result->observations = 3; result->totals = core::HistoryCounters{101, 202, 303, 404, 505, 606};
  }
  return result;
}
auto packet(std::shared_ptr<core::SavedHistoryRange> source = range(), quint64 request = 1, quint64 session = 1) {
  auto value = std::make_shared<ui::HistoryRangePacket>(); value->session = session; value->request = request;
  value->query = source->plan.request; value->state = ui::HistoryRangeReadState::Ready; value->snapshot = std::move(source); return value;
}
void begin(ui::HistoryRangeView& view, core::HistoryRangeRequest request = query(), quint64 number = 1) { view.beginRequest(1, number, request); }
QString total(ui::HistoryRangeView& view, const char* suffix) {
  return child<QLabel>(view, (QByteArray("saved-history-range-total-") + suffix).constData()).text();
}
class TipHook final : public QObject {
public:
  std::function<void()> callback; bool armed = true, consume = false;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; }
    return false;
  }
};
class ClipboardRestore final {
public:
  QPointer<QClipboard> clipboard{QGuiApplication::clipboard()}; std::unique_ptr<QMimeData> saved{std::make_unique<QMimeData>()};
  ClipboardRestore() {
    if (const auto* mime = clipboard->mimeData(QClipboard::Clipboard)) {
      for (const auto& format : mime->formats()) { saved->setData(format, mime->data(format)); }
    }
  }
  ~ClipboardRestore() { if (clipboard) { clipboard->setMimeData(saved.release(), QClipboard::Clipboard); } }
};
} // namespace

class HistoryRangeViewTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<core::HistoryRangeRequest>(); qRegisterMetaType<ui::HistoryRangePresentation>(); qRegisterMetaType<QClipboard::Mode>();
    QCOMPARE(QDateTime::fromMSecsSinceEpoch(Now, QTimeZone::utc()).toUTC().toString(Qt::ISODate), QStringLiteral("2024-03-02T12:34:00Z"));
  }
  void defaultsAndInvalidTupleUseWeekPayloadWithoutReads() {
    ui::HistoryRangeView view; QSignalSpy reads(&view, &ui::HistoryRangeView::readRequested);
    QCOMPARE(view.period(), core::HistoryPeriod::Week); QCOMPARE(view.offset(), std::int64_t{0}); QCOMPARE(view.traffic(), ui::SavedHistoryTraffic::Payload);
    QCOMPARE(child<QComboBox>(view, "saved-history-range-period").count(), 6); QVERIFY(!view.presentation()); QCOMPARE(reads.count(), 0);
    ui::HistoryRangeView invalid(nullptr, core::HistoryPeriod::All, -1, ui::SavedHistoryTraffic::Dht);
    QCOMPARE(invalid.period(), core::HistoryPeriod::Week); QCOMPARE(invalid.offset(), std::int64_t{0}); QCOMPARE(invalid.traffic(), ui::SavedHistoryTraffic::Payload);
    for (auto* value : view.findChildren<QLabel*>()) { QCOMPARE(value->textFormat(), Qt::PlainText); }
  }
  void allSixPeriodsAndThreeTrafficPairs_data() {
    QTest::addColumn<int>("period"); QTest::addColumn<int>("traffic");
    for (int p = 0; p < 6; ++p) { for (int t = 0; t < 3; ++t) { QTest::newRow(qPrintable(QStringLiteral("period-%1-traffic-%2").arg(p).arg(t))) << p << t; } }
  }
  void allSixPeriodsAndThreeTrafficPairs() {
    QFETCH(int, period); QFETCH(int, traffic);
    const auto p = static_cast<core::HistoryPeriod>(period); const auto t = static_cast<ui::SavedHistoryTraffic>(traffic);
    ui::HistoryRangeView view(nullptr, p, 0, t); show(view); const auto q = query(p); begin(view, q); const auto value = packet(range(q)); view.setPresentation(value);
    QCOMPARE(view.presentation(), value); auto& plot = child<ui::HistoryRangePlot>(view, "saved-history-range-chart"); QVERIFY(plot.presentation());
    constexpr std::array<std::size_t, 5> counts{60, 1440, 168, 720, 365};
    if (period < 5) { QCOMPARE(plot.presentation()->buckets().size(), counts[static_cast<std::size_t>(period)]); }
    else { QVERIFY(plot.presentation()->buckets().size() >= std::size_t{1}); }
    constexpr std::array<std::array<std::int64_t, 2>, 3> expected{{{-44, -22}, {-33, -11}, {-66, -55}}};
    QCOMPARE(plot.presentation()->traffic(), t); QCOMPARE(plot.presentation()->buckets().front().downloaded, std::optional<std::int64_t>{0});
    QCOMPARE(plot.presentation()->buckets().back().downloaded, std::optional<std::int64_t>{expected[static_cast<std::size_t>(traffic)][0]});
    QCOMPARE(plot.presentation()->buckets().back().uploaded, std::optional<std::int64_t>{expected[static_cast<std::size_t>(traffic)][1]});
    QCOMPARE(total(view, "protocol-up"), QStringLiteral("101 bytes")); QCOMPARE(total(view, "payload-up"), QStringLiteral("202 bytes"));
    QCOMPARE(total(view, "protocol-down"), QStringLiteral("303 bytes")); QCOMPARE(total(view, "payload-down"), QStringLiteral("404 bytes"));
    QCOMPARE(total(view, "dht-up"), QStringLiteral("505 bytes")); QCOMPARE(total(view, "dht-down"), QStringLiteral("606 bytes"));
    QVERIFY(child<QLabel>(view, "saved-history-range-coverage").text().contains(QStringLiteral("1 present")));
    QVERIFY(child<QLabel>(view, "saved-history-range-resolution").text().contains(QStringLiteral("buckets")));
  }
  void initialShowAndRefreshCaptureOneExplicitNowPerRead() {
    ui::HistoryRangeView view; QSignalSpy reads(&view, &ui::HistoryRangeView::readRequested);
    const auto before = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch(); show(view); QTRY_COMPARE(reads.count(), 1);
    auto q = qvariant_cast<core::HistoryRangeRequest>(reads.at(0).at(0)); QCOMPARE(q, view.query()); QVERIFY(q.nowMillis >= before);
    QVERIFY(q.nowMillis <= QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    view.requestRefresh(); QTRY_COMPARE(reads.count(), 2); q = qvariant_cast<core::HistoryRangeRequest>(reads.at(1).at(0)); QCOMPARE(q, view.query());
    QCOMPARE(q.period, core::HistoryPeriod::Week); QCOMPARE(q.offset, std::int64_t{0});
  }
  void previousNextAndAllNeverProducePositiveOffsets() {
    ui::HistoryRangeView view; show(view); QSignalSpy reads(&view, &ui::HistoryRangeView::readRequested);
    child<QPushButton>(view, "saved-history-range-previous").click(); QTRY_COMPARE(reads.count(), 1); QCOMPARE(view.offset(), std::int64_t{-1});
    child<QPushButton>(view, "saved-history-range-next").click(); QTRY_COMPARE(reads.count(), 2); QCOMPARE(view.offset(), std::int64_t{0});
    QVERIFY(!child<QPushButton>(view, "saved-history-range-next").isEnabled());
    child<QComboBox>(view, "saved-history-range-period").setCurrentIndex(5); QTRY_COMPARE(reads.count(), 3);
    QCOMPARE(view.period(), core::HistoryPeriod::All); QCOMPARE(view.offset(), std::int64_t{0});
    QVERIFY(!child<QPushButton>(view, "saved-history-range-previous").isEnabled()); QVERIFY(!child<QPushButton>(view, "saved-history-range-next").isEnabled());
    child<QPushButton>(view, "saved-history-range-previous").click(); child<QPushButton>(view, "saved-history-range-next").click(); QCOMPARE(reads.count(), 3);
  }
  void trafficChangesReprojectWithoutChangingWholeTotalsOrReadQuery() {
    ui::HistoryRangeView view; show(view); begin(view); const auto value = packet(); view.setPresentation(value);
    QSignalSpy reads(&view, &ui::HistoryRangeView::readRequested); const auto q = view.query(); auto& plot = child<ui::HistoryRangePlot>(view, "saved-history-range-chart"); const auto old = plot.presentation();
    child<QComboBox>(view, "saved-history-range-traffic").setCurrentIndex(1); child<QComboBox>(view, "saved-history-range-traffic").setCurrentIndex(2); QCOMPARE(plot.presentation(), old);
    QTRY_COMPARE(plot.presentation()->traffic(), ui::SavedHistoryTraffic::Dht); QCOMPARE(view.query(), q); QCOMPARE(view.presentation(), value); QCOMPARE(reads.count(), 0); QCOMPARE(total(view, "payload-down"), QStringLiteral("404 bytes"));
  }
  void wrongSessionRequestQueryAndDuplicatePacketCannotSettleNewRead() {
    ui::HistoryRangeView view; show(view); begin(view, query(), 4); view.setPresentation(packet(range(), 3)); view.setPresentation(packet(range(), 4, 2));
    auto wrong = packet(range(), 4); ++wrong->query.nowMillis; view.setPresentation(wrong); QVERIFY(!view.presentation());
    const auto value = packet(range(), 4); view.setPresentation(value); QCOMPARE(view.presentation(), value); view.setPresentation(packet(range(query(), false), 4)); QCOMPARE(view.presentation(), value);
    view.resetSession(2); QVERIFY(!view.presentation()); view.beginRequest(1, 5, query()); view.setPresentation(value); QVERIFY(!view.presentation());
    view.beginRequest(2, 1, query()); const auto next = packet(range(), 1, 2); view.setPresentation(next); QCOMPARE(view.presentation(), next);
  }
  void malformedReadyFailsWithoutLeakingPriorFacts_data() {
    QTest::addColumn<int>("kind"); for (int i = 0; i < 8; ++i) { QTest::newRow(qPrintable(QStringLiteral("bad-%1").arg(i))) << i; }
  }
  void malformedReadyFailsWithoutLeakingPriorFacts() {
    QFETCH(int, kind); ui::HistoryRangeView view; show(view); begin(view); auto source = range(); auto value = packet(source);
    switch (kind) {
      case 0: value->snapshot.reset(); break; case 1: source->buckets.pop_back(); break;
      case 2: ++source->plan.startMillis; break; case 3: ++source->buckets[0].endMillis; break;
      case 4: source->presentFiles = core::SavedHistoryRangeMaximumFiles + 1; break;
      case 5: source->buckets[0].observations = 0; break; case 6: value->failure = QStringLiteral("contradictory Ready"); break;
      default: value->state = static_cast<ui::HistoryRangeReadState>(88); break;
    }
    view.setPresentation(value); QVERIFY(view.presentation()); QCOMPARE(view.presentation()->state, ui::HistoryRangeReadState::Failed); QCOMPARE(total(view, "payload-down"), QString::fromUtf8("—"));
  }
  void inconsistentReadyCoverageCannotPublishKnownValues_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("observations-with-no-present-files") << 0;
    QTest::newRow("observations-exceed-present-file-capacity") << 1;
    QTest::newRow("bucket-observations-exceed-whole-observations") << 2;
  }
  void inconsistentReadyCoverageCannotPublishKnownValues() {
    QFETCH(int, kind); ui::HistoryRangeView view; show(view); begin(view); auto source = range();
    if (kind == 0) { source->missingFiles += source->presentFiles; source->presentFiles = 0; }
    else if (kind == 1) { source->observations = core::SavedHistoryMaximumRecords + 1; }
    else { source->buckets.front().observations = 3; source->buckets.back().observations = 3; }
    view.setPresentation(packet(source)); QVERIFY(view.presentation());
    QCOMPARE(view.presentation()->state, ui::HistoryRangeReadState::Failed); QVERIFY(!view.presentation()->snapshot);
    QCOMPARE(total(view, "payload-down"), QString::fromUtf8("—"));
    const auto plot = child<ui::HistoryRangePlot>(view, "saved-history-range-chart").presentation(); QVERIFY(plot);
    for (const auto& bucket : plot->buckets()) { QVERIFY(!bucket.downloaded && !bucket.uploaded); }
  }
  void missingCoverageEmptyFilesAndKnownZeroAreDifferent() {
    ui::HistoryRangeView view; show(view); begin(view); auto source = range(query(), false); source->missingFiles += source->presentFiles; source->presentFiles = 0;
    view.setPresentation(packet(source)); QVERIFY(child<QLabel>(view, "saved-history-range-status").text().startsWith(QStringLiteral("No saved files")));
    begin(view, query(), 2); source = range(query(), false); view.setPresentation(packet(source, 2)); QVERIFY(child<QLabel>(view, "saved-history-range-status").text().contains(QStringLiteral("no observations")));
    QCOMPARE(total(view, "payload-down"), QString::fromUtf8("—")); begin(view, query(), 3); view.setPresentation(packet(range(), 3));
    auto frame = child<ui::HistoryRangePlot>(view, "saved-history-range-chart").presentation(); QCOMPARE(frame->buckets().front().downloaded, std::optional<std::int64_t>{0}); QVERIFY(frame->point(0, false, QRectF(0, 0, 600, 100))); QVERIFY(!frame->buckets()[1].downloaded);
  }
  void hiddenMinimizedReadAndRenderingWaitForVisibility() {
    ui::HistoryRangeView view; QSignalSpy reads(&view, &ui::HistoryRangeView::readRequested);
    child<QComboBox>(view, "saved-history-range-period").setCurrentIndex(0); view.requestRefresh(); QCoreApplication::processEvents(); QCOMPARE(reads.count(), 0);
    show(view); QTRY_COMPARE(reads.count(), 1); QCOMPARE(qvariant_cast<core::HistoryRangeRequest>(reads[0][0]).period, core::HistoryPeriod::Hour);
    const auto q = query(core::HistoryPeriod::Hour); begin(view, q); view.setPresentation(packet(range(q))); const auto oldText = total(view, "payload-down");
    view.hide(); view.beginRequest(1, 2, q); view.setFailed(1, 2, q, QStringLiteral("<b>plain failure</b>")); QCoreApplication::processEvents(); QCOMPARE(total(view, "payload-down"), oldText);
    show(view); QTRY_COMPARE(total(view, "payload-down"), QString::fromUtf8("—")); QCOMPARE(child<QLabel>(view, "saved-history-range-status").text(), QStringLiteral("<b>plain failure</b>"));
    view.showMinimized(); QCoreApplication::processEvents(); QVERIFY(view.isMinimized()); const auto count = reads.count(); view.requestRefresh(); QCoreApplication::processEvents(); QCOMPARE(reads.count(), count);
    view.showNormal(); QTRY_COMPARE(reads.count(), count + 1);
  }
  void failureTextIsBoundedPlainTextAndStaleFailureIgnored() {
    ui::HistoryRangeView view; show(view); begin(view); view.setFailed(1, 2, query(), QStringLiteral("stale")); QVERIFY(!view.presentation());
    const auto error = QStringLiteral("<x>") + QString(10000, QLatin1Char('z')); view.setFailed(1, 1, query(), error);
    QCOMPARE(child<QLabel>(view, "saved-history-range-status").text(), error.left(8192)); QCOMPARE(child<QLabel>(view, "saved-history-range-status").textFormat(), Qt::PlainText);
  }
  void singleAllBucketAndFullSignedCoordinatesAreFinite() {
    auto q = query(core::HistoryPeriod::All); const auto plan = core::planSavedHistoryRange(q); QVERIFY(plan);
    auto source = std::make_shared<core::SavedHistoryRange>(); source->plan = *plan; source->buckets.push_back({plan->startMillis, plan->endMillis, core::HistoryCounters{0, 0, 0, 0, 0, 0}, 1});
    const auto single = ui::historyRangePlot(*plan, source); QVERIFY(single); QCOMPARE(single->buckets().size(), std::size_t{1}); const auto point = single->point(0, false, QRectF(10, 20, 100, 80)); QVERIFY(point); QCOMPARE(point->x(), qreal{60});
    source = range(query(core::HistoryPeriod::Hour)); (*source->buckets[0].totals)[3] = std::numeric_limits<std::int64_t>::min(); (*source->buckets.back().totals)[3] = std::numeric_limits<std::int64_t>::max();
    const auto extremes = ui::historyRangePlot(source->plan, source); QVERIFY(extremes); for (const auto i : {std::size_t{0}, extremes->buckets().size() - 1}) { const auto at = extremes->point(i, false, QRectF(0, 0, 200, 100)); QVERIFY(at); QVERIFY(std::isfinite(at->x()) && std::isfinite(at->y())); }
    QVERIFY(extremes->bucketToolTip(0).contains(QStringLiteral("-9223372036854775808 bytes"))); QVERIFY(extremes->bucketToolTip(extremes->buckets().size() - 1).contains(QStringLiteral("9223372036854775807 bytes")));
  }
  void frameOwnsCopiedScalarsAndWrongBindingBecomesUnknown() {
    auto source = range(); const auto frame = ui::historyRangePlot(source->plan, source); QVERIFY(frame); (*source->buckets.back().totals)[3] = 999; source.reset(); QCOMPARE(frame->buckets().back().downloaded, std::optional<std::int64_t>{-44});
    source = range(); ++source->buckets[3].endMillis; const auto unknown = ui::historyRangePlot(source->plan, source); QVERIFY(unknown); for (const auto& bucket : unknown->buckets()) { QVERIFY(!bucket.downloaded && !bucket.uploaded); }
  }
  void keyboardAndExplicitCopyUseExactSelectedIntervalOnly() {
    ClipboardRestore restore; QVERIFY(restore.clipboard); const auto sentinel = QStringLiteral("rolling history Clipboard sentinel"); restore.clipboard->setText(sentinel, QClipboard::Clipboard);
    QSignalSpy copied(restore.clipboard, &QClipboard::changed); ui::HistoryRangeView view(nullptr, core::HistoryPeriod::Hour); show(view); begin(view, query(core::HistoryPeriod::Hour)); view.setPresentation(packet(range(query(core::HistoryPeriod::Hour))));
    auto& plot = child<ui::HistoryRangePlot>(view, "saved-history-range-chart"); auto& copy = child<QAction>(plot, "saved-history-range-copy-selection");
    QVERIFY(!copy.isEnabled()); QCOMPARE(copied.count(), 0); view.activateWindow(); QTRY_VERIFY(view.isActiveWindow()); plot.setFocus(); QTRY_VERIFY(plot.hasFocus());
    QTest::keyClick(&plot, Qt::Key_Home); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{0}); QTest::keyClick(&plot, Qt::Key_Right); QCOMPARE(plot.selectedBucket(), std::optional<std::size_t>{1});
    const auto exact = QStringLiteral("2024-03-02T11:35:00.000Z — 2024-03-02T11:36:00.000Z UTC; nominal end exclusive\nQuery through 2024-03-02T11:35:59.999Z UTC (inclusive); epoch milliseconds [1709379300000, 1709379359999]\nPayload downloaded: Unknown\nPayload uploaded: Unknown");
    QCOMPARE(plot.toolTip(), exact); QCOMPARE(copied.count(), 0); QCOMPARE(restore.clipboard->text(QClipboard::Clipboard), sentinel);
    QTest::keyClick(&plot, Qt::Key_C, Qt::ControlModifier); QTRY_COMPARE(restore.clipboard->text(QClipboard::Clipboard), exact); QCOMPARE(copied.count(), 1);
    QVERIFY(restore.clipboard->mimeData(QClipboard::Clipboard)->hasText()); QVERIFY(!restore.clipboard->mimeData(QClipboard::Clipboard)->hasHtml());
    QTest::keyClick(&plot, Qt::Key_End); const auto last = plot.presentation()->bucketToolTip(59); copy.trigger(); QCOMPARE(restore.clipboard->text(QClipboard::Clipboard), last); QCOMPARE(copied.count(), 2);
    child<QComboBox>(view, "saved-history-range-traffic").setCurrentIndex(2); QTRY_COMPARE(plot.presentation()->traffic(), ui::SavedHistoryTraffic::Dht); QVERIFY(!copy.isEnabled()); QVERIFY(!plot.selectedBucket()); QCOMPARE(copied.count(), 2);
  }
  void readSignalMayDeleteItsOwner() {
    auto* raw = new ui::HistoryRangeView; QPointer<ui::HistoryRangeView> guard(raw);
    connect(raw, &ui::HistoryRangeView::readRequested, this, [raw](core::HistoryRangeRequest) { delete raw; }); show(*raw); QTRY_VERIFY(guard.isNull());
  }
  void newerReadReenteredDuringLabelWriteWins() {
    ui::HistoryRangeView view; show(view); begin(view); auto& status = child<QLabel>(view, "saved-history-range-status"); TipHook hook;
    hook.callback = [&] { begin(view, query(), 2); view.setFailed(1, 2, query(), QStringLiteral("newer failure")); }; status.installEventFilter(&hook);
    view.setPresentation(packet()); QVERIFY(!hook.armed); QCOMPARE(view.presentation()->request, quint64{2}); QCOMPARE(status.text(), QStringLiteral("newer failure")); QCOMPARE(total(view, "payload-down"), QString::fromUtf8("—"));
  }
  void finalTotalTooltipMayDeleteOrReenterOwner_data() {
    QTest::addColumn<bool>("deleteOwner");
    QTest::newRow("delete-on-final-dht-tooltip") << true;
    QTest::newRow("newer-failure-on-final-dht-tooltip") << false;
  }
  void finalTotalTooltipMayDeleteOrReenterOwner() {
    QFETCH(bool, deleteOwner);
    auto* raw = new ui::HistoryRangeView; QPointer<ui::HistoryRangeView> guard(raw); show(*raw); begin(*raw);
    auto& finalTotal = child<QLabel>(*raw, "saved-history-range-total-dht-down");
    // beginRequest already installed the normal tooltip. Force an actual
    // ToolTipChange on this last label when the accepted result renders.
    finalTotal.setToolTip(QStringLiteral("arm next final-total tooltip change"));
    TipHook hook; hook.consume = deleteOwner;
    hook.callback = [&] {
      if (deleteOwner) { delete raw; }
      else { begin(*raw, query(), 2); raw->setFailed(1, 2, query(), QStringLiteral("newer final-counter failure")); }
    };
    finalTotal.installEventFilter(&hook); raw->setPresentation(packet()); QVERIFY(!hook.armed);
    if (deleteOwner) { QVERIFY(guard.isNull()); }
    else {
      QVERIFY(guard); QVERIFY(raw->presentation()); QCOMPARE(raw->presentation()->request, quint64{2});
      QCOMPARE(raw->presentation()->state, ui::HistoryRangeReadState::Failed);
      QCOMPARE(child<QLabel>(*raw, "saved-history-range-status").text(), QStringLiteral("newer final-counter failure"));
      QCOMPARE(total(*raw, "dht-down"), QString::fromUtf8("—"));
      const auto plot = child<ui::HistoryRangePlot>(*raw, "saved-history-range-chart").presentation(); QVERIFY(plot);
      QVERIFY(!plot->buckets().back().downloaded); QVERIFY(!plot->buckets().back().uploaded);
      delete raw;
    }
  }
  void queuedTrafficRefreshMayDeleteOwnerAfterComboSetterReturns() {
    auto* raw = new ui::HistoryRangeView; QPointer<ui::HistoryRangeView> guard(raw); show(*raw); begin(*raw); raw->setPresentation(packet());
    auto& plot = child<ui::HistoryRangePlot>(*raw, "saved-history-range-chart"); TipHook hook; hook.consume = true; bool setterReturned = false, callbackAfterSetter = false;
    hook.callback = [&] { callbackAfterSetter = setterReturned; delete raw; }; plot.installEventFilter(&hook);
    child<QComboBox>(*raw, "saved-history-range-traffic").setCurrentIndex(2); setterReturned = true; QVERIFY(guard); QTRY_VERIFY(guard.isNull()); QVERIFY(callbackAfterSetter);
  }
  void clipboardNotificationMayDeletePlotWithoutFurtherReceiverAccess() {
    ClipboardRestore restore; auto* raw = new ui::HistoryRangePlot; QPointer<ui::HistoryRangePlot> guard(raw); const auto source = range(); raw->setRange(source->plan, source); show(*raw);
    raw->activateWindow(); QTRY_VERIFY(raw->isActiveWindow()); raw->setFocus(); QTRY_VERIFY(raw->hasFocus()); QTest::keyClick(raw, Qt::Key_Home);
    const auto expected = raw->presentation()->bucketToolTip(0); auto* copy = &child<QAction>(*raw, "saved-history-range-copy-selection");
    const auto connection = connect(restore.clipboard, &QClipboard::changed, this, [raw](QClipboard::Mode mode) { if (mode == QClipboard::Clipboard) { delete raw; } });
    copy->trigger(); disconnect(connection); QVERIFY(guard.isNull()); QCOMPARE(restore.clipboard->text(QClipboard::Clipboard), expected);
  }
  void optionalOwnedCaptureContainsActualAcceptedRange() {
    const auto directory = qEnvironmentVariable("BIGLYBTC_HISTORY_RANGE_CAPTURE_DIRECTORY"); if (directory.isEmpty()) { return; }
    const QDir out(directory); QVERIFY(out.exists()); QVERIFY(QFileInfo(directory).canonicalFilePath() == directory);
    ui::HistoryRangeView view(nullptr, core::HistoryPeriod::Month); show(view); begin(view, query(core::HistoryPeriod::Month)); view.setPresentation(packet(range(query(core::HistoryPeriod::Month))));
    child<QComboBox>(view, "saved-history-range-traffic").setCurrentIndex(2); QTRY_COMPARE(child<ui::HistoryRangePlot>(view, "saved-history-range-chart").presentation()->traffic(), ui::SavedHistoryTraffic::Dht);
    const auto path = out.filePath(QStringLiteral("saved-history-rolling-month.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(view.grab().save(path));
  }
};
QTEST_MAIN(HistoryRangeViewTests)
#include "history_range_view_tests.moc"
