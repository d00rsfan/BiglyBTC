// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_device_description.hpp"
#include "library_window.hpp"
#include "local_media_controls.hpp"
#include "local_media_identity.hpp"
#include "owned_engine_fixture.hpp"
#include "profile.hpp"
#include "session_bridge.hpp"

#include <QClipboard>
#include <QElapsedTimer>
#include <QMenu>
#include <QTableView>
#include <QNetworkProxy>
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QUdpSocket>
#include <QUrl>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest>

namespace {
using namespace biglybtc;

ui::Target current(const QSignalSpy& rows) {
  const auto snapshot = qvariant_cast<ui::Presentation>(rows.last().at(0));
  test::require(snapshot && snapshot->items.size() == 1, "Missing owned media torrent");
  return {snapshot->items.front().hash, snapshot->revision};
}
class OwnedRenderer final : public QObject {
public:
  const QUuid id{u"cb7bf591-d64e-45ed-b853-8ad52cfb7b7e"};
  std::vector<QByteArray> calls;
  QUrl mediaUrl;

  explicit OwnedRenderer(bool converted = false, bool alternate = false)
      : sink_(converted ? "video/mp4" : alternate ? "video/x-mkv" : "unknown/unknown") {
    udp_.setProxy(QNetworkProxy::NoProxy);
    responses_.setProxy(QNetworkProxy::NoProxy);
    http_.setProxy(QNetworkProxy::NoProxy);
    const QHostAddress group("239.255.255.250");
    const auto interface = QNetworkInterface::interfaceFromName("lo");
    test::require(udp_.bind(group, 1900, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint) &&
        udp_.joinMulticastGroup(group, interface) && responses_.bind(QHostAddress::LocalHost, 0) &&
        http_.listen(QHostAddress::LocalHost, 0), "Own the renderer SSDP and HTTP fixture");
    connect(&udp_, &QUdpSocket::readyRead, this, [this] {
      while (udp_.hasPendingDatagrams()) {
        const auto packet = udp_.receiveDatagram();
        if (!packet.data().startsWith("M-SEARCH * HTTP/1.1") || !packet.data().contains("ST: " + type_)) { continue; }
        const auto reply = "HTTP/1.1 200 OK\r\nST: " + type_ + "\r\nUSN: " + udn() + "::" + type_ +
            "\r\nLocation: http://127.0.0.1:" + QByteArray::number(http_.serverPort()) + "/device.xml\r\n\r\n";
        responses_.writeDatagram(reply, packet.senderAddress(), packet.senderPort());
      }
    });
    connect(&http_, &QTcpServer::newConnection, this, [this] {
      while (auto* socket = http_.nextPendingConnection()) {
        auto input = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, input] {
          *input += socket->readAll();
          const auto end = input->indexOf("\r\n\r\n");
          if (end < 0) { return; }
          if (input->startsWith("GET /device.xml HTTP/1.1")) { send(*socket, description()); return; }
          const auto headers = input->first(end).toLower();
          const auto length = headers.indexOf("\r\ncontent-length: ");
          const auto action = headers.indexOf("\r\nsoapaction: ");
          if (length < 0 || action < 0) { socket->abort(); return; }
          const auto size = headers.mid(length + 18, headers.indexOf("\r\n", length + 2) - length - 18).toLongLong();
          if (input->size() - end - 4 < size) { return; }
          const auto hash = input->indexOf('#', action);
          const auto quote = input->indexOf('"', hash);
          const auto name = input->mid(hash + 1, quote - hash - 1);
          calls.push_back(name);
          if (name == "SetAVTransportURI") {
            const auto start = input->indexOf("<CurrentURI>", end) + 12;
            mediaUrl = QUrl::fromEncoded(input->mid(start, input->indexOf("</CurrentURI>", start) - start));
          }
          send(*socket, soap(name, input->startsWith("POST /cm")));
        });
      }
    });
  }

private:
  const QByteArray sink_;
  QByteArray udn() const { return "uuid:" + id.toString(QUuid::WithoutBraces).toLatin1(); }
  QByteArray description() const {
    return "<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device><deviceType>" + type_ +
        "</deviceType><friendlyName>Fixture Renderer</friendlyName><UDN>" + udn() + "</UDN><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType><controlURL>/av</controlURL></service>"
        "<service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType><controlURL>/cm</controlURL></service>"
        "</serviceList></device></root>";
  }
  QByteArray soap(const QByteArray& action, bool manager) const {
    QByteArray fields;
    if (action == "GetProtocolInfo") { fields = "<Sink>http-get:*:" + sink_ + ":*</Sink>"; }
    if (action == "PrepareForConnection") { fields = "<ConnectionID>2</ConnectionID><AVTransportID>8</AVTransportID>"; }
    const auto service = manager ? "ConnectionManager:1" : "AVTransport:1";
    return "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:" +
        action + "Response xmlns:u=\"urn:schemas-upnp-org:service:" + service + "\">" + fields +
        "</u:" + action + "Response></s:Body></s:Envelope>";
  }
  void send(QTcpSocket& socket, const QByteArray& body) {
    socket.disconnect(this);
    socket.write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
        "\r\nContent-Type: text/xml\r\nConnection: close\r\n\r\n" + body);
    socket.disconnectFromHost();
  }

  const QByteArray type_{"urn:schemas-upnp-org:device:MediaRenderer:1"};
  QUdpSocket udp_;
  QUdpSocket responses_;
  QTcpServer http_;
};

} // namespace

class MediaHostTests final : public QObject {
  Q_OBJECT
private slots:
  void savedDeviceIdentity() {
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    const auto path = profile.path().toStdString();
    auto lease = storage::ProfileLease::acquire(path);
    QVERIFY(lease);
    const auto original = app::loadOrCreateLocalMediaId(*lease);
    QVERIFY(original && !original->isNull());
    auto saved = storage::ConfigFile::load(*lease, "biglybt.config");
    QVERIFY(saved);
    const auto* id = saved->document().find("Plugin.azupnpav.uuid");
    QVERIFY(id && id->type == core::Node::Type::Bytes);
    const auto wire = "uuid:" + original->toString(QUuid::WithoutBraces).toStdString();
    QCOMPARE(id->bytes, std::string_view(wire));
    const auto stable = app::loadOrCreateLocalMediaId(*lease);
    QVERIFY(stable && *stable == *original);

    const QUuid legacy(u"a6426e84-2652-47ed-841c-fb2c1a32e499");
    QVERIFY(saved->document().replaceValue("Plugin.azupnpav.uuid", "41:uuid:" + legacy.toString(QUuid::WithoutBraces).toStdString()));
    QVERIFY(saved->document().replaceValue("original.unknown", "3:abc"));
    QVERIFY(saved->save(*lease));
    const auto expected = saved->document().serialize();
    const auto readLegacy = app::loadOrCreateLocalMediaId(*lease);
    QVERIFY(readLegacy && *readLegacy == legacy);
    auto untouched = storage::ConfigFile::load(*lease, "biglybt.config");
    QVERIFY(untouched && untouched->document().originalBytes() == expected);

    QVERIFY(untouched->document().replaceValue("Plugin.azupnpav.uuid", "7:invalid"));
    QVERIFY(untouched->save(*lease));
    const auto invalid = app::loadOrCreateLocalMediaId(*lease);
    QVERIFY(!invalid && invalid.error().code == storage::ErrorCode::InvalidFormat);
    const auto preserved = storage::ConfigFile::load(*lease, "biglybt.config");
    QVERIFY(preserved && preserved->document().find("Plugin.azupnpav.uuid")->bytes == "invalid");
  }

  void actualSessionOwner() {
    test::OwnedEngineFixture fixture(false);
    for (const auto& file : fixture.files) {
      test::write(fixture.payload + '/' + file.relative, file.expected);
    }
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 15000);
    app::SessionBridge bridge;
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy messages(&bridge, &app::SessionBridge::messageReady);
    QSignalSpy media(&bridge, &app::SessionBridge::localMediaReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.isEmpty(), 10000);
    QVERIFY(bridge.downloadAction({current(rows)}, core::DownloadAction::Start));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 15000);

    const media::MediaDeviceIdentity device{QUuid(u"ddfb9663-ad52-4ab1-a935-ff94f6c6c642"), QStringLiteral("Host fixture")};
    QVERIFY(bridge.startLocalMedia(device, 0, true));
    QTRY_VERIFY2_WITH_TIMEOUT(!bridge.busy() && media.count() == 1,
        messages.isEmpty() ? "" : messages.last().at(0).toString().toUtf8().constData(), 10000);
    const auto http = static_cast<quint16>(media.last().at(1).toUInt());
    const auto udp = static_cast<quint16>(media.last().at(2).toUInt());
    QVERIFY(http != 0 && udp != 0);

    const auto request = [&](const QByteArray& path) {
      QTcpSocket socket;
      socket.setProxy(QNetworkProxy::NoProxy);
      socket.connectToHost(QHostAddress::LocalHost, http);
      test::require(QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }, 3000),
          "Connect to the actual desktop Host media listener");
      const QByteArray wire = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=16381-16391\r\n\r\n";
      test::require(socket.write(wire) == wire.size(), "Send an owned media request");
      QByteArray received;
      test::require(QTest::qWaitFor([&] {
        received += socket.readAll();
        return socket.state() == QAbstractSocket::UnconnectedState;
      }, 5000), "Wait for the desktop worker to pump its media response");
      return received;
    };
    const auto path = "/Content/" + current(rows).hash.toLatin1() + "-0.bin";
    QVERIFY(request(path).startsWith("HTTP/1.1 404 "));
    const auto attempt = [&] {
      if (!bridge.busy() && media.count() < 2) {
        QVERIFY(bridge.publishLocalMediaFile(current(rows), 0));
      }
    };
    // The retained startup row may still be waiting for the first check alert.
    QElapsedTimer deadline;
    deadline.start();
    while (media.count() < 2 && deadline.elapsed() < 15000) {
      attempt();
      QTest::qWait(150);
    }
    QTRY_VERIFY2_WITH_TIMEOUT(!bridge.busy() && media.count() >= 2,
        messages.isEmpty() ? "" : messages.last().at(0).toString().toUtf8().constData(), 3000);
    QCOMPARE(media.last().at(3).toByteArray(), "http://127.0.0.1:" + QByteArray::number(http) + path);
    const auto answer = request(path);
    QVERIFY(answer.startsWith("HTTP/1.1 206 "));
    QVERIFY(answer.endsWith(fixture.files.front().expected.mid(16381, 11)));
    QVERIFY(bridge.closeSession());
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 20000);
    QVERIFY(closed.last().at(1).toBool() && closed.last().at(2).toBool());
    QVERIFY(storage::ProfileLease::acquire(fixture.native()));
    QTcpSocket stopped;
    stopped.setProxy(QNetworkProxy::NoProxy);
    stopped.connectToHost(QHostAddress::LocalHost, http);
    QTRY_VERIFY_WITH_TIMEOUT(stopped.error() == QAbstractSocket::ConnectionRefusedError, 3000);
  }
  void desktopControls_data() {
    QTest::addColumn<bool>("lan");
    QTest::addColumn<int>("profile");
    QTest::newRow("loopback") << false << 0;
    QTest::newRow("selected-interface") << true << 0;
    QTest::newRow("renderer-mime-alternative") << true << -1;
    if (!qEnvironmentVariableIsEmpty("BIGLYBTC_TEST_CONVERT_INPUT")) {
      const auto profile = qEnvironmentVariableIntValue("BIGLYBTC_TEST_CONVERT_LIMIT");
      if (profile == 2) { QTest::newRow("converted-stream") << true << 2; }
      else if (profile == 480) { QTest::newRow("converted-480p") << true << 480; }
      else if (profile == 720) { QTest::newRow("converted-720p") << true << 720; }
      else { QTest::newRow("converted-media") << true << 1; }
    }
  }

  void desktopControls() {
    QFETCH(bool, lan);
    QFETCH(int, profile);
    const bool convert = profile > 0;
    const bool alternate = profile == -1;
    const bool missingRequiredAudio = (profile == 480 || profile == 720) &&
        qEnvironmentVariableIsSet("BIGLYBTC_TEST_MISSING_ORIGINAL_AUDIO");
    auto renderer = lan ? std::make_unique<OwnedRenderer>(convert, alternate) : nullptr;
    const auto input = convert ? test::read(qEnvironmentVariable("BIGLYBTC_TEST_CONVERT_INPUT")) :
        alternate ? QByteArray(test::OwnedEngineFixture::PayloadBytes, 'm') : QByteArray{};
    QVERIFY(!convert || !input.isEmpty());
    test::OwnedEngineFixture fixture(false, false, input);
    for (const auto& file : fixture.files) { test::write(fixture.payload + '/' + file.relative, file.expected); }
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 15000);
    ui::LibraryWindow window;
    app::SessionBridge bridge;
    const auto selection = lan ? std::optional{app::LocalMediaInterface{QStringLiteral("lo"), QStringLiteral("127.0.0.1")}}
                               : std::nullopt;
    const auto ffmpeg = convert ? std::optional{app::LocalMediaFfmpeg{qEnvironmentVariable("BIGLYBTC_TEST_CONVERT_FFMPEG"),
        QByteArray::fromHex(qgetenv("BIGLYBTC_TEST_CONVERT_SHA256"))}} : std::nullopt;
    app::bindLocalMediaControls(window, bridge, selection, ffmpeg);
    connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
    connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &window, &ui::LibraryWindow::beginColumnLayoutSession);
    connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
    connect(&bridge, &app::SessionBridge::detailsReady, &window, &ui::LibraryWindow::setDetails);
    connect(&window, &ui::LibraryWindow::detailsRequested, &bridge, &app::SessionBridge::details);
    QSignalSpy rows(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy media(&bridge, &app::SessionBridge::localMediaReady);
    window.show();
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && !rows.isEmpty(), 10000);
    QVERIFY(bridge.downloadAction({current(rows)}, core::DownloadAction::Start));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 15000);
    auto* action = window.findChild<QAction*>("classic-local-media");
    QVERIFY(action);
    action->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(media.count(), 1, 10000);
    const auto port = static_cast<quint16>(media.last().at(1).toUInt());
    QVERIFY(port != 0);
    QCOMPARE(media.last().at(2).toUInt(), lan ? 1900u : 0u);
    QFile stored(fixture.profile + QStringLiteral("/biglybt.config"));
    QVERIFY(stored.open(QIODevice::ReadOnly));
    const auto settings = core::Document::parse(stored.readAll().toStdString());
    QVERIFY(settings && settings->find("Plugin.azupnpav.uuid"));

    auto* table = window.completeTable().model()->rowCount() ? &window.completeTable() : &window.incompleteTable();
    table->selectRow(0);
    QTRY_VERIFY(window.filesAction().isEnabled());
    window.filesAction().trigger();
    auto& files = window.filesTable();
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && files.model()->rowCount() == static_cast<int>(fixture.files.size()), 5000);
    const auto fetch = [&] {
      QTcpSocket client;
      client.setProxy(QNetworkProxy::NoProxy);
      client.connectToHost(QHostAddress::LocalHost, port);
      test::require(QTest::qWaitFor([&] { return client.state() == QAbstractSocket::ConnectedState; }, 2000),
          "Connect the fixture to the actual renderer resource");
      const auto request = "GET " + renderer->mediaUrl.path().toLatin1() + " HTTP/1.1\r\nHost: 127.0.0.1:" +
          QByteArray::number(port) + (convert ? "\r\n\r\n" : "\r\nRange: bytes=16381-16391\r\n\r\n");
      test::require(client.write(request) == request.size(), "Fetch the published renderer resource");
      QByteArray response;
      const auto finished = QTest::qWaitFor([&] {
        response += client.readAll();
        return client.state() == QAbstractSocket::UnconnectedState;
      }, profile == 2 ? 45000 : 4000);
      if (!finished) { qWarning("Renderer fixture received %lld bytes before its deadline", static_cast<long long>(response.size())); }
      test::require(finished, "Read the verified renderer response");
      return response;
    };
    if (renderer) {
      QSignalSpy catalogues(&bridge, &app::SessionBridge::mediaRenderersReady);
      QSignalSpy playback(&bridge, &app::SessionBridge::mediaPlaybackReady);
      auto* search = window.findChild<QAction*>("classic-find-media-renderers");
      QVERIFY(search);
      search->trigger();
      QTRY_COMPARE_WITH_TIMEOUT(catalogues.count(), 2, 6000);
      const auto names = qvariant_cast<ui::MediaRenderers>(catalogues.last().at(2));
      QCOMPARE(names.value(renderer->id), QStringLiteral("Fixture Renderer"));
      QVERIFY(!bridge.busy());

      files.selectRow(0);
      const auto point = files.visualRect(files.model()->index(0, 0)).center();
      QVERIFY(files.viewport()->rect().contains(point));
      bool requested = false;
      QTimer::singleShot(0, &window, [&] {
        if (auto* popup = window.findChild<QMenu*>("files-priority-context")) {
          auto name = convert ? "files-converted-media-renderers" : "files-media-renderers";
          if (profile == 2) { name = "files-stream-media-renderers"; }
          if (profile == 480) { name = "files-480p-media-renderers"; }
          if (profile == 720) { name = "files-720p-media-renderers"; }
          if (auto* menu = popup->findChild<QMenu*>(QString::fromLatin1(name)); menu && menu->actions().size() == 1) {
            requested = true;
            menu->actions().front()->trigger();
          }
          popup->close();
        }
      });
      emit files.customContextMenuRequested(point);
      QVERIFY(requested);
      if (missingRequiredAudio) {
        QTRY_VERIFY_WITH_TIMEOUT(!playback.isEmpty() &&
            qvariant_cast<ui::MediaPlaybackState>(playback.last().at(1)) == ui::MediaPlaybackState::Failed, 10000);
        QVERIFY(renderer->calls.empty());
        QVERIFY(renderer->mediaUrl.isEmpty());
        qInfo("PASS original profile refuses missing audio before renderer publication");
      } else {
        QTRY_VERIFY_WITH_TIMEOUT(!playback.isEmpty() && qvariant_cast<ui::MediaPlaybackState>(playback.last().at(1)) ==
            ui::MediaPlaybackState::Playing, 10000);
        if (convert) {
          QVERIFY(std::ranges::any_of(playback, [](const auto& event) {
            return qvariant_cast<ui::MediaPlaybackState>(event.at(1)) == ui::MediaPlaybackState::Preparing;
          }));
          QVERIFY(renderer->mediaUrl.path().startsWith(u"/Content/transcoded-"));
          QVERIFY(renderer->mediaUrl.path().endsWith(u".mp4"));
        }
        QVERIFY(renderer->mediaUrl.isValid());
        QVERIFY(renderer->calls == std::vector<QByteArray>({"GetProtocolInfo", "PrepareForConnection", "SetAVTransportURI", "Play"}));
        const auto response = fetch();
        if (convert) {
          const auto header = response.indexOf("\r\n\r\n");
          QVERIFY(response.startsWith("HTTP/1.1 200 ") && response.contains("Content-Type: video/mp4\r\n") && header > 0);
          auto encoded = response.sliced(header + 4);
          if (profile == 2) {
            QVERIFY(response.first(header).contains("Transfer-Encoding: chunked"));
            QByteArray decoded;
            bool complete = false;
            while (!encoded.isEmpty()) {
              const auto crlf = encoded.indexOf("\r\n");
              bool valid = false;
              const auto size = encoded.first(std::max<qsizetype>(0, crlf)).toLongLong(&valid, 16);
              QVERIFY(valid && size >= 0 && size <= encoded.size() - crlf - 4);
              if (size == 0) {
                QCOMPARE(encoded, QByteArray("0\r\n\r\n"));
                complete = true;
                break;
              }
              QCOMPARE(encoded.mid(crlf + 2 + size, 2), QByteArray("\r\n"));
              decoded += encoded.mid(crlf + 2, size);
              encoded.remove(0, crlf + 4 + size);
            }
            QVERIFY(complete && !decoded.isEmpty());
            encoded = std::move(decoded);
          }
          QVERIFY(encoded.size() > 1000 && encoded.mid(4, 4) == "ftyp" && encoded != input);
          if (profile == 2) { QVERIFY(encoded.contains("moof") && encoded.contains("mdat")); }
          const auto output = qEnvironmentVariable("BIGLYBTC_TEST_CONVERT_OUTPUT");
          if (!output.isEmpty()) { test::write(output, encoded); }
        } else {
          QVERIFY(response.startsWith("HTTP/1.1 206 ") && response.endsWith(fixture.files.front().expected.mid(16381, 11)));
          if (alternate) { QVERIFY(response.contains("Content-Type: video/x-mkv\r\n")); }
        }
        auto* stopPlayback = window.findChild<QAction*>("classic-stop-media-playback");
        QVERIFY(stopPlayback);
        stopPlayback->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(qvariant_cast<ui::MediaPlaybackState>(playback.last().at(1)) ==
            ui::MediaPlaybackState::Stopped, 5000);
        QCOMPARE(renderer->calls.back(), QByteArray("ConnectionComplete"));
        QVERIFY(fetch().startsWith("HTTP/1.1 404 "));
      }
    }

    QSignalSpy publications(&window, &ui::LibraryWindow::localMediaFileRequested);
    QSignalSpy messages(&bridge, &app::SessionBridge::messageReady);
    QElapsedTimer readiness;
    readiness.start();
    // Startup can expose the saved row before the managed recheck has finished.
    while (media.count() < 2 && readiness.elapsed() < 8000) {
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && files.model()->rowCount() == static_cast<int>(fixture.files.size()), 3000);
      files.selectRow(0);
      const auto point = files.visualRect(files.model()->index(0, 0)).center();
      QVERIFY(files.viewport()->rect().contains(point));
      QTimer::singleShot(0, &window, [&] {
        if (auto* popup = window.findChild<QMenu*>("files-priority-context")) {
          if (auto* publish = popup->findChild<QAction*>("files-copy-local-media")) { publish->trigger(); }
          popup->close();
        }
      });
      emit files.customContextMenuRequested(point);
      QTest::qWait(250);
    }
    QVERIFY(!publications.isEmpty());
    QVERIFY2(media.count() == 2, messages.isEmpty() ? "No publication result" :
        messages.last().at(0).toString().toUtf8().constData());
    const auto url = media.last().at(3).toByteArray();
    QVERIFY(url.startsWith("http://127.0.0.1:" + QByteArray::number(port) + "/Content/"));
    QCOMPARE(QApplication::clipboard()->text(), QString::fromLatin1(url));
    if (alternate) {
      const auto republished = fetch();
      QVERIFY(republished.startsWith("HTTP/1.1 206 ") && republished.contains("Content-Type: video/x-matroska\r\n"));
      qInfo("PASS desktop renderer negotiates the original HTTP MIME alternative and retires it on Stop");
    }

    action->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(media.count(), 3, 10000);
    QCOMPARE(media.last().at(1).toUInt(), 0u);
    QVERIFY(bridge.closeSession());
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 15000);
  }
};

QTEST_MAIN(MediaHostTests)
#include "media_host_tests.moc"
