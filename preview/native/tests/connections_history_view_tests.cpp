#include "connections_history_view.hpp"
#include "runtime_metrics_view.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPointer>
#include <QSignalSpy>
#include <QTableView>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
using namespace biglybtc;
using G = ui::ConnectionGauge;
constexpr std::size_t ix(G field) { return static_cast<std::size_t>(field); }
template<class T> T& child(QObject& parent, const char* id) {
  auto* result = parent.findChild<T*>(QString::fromLatin1(id));
  if (!result) { throw std::runtime_error("Missing Connections control"); }
  return *result;
}
auto source(std::uint64_t sequence = 1, std::int64_t nanoseconds = 0) {
  auto result = std::make_shared<core::RuntimeMetrics>();
  result->sequence = sequence; result->ownerElapsedNanoseconds = nanoseconds;
  using Type = core::RuntimeMetricType; using Group = core::RuntimeMetricGroup;
  result->metrics = {
    {"peer.num_peers_connected",Type::Gauge,Group::Peer,6},
    {"peer.num_peers_half_open",Type::Gauge,Group::Peer,10},
    {"peer.num_peers_up_unchoked_all",Type::Gauge,Group::Peer,3},
    {"peer.num_peers_up_interested",Type::Gauge,Group::Peer,4},
    {"peer.num_peers_down_interested",Type::Gauge,Group::Peer,0},
    {"peer.num_peers_up_requests",Type::Gauge,Group::Peer,2},
    {"peer.num_peers_down_requests",Type::Gauge,Group::Peer,5},
    {"net.limiter_up_queue",Type::Gauge,Group::Net,7},
    {"net.limiter_down_queue",Type::Gauge,Group::Net,8}};
  return result;
}
auto packet(quint64 sequence = 1, quint64 epoch = 1, quint64 session = 7) {
  auto result = std::make_shared<ui::RuntimeMetricsPacket>();
  result->session = session; result->epoch = epoch; result->sequence = sequence;
  result->snapshot = source(sequence,static_cast<std::int64_t>(sequence-1)*1'000'000'000);
  return result;
}
ui::ConnectionsHistoryView& history(QObject& view) { return child<ui::ConnectionsHistoryView>(view,"connections-history"); }
QWidget& plot(QObject& view) { return child<QWidget>(view,"connections-history-plot"); }
QLabel& interested(QObject& view) { return child<QLabel>(view,"runtime-peer-interested"); }
QComboBox& mode(QObject& view) { return child<QComboBox>(view,"connections-history-mode"); }
QTableView& table(QObject& view) { return child<QTableView>(view,"runtime-metrics-table"); }
void show(QWidget& view) {
  auto font = view.font(); font.setStyleStrategy(QFont::NoSubpixelAntialias); view.setFont(font);
  view.resize(1024,768); view.show(); QCoreApplication::processEvents();
}
class Hook final : public QObject {
public:
  bool armed = true, consume = false;
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (armed && event->type() == QEvent::ToolTipChange) { armed = false; callback(); return consume; }
    return false;
  }
};
class PaintCounter final : public QObject {
public:
  int paints = 0;
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::Paint) { ++paints; }
    return false;
  }
};
int bluePixels(const QImage& image, QRectF logical) {
  const qreal scale = image.devicePixelRatio();
  const int left = std::clamp(static_cast<int>(std::floor(logical.left()*scale)),0,image.width());
  const int right = std::clamp(static_cast<int>(std::ceil(logical.right()*scale)),0,image.width());
  const int top = std::clamp(static_cast<int>(std::floor(logical.top()*scale)),0,image.height());
  const int bottom = std::clamp(static_cast<int>(std::ceil(logical.bottom()*scale)),0,image.height());
  int found = 0;
  for (int y = top; y < bottom; ++y) { for (int x = left; x < right; ++x) {
    const auto pixel = image.pixelColor(x,y);
    if (std::abs(pixel.red()-30)<30 && std::abs(pixel.green()-100)<30 && std::abs(pixel.blue()-210)<30) { ++found; }
  } }
  return found;
}
}

class ConnectionsHistoryViewTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void projectionUsesExactGaugeNamesAndPreservesZeroSignedValuesAndOwnership() {
    auto input = source();
    input->metrics[0].value = std::numeric_limits<std::int64_t>::max();
    input->metrics[7].value = std::numeric_limits<std::int64_t>::min();
    const auto values = ui::projectConnectionValues(input.get());
    const ui::ConnectionValues expected{std::numeric_limits<std::int64_t>::max(),10,3,4,0,2,5,
      std::numeric_limits<std::int64_t>::min(),8};
    QCOMPARE(values,expected);
    input->metrics.clear(); QCOMPARE(values,expected);
    QCOMPARE(ui::projectConnectionValues(nullptr),ui::ConnectionValues{});
  }
  void wrongTypeGroupMissingAndDuplicateFieldsAreUnknownIndependently() {
    auto input = source(); input->metrics[0].type = core::RuntimeMetricType::Counter;
    input->metrics[1].group = core::RuntimeMetricGroup::Other;
    input->metrics.push_back(input->metrics[3]);
    input->metrics[4].name += ".different";
    const auto values = ui::projectConnectionValues(input.get());
    QVERIFY(!values[ix(G::Connected)]); QVERIFY(!values[ix(G::Connecting)]);
    QVERIFY(!values[ix(G::InterestedInUs)]); QVERIFY(!values[ix(G::InterestedInPeers)]);
    QVERIFY(values[ix(G::Unchoked)] == 3); QVERIFY(values[ix(G::SendWaitingSockets)] == 7);
    input->metrics.resize(core::MaximumRuntimeMetrics+1);
    QCOMPARE(ui::projectConnectionValues(input.get()),ui::ConnectionValues{});
  }
  void gaugesMayDecreaseButIdentityAndClockRegressionDoNotAppend() {
    ui::ConnectionHistory values; auto first = source(1,10); QVERIFY(values.observe(*first));
    auto second = source(2,20); second->metrics[0].value = 1; QVERIFY(values.observe(*second));
    auto frame = values.frame(); QCOMPARE(frame.count,std::size_t{2}); QVERIFY(frame.points[1].connectedToPrevious);
    QVERIFY(frame.points[1].values[ix(G::Connected)] == 1);
    QVERIFY(!values.observe(*second)); auto backwards = source(3,19); QVERIFY(!values.observe(*backwards));
    backwards->ownerElapsedNanoseconds = -1; QVERIFY(!values.observe(*backwards));
    backwards->sequence = 0; QVERIFY(!values.observe(*backwards)); QCOMPARE(values.frame().count,std::size_t{2});
    auto equalTime = source(3,20); QVERIFY(values.observe(*equalTime)); QVERIFY(!values.frame().points[2].connectedToPrevious);
    auto later = source(4,21); QVERIFY(values.observe(*later)); QVERIFY(values.frame().points[3].connectedToPrevious);
    auto skipped = source(6,22); QVERIFY(values.observe(*skipped)); QVERIFY(!values.frame().points[4].connectedToPrevious);
  }
  void missingObservationsLeaveGapsAndTheRingKeepsOnly600PrimitiveSamples() {
    ui::ConnectionHistory values;
    for (std::uint64_t sequence = 1; sequence <= 605; ++sequence) {
      auto input = source(sequence,static_cast<std::int64_t>(sequence));
      input->metrics[0].value = static_cast<std::int64_t>(sequence);
      if (sequence == 603) { input->metrics.clear(); }
      if (sequence == 604) { values.gap(); }
      QVERIFY(values.observe(*input));
    }
    const auto frame = values.frame(); QCOMPARE(frame.count,std::size_t{600});
    QCOMPARE(frame.points[0].sequence,std::uint64_t{6}); QVERIFY(!frame.points[0].connectedToPrevious);
    QCOMPARE(frame.points[599].sequence,std::uint64_t{605});
    QVERIFY(!frame.points[597].values[ix(G::Connected)]); QVERIFY(!frame.points[598].connectedToPrevious);
    QVERIFY(frame.points[599].connectedToPrevious); values.clear(); QCOMPARE(values.frame().count,std::size_t{0});
    QVERIFY(frame.points[599].values[ix(G::Connected)] == 605);
  }
  void peerViewAddsInterestedFactsAndHistoryWhileRetainingRawSearchAndNumericSort() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    QVERIFY(view.setPresentation(packet()));
    QCOMPARE(interested(view).text(),QStringLiteral("Interested peers (in us / we are interested): 4 / 0"));
    QCOMPARE(interested(view).textFormat(),Qt::PlainText);
    QVERIFY(interested(view).toolTip().contains(QStringLiteral("peer.num_peers_up_interested")));
    QCOMPARE(history(view).activity().count,std::size_t{1}); QCOMPARE(mode(view).count(),3);
    QCOMPARE(mode(view).currentIndex(),0); QCOMPARE(table(view).model()->rowCount(),7);
    table(view).sortByColumn(1,Qt::AscendingOrder);
    QCOMPARE(table(view).model()->index(0,1).data(Qt::UserRole).toLongLong(),qint64{0});
    child<QLineEdit>(view,"runtime-metrics-filter").setText(QStringLiteral("interested"));
    QTRY_COMPARE(table(view).model()->rowCount(),2);
    QCOMPARE(history(view).activity().count,std::size_t{1});
    QVERIFY(view.findChildren<QTimer*>().isEmpty());
    QVERIFY(view.minimumSizeHint().height() <= 768);
    const auto capture=qEnvironmentVariable("BIGLYBTC_CONNECTIONS_CAPTURE_DIRECTORY");
    if(!capture.isEmpty()){QVERIFY(view.grab().save(capture+QStringLiteral("/connections-history-policy-fixture.png")));}
  }
  void otherScopesDoNotAcquireAConnectionHistory() {
    for (const auto group : {core::RuntimeMetricGroup::Disk,core::RuntimeMetricGroup::Tracker,
        core::RuntimeMetricGroup::Net,core::RuntimeMetricGroup::Dht}) {
      ui::RuntimeMetricsView view(group); QVERIFY(view.beginSession(7)); show(view); QVERIFY(view.setPresentation(packet()));
      QVERIFY(!view.findChild<ui::ConnectionsHistoryView*>());
    }
    ui::RuntimeMetricsView all; QVERIFY(!all.findChild<ui::ConnectionsHistoryView*>());
  }
  void terminalDuplicateRetainsObservedHistoryAndMissingSnapshotDoesNotInventZero() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    const auto first = packet(); QVERIFY(view.setPresentation(first));
    auto ended = std::make_shared<ui::RuntimeMetricsPacket>(*first); ended->sequence = 2; ended->current = false;
    QVERIFY(view.setPresentation(ended)); QCOMPARE(history(view).activity().count,std::size_t{1});
    QVERIFY(plot(view).toolTip().startsWith(QStringLiteral("Last observed")));
    auto absent = std::make_shared<ui::RuntimeMetricsPacket>(*ended); absent->sequence = 3; absent->snapshot.reset();
    QVERIFY(view.setPresentation(absent)); QCOMPARE(history(view).activity().count,std::size_t{1});
    QVERIFY(interested(view).text().endsWith(QStringLiteral("Unavailable / Unavailable")));
    auto next = packet(4); QVERIFY(view.setPresentation(next));
    const auto frame = history(view).activity(); QCOMPARE(frame.count,std::size_t{2}); QVERIFY(!frame.points[1].connectedToPrevious);
  }
  void profileEpochAndStalePacketFencesResetOrRetainTheCorrectHistory() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    QVERIFY(view.setPresentation(packet(2)));
    QVERIFY(!view.setPresentation(packet(3,1,8))); QVERIFY(!view.setPresentation(packet(1)));
    auto earlier = packet(3); auto facts = source(3,-1); earlier->snapshot = facts;
    QVERIFY(!view.setPresentation(earlier)); QCOMPARE(history(view).activity().count,std::size_t{1});
    auto epoch = packet(1,2); QVERIFY(view.setPresentation(epoch)); QCOMPARE(history(view).activity().count,std::size_t{1});
    QCOMPARE(history(view).activity().points[0].sequence,std::uint64_t{1});
    QVERIFY(!view.setPresentation(packet(9,1))); QVERIFY(view.beginSession(8)); QCOMPARE(history(view).activity().count,std::size_t{0});
    QVERIFY(interested(view).text().endsWith(QStringLiteral("Unavailable / Unavailable")));
    QVERIFY(!view.setPresentation(packet(10,2,7))); QVERIFY(view.setPresentation(packet(1,1,8)));
    QVERIFY(!view.beginSession(0)); QVERIFY(!view.beginSession(8));
  }
  void hiddenAndMinimizedViewsKeepActualHistoryWithoutPublishingLabelsOrPainting() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    QVERIFY(view.setPresentation(packet())); const auto text = interested(view).text();
    view.hide(); auto next = packet(2); auto facts = source(2,1'000'000'000); facts->metrics[3].value = 22; next->snapshot = facts;
    QVERIFY(view.setPresentation(next)); QCoreApplication::processEvents();
    QCOMPARE(interested(view).text(),text); QCOMPARE(history(view).activity().count,std::size_t{2});
    view.show(); QTRY_VERIFY(interested(view).text().contains(QStringLiteral("22 / 0")));
    view.showMinimized(); QTRY_VERIFY(view.isMinimized()); QCoreApplication::processEvents();
    PaintCounter paint; plot(view).installEventFilter(&paint); const auto tip = plot(view).toolTip();
    QVERIFY(view.setPresentation(packet(3))); QCoreApplication::processEvents();
    QCOMPARE(paint.paints,0); QCOMPARE(plot(view).toolTip(),tip); QVERIFY(interested(view).text().contains(QStringLiteral("22 / 0")));
    QCOMPARE(history(view).activity().count,std::size_t{3}); view.showNormal();
    QTRY_COMPARE(interested(view).text(),text); QTRY_VERIFY(plot(view).toolTip().contains(QStringLiteral("response 3")));
  }
  void changingChartIsQueuedAndDoesNotResetRawSelectionOrAddObservations() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    QVERIFY(view.setPresentation(packet())); QCoreApplication::processEvents();
    table(view).setCurrentIndex(table(view).model()->index(2,0)); const auto selected = table(view).currentIndex().data().toString();
    QSignalSpy resets(table(view).model(),&QAbstractItemModel::modelReset);
    const auto previous = plot(view).accessibleDescription(); mode(view).setCurrentIndex(1); mode(view).setCurrentIndex(2);
    QCOMPARE(plot(view).accessibleDescription(),previous);
    QTRY_VERIFY(plot(view).accessibleDescription().contains(QStringLiteral("Send: 7")));
    QVERIFY(plot(view).accessibleDescription().contains(QStringLiteral("Receive: 8")));
    QCOMPARE(resets.count(),0); QCOMPARE(table(view).currentIndex().data().toString(),selected);
    QCOMPARE(history(view).activity().count,std::size_t{1});
  }
  void actualStrokesJoinKnownObservationsAndLeaveMissingIntervalsEmpty() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    for (quint64 sequence = 1; sequence <= 5; ++sequence) {
      auto input = packet(sequence); if (sequence == 3) {
        auto empty = source(sequence,2'000'000'000); empty->metrics.clear(); input->snapshot = empty;
      }
      QVERIFY(view.setPresentation(input));
    }
    QCoreApplication::processEvents(); const auto image = plot(view).grab().toImage();
    const QRectF area = QRectF(plot(view).rect()).adjusted(64,38,-16,-31); QVERIFY(area.width() > 200);
    // Midpoints are far from both actual markers; a marker alone cannot pass.
    const QRectF connected{area.left()+area.width()/8-8,area.top(),16,area.height()};
    const QRectF missing{area.left()+area.width()/2-8,area.top(),16,area.height()};
    QVERIFY2(bluePixels(image,connected)>5,"No interior connected-peer stroke");
    QCOMPARE(bluePixels(image,missing),0);
  }
  void signedExtremaRemainExactAndRenderAtExtremeOwnerTimes() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    auto input = packet(); auto first = source(1,std::numeric_limits<std::int64_t>::max()-1);
    first->metrics[0].value = std::numeric_limits<std::int64_t>::min(); input->snapshot = first;
    QVERIFY(view.setPresentation(input)); auto next = packet(2);
    auto second = source(2,std::numeric_limits<std::int64_t>::max()); second->metrics[0].value = std::numeric_limits<std::int64_t>::max(); next->snapshot = second;
    QVERIFY(view.setPresentation(next)); const auto frame = history(view).activity();
    QVERIFY(frame.points[0].values[ix(G::Connected)] == std::numeric_limits<std::int64_t>::min());
    QVERIFY(frame.points[1].values[ix(G::Connected)] == std::numeric_limits<std::int64_t>::max());
    QVERIFY(!plot(view).grab().isNull()); QVERIFY(plot(view).accessibleDescription().contains(QString::number(std::numeric_limits<std::int64_t>::max())));
  }
  void modelResetReentryKeepsLatestHistoryAndNarration() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    bool once = true;
    connect(table(view).model(),&QAbstractItemModel::modelAboutToBeReset,&view,[&] {
      if (once) { once = false; QVERIFY(view.setPresentation(packet(2))); }
    });
    QVERIFY(view.setPresentation(packet())); QTRY_COMPARE(view.presentation()->sequence,quint64{2});
    QTRY_VERIFY(plot(view).toolTip().contains(QStringLiteral("response 2")));
    QCOMPARE(history(view).activity().count,std::size_t{2});
  }
  void interestedTooltipReentryCannotPublishAnOlderHistoryFrame() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    QVERIFY(view.setPresentation(packet())); Hook hook;
    hook.callback = [&] { QVERIFY(view.setPresentation(packet(3))); };
    interested(view).installEventFilter(&hook);
    auto input = packet(2); auto facts = source(2,1'000'000'000); facts->metrics[3].value = 99; input->snapshot = facts;
    QVERIFY(view.setPresentation(input)); QVERIFY(!hook.armed);
    QTRY_VERIFY(plot(view).toolTip().contains(QStringLiteral("response 3")));
    QTRY_COMPARE(interested(view).text(),QStringLiteral("Interested peers (in us / we are interested): 4 / 0"));
    QCOMPARE(history(view).activity().count,std::size_t{3});
  }
  void chartTooltipMayDeleteItsOwnerAfterTheComboSetterReturns() {
    auto* owner = new ui::RuntimeMetricsView(core::RuntimeMetricGroup::Peer); QPointer<ui::RuntimeMetricsView> guard(owner);
    QVERIFY(owner->beginSession(7)); show(*owner); QVERIFY(owner->setPresentation(packet()));
    Hook hook; hook.consume = true; hook.callback = [&] { delete owner; };
    plot(*owner).installEventFilter(&hook); mode(*owner).setCurrentIndex(1);
    QVERIFY(guard); QTRY_VERIFY(guard.isNull()); QVERIFY(!hook.armed);
  }
  void tooltipMayDeleteOnlyThePlotWithoutTrailingChildAccess() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    QVERIFY(view.setPresentation(packet())); QPointer<QWidget> target(&plot(view)); Hook hook; hook.consume = true;
    hook.callback = [&] { delete target.data(); };
    target->installEventFilter(&hook); mode(view).setCurrentIndex(2);
    QTRY_VERIFY(target.isNull()); QVERIFY(!hook.armed); QCOMPARE(view.presentation()->sequence,quint64{1});
    QVERIFY(view.setPresentation(packet(2))); // Plain cached history remains bounded even with a removed child.
    QCOMPARE(history(view).activity().count,std::size_t{2});
  }
  void modeReentrySelectsLatestChartWithoutChangingTheActualSource() {
    ui::RuntimeMetricsView view(core::RuntimeMetricGroup::Peer); QVERIFY(view.beginSession(7)); show(view);
    QVERIFY(view.setPresentation(packet())); Hook hook; hook.callback = [&] { mode(view).setCurrentIndex(2); };
    plot(view).installEventFilter(&hook); mode(view).setCurrentIndex(1);
    QTRY_VERIFY(!hook.armed); QTRY_VERIFY(plot(view).toolTip().endsWith(QStringLiteral("Waiting sockets")));
    QVERIFY(plot(view).accessibleDescription().contains(QStringLiteral("Receive: 8")));
    QCOMPARE(history(view).activity().count,std::size_t{1});
  }
};
QTEST_MAIN(ConnectionsHistoryViewTests)
#include "connections_history_view_tests.moc"
