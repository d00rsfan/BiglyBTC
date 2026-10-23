#include "live_speed_manager_view.hpp"
#include "live_speed_map_widget.hpp"
#include "saved_speed_manager_view.hpp"
#include "saved_speed_map_widget.hpp"
#include "biglybtc/saved_speed_manager.hpp"
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
using Mapper = core::PassiveSpeedMapper;
template<class T> T& child(QObject& owner, const char* name) {
  auto* found = owner.findChild<T*>(QString::fromLatin1(name));
  if (!found) { throw std::runtime_error("Missing live SpeedManager control"); } return *found;
}
void drain() { QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); QCoreApplication::processEvents(); }
void show(QWidget& widget) {
  auto font = widget.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); widget.setFont(font);
  widget.resize(1024, 768); widget.show(); widget.activateWindow(); drain();
}
void observe(Mapper& mapper, std::int64_t time, std::int64_t up, std::int64_t down, std::int64_t rtt) {
  if (!mapper.observe({time, up, down, rtt, false})) { throw std::runtime_error("Actual mapper rejected generated qualified pair"); }
}
void ordinary(Mapper& mapper) {
  observe(mapper, 3000, 51200, 102400, 30); observe(mapper, 6000, 102400, 153600, 50);
  observe(mapper, 9000, 153600, 204800, 70);
}
auto frame(const Mapper& mapper, quint64 sequence = 1, quint64 session = 7, quint64 epoch = 1, bool current = true) {
  auto actual = mapper.snapshot(); if (!actual) { throw std::runtime_error("Actual mapper snapshot failed"); }
  auto packet = std::make_shared<ui::LiveSpeedManagerPacket>(); packet->session = session; packet->epoch = epoch;
  packet->sequence = sequence; packet->current = current;
  packet->snapshot = std::make_shared<const core::PassiveSpeedSnapshot>(std::move(*actual)); return packet;
}
auto unavailable(quint64 sequence = 1, quint64 session = 7) {
  auto packet = std::make_shared<ui::LiveSpeedManagerPacket>(); packet->session = session; packet->sequence = sequence;
  packet->failure = QStringLiteral("<b>no qualified owner</b>"); return packet;
}
auto savedFrame() {
  const auto settings = core::readSavedSpeedSettings("de");
  const auto history = core::readSavedSpeedHistory("d5:pingsld1:mi50e1:xi1e1:yi2eed1:mi150e1:xi3e1:yi4eeee");
  if (!settings || !history) { throw std::runtime_error("Invalid original-format saved fixture"); }
  auto packet = std::make_shared<ui::SavedSpeedManagerPacket>(); packet->session = 7; packet->sequence = 1;
  packet->state = ui::SavedSpeedManagerState::Ready; packet->source = ui::SavedSpeedManagerSource::Primary;
  packet->livePolicy = ui::AutoSpeedPolicy::Inactive;
  packet->settings = std::make_shared<const core::SavedSpeedSettings>(*settings);
  packet->history = std::make_shared<const core::SavedSpeedHistory>(*history); return packet;
}
int coloredNear(const QImage& image, QPointF logical) {
  const auto scale = image.devicePixelRatio(); const auto x = qRound(logical.x() * scale), y = qRound(logical.y() * scale);
  const auto radius = qCeil(5 * scale); int count = 0;
  for (int py = std::max(0, y - radius); py <= std::min(image.height() - 1, y + radius); ++py) {
    for (int px = std::max(0, x - radius); px <= std::min(image.width() - 1, x + radius); ++px) {
      const auto color = image.pixelColor(px, py); if (color.saturation() > 120 && color.value() > 100) { ++count; }
    }
  }
  return count;
}
void optionalCapture(QWidget& widget, const QString& name) {
  const auto directory = qEnvironmentVariable("BIGLYBTC_LIVE_SPEED_CAPTURE_DIRECTORY"); if (directory.isEmpty()) { return; }
  const QFileInfo info(directory); const auto path = info.absoluteFilePath(); struct stat st{};
  if (!info.isAbsolute() || info.isSymLink() || info.canonicalFilePath() != path
      || ::lstat(path.toLocal8Bit().constData(), &st) != 0 || !S_ISDIR(st.st_mode)
      || st.st_uid != ::geteuid() || (st.st_mode & 0077) != 0) { throw std::runtime_error("Capture directory must be an owned canonical private generated directory"); }
  QFile output(path + QLatin1Char('/') + name);
  if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
      || !widget.grab().save(&output, "PNG") || !output.flush()) { throw std::runtime_error("Exclusive optional live Capacity capture failed"); }
}
class Hook final : public QObject {
public:
  std::function<void()> callback;
  bool eventFilter(QObject* object, QEvent* event) override {
    QPointer<QObject> watched(object); if (event->type() == QEvent::ToolTipChange && callback) { auto once = std::move(callback); once(); }
    return watched.isNull();
  }
};
}
class LiveSpeedManagerUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
  }
  void actualMapperPointsAndPolicyLimitsRemainSeparateFromCapacity() {
    Mapper mapper; ordinary(mapper); const auto packet = frame(mapper);
    QVERIFY(packet->snapshot->upload); QVERIFY(packet->snapshot->download);
    ui::LiveSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(packet));
    auto& plot = child<ui::LiveSpeedMapWidget>(view, "live-speed-map"); QVERIFY(plot.snapshot() == packet->snapshot);
    QCOMPARE(plot.snapshot()->history.size(), std::size_t{3});
    QVERIFY(child<QLabel>(view, "live-speed-upload-limit").text().contains(QString::number(packet->snapshot->upload->bytesPerSecond) + QStringLiteral(" bytes/s")));
    QVERIFY(child<QLabel>(view, "live-speed-upload-limit").text().contains(QStringLiteral("policy estimate")));
    QVERIFY(child<QLabel>(view, "live-speed-input").text().contains(QStringLiteral("owner time 9000 ms")));
    QVERIFY(child<QLabel>(view, "live-speed-upload-capacity").text().contains(QStringLiteral("Unavailable")));
    const auto a = plot.point(0), b = plot.point(2); QVERIFY(a); QVERIFY(b);
    QVERIFY(a->x() < b->x()); QVERIFY(a->y() > b->y());
    const auto image = plot.grab().toImage(); QVERIFY(coloredNear(image, *a) > 0); QVERIFY(coloredNear(image, *b) > 0);
    QCOMPARE(child<QLabel>(view, "live-speed-status").textFormat(), Qt::PlainText); QVERIFY(view.findChildren<QTimer*>().isEmpty());
  }
  void absentEmptyAndActualZeroAreDifferent() {
    ui::LiveSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(unavailable()));
    auto& plot = child<ui::LiveSpeedMapWidget>(view, "live-speed-map"); QVERIFY(!plot.snapshot()); QVERIFY(!plot.point(0));
    QVERIFY(child<QLabel>(view, "live-speed-status").text().contains(QStringLiteral("<b>no qualified owner</b>")));
    Mapper empty; QVERIFY(view.setPresentation(frame(empty, 2))); QVERIFY(plot.snapshot()); QVERIFY(plot.snapshot()->history.empty());
    QVERIFY(child<QLabel>(view, "live-speed-status").text().contains(QStringLiteral("waiting for real RTT/rate pairs")));
    Mapper zero; observe(zero, 3000, 0, 0, 0); observe(zero, 6000, 0, 0, 0);
    QVERIFY(view.setPresentation(frame(zero, 3))); QCOMPARE(plot.snapshot()->history.size(), std::size_t{2}); QVERIFY(plot.point(0));
    QCOMPARE(*plot.point(0), plot.plotRect().bottomLeft());
    QVERIFY(plot.pointDescription(0).contains(QStringLiteral("RTT 0 ms")));
  }
  void sustainedRateOnlyCapacityDoesNotInventAnRttOrMapPoint() {
    Mapper mapper;
    for (std::int64_t i = 1; i <= 60; ++i) { QVERIFY(mapper.observeSpeed({i * 3000, 102400, 204800})); }
    const auto packet = frame(mapper); QVERIFY(packet->snapshot->uploadCapacity); QVERIFY(packet->snapshot->downloadCapacity);
    QVERIFY(packet->snapshot->history.empty()); QCOMPARE(packet->snapshot->speedOnlyAccepted, std::uint64_t{60});
    ui::LiveSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(packet));
    QVERIFY(child<QLabel>(view, "live-speed-upload-capacity").text().contains(QStringLiteral("sustained-rate policy")));
    QVERIFY(child<QLabel>(view, "live-speed-input").text().contains(QStringLiteral("rate-only")));
    QVERIFY(child<ui::LiveSpeedMapWidget>(view, "live-speed-map").snapshot()->history.empty());
    QVERIFY(!child<ui::LiveSpeedMapWidget>(view, "live-speed-map").point(0));
  }
  void savedCapacitySeedIsNeverPresentedAsALivePointOrCurrentMeasurement() {
    Mapper mapper;
    QVERIFY(mapper.restoreSaved("d7:downcapd1:hi0e1:m3:0.01:si200000e1:t3:0.81:wi1700000000000ee5:upcapd1:hi0e1:m3:0.01:si100000e1:t3:1.01:wi1700000000000eee"));
    const auto packet = frame(mapper); QVERIFY(packet->snapshot->uploadCapacity); QVERIFY(packet->snapshot->history.empty());
    ui::LiveSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(packet));
    const auto text = child<QLabel>(view, "live-speed-upload-capacity").text();
    QVERIFY(text.contains(QStringLiteral("Saved capacity seed: 100000 bytes/s")));
    QVERIFY(text.contains(QStringLiteral("manual, read-only"))); QVERIFY(text.contains(QStringLiteral("not a current measurement")));
    QVERIFY(child<ui::LiveSpeedMapWidget>(view, "live-speed-map").snapshot()->history.empty());
  }
  void gapAndLastObservedRetainActualGeometryButClearCurrentEstimates() {
    Mapper mapper; ordinary(mapper); const auto first = frame(mapper);
    ui::LiveSpeedManagerView view; QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(first));
    const auto failed = mapper.observe({12000, 204800, 307200, {}, false}); QVERIFY(!failed);
    const auto gap = frame(mapper, 2); QVERIFY(view.setPresentation(gap));
    auto& plot = child<ui::LiveSpeedMapWidget>(view, "live-speed-map"); QCOMPARE(plot.snapshot()->history.size(), std::size_t{3});
    QVERIFY(child<QLabel>(view, "live-speed-status").text().contains(QStringLiteral("missing paired input")));
    QVERIFY(child<QLabel>(view, "live-speed-upload-limit").text().contains(QStringLiteral("Unavailable")));
    QVERIFY(plot.accessibleDescription().contains(QStringLiteral("last observed")));
    auto retained = frame(mapper, 3, 7, 1, false); QVERIFY(view.setPresentation(retained));
    QVERIFY(child<QLabel>(view, "live-speed-status").text().startsWith(QStringLiteral("Last observed")));
    QVERIFY(plot.snapshot() == retained->snapshot); QVERIFY(first->snapshot->history.size() == 3);
  }
  void hiddenMinimizedAndNewSessionKeepTheLatestFrameWithoutOldSourceRetention() {
    Mapper mapper; ordinary(mapper);
    ui::SavedSpeedManagerView parent; QVERIFY(parent.beginSession(7)); show(parent); const auto first = frame(mapper);
    parent.setLivePresentation(first); auto& live = child<ui::LiveSpeedManagerView>(parent, "live-speed-manager-view");
    auto& plot = child<ui::LiveSpeedMapWidget>(parent, "live-speed-map"); QVERIFY(parent.livePresentation() == first); QVERIFY(plot.snapshot());
    const auto before = child<QLabel>(live, "live-speed-status").text(); parent.hide(); drain();
    const auto later = frame(mapper, 2, 7, 1, false); parent.setLivePresentation(later);
    QVERIFY(live.presentation() == later); QCOMPARE(child<QLabel>(live, "live-speed-status").text(), before);
    QVERIFY(parent.beginSession(8)); QVERIFY(!parent.livePresentation()); QVERIFY(!live.presentation()); QVERIFY(!plot.snapshot());
    parent.setLivePresentation(first); QVERIFY(!parent.livePresentation());
    const auto current = frame(mapper, 1, 8); parent.setLivePresentation(current); QVERIFY(parent.livePresentation() == current); QVERIFY(!plot.snapshot());
    show(parent); QTRY_VERIFY(plot.snapshot() == current->snapshot);
    parent.showMinimized(); QTRY_VERIFY(parent.isMinimized()); const auto last = frame(mapper, 2, 8, 1, false);
    parent.setLivePresentation(last); QVERIFY(plot.snapshot() == current->snapshot);
    parent.showNormal(); QTRY_VERIFY(!parent.isMinimized()); QTRY_VERIFY(plot.snapshot() == last->snapshot);
  }
  void staleProfilesEpochsAndSequencesCannotReplaceTheAcceptedSnapshot() {
    Mapper mapper; ordinary(mapper); ui::LiveSpeedManagerView view; QVERIFY(view.beginSession(7));
    const auto accepted = frame(mapper, 5); QVERIFY(view.setPresentation(accepted));
    QVERIFY(!view.setPresentation(accepted)); QVERIFY(!view.setPresentation(frame(mapper, 6, 6)));
    auto future = frame(mapper, 6, 7, 2); QVERIFY(view.setPresentation(future));
    QVERIFY(!view.setPresentation(frame(mapper, 7, 7, 1))); QVERIFY(view.presentation() == future);
    QVERIFY(!view.beginSession(7)); QVERIFY(view.beginSession(8)); QVERIFY(!view.presentation());
    QVERIFY(!view.setPresentation(frame(mapper, 99, 7))); QVERIFY(view.setPresentation(frame(mapper, 1, 8)));
  }
  void rendererHasFiniteGeometryAtNarrowSizesAndSourceRateExtremes() {
    Mapper mapper; observe(mapper, 1, std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max(), 50);
    const auto packet = frame(mapper); ui::LiveSpeedMapWidget plot; show(plot); QVERIFY(plot.setSnapshot(packet->snapshot, true)); plot.setMinimumSize(0, 0);
    for (const auto& size : {QSize(1, 1), QSize(80, 80), QSize(1024, 300)}) {
      plot.resize(size); QVERIFY(!plot.grab().isNull()); const auto p = plot.point(0);
      if (p) { QVERIFY(std::isfinite(p->x())); QVERIFY(std::isfinite(p->y())); QVERIFY(plot.plotRect().contains(*p)); }
    }
  }
  void capacityScrollPreservesSavedControlsAndSupportsOptionalOwnedLayoutCapture() {
    Mapper mapper; ordinary(mapper); ui::SavedSpeedManagerView parent; QSignalSpy reads(&parent, &ui::SavedSpeedManagerView::readRequested);
    QVERIFY(parent.beginSession(7)); show(parent); QTRY_COMPARE(reads.count(), 1);
    const auto saved = savedFrame(); QVERIFY(parent.setPresentation(saved)); const auto live = frame(mapper); parent.setLivePresentation(live);
    auto& scroll = child<QScrollArea>(parent, "capacity-scroll");
    auto& savedMap = child<ui::SavedSpeedMapWidget>(parent, "saved-speed-map");
    QVERIFY(savedMap.presentation() == saved); QVERIFY(parent.livePresentation() == live);
    QVERIFY(child<QLabel>(parent, "saved-speed-records").text().contains(QStringLiteral("2 (not live pings or routes)")));
    QTRY_VERIFY(scroll.verticalScrollBar()->maximum() > 0);
    optionalCapture(parent, QStringLiteral("live-speed-capacity-preview.png"));
    scroll.ensureWidgetVisible(&savedMap); drain(); optionalCapture(parent, QStringLiteral("saved-speed-capacity-preview.png"));
    child<QPushButton>(parent, "saved-speed-refresh").click(); QTRY_COMPARE(reads.count(), 2);
    QVERIFY(parent.findChildren<QTimer*>().isEmpty());
  }
  void renderingCallbacksMayRebindOrDeleteTheOwningCapacityView() {
    Mapper mapper; ordinary(mapper); ui::SavedSpeedManagerView parent; QVERIFY(parent.beginSession(7)); show(parent);
    Hook rebind; bool changed = false; rebind.callback = [&] { changed = true; parent.beginSession(8); parent.setLivePresentation(frame(mapper, 1, 8)); };
    child<QLabel>(parent, "live-speed-upload-limit").installEventFilter(&rebind); parent.setLivePresentation(frame(mapper));
    QVERIFY(changed); QTRY_VERIFY(parent.livePresentation() && parent.livePresentation()->session == 8);
    QTRY_VERIFY(child<ui::LiveSpeedMapWidget>(parent, "live-speed-map").snapshot() == parent.livePresentation()->snapshot);
    QPointer<ui::SavedSpeedManagerView> victim = new ui::SavedSpeedManagerView; QVERIFY(victim->beginSession(7)); show(*victim);
    Hook remove; bool removed = false; remove.callback = [&] { removed = true; delete victim.data(); };
    child<QLabel>(*victim, "live-speed-upload-limit").installEventFilter(&remove); victim->setLivePresentation(frame(mapper));
    QVERIFY(removed); QVERIFY(victim.isNull());
  }
};
QTEST_MAIN(LiveSpeedManagerUiTests)
#include "live_speed_manager_ui_tests.moc"
