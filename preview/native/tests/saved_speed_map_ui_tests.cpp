#include "saved_speed_manager_view.hpp"
#include "saved_speed_map_widget.hpp"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string dict(std::initializer_list<std::pair<std::string, std::string>> fields) {
  std::vector<std::pair<std::string, std::string>> sorted(fields); std::sort(sorted.begin(), sorted.end());
  std::string result = "d"; for (const auto& [key, value] : sorted) { result += bytes(key) + value; }
  return result + 'e';
}
std::string record(std::string x = "i1e", std::string y = "i2e", std::string metric = "i50e") {
  return dict({{"x", x}, {"y", y}, {"m", metric}});
}
std::string history(std::string pings = "l" + record() + record("i3e", "i4e", "i150e") + "e") {
  return dict({{"pings", pings}, {"opaque", bytes("preserve")}});
}
auto packet(std::string raw = history(), quint64 sequence = 1, quint64 session = 7) {
  auto settings = core::readSavedSpeedSettings("de"); auto source = core::readSavedSpeedHistory(raw);
  if (!settings || !source) { throw std::runtime_error("Invalid original-format generated SpeedManager fixture"); }
  auto result = std::make_shared<ui::SavedSpeedManagerPacket>(); result->session = session; result->sequence = sequence;
  result->state = ui::SavedSpeedManagerState::Ready; result->livePolicy = ui::AutoSpeedPolicy::Inactive;
  result->source = ui::SavedSpeedManagerSource::Primary;
  result->settings = std::make_shared<const core::SavedSpeedSettings>(std::move(*settings));
  result->history = std::make_shared<const core::SavedSpeedHistory>(std::move(*source)); return result;
}
template<class T> T& child(QObject& owner, const char* name) {
  auto* value = owner.findChild<T*>(QString::fromLatin1(name));
  if (!value) { throw std::runtime_error("Missing saved map widget"); } return *value;
}
void drain() { QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); QCoreApplication::processEvents(); }
void show(QWidget& widget) {
  auto font = widget.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); widget.setFont(font);
  widget.resize(1100, 900); widget.show(); widget.activateWindow(); drain();
}
int coloredNear(const QImage& image, QPointF logical) {
  const auto dpr = image.devicePixelRatio(); const int cx = qRound(logical.x() * dpr), cy = qRound(logical.y() * dpr);
  const int radius = qCeil(5 * dpr); int count = 0;
  for (int y = std::max(0, cy - radius); y <= std::min(image.height() - 1, cy + radius); ++y) {
    for (int x = std::max(0, cx - radius); x <= std::min(image.width() - 1, cx + radius); ++x) {
      const auto c = image.pixelColor(x, y); if (c.saturation() > 120 && c.value() > 100) { ++count; }
    }
  }
  return count;
}
class Hook final : public QObject {
public:
  std::function<void()> callback;
  bool eventFilter(QObject* object, QEvent* event) override {
    QPointer<QObject> watched(object);
    if (event->type() == QEvent::ToolTipChange && callback) { auto once = std::move(callback); once(); }
    return watched.isNull();
  }
};
}

class SavedSpeedMapUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void actualFormatProjectsSourceUnitsAndKeepsTheImmutableDocument() {
    std::string raw = history(); const auto original = raw; const auto frame = packet(raw); raw = "de";
    ui::SavedSpeedMapWidget map; show(map); QVERIFY(map.setPresentation(frame, 7));
    QVERIFY(map.map()); QVERIFY(map.map()->source == frame->history->source);
    QCOMPARE(map.map()->source->originalBytes(), original);
    QCOMPARE(map.map()->records.size(), std::size_t{2});
    QCOMPARE(map.map()->records[0].point->uploadBytesPerSecond, std::uint32_t{256});
    QCOMPARE(map.map()->records[0].point->downloadBytesPerSecond, std::uint32_t{512});
    QCOMPARE(map.map()->records[0].point->metric, std::uint16_t{50});
    const auto first = map.point(0), last = map.point(1); QVERIFY(first); QVERIFY(last);
    QVERIFY(map.plotRect().contains(*first)); QVERIFY(map.plotRect().contains(*last));
    QVERIFY(first->x() < last->x()); QVERIFY(first->y() > last->y());
    QVERIFY(map.accessibleDescription().contains(QStringLiteral("not raw RTT or time")));
    QVERIFY(map.findChildren<QTimer*>().isEmpty());
  }
  void emptyMissingMalformedAndPartialHaveDifferentSavedMeaning_data() {
    QTest::addColumn<QString>("raw"); QTest::addColumn<QString>("status"); QTest::addColumn<int>("validPoints");
    QTest::newRow("missing") << QStringLiteral("de") << QStringLiteral("not recorded") << 0;
    QTest::newRow("malformed-list") << QString::fromStdString(history("i7e")) << QStringLiteral("list is malformed") << 0;
    QTest::newRow("known-empty") << QString::fromStdString(history("le")) << QStringLiteral("Known empty") << 0;
    QTest::newRow("known-zero") << QString::fromStdString(history("l" + record("i0e", "i0e", "i0e") + "e")) << QStringLiteral("1 valid records") << 1;
    QTest::newRow("partial") << QString::fromStdString(history("l" + record() + "i7e" + record("i3e") + "e")) << QStringLiteral("partial: 1 malformed") << 2;
  }
  void emptyMissingMalformedAndPartialHaveDifferentSavedMeaning() {
    QFETCH(QString, raw); QFETCH(QString, status); QFETCH(int, validPoints);
    ui::SavedSpeedMapWidget map; show(map); QVERIFY(map.setPresentation(packet(raw.toStdString()), 7));
    QVERIFY(map.map()); QVERIFY(map.accessibleDescription().contains(status));
    int actual = 0; for (std::size_t i = 0; i < map.map()->records.size(); ++i) { if (map.point(i)) { ++actual; } }
    QCOMPARE(actual, validPoints);
    if (validPoints == 1) { QCOMPARE(map.point(0), std::optional<QPointF>(map.plotRect().bottomLeft())); }
    if (validPoints == 2) { QVERIFY(!map.point(1)); QVERIFY(map.recordDescription(1).contains(QStringLiteral("malformed fields"))); }
  }
  void narrowSizesAndJavaWidthExtremesAlwaysHaveFiniteGeometry() {
    auto raw = history("l" + record("i-1e", "i4294967297e", "i9223372036854775807e") + record("i0e", "i0e", "i-1e") + "e");
    ui::SavedSpeedMapWidget map; show(map); QVERIFY(map.setPresentation(packet(raw), 7));
    QCOMPARE(map.map()->records[0].point->uploadBytesPerSecond, std::uint32_t{16776960});
    QCOMPARE(map.map()->records[0].point->downloadBytesPerSecond, std::uint32_t{256});
    QVERIFY(map.recordDescription(0).contains(QStringLiteral("4294967297")));
    QCOMPARE(map.map()->records[1].point->metric, std::uint16_t{65535});
    map.setMinimumSize(0, 0);
    for (const auto& size : {QSize(1, 1), QSize(80, 70), QSize(240, 220), QSize(1200, 700)}) {
      map.resize(size); const auto image = map.grab().toImage(); QVERIFY(!image.isNull());
      for (std::size_t i = 0; i < 2; ++i) { const auto p = map.point(i);
        if (p) { QVERIFY(std::isfinite(p->x())); QVERIFY(std::isfinite(p->y())); QVERIFY(map.plotRect().contains(*p)); }
      }
    }
  }
  void painterMarksAreAtTheActualProjectedPointsAndNeverJoinTheGap() {
    ui::SavedSpeedMapWidget map; show(map);
    const auto frame = packet(history("l" + record("i1e", "i1e", "i0e") + "i7e" + record("i4e", "i4e", "i1500e") + "e"));
    QVERIFY(map.setPresentation(frame, 7)); const auto a = map.point(0), b = map.point(2); QVERIFY(a); QVERIFY(b);
    const auto image = map.grab().toImage(); QVERIFY(coloredNear(image, *a) > 0); QVERIFY(coloredNear(image, *b) > 0);
    QCOMPARE(coloredNear(image, (*a + *b) / 2), 0); // No route/time line is inferred across records.
  }
  void keyboardInspectsExactRawValuesIncludingMalformedSlotsWithoutReading() {
    ui::SavedSpeedMapWidget map; show(map);
    const auto frame = packet(history("l" + record("i4294967297e", "i2e", "i2000e") + "i7e" + record() + "e"));
    QVERIFY(map.setPresentation(frame, 7)); map.setFocus(); QTRY_VERIFY(map.hasFocus());
    QTest::keyClick(&map, Qt::Key_Home); QCOMPARE(map.selectedRecord(), std::optional<std::size_t>{0});
    QVERIFY(map.toolTip().contains(QStringLiteral("upload 256 bytes/s; download 512 bytes/s; saved latency metric 1500")));
    QVERIFY(map.toolTip().contains(QStringLiteral("Raw saved x 4294967297 / y 2 / metric 2000")));
    QVERIFY(map.toolTip().contains(QStringLiteral("source metric upper-clamped")));
    QTest::keyClick(&map, Qt::Key_Right); QCOMPARE(map.selectedRecord(), std::optional<std::size_t>{1});
    QVERIFY(map.toolTip().contains(QStringLiteral("malformed fields"))); QVERIFY(!map.point(1));
    QTest::keyClick(&map, Qt::Key_End); QCOMPARE(map.selectedRecord(), std::optional<std::size_t>{2});
    QTest::keyClick(&map, Qt::Key_Right); QCOMPARE(map.selectedRecord(), std::optional<std::size_t>{2});
    QTest::keyClick(&map, Qt::Key_Home, Qt::ControlModifier); QCOMPARE(map.selectedRecord(), std::optional<std::size_t>{2});
    Hook nested; nested.callback = [&] { QTest::keyClick(&map, Qt::Key_Home); };
    map.installEventFilter(&nested); QTest::keyClick(&map, Qt::Key_Left);
    QCOMPARE(map.selectedRecord(), std::optional<std::size_t>{0});
    QTRY_VERIFY(map.accessibleDescription().contains(QStringLiteral("Saved record 1:")));
    QVERIFY(map.setPresentation(packet(history(), 2), 7)); QVERIFY(!map.selectedRecord());
    QVERIFY(map.presentation()->history->source->originalBytes() == history());
  }
  void summaryLabelsAndLazyRefreshRemainUnchangedAndMapFollowsVisibility() {
    ui::SavedSpeedManagerView view; QSignalSpy reads(&view, &ui::SavedSpeedManagerView::readRequested);
    QVERIFY(view.beginSession(7)); show(view); QTRY_COMPARE(reads.count(), 1);
    const auto frame = packet(); QVERIFY(view.setPresentation(frame));
    auto& map = child<ui::SavedSpeedMapWidget>(view, "saved-speed-map"); QVERIFY(map.presentation() == frame);
    QVERIFY(child<QLabel>(view, "saved-speed-records").text().contains(QStringLiteral("2 (not live pings or routes)")));
    QVERIFY(child<QLabel>(view, "saved-autospeed-policy").text().contains(QStringLiteral("inactive by native host policy")));
    view.hide(); const auto newer = packet(history("le"), 2); QVERIFY(view.setPresentation(newer));
    QCOMPARE(reads.count(), 1); QVERIFY(map.presentation() == frame); show(view);
    QTRY_VERIFY(map.presentation() == newer); QVERIFY(map.accessibleDescription().contains(QStringLiteral("Known empty")));
    view.showMinimized(); QTRY_VERIFY(view.isMinimized()); const auto last = packet(history(), 3);
    QVERIFY(view.setPresentation(last)); drain(); QVERIFY(map.presentation() == newer);
    view.showNormal(); QTRY_VERIFY(!view.isMinimized()); QTRY_VERIFY(map.presentation() == last);
    child<QPushButton>(view, "saved-speed-refresh").click(); QTRY_COMPARE(reads.count(), 2);
    QVERIFY(view.findChildren<QTimer*>().isEmpty());
  }
  void stalePacketsAndNewSessionCannotRestoreOldSavedGeometry() {
    ui::SavedSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view);
    const auto frame = packet(); QVERIFY(view.setPresentation(frame));
    auto& map = child<ui::SavedSpeedMapWidget>(view, "saved-speed-map"); QVERIFY(map.map());
    QVERIFY(!view.setPresentation(frame)); QVERIFY(!view.setPresentation(packet(history(), 2, 6)));
    QVERIFY(view.beginSession(8)); QVERIFY(!map.presentation()); QVERIFY(!map.map());
    QVERIFY(!view.setPresentation(packet(history(), 99, 7))); QVERIFY(!map.presentation());
    QVERIFY(view.setPresentation(packet(history("le"), 1, 8))); QVERIFY(map.map()->records.empty());
    std::string over = "l"; for (std::size_t i = 0; i <= core::SavedSpeedMapMaximumRecords; ++i) { over += record(); } over += 'e';
    QVERIFY(view.setPresentation(packet(history(over), 2, 8))); QVERIFY(!map.map());
    QTRY_VERIFY(map.accessibleDescription().contains(QStringLiteral("projection limit")));
    QVERIFY(child<QLabel>(view, "saved-speed-records").text().contains(QStringLiteral("1201")));
  }
  void hiddenProfileChangeImmediatelyReleasesTheOldMapAndRejectsItsPacket() {
    ui::SavedSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view);
    const auto old = packet(history()); QVERIFY(view.setPresentation(old));
    auto& map = child<ui::SavedSpeedMapWidget>(view, "saved-speed-map");
    QVERIFY(map.presentation() == old); QVERIFY(map.map());
    const auto summary = child<QLabel>(view, "saved-speed-records").text();
    view.hide(); drain(); QSignalSpy reads(&view, &ui::SavedSpeedManagerView::readRequested);
    QVERIFY(view.beginSession(8)); QVERIFY(!view.presentation());
    QVERIFY(!map.presentation()); QVERIFY(!map.map()); QVERIFY(!map.selectedRecord());
    QCOMPARE(child<QLabel>(view, "saved-speed-records").text(), summary); // Existing hidden summary deferral remains.
    QVERIFY(!view.setPresentation(old)); QVERIFY(!view.setPresentation(packet(history(), 99, 7)));
    QVERIFY(!map.presentation()); QVERIFY(!map.map()); drain(); QCOMPARE(reads.count(), 0);
    show(view); QTRY_COMPARE(reads.count(), 1); QVERIFY(!map.presentation()); QVERIFY(!map.map());
    const auto fresh = packet(history("le"), 1, 8); QVERIFY(view.setPresentation(fresh));
    QVERIFY(map.presentation() == fresh); QVERIFY(map.map()); QVERIFY(map.map()->records.empty());
    QVERIFY(!view.setPresentation(old)); QVERIFY(map.presentation() == fresh);
  }
  void mapTooltipRebindPreventsAnyOldFramePublication() {
    ui::SavedSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view);
    auto& map = child<ui::SavedSpeedMapWidget>(view, "saved-speed-map");
    Hook rebind; rebind.callback = [&] { QVERIFY(view.beginSession(8)); QVERIFY(view.setPresentation(packet(history("le"), 1, 8))); };
    map.installEventFilter(&rebind); QVERIFY(view.setPresentation(packet()));
    QTRY_VERIFY(map.presentation() && map.presentation()->session == 8);
    QTRY_VERIFY(map.accessibleDescription().contains(QStringLiteral("Known empty")));
    QVERIFY(map.map()->records.empty());
  }
  void mapTooltipMayDeleteOwningView() {
    QPointer<ui::SavedSpeedManagerView> view = new ui::SavedSpeedManagerView;
    QVERIFY(view->beginSession(7)); show(*view);
    auto& map = child<ui::SavedSpeedMapWidget>(*view, "saved-speed-map");
    Hook remove; bool removed = false; remove.callback = [&] { removed = true; delete view.data(); };
    map.installEventFilter(&remove); view->setPresentation(packet()); QVERIFY(removed); QVERIFY(view.isNull());
  }
};
QTEST_MAIN(SavedSpeedMapUiTests)
#include "saved_speed_map_ui_tests.moc"
