#include "session_bridge.hpp"
#include "persistent_session.hpp"
#include "magnet_intake.hpp"
#include "torrent_intake.hpp"
#include "library_commands.hpp"
#include "library_view.hpp"
#include "owned_engine_fixture.hpp"

#include <QApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QHostAddress>
#include <QtTest>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>

namespace {
using namespace biglybtc;
namespace lt = libtorrent;
using namespace std::chrono_literals;
constexpr quint16 bit(core::DownloadAction value) { return static_cast<quint16>(1U << static_cast<unsigned>(value)); }
core::Document parsed(const QByteArray& bytes) {
  auto doc = core::Document::parse(bytes.toStdString(), storage::compatibleConfigLimits());
  test::require(doc.has_value(), "Invalid generated magnet profile"); return std::move(*doc);
}
std::string encode(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
// The normal-profile entry point is exercised, but every enabled network route
// remains numeric owned loopback. No tracker, DHT, discovery, public seed or DNS.
lt::entry config() {
  lt::entry result(lt::entry::dictionary_type{});
  result["Bind IP"] = "127.0.0.1";
  result["TCP.Listen.Port.Enable"] = 0; // Outgoing-only; no fixed listen port is opened.
  result["peercontrol.udp.public.enable"] = 0;
  result["Peer Source Selection Default.Tracker"] = 0;
  result["Peer Source Selection Default.DHT"] = 0;
  result["Peer Source Selection Default.PeerExchange"] = 0;
  result["Plugin.DHT.dht.enabled"] = 0;
  result["opaque.magnet.fixture"] = std::string("\0\xffM", 3);
  return result;
}
void saveConfig(test::OwnedEngineFixture& fixture, const lt::entry& value) {
  test::write(fixture.profile + QStringLiteral("/biglybt.config"), QByteArray::fromStdString(test::encoded(value)));
  const auto doc = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
  const auto policy = engine::readNetworkPolicy(&doc, {.mode = engine::NetworkMode::Normal});
  test::require(policy && policy->bindAddresses == std::vector<std::string>{"127.0.0.1"}
      && !policy->incomingTcp && policy->outgoingTcp && !policy->dht && !policy->pex && !policy->lsd
      && !policy->httpTrackers && !policy->udpTrackers && !policy->outgoingUtp,
      "Generated normal profile must remain isolated from discovery and public endpoints");
}
engine::NetworkPolicy policy(test::OwnedEngineFixture& fixture) {
  const auto doc = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
  auto value = engine::readNetworkPolicy(&doc, {.mode = engine::NetworkMode::Normal});
  test::require(value.has_value(), "Cannot read generated loopback policy"); return *value;
}
void emptyLibrary(test::OwnedEngineFixture& fixture) {
  test::write(fixture.profile + QStringLiteral("/downloads.config"), QByteArray("d9:downloadslee"));
  test::require(QFile::remove(fixture.stateFile), "Cannot remove unused generated state");
}
QString magnet(const test::OwnedEngineFixture& fixture) {
  return QStringLiteral("magnet:?xt=urn:btih:") + QString::fromStdString(fixture.hash->hex())
      + QStringLiteral("&dn=Owned+metadata&x.pe=127.0.0.1%3A") + QString::number(fixture.port());
}
ui::Presentation last(const QSignalSpy& spy) {
  return spy.empty() ? ui::Presentation{} : qvariant_cast<ui::Presentation>(spy.last().at(0));
}
std::optional<ui::LibraryItem> row(const QSignalSpy& spy, const QString& hash) {
  const auto frame = last(spy); if (!frame) { return std::nullopt; }
  const auto found = std::ranges::find_if(frame->items, [&](const auto& item) { return item.hash == hash; });
  return found == frame->items.end() ? std::nullopt : std::optional{*found};
}
ui::Target target(const QSignalSpy& spy, const QString& hash) {
  test::require(row(spy, hash).has_value(), "Missing actual magnet library row"); return {hash, last(spy)->revision};
}
bool payloadComplete(const test::OwnedEngineFixture& fixture) {
  for (const auto& file : fixture.files) {
    if (test::read(fixture.payload + '/' + file.relative) != file.expected) { return false; }
  }
  return true;
}
void preservedOpaque(const test::OwnedEngineFixture& fixture) {
  const auto doc = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
  const auto* node = doc.find("opaque.magnet.fixture");
  test::require(node && node->type == core::Node::Type::Bytes && node->bytes == std::string("\0\xffM", 3),
      "Pending transactions changed an opaque Java config value");
}
const QString LegacyHash = QStringLiteral("0123456789012345678901234567890123456789");
void legacy(test::OwnedEngineFixture& fixture) {
  auto value = config();
  lt::entry pending(lt::entry::dictionary_type{});
  pending["hash"] = QByteArray::fromHex(LegacyHash.toLatin1()).toStdString();
  pending["args"] = "xt=urn:btih:" + LegacyHash.toStdString() + "&dn=Legacy+visible";
  pending["sources"] = lt::entry::list_type{};
  pending["timeout"] = 600000;
  pending["ss"] = 1;
  value["MagnetPlugin.active.magnets"]["AERUKZ4JAERUKZ4JAERUKZ4JAERUKZ4J"] = pending;
  saveConfig(fixture, value);
}
} // namespace

class MagnetBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    auto owner = engine::PersistentSession::create({901, 902, 903});
    QVERIFY2(owner.has_value(), "Magnet Host tests require the actual approved source runtime; no skip");
    const auto closed = (*owner)->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined);
  }
  void actualMetadataImportsAndStartsInSameOwner_data() {
    QTest::addColumn<bool>("multifile");
    QTest::newRow("single") << false; QTest::newRow("multi") << true;
  }
  void actualMetadataImportsAndStartsInSameOwner() {
    QFETCH(bool, multifile);
    test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    emptyLibrary(fixture); saveConfig(fixture, config());
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(fixture.native()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && last(rows), 30000);
    QCOMPARE(last(rows)->items.size(), std::size_t{0});
    QVERIFY(bridge.magnetImport(magnet(fixture), fixture.payload));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && row(rows, QString::fromStdString(fixture.hash->hex())), 30000);
    const auto pending = target(rows, QString::fromStdString(fixture.hash->hex()));
    QCOMPARE(row(rows, pending.hash)->name, QStringLiteral("Owned metadata"));
    QVERIFY(!row(rows, pending.hash)->bytes);
    QCOMPARE(row(rows, pending.hash)->actionMask, bit(core::DownloadAction::Stop));
    const auto pendingConfig = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
    const auto* saved = pendingConfig.find("MagnetPlugin.active.magnets");
    QVERIFY(saved && saved->children.size() == 2);
    const auto* other = saved->children[1].find("other_metadata"); QVERIFY(other);
    QVERIFY(other->find("BiglyBTC.pending.destination"));
    // Idle polling must replace the pending row with the real imported row,
    // without Refresh, an extra owner, synthetic completed bytes or a busy reset.
    QTRY_VERIFY_WITH_TIMEOUT(row(rows, pending.hash) && row(rows, pending.hash)->bytes == test::OwnedEngineFixture::PayloadBytes, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(payloadComplete(fixture), 30000);
    QCOMPARE(last(rows)->items.size(), std::size_t{1});
    const auto beforeStale = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    QVERIFY(bridge.downloadAction({pending}, core::DownloadAction::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 10000);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), beforeStale);
    const auto current = target(rows, pending.hash);
    QVERIFY(bridge.downloadAction({current}, core::DownloadAction::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 10000);
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    auto held = storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
    const auto intents = engine::readPendingMagnets(*held, policy(fixture)); QVERIFY(intents && intents->empty());
    const auto library = storage::readLibraryView(*held); QVERIFY(library && library->items.size() == 1);
    QCOMPARE(library->items[0].saved.startup, core::StartupState::Stopped);
    QCOMPARE(library->items[0].saved.hash, fixture.hash);
    QVERIFY(payloadComplete(fixture)); preservedOpaque(fixture);
  }
  void importedCrashRecoveryPreservesLaterStopAndDoesNotResolveAgain() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto actual = fixture.root.path() + QStringLiteral("/metadata.torrent");
    test::write(actual, test::read(fixture.stateFile));
    emptyLibrary(fixture); saveConfig(fixture, config());
    {
      auto held = storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
      auto intent = engine::parseMagnetIntent(magnet(fixture).toStdString(), fixture.payload.toStdString(), false, policy(fixture)); QVERIFY(intent);
      intent->forceStart = true;
      const auto queued = engine::queuePendingMagnet(*held, *intent, policy(fixture)); QVERIFY(queued);
      const auto imported = storage::importTorrent(*held, actual.toStdString(), fixture.payload.toStdString(), false); QVERIFY(imported);
      QVERIFY(storage::applyLibraryCommand(*held, *fixture.hash, core::DownloadAction::Stop));
    }
    const auto before = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(fixture.native())); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && last(rows), 30000);
    QCOMPARE(last(rows)->items.size(), std::size_t{1});
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), before);
    QCOMPARE(last(rows)->items[0].state, QStringLiteral("Stopped"));
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), before);
    auto held = storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
    const auto pending = engine::readPendingMagnets(*held, policy(fixture)); QVERIFY(pending && pending->empty());
    preservedOpaque(fixture);
  }
  void legacyDestinationAndWholeSelectionCasRemainSafe() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000); legacy(fixture);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), messages(&bridge, &app::SessionBridge::messageReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(fixture.native())); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && last(rows), 30000);
    const auto ordinary = target(rows, QString::fromStdString(fixture.hash->hex()));
    const auto pending = target(rows, LegacyHash);
    QCOMPARE(row(rows, LegacyHash)->name, QStringLiteral("Legacy visible"));
    QCOMPARE(row(rows, LegacyHash)->state, QStringLiteral("Magnet needs destination"));
    QCOMPARE(row(rows, LegacyHash)->actionMask, bit(core::DownloadAction::Stop));
    QVERIFY(!messages.empty()); QVERIFY(messages.last().at(0).toString().contains(QStringLiteral("destination")));
    const auto inventory = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    // Simulate a noncooperating editor inside THIS generated profile only.
    auto doc = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
    const auto* map = doc.find("MagnetPlugin.active.magnets"); QVERIFY(map && map->children.size() == 2);
    auto edited = core::Document::parse(doc.originalBytes().substr(map->children[1].begin, map->children[1].end - map->children[1].begin)); QVERIFY(edited);
    QVERIFY(edited->replaceValue("ss", "i3e"));
    QVERIFY(doc.replaceValue("MagnetPlugin.active.magnets", "d" + encode(map->children[0].bytes) + edited->serialize() + 'e'));
    test::write(fixture.profile + QStringLiteral("/biglybt.config"), QByteArray::fromStdString(doc.serialize()));
    const auto changedConfig = test::read(fixture.profile + QStringLiteral("/biglybt.config"));
    QVERIFY(bridge.downloadAction({ordinary, pending}, core::DownloadAction::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 10000);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), inventory);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/biglybt.config")), changedConfig);
    const auto current = target(rows, LegacyHash);
    QVERIFY(bridge.downloadAction({current}, core::DownloadAction::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !row(rows, LegacyHash), 10000);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), inventory);
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    preservedOpaque(fixture);
  }
  void legacyResubmissionAttachesDestinationAndRetainsOriginalStoppedIntent() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    emptyLibrary(fixture); saveConfig(fixture, config());
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    // An actual owned peer accepts TCP but withholds metadata. This keeps the
    // retry/cancel assertions causally separate from a fast metadata import.
    const QString uri = QStringLiteral("magnet:?xt=urn:btih:") + QString::fromStdString(fixture.hash->hex())
        + QStringLiteral("&dn=Legacy+retry&x.pe=127.0.0.1%3A") + QString::number(silent.serverPort());
    std::string key;
    {
      auto held = storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
      const auto intent = engine::parseMagnetIntent(uri.toStdString(), fixture.payload.toStdString(), true, policy(fixture)); QVERIFY(intent);
      const auto queued = engine::queuePendingMagnet(*held, *intent, policy(fixture)); QVERIFY(queued);
      auto doc = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
      const auto* map = doc.find("MagnetPlugin.active.magnets"); QVERIFY(map && map->children.size() == 2);
      key = map->children[0].bytes;
      auto owned = core::Document::parse(queued->sourceEntry); QVERIFY(owned);
      QVERIFY(owned->replaceValue("other_metadata", "d6:opaque3:rawe"));
      QVERIFY(owned->replaceValue("future", "l6:unsafee")); // Opaque original data is not interpreted.
      QVERIFY(doc.replaceValue("MagnetPlugin.active.magnets", "d" + encode(key) + owned->serialize() + 'e'));
      test::write(fixture.profile + QStringLiteral("/biglybt.config"), QByteArray::fromStdString(doc.serialize()));
    }
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), closed(&bridge, &app::SessionBridge::sessionClosed);
    const auto hash = QString::fromStdString(fixture.hash->hex());
    QVERIFY(bridge.openNormal(fixture.native())); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && row(rows, hash), 30000);
    QCOMPARE(row(rows, hash)->state, QStringLiteral("Magnet needs destination"));
    const auto before = test::read(fixture.profile + QStringLiteral("/biglybt.config"));
    QVERIFY(bridge.magnetImport(uri + QStringLiteral("&dn=Conflicting"), fixture.payload));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 10000);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/biglybt.config")), before);
    QVERIFY(bridge.magnetImport(uri, fixture.payload));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 10000);
    // Queue acknowledgement precedes the next idle pump. Inspect the actual
    // persisted row rather than reconstructing the expected typed request.
    const auto configNow = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
    const auto* mapNow = configNow.find("MagnetPlugin.active.magnets"); QVERIFY(mapNow);
    const auto* attached = mapNow->find(key); QVERIFY(attached);
    QCOMPARE(attached->find("ss")->integer, std::int64_t{3});
    QVERIFY(attached->find("other_metadata"));
    QCOMPARE(attached->find("other_metadata")->find("opaque")->bytes, std::string_view("raw"));
    QCOMPARE(attached->find("other_metadata")->find("BiglyBTC.pending.destination")->bytes, std::string_view(fixture.payload.toStdString()));
    QCOMPARE(attached->find("future")->type, core::Node::Type::List);
    QTRY_VERIFY_WITH_TIMEOUT(silent.hasPendingConnections(), 10000);
    // While this exact metadata handle is retained, changing destination is
    // rejected before touching its saved intent or attaching another handle.
    const auto attachedBytes = test::read(fixture.profile + QStringLiteral("/biglybt.config"));
    QVERIFY(bridge.magnetImport(uri, fixture.root.path() + QStringLiteral("/conflicting-destination")));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 10000);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/biglybt.config")), attachedBytes);
    QCOMPARE(row(rows, hash)->state, QStringLiteral("Resolving metadata"));
    QCOMPARE(last(rows)->items.size(), std::size_t{1});
    for (const auto& file : fixture.files) {
      QCOMPARE(test::read(fixture.payload + '/' + file.relative), QByteArray(file.expected.size(), '\0'));
    }
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    auto held = storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
    const auto pending = engine::readPendingMagnets(*held, policy(fixture)); QVERIFY(pending && pending->size() == 1);
    QCOMPARE(pending->front().destination, std::optional<std::filesystem::path>{fixture.payload.toStdString()});
    QVERIFY(pending->front().stopped && !pending->front().forceStart);
    QCOMPARE(pending->front().uri, uri.toStdString());
    const auto library = storage::readLibraryView(*held); QVERIFY(library && library->items.empty());
    preservedOpaque(fixture);
  }
  void malformedPendingReadRetainsDisabledRowAndDoesNotRewriteConfig() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000); legacy(fixture);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady), messages(&bridge, &app::SessionBridge::messageReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(fixture.native())); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && row(rows, LegacyHash), 30000);
    auto doc = parsed(test::read(fixture.profile + QStringLiteral("/biglybt.config")));
    QVERIFY(doc.replaceValue("MagnetPlugin.active.magnets", "i9e"));
    test::write(fixture.profile + QStringLiteral("/biglybt.config"), QByteArray::fromStdString(doc.serialize()));
    const auto corrupted = test::read(fixture.profile + QStringLiteral("/biglybt.config"));
    QVERIFY(bridge.refresh()); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 10000);
    const auto retained = row(rows, LegacyHash); QVERIFY(retained); QCOMPARE(retained->actionMask, quint16{0});
    QVERIFY(!messages.empty()); QVERIFY(messages.last().at(0).toString().contains(QStringLiteral("could not be read")));
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/biglybt.config")), corrupted);
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/biglybt.config")), corrupted);
  }
};
QTEST_MAIN(MagnetBridgeTests)
#include "magnet_bridge_tests.moc"
