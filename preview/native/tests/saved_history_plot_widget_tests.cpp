#include "saved_history_plot.hpp"

#include <QAccessible>
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate Date{2024, 2, 29}, Later{2024, 3, 2};
std::shared_ptr<core::SavedHistoryDay> day() {
  auto value = std::make_shared<core::SavedHistoryDay>(); value->date = Date; return value;
}
void known(core::SavedHistoryDay& value, unsigned hour, std::int64_t down = 6, std::int64_t up = 2) {
  value.hours[hour] = core::HistoryCounters{99, up, 88, down, 77, 66};
}
void show(ui::SavedHistoryPlot& plot, int width = 760) {
  // Keep black text grayscale so LCD fringes cannot impersonate colored traces.
  auto font = plot.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); plot.setFont(font);
  plot.resize(width, 220); plot.show(); QCoreApplication::processEvents();
}
void hover(ui::SavedHistoryPlot& plot, std::size_t index) {
  const auto area = plot.plotRect(); const auto count = plot.presentation()->buckets().size();
  const QPointF point{area.left() + area.width() * (static_cast<qreal>(index) / static_cast<qreal>(count - 1)), area.center().y()};
  QMouseEvent event(QEvent::MouseMove, point, QPointF(plot.mapToGlobal(point.toPoint())), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&plot, &event);
}
bool seriesColor(QColor color, bool upload) {
  return upload ? color.red() > color.green() + 40 && color.green() > color.blue() + 30
                : color.blue() > color.red() + 60 && color.blue() > color.green() + 30;
}
int colored(const QImage& image, QRectF logical, bool upload) {
  const auto scale = image.devicePixelRatio();
  const int left = std::clamp(static_cast<int>(std::floor(logical.left() * scale)), 0, image.width());
  const int right = std::clamp(static_cast<int>(std::ceil(logical.right() * scale)), 0, image.width());
  const int top = std::clamp(static_cast<int>(std::floor(logical.top() * scale)), 0, image.height());
  const int bottom = std::clamp(static_cast<int>(std::ceil(logical.bottom() * scale)), 0, image.height());
  int count = 0;
  for (int x = left; x < right; ++x) { for (int y = top; y < bottom; ++y) {
    if (seriesColor(image.pixelColor(x, y), upload)) { ++count; }
  } }
  return count;
}
QRectF around(QPointF point, qreal radius = 4) { return {point.x() - radius, point.y() - radius, radius * 2, radius * 2}; }
class ToolTipHook final : public QObject {
public:
  bool armed = true, consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; }
    return false;
  }
};
struct AccessArm {
  QObject* target;
  std::function<void()> callback;
  bool fired = false;
};
thread_local AccessArm* activeAccess = nullptr;
void accessibilityUpdate(QAccessibleEvent* event) {
  auto* arm = activeAccess;
  if (arm && event->type() == QAccessible::NameChanged && event->object() == arm->target) {
    activeAccess = nullptr; arm->fired = true; arm->callback();
  }
}
class AccessScope final {
public:
  AccessArm arm;
  AccessScope(QObject* target, std::function<void()> callback)
      : arm{target, std::move(callback)}, wasActive_(QAccessible::isActive()),
        previous_(QAccessible::installUpdateHandler(accessibilityUpdate)) {
    QAccessible::setActive(true); (void)QAccessible::queryAccessibleInterface(target); activeAccess = &arm;
  }
  ~AccessScope() {
    activeAccess = nullptr; QAccessible::installUpdateHandler(previous_); QAccessible::setActive(wasActive_);
  }
  AccessScope(const AccessScope&) = delete; AccessScope& operator=(const AccessScope&) = delete;
private:
  bool wasActive_;
  QAccessible::UpdateHandler previous_;
};
} // namespace

class SavedHistoryPlotWidgetTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void boundedHeightAndUnknownFramesNeverInventVisibleSamples() {
    ui::SavedHistoryPlot plot; show(plot); QVERIFY(!plot.presentation());
    QCOMPARE(plot.minimumHeight(), 180); QCOMPARE(plot.maximumHeight(), 260); QCOMPARE(plot.sizeHint().height(), 220);
    QVERIFY(plot.findChildren<QTimer*>().isEmpty()); QVERIFY(plot.accessibleName().contains(QStringLiteral("bytes")));
    plot.setDay(Date, {}); QCOMPARE(plot.presentation()->buckets().size(), std::size_t{24});
    const auto image = plot.grab().toImage();
    QCOMPARE(colored(image, plot.plotRect(), false), 0); QCOMPARE(colored(image, plot.plotRect(), true), 0);
    QVERIFY(!plot.accessibleName().contains(QStringLiteral("bytes/s"))); QVERIFY(!plot.toolTip().contains(QStringLiteral("0 bytes")));
  }
  void adjacentKnownPointsConnectAndUnknownBucketBreaksBothTraces() {
    auto continuous = day(), gap = day();
    for (unsigned hour : {4U, 5U, 6U}) { known(*continuous, hour); }
    for (unsigned hour : {4U, 6U}) { known(*gap, hour); }
    ui::SavedHistoryPlot plot; show(plot); plot.setDay(Date, continuous);
    const auto connected = plot.grab().toImage(); const auto frame = plot.presentation(); const auto area = plot.plotRect();
    const auto down = frame->point(5, false, area), up = frame->point(5, true, area); QVERIFY(down); QVERIFY(up);
    plot.setDay(Date, gap); const auto disconnected = plot.grab().toImage();
    QVERIFY(!plot.presentation()->buckets()[5].downloaded); QVERIFY(!plot.presentation()->buckets()[5].uploaded);
    QVERIFY(colored(connected, around(*down), false) > 0); QVERIFY(colored(connected, around(*up), true) > 0);
    QCOMPARE(colored(disconnected, around(*down), false), 0); QCOMPARE(colored(disconnected, around(*up), true), 0);
    // A marker at bucket 5 cannot satisfy these interior segment checks.
    for (const bool upload : {false, true}) {
      const auto fourth = frame->point(4, upload, area), fifth = frame->point(5, upload, area);
      QVERIFY(fourth); QVERIFY(fifth);
      const QPointF midpoint{(fourth->x() + fifth->x()) / 2, (fourth->y() + fifth->y()) / 2};
      QVERIFY(colored(connected, around(midpoint, 4), upload) > 0);
      QCOMPARE(colored(disconnected, around(midpoint, 4), upload), 0);
    }
  }
  void isolatedKnownAndActualZeroHaveVisibleMarkers() {
    auto source = day(); known(*source, 12); ui::SavedHistoryPlot plot; show(plot); plot.setDay(Date, source);
    auto image = plot.grab().toImage(); const auto area = plot.plotRect();
    for (const bool upload : {false, true}) {
      const auto point = plot.presentation()->point(12, upload, area); QVERIFY(point); QVERIFY(colored(image, around(*point), upload) > 0);
    }
    known(*source, 12, 0, 0); plot.setDay(Date, source); image = plot.grab().toImage();
    const auto zero = plot.presentation()->point(12, false, plot.plotRect()); QVERIFY(zero);
    QVERIFY(colored(image, around(*zero), false) + colored(image, around(*zero), true) > 0);
    QCOMPARE(plot.presentation()->buckets()[12].downloaded, std::optional<std::int64_t>{0});
  }
  void fullSignedExtremesAndNarrowWidthsRenderSafely() {
    auto source = day(); known(*source, 0, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());
    known(*source, 23, std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min());
    ui::SavedHistoryPlot plot; plot.setDay(Date, source);
    for (const int width : {1, 40, 120, 760}) {
      show(plot, width); const auto image = plot.grab().toImage(); QVERIFY(!image.isNull());
      QCOMPARE(plot.presentation()->buckets().size(), std::size_t{24});
      if (plot.plotRect().width() > 0) {
        for (const std::size_t index : {0U, 23U}) { for (const bool upload : {false, true}) {
          const auto point = plot.presentation()->point(index, upload, plot.plotRect()); QVERIFY(point);
          QVERIFY(std::isfinite(point->x())); QVERIFY(std::isfinite(point->y()));
        } }
      }
    }
    QVERIFY(plot.presentation()->bucketToolTip(0).contains(QStringLiteral("-9223372036854775808 bytes")));
  }
  void hoveringEveryBucketSelectsExactPlainValuesAndUnknowns() {
    auto source = day(); known(*source, 9, -123, 456); ui::SavedHistoryPlot plot; show(plot); plot.setDay(Date, source);
    for (std::size_t i = 0; i < 24; ++i) { hover(plot, i); QCOMPARE(plot.toolTip(), plot.presentation()->bucketToolTip(i)); }
    hover(plot, 9); QCOMPARE(plot.toolTip(), QStringLiteral("2024-02-29 09:00 UTC\nPayload downloaded: -123 bytes\nPayload uploaded: 456 bytes"));
    hover(plot, 10); QVERIFY(plot.toolTip().contains(QStringLiteral("Unknown"))); QVERIFY(!plot.toolTip().contains(QStringLiteral("bytes/s")));
    plot.setWeek(Later, {});
    for (std::size_t i = 0; i < 7; ++i) { hover(plot, i); QCOMPARE(plot.toolTip(), plot.presentation()->bucketToolTip(i)); }
    QVERIFY(!plot.toolTip().contains(QLatin1Char('<')));
  }
  void newUnknownFrameClearsOldHoveredValueAndInvalidRequestDoesNotRetarget() {
    auto source = day(); known(*source, 9, 12345, -987); ui::SavedHistoryPlot plot; show(plot); plot.setDay(Date, source); hover(plot, 9);
    QVERIFY(plot.toolTip().contains(QStringLiteral("12345 bytes"))); const auto old = plot.presentation();
    plot.setWeek({1970, 1, 6}, {}); QCOMPARE(plot.presentation(), old); QVERIFY(plot.toolTip().contains(QStringLiteral("12345 bytes")));
    plot.setDay(Later, {}); QCOMPARE(plot.presentation()->endingDate(), Later);
    QVERIFY(!plot.toolTip().contains(QStringLiteral("12345"))); QVERIFY(!plot.toolTip().contains(QStringLiteral("-987")));
    hover(plot, 9); QVERIFY(plot.toolTip().contains(QStringLiteral("Unknown"))); QVERIFY(plot.toolTip().startsWith(QStringLiteral("2024-03-02")));
  }
  void nestedToolTipReplacementKeepsNewerFrameAndAccessibility() {
    ui::SavedHistoryPlot plot; show(plot); plot.setDay(Date, {}); ToolTipHook hook;
    hook.callback = [&] { plot.setWeek(Later, {}); }; plot.installEventFilter(&hook);
    plot.setDay({2024, 3, 1}, {}); QVERIFY(!hook.armed);
    QCOMPARE(plot.presentation()->mode(), ui::SavedHistoryPlotMode::Week); QCOMPARE(plot.presentation()->endingDate(), Later);
    QVERIFY(plot.accessibleName().contains(QStringLiteral("2024-03-02"))); QVERIFY(plot.toolTip().contains(QStringLiteral("seven independent UTC days")));
  }
  void toolTipCallbackCanDeletePlotDuringSetter() {
    auto owner = std::make_unique<ui::SavedHistoryPlot>(); show(*owner); owner->setDay(Date, {}); QPointer guard(owner.get()); ToolTipHook hook;
    hook.consume = true; hook.callback = [&] { owner.reset(); }; owner->installEventFilter(&hook);
    owner->setWeek(Later, {}); QVERIFY(!hook.armed); QVERIFY(guard.isNull()); QCoreApplication::processEvents();
  }
  void accessibilityCallbackCannotResumeAnOlderSetter_data() {
    QTest::addColumn<bool>("deletion"); QTest::newRow("newer-frame") << false; QTest::newRow("owner-deleted") << true;
  }
  void accessibilityCallbackCannotResumeAnOlderSetter() {
    QFETCH(bool, deletion); auto owner = std::make_unique<ui::SavedHistoryPlot>(); show(*owner); owner->setDay(Date, {}); QPointer guard(owner.get());
    AccessScope access(owner.get(), [&] { if (deletion) { owner.reset(); } else { owner->setWeek(Later, {}); } });
    owner->setDay({2024, 3, 1}, {}); QVERIFY(access.arm.fired);
    if (deletion) { QVERIFY(guard.isNull()); }
    else {
      QVERIFY(guard); QCOMPARE(owner->presentation()->mode(), ui::SavedHistoryPlotMode::Week); QCOMPARE(owner->presentation()->endingDate(), Later);
      QVERIFY(owner->toolTip().contains(QStringLiteral("seven independent UTC days"))); QVERIFY(owner->accessibleName().contains(QStringLiteral("2024-03-02")));
    }
  }
  void parentDestructionOwnsPlotAndImmutableFrameOutlivesWidgets() {
    auto parent = std::make_unique<QWidget>(); auto* plot = new ui::SavedHistoryPlot(parent.get());
    auto source = day(); known(*source, 4, 19, -23); plot->setDay(Date, source);
    const auto frame = plot->presentation(); QPointer guard(plot); parent.reset(); QVERIFY(guard.isNull()); QCoreApplication::processEvents();
    QCOMPARE(frame->buckets()[4].downloaded, std::optional<std::int64_t>{19});
    QCOMPARE(frame->buckets()[4].uploaded, std::optional<std::int64_t>{-23});
  }
};
QTEST_MAIN(SavedHistoryPlotWidgetTests)
#include "saved_history_plot_widget_tests.moc"
