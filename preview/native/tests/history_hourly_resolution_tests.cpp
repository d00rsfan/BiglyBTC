#include "history_week_view.hpp"
#include "saved_history_plot.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QMouseEvent>
#include <QPointer>
#include <QTableView>
#include <QTimeZone>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate End{2024, 3, 2};
template<class T> T& child(QWidget& owner, const char* name) {
  auto* value = owner.findChild<T*>(QString::fromLatin1(name));
  if (!value) { throw std::runtime_error("Missing hourly resolution control"); } return *value;
}
QDate qtDate(core::HistoryDate date) { return {date.year, static_cast<int>(date.month), static_cast<int>(date.day)}; }
QString dateText(core::HistoryDate date) { return qtDate(date).toString(Qt::ISODate); }
std::string anchor(core::HistoryDate date, unsigned hour) {
  const auto minute = QDateTime(qtDate(date), QTime(static_cast<int>(hour), 0), QTimeZone::UTC).toSecsSinceEpoch() / 60;
  return "s,1," + std::to_string(minute) + ",100,200,300,400,500,600\n";
}
std::string record(core::HistoryDate date, unsigned hour, std::int64_t down, std::int64_t up) {
  return anchor(date, hour) + "1000," + std::to_string(up) + ",3000," + std::to_string(down) + ",5000,6000\n";
}
core::SavedHistoryDay parsed(core::HistoryDate date, const std::string& text) {
  auto value = core::readSavedHistoryDay(text, date);
  if (!value) { throw std::runtime_error("Generated original-format fixture rejected"); } return *value;
}
std::shared_ptr<core::SavedHistoryWeek> week(bool populated = true) {
  const auto dates = core::savedHistoryWeekDates(End); if (!dates) { throw std::runtime_error("Invalid finite week"); }
  auto value = std::make_shared<core::SavedHistoryWeek>(); value->endingDate = End;
  for (std::size_t day = 0; day < dates->size(); ++day) {
    auto& row = value->days[day]; row.date = (*dates)[day];
    if (populated) {
      std::string text;
      for (unsigned hour = 0; hour < 24; ++hour) {
        text += record(row.date, hour, static_cast<std::int64_t>(day * 100 + hour) - 80,
            900 + static_cast<std::int64_t>(day * 7 + hour));
      }
      row.snapshot = parsed(row.date, text);
    }
  }
  return value;
}
auto packet(std::shared_ptr<const core::SavedHistoryWeek> value = week(), quint64 request = 1, quint64 session = 1) {
  auto result = std::make_shared<ui::HistoryWeekPacket>(); result->session = session; result->request = request;
  result->endingDate = End; result->state = ui::HistoryWeekReadState::Ready; result->snapshot = std::move(value); return result;
}
void show(QWidget& widget, int width = 1120) { widget.resize(width, 800); widget.show(); QCoreApplication::processEvents(); }
void begin(ui::HistoryWeekView& view, quint64 request = 1) {
  child<QDateEdit>(view, "saved-history-week-date").setDate(qtDate(End)); view.beginRequest(1, request, End);
}
auto& resolution(ui::HistoryWeekView& view) { return child<QComboBox>(view, "saved-history-week-resolution"); }
auto& plot(ui::HistoryWeekView& view) { return child<ui::SavedHistoryPlot>(view, "saved-history-week-chart"); }
auto& model(ui::HistoryWeekView& view) { return *child<QTableView>(view, "saved-history-week-days").model(); }
bool unknown(const ui::SavedHistoryPlotPresentation& frame) {
  if (!frame || frame->buckets().size() != 168) { return false; }
  return std::all_of(frame->buckets().begin(), frame->buckets().end(), [](const auto& bucket) { return !bucket.downloaded && !bucket.uploaded; });
}
void showPlot(ui::SavedHistoryPlot& widget, int width = 1000) {
  // Test fixture only: prevent colored LCD text fringes impersonating a trace.
  auto font = widget.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); widget.setFont(font);
  widget.resize(width, 220); widget.show(); QCoreApplication::processEvents();
}
void hover(ui::SavedHistoryPlot& widget, qreal bucket) {
  const auto area = widget.plotRect();
  const QPointF local{area.left() + area.width() * bucket / 167, area.center().y()};
  QMouseEvent event(QEvent::MouseMove, local, QPointF(widget.mapToGlobal(local.toPoint())), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&widget, &event);
}
bool seriesColor(QColor color, bool upload) {
  return upload ? color.red() > color.green() + 40 && color.green() > color.blue() + 30
                : color.blue() > color.red() + 60 && color.blue() > color.green() + 30;
}
int colored(const QImage& image, QRectF logical, bool upload) {
  const auto scale = image.devicePixelRatio();
  const auto left = std::clamp(static_cast<int>(std::floor(logical.left() * scale)), 0, image.width());
  const auto right = std::clamp(static_cast<int>(std::ceil(logical.right() * scale)), 0, image.width());
  const auto top = std::clamp(static_cast<int>(std::floor(logical.top() * scale)), 0, image.height());
  const auto bottom = std::clamp(static_cast<int>(std::ceil(logical.bottom() * scale)), 0, image.height());
  int count = 0;
  for (int x = left; x < right; ++x) { for (int y = top; y < bottom; ++y) {
    if (seriesColor(image.pixelColor(x, y), upload)) { ++count; }
  } }
  return count;
}
class TipHook final : public QObject {
public:
  bool armed = true, consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; } return false;
  }
};
} // namespace

class HistoryHourlyResolutionTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<core::HistoryDate>();
  }
  void all168BucketsHaveExactUtcIdentityAndPayloadOnlyValues() {
    const auto source = week(); const auto frame = ui::savedHistoryWeekHourlyPlot(End, source); QVERIFY(frame);
    QCOMPARE(static_cast<int>(ui::SavedHistoryPlotMode::Day), 0); QCOMPARE(static_cast<int>(ui::SavedHistoryPlotMode::Week), 1);
    QCOMPARE(frame->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(frame->endingDate(), End); QCOMPARE(frame->buckets().size(), std::size_t{168});
    for (std::size_t day = 0; day < 7; ++day) { for (unsigned hour = 0; hour < 24; ++hour) {
      const auto index = day * 24 + hour; const auto& bucket = frame->buckets()[index];
      QCOMPARE(bucket.date, source->days[day].date); QCOMPARE(bucket.hour, std::optional<unsigned>{hour});
      QCOMPARE(bucket.downloaded, std::optional<std::int64_t>{static_cast<std::int64_t>(day * 100 + hour) - 80});
      QCOMPARE(bucket.uploaded, std::optional<std::int64_t>{900 + static_cast<std::int64_t>(day * 7 + hour)});
      QCOMPARE(frame->bucketLabel(index), dateText(bucket.date) + QStringLiteral(" %1:00 UTC").arg(hour, 2, 10, QLatin1Char('0')));
    } }
    QCOMPARE(frame->minimum(), std::int64_t{-80}); QCOMPARE(frame->maximum(), std::int64_t{965});
    const auto daily = ui::savedHistoryWeekPlot(End, source); QVERIFY(daily); QCOMPARE(daily->buckets().size(), std::size_t{7});
    QCOMPARE(daily->buckets()[0].downloaded, std::optional<std::int64_t>{source->days[0].snapshot->totals->at(3)});
    QVERIFY(daily->buckets()[0].downloaded != frame->buckets()[0].downloaded); // Hourly is neither total/24 nor the daily total.
  }
  void missingDayMissingHourEmptyAndHeaderAreUnknownButRecordedZeroIsKnown() {
    auto source = week(false);
    source->days[1].snapshot = parsed(source->days[1].date, "");
    source->days[2].snapshot = parsed(source->days[2].date, anchor(source->days[2].date, 0));
    source->days[3].snapshot = parsed(source->days[3].date, record(source->days[3].date, 5, 0, 0));
    source->days[4].snapshot = parsed(source->days[4].date, record(source->days[4].date, 23, -9, -3));
    const auto frame = ui::savedHistoryWeekHourlyPlot(End, source); QVERIFY(frame);
    for (std::size_t i = 0; i < 168; ++i) {
      const auto& bucket = frame->buckets()[i];
      if (i == 77) { QCOMPARE(bucket.downloaded, std::optional<std::int64_t>{0}); QCOMPARE(bucket.uploaded, std::optional<std::int64_t>{0}); }
      else if (i == 119) { QCOMPARE(bucket.downloaded, std::optional<std::int64_t>{-9}); QCOMPARE(bucket.uploaded, std::optional<std::int64_t>{-3}); }
      else { QVERIFY(!bucket.downloaded && !bucket.uploaded); }
    }
    QVERIFY(frame->bucketToolTip(0).contains(QStringLiteral("Unknown"))); QVERIFY(frame->bucketToolTip(77).contains(QStringLiteral("0 bytes")));
  }
  void invalidRequestedRangesHaveNoFrameAndCannotRetargetWidget() {
    const std::array<core::HistoryDate, 5> invalid{{{2023, 2, 29}, {1970, 1, 6}, {1969, 12, 31}, {10000, 1, 1}, {2024, 0, 1}}};
    ui::SavedHistoryPlot widget; widget.setWeekHourly(End, week()); const auto original = widget.presentation(); QVERIFY(original);
    for (const auto date : invalid) {
      QVERIFY(!ui::savedHistoryWeekHourlyPlot(date, week())); widget.setWeekHourly(date, {}); QCOMPARE(widget.presentation(), original);
    }
  }
  void anyMismatchedSourceDateMakesWholeFrameUnknownWithRequestedLabels() {
    for (int variant = 0; variant < 3; ++variant) {
      auto source = week();
      if (variant == 0) { source->endingDate.day = 1; }
      else if (variant == 1) { source->days[4].date.day = 1; }
      else { source->days[6].snapshot->date.day = 1; }
      const auto frame = ui::savedHistoryWeekHourlyPlot(End, source); QVERIFY(unknown(frame));
      QCOMPARE(frame->bucketLabel(0), QStringLiteral("2024-02-25 00:00 UTC")); QCOMPARE(frame->bucketLabel(167), QStringLiteral("2024-03-02 23:00 UTC"));
    }
  }
  void copiedFrameOutlivesAndDoesNotAliasMutableSource() {
    auto source = week(); std::weak_ptr<core::SavedHistoryWeek> weak = source;
    const auto frame = ui::savedHistoryWeekHourlyPlot(End, source); const auto oldTip = frame->bucketToolTip(167);
    source->days[6].snapshot->hours[23] = core::HistoryCounters{}; source->days[0].snapshot.reset(); source->endingDate = {2025, 1, 1}; source.reset();
    QVERIFY(weak.expired()); QCOMPARE(frame->bucketToolTip(167), oldTip);
    QCOMPARE(frame->buckets()[167].downloaded, std::optional<std::int64_t>{543}); QCOMPARE(frame->buckets()[0].downloaded, std::optional<std::int64_t>{-80});
  }
  void exactSignedExtremesHaveFiniteGeometryWithoutChangingDailyFactory() {
    auto source = week(false);
    source->days[0].snapshot = parsed(source->days[0].date, record(source->days[0].date, 0, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()));
    source->days[6].snapshot = parsed(source->days[6].date, record(source->days[6].date, 23, std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min()));
    const auto frame = ui::savedHistoryWeekHourlyPlot(End, source); QVERIFY(frame);
    QCOMPARE(frame->minimum(), std::numeric_limits<std::int64_t>::min()); QCOMPARE(frame->maximum(), std::numeric_limits<std::int64_t>::max());
    const QRectF area{7, 9, 900, 100};
    for (const std::size_t index : {0U, 167U}) { for (const bool up : {false, true}) {
      const auto point = frame->point(index, up, area); QVERIFY(point); QVERIFY(std::isfinite(point->x()) && std::isfinite(point->y())); QVERIFY(area.contains(*point));
    } }
    QVERIFY(!frame->point(168, false, area)); QVERIFY(!frame->point(1, false, area)); QVERIFY(!frame->point(0, false, {}));
    QCOMPARE(ui::savedHistoryDayPlot(source->days[0].date, std::make_shared<const core::SavedHistoryDay>(*source->days[0].snapshot))->buckets().size(), std::size_t{24});
    QCOMPARE(ui::savedHistoryWeekPlot(End, source)->buckets().size(), std::size_t{7});
  }
  void hovering168BucketsAndHalfwayOffsetsUsesNearestExactUtcIdentity() {
    ui::SavedHistoryPlot widget; showPlot(widget, 1400); widget.setWeekHourly(End, week());
    for (std::size_t i = 0; i < 168; ++i) { hover(widget, static_cast<qreal>(i)); QCOMPARE(widget.toolTip(), widget.presentation()->bucketToolTip(i)); }
    hover(widget, 23.49); QVERIFY(widget.toolTip().startsWith(QStringLiteral("2024-02-25 23:00 UTC")));
    hover(widget, 23.51); QVERIFY(widget.toolTip().startsWith(QStringLiteral("2024-02-26 00:00 UTC")));
    hover(widget, -100); QCOMPARE(widget.toolTip(), widget.presentation()->bucketToolTip(0));
    hover(widget, 300); QCOMPARE(widget.toolTip(), widget.presentation()->bucketToolTip(167));
    QVERIFY(!widget.toolTip().contains(QLatin1Char('<'))); QVERIFY(!widget.accessibleName().contains(QStringLiteral("bytes/s")));
    QVERIFY(widget.accessibleName().contains(QStringLiteral("hourly"))); QVERIFY(widget.accessibleName().contains(QStringLiteral("2024-03-02")));
    widget.setWeekHourly(End, {}); hover(widget, 24); QVERIFY(widget.toolTip().contains(QStringLiteral("Unknown")));
  }
  void midnightConnectionHasInteriorStrokesAndMissingMidnightBreaksBoth() {
    auto connected = week(false);
    connected->days[0].snapshot = parsed(connected->days[0].date, record(connected->days[0].date, 23, 6, 2));
    connected->days[1].snapshot = parsed(connected->days[1].date, record(connected->days[1].date, 0, 6, 2) + record(connected->days[1].date, 1, 6, 2));
    connected->days[4].snapshot = parsed(connected->days[4].date, record(connected->days[4].date, 0, 10, 8));
    auto missing = std::make_shared<core::SavedHistoryWeek>(*connected); missing->days[1].snapshot->hours[0].reset();
    ui::SavedHistoryPlot widget; showPlot(widget, 4000); widget.setWeekHourly(End, connected);
    const auto frame = widget.presentation(); const auto area = widget.plotRect(); const auto continuous = widget.grab().toImage();
    widget.setWeekHourly(End, missing); const auto gap = widget.grab().toImage();
    for (const bool up : {false, true}) {
      const auto left = frame->point(23, up, area), right = frame->point(24, up, area); QVERIFY(left); QVERIFY(right);
      // Exclude marker radius + pen + antialias and floor/ceil pixel coverage.
      // A marker alone cannot pass this interior-stroke proof.
      const QRectF interior{left->x() + 6, left->y() - 2, right->x() - left->x() - 12, 4}; QVERIFY(interior.width() > 5);
      QVERIFY2(colored(continuous, interior, up) > 0, up ? "No upload stroke crossing UTC midnight" : "No download stroke crossing UTC midnight");
      QCOMPARE(colored(gap, interior, up), 0);
    }
  }
  void isolatedHourlyPointAndKnownZeroRemainVisibleMarkers() {
    auto source = week(false); source->days[3].snapshot = parsed(source->days[3].date, record(source->days[3].date, 8, 6, 2));
    ui::SavedHistoryPlot widget; showPlot(widget, 1000); widget.setWeekHourly(End, source); const auto image = widget.grab().toImage();
    for (const bool up : {false, true}) {
      const auto point = widget.presentation()->point(80, up, widget.plotRect()); QVERIFY(point);
      QVERIFY(colored(image, {point->x() - 4, point->y() - 4, 8, 8}, up) > 0);
    }
    source->days[3].snapshot = parsed(source->days[3].date, record(source->days[3].date, 8, 0, 0)); widget.setWeekHourly(End, source);
    const auto zero = widget.presentation()->point(80, false, widget.plotRect()); QVERIFY(zero); const auto raster = widget.grab().toImage();
    const QRectF near{zero->x() - 4, zero->y() - 4, 8, 8}; QVERIFY(colored(raster, near, false) + colored(raster, near, true) > 0);
    QCOMPARE(widget.presentation()->buckets()[80].downloaded, std::optional<std::int64_t>{0});
  }
  void narrowHourlyRenderingKeeps168FactsAndNoSamplingTimer() {
    ui::SavedHistoryPlot widget; widget.setWeekHourly(End, week());
    for (const int width : {1, 40, 120, 1000}) {
      showPlot(widget, width); QVERIFY(!widget.grab().isNull()); QCOMPARE(widget.presentation()->buckets().size(), std::size_t{168});
      QVERIFY(widget.height() >= 180 && widget.height() <= 260);
      if (widget.plotRect().width() > 0) {
        const auto point = widget.presentation()->point(167, false, widget.plotRect()); QVERIFY(point); QVERIFY(std::isfinite(point->x()) && std::isfinite(point->y()));
      }
    }
    QVERIFY(widget.findChildren<QTimer*>().isEmpty()); QVERIFY(widget.presentation()->bucketLabel(24).contains(QStringLiteral("2024-02-26 00:00 UTC")));
  }
  void daysDefaultAndModeSwitchPreserveAllSevenTableRowsAndBindingWithoutReads() {
    ui::HistoryWeekView view; begin(view); show(view); auto binding = packet(); view.setPresentation(binding);
    auto& choice = resolution(view); QCOMPARE(choice.count(), 2); QCOMPARE(choice.currentIndex(), 0);
    QCOMPARE(choice.itemText(0), QStringLiteral("Daily totals")); QCOMPARE(choice.itemText(1), QStringLiteral("Hourly values")); QCOMPARE(choice.accessibleName(), QStringLiteral("Graph resolution"));
    QCOMPARE(plot(view).presentation()->mode(), ui::SavedHistoryPlotMode::Week); QCOMPARE(model(view).rowCount(), 7); QCOMPARE(model(view).columnCount(), 8);
    std::array<QVariant, 56> before{}; for (int row = 0; row < 7; ++row) { for (int col = 0; col < 8; ++col) { before[static_cast<std::size_t>(row * 8 + col)] = model(view).data(model(view).index(row, col), Qt::UserRole); } }
    QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested), changes(&model(view), &QAbstractItemModel::dataChanged), resets(&model(view), &QAbstractItemModel::modelReset);
    for (const int index : {1, 0, 1}) {
      choice.setCurrentIndex(index); QTRY_COMPARE(plot(view).presentation()->mode(), index == 1 ? ui::SavedHistoryPlotMode::WeekHourly : ui::SavedHistoryPlotMode::Week);
      QCOMPARE(view.presentation(), ui::HistoryWeekPresentation(binding)); QCOMPARE(view.selectedDate(), End);
    }
    QCOMPARE(plot(view).presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(reads.size(), 0); QCOMPARE(changes.size(), 0); QCOMPARE(resets.size(), 0);
    for (int row = 0; row < 7; ++row) { for (int col = 0; col < 8; ++col) { QCOMPARE(model(view).data(model(view).index(row, col), Qt::UserRole), before[static_cast<std::size_t>(row * 8 + col)]); } }
  }
  void selectingHoursWhilePendingUsesActualArrivingSnapshotWithoutAnotherRequest() {
    ui::HistoryWeekView view; begin(view); show(view); QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested);
    resolution(view).setCurrentIndex(1); QTRY_VERIFY(unknown(plot(view).presentation())); view.setPresentation(packet());
    QCOMPARE(plot(view).presentation()->buckets()[167].downloaded, std::optional<std::int64_t>{543}); QCOMPARE(reads.size(), 0);
  }
  void hiddenModeChangesDeferChartTooltipAndTableUntilShownWithoutReread() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); const auto original = plot(view).presentation(); const auto tip = plot(view).toolTip();
    view.hide(); QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested), changes(&model(view), &QAbstractItemModel::dataChanged);
    resolution(view).setCurrentIndex(1); QCoreApplication::processEvents(); QCOMPARE(plot(view).presentation(), original); QCOMPARE(plot(view).toolTip(), tip); QCOMPARE(changes.size(), 0);
    show(view); QCOMPARE(plot(view).presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(reads.size(), 0); QCOMPARE(changes.size(), 0);
  }
  void minimizedModeChangesDeferUntilRestoreWithoutReread() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); view.showMinimized(); QCoreApplication::processEvents(); QVERIFY(view.isMinimized());
    const auto original = plot(view).presentation(); const auto tip = plot(view).toolTip(); QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested), changes(&model(view), &QAbstractItemModel::dataChanged);
    resolution(view).setCurrentIndex(1); QCoreApplication::processEvents(); QCOMPARE(plot(view).presentation(), original); QCOMPARE(plot(view).toolTip(), tip);
    view.showNormal(); QTRY_COMPARE(plot(view).presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(reads.size(), 0); QCOMPARE(changes.size(), 0);
  }
  void staleSessionRequestAndDateCannotReplaceAcceptedHourlyFrame() {
    ui::HistoryWeekView view; begin(view); show(view); resolution(view).setCurrentIndex(1); view.setPresentation(packet()); const auto original = plot(view).presentation();
    view.setPresentation(packet(week(false), 1, 2)); view.setPresentation(packet(week(false), 0)); auto wrongDate = packet(week(false)); wrongDate->endingDate.day = 1; view.setPresentation(wrongDate);
    QCOMPARE(plot(view).presentation(), original); QCOMPARE(resolution(view).currentIndex(), 1);
  }
  void pendingFailureMissingDaysAndNewSessionClearHoursButRetainResolution() {
    ui::HistoryWeekView view; begin(view); show(view); resolution(view).setCurrentIndex(1); view.setPresentation(packet()); view.beginRequest(1, 2, End);
    QVERIFY(unknown(plot(view).presentation())); view.setFailed(1, 2, End, QStringLiteral("Read failed")); QVERIFY(unknown(plot(view).presentation()));
    view.beginRequest(1, 3, End); view.setPresentation(packet(week(false), 3)); QVERIFY(unknown(plot(view).presentation()));
    view.beginRequest(1, 4, End); view.setPresentation(packet(week(), 4)); QVERIFY(!unknown(plot(view).presentation())); view.resetSession(2);
    QVERIFY(unknown(plot(view).presentation())); QCOMPARE(resolution(view).currentIndex(), 1); QVERIFY(!model(view).data(model(view).index(0, 5), Qt::UserRole).isValid());
  }
  void selectingAnotherDateClearsHourlyFactsAndUsesOnlyExistingDateRead() {
    ui::HistoryWeekView view; begin(view); show(view); resolution(view).setCurrentIndex(1); view.setPresentation(packet()); QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested);
    child<QDateEdit>(view, "saved-history-week-date").setDate(QDate(2024, 3, 3)); QCOMPARE(reads.size(), 1); QVERIFY(unknown(plot(view).presentation()));
    QCOMPARE(plot(view).presentation()->bucketLabel(167), QStringLiteral("2024-03-03 23:00 UTC")); resolution(view).setCurrentIndex(0); resolution(view).setCurrentIndex(1);
    QTRY_COMPARE(plot(view).presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(reads.size(), 1);
  }
  void tooltipModeReentryKeepsNewestDailyModeAndNeverNotifiesTable() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); plot(view).setToolTip(QStringLiteral("arm")); TipHook hook;
    hook.callback = [&] { resolution(view).setCurrentIndex(0); }; plot(view).installEventFilter(&hook);
    QSignalSpy reads(&view, &ui::HistoryWeekView::readRequested), changes(&model(view), &QAbstractItemModel::dataChanged); const auto binding = view.presentation();
    resolution(view).setCurrentIndex(1); QTRY_VERIFY(!hook.armed); QCOMPARE(resolution(view).currentIndex(), 0); QTRY_COMPARE(plot(view).presentation()->mode(), ui::SavedHistoryPlotMode::Week);
    QCOMPARE(view.presentation(), binding); QCOMPARE(reads.size(), 0); QCOMPARE(changes.size(), 0);
  }
  void tooltipNewRequestKeepsNewHourlyFrameAndDoesNotResumeOlderBinding() {
    ui::HistoryWeekView view; begin(view); show(view); view.setPresentation(packet()); auto newer = week(false);
    newer->days[0].snapshot = parsed(newer->days[0].date, record(newer->days[0].date, 0, 99, 88)); plot(view).setToolTip(QStringLiteral("arm")); TipHook hook;
    std::vector<QVariant> seen; connect(&model(view), &QAbstractItemModel::dataChanged, &view, [&] { seen.push_back(model(view).data(model(view).index(0, 5), Qt::UserRole)); });
    hook.callback = [&] { view.beginRequest(1, 2, End); view.setPresentation(packet(newer, 2)); }; plot(view).installEventFilter(&hook);
    resolution(view).setCurrentIndex(1); QTRY_VERIFY(!hook.armed); QCOMPARE(view.presentation()->request, quint64{2}); QCOMPARE(plot(view).presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly);
    QCOMPARE(plot(view).presentation()->buckets()[0].downloaded, std::optional<std::int64_t>{99}); QCOMPARE(seen.size(), std::size_t{2}); QVERIFY(!seen[0].isValid()); QCOMPARE(seen[1].toLongLong(), qlonglong{99});
  }
  void tooltipCallbackMayDeleteOwningViewDuringModeSwitch() {
    auto owner = std::make_unique<ui::HistoryWeekView>(); begin(*owner); show(*owner); owner->setPresentation(packet()); plot(*owner).setToolTip(QStringLiteral("arm"));
    TipHook hook; QPointer guard(owner.get()); bool setterReturned = false, callbackAfterSetter = false, deletedInsideCallback = false;
    hook.consume = true; hook.callback = [&] { callbackAfterSetter = setterReturned; owner.reset(); deletedInsideCallback = guard.isNull(); }; plot(*owner).installEventFilter(&hook);
    resolution(*owner).setCurrentIndex(1); setterReturned = true; QTRY_VERIFY(guard.isNull());
    QVERIFY(!hook.armed); QVERIFY(guard.isNull()); QVERIFY(callbackAfterSetter); QVERIFY(deletedInsideCallback);
  }
};

QTEST_MAIN(HistoryHourlyResolutionTests)
#include "history_hourly_resolution_tests.moc"
