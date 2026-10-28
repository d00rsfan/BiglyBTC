// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QHostAddress>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>
using biglybtc::services::tor::ControlChannel;
namespace {
QByteArray nonce() {
  QByteArray bytes(32, '\0');
  for (int i = 0; i < 32; ++i) { bytes[i] = static_cast<char>(i); }
  return bytes;
}
const QByteArray challenge = "250 AUTHCHALLENGE SERVERHASH=9AC6D0EF401195DCFCBDCBC6E2876DA2DDB4319D4D92242C4E402C450BA848C8 SERVERNONCE=1F1E1D1C1B1A191817161514131211100F0E0D0C0B0A09080706050403020100";
const QByteArray proof = "AUTHENTICATE F40A1FFB4D7754B5206A40D9DB648ED970DC28CBF45C2AA35C2A9899AAF5B882";
// A fresh in-process loopback server for each test, never an existing Tor port.
class Peer final : public QObject {
public:
  enum class Mode { Good, Fragmented, BadProof, BadAck, Hang, Oversize, Eof, BadFrame };
  explicit Peer(Mode requestedMode = Mode::Good) : mode(requestedMode), server(this) {
    QObject::connect(&server, &QTcpServer::newConnection, this, [this] {
      while (server.hasPendingConnections()) {
        auto* client = server.nextPendingConnection();
        client->setReadBufferSize(4096);
        clients.append(client);
        QObject::connect(client, &QTcpSocket::readyRead, client, [this, client] {
          while (client->canReadLine()) {
            const auto line = client->readLine();
            commands.append(line);
            if (commands.size() == 1) {
              if (mode == Mode::Hang) { continue; }
              if (mode == Mode::Eof) { client->disconnectFromHost(); continue; }
              auto reply = challenge;
              if (mode == Mode::BadProof) { reply[29] = reply[29] == '0' ? '1' : '0'; }
              if (mode == Mode::Oversize) { reply = QByteArray(3000, 'x'); }
              if (mode == Mode::BadFrame) { reply.insert(50, '\r'); }
              if (mode == Mode::Fragmented) {
                client->write(reply.first(7));
                QTimer::singleShot(10, client, [client, reply] { client->write(reply.sliced(7) + "\r\n"); });
              } else { client->write(reply + "\r\n"); }
            } else if (commands.size() == 2) {
              client->write(mode == Mode::BadAck ? "515 Authentication failed\r\n" : "250 OK\r\n");
            } else if (line == "GETINFO version\r\n") {
              client->write("250-version=owned-test-only\r\n250 OK\r\n");
            }
          }
        });
      }
    });
    listening = server.listen(QHostAddress::LocalHost, 0);
  }
  quint16 port() const { return server.serverPort(); }
  Mode mode;
  QTcpServer server;
  QList<QByteArray> commands;
  QList<QPointer<QTcpSocket>> clients;
  bool listening = false;
};
}
class ChannelTests final : public QObject {
  Q_OBJECT
private slots:
  void authenticatedCommandsAndFragmentation_data() {
    QTest::addColumn<bool>("fragmented");
    QTest::newRow("whole") << false;
    QTest::newRow("fragmented") << true;
  }
  void authenticatedCommandsAndFragmentation() {
    QFETCH(bool, fragmented);
    Peer peer(fragmented ? Peer::Mode::Fragmented : Peer::Mode::Good);
    QVERIFY(peer.listening);
    ControlChannel channel;
    QSignalSpy ready(&channel, &ControlChannel::authenticated), failed(&channel, &ControlChannel::failed);
    QSignalSpy replies(&channel, &ControlChannel::replyLine);
    QVERIFY(!channel.sendCommand("GETINFO version"));
    QVERIFY(channel.begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(channel.state(), ControlChannel::State::Ready);
    QVERIFY(!channel.deadlineActive());
    QCOMPARE(peer.commands.size(), 2);
    QCOMPARE(peer.commands[0], "AUTHCHALLENGE SAFECOOKIE " + nonce().toHex().toUpper() + "\r\n");
    QCOMPARE(peer.commands[1], proof + "\r\n");
    QVERIFY(!channel.sendCommand("GETINFO version\r\nSIGNAL SHUTDOWN"));
    QVERIFY(!channel.sendCommand(QByteArray(1025, 'x')));
    QVERIFY(!channel.sendCommand(QByteArray("GETINFO\0version", 15)));
    QVERIFY(channel.sendCommand("GETINFO version"));
    QTRY_COMPARE_WITH_TIMEOUT(replies.count(), 2, 3000);
    QCOMPARE(replies[0][0].toByteArray(), QByteArray("250-version=owned-test-only"));
    QCOMPARE(replies[1][0].toByteArray(), QByteArray("250 OK"));
    QSignalSpy closed(&channel, &ControlChannel::closed);
    channel.stop(); channel.stop();
    QCOMPARE(closed.count(), 1);
    QCOMPARE(channel.state(), ControlChannel::State::Closed);
    QVERIFY(!channel.sendCommand("SIGNAL SHUTDOWN"));
    QVERIFY(!channel.begin(peer.port(), QByteArray(32, '\0'), nonce()));
  }
  void failureNeverEnablesCommands_data() {
    QTest::addColumn<int>("mode");
    for (auto mode : {Peer::Mode::BadProof, Peer::Mode::BadAck, Peer::Mode::Hang,
                     Peer::Mode::Oversize, Peer::Mode::Eof, Peer::Mode::BadFrame}) {
      QTest::newRow(qPrintable(QString::number(static_cast<int>(mode)))) << static_cast<int>(mode);
    }
  }
  void failureNeverEnablesCommands() {
    QFETCH(int, mode);
    Peer peer(static_cast<Peer::Mode>(mode));
    QVERIFY(peer.listening);
    ControlChannel channel;
    QSignalSpy ready(&channel, &ControlChannel::authenticated), failed(&channel, &ControlChannel::failed);
    QVERIFY(channel.begin(peer.port(), QByteArray(32, '\0'), nonce(),
        mode == static_cast<int>(Peer::Mode::Hang) ? 100 : 1000));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(channel.state(), ControlChannel::State::Failed);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!channel.deadlineActive());
    QVERIFY(!channel.sendCommand("GETINFO version"));
    QCOMPARE(peer.commands.size(), mode == static_cast<int>(Peer::Mode::BadAck) ? 2 : 1);
    channel.stop();
    QCOMPARE(failed.count(), 1);
  }
  void invalidParametersOpenNoConnection() {
    Peer peer;
    QVERIFY(peer.listening);
    ControlChannel channel;
    QSignalSpy failed(&channel, &ControlChannel::failed);
    QVERIFY(!channel.begin(peer.port(), QByteArray(31, '\0'), nonce()));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(channel.state(), ControlChannel::State::Failed);
    QTest::qWait(30);
    QVERIFY(peer.clients.empty());
    QVERIFY(!channel.deadlineActive());
  }
  void cancelFromStateNotification_data() {
    QTest::addColumn<int>("state");
    for (auto state : {ControlChannel::State::Connecting, ControlChannel::State::Challenge,
                      ControlChannel::State::Authentication, ControlChannel::State::Ready}) {
      QTest::newRow(qPrintable(QString::number(static_cast<int>(state)))) << static_cast<int>(state);
    }
  }
  void cancelFromStateNotification() {
    QFETCH(int, state);
    Peer peer;
    QVERIFY(peer.listening);
    ControlChannel channel;
    QSignalSpy ready(&channel, &ControlChannel::authenticated), closed(&channel, &ControlChannel::closed);
    connect(&channel, &ControlChannel::stateChanged, &channel, [&channel, state](ControlChannel::State value) {
      if (static_cast<int>(value) == state) { channel.stop(); }
    });
    QVERIFY(channel.begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000));
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 3000);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!channel.deadlineActive());
    QTest::qWait(20);
    const auto count = peer.commands.size();
    QCOMPARE(count, state == static_cast<int>(ControlChannel::State::Connecting)
        || state == static_cast<int>(ControlChannel::State::Challenge) ? 0
        : state == static_cast<int>(ControlChannel::State::Authentication) ? 1 : 2);
  }
  void destructionFromStateNotification_data() { cancelFromStateNotification_data(); }
  void destructionFromStateNotification() {
    QFETCH(int, state);
    Peer peer;
    QVERIFY(peer.listening);
    QPointer<ControlChannel> channel = new ControlChannel;
    connect(channel, &ControlChannel::stateChanged, &peer, [channel, state](ControlChannel::State value) {
      if (static_cast<int>(value) == state) { delete channel.data(); }
    });
    channel->begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(channel.isNull(), 3000);
    QTest::qWait(20);
  }
  void stopCancelsPendingDeadline() {
    Peer peer(Peer::Mode::Hang);
    QVERIFY(peer.listening);
    ControlChannel channel;
    QSignalSpy failed(&channel, &ControlChannel::failed), closed(&channel, &ControlChannel::closed);
    QVERIFY(channel.begin(peer.port(), QByteArray(32, '\0'), nonce(), 300));
    QTRY_COMPARE_WITH_TIMEOUT(peer.commands.size(), 1, 3000);
    channel.stop();
    QTest::qWait(350);
    QCOMPARE(channel.state(), ControlChannel::State::Closed);
    QCOMPARE(closed.count(), 1); QCOMPARE(failed.count(), 0);
  }
  void destructionFromAuthenticatedOrReply_data() {
    QTest::addColumn<bool>("onReply");
    QTest::newRow("authenticated") << false;
    QTest::newRow("reply") << true;
  }
  void destructionFromAuthenticatedOrReply() {
    QFETCH(bool, onReply);
    Peer peer;
    QVERIFY(peer.listening);
    QPointer<ControlChannel> channel = new ControlChannel;
    connect(channel, &ControlChannel::authenticated, &peer, [channel, onReply] {
      if (onReply) { channel->sendCommand("GETINFO version"); }
      else { delete channel.data(); }
    });
    connect(channel, &ControlChannel::replyLine, &peer, [channel] { delete channel.data(); });
    channel->begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(channel.isNull(), 3000);
    QTest::qWait(20);
  }
  void remoteCloseAfterAuthentication() {
    Peer peer;
    QVERIFY(peer.listening);
    ControlChannel channel;
    QSignalSpy ready(&channel, &ControlChannel::authenticated), failed(&channel, &ControlChannel::failed);
    QVERIFY(channel.begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
    peer.clients[0]->disconnectFromHost();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(!channel.deadlineActive());
    QVERIFY(!channel.sendCommand("GETINFO version"));
    channel.stop();
    QCOMPARE(failed.count(), 1);
  }
  void queuedCancellationWinsAgainstReplyFlood() {
    Peer peer;
    QVERIFY(peer.listening);
    ControlChannel channel;
    QSignalSpy ready(&channel, &ControlChannel::authenticated), replies(&channel, &ControlChannel::replyLine);
    QSignalSpy closed(&channel, &ControlChannel::closed);
    QVERIFY(channel.begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
    connect(&channel, &ControlChannel::replyLine, &channel, [&channel, count = 0]() mutable {
      if (++count == 1) { QMetaObject::invokeMethod(&channel, &ControlChannel::stop, Qt::QueuedConnection); }
    });
    peer.clients[0]->write(QByteArray("650 EVENT\r\n").repeated(200));
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 3000);
    QVERIFY(replies.count() > 0 && replies.count() <= 32);
  }
  void destructionFromAuthenticatedAndFailure_data() {
    QTest::addColumn<bool>("failure");
    QTest::newRow("authenticated") << false;
    QTest::newRow("failed") << true;
  }
  void destructionFromAuthenticatedAndFailure() {
    QFETCH(bool, failure);
    Peer peer(failure ? Peer::Mode::BadAck : Peer::Mode::Good);
    QVERIFY(peer.listening);
    QPointer<ControlChannel> channel = new ControlChannel;
    const auto cleanup = qScopeGuard([&] { delete channel.data(); });
    QSignalSpy ready(channel, &ControlChannel::authenticated), failed(channel, &ControlChannel::failed);
    QSignalSpy closed(channel, &ControlChannel::closed);
    // Both signals have a deleter: a failure emitted during destruction of an
    // authenticated channel would reenter deletion before QPointer clears.
    connect(channel, &ControlChannel::authenticated, &peer, [channel] { delete channel.data(); });
    connect(channel, &ControlChannel::failed, &peer, [channel] { delete channel.data(); });
    QVERIFY(channel->begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000));
    QTRY_VERIFY_WITH_TIMEOUT(channel.isNull(), 3000);
    QCOMPARE(ready.count(), failure ? 0 : 1);
    QCOMPARE(failed.count(), failure ? 1 : 0);
    QCOMPARE(closed.count(), 0);
    QTest::qWait(20);
    QCOMPARE(ready.count(), failure ? 0 : 1);
    QCOMPARE(failed.count(), failure ? 1 : 0);
    QCOMPARE(closed.count(), 0);
    QCOMPARE(peer.commands.size(), 2);
    QVERIFY(peer.clients[0]);
    QTRY_COMPARE_WITH_TIMEOUT(peer.clients[0]->state(), QAbstractSocket::UnconnectedState, 3000);
  }
  void destructionEmitsNoPublicSignals_data() {
    QTest::addColumn<bool>("ready");
    QTest::newRow("ready") << true;
    QTest::newRow("pending-challenge") << false;
  }
  void destructionEmitsNoPublicSignals() {
    QFETCH(bool, ready);
    Peer peer(ready ? Peer::Mode::Good : Peer::Mode::Hang);
    QVERIFY(peer.listening);
    QPointer<ControlChannel> channel = new ControlChannel;
    const auto cleanup = qScopeGuard([&] { delete channel.data(); });
    QSignalSpy states(channel, &ControlChannel::stateChanged), authenticated(channel, &ControlChannel::authenticated);
    QSignalSpy replies(channel, &ControlChannel::replyLine), failed(channel, &ControlChannel::failed);
    QSignalSpy closed(channel, &ControlChannel::closed);
    QVERIFY(channel->begin(peer.port(), QByteArray(32, '\0'), nonce(), 1000));
    if (ready) {
      QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 3000);
      QCOMPARE(channel->state(), ControlChannel::State::Ready);
    } else {
      QTRY_COMPARE_WITH_TIMEOUT(peer.commands.size(), 1, 3000);
      QCOMPARE(channel->state(), ControlChannel::State::Challenge);
      QVERIFY(channel->deadlineActive());
    }
    QCOMPARE(replies.count(), 0); QCOMPARE(failed.count(), 0); QCOMPARE(closed.count(), 0);
    const auto stateCount = states.count(), readyCount = authenticated.count();
    delete channel.data();
    QVERIFY(channel.isNull());
    QCOMPARE(states.count(), stateCount); QCOMPARE(authenticated.count(), readyCount);
    QCOMPARE(replies.count(), 0); QCOMPARE(failed.count(), 0); QCOMPARE(closed.count(), 0);
    QTest::qWait(20);
    QCOMPARE(states.count(), stateCount); QCOMPARE(authenticated.count(), readyCount);
    QCOMPARE(replies.count(), 0); QCOMPARE(failed.count(), 0); QCOMPARE(closed.count(), 0);
    QCOMPARE(peer.commands.size(), ready ? 2 : 1);
    QVERIFY(peer.clients[0]);
    QTRY_COMPARE_WITH_TIMEOUT(peer.clients[0]->state(), QAbstractSocket::UnconnectedState, 3000);
  }
};
QTEST_GUILESS_MAIN(ChannelTests)
#include "channel_tests.moc"
