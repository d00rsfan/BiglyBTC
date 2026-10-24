#include "persistent_session.hpp"
#include "magnet_intake.hpp"
#include "owned_engine_fixture.hpp"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
using MagnetPhase = engine::MagnetPhase;

engine::NetworkPolicy localPolicy() {
  engine::NetworkPolicy policy;
  policy.mode = engine::NetworkMode::Normal;
  // Explicit loopback TCP only. No public tracker/router/DHT/PEX/LSD/proxy.
  return policy;
}
core::Document parse(const QByteArray& bytes) {
  auto result = core::Document::parse(bytes.toStdString(), storage::compatibleConfigLimits());
  test::require(result.has_value(), "Generated document is not complete");
  return std::move(*result);
}
struct Client {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-managed-magnet-XXXXXX")};
  QString profile, payload, sentinel;
  std::optional<storage::ProfileLease> lease;
  Client() {
    test::require(root.isValid(), "Cannot create owned magnet client");
    profile = root.path() + QStringLiteral("/profile"); payload = root.path() + QStringLiteral("/payload");
    sentinel = root.path() + QStringLiteral("/sentinel.bin");
    test::require(QDir().mkpath(profile), "Cannot create owned profile");
    test::write(sentinel, QByteArray("unchanged-sentinel\0X", 20));
    auto held = storage::ProfileLease::acquire(profile.toStdString());
    test::require(held.has_value(), "Cannot hold owned client profile"); lease.emplace(std::move(*held));
  }
  std::filesystem::path native() const { return profile.toStdString(); }
  engine::PendingMagnet intent(const core::TorrentHash& hash, unsigned short port, bool stopped = false, bool force = false) {
    const std::string uri = "magnet:?xt=urn:btih:" + hash.hex() + "&x.pe=127.0.0.1%3A" + std::to_string(port);
    auto value = engine::parseMagnetIntent(uri, payload.toStdString(), stopped, localPolicy());
    test::require(value.has_value(), "Generated loopback magnet was rejected"); value->forceStart = force;
    return std::move(*value);
  }
  engine::PendingMagnet queue(const core::TorrentHash& hash, unsigned short port, bool stopped = false, bool force = false) {
    auto value = engine::queuePendingMagnet(*lease, intent(hash, port, stopped, force), localPolicy());
    test::require(value.has_value(), "Cannot persist generated magnet intent"); return std::move(*value);
  }
  bool noDownload() const {
    return !QFileInfo::exists(payload) && !QFileInfo::exists(profile + QStringLiteral("/downloads.config"))
        && !QFileInfo::exists(profile + QStringLiteral("/active")) && !QFileInfo::exists(profile + QStringLiteral("/torrents"));
  }
  bool sentinelIntact() const { return test::read(sentinel) == QByteArray("unchanged-sentinel\0X", 20); }
};
std::unique_ptr<engine::PersistentSession> owner(Client& client) {
  auto created = engine::PersistentSession::create({901, 902, 903});
  test::require(created.has_value(), "Actual magnet tests require approved source-bound runtime");
  auto result = std::move(*created);
  test::require(result->configureNetwork(localPolicy()).has_value(), "Cannot configure explicit loopback policy");
  test::require(result->startIdle(*client.lease).has_value(), "Cannot start owned source-qualified session");
  return result;
}
std::optional<engine::PendingMagnetStatus> magnet(engine::PersistentSession& session, const core::TorrentHash& hash) {
  const auto all = session.pendingMagnets();
  const auto found = std::find_if(all.begin(), all.end(), [&](const auto& row) { return row.hash == hash; });
  return found == all.end() ? std::nullopt : std::optional{*found};
}
// Passive bounded diagnostics, evaluated only by Qt's assertion machinery;
// never a second alert consumer or a replacement success/failure condition.
QByteArray magnetDiagnostic(engine::PersistentSession& session, const core::TorrentHash& hash) {
  const auto row = magnet(session, hash);
  QString text = QStringLiteral("pending magnet absent");
  if (row) {
    const auto phase = [&] {
      switch (row->phase) {
        case MagnetPhase::Resolving: return "Resolving";
        case MagnetPhase::Retiring: return "Retiring";
        case MagnetPhase::Imported: return "Imported";
        case MagnetPhase::Failed: return "Failed";
        case MagnetPhase::Cancelled: return "Cancelled";
      }
      return "invalid";
    }();
    text = QStringLiteral("magnet phase=%1 retained=%2 admission=%3 failure=%4")
        .arg(QString::fromLatin1(phase)).arg(static_cast<int>(row->retained))
        .arg(static_cast<qulonglong>(row->admission))
        .arg(row->failure ? QString::fromStdString(row->failure->message).left(512) : QStringLiteral("none"));
  }
  if (const auto observed = session.latest()) {
    text += QStringLiteral("; owner state=%1 sequence=%2 actualCounters=%3")
        .arg(static_cast<int>(observed->state)).arg(static_cast<qulonglong>(observed->sequence))
        .arg(static_cast<int>(observed->observed.has_value()));
  }
  for (const auto& managed : session.managedDownloads()) {
    if (managed.hash != hash) { continue; }
    text += QStringLiteral("; payload phase=%1 retained=%2 complete=%3 received=%4 failure=%5")
        .arg(static_cast<int>(managed.phase)).arg(static_cast<int>(managed.retained)).arg(static_cast<int>(managed.complete))
        .arg(static_cast<qlonglong>(managed.received))
        .arg(managed.failure ? QString::fromStdString(managed.failure->message).left(512) : QStringLiteral("none"));
    break;
  }
  return text.left(2048).toUtf8();
}
bool pump(engine::PersistentSession& session, const core::TorrentHash& hash, MagnetPhase phase) {
  if (!session.poll()) { return false; }
  const auto row = magnet(session, hash); return row && row->phase == phase && !row->retained;
}
bool complete(engine::PersistentSession& session, const core::TorrentHash& hash) {
  if (!session.poll()) { return false; }
  const auto rows = session.managedDownloads();
  return std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row.hash == hash && row.complete && !row.failure; });
}
core::LibrarySnapshot saved(Client& client) {
  const auto source = parse(test::read(client.profile + QStringLiteral("/downloads.config")));
  auto value = core::readLibrary(source); test::require(value && value->issues.empty(), "Generated inventory was rejected");
  return std::move(*value);
}

// Actual loopback wire peer: BEP10 + BEP9, no invented LT alert/status. Used to
// hold metadata requests for cancel/close and deliver exact malicious raw info.
// The two unsafe payload names are never opened by this fixture.
class MetadataPeer final {
  struct Connection { QByteArray input; bool handshake = false; int receiverId = 0; };
  QTcpServer server_;
  QByteArray info_, hash_;
  bool reply_;
  static void append32(QByteArray& bytes, quint32 value) {
    for (int shift = 24; shift >= 0; shift -= 8) { bytes.append(static_cast<char>(value >> shift)); }
  }
  static quint32 read32(const QByteArray& bytes) {
    quint32 result = 0;
    for (qsizetype n = 0; n < 4; ++n) { result = (result << 8) | static_cast<unsigned char>(bytes[n]); }
    return result;
  }
  void frame(QTcpSocket& socket, const QByteArray& body) {
    QByteArray bytes; append32(bytes, static_cast<quint32>(body.size())); bytes += body;
    if (socket.write(bytes) != bytes.size()) { invalid = true; socket.abort(); }
  }
  void receive(QTcpSocket& socket, Connection& state) {
    state.input += socket.readAll();
    if (state.input.size() > 1024 * 1024) { invalid = true; socket.abort(); return; }
    if (!state.handshake) {
      if (state.input.size() < 68) { return; }
      if (state.input[0] != 19 || state.input.mid(1, 19) != "BitTorrent protocol" || state.input.mid(28, 20) != hash_) {
        invalid = true; socket.abort(); return;
      }
      QByteArray handshake(68, '\0'); handshake[0] = 19; handshake.replace(1, 19, "BitTorrent protocol");
      handshake[25] = 0x10; handshake.replace(28, 20, hash_); handshake.replace(48, 20, "-BCMETA-012345678901");
      if (socket.write(handshake) != handshake.size()) { invalid = true; socket.abort(); return; }
      state.handshake = true; state.input.remove(0, 68);
      QByteArray extension; extension.append(20); extension.append('\0');
      extension += "d1:md11:ut_metadatai1ee13:metadata_sizei" + QByteArray::number(static_cast<qlonglong>(info_.size())) + "ee";
      frame(socket, extension);
    }
    while (state.input.size() >= 4) {
      const auto length = read32(state.input);
      if (length > 256 * 1024) { invalid = true; socket.abort(); return; }
      if (state.input.size() < static_cast<qsizetype>(length) + 4) { return; }
      const auto body = state.input.mid(4, length); state.input.remove(0, static_cast<qsizetype>(length) + 4);
      if (body.isEmpty()) { continue; }
      if (static_cast<unsigned char>(body[0]) == 6) { ++payloadRequests; continue; }
      if (body.size() < 2 || static_cast<unsigned char>(body[0]) != 20) { continue; }
      const auto message = core::Document::parse(body.mid(2).toStdString());
      if (!message || message->root().type != core::Node::Type::Dictionary) { invalid = true; socket.abort(); return; }
      if (body[1] == 0) {
        const auto* messages = message->find("m"); const auto* id = messages ? messages->find("ut_metadata") : nullptr;
        if (id && id->type == core::Node::Type::Integer && id->integer > 0 && id->integer <= 255) {
          state.receiverId = static_cast<int>(id->integer);
        }
      } else if (static_cast<unsigned char>(body[1]) == 1) {
        const auto* kind = message->find("msg_type"); const auto* piece = message->find("piece");
        if (!kind || !piece || kind->type != core::Node::Type::Integer || piece->type != core::Node::Type::Integer
            || kind->integer != 0 || piece->integer < 0 || piece->integer >= (info_.size() + 16383) / 16384 || state.receiverId == 0) {
          invalid = true; socket.abort(); return;
        }
        ++metadataRequests;
        if (!reply_) { continue; }
        QByteArray response; response.append(20); response.append(static_cast<char>(state.receiverId));
        response += "d8:msg_typei1e5:piecei" + QByteArray::number(static_cast<qlonglong>(piece->integer))
            + "e10:total_sizei" + QByteArray::number(static_cast<qlonglong>(info_.size())) + "ee";
        response += info_.mid(static_cast<qsizetype>(piece->integer) * 16384, 16384);
        frame(socket, response); ++metadataReplies;
      }
    }
  }
public:
  int connections = 0, disconnected = 0, metadataRequests = 0, metadataReplies = 0, payloadRequests = 0;
  bool invalid = false;
  std::optional<core::TorrentHash> hash;
  MetadataPeer(QByteArray info, bool reply) : info_(std::move(info)), hash_(QCryptographicHash::hash(info_, QCryptographicHash::Sha1)), reply_(reply) {
    auto parsed = core::TorrentHash::parse(std::string_view(hash_.constData(), static_cast<std::size_t>(hash_.size())));
    test::require(parsed.has_value(), "Cannot identify generated raw metainfo"); hash = *parsed;
    test::require(server_.listen(QHostAddress::LocalHost, 0), "Cannot bind owned metadata peer");
    QObject::connect(&server_, &QTcpServer::newConnection, &server_, [&] {
      while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection(); ++connections;
        if (connections > 8) { invalid = true; socket->abort(); socket->deleteLater(); continue; }
        auto state = std::make_shared<Connection>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, [&, socket] { ++disconnected; socket->deleteLater(); });
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, state] { receive(*socket, *state); });
        if (socket->bytesAvailable()) { receive(*socket, *state); }
      }
    });
  }
  ~MetadataPeer() {
    QObject::disconnect(&server_, nullptr, nullptr, nullptr);
    for (auto* socket : server_.findChildren<QTcpSocket*>()) { QObject::disconnect(socket, nullptr, nullptr, nullptr); }
  }
  unsigned short port() const { return server_.serverPort(); }
};
QByteArray rawInfo(bool unsafeMulti = false) {
  const auto piece = QByteArray(20, 'h');
  if (unsafeMulti) {
    return "d5:filesld6:lengthi16384e4:pathl2:..18:payload-escape.bineee4:name6:bundle12:piece lengthi16384e6:pieces20:" + piece + 'e';
  }
  return "d6:lengthi16384e4:name21:../payload-escape.bin12:piece lengthi16384e6:pieces20:" + piece + 'e';
}
bool noPayloadFiles(const Client& client) {
  if (QFileInfo::exists(client.payload) || QFileInfo::exists(client.root.path() + QStringLiteral("/payload-escape.bin"))) { return false; }
  QDirIterator entries(client.profile, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (entries.hasNext()) {
    const auto path = entries.next();
    const auto name = QFileInfo(path).fileName();
    if (name == QStringLiteral("payload-escape.bin") || name.startsWith(QStringLiteral(".BiglyBTC.metadata."))) { return false; }
  }
  return client.sentinelIntact();
}
} // namespace

class ManagedMagnetTests final : public QObject {
  Q_OBJECT
private slots:
  void actualMetadataThenMaterialPayload_data() {
    QTest::addColumn<bool>("multifile"); QTest::addColumn<bool>("force");
    QTest::newRow("single-forced") << false << true;
    QTest::newRow("multifile-normal") << true << false;
  }
  void actualMetadataThenMaterialPayload() {
    QFETCH(bool, multifile); QFETCH(bool, force);
    test::OwnedEngineFixture seed(multifile); QTRY_VERIFY_WITH_TIMEOUT(seed.ready(), 30000);
    Client client; auto pending = client.queue(*seed.hash, seed.port(), false, force);
    auto service = owner(client);
    QVERIFY(client.noDownload()); QVERIFY(service->startMagnet(*client.lease, pending, client.native()));
    auto initial = magnet(*service, pending.hash); QVERIFY(initial && initial->admission != 0 && initial->retained);
    QCOMPARE(initial->phase, MagnetPhase::Resolving); QVERIFY(client.noDownload() && noPayloadFiles(client));
    QVERIFY(!QFileInfo::exists(client.profile + QStringLiteral("/.BiglyBTC.metadata.")
        + QString::fromStdString(pending.hash.hex()) + '.' + QString::number(static_cast<qulonglong>(initial->admission))));
    QTRY_VERIFY2_WITH_TIMEOUT(pump(*service, pending.hash, MagnetPhase::Imported), magnetDiagnostic(*service, pending.hash).constData(), 30000);
    const auto imported = magnet(*service, pending.hash); QVERIFY(imported && !imported->failure && !imported->retained);
    QCOMPARE(imported->admission, initial->admission);
    QTRY_VERIFY2_WITH_TIMEOUT(complete(*service, pending.hash), magnetDiagnostic(*service, pending.hash).constData(), 30000);
    const auto inventory = saved(client); QCOMPARE(inventory.downloads.size(), std::size_t{1});
    QVERIFY(inventory.downloads[0].hash && *inventory.downloads[0].hash == *seed.hash);
    QCOMPARE(inventory.downloads[0].forceStart, force);
    for (const auto& file : seed.files) {
      const auto path = client.payload + '/' + file.relative; QVERIFY(QFileInfo(path).isFile() && !QFileInfo(path).isSymLink());
      QCOMPARE(test::read(path), file.expected);
    }
    const auto remaining = engine::readPendingMagnets(*client.lease, localPolicy()); QVERIFY(remaining && remaining->empty());
    QVERIFY(client.sentinelIntact()); QVERIFY(seed.intact(false));
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(closed.retirement.peerCounters->counters().payloadReceived >= test::OwnedEngineFixture::PayloadBytes);
  }
  void stoppedIntentImportsRealMetadataWithoutMaterialPayload() {
    test::OwnedEngineFixture seed(false); QTRY_VERIFY_WITH_TIMEOUT(seed.ready(), 30000);
    Client client; const auto pending = client.queue(*seed.hash, seed.port(), true);
    auto service = owner(client); QVERIFY(service->startMagnet(*client.lease, pending, client.native()));
    QTRY_VERIFY2_WITH_TIMEOUT(pump(*service, pending.hash, MagnetPhase::Imported), magnetDiagnostic(*service, pending.hash).constData(), 30000);
    QVERIFY(service->managedDownloads().empty()); QVERIFY(!QFileInfo::exists(client.payload));
    const auto inventory = saved(client); QCOMPARE(inventory.downloads.size(), std::size_t{1});
    QCOMPARE(inventory.downloads.front().startup, core::StartupState::Stopped); QVERIFY(!inventory.downloads.front().forceStart);
    QVERIFY(QFileInfo::exists(client.profile + QStringLiteral("/active/") + QString::fromStdString(pending.hash.hex()) + QStringLiteral(".dat")));
    const auto remaining = engine::readPendingMagnets(*client.lease, localPolicy()); QVERIFY(remaining && remaining->empty());
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased); QVERIFY(client.sentinelIntact()); QVERIFY(seed.intact(false));
  }
  void resolvingCancelAndJoinedClose_data() {
    QTest::addColumn<bool>("closeInstead");
    QTest::newRow("explicit-cancel-retires-intent") << false;
    QTest::newRow("joined-close-retains-recovery-intent") << true;
  }
  void resolvingCancelAndJoinedClose() {
    QFETCH(bool, closeInstead);
    // Valid, source-hashed metainfo; this owned peer holds the ACTUAL request.
    MetadataPeer peer(QByteArray("d6:lengthi16384e4:name8:held.bin12:piece lengthi16384e6:pieces20:") + QByteArray(20, 'h') + 'e', false);
    Client client; const auto pending = client.queue(*peer.hash, peer.port()); auto service = owner(client);
    QVERIFY(service->startMagnet(*client.lease, pending, client.native()));
    QTRY_VERIFY2_WITH_TIMEOUT(service->poll().has_value() && peer.metadataRequests > 0, magnetDiagnostic(*service, pending.hash).constData(), 15000);
    QVERIFY(!peer.invalid && peer.metadataReplies == 0 && peer.payloadRequests == 0 && client.noDownload() && noPayloadFiles(client));
    if (!closeInstead) {
      QVERIFY(service->cancelMagnet(pending.hash));
      QTRY_VERIFY2_WITH_TIMEOUT(pump(*service, pending.hash, MagnetPhase::Cancelled), magnetDiagnostic(*service, pending.hash).constData(), 15000);
      const auto remaining = engine::readPendingMagnets(*client.lease, localPolicy()); QVERIFY(remaining && remaining->empty());
    }
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
    QTRY_VERIFY_WITH_TIMEOUT(peer.disconnected > 0, 5000);
    QVERIFY(client.noDownload() && noPayloadFiles(client) && service->managedDownloads().empty());
    const auto remaining = engine::readPendingMagnets(*client.lease, localPolicy()); QVERIFY(remaining);
    QCOMPARE(remaining->size(), closeInstead ? std::size_t{1} : std::size_t{0});
    if (closeInstead) { QCOMPARE(remaining->front().sourceEntry, pending.sourceEntry); }
  }
  void unsafeActualMetadataNeverTouchesPayload_data() {
    QTest::addColumn<bool>("multifile");
    QTest::newRow("single-name-traversal") << false;
    QTest::newRow("multifile-component-traversal") << true;
  }
  void unsafeActualMetadataNeverTouchesPayload() {
    QFETCH(bool, multifile);
    MetadataPeer peer(rawInfo(multifile), true);
    Client client; const auto pending = client.queue(*peer.hash, peer.port()); auto service = owner(client);
    QVERIFY(service->startMagnet(*client.lease, pending, client.native()));
    QTRY_VERIFY2_WITH_TIMEOUT(service->poll().has_value() && peer.metadataReplies > 0, magnetDiagnostic(*service, pending.hash).constData(), 15000);
    QTRY_VERIFY2_WITH_TIMEOUT(pump(*service, pending.hash, MagnetPhase::Failed), magnetDiagnostic(*service, pending.hash).constData(), 30000);
    const auto failed = magnet(*service, pending.hash); QVERIFY(failed && failed->failure && !failed->retained);
    QVERIFY(!peer.invalid && peer.payloadRequests == 0); QVERIFY(client.noDownload() && noPayloadFiles(client));
    const auto remaining = engine::readPendingMagnets(*client.lease, localPolicy());
    QVERIFY(remaining && remaining->size() == 1 && remaining->front().sourceEntry == pending.sourceEntry);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased); QVERIFY(client.noDownload() && noPayloadFiles(client));
  }
  void admissionsRequirePersistedIntentAndExactHeldProfile() {
    MetadataPeer peer(QByteArray("d6:lengthi16384e4:name8:held.bin12:piece lengthi16384e6:pieces20:") + QByteArray(20, 'h') + 'e', false);
    Client client, other; auto service = owner(client);
    const auto bare = client.intent(*peer.hash, peer.port());
    QVERIFY(!service->startMagnet(*client.lease, bare, client.native()));
    const auto queued = client.queue(*peer.hash, peer.port());
    QVERIFY(!service->startMagnet(*client.lease, queued, other.native()));
    QVERIFY(engine::cancelPendingMagnet(*client.lease, queued));
    QVERIFY(!service->startMagnet(*client.lease, queued, client.native()));
    QVERIFY(service->pendingMagnets().empty() && service->managedDownloads().empty() && client.noDownload());
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
    QCOMPARE(peer.connections, 0); QVERIFY(client.sentinelIntact() && other.noDownload());
  }
};
QTEST_GUILESS_MAIN(ManagedMagnetTests)
#include "managed_magnet_tests.moc"
