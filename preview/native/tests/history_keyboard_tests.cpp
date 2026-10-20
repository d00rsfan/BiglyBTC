#include "saved_history_plot.hpp"
#include "saved_history_traffic.hpp"

#include <QAccessible>
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
using Traffic = ui::SavedHistoryTraffic;
constexpr core::HistoryDate Date{2024, 2, 29}, End{2024, 3, 2};
constexpr std::array<Traffic, 3> Kinds{Traffic::Payload, Traffic::Protocol, Traffic::Dht};
constexpr std::array<std::size_t, 3> Down{3, 2, 5}, Up{1, 0, 4};
std::shared_ptr<core::SavedHistoryDay> day(core::HistoryDate date = Date) {
  auto d = std::make_shared<core::SavedHistoryDay>(); d->date = date; d->observations = 2;
  d->hours[1] = core::HistoryCounters{}; d->hours[23] = core::HistoryCounters{10, 20, 30, 40, 50, 60}; d->totals = *d->hours[23]; return d;
}
std::shared_ptr<core::SavedHistoryWeek> week() {
  auto w = std::make_shared<core::SavedHistoryWeek>(); w->endingDate = End; const auto dates = core::savedHistoryWeekDates(End);
  if (!dates) { throw std::runtime_error("Finite week invalid"); }
  for (std::size_t i = 0; i < 7; ++i) { w->days[i].date = (*dates)[i]; }
  auto zero = day(w->days[1].date); zero->hours = {}; zero->hours[0] = core::HistoryCounters{}; zero->totals = core::HistoryCounters{}; zero->observations = 1;
  w->days[1].snapshot = *zero; w->days[6].snapshot = *day(w->days[6].date); return w;
}
void frame(ui::SavedHistoryPlot& p, int mode, Traffic kind = Traffic::Payload, bool unknown = false) {
  if (mode == 0) { p.setDay(Date, unknown ? nullptr : day(), kind); }
  else if (mode == 1) { p.setWeek(End, unknown ? nullptr : week(), kind); }
  else { p.setWeekHourly(End, unknown ? nullptr : week(), kind); }
}
void show(ui::SavedHistoryPlot& p, int width = 1100) {
  auto font = p.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); p.setFont(font);
  p.resize(width, 240); p.show(); QCoreApplication::processEvents(); p.activateWindow(); p.setFocus(Qt::OtherFocusReason); QCoreApplication::processEvents();
}
// One event only for destructive observers. QTest::keyClick sends a later
// KeyRelease to its original pointer, which would be a fixture UAF after deletion.
void press(ui::SavedHistoryPlot* p, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers); QCoreApplication::sendEvent(p, &event);
}
void hover(ui::SavedHistoryPlot& p, std::size_t index) {
  const auto area = p.plotRect(); const auto count = p.presentation()->buckets().size();
  const QPointF local{area.left() + area.width() * static_cast<qreal>(index) / static_cast<qreal>(count - 1), area.center().y()};
  QMouseEvent event(QEvent::MouseMove, local, QPointF(p.mapToGlobal(local.toPoint())), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QCoreApplication::sendEvent(&p, &event);
}
void selection(ui::SavedHistoryPlot& p, std::size_t index) {
  QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{index}); const auto exact = p.presentation()->bucketToolTip(index);
  QCOMPARE(p.toolTip(), exact); QCOMPARE(p.accessibleDescription(), exact);
}
class TipHook final : public QObject {
public:
  bool armed = true, consume = false; std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* e) override { if (armed && e->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; } return false; }
};
struct AccessArm { QObject* object; std::function<void()> callback; bool fired = false; };
thread_local AccessArm* activeAccess = nullptr;
void accessibility(QAccessibleEvent* event) {
  auto* arm = activeAccess;
  if (arm && event->object() == arm->object && event->type() == QAccessible::DescriptionChanged) {
    activeAccess = nullptr; arm->fired = true; arm->callback();
  }
}
class AccessScope final {
public:
  AccessArm arm;
  AccessScope(QObject* object, std::function<void()> callback)
      : arm{object, std::move(callback)}, wasActive_(QAccessible::isActive()), previous_(QAccessible::installUpdateHandler(accessibility)) {
    QAccessible::setActive(true); (void)QAccessible::queryAccessibleInterface(object); activeAccess = &arm;
  }
  ~AccessScope() { activeAccess = nullptr; QAccessible::installUpdateHandler(previous_); QAccessible::setActive(wasActive_); }
  AccessScope(const AccessScope&) = delete; AccessScope& operator=(const AccessScope&) = delete;
private:
  bool wasActive_; QAccessible::UpdateHandler previous_;
};
int differences(const QImage& a, const QImage& b, QRectF logical) {
  if (a.size() != b.size() || a.devicePixelRatio() != b.devicePixelRatio()) { return -1; }
  const auto scale = a.devicePixelRatio(); const int x0 = std::clamp(static_cast<int>(std::floor(logical.left() * scale)), 0, a.width());
  const int x1 = std::clamp(static_cast<int>(std::ceil(logical.right() * scale)), 0, a.width()); const int y0 = std::clamp(static_cast<int>(std::floor(logical.top() * scale)), 0, a.height());
  const int y1 = std::clamp(static_cast<int>(std::ceil(logical.bottom() * scale)), 0, a.height()); int result = 0;
  for (int x = x0; x < x1; ++x) { for (int y = y0; y < y1; ++y) { result += a.pixelColor(x, y) != b.pixelColor(x, y); } } return result;
}
int ink(const QImage& image, QRectF logical, QColor background) {
  const auto scale = image.devicePixelRatio(); const int x0 = std::clamp(static_cast<int>(std::floor(logical.left() * scale)), 0, image.width());
  const int x1 = std::clamp(static_cast<int>(std::ceil(logical.right() * scale)), 0, image.width()); const int y0 = std::clamp(static_cast<int>(std::floor(logical.top() * scale)), 0, image.height());
  const int y1 = std::clamp(static_cast<int>(std::ceil(logical.bottom() * scale)), 0, image.height()); int result = 0;
  for (int x = x0; x < x1; ++x) { for (int y = y0; y < y1; ++y) { result += image.pixelColor(x, y) != background; } } return result;
}
} // namespace

class HistoryKeyboardTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); }
  void everyShapeAndTrafficSupportsFirstKeysBoundsAndUnknownBuckets() {
    for (int mode = 0; mode < 3; ++mode) { for (const auto kind : Kinds) {
      ui::SavedHistoryPlot p; frame(p, mode, kind); show(p); QVERIFY(p.hasFocus()); QCOMPARE(p.focusPolicy(), Qt::StrongFocus); QVERIFY(!p.selectedBucket());
      const auto immutable = p.presentation(); const auto last = immutable->buckets().size() - 1;
      for (const int first : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Home}) {
        frame(p, mode, kind); QVERIFY(!p.selectedBucket()); QTest::keyClick(&p, static_cast<Qt::Key>(first)); selection(p, 0); QVERIFY(p.toolTip().contains(QStringLiteral("Unknown")));
      }
      QTest::keyClick(&p, Qt::Key_Left); selection(p, 0); QTest::keyClick(&p, Qt::Key_Right); selection(p, 1);
      QTest::keyClick(&p, Qt::Key_End); selection(p, last); QTest::keyClick(&p, Qt::Key_Right); selection(p, last);
      QTest::keyClick(&p, Qt::Key_Left); selection(p, last - 1); QTest::keyClick(&p, Qt::Key_Home); selection(p, 0);
      for (int i = 0; i < 5; ++i) { QTest::keyClick(&p, Qt::Key_Right); } selection(p, 5); // Never skip missing buckets.
      QCOMPARE(p.presentation()->traffic(), kind); QCOMPARE(p.presentation()->buckets().size(), immutable->buckets().size()); QVERIFY(p.findChildren<QTimer*>().isEmpty());
    } }
  }
  void firstEndSelectsLastAndKnownZeroNeverBecomesUnknown() {
    for (int mode = 0; mode < 3; ++mode) { for (const auto kind : Kinds) {
      ui::SavedHistoryPlot p; frame(p, mode, kind); show(p); const auto last = p.presentation()->buckets().size() - 1;
      QTest::keyClick(&p, Qt::Key_End); selection(p, last); QVERIFY(!p.toolTip().contains(QStringLiteral("Unknown")));
      QTest::keyClick(&p, Qt::Key_Home); const std::size_t zero = mode == 2 ? 24 : 1;
      for (std::size_t i = 0; i < zero; ++i) { QTest::keyClick(&p, Qt::Key_Right); } selection(p, zero);
      QVERIFY(p.toolTip().contains(QStringLiteral("0 bytes"))); QVERIFY(!p.toolTip().contains(QStringLiteral("Unknown")));
    } }
  }
  void exactSignedExtremeDescriptionsIncludeMetricDateAndHour() {
    const std::array<QString, 3> names{QStringLiteral("Payload"), QStringLiteral("Protocol"), QStringLiteral("DHT")};
    for (std::size_t k = 0; k < Kinds.size(); ++k) {
      auto d = day(); core::HistoryCounters raw{}; raw[Down[k]] = std::numeric_limits<std::int64_t>::min(); raw[Up[k]] = std::numeric_limits<std::int64_t>::max();
      d->hours[23] = raw; d->totals = raw; ui::SavedHistoryPlot p; p.setDay(Date, d, Kinds[k]); show(p); QTest::keyClick(&p, Qt::Key_End); selection(p, 23);
      QCOMPARE(p.toolTip(), QStringLiteral("2024-02-29 23:00 UTC\n%1 downloaded: -9223372036854775808 bytes\n%1 uploaded: 9223372036854775807 bytes").arg(names[k]));
      QVERIFY(!p.accessibleDescription().contains(QStringLiteral("bytes/s"))); QVERIFY(!p.toolTip().contains(QLatin1Char('<')));
    }
  }
  void modifiersAndUnrelatedKeysLeaveSelectionAndDescriptionUnchanged() {
    ui::SavedHistoryPlot p; frame(p, 2); show(p); QTest::keyClick(&p, Qt::Key_Right); const auto desc = p.accessibleDescription(), tip = p.toolTip();
    for (const auto modifier : {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier, Qt::MetaModifier, Qt::KeypadModifier}) {
      for (const int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Home, Qt::Key_End}) { press(&p, key, modifier); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{0}); QCOMPARE(p.accessibleDescription(), desc); QCOMPARE(p.toolTip(), tip); }
    }
    for (const int key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_PageUp, Qt::Key_PageDown, Qt::Key_Space, Qt::Key_A}) { press(&p, key); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{0}); QCOMPARE(p.accessibleDescription(), desc); }
  }
  void focusChainIsRealAndFocusLossRetainsCursorWithoutAcceptingKeys() {
    QWidget host; QVBoxLayout layout(&host); QPushButton before(QStringLiteral("Before")), after(QStringLiteral("After")); ui::SavedHistoryPlot p;
    layout.addWidget(&before); layout.addWidget(&p); layout.addWidget(&after); QWidget::setTabOrder(&before, &p); QWidget::setTabOrder(&p, &after);
    frame(p, 0); host.resize(900, 340); host.show(); host.activateWindow(); before.setFocus(); QTRY_VERIFY(before.hasFocus());
    QTest::keyClick(&before, Qt::Key_Tab); QTRY_VERIFY(p.hasFocus()); QTest::keyClick(&p, Qt::Key_Right); selection(p, 0);
    QTest::keyClick(&p, Qt::Key_Tab); QTRY_VERIFY(after.hasFocus()); const auto desc = p.accessibleDescription(); press(&p, Qt::Key_End); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{0}); QCOMPARE(p.accessibleDescription(), desc);
    p.setFocus(); QTRY_VERIFY(p.hasFocus()); QTest::keyClick(&p, Qt::Key_Right); selection(p, 1);
  }
  void noFrameHiddenAndMinimizedWidgetsRejectInspection() {
    ui::SavedHistoryPlot p; show(p); press(&p, Qt::Key_End); QVERIFY(!p.selectedBucket()); frame(p, 1); QTest::keyClick(&p, Qt::Key_Home); selection(p, 0);
    p.hide(); const auto desc = p.accessibleDescription(), tip = p.toolTip(); press(&p, Qt::Key_End); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{0}); QCOMPARE(p.accessibleDescription(), desc); QCOMPARE(p.toolTip(), tip);
    // Offscreen retains a stale focusWindow across hide/show; transfer through an owned window.
    QWidget activationAnchor; activationAnchor.show(); QCoreApplication::processEvents(); activationAnchor.activateWindow(); QCoreApplication::processEvents();
    show(p); QTRY_VERIFY(p.hasFocus()); QTest::keyClick(&p, Qt::Key_End); selection(p, 6); p.showMinimized(); QCoreApplication::processEvents(); QVERIFY(p.isMinimized());
    press(&p, Qt::Key_Home); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{6}); p.showNormal(); QCoreApplication::processEvents(); p.activateWindow(); p.setFocus(Qt::OtherFocusReason); QCoreApplication::processEvents(); QTRY_VERIFY(p.hasFocus()); QTest::keyClick(&p, Qt::Key_Home); selection(p, 0);
  }
  void newAcceptedFrameClearsCursorButInvalidSetterKeepsIt() {
    ui::SavedHistoryPlot p; frame(p, 2, Traffic::Dht); show(p); QTest::keyClick(&p, Qt::Key_End); selection(p, 167); const auto old = p.presentation(); const auto tip = p.toolTip();
    p.setWeekHourly(End, week(), static_cast<Traffic>(99)); QCOMPARE(p.presentation(), old); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{167}); QCOMPARE(p.toolTip(), tip);
    frame(p, 2, Traffic::Dht); QVERIFY(p.presentation() != old); QVERIFY(!p.selectedBucket()); QVERIFY(p.accessibleDescription() != tip);
    QTest::keyClick(&p, Qt::Key_Home); selection(p, 0); frame(p, 1, Traffic::Protocol); QVERIFY(!p.selectedBucket()); QCOMPARE(p.presentation()->traffic(), Traffic::Protocol);
  }
  void mouseHoverIsIndependentOfKeyboardCursorAndNextKeyUsesCursor() {
    ui::SavedHistoryPlot p; frame(p, 2); show(p); QTest::keyClick(&p, Qt::Key_Right); selection(p, 0); const auto desc = p.accessibleDescription();
    hover(p, 167); QCOMPARE(p.toolTip(), p.presentation()->bucketToolTip(167)); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{0}); QCOMPARE(p.accessibleDescription(), desc);
    QTest::keyClick(&p, Qt::Key_Right); selection(p, 1);
  }
  void tooltipObserverNestedKeyOnSameFrameWinsAllProperties() {
    ui::SavedHistoryPlot p; frame(p, 0); show(p); const auto original = p.presentation(); TipHook hook; hook.callback = [&] { press(&p, Qt::Key_Right); }; p.installEventFilter(&hook);
    press(&p, Qt::Key_Right); QVERIFY(!hook.armed); QCOMPARE(p.presentation(), original); selection(p, 1);
  }
  void tooltipObserverNewFrameClearsTheOldInteraction() {
    ui::SavedHistoryPlot p; frame(p, 0); show(p); TipHook hook; hook.callback = [&] { frame(p, 1, Traffic::Protocol, true); }; p.installEventFilter(&hook);
    press(&p, Qt::Key_Right); QVERIFY(!hook.armed); QVERIFY(!p.selectedBucket()); QCOMPARE(p.presentation()->mode(), ui::SavedHistoryPlotMode::Week); QCOMPARE(p.presentation()->traffic(), Traffic::Protocol);
    QVERIFY(p.accessibleDescription() != p.presentation()->bucketToolTip(0));
  }
  void tooltipObserverMayDirectlyDeleteTheReceivingWidget() {
    auto owner = std::make_unique<ui::SavedHistoryPlot>(); frame(*owner, 0); show(*owner); QPointer guard(owner.get()); TipHook hook;
    hook.consume = true; hook.callback = [&] { owner.reset(); }; owner->installEventFilter(&hook); press(owner.get(), Qt::Key_Right); QVERIFY(!hook.armed); QVERIFY(guard.isNull());
  }
  void accessibilityObserverNestedKeyOnSameFrameWins() {
    ui::SavedHistoryPlot p; frame(p, 2, Traffic::Dht); show(p); const auto original = p.presentation(); AccessScope scope(&p, [&] { press(&p, Qt::Key_Right); });
    press(&p, Qt::Key_Right); QVERIFY(scope.arm.fired); QCOMPARE(p.presentation(), original); selection(p, 1);
  }
  void accessibilityObserverNewFrameCannotBeOverwrittenByOldSelection() {
    ui::SavedHistoryPlot p; frame(p, 0); show(p); AccessScope scope(&p, [&] { frame(p, 2, Traffic::Dht, true); });
    press(&p, Qt::Key_Right); QVERIFY(scope.arm.fired); QVERIFY(!p.selectedBucket()); QCOMPARE(p.presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(p.presentation()->traffic(), Traffic::Dht);
    QVERIFY(p.accessibleDescription() != p.presentation()->bucketToolTip(0));
  }
  void accessibilityObserverMayDirectlyDeleteTheReceivingWidget() {
    auto owner = std::make_unique<ui::SavedHistoryPlot>(); frame(*owner, 0); show(*owner); QPointer guard(owner.get()); AccessScope scope(owner.get(), [&] { owner.reset(); });
    press(owner.get(), Qt::Key_Right); QVERIFY(scope.arm.fired); QVERIFY(guard.isNull());
  }
  void selectedGuideIsVisibleOnlyWithFocusAndNarrowPaintRemainsFinite() {
    QWidget host; QVBoxLayout layout(&host); ui::SavedHistoryPlot p; QPushButton next(QStringLiteral("Other focus")); layout.addWidget(&p); layout.addWidget(&next);
    frame(p, 0, Traffic::Protocol, true); auto font = p.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); p.setFont(font);
    host.resize(1000, 320); host.show(); host.activateWindow(); p.setFocus(); QTRY_VERIFY(p.hasFocus());
    const auto unselectedArea = p.plotRect();
    QTest::keyClick(&p, Qt::Key_Home); for (int i = 0; i < 5; ++i) { QTest::keyClick(&p, Qt::Key_Right); } selection(p, 5);
    const auto area = p.plotRect(); const qreal x = area.left() + area.width() * qreal{5} / 23; const QRectF interior{x - 2, area.top() + 8, 4, area.height() - 16};
    QCOMPARE(area.height(), unselectedArea.height() - 24);
    // Isolate the caption's bottom strip from axes and the focus border. Exact
    // text is checked above through both public text properties; these pixels
    // independently prove that inspection also remains visibly present.
    const QRectF caption{8, static_cast<qreal>(p.height() - 23), static_cast<qreal>(p.width() - 16), 21};
    const auto focused = p.grab().toImage(); next.setFocus(); QTRY_VERIFY(next.hasFocus()); const auto unfocused = p.grab().toImage();
    QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{5}); QVERIFY(differences(focused, unfocused, interior) > 5);
    QVERIFY(ink(unfocused, caption, p.palette().base().color()) > 5); QCOMPARE(differences(focused, unfocused, caption), 0);
    p.setFocus(); QTRY_VERIFY(p.hasFocus()); const auto restored = p.grab().toImage(); QVERIFY(differences(restored, unfocused, interior) > 5);
    for (const int width : {80, 180, 1000}) { host.resize(width, 320); QCoreApplication::processEvents(); QVERIFY(!p.grab().isNull()); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{5}); QCOMPARE(p.presentation()->buckets().size(), std::size_t{24}); }
  }
};
QTEST_MAIN(HistoryKeyboardTests)
#include "history_keyboard_tests.moc"
