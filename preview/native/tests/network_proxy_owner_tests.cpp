#include "network_policy.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include <QHostAddress>
#include <QStringList>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <libtorrent/alert_types.hpp>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// Actual owned-loopback SOCKS peers/origin. No public address, external name,
// original profile, Java, DNS probe or production helper is started here.
namespace {
using namespace biglybtc;
namespace lt = libtorrent;
using namespace std::chrono_literals;
QByteArray bytes(const QByteArray& value) { return QByteArray::number(value.size()) + ':' + value; }
core::Document configuration(quint16 port, bool v5, bool trackerProxy, bool rejectedPassword = false) {
  QByteArray raw = "d";
  const auto boolean = [&](const char* key, bool value) { raw += bytes(key) + (value ? "i1e" : "i0e"); };
  const auto text = [&](const char* key, const QByteArray& value) { raw += bytes(key) + bytes(value); };
  boolean("Enable.Proxy", trackerProxy); boolean("Enable.SOCKS", trackerProxy);
  boolean("Proxy.Data.Enable", true); boolean("Proxy.Data.Same", true);
  text("Proxy.Data.SOCKS.version", v5 ? "V5" : "V4");
  text("Proxy.Host", "127.0.0.1"); text("Proxy.Port", QByteArray::number(port));
  text("Proxy.Username", v5 ? "owned-user" : ""); text("Proxy.Password", v5 ? (rejectedPassword ? "rejected" : "owned-password") : "");
  boolean("peercontrol.udp.public.enable", false); boolean("Server Enable UDP", false);
  boolean("Peer Source Selection Default.DHT", false); boolean("Peer Source Selection Default.PeerExchange", false);
  // Proxy.SOCKS.Tracker.DNS.Disable intentionally ABSENT: original default true.
  auto result = core::Document::parse((raw + 'e').toStdString());
  test::require(result.has_value(), "generated proxy config"); return std::move(*result);
}
void trackerSource(test::OwnedEngineFixture& fixture, quint16 port, bool domain) {
  auto state = core::Document::parse(test::read(fixture.stateFile).toStdString());
  test::require(state.has_value(), "owned metainfo");
  const auto url = QByteArray("http://") + (domain ? "localhost" : "127.0.0.1") + ':' + QByteArray::number(port) + "/announce";
  test::require(state->replaceValue("announce", bytes(url).toStdString()).has_value(), "owned tracker URL");
  test::write(fixture.stateFile, QByteArray::fromStdString(state->serialize()));
}
// Teardown only: stop accepting and abort every fixture-owned partner before
// the synchronous LT close/join. No Qt event loop is required by these peers
// once the test assertion returns. No shipping networking setting is changed.
void abortOwnedSockets(QTcpServer& server) noexcept {
  server.close();
  const auto sockets = server.findChildren<QTcpSocket*>();
  for (auto* socket : sockets) socket->blockSignals(true);
  for (auto* socket : sockets) socket->abort();
}
class Tracker final : public QObject {
public:
  QTcpServer server{this};
  unsigned requests = 0, connections = 0;
  bool wrongSource = false;
  void stopOwned() noexcept { abortOwnedSockets(server); }
  Tracker(quint16 peerPort, bool proxied) {
    test::require(server.listen(QHostAddress::LocalHost, 0), "owned tracker listen");
    connect(&server, &QTcpServer::newConnection, this, [this, peerPort, proxied] {
      while (server.hasPendingConnections()) {
        auto* socket = server.nextPendingConnection();
        if (++connections > 32) { wrongSource = true; socket->abort(); continue; }
        wrongSource |= socket->peerAddress() != QHostAddress(proxied ? "127.0.0.3" : "127.0.0.1");
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer, peerPort] {
          *buffer += socket->readAll();
          if (buffer->size() > 16384) { wrongSource = true; socket->abort(); return; }
          if (!buffer->contains("\r\n\r\n")) return;
          if (!buffer->startsWith("GET /announce?") || !buffer->contains("info_hash=")) { wrongSource = true; socket->abort(); return; }
          ++requests;
          QByteArray peer = QByteArray::fromHex("7f000002");
          peer += static_cast<char>(peerPort >> 8); peer += static_cast<char>(peerPort);
          const QByteArray body = "d8:intervali3600e5:peers6:" + peer + 'e';
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
          socket->disconnectFromHost(); buffer->clear();
        });
      }
    });
  }
};
class Socks final : public QObject {
  enum class Phase { Greeting, Authentication, Connect, Opening, Relay, Rejected };
  struct Channel { QPointer<QTcpSocket> client, upstream; QByteArray pending; Phase phase = Phase::Greeting; std::optional<std::size_t> route; };
public:
  struct Route {
    QByteArray host; quint16 port; bool domain; QHostAddress local, remote;
    quint64 queuedToSeed = 0, queuedToClient = 0;
    QByteArray firstToSeed{}, firstToClient{};
  };
  QTcpServer server{this};
  unsigned authAttempts = 0, refused = 0, connections = 0, udpAssociatesRejected = 0;
  std::vector<Route> routes;
  QByteArray failure;
  const bool v5;
  const QByteArray expectedSocks4User;
  const bool remoteSocks4;
  std::vector<QByteArray> socks4Users;
  const quint16 seedPort, trackerPort;
  void stopOwned() noexcept { abortOwnedSockets(server); }
  Socks(bool version5, quint16 seed, quint16 tracker, QByteArray user = {}, bool remote = false)
      : v5(version5), expectedSocks4User(std::move(user)), remoteSocks4(remote), seedPort(seed), trackerPort(tracker) {
    test::require(server.listen(QHostAddress::LocalHost, 0), "owned proxy listen");
    connect(&server, &QTcpServer::newConnection, this, [this] {
      while (server.hasPendingConnections()) {
        auto* socket = server.nextPendingConnection(); ++connections;
        if (connections > 32) { failure = "proxy connection bound"; socket->abort(); continue; }
        auto channel = std::make_shared<Channel>(); channel->client = socket;
        channel->phase = v5 ? Phase::Greeting : Phase::Connect;
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [this, channel] { receive(channel); });
      }
    });
  }
private:
  void bad(const std::shared_ptr<Channel>& channel, const char* reason) {
    failure = reason; if (channel->client) channel->client->abort();
  }
  void relay(const std::shared_ptr<Channel>& channel, const QByteArray& bytes, bool toSeed) {
    if (bytes.isEmpty()) return;
    auto* target = toSeed ? channel->upstream.data() : channel->client.data();
    if (!target || !channel->route || *channel->route >= routes.size()) { bad(channel, "fixture relay target lost"); return; }
    const auto accepted = target->write(bytes);
    if (accepted != bytes.size()) { bad(channel, "fixture relay did not enqueue exact bytes"); return; }
    auto& route = routes[*channel->route];
    auto& count = toSeed ? route.queuedToSeed : route.queuedToClient;
    auto& first = toSeed ? route.firstToSeed : route.firstToClient;
    count += static_cast<quint64>(accepted);
    first += bytes.first(std::min<qsizetype>(bytes.size(), 96 - first.size()));
  }
  void receive(const std::shared_ptr<Channel>& channel) {
    if (!channel->client) return;
    if (channel->phase == Phase::Relay) { relay(channel, channel->client->readAll(), true); return; }
    if (channel->phase == Phase::Rejected) {
      if (!channel->client->readAll().isEmpty()) bad(channel, "bytes after rejected SOCKS5 command");
      return;
    }
    channel->pending += channel->client->readAll();
    if (channel->pending.size() > 8192) { bad(channel, "bounded SOCKS handshake"); return; }
    auto& data = channel->pending;
    const auto octet = [&](qsizetype index) { return static_cast<quint8>(data.at(index)); };
    if (channel->phase == Phase::Greeting) {
      if (data.size() < 2) return;
      const qsizetype count = octet(1); if (data.size() < 2 + count) return;
      if (octet(0) != 5 || !data.mid(2, count).contains('\2')) { bad(channel, "SOCKS5 authentication not offered"); return; }
      data.remove(0, 2 + count); channel->client->write(QByteArray::fromHex("0502")); channel->phase = Phase::Authentication;
    }
    if (channel->phase == Phase::Authentication) {
      if (data.size() < 2) return;
      const qsizetype userBytes = octet(1); if (data.size() < 3 + userBytes) return;
      const qsizetype passwordBytes = octet(2 + userBytes); if (data.size() < 3 + userBytes + passwordBytes) return;
      ++authAttempts;
      if (octet(0) != 1 || data.mid(2, userBytes) != "owned-user" || data.mid(3 + userBytes, passwordBytes) != "owned-password") {
        ++refused; channel->client->write(QByteArray::fromHex("0101")); channel->client->disconnectFromHost(); return;
      }
      data.remove(0, 3 + userBytes + passwordBytes); channel->client->write(QByteArray::fromHex("0100")); channel->phase = Phase::Connect;
    }
    if (channel->phase != Phase::Connect) return;
    QByteArray host; qsizetype consumed = 0; bool domain = false;
    quint16 port = 0;
    if (v5) {
      if (data.size() < 5) return;
      if (octet(0) != 5 || octet(2) != 0) { bad(channel, "invalid SOCKS5 CONNECT"); return; }
      if (octet(1) == 3) {
        // LT opens its authenticated UDP control channel even when all UDP
        // consumers are disabled. Reject ONLY its exact default association;
        // this fixture never creates a UDP relay or an upstream route for it.
        if (data.size() < 10) return;
        if (data != QByteArray::fromHex("05030001000000000000")) { bad(channel, "unexpected SOCKS5 UDP association"); return; }
        ++udpAssociatesRejected; data.clear(); channel->phase = Phase::Rejected;
        channel->client->write(QByteArray::fromHex("05070001000000000000")); // Command not supported.
        channel->client->disconnectFromHost(); return;
      }
      if (octet(1) != 1) { bad(channel, "invalid SOCKS5 CONNECT"); return; }
      if (octet(3) == 1) {
        if (data.size() < 10) return;
        host = QHostAddress((static_cast<quint32>(octet(4)) << 24) | (static_cast<quint32>(octet(5)) << 16)
            | (static_cast<quint32>(octet(6)) << 8) | octet(7)).toString().toLatin1(); consumed = 10;
      } else if (octet(3) == 3) {
        const qsizetype count = octet(4); if (data.size() < 7 + count) return;
        host = data.mid(5, count); consumed = 7 + count; domain = true;
      } else { bad(channel, "unplanned SOCKS5 address family"); return; }
      port = static_cast<quint16>((octet(consumed - 2) << 8) | octet(consumed - 1));
    } else {
      if (data.size() < 9) return;
      const auto end = data.indexOf('\0', 8); if (end < 0) return;
      if (octet(0) != 4 || octet(1) != 1 || data.mid(8, end - 8) != expectedSocks4User) { bad(channel, "invalid SOCKS4 CONNECT"); return; }
      domain = data.mid(4, 4) == QByteArray::fromHex("00000001");
      if (domain != remoteSocks4) { bad(channel, "unexpected SOCKS4 address mode"); return; }
      port = static_cast<quint16>((octet(2) << 8) | octet(3)); consumed = end + 1;
      if (domain) {
        const auto hostEnd = data.indexOf('\0', consumed); if (hostEnd < 0) return;
        host = data.mid(consumed, hostEnd - consumed); consumed = hostEnd + 1;
      } else {
        host = QHostAddress((static_cast<quint32>(octet(4)) << 24) | (static_cast<quint32>(octet(5)) << 16)
            | (static_cast<quint32>(octet(6)) << 8) | octet(7)).toString().toLatin1();
      }
      socks4Users.push_back(data.mid(8, end - 8));
    }
    const bool tracker = domain && host == "localhost" && port == trackerPort;
    const bool peer = (!domain || (!v5 && remoteSocks4)) && host == "127.0.0.2" && port == seedPort;
    if (!tracker && !peer) { bad(channel, "unplanned proxy destination"); return; }
    data.remove(0, consumed); channel->phase = Phase::Opening;
    auto* upstream = new QTcpSocket(channel->client); channel->upstream = upstream;
    upstream->setProxy(QNetworkProxy::NoProxy);
    if (!upstream->bind(QHostAddress("127.0.0.3"), 0)) { bad(channel, "owned proxy source bind"); return; }
    connect(upstream, &QTcpSocket::connected, channel->client, [this, channel, host, port, domain] {
      if (!channel->client || !channel->upstream) return;
      routes.push_back({host, port, domain, channel->upstream->localAddress(), channel->upstream->peerAddress()});
      channel->route = routes.size() - 1;
      channel->client->write(QByteArray::fromHex(v5 ? "05000001000000000000" : "005a000000000000"));
      channel->phase = Phase::Relay;
      if (!channel->pending.isEmpty()) { relay(channel, channel->pending, true); channel->pending.clear(); }
    });
    connect(upstream, &QTcpSocket::readyRead, channel->client, [this, channel] { if (channel->client && channel->upstream) relay(channel, channel->upstream->readAll(), false); });
    connect(upstream, &QTcpSocket::disconnected, channel->client, [channel] { if (channel->client) channel->client->disconnectFromHost(); });
    connect(channel->client, &QTcpSocket::disconnected, upstream, [channel] { if (channel->upstream) channel->upstream->abort(); });
    connect(upstream, &QTcpSocket::errorOccurred, channel->client, [this, channel](QAbstractSocket::SocketError error) {
      if (error != QAbstractSocket::RemoteHostClosedError) bad(channel, "owned proxy upstream failed");
    });
    upstream->connectToHost(QHostAddress(tracker ? "127.0.0.1" : "127.0.0.2"), port);
  }
};
// Does not fabricate a successful close: the existing tests still assert the
// actual returned join/storage receipt. On any earlier QVERIFY return, release
// all owned socket partners first so destructor cleanup cannot depend on their
// stalled Qt callbacks. The five-second certificate budget is unchanged.
struct OwnerExit final {
  Tracker& tracker;
  Socks& proxy;
  engine::PersistentSession& owner;
  bool closed = false;
  engine::CloseReceipt close() noexcept {
    proxy.stopOwned(); tracker.stopOwned();
    auto result = owner.close(std::chrono::steady_clock::now() + 5s);
    closed = true; return result;
  }
  ~OwnerExit() {
    if (!closed) {
      const auto result = close();
      qInfo("Owned proxy failure cleanup: joined=%d storageReleased=%d", result.sessionJoined,
          result.retirement.storageReleased);
    }
  }
};
QString facts(engine::PersistentSession& owner, const Tracker& tracker, const Socks& proxy,
              const std::optional<engine::AccountingError>& pollFailure) {
  QStringList result;
  result << QStringLiteral("tracker connections=%1 requests=%2 wrongSource=%3; proxy connections=%4 routes=%5 auth=%6 refused=%7 error=%8; pollError=%9")
      .arg(tracker.connections).arg(tracker.requests).arg(tracker.wrongSource)
      .arg(proxy.connections).arg(static_cast<qulonglong>(proxy.routes.size())).arg(proxy.authAttempts)
      .arg(proxy.refused).arg(QString::fromLatin1(proxy.failure)).arg(pollFailure ? static_cast<int>(*pollFailure) : -1);
  for (const auto& row : owner.managedDownloads()) {
    result << QStringLiteral("managed phase=%1 checked=%2 pieces=%3/%4 complete=%5 selected=%6 received=%7 sent=%8 retained=%9 error=%10")
        .arg(static_cast<int>(row.phase)).arg(row.checked).arg(row.pieces).arg(row.totalPieces)
        .arg(row.complete).arg(row.selectedComplete).arg(row.received).arg(row.sent).arg(row.retained)
        .arg(row.failure ? QString::fromStdString(row.failure->message).left(240) : QStringLiteral("none"));
  }
  if (const auto trackers = owner.trackerStatistics()) {
    for (const auto& row : trackers->rows) {
      result << QStringLiteral("tracker endpoint=%1 failures=%2 code=%3 enabled=%4 updating=%5")
          .arg(QString::fromStdString(row.localEndpoint)).arg(row.failures.value_or(-1)).arg(row.errorCode.value_or(-1))
          .arg(row.enabled ? static_cast<int>(*row.enabled) : -1).arg(row.updating ? static_cast<int>(*row.updating) : -1);
      if (result.size() >= 8) break;
    }
  }
  for (std::size_t index = 0; index < proxy.routes.size(); ++index) {
    const auto& route = proxy.routes[index];
    result << QStringLiteral("relay%1 %2:%3 queuedToSeed=%4 queuedToClient=%5 firstToSeed=%6 firstToClient=%7")
        .arg(static_cast<qulonglong>(index)).arg(QString::fromLatin1(route.host)).arg(route.port)
        .arg(route.queuedToSeed).arg(route.queuedToClient)
        .arg(QString::fromLatin1(route.firstToSeed.toHex())).arg(QString::fromLatin1(route.firstToClient.toHex()));
    if (index == 3) break;
  }
  return result.join(QStringLiteral(" | "));
}
std::unique_ptr<lt::session> seeder(test::OwnedEngineFixture& fixture, lt::torrent_handle& handle) {
  auto settings = test::localSettings(); settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.2:0");
  settings.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.2");
  settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(static_cast<std::uint32_t>(lt::alert_category::all)));
  auto result = std::make_unique<lt::session>(settings); lt::error_code error;
  const auto raw = test::read(fixture.stateFile).toStdString();
  auto metadata = std::make_shared<lt::torrent_info>(raw.data(), static_cast<int>(raw.size()), error);
  test::require(!error, "owned seed metadata"); metadata->clear_trackers();
  lt::add_torrent_params params; params.ti = metadata; params.save_path = (fixture.root.path() + "/seed").toStdString();
  params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
  params.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex | lt::torrent_flags::disable_lsd;
  handle = result->add_torrent(params, error); test::require(!error && handle.is_valid(), "owned seeder admission"); return result;
}
struct SeedTrace {
  QStringList events;
  void append(const QString& text) {
    if (events.size() == 48) events.removeFirst();
    events.push_back(text.left(256));
  }
  QString facts(const lt::torrent_handle& handle, unsigned peers, bool direct) const {
    const auto status = handle.status();
    return QStringLiteral("seed peersAccepted=%1 direct=%2 isSeed=%3 peers=%4 uploads=%5 payloadSent=%6; events=[%7]")
        .arg(peers).arg(direct).arg(status.is_seeding).arg(status.num_peers).arg(status.num_uploads)
        .arg(status.total_payload_upload).arg(events.join(QStringLiteral(" || ")));
  }
};
void popSeed(lt::session& seed, unsigned& actualPeers, bool& direct, SeedTrace* trace = nullptr) {
  std::vector<lt::alert*> alerts; seed.pop_alerts(&alerts);
  for (const auto* alert : alerts) {
    if (const auto* peer = lt::alert_cast<lt::peer_connect_alert>(alert)) {
      ++actualPeers; direct |= peer->endpoint.address().to_string() != "127.0.0.3";
    }
    if (!trace) continue;
    bool retain = lt::alert_cast<lt::peer_connect_alert>(alert) || lt::alert_cast<lt::peer_disconnected_alert>(alert)
        || lt::alert_cast<lt::peer_error_alert>(alert) || lt::alert_cast<lt::invalid_request_alert>(alert)
        || lt::alert_cast<lt::block_uploaded_alert>(alert) || lt::alert_cast<lt::alerts_dropped_alert>(alert);
#ifndef TORRENT_DISABLE_LOGGING
    if (const auto* peer = lt::alert_cast<lt::peer_log_alert>(alert)) {
      const auto event = QByteArray(peer->event_type).toLower();
      for (const char* key : {"handshake", "connect", "choke", "interested", "bitfield", "reject", "request", "error", "hash", "encrypt", "rc4", "dh"}) {
        if (event.contains(key)) { retain = true; break; }
      }
    }
#endif
    if (retain) trace->append(QString::fromStdString(alert->message()));
  }
}
} // namespace

class NetworkProxyOwnerTests final : public QObject {
  Q_OBJECT
private slots:
  void dataOnlySocks4UsesNoTrackerDnsPreference() {
    auto document = configuration(1080, false, false); const auto original = document.originalBytes();
    const auto policy = engine::readNetworkPolicy(&document, {engine::NetworkMode::Normal});
    QVERIFY(policy); QCOMPARE(policy->proxy.kind, engine::NetworkProxyKind::Socks4);
    QVERIFY(policy->proxy.peers && !policy->proxy.trackers && !policy->proxy.remoteDns);
    const auto settings = engine::makeNetworkSettings(*policy); QVERIFY(settings);
    QCOMPARE(settings->get_int(lt::settings_pack::proxy_type), static_cast<int>(lt::settings_pack::socks4));
    QVERIFY(!settings->get_bool(lt::settings_pack::proxy_hostnames)); QCOMPARE(document.serialize(), original);
    auto impossible = *policy; impossible.proxy.remoteDns = true;
    QVERIFY(!engine::validateNetworkPolicy(impossible));
  }
  void actualProxyPayloadRoutes_data() {
    QTest::addColumn<bool>("v5"); QTest::newRow("socks4-numeric-data") << false; QTest::newRow("socks5-auth-domain-tracker") << true;
  }
  void actualProxyPayloadRoutes() {
    QFETCH(bool, v5);
    test::OwnedEngineFixture fixture(false); lt::torrent_handle seedHandle; auto seed = seeder(fixture, seedHandle);
    QTRY_VERIFY_WITH_TIMEOUT(seed->listen_port() != 0 && seedHandle.status().is_seeding, 30000);
    Tracker tracker(seed->listen_port(), v5); Socks proxy(v5, seed->listen_port(), tracker.server.serverPort());
    trackerSource(fixture, tracker.server.serverPort(), v5);
    auto config = configuration(proxy.server.serverPort(), v5, v5);
    auto policy = engine::readNetworkPolicy(&config, {engine::NetworkMode::Normal}); QVERIFY(policy);
    policy->listenPort = 0; // Explicit owned ephemeral listener policy; never uses the original port.
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    auto owner = engine::PersistentSession::create({401, 402, static_cast<std::uint64_t>(v5 ? 2 : 1)}); QVERIFY(owner);
    OwnerExit exit{tracker, proxy, **owner};
    QVERIFY((*owner)->configureNetwork(*policy)); QVERIFY((*owner)->startIdle(*lease));
    QVERIFY((*owner)->startManaged(*lease, {*fixture.hash, 0, true, false}));
    unsigned peerConnections = 0; bool direct = false;
    std::optional<engine::AccountingError> pollFailure; SeedTrace trace;
    const auto completed = [&] {
      const auto polled = (*owner)->poll(); if (!polled) pollFailure = polled.error();
      popSeed(*seed, peerConnections, direct, &trace);
      const auto rows = (*owner)->managedDownloads(); return rows.size() == 1 && rows[0].complete && rows[0].received > 0;
    };
    QTRY_VERIFY2_WITH_TIMEOUT(completed(), qPrintable(facts(**owner, tracker, proxy, pollFailure) + QStringLiteral(" | ") + trace.facts(seedHandle, peerConnections, direct)), 30000);
    QVERIFY2(proxy.failure.isEmpty(), proxy.failure.constData()); QVERIFY(tracker.requests > 0 && !tracker.wrongSource);
    QVERIFY(peerConnections > 0 && !direct); QVERIFY(seedHandle.status().total_payload_upload > 0);
    QVERIFY(std::ranges::any_of(proxy.routes, [](const auto& route) { return route.host == "127.0.0.2" && !route.domain && route.local == QHostAddress("127.0.0.3"); }));
    if (v5) {
      QVERIFY(proxy.authAttempts > 0);
      QVERIFY(proxy.udpAssociatesRejected > 0);
      QVERIFY(std::ranges::any_of(proxy.routes, [](const auto& route) { return route.host == "localhost" && route.domain; }));
    }
    const auto closed = exit.close();
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased); QVERIFY(fixture.accountingIntact());
  }
#if defined(__linux__) && __has_include(<libtorrent/biglybtc_socks4a.hpp>)
  void actualVerifiedSocks4IdentityAndRemoteNameRoutes_data() {
    QTest::addColumn<bool>("remote");
    QTest::newRow("qualified-numeric4-USERID") << false;
    QTest::newRow("qualified4a-numeric-peer-name") << true;
  }
  void actualVerifiedSocks4IdentityAndRemoteNameRoutes() {
    QFETCH(bool, remote);
    // No skip or caller-trust substitute: this positive cohort requires the
    // actual approved new SDK + complete mapped-image RuntimeGuard proof.
    const auto capability = engine::verifiedSocks4Capability();
    QVERIFY2(capability, "The actual loaded provider did not pass the exact source/image SOCKS4 capability proof");
    test::OwnedEngineFixture fixture(false); lt::torrent_handle seedHandle; auto seed = seeder(fixture, seedHandle);
    QTRY_VERIFY_WITH_TIMEOUT(seed->listen_port() != 0 && seedHandle.status().is_seeding, 30000);
    Tracker tracker(seed->listen_port(), false);
    Socks proxy(false, seed->listen_port(), tracker.server.serverPort(), "owned-user", remote);
    trackerSource(fixture, tracker.server.serverPort(), false);
    auto config = configuration(proxy.server.serverPort(), false, false);
    QVERIFY(config.replaceValue("Proxy.Username", bytes("owned-user").toStdString()));
    if (remote) { QVERIFY(config.replaceValue("Proxy.Data.SOCKS.version", bytes("V4a").toStdString())); }
    const auto original = config.serialize();
    QVERIFY(!engine::readNetworkPolicy(&config, {engine::NetworkMode::Normal}));
    auto policy = engine::readNetworkPolicy(&config,
        {.mode = engine::NetworkMode::Normal, .socks4Capability = capability});
    QVERIFY(policy); QCOMPARE(policy->proxy.remoteDns, remote);
    QCOMPARE(policy->proxy.kind, remote ? engine::NetworkProxyKind::Socks4a : engine::NetworkProxyKind::Socks4);
    QCOMPARE(config.serialize(), original);
    const auto settings = engine::makeNetworkSettings(*policy); QVERIFY(settings);
    QCOMPARE(settings->get_int(lt::settings_pack::proxy_type), static_cast<int>(lt::settings_pack::socks4));
    QCOMPARE(settings->get_str(lt::settings_pack::proxy_username), std::string("owned-user"));
    QCOMPARE(settings->get_bool(lt::settings_pack::proxy_hostnames), remote);
    auto invalid = *policy; invalid.socks4Capability.reset();
    QVERIFY(!engine::makeNetworkSettings(invalid));
    invalid = *policy; invalid.proxy.password = "not-a-SOCKS4-password";
    QVERIFY(!engine::makeNetworkSettings(invalid));
    invalid = *policy; invalid.outgoingUtp = true;
    QVERIFY(!engine::makeNetworkSettings(invalid));
    policy->listenPort = 0;
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    auto owner = engine::PersistentSession::create({420, 421, static_cast<std::uint64_t>(remote ? 2 : 1)}); QVERIFY(owner);
    OwnerExit exit{tracker, proxy, **owner};
    QVERIFY((*owner)->configureNetwork(*policy)); QVERIFY((*owner)->startIdle(*lease));
    QVERIFY((*owner)->startManaged(*lease, {*fixture.hash, 0, true, false}));
    unsigned peerConnections = 0; bool direct = false;
    std::optional<engine::AccountingError> pollFailure; SeedTrace trace;
    const auto completed = [&] {
      const auto polled = (*owner)->poll(); if (!polled) pollFailure = polled.error();
      popSeed(*seed, peerConnections, direct, &trace);
      const auto rows = (*owner)->managedDownloads(); return rows.size() == 1 && rows[0].complete && rows[0].received > 0;
    };
    QTRY_VERIFY2_WITH_TIMEOUT(completed(), qPrintable(facts(**owner, tracker, proxy, pollFailure) + QStringLiteral(" | ") + trace.facts(seedHandle, peerConnections, direct)), 30000);
    QVERIFY2(proxy.failure.isEmpty(), proxy.failure.constData()); QVERIFY(tracker.requests > 0 && !tracker.wrongSource);
    QVERIFY(peerConnections > 0 && !direct); QVERIFY(seedHandle.status().total_payload_upload > 0);
    QVERIFY(!proxy.socks4Users.empty());
    QVERIFY(std::ranges::all_of(proxy.socks4Users, [](const auto& user) { return user == "owned-user"; }));
    QVERIFY(std::ranges::any_of(proxy.routes, [remote](const auto& route) {
      return route.host == "127.0.0.2" && route.domain == remote && route.local == QHostAddress("127.0.0.3")
          && route.queuedToSeed > 0 && route.queuedToClient > 0;
    }));
    const auto closed = exit.close();
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased); QVERIFY(fixture.accountingIntact());
  }
#else
  void oldSdkCannotMintSocks4IdentityOrRemoteNameAuthority() {
    QVERIFY(!engine::verifiedSocks4Capability());
    auto config = configuration(1080, false, false);
    QVERIFY(config.replaceValue("Proxy.Username", bytes("owned-user").toStdString()));
    QVERIFY(!engine::readNetworkPolicy(&config, {engine::NetworkMode::Normal}));
    QVERIFY(config.replaceValue("Proxy.Username", bytes("").toStdString()));
    QVERIFY(config.replaceValue("Proxy.Data.SOCKS.version", bytes("V4a").toStdString()));
    QVERIFY(!engine::readNetworkPolicy(&config, {engine::NetworkMode::Normal}));
  }
#endif
  void actualAuthenticationFailureHasNoDirectTrackerOrPayloadFallback() {
    test::OwnedEngineFixture fixture(false); lt::torrent_handle seedHandle; auto seed = seeder(fixture, seedHandle);
    QTRY_VERIFY_WITH_TIMEOUT(seed->listen_port() != 0 && seedHandle.status().is_seeding, 30000);
    Tracker tracker(seed->listen_port(), true); Socks proxy(true, seed->listen_port(), tracker.server.serverPort());
    trackerSource(fixture, tracker.server.serverPort(), true);
    auto config = configuration(proxy.server.serverPort(), true, true, true);
    auto policy = engine::readNetworkPolicy(&config, {engine::NetworkMode::Normal}); QVERIFY(policy); policy->listenPort = 0;
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    auto owner = engine::PersistentSession::create({410, 411, 412}); QVERIFY(owner);
    OwnerExit exit{tracker, proxy, **owner};
    QVERIFY((*owner)->configureNetwork(*policy)); QVERIFY((*owner)->startIdle(*lease));
    QVERIFY((*owner)->startManaged(*lease, {*fixture.hash, 0, true, false}));
    QTRY_VERIFY_WITH_TIMEOUT(proxy.refused > 0, 10000);
    QTest::qWait(1500); static_cast<void>((*owner)->poll());
    unsigned peerConnections = 0; bool direct = false; popSeed(*seed, peerConnections, direct);
    QVERIFY(proxy.failure.isEmpty()); QVERIFY(proxy.routes.empty()); QCOMPARE(tracker.requests, 0U); QCOMPARE(peerConnections, 0U);
    const auto rows = (*owner)->managedDownloads(); QCOMPARE(rows.size(), std::size_t{1}); QCOMPARE(rows[0].received, std::int64_t{0});
    const auto closed = exit.close();
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased); QVERIFY(fixture.accountingIntact(false));
  }
};
QTEST_GUILESS_MAIN(NetworkProxyOwnerTests)
#include "network_proxy_owner_tests.moc"
