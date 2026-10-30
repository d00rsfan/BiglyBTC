#include "azureus_statistics_view.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "owned_transport.hpp"
#include "router.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QHostAddress>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
namespace udp = services::azureus_udp;
namespace routing = services::azureus_router;
using namespace std::chrono_literals;
constexpr quint64 Session = 73;
constexpr std::array<const char*, 3> ViewIds{
    "azureus-dht-view", "azureus-operations-view", "azureus-vivaldi-view"};
template<class T> T& child(QObject& owner, const char* name) {
  auto* result = owner.findChild<T*>(QString::fromLatin1(name));
  if (!result) { throw std::runtime_error("Missing Azureus channel test control"); }
  return *result;
}
void drain() {
  for (int i = 0; i < 3; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents();
  }
}
void show(QWidget& widget) { widget.resize(1250, 850); widget.show(); drain(); }
ui::AzureusStatisticsView& view(ui::StatisticsPage& page, std::size_t index) {
  return child<ui::AzureusStatisticsView>(page, ViewIds.at(index));
}
QTableView& table(ui::StatisticsPage& page, std::size_t index = 0) {
  return child<QTableView>(view(page, index), "azureus-statistics-table");
}
QLabel& status(ui::StatisticsPage& page, std::size_t index = 0) {
  return child<QLabel>(view(page, index), "azureus-statistics-status");
}
ui::StatisticsPage& open(ui::LibraryWindow& window) {
  child<QAction>(window, "classic-statistics").trigger();
  if (!window.statisticsPage()) { throw std::runtime_error("Statistics did not open"); }
  return *window.statisticsPage();
}
void savedTotalsOwner(ui::LibraryWindow& window) {
  // Only the unrelated optional saved-file request is completed synthetically.
  // Every observed Azureus snapshot below comes from the actual loopback actors.
  QObject::connect(&window, &ui::LibraryWindow::statisticsRequested, &window,
      [&window](quint64 session, quint64 request) {
    window.setBusy(true);
    auto packet = std::make_shared<ui::StatisticsPacket>();
    packet->session = session; packet->request = request;
    packet->state = ui::StatisticsReadState::Missing;
    window.setStatistics(packet); window.setBusy(false);
  });
}
// Same actual numeric-loopback provider setup used by azureus_statistics_tests.
struct Owners {
  udp::OwnedPingTransport server, client;
  QObject receiver;
  explicit Owners(bool distinctRouterAddress = false)
      : server([] { udp::Config c; c.servePings = true;
          c.observeOperations = true; c.enableVivaldi = true; return c; }()),
        client([] { udp::Config c; c.servePings = true;
          c.observeOperations = true; c.enableVivaldi = true; return c; }()) {
    if (!server.start(distinctRouterAddress ? QHostAddress(QStringLiteral("127.0.0.2")) : QHostAddress(QHostAddress::LocalHost))
        || !client.start() || !server.allowPeer(client.endpoint())
        || !client.allowPeer(server.endpoint())) {
      throw std::runtime_error("Owned Azureus numeric-loopback setup failed");
    }
  }
  bool ping(int& calls) {
    return client.ping(server.endpoint(), 55, &receiver,
        [&calls](udp::Outcome) { ++calls; }, 2s).has_value();
  }
  auto pull(quint64 sequence = 1, quint64 session = Session) {
    auto actual = client.dhtStatisticsSnapshot();
    if (!actual) { throw std::runtime_error("Actual Azureus snapshot failed"); }
    auto packet = std::make_shared<ui::AzureusStatisticsPacket>();
    packet->session = session; packet->sequence = sequence;
    packet->state = ui::AzureusStatisticsState::Observed;
    packet->snapshot = std::make_shared<const udp::OwnedDhtStatisticsSnapshot>(std::move(*actual));
    return packet;
  }
};
routing::NodeId sourceIdentity(const udp::Peer& peer) {
  bool numeric = false; const auto ipv4 = peer.address.toIPv4Address(&numeric);
  if (!numeric || !peer.address.isLoopback() || !peer.port) {
    throw std::runtime_error("Router fixture requires a numeric owned loopback endpoint");
  }
  services::azureus_dht::Address address; address.length = 4; address.port = peer.port;
  for (unsigned i = 0; i < 4; ++i) {
    address.bytes[i] = static_cast<std::uint8_t>(ipv4 >> (24 - i * 8));
  }
  services::azureus_nodes::NodeCache cache;
  const auto identity = cache.lookup({address, {}}, 55);
  if (!identity) { throw std::runtime_error("Source numeric identity lookup failed"); }
  return identity->id;
}
auto routerOwner(const udp::Peer& local, std::uint64_t generation, routing::RouterConfig config = {}) {
  auto owner = routing::RouterOwner::create(sourceIdentity(local), config, generation, 0);
  if (!owner) { throw std::runtime_error("Actual source router creation failed"); }
  return std::move(*owner);
}
auto routerSnapshot(const routing::RouterOwner& owner) {
  const auto source = owner.snapshot();
  if (!source) { throw std::runtime_error("Actual source router snapshot failed"); }
  return std::make_shared<const routing::RouterStatisticsSnapshot>(*source);
}
QTableView& routerTable(ui::StatisticsPage& page) {
  return child<QTableView>(view(page, 0), "azureus-router-table");
}
QLabel& routerStatus(ui::StatisticsPage& page) {
  return child<QLabel>(view(page, 0), "azureus-router-status");
}
class TooltipHook final : public QObject {
public:
  std::function<void()> callback;
  bool eventFilter(QObject* object, QEvent* event) override {
    QPointer<QObject> watched(object);
    if (event->type() == QEvent::ToolTipChange && callback) {
      auto once = std::move(callback); once();
    }
    return watched.isNull();
  }
};
}

class AzureusStatisticsChannelTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void actualActorSnapshotRoutesToAllThreeViewsWithoutEngineMetrics() {
    Owners owners; int calls = 0; QVERIFY(owners.ping(calls));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    const auto packet = owners.pull(); const auto sent = owners.client.statistics().sent;
    ui::StatisticsPage page; page.beginNativeSession(Session); show(page);
    QCOMPARE(page.tabs().count(), 11);QCOMPARE(page.tabs().tabText(10),QStringLiteral("I2P"));
    QCOMPARE(page.tabs().tabText(7), QStringLiteral("DHT"));
    QCOMPARE(page.tabs().tabText(8), QStringLiteral("DDB Operations"));
    QCOMPARE(page.tabs().tabText(9), QStringLiteral("Vivaldi"));
    page.setAzureusStatistics(packet);
    for (std::size_t i = 0; i < ViewIds.size(); ++i) {
      QVERIFY(view(page, i).presentation() == packet);
    }
    page.tabs().setCurrentIndex(7); QTRY_COMPARE(table(page).model()->rowCount(), 22);
    QCOMPARE(table(page).model()->index(0, 1).data(Qt::UserRole).toULongLong(),
        static_cast<qulonglong>(packet->snapshot->transport().counters().sent));
    page.tabs().setCurrentIndex(8); QTRY_COMPARE(table(page, 1).model()->rowCount(), 2);
    QCOMPARE(table(page, 1).model()->index(1, 2).data().toString(), QStringLiteral("Reply"));
    page.tabs().setCurrentIndex(9);
    QTRY_VERIFY(child<QLabel>(view(page, 2), "azureus-vivaldi-coordinates").text().contains("1 accepted observations"));
    page.setRuntimeMetrics({}); // An absent/unrelated engine stream cannot clear Azureus.
    for (std::size_t i = 0; i < ViewIds.size(); ++i) {
      QVERIFY(view(page, i).presentation() == packet);
    }
    QCOMPARE(owners.client.statistics().sent, sent);
    QCOMPARE(owners.client.pendingCount(), std::size_t{0});
  }
  void closedWindowCachesActualPacketsAndFreshPageReplaysThem() {
    Owners owners; int calls = 0; QVERIFY(owners.ping(calls));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    ui::LibraryWindow window; savedTotalsOwner(window); show(window);
    window.beginColumnLayoutSession(Session);
    const auto first = owners.pull(); window.setAzureusStatistics(first);
    QVERIFY(!window.statisticsPage()); QVERIFY(window.azureusStatisticsSnapshot() == first);
    auto& page = open(window); page.tabs().setCurrentIndex(7);
    QTRY_COMPARE(table(page).model()->rowCount(), 22);
    for (std::size_t i = 0; i < ViewIds.size(); ++i) { QVERIFY(view(page, i).presentation() == first); }
    QPointer<ui::StatisticsPage> old = &page;
    child<QAction>(window, "classic-close-tab").trigger(); QVERIFY(!window.statisticsPage());
    const auto cached = owners.pull(2); cached->current = false;
    window.setAzureusStatistics(cached); window.setAzureusStatistics(first);
    QVERIFY(window.azureusStatisticsSnapshot() == cached);
    auto& fresh = open(window); QVERIFY(&fresh != old.data());
    QCOMPARE(fresh.tabs().currentIndex(), 7);
    for (std::size_t i = 0; i < ViewIds.size(); ++i) { QVERIFY(view(fresh, i).presentation() == cached); }
    QTRY_VERIFY(status(fresh).text().contains(QStringLiteral("last observed")));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(old.isNull());
    child<QAction>(window, "classic-close-tab").trigger();
    window.beginColumnLayoutSession(Session); window.beginColumnLayoutSession(0);
    QVERIFY(window.azureusStatisticsSnapshot() == cached);
    window.beginColumnLayoutSession(Session + 1); QVERIFY(!window.azureusStatisticsSnapshot());
    window.setAzureusStatistics(cached); QVERIFY(!window.azureusStatisticsSnapshot());
    auto& next = open(window); QCOMPARE(next.tabs().currentIndex(), 0);
    for (std::size_t i = 0; i < ViewIds.size(); ++i) { QVERIFY(!view(next, i).presentation()); }
  }
  void independentAdmissionRejectsStaleIdentityAndRegressingActualSources() {
    Owners owners; const auto first = owners.pull(10);
    ui::LibraryWindow window; window.setAzureusStatistics(first);
    QVERIFY(!window.azureusStatisticsSnapshot()); window.beginColumnLayoutSession(Session);
    window.setAzureusStatistics(first); QVERIFY(window.azureusStatisticsSnapshot() == first);
    auto duplicate = std::make_shared<ui::AzureusStatisticsPacket>(*first); duplicate->sequence = 11;
    window.setAzureusStatistics(duplicate); // Same source publication/current is not a new observation.
    QVERIFY(window.azureusStatisticsSnapshot() == first);
    auto wrong = owners.pull(12, Session + 1); window.setAzureusStatistics(wrong);
    auto zero = std::make_shared<ui::AzureusStatisticsPacket>(*first); zero->sequence = 0;
    window.setAzureusStatistics(zero);
    auto malformed = std::make_shared<ui::AzureusStatisticsPacket>(*first);
    malformed->sequence = 12; malformed->snapshot.reset(); window.setAzureusStatistics(malformed);
    QVERIFY(window.azureusStatisticsSnapshot() == first);
    const auto next = owners.pull(12); window.setAzureusStatistics(next);
    QVERIFY(window.azureusStatisticsSnapshot() == next);
    auto regression = std::make_shared<ui::AzureusStatisticsPacket>(*first); regression->sequence = 13;
    window.setAzureusStatistics(regression); QVERIFY(window.azureusStatisticsSnapshot() == next);
    window.setAzureusStatistics(first); QVERIFY(window.azureusStatisticsSnapshot() == next);
    Owners newerOwner; const auto newerGeneration = newerOwner.pull(14);
    QVERIFY(newerGeneration->snapshot->transport().generation() > next->snapshot->transport().generation());
    window.setAzureusStatistics(newerGeneration);
    auto olderGeneration = owners.pull(15); olderGeneration->current = false;
    window.setAzureusStatistics(olderGeneration);
    QVERIFY(window.azureusStatisticsSnapshot() == newerGeneration);
  }
  void sameActualSourceMayBecomeLastObservedAndRecoverButNotDuplicate() {
    Owners owners; const auto actual = owners.pull();
    ui::StatisticsPage page; page.beginNativeSession(Session); show(page); page.tabs().setCurrentIndex(7);
    page.setAzureusStatistics(actual); QTRY_COMPARE(table(page).model()->rowCount(), 22);
    const auto source = actual->snapshot;
    auto retained = std::make_shared<ui::AzureusStatisticsPacket>(*actual);
    retained->sequence = 2; retained->current = false;
    QVERIFY(ui::acceptAzureusStatistics(actual, retained, Session));
    page.setAzureusStatistics(retained); QTRY_VERIFY(status(page).text().contains(QStringLiteral("last observed")));
    QCOMPARE(table(page).model()->rowCount(), 22); QVERIFY(view(page, 0).presentation()->snapshot == source);
    auto recovered = std::make_shared<ui::AzureusStatisticsPacket>(*retained);
    recovered->sequence = 3; recovered->current = true;
    QVERIFY(ui::acceptAzureusStatistics(retained, recovered, Session));
    page.setAzureusStatistics(recovered); QTRY_VERIFY(!status(page).text().contains(QStringLiteral("last observed")));
    auto duplicate = std::make_shared<ui::AzureusStatisticsPacket>(*recovered); duplicate->sequence = 4;
    QVERIFY(!ui::acceptAzureusStatistics(recovered, duplicate, Session)); page.setAzureusStatistics(duplicate);
    for (std::size_t i = 0; i < ViewIds.size(); ++i) { QVERIFY(view(page, i).presentation() == recovered); }
    const auto newer = owners.pull(5); auto backwards = std::make_shared<ui::AzureusStatisticsPacket>(*retained);
    backwards->sequence = 6; QVERIFY(!ui::acceptAzureusStatistics(newer, backwards, Session));
  }
  void disabledAndUnavailablePolicyRemainExplicitWithoutInventedRows() {
    ui::LibraryWindow window; savedTotalsOwner(window); window.beginColumnLayoutSession(Session); show(window);
    auto disabled = std::make_shared<ui::AzureusStatisticsPacket>(); disabled->session = Session;
    disabled->sequence = 1; disabled->state = ui::AzureusStatisticsState::Disabled;
    window.setAzureusStatistics(disabled); auto& page = open(window); page.tabs().setCurrentIndex(7);
    QTRY_VERIFY(status(page).text().contains(QStringLiteral("disabled by host policy")));
    QCOMPARE(table(page).model()->rowCount(), 0);
    auto unavailable = std::make_shared<ui::AzureusStatisticsPacket>(*disabled);
    unavailable->sequence = 2; unavailable->state = ui::AzureusStatisticsState::Unavailable;
    unavailable->failure = QStringLiteral("<b>owned actor unavailable</b>");
    window.setAzureusStatistics(unavailable); QTRY_VERIFY(status(page).text().contains(unavailable->failure));
    QCOMPARE(status(page).textFormat(), Qt::PlainText); QCOMPARE(table(page).model()->rowCount(), 0);
    window.setAzureusStatistics(disabled); QVERIFY(window.azureusStatisticsSnapshot() == unavailable);
  }
  void hiddenAndMinimizedViewsDeferRenderingWithoutDiscardingThePacket() {
    Owners owners; ui::LibraryWindow window; savedTotalsOwner(window);
    window.beginColumnLayoutSession(Session); show(window); auto& page = open(window);
    page.tabs().setCurrentIndex(8); drain(); QCOMPARE(table(page, 1).model()->rowCount(), 0);
    window.hide(); drain(); int calls = 0; QVERIFY(owners.ping(calls));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    const auto actual = owners.pull(); window.setAzureusStatistics(actual);
    QVERIFY(window.azureusStatisticsSnapshot() == actual);
    QCOMPARE(table(page, 1).model()->rowCount(), 0);
    show(window); QTRY_COMPARE(table(page, 1).model()->rowCount(), 2);
    window.showMinimized(); QTRY_VERIFY(window.isMinimized());
    auto disabled = std::make_shared<ui::AzureusStatisticsPacket>(); disabled->session = Session;
    disabled->sequence = 2; disabled->state = ui::AzureusStatisticsState::Disabled;
    window.setAzureusStatistics(disabled); drain(); QCOMPARE(table(page, 1).model()->rowCount(), 2);
    window.showNormal(); QTRY_VERIFY(!window.isMinimized());
    QTRY_COMPARE(table(page, 1).model()->rowCount(), 0);
  }
  void nestedNewerPacketWinsAllRoutesWithoutCreatingAnotherRead() {
    Owners owners; const auto first = owners.pull(); const auto newer = owners.pull(2);
    ui::LibraryWindow window; savedTotalsOwner(window); window.beginColumnLayoutSession(Session); show(window);
    auto& page = open(window); page.tabs().setCurrentIndex(7); drain();
    QSignalSpy reads(&window, &ui::LibraryWindow::statisticsRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    bool replaced = false;
    const auto connection = QObject::connect(table(page).model(), &QAbstractItemModel::modelReset,
        &window, [&] { if (!replaced) { replaced = true; window.setAzureusStatistics(newer); } });
    window.setAzureusStatistics(first); QVERIFY(replaced); QObject::disconnect(connection);
    QVERIFY(window.azureusStatisticsSnapshot() == newer);
    for (std::size_t i = 0; i < ViewIds.size(); ++i) { QVERIFY(view(page, i).presentation() == newer); }
    drain(); QCOMPARE(reads.count(), 0); QCOMPARE(refreshes.count(), 0);
  }
  void modelCallbackProfileRebindClearsCacheAndStopsOldDelivery() {
    Owners owners; const auto first = owners.pull();
    ui::LibraryWindow window; savedTotalsOwner(window); window.beginColumnLayoutSession(Session); show(window);
    auto& page = open(window); page.tabs().setCurrentIndex(7); drain();
    bool rebound = false;
    const auto connection = QObject::connect(table(page).model(), &QAbstractItemModel::modelReset,
        &window, [&] { if (!rebound) { rebound = true; window.beginColumnLayoutSession(Session + 1); } });
    window.setAzureusStatistics(first); QVERIFY(rebound); QObject::disconnect(connection);
    QVERIFY(!window.azureusStatisticsSnapshot());
    for (std::size_t i = 0; i < ViewIds.size(); ++i) { QVERIFY(!view(page, i).presentation()); }
    QTRY_COMPARE(table(page).model()->rowCount(), 0);
    const auto next = owners.pull(1, Session + 1); window.setAzureusStatistics(next);
    for (std::size_t i = 0; i < ViewIds.size(); ++i) { QVERIFY(view(page, i).presentation() == next); }
  }
  void actualRouterCountsAdvanceWithoutAnEngineOrAnotherActorPull() {
    Owners owners(true); int calls = 0; QVERIFY(owners.ping(calls));
    QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 3000);
    const auto transport = owners.pull(); const auto sent = owners.client.statistics().sent;
    auto router = routerOwner(owners.client.endpoint(), transport->snapshot->transport().generation());
    ui::LibraryWindow window; savedTotalsOwner(window); window.beginColumnLayoutSession(Session); show(window);
    auto& page = open(window); page.tabs().setCurrentIndex(7); window.setAzureusStatistics(transport);
    QTRY_COMPARE(table(page).model()->rowCount(), 22);
    QCOMPARE(routerTable(page).model()->rowCount(), 0);
    QVERIFY(routerStatus(page).text().contains(QStringLiteral("unavailable")));
    auto initial = std::make_shared<ui::AzureusStatisticsPacket>(*transport);
    initial->sequence = 2; initial->routerSnapshot = routerSnapshot(*router);
    window.setAzureusStatistics(initial); QTRY_COMPARE(routerTable(page).model()->rowCount(), 7);
    routing::ContactInput peer; peer.id = sourceIdentity(owners.server.endpoint());
    QVERIFY(peer.id != sourceIdentity(owners.client.endpoint()));
    QVERIFY(router->contactKnown(peer, 1));
    auto known = std::make_shared<ui::AzureusStatisticsPacket>(*initial);
    known->sequence = 3; known->routerSnapshot = routerSnapshot(*router);
    window.setAzureusStatistics(known);
    QTRY_COMPARE(routerTable(page).model()->index(5, 1).data(Qt::UserRole).toULongLong(), 1ULL);
    QVERIFY(router->contactAlive(peer, 2)); // The actual endpoint has already replied above.
    auto alive = std::make_shared<ui::AzureusStatisticsPacket>(*known);
    alive->sequence = 4; alive->routerSnapshot = routerSnapshot(*router);
    window.setAzureusStatistics(alive);
    QTRY_COMPARE(routerTable(page).model()->index(4, 1).data(Qt::UserRole).toULongLong(), 2ULL);
    for (int i = 0; i < 7; ++i) {
      const auto value = routerTable(page).model()->index(i, 1).data(Qt::UserRole);
      QVERIFY(value.isValid()); QCOMPARE(value.toULongLong(),
          static_cast<qulonglong>(alive->routerSnapshot->counts()[static_cast<std::size_t>(i)]));
    }
    QCOMPARE(routerTable(page).model()->index(5, 1).data(Qt::UserRole).toULongLong(), 0ULL);
    QCOMPARE(table(page).model()->rowCount(), 22); // Original transport table remains unchanged.
    QVERIFY(routerStatus(page).text().contains(QStringLiteral("No reported capacity or intent loss")));
    const auto capture = qEnvironmentVariable("BIGLYBTC_AZUREUS_OPERATION_CAPTURE_DIRECTORY");
    if (!capture.isEmpty()) {
      QCoreApplication::processEvents();
      QVERIFY(window.grab().save(capture + QStringLiteral("/actual-owned-dht-router.png")));
    }
    auto absent = std::make_shared<ui::AzureusStatisticsPacket>(*alive);
    absent->sequence = 5; absent->routerSnapshot.reset(); window.setAzureusStatistics(absent);
    QTRY_COMPARE(routerTable(page).model()->rowCount(), 0);
    QVERIFY(routerStatus(page).text().contains(QStringLiteral("unavailable")));
    QCOMPARE(table(page).model()->rowCount(), 22); QCOMPARE(owners.client.statistics().sent, sent);
  }
  void routerIdentityRevisionAvailabilityAndCurrentChangesAreAdmittedIndependently() {
    Owners owners; const auto transport = owners.pull();
    auto router = routerOwner(owners.client.endpoint(), transport->snapshot->transport().generation());
    auto first = std::make_shared<ui::AzureusStatisticsPacket>(*transport);
    first->routerSnapshot = routerSnapshot(*router);
    auto duplicate = std::make_shared<ui::AzureusStatisticsPacket>(*first); duplicate->sequence = 2;
    QVERIFY(!ui::acceptAzureusStatistics(first, duplicate, Session));
    QVERIFY(router->setSleeping(true, 1));
    auto updated = std::make_shared<ui::AzureusStatisticsPacket>(*first);
    updated->sequence = 2; updated->routerSnapshot = routerSnapshot(*router);
    QVERIFY(ui::acceptAzureusStatistics(first, updated, Session));
    auto regressed = std::make_shared<ui::AzureusStatisticsPacket>(*first);
    regressed->sequence = 3; regressed->current = false;
    QVERIFY(!ui::acceptAzureusStatistics(updated, regressed, Session));
    auto nextOwner = routerOwner(owners.client.endpoint(), first->routerSnapshot->generation() + 1);
    auto next = std::make_shared<ui::AzureusStatisticsPacket>(*updated);
    next->sequence = 3; next->routerSnapshot = routerSnapshot(*nextOwner);
    QVERIFY(ui::acceptAzureusStatistics(updated, next, Session));
    regressed->sequence = 4; regressed->routerSnapshot = updated->routerSnapshot;
    QVERIFY(!ui::acceptAzureusStatistics(next, regressed, Session));
    auto unavailable = std::make_shared<ui::AzureusStatisticsPacket>(*next);
    unavailable->sequence = 4; unavailable->routerSnapshot.reset();
    QVERIFY(ui::acceptAzureusStatistics(next, unavailable, Session));
    auto restored = std::make_shared<ui::AzureusStatisticsPacket>(*next); restored->sequence = 5;
    QVERIFY(ui::acceptAzureusStatistics(unavailable, restored, Session));
    duplicate = std::make_shared<ui::AzureusStatisticsPacket>(*restored); duplicate->sequence = 6;
    QVERIFY(!ui::acceptAzureusStatistics(restored, duplicate, Session));
    duplicate->current = false; QVERIFY(ui::acceptAzureusStatistics(restored, duplicate, Session));
    auto invalid = std::make_shared<ui::AzureusStatisticsPacket>(*next);
    invalid->sequence = 7; invalid->state = ui::AzureusStatisticsState::Disabled; invalid->snapshot.reset();
    QVERIFY(!ui::acceptAzureusStatistics(duplicate, invalid, Session)); // Router data cannot disguise disabled service.
  }
  void realBoundedRouterDropsProduceAnExplicitPartialIndicator() {
    Owners owners; const auto transport = owners.pull();
    routing::RouterConfig config; config.k = 1; config.b = 1; config.replacements = 0;
    auto router = routerOwner(owners.client.endpoint(), transport->snapshot->transport().generation(), config);
    std::shared_ptr<const routing::RouterStatisticsSnapshot> limited;
    for (unsigned i = 1; i <= routing::MaximumRouterContacts + 1; ++i) {
      // Exceed the actual native quota with distinct source-derived identities.
      // Known contacts are policy inputs, not fabricated alive replies. The
      // second and third octets avoid protocol55's restricted-ID collisions.
      const udp::Peer endpoint{QHostAddress(QStringLiteral("127.%1.%2.1").arg(i >> 8).arg(i & 255)),
          quint16{40001}};
      routing::ContactInput contact; contact.id = sourceIdentity(endpoint);
      QVERIFY(router->contactKnown(contact, static_cast<std::int64_t>(i)));
      limited = routerSnapshot(*router); if (limited->limited()) { break; }
    }
    QVERIFY(limited); QVERIFY(limited->limited());
    ui::StatisticsPage page; page.beginNativeSession(Session); show(page); page.tabs().setCurrentIndex(7);
    auto packet = std::make_shared<ui::AzureusStatisticsPacket>(*transport); packet->routerSnapshot = limited;
    page.setAzureusStatistics(packet); QTRY_COMPARE(routerTable(page).model()->rowCount(), 7);
    QTRY_VERIFY(routerStatus(page).text().contains(QStringLiteral("Partial:")));
    QVERIFY(routerStatus(page).text().contains(QString::number(limited->capacityDrops())));
    QVERIFY(routerStatus(page).text().contains(QString::number(limited->lostIntents())));
    QVERIFY(routerStatus(page).text().contains(QStringLiteral("Bounded to 2048 contacts / 1023 nodes")));
    QCOMPARE(routerStatus(page).textFormat(), Qt::PlainText);
  }
  void routerModelCallbackCanDeleteTheWindowBeforeRemainingPublication() {
    Owners owners; auto packet = owners.pull();
    auto router = routerOwner(owners.client.endpoint(), packet->snapshot->transport().generation());
    packet->routerSnapshot = routerSnapshot(*router);
    QPointer<ui::LibraryWindow> window = new ui::LibraryWindow;
    savedTotalsOwner(*window); window->beginColumnLayoutSession(Session); show(*window);
    auto& page = open(*window); page.tabs().setCurrentIndex(7); drain();
    bool removed = false;
    QObject::connect(routerTable(page).model(), &QAbstractItemModel::modelReset,
        window.data(), [&] { removed = true; delete window.data(); });
    window->setAzureusStatistics(packet); QVERIFY(removed); QVERIFY(window.isNull());
  }
  void presentationCallbackMayDeleteThePageOrEntireWindow_data() {
    QTest::addColumn<bool>("wholeWindow"); QTest::newRow("page") << false; QTest::newRow("window") << true;
  }
  void presentationCallbackMayDeleteThePageOrEntireWindow() {
    QFETCH(bool, wholeWindow);
    Owners owners; const auto actual = owners.pull();
    QPointer<ui::LibraryWindow> window = new ui::LibraryWindow;
    savedTotalsOwner(*window); window->beginColumnLayoutSession(Session); show(*window);
    QPointer<ui::StatisticsPage> page = &open(*window);
    page->tabs().setCurrentIndex(7); drain();
    auto& label = status(*page); label.setToolTip({});
    TooltipHook remove; bool deleted = false;
    remove.callback = [&] { deleted = true; if (wholeWindow) { delete window.data(); }
      else { delete page.data(); } };
    label.installEventFilter(&remove); window->setAzureusStatistics(actual);
    QVERIFY(deleted); QVERIFY(page.isNull());
    if (wholeWindow) { QVERIFY(window.isNull()); }
    else { QVERIFY(window); QVERIFY(window->azureusStatisticsSnapshot() == actual); delete window.data(); }
  }
};
QTEST_MAIN(AzureusStatisticsChannelTests)
#include "azureus_statistics_channel_tests.moc"
