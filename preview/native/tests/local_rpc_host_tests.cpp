// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_engine_fixture.hpp"
#include "existing_controls_fixture.hpp"
#include "library_window.hpp"
#include "local_rpc_controls.hpp"
#include "encoded_document.hpp"
#include "library_view.hpp"
#include "profile.hpp"
#include "rpc_profile.hpp"
#include "rpc_identity_store.hpp"
#include "library_commands.hpp"
#include "session_bridge.hpp"

#include <QAction>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest>
#include <zlib.h>

namespace {
using namespace biglybtc;

void saveId(test::OwnedEngineFixture& fixture, std::int64_t id) {
  const auto bytes = test::read(fixture.stateFile).toStdString();
  auto state = core::Document::parse(bytes);
  test::require(bool(state), "Parse the generated original state");
  const auto* source = state->find("attributes");
  auto attributes = core::Document::parse(source ? bytes.substr(source->begin, source->end - source->begin) : "de");
  test::require(attributes && attributes->replaceValue("Plugin.xmwebui.xmui.dl.id", "i" + std::to_string(id) + "e") &&
      attributes->replaceValue("parameters", "d25:stats.download.added.timei1700000432999ee") &&
      state->replaceValue("attributes", attributes->serialize()) &&
      state->replaceValue("comment", "17:Generated comment") &&
      state->replaceValue("created by", "11:RPC fixture") &&
      state->replaceValue("creation date", "i1700000106e"), "Install a generated original ID and metainfo");
  test::write(fixture.stateFile, QByteArray::fromStdString(state->serialize()));
}

void saveCachedIdentity(const test::OwnedEngineFixture& fixture, const QString& path, bool duplicate) {
  const auto raw = test::read(fixture.stateFile).toStdString();
  const auto primary = core::Document::parse(raw);
  const auto* source = primary ? primary->find("attributes") : nullptr;
  test::require(source, "Read original cached identity attributes from the generated torrent");
  auto attributes = core::Document::parse(raw.substr(source->begin, source->end - source->begin));
  auto selected = core::Document::parse("de");
  test::require(attributes && attributes->replaceValue("Plugin.xmwebui.xmui.dl.id", "i0e") &&
      attributes->replaceValue("parameters", "d25:stats.download.added.timei1700000654888ee") &&
      attributes->replaceValue("opaque.cache", "11:retain-this"), "Generate the authoritative saved cache attributes");
  const auto hash = fixture.hash->bytes();
  test::require(selected && selected->replaceValue("hash", "20:" + std::string(
          reinterpret_cast<const char*>(hash.data()), hash.size())) &&
      selected->replaceValue("name", "16:Cached test name") &&
      selected->replaceValue("attributes", attributes->serialize()), "Generate the selected original cache entry");
  const auto neighbor = "d4:hash20:" + std::string(20, 'n') + "4:name8:neighbor6:opaque3:raw" + "e";
  const auto entry = selected->serialize();
  const auto cache = "d6:opaque8:preserve5:statel" + entry + (duplicate ? entry : "") + neighbor + "ee";
  const auto file = ::gzopen(path.toUtf8().constData(), "wb");
  test::require(file, "Open the generated original gzip cache");
  const auto written = ::gzwrite(file, cache.data(), static_cast<unsigned int>(cache.size()));
  const auto closed = ::gzclose(file);
  test::require(written == static_cast<int>(cache.size()) && closed == Z_OK, "Save the generated original gzip cache");
}

void saveTorrentTransferPrefix(const test::OwnedEngineFixture& fixture, bool disabledLimit) {
  const auto path = fixture.profile + QStringLiteral("/downloads.config");
  const auto source = test::read(path).toStdString();
  auto inventory = core::Document::parse(source);
  const auto* list = inventory ? inventory->find("downloads") : nullptr;
  test::require(list && list->children.size() == 1, "Read the selected generated original inventory");
  const auto& selected = list->children.front();
  auto download = core::Document::parse(source.substr(selected.begin, selected.end - selected.begin));
  test::require(download && download->replaceValue("downloaded", "i321e") &&
      download->replaceValue("uploaded", "i654e") &&
      download->replaceValue("maxdl", disabledLimit ? "i4294967295e" : "i65536e") &&
      download->replaceValue("maxul", "i777e") &&
      inventory->replaceValue("downloads", "l" + download->serialize() + "e"),
      "Write exact generated original transfer totals");
  test::write(path, QByteArray::fromStdString(inventory->serialize()));
}

void addRemoteProfileFields(const test::OwnedEngineFixture& fixture, core::Document& settings, bool encrypted) {
  const auto path = fixture.payload.toUtf8();
  test::require(settings.replaceValue("Default save path", std::to_string(path.size()) + ':' + path.toStdString()) &&
      settings.replaceValue("TCP.Listen.Port", "i42106e") &&
      settings.replaceValue("Listen.Port.Randomize.Enable", "i1e") &&
      settings.replaceValue("network.transport.encrypted.require", encrypted ? "i1e" : "i0e") &&
      settings.replaceValue("max downloads", "i7e") && settings.replaceValue("max active torrents", "i9e") &&
      settings.replaceValue("Max Download Speed KBs", encrypted ? "i0e" : "i900e") &&
      settings.replaceValue("config.ui.speed.partitions.manual.download.last", "i600e") &&
      settings.replaceValue("Rename Incomplete Files", encrypted ? "i1e" : "i0e") &&
      settings.replaceValue("download.history.active.size", "i4e") &&
      settings.replaceValue("download.history.dead.size", "i3e") &&
      settings.replaceValue("locale.set.complete.count", "i9e") &&
      settings.replaceValue("Max.Peer.Connections.Total", "i400e"),
      "Write selected original core preferences into a generated profile");
}

void saveArchive(test::OwnedEngineFixture& fixture, std::int64_t unnamedId = 41, std::int64_t archivedLength = 64) {
  auto archive = core::Document::parse("de");
  auto stub = core::Document::parse("de");
  auto unnamed = core::Document::parse("de");
  const auto archived = (fixture.payload + QStringLiteral("/archived.bin")).toStdString();
  test::require(archive && stub && unnamed &&
      stub->replaceValue("hash", "20:" + std::string(20, 'a')) &&
      stub->replaceValue("attr", "d25:Plugin.xmwebui.xmui.dl.idi25ee") &&
      stub->replaceValue("name", "18:Generated Archived") &&
      stub->replaceValue("dt", "i1700000123456e") &&
      stub->replaceValue("l", std::to_string(archived.size()) + ':' + archived) &&
      stub->replaceValue("s", "i32768e") &&
      stub->replaceValue("files", "ld3:leni" + std::to_string(archivedLength) + "e3:rel10:sample.binee") &&
      unnamed->replaceValue("hash", "20:" + std::string(20, 'b')) &&
      unnamed->replaceValue("attr", "d25:Plugin.xmwebui.xmui.dl.idi" + std::to_string(unnamedId) + "ee") &&
      archive->replaceValue("stubs", "l" + stub->serialize() + unnamed->serialize() + "e"),
      "Write generated original archive stubs");
  test::write(fixture.profile + QStringLiteral("/dlarchive.config"), QByteArray::fromStdString(archive->serialize()));
}

int reserveRpcPort() {
  QTcpServer reservation;
  reservation.setProxy(QNetworkProxy::NoProxy);
  test::require(reservation.listen(QHostAddress::LocalHost, 0), "Reserve a generated original RPC port");
  const int port = reservation.serverPort();
  reservation.close();
  return port;
}

void setListenerFields(core::Document& settings, int port, bool viewOnly) {
  const auto digest = QCryptographicHash::hash(QByteArrayView("generated-token"), QCryptographicHash::Sha1);
  const std::string prefix = viewOnly ? "Plugin.generated-xmwebui." : "Plugin.xmwebui.";
  test::require(settings.replaceValue("PluginInfo.xmwebui.enabled", "i1e") &&
      settings.replaceValue(prefix + "Enable", "i1e") &&
      settings.replaceValue(prefix + "Password Enable", "i1e") &&
      settings.replaceValue(prefix + "Port", "i" + std::to_string(port) + "e") &&
      settings.replaceValue(prefix + "Protocol", "4:hTtP") &&
      settings.replaceValue(prefix + "Mode", viewOnly ? "4:vIeW" : "4:FuLl") &&
      settings.replaceValue("opaque.rpc.fixture", "8:retained") &&
      settings.replaceValue(prefix + "User", "14:generated-user") &&
      settings.replaceValue(prefix + "Password", "20:" + digest.toStdString()),
      "Write generated original WebPlugin credentials");
  if (viewOnly) {
    test::require(bool(settings.replaceValue(prefix + "xmwebui.hidearchived", "i1e")),
        "Hide archived downloads using the installed original directory");
  }
}

int saveListener(test::OwnedEngineFixture& fixture, bool viewOnly) {
  const auto port = reserveRpcPort();
  auto settings = core::Document::parse("de");
  test::require(bool(settings), "Create an original listener configuration");
  setListenerFields(*settings, port, viewOnly);
  addRemoteProfileFields(fixture, *settings, true);
  test::write(fixture.profile + QStringLiteral("/biglybt.config"), QByteArray::fromStdString(settings->serialize()));
  return port;
}

QByteArray exchange(quint16 port, const QByteArray& request) {
  QTcpSocket socket;
  socket.setProxy(QNetworkProxy::NoProxy);
  socket.connectToHost(QHostAddress::LocalHost, port);
  test::require(QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }, 4000),
      "Connect to the actual RPC listener");
  test::require(socket.write(request) == request.size(), "Send the generated local RPC request");
  QByteArray data;
  test::require(QTest::qWaitFor([&] {
    data += socket.readAll();
    return socket.state() == QAbstractSocket::UnconnectedState;
  }, 5000), "Collect the owned RPC response");
  return data;
}

QByteArray sessionId(const QByteArray& response) {
  constexpr QByteArrayView header("\r\nx-transmission-session-id: ");
  const auto at = response.toLower().indexOf(header);
  if (at < 0) { return {}; }
  const auto start = at + header.size();
  return response.mid(start, response.indexOf("\r\n", start) - start);
}
} // namespace

class LocalRpcHostTests final : public QObject {
  Q_OBJECT
private slots:
  void reportWantedFileProgress_data() {
    QTest::addColumn<int>("sharedPieceBytes");
    QTest::newRow("aligned") << 0;
    QTest::newRow("wanted-and-skipped-share-piece") << 911;
  }

  void reportWantedFileProgress() {
    QFETCH(int, sharedPieceBytes);
    test::OwnedEngineFixture fixture(true, true, {}, sharedPieceBytes);
    saveId(fixture, 37);
    saveArchive(fixture, 41, -64);
    const auto selectedPath = fixture.payload + '/' + fixture.files[0].relative;
    const auto skippedPath = fixture.payload + '/' + fixture.files[1].relative;
    auto skippedBefore = test::read(skippedPath);
    if (sharedPieceBytes) {
      skippedBefore.replace(0, sharedPieceBytes, fixture.files[1].expected.first(sharedPieceBytes));
      test::write(skippedPath, skippedBefore);
    }
    const qint64 wanted = fixture.files[0].expected.size();
    auto metadata = core::Document::parse(test::read(fixture.stateFile).toStdString());
    QVERIFY(metadata && metadata->find("attributes"));
    auto attrs = core::Document::parse(core::encodeValue(*metadata->find("attributes")).value());
    QVERIFY(attrs && attrs->replaceValue("skipfss", "i" + std::to_string(skippedBefore.size()) + "e") &&
        attrs->replaceValue("skipbdl", "i7e") &&
        attrs->replaceValue("filedownloaded", "d10:downloadedli0ei7eee") &&
        metadata->replaceValue("attributes", attrs->serialize()));
    test::write(fixture.stateFile, QByteArray::fromStdString(metadata->serialize()));
    const auto inventoryPath = fixture.profile + QStringLiteral("/downloads.config");
    auto inventory = core::Document::parse(test::read(inventoryPath).toStdString());
    QVERIFY(inventory && inventory->find("downloads") && inventory->find("downloads")->children.size() == 1);
    auto entry = core::Document::parse(core::encodeValue(inventory->find("downloads")->children.front()).value());
    QVERIFY(entry && entry->replaceValue("state", "i70e") &&
        entry->replaceValue("completedbytes", "i7e") &&
        entry->replaceValue("file_priorities", "li1ei0ee") &&
        inventory->replaceValue("downloads", "l" + entry->serialize() + "e"));
    test::write(inventoryPath, QByteArray::fromStdString(inventory->serialize()));

    const auto configuredPort = saveListener(fixture, false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 10000);
    app::SessionBridge bridge;
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy listening(&bridge, &app::SessionBridge::localRpcReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !snapshots.empty(), 10000);
    QVERIFY(bridge.startSavedLocalRpc("xmwebui"));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && listening.size() == 1, 10000);
    const auto port = static_cast<quint16>(listening.last().at(1).toUInt());
    QCOMPARE(int(port), configuredPort);
    const auto credentials = "Authorization: Basic " + QByteArray("generated-user:generated-token").toBase64() + "\r\n";
    const auto request = [&](const QByteArray& json, const QByteArray& session = {}) {
      return "POST /transmission/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
          "\r\n" + credentials + session + "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    };
    const auto id = sessionId(exchange(port, request({})));
    QVERIFY(!id.isEmpty());
    const auto session = "X-Transmission-Session-Id: " + id + "\r\n";
    const auto send = [&](const QByteArray& json) {
      const auto response = exchange(port, request(json, session));
      return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n") + 4)).object();
    };
    const QByteArray progress = R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["sizeWhenDone","leftUntilDone","percentDone"]}})";
    const auto wantedProgress = [&] {
      const auto rows = send(progress).value("arguments").toObject().value("torrents").toArray();
      return rows.isEmpty() ? QJsonObject{} : rows.first().toObject();
    };
    const QJsonObject stopped{{"sizeWhenDone", wanted}, {"leftUntilDone", wanted}, {"percentDone", 0}};
    QCOMPARE(wantedProgress(), stopped);
    const auto validity = [&](const QByteArray& query, const QString& result = QStringLiteral("arguments")) {
      const auto rows = send(query).value(result).toObject().value("torrents").toArray();
      return rows.isEmpty() ? QJsonObject{} : rows.first().toObject();
    };
    const QByteArray selectedValidity = R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["haveValid","haveUnchecked","isFinished","metadataPercentComplete"]}})";
    const QJsonObject stoppedValidity{{"haveValid", 0}, {"haveUnchecked", 0}, {"isFinished", false}, {"metadataPercentComplete", 1}};
    QCOMPARE(validity(selectedValidity), stoppedValidity);
    const auto archivedValidity = validity(
        R"({"jsonrpc":"2.0","id":43,"method":"torrent_get","params":{"ids":[25],"fields":["have_valid","have_unchecked","is_finished","metadata_percent_complete"]}})",
        QStringLiteral("result"));
    const QJsonObject expectedArchive{{"have_valid", 32768}, {"have_unchecked", 0}, {"is_finished", true}, {"metadata_percent_complete", 1}};
    QCOMPARE(archivedValidity, expectedArchive);
    const QByteArray stats = R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["fileStats"]}})";
    const auto readStats = [&](const QByteArray& query = {}) {
      const auto rows = send(query.isEmpty() ? stats : query).value("arguments").toObject().value("torrents").toArray();
      return rows.isEmpty() ? QJsonValue{} : rows.first().toObject().value("fileStats");
    };
    const auto firstStats = [](qint64 bytes) { return QJsonObject{{"bytesCompleted", bytes}, {"wanted", true}, {"priority", 1}}; };
    const auto secondStats = [](qint64 bytes) { return QJsonObject{{"bytesCompleted", bytes}, {"wanted", false}, {"priority", 0}}; };
    QCOMPARE(readStats(), QJsonValue(QJsonArray{firstStats(0), secondStats(7)}));
    const auto archived = readStats(R"({"method":"torrent-get","arguments":{"ids":[25],"fields":["fileStats"],"file-fields":["wanted"]}})");
    QCOMPARE(archived, QJsonValue(QJsonArray{secondStats(0)}));
    QCOMPARE(send(R"({"method":"torrent-start","arguments":{"ids":[37]}})").value("result"), QJsonValue("success"));
    const QJsonObject completed{{"sizeWhenDone", wanted}, {"leftUntilDone", 0}, {"percentDone", 1}};
    QTRY_COMPARE_WITH_TIMEOUT(wantedProgress(), completed, 12000);
    QCOMPARE(readStats(), QJsonValue(QJsonArray{firstStats(wanted), secondStats(sharedPieceBytes)}));
    const QJsonObject activeValidity{{"haveValid", wanted + sharedPieceBytes}, {"haveUnchecked", 0},
        {"isFinished", false}, {"metadataPercentComplete", 1}};
    QCOMPARE(validity(selectedValidity), activeValidity);
    qInfo("PASS original saved bytes, whole verified wanted pieces, and archived completion over RPC");
    const auto filtered = send(R"({"jsonrpc":"2.0","id":65,"method":"torrent_get","params":{"ids":[37],"fields":["file_stats"],"file_fields":["bytes_completed","wanted"]}})");
    QCOMPARE(filtered.value("id"), QJsonValue(65));
    const auto filteredRows = filtered.value("result").toObject().value("torrents").toArray();
    const QJsonArray expectedFiltered{QJsonObject{{"bytes_completed", wanted}, {"wanted", true}},
        QJsonObject{{"bytes_completed", sharedPieceBytes}, {"wanted", false}}};
    QCOMPARE(filteredRows.size(), 1);
    QCOMPARE(filteredRows.first().toObject().value("file_stats"), QJsonValue(expectedFiltered));
    qInfo("PASS original saved, active and archived file statistics in both RPC dialects");
    const auto lib = qvariant_cast<ui::Presentation>(snapshots.last().at(0));
    QVERIFY(lib && lib->items.size() == 1 && lib->items.front().providerAdmission);
    QVERIFY(lib->items.front().bytes && *lib->items.front().bytes > wanted);
    const auto rpc2 = send(R"({"jsonrpc":"2.0","id":59,"method":"torrent_get","params":{"ids":[37],"format":"table","fields":["size_when_done","left_until_done","percent_done"]}})");
    const auto table = rpc2.value("result").toObject().value("torrents");
    QCOMPARE(table, QJsonValue(QJsonArray{QJsonArray{"left_until_done", "percent_done", "size_when_done"},
        QJsonArray{0, 1, wanted}}));
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 12000);
    QCOMPARE(test::read(selectedPath), fixture.files[0].expected);
    QCOMPARE(test::read(skippedPath), skippedBefore);
    qInfo("PASS actual selected payload progress excludes skipped files from both RPC dialects");
  }

  void verifySelectedTorrent_data() {
    QTest::addColumn<bool>("viewOnly");
    QTest::addColumn<bool>("paused");
    QTest::newRow("full-transmission-corrupt-stopped") << false << false;
    QTest::newRow("full-jsonrpc2-corrupt-paused") << false << true;
    QTest::newRow("view-transmission-denied") << true << false;
  }

  void verifySelectedTorrent() {
    QFETCH(bool, viewOnly);
    QFETCH(bool, paused);
    using existing_controls_fixture::Profile;
    Profile fixture(true, false, paused);
    const auto payloadBefore = test::read(fixture.payload);
    const auto stateBefore = test::read(fixture.state);
    auto metadata = core::Document::parse(stateBefore.toStdString());
    const auto* attributes = metadata ? metadata->find("attributes") : nullptr;
    QVERIFY(attributes);
    auto savedAttributes = core::Document::parse(
        stateBefore.mid(attributes->begin, attributes->end - attributes->begin).toStdString());
    QVERIFY(savedAttributes && savedAttributes->replaceValue("Plugin.xmwebui.xmui.dl.id", "i37e") &&
        metadata->replaceValue("attributes", savedAttributes->serialize()));
    test::write(fixture.state, QByteArray::fromStdString(metadata->serialize()));

    const auto configuredPort = reserveRpcPort();
    const auto configPath = fixture.path + QStringLiteral("/biglybt.config");
    auto config = core::Document::parse(test::read(configPath).toStdString());
    QVERIFY(config);
    setListenerFields(*config, configuredPort, viewOnly);
    test::write(configPath, QByteArray::fromStdString(config->serialize()));

    const auto archivePath = fixture.path + QStringLiteral("/dlarchive.config");
    test::write(archivePath, QByteArray("d5:stubsld4:attrd25:Plugin.xmwebui.xmui.dl.idi25ee4:hash20:") +
        QByteArray(20, 'x') + "4:name8:Archivedeee");
    const auto archiveBefore = test::read(archivePath);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy listening(&bridge, &app::SessionBridge::localRpcReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QSignalSpy progress(&bridge, &app::SessionBridge::recheckReady);
    QVERIFY(bridge.openNormal(fixture.path.toStdString()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 15000);
    const auto directory = viewOnly ? "generated-xmwebui" : "xmwebui";
    QVERIFY(bridge.startSavedLocalRpc(directory));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && listening.size() == 1, 10000);
    const auto port = static_cast<quint16>(listening.last().at(1).toUInt());
    QCOMPARE(int(port), configuredPort);
    const auto basic = "Authorization: Basic " + QByteArray("generated-user:generated-token").toBase64() + "\r\n";
    const auto request = [&](const QByteArray& json, const QByteArray& session = {}, bool authenticate = true) {
      return "POST /transmission/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
          "\r\n" + (authenticate ? basic : QByteArray{}) + session + "Content-Length: " +
          QByteArray::number(json.size()) + "\r\n\r\n" + json;
    };
    const QByteArray verify = R"({"method":"torrent-verify","arguments":{"ids":[37]}})";
    QVERIFY(exchange(port, request(verify, {}, false)).startsWith("HTTP/1.1 401 "));
    const auto id = sessionId(exchange(port, request({})));
    QVERIFY(!id.isEmpty());
    const auto session = "X-Transmission-Session-Id: " + id + "\r\n";
    const auto send = [&](const QByteArray& json) {
      const auto response = exchange(port, request(json, session));
      return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n") + 4)).object();
    };
    const auto currentMap = [&]() -> QByteArray {
      const auto document = core::Document::parse(test::read(fixture.state).toStdString());
      const auto* resume = document ? document->find("resume") : nullptr;
      const auto* data = resume ? resume->find("data") : nullptr;
      const auto* bits = data ? data->find("resume data") : nullptr;
      if (!bits || bits->type != core::Node::Type::Bytes) { return {}; }
      return QByteArray(bits->bytes.data(), static_cast<qsizetype>(bits->bytes.size()));
    };
    QVERIFY(currentMap().isEmpty());
    if (viewOnly) {
      QVERIFY(exchange(port, request(verify, session)).startsWith("HTTP/1.1 403 "));
    } else {
      const auto missing = send(R"({"method":"torrent-verify","arguments":{}})");
      QVERIFY(missing.value("result").toString().contains("explicit torrent IDs"));
      const auto archived = send(R"({"method":"torrent-verify","arguments":{"ids":[25,37]}})");
      QVERIFY2(archived.value("result").toString().contains("archived torrent"),
          qPrintable(QJsonDocument(archived).toJson()));
      QVERIFY(currentMap().isEmpty());
      if (paused) {
        const auto result = send(R"({"jsonrpc":"2.0","method":"torrent_verify","id":77,"params":{"ids":[37]}})");
        QVERIFY(result.value("result").isObject());
        QCOMPARE(result.value("id"), QJsonValue(77));
      } else {
        QCOMPARE(send(verify).value("result"), QJsonValue("success"));
      }
      const QByteArray expected("\0\1\1\1", 4);
      QTRY_COMPARE_WITH_TIMEOUT(currentMap(), expected, 15000);
    }
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    QVERIFY(progress.empty());
    QCOMPARE(test::read(fixture.payload), payloadBefore);
    QCOMPARE(test::read(archivePath), archiveBefore);
    const auto lease = storage::ProfileLease::acquire(fixture.path.toStdString());
    QVERIFY(lease);
    const auto library = storage::readLibraryView(*lease);
    QVERIFY(library && library->items.size() == 1);
    QCOMPARE(library->items.front().saved.originalState, std::int64_t{70});
    QVERIFY(!library->items.front().saved.forceStart);
    QCOMPARE(library->pauses.size(), paused ? std::size_t{1} : std::size_t{0});
    if (paused) { QVERIFY(library->pauses.front().forceStart); }
    if (viewOnly) { QVERIFY(currentMap().isEmpty()); }
  }

  void repairSelectedStoppedDuplicate() {
    test::OwnedEngineFixture winner(false);
    test::OwnedEngineFixture successor(true);
    QVERIFY(winner.hash && successor.hash && winner.hash != successor.hash);
    saveId(winner, 37);
    saveId(successor, 37);
    const auto selectedState = winner.profile + QStringLiteral("/active/") +
        QString::fromStdString(successor.hash->hex()) + QStringLiteral(".dat");
    const auto winnerBefore = test::read(winner.stateFile);
    const auto successorBefore = test::read(successor.stateFile);
    test::write(selectedState, successorBefore);
    const auto rowFor = [&](const test::OwnedEngineFixture& fixture, int position, const QString& state) {
      const auto inventory = core::Document::parse(test::read(fixture.profile + "/downloads.config").toStdString());
      const auto* rows = inventory ? inventory->find("downloads") : nullptr;
      test::require(rows && rows->children.size() == 1, "Capture one generated original download");
      auto row = core::Document::parse(core::encodeValue(rows->children.front()).value());
      const auto file = state.toUtf8();
      test::require(row && row->replaceValue("state", "i70e") &&
          row->replaceValue("position", "i" + std::to_string(position) + "e") &&
          row->replaceValue("torrent", std::to_string(file.size()) + ':' + file.toStdString()),
          "Join one generated stopped download to the current original inventory");
      return row->serialize();
    };
    const auto inventory = "d9:downloads" "l" + rowFor(winner, 1, winner.stateFile) +
        rowFor(successor, 2, selectedState) + "ee";
    test::write(winner.profile + "/downloads.config", QByteArray::fromStdString(inventory));
    const auto successorPayload = test::read(successor.payload + '/' + successor.files.front().relative);
    const auto configuredPort = saveListener(winner, false);
    const auto configPath = winner.profile + "/biglybt.config";
    auto settings = core::Document::parse(test::read(configPath).toStdString());
    QVERIFY(settings && settings->replaceValue("Plugin.xmwebui.xmui.next.id", "i60e"));
    test::write(configPath, QByteArray::fromStdString(settings->serialize()));

    QTRY_VERIFY_WITH_TIMEOUT(winner.ready() && successor.ready(), 15000);
    app::SessionBridge bridge;
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy listening(&bridge, &app::SessionBridge::localRpcReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(winner.native(), winner.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !snapshots.empty(), 10000);
    QVERIFY(bridge.startSavedLocalRpc("xmwebui"));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && listening.size() == 1, 10000);
    const auto port = static_cast<quint16>(listening.last().at(1).toUInt());
    QCOMPARE(int(port), configuredPort);
    const auto credentials = "Authorization: Basic " + QByteArray("generated-user:generated-token").toBase64() + "\r\n";
    const auto request = [&](const QByteArray& json, const QByteArray& session = {}) {
      return "POST /transmission/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
          "\r\n" + credentials + session + "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    };
    const auto token = sessionId(exchange(port, request({})));
    QVERIFY(!token.isEmpty());
    const auto session = "X-Transmission-Session-Id: " + token + "\r\n";
    const auto get = [&](QJsonArray selection) {
      const auto json = QJsonDocument(QJsonObject{{"method", "torrent-get"},
          {"arguments", QJsonObject{{"ids", std::move(selection)}, {"fields", QJsonArray{"id", "hashString"}}}}})
          .toJson(QJsonDocument::Compact);
      const auto response = exchange(port, request(json, session));
      return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n") + 4)).object();
    };
    const auto first = QString::fromStdString(winner.hash->hex());
    const auto second = QString::fromStdString(successor.hash->hex());
    QVERIFY(get({first}).value("result").toString().contains("duplicate RPC identities"));
    QCOMPARE(test::read(winner.stateFile), winnerBefore);
    QCOMPARE(test::read(selectedState), successorBefore);
    const auto selected = get({second});
    QCOMPARE(selected.value("result"), QJsonValue("success"));
    QCOMPARE(selected.value("arguments").toObject().value("torrents"),
        QJsonValue(QJsonArray{QJsonObject{{"id", 60}, {"hashString", second}}}));
    const auto both = get({37, 60});
    QCOMPARE(both.value("arguments").toObject().value("torrents"),
        QJsonValue(QJsonArray{QJsonObject{{"id", 37}, {"hashString", first}},
            QJsonObject{{"id", 60}, {"hashString", second}}}));
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 12000);
    QCOMPARE(test::read(winner.stateFile), winnerBefore);
    QCOMPARE(test::read(successor.payload + '/' + successor.files.front().relative), successorPayload);
    const auto lease = storage::ProfileLease::acquire(winner.native());
    QVERIFY(lease);
    const auto profile = storage::readRpcProfileSnapshot(*lease, "xmwebui");
    QVERIFY(profile && profile->identities.size() == 2);
    QCOMPARE(profile->identities[0].id, std::optional<std::int64_t>{37});
    QCOMPARE(profile->identities[1].id, std::optional<std::int64_t>{60});
    QCOMPARE(profile->nextId, std::optional<std::int64_t>{61});
    qInfo("PASS selected stopped duplicate RPC successor repair preserves the first original ID");
  }

  void allocateRpcIdentity_data() {
    QTest::addColumn<bool>("viewOnly");
    QTest::addColumn<bool>("archived");
    QTest::addColumn<int>("counter");
    QTest::addColumn<bool>("cached");
    QTest::addColumn<bool>("duplicateCache");
    QTest::addColumn<int>("archiveCollision");
    QTest::addColumn<bool>("archiveWinner");
    QTest::addColumn<bool>("multifile");
    QTest::newRow("stopped-older-counter") << false << false << 40 << false << false << 0 << false << false;
    QTest::newRow("stopped-reserved-counter") << false << false << 60 << false << false << 0 << false << false;
    QTest::newRow("stopped-view-mode") << true << false << 60 << false << false << 0 << false << false;
    QTest::newRow("archived-older-counter") << false << true << 29 << false << false << 0 << false << false;
    QTest::newRow("archived-hidden-in-view") << true << true << 60 << false << false << 0 << false << false;
    QTest::newRow("stopped-authoritative-cache") << false << false << 40 << true << false << 0 << false << false;
    QTest::newRow("stopped-cache-view-mode") << true << false << 60 << true << false << 0 << false << false;
    QTest::newRow("stopped-duplicate-cache") << false << false << 40 << true << true << 0 << false << false;
    QTest::newRow("archive-duplicates-active") << false << true << 29 << false << false << 37 << false << false;
    QTest::newRow("archive-duplicates-archive-selected-winner") << false << true << 60 << false << false << 25 << true << false;
    QTest::newRow("archive-duplicates-hidden-in-view") << true << true << 60 << false << false << 37 << false << false;
    QTest::newRow("multifile-current-location") << false << false << 40 << false << false << 0 << false << true;
  }

  void allocateRpcIdentity() {
    QFETCH(bool, viewOnly);
    QFETCH(bool, archived);
    QFETCH(int, counter);
    QFETCH(bool, cached);
    QFETCH(bool, duplicateCache);
    QFETCH(int, archiveCollision);
    QFETCH(bool, archiveWinner);
    QFETCH(bool, multifile);
    test::OwnedEngineFixture fixture(multifile);
    std::vector<QByteArray> payloadBefore;
    for (const auto& file : fixture.files) { payloadBefore.push_back(test::read(fixture.payload + '/' + file.relative)); }
    saveId(fixture, archived ? 37 : (cached ? 88 : 0));
    saveArchive(fixture, archived ? archiveCollision : 41);
    {
      const auto preparation = storage::ProfileLease::acquire(fixture.native());
      QVERIFY(preparation && fixture.hash);
      const auto stopped = storage::applyLibraryCommand(*preparation, *fixture.hash, core::DownloadAction::Stop, {});
      QVERIFY(stopped && stopped->after.startup == core::StartupState::Stopped);
    }
    const auto configuredPort = saveListener(fixture, viewOnly);
    const auto directory = viewOnly ? std::string("generated-xmwebui") : std::string("xmwebui");
    const auto configPath = fixture.profile + QStringLiteral("/biglybt.config");
    auto config = core::Document::parse(test::read(configPath).toStdString());
    QVERIFY(config && config->replaceValue("Plugin." + directory + ".xmui.next.id",
        "i" + std::to_string(counter) + "e"));
    test::write(configPath, QByteArray::fromStdString(config->serialize()));
    const auto cachePath = fixture.profile + QStringLiteral("/active/cache.dat");
    QByteArray originalCache;
    if (cached) {
      QVERIFY(fixture.hash);
      saveCachedIdentity(fixture, cachePath, duplicateCache);
      originalCache = test::read(cachePath);
      const auto preparation = storage::ProfileLease::acquire(fixture.native());
      QVERIFY(preparation);
      const auto snapshot = storage::readRpcProfileSnapshot(*preparation, directory);
      QVERIFY(snapshot && snapshot->issues.empty() && !snapshot->library.cacheWarning && snapshot->library.cacheIssues.empty());
      QCOMPARE(snapshot->identities.front().origin, storage::RpcIdOrigin::Cache);
      QCOMPARE(snapshot->identities.front().id, std::optional<std::int64_t>{0});
    }

    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 15000);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy listening(&bridge, &app::SessionBridge::localRpcReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.isEmpty(), 10000);
    const auto library = qvariant_cast<ui::Presentation>(rows.last().at(0));
    QVERIFY(library && library->items.size() == 1);
    const auto named = QStringLiteral("6161616161616161616161616161616161616161");
    const auto unnamed = QStringLiteral("6262626262626262626262626262626262626262");
    const auto hash = archived ? (archiveWinner ? named : unnamed) : library->items.front().hash;
    QVERIFY(bridge.startSavedLocalRpc(directory));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && listening.size() == 1, 10000);
    const auto port = static_cast<quint16>(listening.last().at(1).toUInt());
    QCOMPARE(int(port), configuredPort);
    const auto originalState = test::read(fixture.stateFile);
    const auto archive = test::read(fixture.profile + QStringLiteral("/dlarchive.config"));
    const auto basic = "Authorization: Basic " + QByteArray("generated-user:generated-token").toBase64() + "\r\n";
    const auto request = [&](const QByteArray& json, const QByteArray& session = {}) {
      return "POST /transmission/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
          "\r\n" + basic + session + "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    };
    const auto idHeader = sessionId(exchange(port, request({})));
    QVERIFY(!idHeader.isEmpty());
    const auto session = "X-Transmission-Session-Id: " + idHeader + "\r\n";
    const auto get = [&](QJsonArray select, QJsonArray fields = {"id", "hashString"}) {
      const auto json = QJsonDocument(QJsonObject{{"method", "torrent-get"},
          {"arguments", QJsonObject{{"ids", std::move(select)}, {"fields", std::move(fields)}}}})
          .toJson(QJsonDocument::Compact);
      const auto http = exchange(port, request(json, session));
      return QJsonDocument::fromJson(http.mid(http.indexOf("\r\n\r\n") + 4)).object();
    };
    const auto allocated = get({hash});
    const auto issued = std::max(archived ? 38 : 42, counter);
    if (viewOnly && archived) {
      QCOMPARE(allocated.value("result"), QJsonValue("success"));
      QCOMPARE(allocated.value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{}));
    } else if (viewOnly || duplicateCache) {
      QVERIFY(allocated.value("result") != QJsonValue("success"));
      QCOMPARE(test::read(fixture.stateFile), originalState);
    } else {
      QCOMPARE(allocated.value("result"), QJsonValue("success"));
      const auto returned = archiveWinner ? 25 : issued;
      const auto expected = QJsonArray{QJsonObject{{"id", returned}, {"hashString", hash}}};
      QCOMPARE(allocated.value("arguments").toObject().value("torrents"), QJsonValue(expected));
      QCOMPARE(get({hash}), allocated);
      QCOMPARE(get({returned}), allocated);
      if (cached || multifile) {
        const auto timestamps = get({returned}, {"addedDate"});
        QCOMPARE(timestamps.value("result"), QJsonValue("success"));
        QCOMPARE(timestamps.value("arguments").toObject().value("torrents"),
            QJsonValue(QJsonArray{QJsonObject{{"addedDate", cached ? 1700000654 : 1700000432}}}));
      }
      if (multifile) {
        const auto directory = get({returned}, {"downloadDir"});
        QCOMPARE(directory.value("result"), QJsonValue("success"));
        QCOMPARE(directory.value("arguments").toObject().value("torrents"),
            QJsonValue(QJsonArray{QJsonObject{{"downloadDir", fixture.payload + QStringLiteral("/bundle")}}}));
        qInfo("PASS original multifile data root and stored addition time over local RPC");
      }
      if (archiveCollision) {
        const auto repair = get({issued}).value("arguments").toObject().value("torrents");
        QCOMPARE(repair, QJsonValue(QJsonArray{QJsonObject{{"id", issued}, {"hashString", unnamed}}}));
        const auto active = get({37}).value("arguments").toObject().value("torrents");
        QCOMPARE(active, QJsonValue(QJsonArray{QJsonObject{{"id", 37}, {"hashString", library->items.front().hash}}}));
        qInfo("PASS archived RPC duplicate successor repair retains the first original identity");
      }
    }
    const auto archiveAfter = test::read(fixture.profile + QStringLiteral("/dlarchive.config"));
    if (archived && !viewOnly) {
      const auto before = core::Document::parse(archive.toStdString());
      const auto after = core::Document::parse(archiveAfter.toStdString());
      QVERIFY(before && after && before->find("stubs") && after->find("stubs"));
      const auto& original = before->find("stubs")->children.front();
      const auto& preserved = after->find("stubs")->children.front();
      QCOMPARE(archive.mid(original.begin, original.end - original.begin),
          archiveAfter.mid(preserved.begin, preserved.end - preserved.begin));
      QCOMPARE(test::read(fixture.stateFile), originalState);
    } else {
      QCOMPARE(archiveAfter, archive);
    }
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 20000);
    const auto reopened = storage::ProfileLease::acquire(fixture.native());
    QVERIFY(reopened);
    const auto snapshot = storage::readRpcProfileSnapshot(*reopened, directory);
    QVERIFY(snapshot && snapshot->identities.size() == 3 && snapshot->nextId && snapshot->identities.front().id);
    QCOMPARE(*snapshot->nextId, qint64(viewOnly || duplicateCache ? counter : issued + 1));
    const qint64 expectedActive = archived ? 37 : (viewOnly || duplicateCache ? 0 : issued);
    const qint64 expectedArchive = archived ? (viewOnly ? archiveCollision : issued) : 41;
    QCOMPARE(*snapshot->identities.front().id, expectedActive);
    QVERIFY(snapshot->identities.back().id);
    QCOMPARE(*snapshot->identities.back().id, expectedArchive);
    if (cached && (viewOnly || duplicateCache)) {
      QCOMPARE(snapshot->identities.front().origin, storage::RpcIdOrigin::Cache);
      QCOMPARE(test::read(cachePath), originalCache);
      if (duplicateCache) { qInfo("PASS duplicate original cache identity refuses mutation before allocating its counter"); }
    } else if (cached) {
      QCOMPARE(snapshot->identities.front().origin, storage::RpcIdOrigin::Primary);
      const auto primary = core::Document::parse(test::read(fixture.stateFile).toStdString());
      QVERIFY(primary && primary->find("attributes") && primary->find("opaque.fixture"));
      const auto* opaque = primary->find("attributes")->find("opaque.cache");
      QVERIFY(opaque && opaque->bytes == "retain-this");
      const auto prior = storage::EncodedDocument::parse(originalCache.toStdString());
      const auto current = storage::EncodedDocument::parse(test::read(cachePath).toStdString());
      QVERIFY(prior && prior->compressed() && current && current->compressed());
      const auto& preimage = prior->document();
      const auto& postimage = current->document();
      const auto* oldRows = preimage.find("state");
      const auto* newRows = postimage.find("state");
      QVERIFY(oldRows && oldRows->children.size() == 2 && newRows && newRows->children.size() == 1);
      const auto& oldNeighbor = oldRows->children.back();
      const auto& newNeighbor = newRows->children.front();
      QCOMPARE(preimage.originalBytes().substr(oldNeighbor.begin, oldNeighbor.end - oldNeighbor.begin),
          postimage.originalBytes().substr(newNeighbor.begin, newNeighbor.end - newNeighbor.begin));
      QVERIFY(postimage.find("opaque") && postimage.find("opaque")->bytes == "preserve");
      qInfo("PASS authoritative original cache identity hydrates primary and preserves neighboring cache bytes");
    }
    for (std::size_t index = 0; index < fixture.files.size(); ++index) {
      const auto& file = fixture.files[index];
      const auto path = fixture.payload + '/' + file.relative;
      struct stat current{};
      QVERIFY(::stat(path.toStdString().c_str(), &current) == 0);
      QCOMPARE(current.st_dev, file.before.st_dev);
      QCOMPARE(current.st_ino, file.before.st_ino);
      QCOMPARE(test::read(path), payloadBefore[index]);
    }
  }

  void refuseAmbiguousArchiveRepair() {
    test::OwnedEngineFixture fixture(false);
    saveId(fixture, 37);
    saveArchive(fixture, 37);
    const auto path = fixture.profile + QStringLiteral("/dlarchive.config");
    const auto original = test::read(path).toStdString();
    auto archive = core::Document::parse(original);
    QVERIFY(archive);
    const auto* stubs = archive->find("stubs");
    QVERIFY(stubs && stubs->children.size() == 2);
    const auto bytes = [&](std::size_t index) {
      const auto& row = stubs->children.at(index);
      return original.substr(row.begin, row.end - row.begin);
    };
    auto selected = core::Document::parse(bytes(0));
    auto duplicate = core::Document::parse(bytes(1));
    QVERIFY(selected && duplicate &&
        selected->replaceValue("attr", "d25:Plugin.xmwebui.xmui.dl.idi0ee") &&
        duplicate->replaceValue("attr", "d25:Plugin.xmwebui.xmui.dl.idi90ee"));
    QVERIFY(archive->replaceValue("stubs", "l" + selected->serialize() + bytes(1) + duplicate->serialize() + "e"));
    const auto ambiguous = QByteArray::fromStdString(archive->serialize());
    test::write(path, ambiguous);
    const auto config = fixture.profile + QStringLiteral("/biglybt.config");
    auto settings = core::Document::parse("de");
    QVERIFY(settings && settings->replaceValue("Plugin.xmwebui.xmui.next.id", "i41e"));
    test::write(config, QByteArray::fromStdString(settings->serialize()));
    const auto originalConfig = test::read(config);
    const auto lease = storage::ProfileLease::acquire(fixture.native());
    const auto hash = core::TorrentHash::parse(std::string(20, 'a'));
    QVERIFY(lease && hash);
    const auto repaired = storage::allocateRpcIdentity(*lease, "xmwebui", *hash, storage::RpcIdentityMember::Archived);
    QVERIFY(!repaired && repaired.error().code == storage::ErrorCode::Conflict &&
        repaired.error().message == "A duplicate RPC identity has an ambiguous torrent hash");
    QCOMPARE(test::read(path), ambiguous);
    QCOMPARE(test::read(config), originalConfig);
  }

  void setTorrentRates_data() {
    QTest::addColumn<bool>("viewOnly");
    QTest::newRow("full-with-stopped-and-retained-controls") << false;
    QTest::newRow("view-denied") << true;
  }

  void setTorrentRates() {
    QFETCH(bool, viewOnly);
    test::OwnedEngineFixture fixture(false);
    for (const auto& file : fixture.files) { test::write(fixture.payload + '/' + file.relative, file.expected); }
    saveId(fixture, 37);
    {
      const auto setup = storage::ProfileLease::acquire(fixture.native());
      QVERIFY(setup && fixture.hash);
      const auto stopped = storage::applyLibraryCommand(*setup, *fixture.hash, core::DownloadAction::Stop, {});
      QVERIFY(stopped && stopped->after.startup == core::StartupState::Stopped);
    }
    const auto configPath = fixture.profile + "/downloads.config";
    const auto before = test::read(configPath);
    const auto configuredPort = saveListener(fixture, viewOnly);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 15000);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy listening(&bridge, &app::SessionBridge::localRpcReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.empty(), 10000);
    QVERIFY(bridge.startSavedLocalRpc(viewOnly ? "generated-xmwebui" : "xmwebui"));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && listening.size() == 1, 10000);
    const auto port = static_cast<quint16>(listening.last().at(1).toUInt());
    QCOMPARE(int(port), configuredPort);
    const auto credentials = "Authorization: Basic " + QByteArray("generated-user:generated-token").toBase64() + "\r\n";
    const auto request = [&](const QByteArray& json, const QByteArray& session = {}) {
      return "POST /transmission/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
          "\r\n" + credentials + session + "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    };
    const auto token = sessionId(exchange(port, request({})));
    QVERIFY(!token.isEmpty());
    const auto session = "X-Transmission-Session-Id: " + token + "\r\n";
    const auto rpc = [&](const QByteArray& json) {
      const auto answer = exchange(port, request(json, session));
      return QJsonDocument::fromJson(answer.mid(answer.indexOf("\r\n\r\n") + 4)).object();
    };
    const QByteArray set = R"({"method":"torrent-set","arguments":{"ids":[37],"downloadLimit":12345,"downloadLimited":true,"uploadLimit":8192}})";
    const auto rawLimits = [&] {
      auto doc = core::Document::parse(test::read(configPath).toStdString());
      const auto* list = doc ? doc->find("downloads") : nullptr;
      if (!list || list->children.size() != 1) { return std::array<std::int64_t, 2>{-2, -2}; }
      const auto& row = list->children.front();
      const auto limit = [&](std::string_view key) {
        const auto* value = row.find(key);
        return value && value->type == core::Node::Type::Integer ? value->integer : std::int64_t{0};
      };
      return std::array{limit("maxdl"), limit("maxul")};
    };
    if (viewOnly) {
      QCOMPARE(exchange(port, request(set, session)).left(12), QByteArray("HTTP/1.1 403"));
      QCOMPARE(test::read(configPath), before);
    } else {
      auto unsafe = request(set, session);
      unsafe.replace(credentials, "");
      QCOMPARE(exchange(port, unsafe).left(12), QByteArray("HTTP/1.1 401"));
      const auto unsupported = rpc(R"({"method":"torrent-set","arguments":{"ids":[37],"downloadLimit":2147483648,"downloadLimited":true}})");
      QVERIFY(unsupported.value("result").toString().contains("supported integer"));
      QCOMPARE(test::read(configPath), before);
      QCOMPARE(rpc(set).value("result"), QJsonValue("success"));
      QCOMPARE(rawLimits(), (std::array<std::int64_t, 2>{12345, 0}));
      const auto cleared = rpc(R"({"jsonrpc":"2.0","method":"torrent_set","id":83,"params":{"ids":[37],"download_limited":false,"upload_limit":8192,"upload_limited":true}})");
      QCOMPARE(cleared.value("id"), QJsonValue(83));
      QCOMPARE(cleared.value("result"), QJsonValue(QJsonObject{}));
      QCOMPARE(rawLimits(), (std::array<std::int64_t, 2>{0, 8192}));
      const auto values = rpc(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["downloadLimit","downloadLimited","uploadLimit","uploadLimited"]}})");
      const QJsonObject expected{{"downloadLimit", 0}, {"downloadLimited", false}, {"uploadLimit", 8}, {"uploadLimited", true}};
      QCOMPARE(values.value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{expected}));
      QCOMPARE(rpc(R"({"method":"torrent-start-now","arguments":{"ids":[37]}})").value("result"), QJsonValue("success"));
      QTRY_VERIFY_WITH_TIMEOUT(qvariant_cast<ui::Presentation>(rows.last().at(0))->items.front().providerAdmission, 10000);
      const auto retained = rpc(set);
      QVERIFY(retained.value("result").toString().contains("Stop the torrent") ||
          retained.value("result").toString().contains("stop and retire"));
      QCOMPARE(rawLimits(), (std::array<std::int64_t, 2>{0, 8192}));
      qInfo("PASS original stopped torrent rate writes in bytes per second over both RPC dialects");
    }
    QVERIFY(bridge.closeSession());
    QTRY_VERIFY_WITH_TIMEOUT(closed.size() == 1, 15000);
    if (!viewOnly) { QCOMPARE(rawLimits(), (std::array<std::int64_t, 2>{0, 8192})); }
    for (const auto& file : fixture.files) { QCOMPARE(test::read(fixture.payload + '/' + file.relative), file.expected); }
  }

  void reportCommittedRemoval_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("desktop-approval") << 0;
    QTest::newRow("transmission-full") << 1;
    QTest::newRow("jsonrpc2-full-retained") << 2;
    QTest::newRow("transmission-view") << 3;
  }

  void reportCommittedRemoval() {
    QFETCH(int, mode);
    const bool retained = mode == 2;
    const bool view = mode == 3;
    test::OwnedEngineFixture fixture(false);
    if (retained) {
      for (const auto& file : fixture.files) { test::write(fixture.payload + '/' + file.relative, file.expected); }
    }
    std::vector<QByteArray> payloadBefore;
    for (const auto& file : fixture.files) { payloadBefore.push_back(test::read(fixture.payload + '/' + file.relative)); }
    saveId(fixture, 37);
    saveArchive(fixture);
    const auto archivePath = fixture.profile + QStringLiteral("/dlarchive.config");
    const auto archiveBytes = test::read(archivePath);
    const auto configuredPort = saveListener(fixture, view);
    {
      const auto preparation = storage::ProfileLease::acquire(fixture.native());
      QVERIFY(preparation && fixture.hash);
      const auto stopped = storage::applyLibraryCommand(*preparation, *fixture.hash, core::DownloadAction::Stop, {});
      QVERIFY(stopped && stopped->after.startup == core::StartupState::Stopped);
    }

    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 15000);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy listening(&bridge, &app::SessionBridge::localRpcReady);
    QSignalSpy plans(&bridge, &app::SessionBridge::fileOperationPrepared);
    QSignalSpy operations(&bridge, &app::SessionBridge::fileOperationFinished);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.isEmpty() && sessions.size() == 1, 10000);
    QVERIFY(bridge.startSavedLocalRpc(view ? "generated-xmwebui" : "xmwebui"));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && listening.size() == 1, 10000);
    const auto port = static_cast<quint16>(listening.last().at(1).toUInt());
    QCOMPARE(int(port), configuredPort);

    const auto auth = "Authorization: Basic " + QByteArray("generated-user:generated-token").toBase64() + "\r\n";
    const auto request = [&](const QByteArray& json, const QByteArray& headers = {}) {
      return "POST /transmission/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
          "\r\n" + auth + headers + "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    };
    const auto token = sessionId(exchange(port, request({})));
    QVERIFY(!token.isEmpty());
    const auto session = "X-Transmission-Session-Id: " + token + "\r\n";
    const auto rpc = [&](const QByteArray& body) {
      const auto answer = exchange(port, request(body, session));
      return QJsonDocument::fromJson(answer.mid(answer.indexOf("\r\n\r\n") + 4)).object();
    };
    const QByteArray recent = R"({"method":"torrent-get","arguments":{"ids":"recently-active","fields":["id"]}})";
    const auto original = rpc(recent).value("arguments").toObject();
    QJsonArray originalRows{QJsonObject{{"id", 37}}};
    if (!view) {
      originalRows.push_back(QJsonObject{{"id", 25}});
      originalRows.push_back(QJsonObject{{"id", 41}});
    }
    QCOMPARE(original.value("torrents"), QJsonValue(originalRows));
    QVERIFY(!original.contains("removed"));

    const QByteArray removal = R"({"method":"torrent-remove","arguments":{"ids":[37],"delete-local-data":false}})";
    if (view) {
      QCOMPARE(exchange(port, request(removal, session)).left(12), QByteArray("HTTP/1.1 403"));
      QVERIFY(!rpc(recent).value("arguments").toObject().contains("removed"));
      QVERIFY(QFileInfo::exists(fixture.stateFile));
      QCOMPARE(test::read(archivePath), archiveBytes);
      for (std::size_t index = 0; index < fixture.files.size(); ++index) {
        QCOMPARE(test::read(fixture.payload + '/' + fixture.files[index].relative), payloadBefore[index]);
      }
      QVERIFY(bridge.closeSession());
      QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 20000);
      return;
    }
    if (mode == 0) {
      const auto review = [&](quint64 id) {
        const auto library = qvariant_cast<ui::Presentation>(rows.last().at(0));
        const ui::Target target{library->items.front().hash, library->revision};
        return ui::FileOperationRequest{sessions[0][0].toULongLong(), id, library->revision, target,
            ui::FileOperation::RemoveKeepData, {}, false};
      };
      QVERIFY(bridge.prepareFileOperation(review(1)));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && plans.size() == 1, 10000);
      const auto first = qvariant_cast<ui::FileOperationPlanPresentation>(plans.last().at(0));
      QVERIFY2(first && first->failure.isEmpty(), first ? qPrintable(first->failure) : "missing first plan");
      auto rejected = std::make_shared<ui::FileOperationPlanPacket>(*first);
      rejected->approvalScopeDigest += 'x';
      QVERIFY(bridge.executeFileOperation({rejected, false, false}));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && operations.size() == 1, 10000);
      QVERIFY(!qvariant_cast<ui::FileOperationResultPresentation>(operations.last().at(0))->completed);
      QVERIFY(!rpc(recent).value("arguments").toObject().contains("removed"));

      QVERIFY(bridge.prepareFileOperation(review(2)));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && plans.size() == 2, 10000);
      const auto approved = qvariant_cast<ui::FileOperationPlanPresentation>(plans.last().at(0));
      QVERIFY2(approved && approved->failure.isEmpty(), approved ? qPrintable(approved->failure) : "missing final plan");
      QVERIFY(bridge.executeFileOperation({approved, false, false}));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && operations.size() == 2, 15000);
      const auto finished = qvariant_cast<ui::FileOperationResultPresentation>(operations.last().at(0));
      QVERIFY2(finished && finished->completed, finished ? qPrintable(finished->failure) : "missing result");
    } else {
      auto unauthenticated = request(removal, session);
      unauthenticated.replace(auth, "");
      QCOMPARE(exchange(port, unauthenticated).left(12), QByteArray("HTTP/1.1 401"));
      const auto deleteData = rpc(R"({"method":"torrent-remove","arguments":{"ids":[37],"delete-local-data":true}})");
      QVERIFY(deleteData.value("result").toString().contains("keeping the local data"));
      const auto archived = rpc(R"({"method":"torrent-remove","arguments":{"ids":[25]}})");
      QVERIFY(archived.value("result").toString().contains("Restore"));
      QVERIFY(!rpc(recent).value("arguments").toObject().contains("removed"));
      if (retained) {
        QCOMPARE(rpc(R"({"method":"torrent-start-now","arguments":{"ids":[37]}})").value("result"), QJsonValue("success"));
        QTRY_VERIFY_WITH_TIMEOUT(qvariant_cast<ui::Presentation>(rows.last().at(0))->items.front().providerAdmission, 10000);
        const auto removed = rpc(R"({"jsonrpc":"2.0","id":87,"method":"torrent_remove","params":{"ids":[37],"delete_local_data":false}})");
        QCOMPARE(removed.value("id"), QJsonValue(87));
        QCOMPARE(removed.value("result"), QJsonValue(QJsonObject{}));
      } else {
        QCOMPARE(rpc(R"({"method":"torrent-remove","arguments":{"ids":[37]}})").value("result"), QJsonValue("success"));
      }
      qInfo("PASS authenticated RPC Keep Data removes the original record and queues one committed receipt");
    }
    const auto regular = rpc(R"({"method":"torrent-get","arguments":{"fields":["id"]}})").value("arguments").toObject();
    QCOMPARE(regular.value("torrents"), QJsonValue(QJsonArray{QJsonObject{{"id", 25}}, QJsonObject{{"id", 41}}}));
    QVERIFY(!regular.contains("removed"));
    const auto delta = rpc(R"({"jsonrpc":"2.0","id":52,"method":"torrent_get","params":{"ids":"recently_active","fields":["id"]}})").value("result").toObject();
    QCOMPARE(delta.value("torrents"), QJsonValue(QJsonArray{}));
    QCOMPARE(delta.value("removed"), QJsonValue(QJsonArray{37}));
    QVERIFY(!rpc(recent).value("arguments").toObject().contains("removed"));

    const auto saved = core::Document::parse(test::read(fixture.profile + QStringLiteral("/downloads.config")).toStdString());
    QVERIFY(saved);
    const auto library = core::readLibrary(*saved);
    QVERIFY(library && library->downloads.empty());
    QVERIFY(!QFileInfo::exists(fixture.stateFile));
    QCOMPARE(test::read(archivePath), archiveBytes);
    for (std::size_t index = 0; index < fixture.files.size(); ++index) {
      QCOMPARE(test::read(fixture.payload + '/' + fixture.files[index].relative), payloadBefore[index]);
    }
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 20000);
    qInfo("PASS committed Keep Data removal reported once and rejected approval emits nothing");
  }

  void actualProfileDispatch_data() {
    QTest::addColumn<bool>("saved");
    QTest::addColumn<bool>("viewOnly");
    QTest::newRow("explicit-credentials") << false << false;
    QTest::newRow("original-profile-menu") << true << false;
    QTest::newRow("original-view-mode") << true << true;
  }

  void actualProfileDispatch() {
    QFETCH(bool, saved);
    QFETCH(bool, viewOnly);
    test::OwnedEngineFixture fixture(false);
    saveId(fixture, 37);
    saveArchive(fixture);
    saveTorrentTransferPrefix(fixture, viewOnly);
    if (saved && !viewOnly) {
      const auto preparation = storage::ProfileLease::acquire(fixture.native());
      QVERIFY(preparation && fixture.hash);
      const auto stopped = storage::applyLibraryCommand(*preparation, *fixture.hash, core::DownloadAction::Stop, {});
      QVERIFY(stopped && stopped->after.startup == core::StartupState::Stopped);
    }
    const auto configuredPort = saved ? saveListener(fixture, viewOnly) : 0;
    if (!saved) {
      auto settings = core::Document::parse("de");
      QVERIFY(settings);
      addRemoteProfileFields(fixture, *settings, false);
      test::write(fixture.profile + QStringLiteral("/biglybt.config"), QByteArray::fromStdString(settings->serialize()));
    }
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 15000);
    app::SessionBridge bridge;
    ui::LibraryWindow window;
    app::bindLocalRpcControls(window, bridge);
    connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &window, &ui::LibraryWindow::beginColumnLayoutSession);
    connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy endpoints(&bridge, &app::SessionBridge::localRpcReady);
    QSignalSpy overall(&bridge, &app::SessionBridge::overallReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.isEmpty(), 10000);
    const auto library = qvariant_cast<ui::Presentation>(rows.last().at(0));
    QVERIFY(library && library->items.size() == 1);
    QVERIFY(!bridge.startLocalRpc(0, "", "generated-token"));
    if (viewOnly) {
      QVERIFY(bridge.startSavedLocalRpc("generated-xmwebui"));
    } else if (saved) {
      auto* action = window.findChild<QAction*>("classic-local-rpc");
      QVERIFY(action);
      action->trigger();
    } else {
      QVERIFY(bridge.startLocalRpc(0, "generated-user", "generated-token"));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && endpoints.size() == 1, 10000);
    const auto port = static_cast<quint16>(endpoints.last().at(1).toUInt());
    QVERIFY(port);
    if (saved) { QCOMPARE(int(port), configuredPort); }
    const auto basic = "Authorization: Basic " + QByteArray("generated-user:generated-token").toBase64() + "\r\n";
    const auto request = [&](const QByteArray& json, const QByteArray& headers = {}) {
      return "POST /transmission/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
          "\r\n" + basic + headers + "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    };
    const auto challenge = exchange(port, request({}));
    QVERIFY(challenge.startsWith("HTTP/1.1 409 "));
    const auto session = sessionId(challenge);
    QVERIFY(!session.isEmpty());
    const auto hasSession = "X-Transmission-Session-Id: " + session + "\r\n";
    const auto answer = exchange(port, request(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["id","name","hashString","totalSize"]},"tag":4})", hasSession));
    QVERIFY(answer.startsWith("HTTP/1.1 200 "));
    const auto result = QJsonDocument::fromJson(answer.mid(answer.indexOf("\r\n\r\n") + 4)).object();
    QCOMPARE(result.value("result").toString(), QStringLiteral("success"));
    QCOMPARE(result.value("tag").toInt(), 4);
    const auto returned = result.value("arguments").toObject().value("torrents").toArray();
    QCOMPARE(returned.size(), 1);
    const auto torrent = returned.first().toObject();
    QCOMPARE(torrent.value("id").toInteger(), qint64{37});
    QCOMPARE(torrent.value("name").toString(), library->items.front().name);
    QCOMPARE(torrent.value("hashString").toString(), QString::fromStdString(fixture.hash->hex()));
    QCOMPARE(torrent.value("totalSize").toInteger(), qint64{test::OwnedEngineFixture::PayloadBytes});
    const auto rpcResult = [&](const QByteArray& json) {
      const auto wire = exchange(port, request(json, hasSession));
      test::require(wire.startsWith("HTTP/1.1 200 "), "Read authenticated RPC data");
      return QJsonDocument::fromJson(wire.mid(wire.indexOf("\r\n\r\n") + 4)).object();
    };
    const auto metainfo = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["comment","creator","dateCreated","file-count","isPrivate","pieceCount","pieceSize"]}})");
    QCOMPARE(metainfo.value("result").toString(), QStringLiteral("success"));
    const auto metadata = metainfo.value("arguments").toObject().value("torrents").toArray().first().toObject();
    QCOMPARE(metadata, (QJsonObject{{"comment", "Generated comment"}, {"creator", "RPC fixture"}, {"dateCreated", 1700000106},
        {"file-count", 1}, {"isPrivate", false}, {"pieceCount", 8}, {"pieceSize", 16384}}));
    const auto metainfo2 = rpcResult(R"({"jsonrpc":"2.0","id":12,"method":"torrent_get","params":{"ids":[37],"format":"table","fields":["piece_size","date_created","is_private","file_count"]}})");
    QCOMPARE(metainfo2.value("id").toInt(), 12);
    const auto metadataTable = metainfo2.value("result").toObject().value("torrents").toArray();
    QCOMPARE(metadataTable, (QJsonArray{QJsonArray{"date_created", "file_count", "is_private", "piece_size"},
        QJsonArray{1700000106, 1, false, 16384}}));
    const auto currentStorage = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["addedDate","downloadDir"]}})");
    const QJsonObject originalStorage{{"addedDate", 1700000432}, {"downloadDir", fixture.payload}};
    QCOMPARE(currentStorage.value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{originalStorage}));
    const auto archivedStorage = rpcResult(R"({"jsonrpc":"2.0","id":58,"method":"torrent_get","params":{"ids":[25,41],"format":"table","fields":["download_dir","added_date"]}})");
    const auto archivedStorageRows = archivedStorage.value("result").toObject().value("torrents").toArray();
    if (viewOnly) { QVERIFY(archivedStorageRows.empty()); }
    else {
      QCOMPARE(archivedStorageRows, (QJsonArray{QJsonArray{"added_date", "download_dir"},
          QJsonArray{1700000123, fixture.payload + QStringLiteral("/archived.bin")},
          QJsonArray{0, QJsonValue(QJsonValue::Null)}}));
      const auto unnamed = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[41],"fields":["addedDate","downloadDir"]}})");
      QCOMPARE(unnamed.value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{QJsonObject{{"addedDate", 0}}}));
      qInfo("PASS original single-file directory, archive location and added dates in both RPC dialects");
    }
    const auto settings = rpcResult(R"({"method":"session-get","arguments":{"fields":["download-dir","incomplete-dir","peer-port","peer-port-random-on-start","encryption","download-queue-size","download-queue-enabled","peer-limit-global","peer-limit-per-torrent"]}})");
    QCOMPARE(settings.value("result").toString(), QStringLiteral("success"));
    const auto preferences = settings.value("arguments").toObject();
    QCOMPARE(preferences.value("download-dir").toString(), fixture.payload);
    QCOMPARE(preferences.value("incomplete-dir").toString(), fixture.payload);
    QCOMPARE(preferences.value("peer-port").toInt(), 42106);
    QCOMPARE(preferences.value("peer-port-random-on-start"), QJsonValue(true));
    QCOMPARE(preferences.value("encryption").toString(), saved ? QStringLiteral("required") : QStringLiteral("preferred"));
    QCOMPARE(preferences.value("download-queue-size").toInt(), 7);
    QCOMPARE(preferences.value("download-queue-enabled"), QJsonValue(true));
    QCOMPARE(preferences.value("peer-limit-global").toInt(), 400);
    QCOMPARE(preferences.value("peer-limit-per-torrent").toInt(), 50);
    const auto settings2 = rpcResult(R"({"jsonrpc":"2.0","id":11,"method":"session_get","params":{"fields":["peer_port","download_queue_size"]}})");
    QCOMPARE(settings2.value("id").toInt(), 11);
    QCOMPARE(settings2.value("result").toObject(), (QJsonObject{{"peer_port", 42106}, {"download_queue_size", 7}}));
    const auto unsupported = rpcResult(R"({"method":"session-get","arguments":{"fields":["rpc-password"]}})");
    QVERIFY(unsupported.value("result").toString() != QStringLiteral("success") && unsupported.value("arguments").isUndefined());
    const auto startPreference = [&] {
      const auto response = rpcResult(R"({"method":"session-get","arguments":{"fields":["start-added-torrents"]}})");
      test::require(response.value("result").toString() == u"success", "Read the current saved intake preference");
      return response.value("arguments").toObject().value("start-added-torrents");
    };
    QCOMPARE(startPreference(), QJsonValue(true));
    const QByteArray stopAdded = R"({"method":"session-set","arguments":{"start-added-torrents":false}})";
    if (saved && !viewOnly) {
      const auto changed = rpcResult(stopAdded);
      QCOMPARE(changed.value("result").toString(), QStringLiteral("success"));
      QCOMPARE(startPreference(), QJsonValue(false));
      const auto unrecognized = rpcResult(R"({"method":"session-set","arguments":{"start-added-torrents":true,"peer-port":9}})");
      QVERIFY(unrecognized.value("result").toString() != QStringLiteral("success"));
      QCOMPARE(startPreference(), QJsonValue(false));
      const auto changed2 = rpcResult(R"({"jsonrpc":"2.0","id":16,"method":"session_set","params":{"start_added_torrents":true}})");
      QCOMPARE(changed2.value("id").toInt(), 16);
      QVERIFY(changed2.value("result").isObject());
      QCOMPARE(startPreference(), QJsonValue(true));
    } else {
      QVERIFY(exchange(port, request(stopAdded, hasSession)).startsWith("HTTP/1.1 403 "));
      QCOMPARE(startPreference(), QJsonValue(true));
    }
    const auto limits = rpcResult(R"({"method":"session-get","arguments":{"fields":["active-queue-size","seed-queue-size","seed-queue-enabled","speed-limit-down","speed-limit-down-enabled","rename-partial-files"]}})");
    QCOMPARE(limits.value("result"), QJsonValue("success"));
    const auto configuredLimits = limits.value("arguments").toObject();
    QCOMPARE(configuredLimits.value("active-queue-size"), QJsonValue(9));
    QCOMPARE(configuredLimits.value("seed-queue-size"), QJsonValue(-2));
    QCOMPARE(configuredLimits.value("seed-queue-enabled"), QJsonValue(true));
    QCOMPARE(configuredLimits.value("speed-limit-down"), QJsonValue(saved ? 600 : 900));
    QCOMPARE(configuredLimits.value("speed-limit-down-enabled"), QJsonValue(!saved));
    QCOMPARE(configuredLimits.value("rename-partial-files"), QJsonValue(saved));
    const auto limits2 = rpcResult(R"({"jsonrpc":"2.0","id":15,"method":"session_get","params":{"fields":["active_queue_size","seed_queue_size","speed_limit_down","rename_partial_files"]}})");
    const auto rpc2Limits = limits2.value("result").toObject();
    QCOMPARE(rpc2Limits.value("active_queue_size"), configuredLimits.value("active-queue-size"));
    QCOMPARE(rpc2Limits.value("seed_queue_size"), configuredLimits.value("seed-queue-size"));
    QCOMPARE(rpc2Limits.value("speed_limit_down"), configuredLimits.value("speed-limit-down"));
    QCOMPARE(rpc2Limits.value("rename_partial_files"), configuredLimits.value("rename-partial-files"));

    const auto table = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[37],"format":"TaBlE","fields":["name","id","totalSize","hashString"]}})");
    QCOMPARE(table.value("result").toString(), QStringLiteral("success"));
    const auto tableRows = table.value("arguments").toObject().value("torrents").toArray();
    QCOMPARE(tableRows, (QJsonArray{QJsonArray{"hashString", "id", "name", "totalSize"},
        QJsonArray{torrent.value("hashString"), torrent.value("id"), torrent.value("name"), torrent.value("totalSize")}}));
    const auto originalArchive = QFile::exists(fixture.profile + QStringLiteral("/dlarchive.config"));
    QVERIFY(originalArchive);
    const auto archiveBytes = test::read(fixture.profile + QStringLiteral("/dlarchive.config"));
    const auto all = rpcResult(R"({"method":"torrent-get","arguments":{"fields":["id","name","hashString","totalSize","comment","creator","dateCreated","file-count","isPrivate","pieceCount","pieceSize"]}})");
    QCOMPARE(all.value("result").toString(), QStringLiteral("success"));
    const auto inventory = all.value("arguments").toObject().value("torrents").toArray();
    QCOMPARE(inventory.size(), viewOnly ? 1 : 3);
    QCOMPARE(inventory.at(0).toObject().value("id"), QJsonValue(37));
    const auto archivedHash = QStringLiteral("6161616161616161616161616161616161616161");
    const auto archivedByHash = rpcResult(R"({"method":"torrent-get","arguments":{"ids":["6161616161616161616161616161616161616161"],"fields":["id","name","totalSize"]}})");
    const auto hashMatches = archivedByHash.value("arguments").toObject().value("torrents").toArray();
    QCOMPARE(hashMatches.size(), viewOnly ? 0 : 1);
    const auto archivedById = rpcResult(R"({"jsonrpc":"2.0","id":19,"method":"torrent_get","params":{"ids":[25,41],"format":"table","fields":["name","id","total_size","hash_string","file_count"]}})");
    const auto archivedTable = archivedById.value("result").toObject().value("torrents").toArray();
    if (viewOnly) {
      QVERIFY(archivedTable.isEmpty());
    } else {
      const auto archive = inventory.at(1).toObject();
      QCOMPARE(archive.value("id"), QJsonValue(25));
      QCOMPARE(archive.value("hashString"), QJsonValue(archivedHash));
      QCOMPARE(archive.value("totalSize"), QJsonValue(32768));
      QCOMPARE(archive.value("name"), QJsonValue(QStringLiteral("Generated Archived")));
      QCOMPARE(archive.value("comment"), QJsonValue(QStringLiteral("Download Archived")));
      QCOMPARE(archive.value("creator"), QJsonValue(QStringLiteral("")));
      QCOMPARE(archive.value("dateCreated"), QJsonValue(0));
      QCOMPARE(archive.value("file-count"), QJsonValue(1));
      QCOMPARE(archive.value("isPrivate"), QJsonValue(false));
      QCOMPARE(archive.value("pieceCount"), QJsonValue(1));
      QCOMPARE(archive.value("pieceSize"), QJsonValue(32768));
      QCOMPARE(hashMatches.at(0).toObject().value("id"), QJsonValue(25));
      const auto unnamed = inventory.at(2).toObject();
      QCOMPARE(unnamed.value("id"), QJsonValue(41));
      QVERIFY(!unnamed.contains("name"));
      QCOMPARE(unnamed.value("pieceSize"), QJsonValue(1));
      QCOMPARE(archivedTable, (QJsonArray{QJsonArray{"file_count", "hash_string", "id", "name", "total_size"},
          QJsonArray{1, archivedHash, 25, "Generated Archived", 32768},
          QJsonArray{0, "6262626262626262626262626262626262626262", 41, QJsonValue(QJsonValue::Null), 0}}));
    }
    const QByteArray recentIds = R"({"method":"torrent-get","arguments":{"ids":"recently-active","fields":["id"]}})";
    const auto recent = rpcResult(recentIds);
    QCOMPARE(recent.value("result"), QJsonValue("success"));
    QJsonArray expectedRecent{QJsonObject{{"id", 37}}};
    if (!viewOnly) {
      expectedRecent.push_back(QJsonObject{{"id", 25}});
      expectedRecent.push_back(QJsonObject{{"id", 41}});
    }
    QCOMPARE(recent.value("arguments").toObject().value("torrents"), QJsonValue(expectedRecent));

    const auto regular = rpcResult(R"({"method":"torrent-get","arguments":{"fields":["id","name"]}})");
    QCOMPARE(regular.value("arguments").toObject().value("torrents").toArray().size(), inventory.size());
    const auto unchanged = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":21,"params":{"ids":"recently_active","fields":["id"],"format":"table"}})");
    QCOMPARE(unchanged.value("result").toObject().value("torrents"), QJsonValue(QJsonArray{}));
    QVERIFY(!unchanged.value("result").toObject().contains("removed"));

    const QByteArray recentNames = R"({"method":"torrent-get","arguments":{"ids":"recently_active","fields":["id","name"]}})";
    const auto named = rpcResult(recentNames);
    QJsonArray expectedNamed{QJsonObject{{"id", 37}, {"name", torrent.value("name")}}};
    if (!viewOnly) { expectedNamed.push_back(QJsonObject{{"id", 25}, {"name", "Generated Archived"}}); }
    QCOMPARE(named.value("arguments").toObject().value("torrents"), QJsonValue(expectedNamed));
    QCOMPARE(rpcResult(recentNames).value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{}));

    const auto changedTable = rpcResult(R"({"method":"torrent-get","arguments":{"ids":"recently-active","fields":["id"],"format":"table"}})");
    QJsonArray expectedTable{QJsonArray{"id"}, QJsonArray{37}};
    if (!viewOnly) { expectedTable.push_back(QJsonArray{25}); }
    QCOMPARE(changedTable.value("arguments").toObject().value("torrents"), QJsonValue(expectedTable));
    QCOMPARE(rpcResult(recentIds).value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{}));
    qInfo("PASS recently-active session diff, ordinary reads and both spellings");

    QCOMPARE(test::read(fixture.profile + QStringLiteral("/dlarchive.config")), archiveBytes);
    const auto noMatch = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[38],"format":"table","fields":["id"]}})");
    QCOMPARE(noMatch.value("result").toString(), QStringLiteral("success"));
    QCOMPARE(noMatch.value("arguments").toObject().value("torrents").toArray().size(), 0);
    const auto rpc2 = rpcResult(R"({"jsonrpc":"2.0","id":8,"method":"torrent_get","params":{"ids":[37],"format":"table","fields":["name","id","total_size","hash_string"]}})");
    QCOMPARE(rpc2.value("id").toInt(), 8);
    const auto rpc2Rows = rpc2.value("result").toObject().value("torrents").toArray();
    QCOMPARE(rpc2Rows, (QJsonArray{QJsonArray{"hash_string", "id", "name", "total_size"}, tableRows.at(1)}));
    QTRY_VERIFY_WITH_TIMEOUT(!overall.isEmpty(), 6000);
    const auto stats = rpcResult(R"({"method":"session-stats","arguments":{"fields":["current-stats"]}})");
    QCOMPARE(stats.value("result").toString(), QStringLiteral("success"));
    const auto current = stats.value("arguments").toObject().value("current-stats").toObject();
    QCOMPARE(current.value("uploadedBytes").toInteger(-1), qint64{0});
    QCOMPARE(current.value("downloadedBytes").toInteger(-1), qint64{0});
    QCOMPARE(current.value("filesAdded").toInteger(-1), qint64{0});
    QCOMPARE(current.value("ratio").toDouble(), 1.0);
    QVERIFY(current.value("secondsActive").toInteger(-1) >= 0);
    const auto stats2 = rpcResult(R"({"jsonrpc":"2.0","id":9,"method":"session_stats","params":{"fields":["current_stats"]}})");
    QCOMPARE(stats2.value("id").toInt(), 9);
    const auto current2 = stats2.value("result").toObject().value("current_stats").toObject();
    QCOMPARE(current2.value("downloaded_bytes"), current.value("downloadedBytes"));
    QCOMPARE(current2.value("uploaded_bytes"), current.value("uploadedBytes"));
    QCOMPARE(current2.value("files_added"), current.value("filesAdded"));
    QVERIFY(current2.value("seconds_active").toInteger(-1) >= 0);
    const auto cumulative = rpcResult(R"({"method":"session-stats","arguments":{"fields":["current-stats","cumulative-stats"]}})");
    QCOMPARE(cumulative.value("result"), QJsonValue("success"));
    const auto both = cumulative.value("arguments").toObject();
    const auto lifetime = both.value("cumulative-stats").toObject();
    QVERIFY(lifetime.value("downloadedBytes").toInteger(-1) >= 999);
    QVERIFY(lifetime.value("uploadedBytes").toInteger(-1) >= 0);
    QVERIFY(lifetime.value("secondsActive").toInteger(-1) >= 0);
    QCOMPARE(lifetime.value("filesAdded"), QJsonValue(7));
    QCOMPARE(lifetime.value("sessionCount"), QJsonValue(9));
    QCOMPARE(both.value("current-stats").toObject().value("downloadedBytes"), QJsonValue(0));
    const auto cumulative2 = rpcResult(R"({"jsonrpc":"2.0","id":11,"method":"session_stats","params":{"fields":["cumulative_stats"]}})");
    const auto lifetime2 = cumulative2.value("result").toObject().value("cumulative_stats").toObject();
    QCOMPARE(lifetime2.value("downloaded_bytes"), lifetime.value("downloadedBytes"));
    QCOMPARE(lifetime2.value("uploaded_bytes"), lifetime.value("uploadedBytes"));
    QCOMPARE(lifetime2.value("files_added"), lifetime.value("filesAdded"));
    QCOMPARE(lifetime2.value("session_count"), lifetime.value("sessionCount"));
    const auto unavailable = rpcResult(R"({"method":"session-stats","arguments":{"fields":["unrecognized-stats"]}})");
    QVERIFY(unavailable.value("result").toString() != QStringLiteral("success"));
    const auto archiveStatus = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":53,"params":{"ids":[25],"fields":["status"]}})");
    const auto archivedActivity = archiveStatus.value("result").toObject().value("torrents").toArray();
    const QJsonObject stoppedActivity{{"status", 0}, {"status.biglybt", 70}};
    if (viewOnly) { QVERIFY(archivedActivity.isEmpty()); }
    else { QCOMPARE(archivedActivity, QJsonArray{stoppedActivity}); }
    const auto statusTable = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":54,"params":{"ids":[25],"fields":["status","id"],"format":"table"}})");
    const auto statusRows = statusTable.value("result").toObject().value("torrents").toArray();
    if (viewOnly) { QVERIFY(statusRows.isEmpty()); }
    else { QCOMPARE(statusRows, (QJsonArray{QJsonArray{"id", "status.biglybt", "status"}, QJsonArray{25, 70, 0}})); }

    const auto totals = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["downloadedEver","uploadedEver","rateDownload","rateUpload"]}})");
    const QJsonObject expectedTotals{{"downloadedEver", 321}, {"uploadedEver", 654}, {"rateDownload", 0}, {"rateUpload", 0}};
    QCOMPARE(totals.value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{expectedTotals}));
    const auto archiveTransfer = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":55,"params":{"ids":[25],"fields":["downloaded_ever","uploaded_ever","rate_download","rate_upload"],"format":"table"}})");
    const auto archiveTransferRows = archiveTransfer.value("result").toObject().value("torrents").toArray();
    if (viewOnly) { QVERIFY(archiveTransferRows.isEmpty()); }
    else { QCOMPARE(archiveTransferRows, (QJsonArray{
        QJsonArray{"downloaded_ever", "rate_download", "rate_upload", "uploaded_ever"}, QJsonArray{0, 0, 0, 0}})); }

    const auto savedLimits = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["downloadLimit","downloadLimited","uploadLimit","uploadLimited"]}})");
    const QJsonObject expectedLimits{{"downloadLimit", viewOnly ? -1 : 64}, {"downloadLimited", !viewOnly},
        {"uploadLimit", 1}, {"uploadLimited", true}};
    QCOMPARE(savedLimits.value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{expectedLimits}));
    const auto archiveLimits = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":72,"params":{"ids":[25],"fields":["download_limit","download_limited","upload_limit","upload_limited"]}})");
    const QJsonObject expectedArchiveLimits{{"download_limit", 0}, {"download_limited", false},
        {"upload_limit", 0}, {"upload_limited", false}};
    const auto archiveLimitRows = archiveLimits.value("result").toObject().value("torrents");
    QCOMPARE(archiveLimitRows, QJsonValue(viewOnly ? QJsonArray{} : QJsonArray{expectedArchiveLimits}));

    const auto archiveStats = rpcResult(R"({"jsonrpc":"2.0","id":68,"method":"torrent_get","params":{"ids":[25,41],"fields":["file_stats"],"format":"table"}})");
    const QJsonArray archivedFiles{QJsonObject{{"bytes_completed", 64}, {"priority", 0}, {"wanted", true}}};
    const auto archiveStatsRows = archiveStats.value("result").toObject().value("torrents").toArray();
    if (viewOnly) { QVERIFY(archiveStatsRows.empty()); }
    else {
      QCOMPARE(archiveStatsRows, (QJsonArray{QJsonArray{"file_stats"}, QJsonArray{archivedFiles}, QJsonArray{QJsonArray{}}}));
    }
    const auto archiveProgress = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[25],"fields":["sizeWhenDone","leftUntilDone","percentDone"]}})");
    const auto expectedArchiveProgress = viewOnly ? QJsonArray{} :
        QJsonArray{QJsonObject{{"sizeWhenDone", 32768}, {"leftUntilDone", 0}, {"percentDone", 1}}};
    QCOMPARE(archiveProgress.value("arguments").toObject().value("torrents"), QJsonValue(expectedArchiveProgress));

    const auto savedQueue = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["isForced","queuePosition"]}})");
    const QJsonObject expectedQueue{{"isForced", false}, {"queuePosition", 1}};
    QCOMPARE(savedQueue.value("arguments").toObject().value("torrents"), QJsonValue(QJsonArray{expectedQueue}));
    const auto archivedQueue = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":56,"params":{"ids":[25],"fields":["is_forced","queue_position"],"format":"table"}})");
    const auto archiveQueueRows = archivedQueue.value("result").toObject().value("torrents").toArray();
    if (viewOnly) { QVERIFY(archiveQueueRows.isEmpty()); }
    else {
      QCOMPARE(archiveQueueRows, (QJsonArray{QJsonArray{"is_forced", "queue_position"},
          QJsonArray{QJsonValue(QJsonValue::Null), 0}}));
      const auto objectArchive = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[25],"fields":["isForced","queuePosition"]}})");
      QCOMPARE(objectArchive.value("arguments").toObject().value("torrents"),
          QJsonValue(QJsonArray{QJsonObject{{"queuePosition", 0}}}));
    }
    const auto reportedForce = [&] {
      const auto value = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":57,"params":{"ids":[37],"fields":["is_forced"]}})");
      const auto torrents = value.value("result").toObject().value("torrents").toArray();
      return torrents.size() == 1 ? torrents.first().toObject().value("is_forced") : QJsonValue{};
    };

    const QByteArray startTorrent = R"({"method":"torrent-start","arguments":{"ids":[37]},"tag":30})";
    const QByteArray forceTorrent = R"({"method":"torrent-start-now","arguments":{"ids":[37]},"tag":34})";
    const QByteArray stopTorrent = R"({"method":"torrent-stop","arguments":{"ids":[37]},"tag":31})";
    if (saved && !viewOnly) {
      const QByteArray pollTransfer = R"({"method":"torrent-get","arguments":{"ids":"recently-active","fields":["id","downloadedEver","uploadedEver"]}})";
      const auto recentTransfer = [&] { return rpcResult(pollTransfer).value("arguments").toObject().value("torrents").toArray(); };
      QCOMPARE(recentTransfer(), (QJsonArray{
          QJsonObject{{"id", 37}, {"downloadedEver", 321}, {"uploadedEver", 654}},
          QJsonObject{{"id", 25}, {"downloadedEver", 0}, {"uploadedEver", 0}},
          QJsonObject{{"id", 41}, {"downloadedEver", 0}, {"uploadedEver", 0}}}));
      QVERIFY(recentTransfer().isEmpty());
      const QByteArray pollActivity = R"({"method":"torrent-get","arguments":{"ids":"recently-active","fields":["id","status"]}})";
      const auto recentActivity = [&] { return rpcResult(pollActivity).value("arguments").toObject().value("torrents").toArray(); };
      QCOMPARE(recentActivity(), (QJsonArray{QJsonObject{{"id", 37}, {"status", 0}, {"status.biglybt", 70}},
          QJsonObject{{"id", 25}, {"status", 0}, {"status.biglybt", 70}},
          QJsonObject{{"id", 41}, {"status", 0}, {"status.biglybt", 70}}}));
      QVERIFY(recentActivity().isEmpty());
      const auto withoutExplicitIds = rpcResult(R"({"method":"torrent-start","arguments":{"ids":"recently-active"}})");
      QVERIFY(withoutExplicitIds.value("result") != QJsonValue("success"));
      const auto archiveStart = rpcResult(R"({"method":"torrent-start","arguments":{"ids":[25]}})");
      QVERIFY(archiveStart.value("result") != QJsonValue("success"));
      const auto started = rpcResult(startTorrent);
      QCOMPARE(started.value("tag"), QJsonValue(30));
      QCOMPARE(started.value("result"), QJsonValue("success"));
      const auto canStop = [&] {
        const auto current = qvariant_cast<ui::Presentation>(rows.last().at(0));
        return current && current->items.size() == 1 &&
            (current->items.front().actionMask & ui::downloadActionBit(core::DownloadAction::Stop));
      };
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && canStop(), 12000);
      const auto repeatedStart = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_start","id":33,"params":{"ids":[37]}})");
      QCOMPARE(repeatedStart.value("id"), QJsonValue(33));
      QVERIFY2(repeatedStart.value("result").isObject(),
          QJsonDocument(repeatedStart).toJson(QJsonDocument::Compact).constData());
      const auto missing = rpcResult(R"({"method":"torrent-stop","arguments":{}})");
      QVERIFY(missing.value("result").toString() != QStringLiteral("success"));
      QCOMPARE(rpcResult(R"({"method":"torrent-stop","arguments":{"ids":[25]}})").value("result"), QJsonValue("success"));
      QVERIFY(canStop());
      const auto activeChange = [&] {
        const auto changes = recentActivity();
        if (changes.size() != 1) { return false; }
        const auto row = changes.first().toObject();
        return row.value("id") == QJsonValue(37) &&
            ((row.value("status") == QJsonValue(4) && row.value("status.biglybt") == QJsonValue(50)) ||
             (row.value("status") == QJsonValue(6) && row.value("status.biglybt") == QJsonValue(60)));
      };
      bool activeChangeObserved = false;
      QTRY_VERIFY_WITH_TIMEOUT((activeChangeObserved = activeChangeObserved || activeChange()), 14000);
      const auto transferred = [&] {
        const auto current = rpcResult(R"({"method":"torrent-get","arguments":{"ids":[37],"fields":["downloadedEver","uploadedEver","rateDownload","rateUpload"]}})");
        const auto rows = current.value("arguments").toObject().value("torrents").toArray();
        if (rows.size() != 1) { return false; }
        const auto row = rows.first().toObject();
        return row.value("downloadedEver").toInteger() > 321 &&
            row.value("uploadedEver").toInteger() == 654;
      };
      bool transferObserved = false;
      QTRY_VERIFY_WITH_TIMEOUT((transferObserved = transferObserved || transferred()), 14000);
      const auto actualLimits = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_get","id":73,"params":{"ids":[37],"fields":["download_limit","download_limited","upload_limit","upload_limited"]}})");
      const QJsonObject expectedLiveLimits{{"download_limit", 64}, {"download_limited", true},
          {"upload_limit", 1}, {"upload_limited", true}};
      QCOMPARE(actualLimits.value("result").toObject().value("torrents"), QJsonValue(QJsonArray{expectedLiveLimits}));
      qInfo("PASS original torrent rate limits over saved, current and archived RPC");
      qInfo("PASS per-torrent original payload totals include actual loopback transfer");

      const auto stopped = rpcResult(stopTorrent);
      QCOMPARE(stopped.value("tag"), QJsonValue(31));
      QCOMPARE(stopped.value("result"), QJsonValue("success"));
      QTRY_VERIFY_WITH_TIMEOUT(!canStop(), 12000);
      const auto stoppedAgain = [&] {
        const auto changes = recentActivity();
        return changes == QJsonArray{QJsonObject{{"id", 37}, {"status", 0}, {"status.biglybt", 70}}};
      };
      bool stoppedChangeObserved = false;
      QTRY_VERIFY_WITH_TIMEOUT((stoppedChangeObserved = stoppedChangeObserved || stoppedAgain()), 12000);
      qInfo("PASS live torrent activity changes through recently-active with stable archived rows");
      const auto repeated = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_stop","id":32,"params":{"ids":[37]}})");
      QCOMPARE(repeated.value("id"), QJsonValue(32));
      QVERIFY2(repeated.value("result").isObject(), QJsonDocument(repeated).toJson(QJsonDocument::Compact).constData());

      const auto canForce = [&] {
        const auto current = qvariant_cast<ui::Presentation>(rows.last().at(0));
        return current && current->items.size() == 1 &&
            (current->items.front().actionMask & ui::downloadActionBit(core::DownloadAction::ForceStart));
      };
      const auto forcedOnDisk = [&]() -> std::optional<bool> {
        const auto path = fixture.profile + QStringLiteral("/downloads.config");
        const auto source = core::Document::parse(test::read(path).toStdString());
        if (!source) { return std::nullopt; }
        const auto library = core::readLibrary(*source);
        if (!library || library->downloads.size() != 1) { return std::nullopt; }
        return library->downloads.front().forceStart;
      };
      QTRY_VERIFY_WITH_TIMEOUT(canForce(), 12000);
      QVERIFY(forcedOnDisk() == std::optional{false});
      const auto archiveForce = rpcResult(R"({"method":"torrent-start-now","arguments":{"ids":[25]}})");
      QVERIFY(archiveForce.value("result") != QJsonValue("success"));
      const auto forced = rpcResult(forceTorrent);
      QCOMPARE(forced.value("tag"), QJsonValue(34));
      QVERIFY2(forced.value("result") == QJsonValue("success"), QJsonDocument(forced).toJson(QJsonDocument::Compact).constData());
      QTRY_VERIFY_WITH_TIMEOUT(canStop() && forcedOnDisk() == std::optional{true}, 12000);
      QCOMPARE(reportedForce(), QJsonValue(true));
      const auto repeatedForce = rpcResult(R"({"jsonrpc":"2.0","method":"torrent_start_now","id":35,"params":{"ids":[37]}})");
      QCOMPARE(repeatedForce.value("id"), QJsonValue(35));
      QVERIFY2(repeatedForce.value("result").isObject(), QJsonDocument(repeatedForce).toJson(QJsonDocument::Compact).constData());
      QCOMPARE(rpcResult(stopTorrent).value("result"), QJsonValue("success"));
      QTRY_VERIFY_WITH_TIMEOUT(!canStop() && forcedOnDisk() == std::optional{false}, 12000);
      QTRY_COMPARE_WITH_TIMEOUT(reportedForce(), QJsonValue(false), 12000);
      qInfo("PASS original queue position and force flag through saved, active and archive RPC paths");
      qInfo("PASS forced local RPC start persists original intent and repeat or stop uses the selected torrent");
    } else {
      QVERIFY(exchange(port, request(startTorrent, hasSession)).startsWith("HTTP/1.1 403 "));
      QVERIFY(exchange(port, request(forceTorrent, hasSession)).startsWith("HTTP/1.1 403 "));
      QVERIFY(exchange(port, request(stopTorrent, hasSession)).startsWith("HTTP/1.1 403 "));
    }

    if (!saved) {
      ui::LibraryModel model;
      QVERIFY(model.apply(qvariant_cast<ui::Presentation>(rows.last().at(0))));
      const auto target = model.targetAt(0);
      QVERIFY(target && bridge.continueDownload(*target));
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 20000);
      const auto downloadTotal = [&] {
        const auto stat = rpcResult(R"({"method":"session-stats","arguments":{"fields":["current-stats"]}})");
        return stat.value("arguments").toObject().value("current-stats").toObject().value("downloadedBytes").toInteger(-1);
      };
      QTRY_VERIFY_WITH_TIMEOUT(downloadTotal() >= test::OwnedEngineFixture::PayloadBytes, 12000);
      QVERIFY(fixture.intact());
    }
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 20000);
    auto reopened = storage::ProfileLease::acquire(fixture.native());
    QVERIFY(reopened);
    const auto persisted = storage::ConfigFile::load(*reopened, "biglybt.config");
    QVERIFY(persisted && persisted->recoverySource() == storage::RecoverySource::Primary);
    const auto* start = persisted->document().find("Default Start Torrents Stopped");
    if (saved && !viewOnly) {
      QVERIFY(start && start->type == core::Node::Type::Integer && start->integer == 0);
      const auto* opaque = persisted->document().find("opaque.rpc.fixture");
      QVERIFY(opaque && opaque->bytes == "retained");
      const auto current = storage::readLibraryView(*reopened);
      QVERIFY(current && current->items.size() == 1);
      QCOMPARE(current->items.front().saved.startup, core::StartupState::Stopped);
      QCOMPARE(test::read(fixture.profile + QStringLiteral("/dlarchive.config")), archiveBytes);
    } else {
      QVERIFY(!start);
    }
    QTcpSocket stopped;
    stopped.setProxy(QNetworkProxy::NoProxy);
    stopped.connectToHost(QHostAddress::LocalHost, port);
    QTRY_VERIFY_WITH_TIMEOUT(stopped.error() == QAbstractSocket::ConnectionRefusedError, 4000);
  }
};

QTEST_MAIN(LocalRpcHostTests)
#include "local_rpc_host_tests.moc"
