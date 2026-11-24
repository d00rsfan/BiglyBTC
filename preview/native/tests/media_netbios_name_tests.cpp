// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_netbios_name.hpp"

#include <QNetworkDatagram>
#include <QtEndian>
#include <QtTest>

namespace {
using namespace biglybtc::media;
using namespace std::chrono_literals;
constexpr auto originalQuery = "11220000000100000000000020434b4141414141414141414141414141414141414141414141414141414141410000210001";

QByteArray answer(std::uint16_t transaction, bool compressed = false, bool empty = false) {
  const auto query = makeNetbiosNameQuery(transaction);
  auto packet = QByteArray::fromHex("000084000000000100000000");
  qToBigEndian(transaction, packet.data());
  if (compressed) {
    packet[5] = 1;
    packet += query.sliced(12);
    packet += QByteArray::fromHex("c00c");
  } else {
    packet += query.sliced(12, 34);
  }
  packet += QByteArray::fromHex("00210001000000000000");
  const auto lengthOffset = packet.size() - 2;
  const auto dataOffset = packet.size();
  packet += char(empty ? 0 : 2);
  if (!empty) {
    packet += QByteArray("MEDIA-SERVER").leftJustified(15);
    packet += QByteArray::fromHex("000400");
    packet += QByteArray("WORKGROUP").leftJustified(15);
    packet += QByteArray::fromHex("008400");
  }
  packet += QByteArray(46, '\0');
  qToBigEndian(static_cast<std::uint16_t>(packet.size() - dataOffset), packet.data() + lengthOffset);
  return packet;
}
} // namespace

class MediaNetbiosNameTests final : public QObject {
  Q_OBJECT
private slots:
  void pinnedNodeStatusCodec() {
    QCOMPARE(makeNetbiosNameQuery(0x1122).toHex(), originalQuery);
    const auto packet = answer(0x1122);
    QCOMPARE(decodeNetbiosName(packet, 0x1122), std::optional{QStringLiteral("MEDIA-SERVER")});
    QCOMPARE(decodeNetbiosName(answer(0x1122, true), 0x1122), std::optional{QStringLiteral("MEDIA-SERVER")});
    QCOMPARE(decodeNetbiosName(answer(0x1122, false, true), 0x1122), std::optional{QString{}});
    QVERIFY(!decodeNetbiosName(packet, 0x1123));
    for (qsizetype size = 0; size < packet.size(); ++size) {
      QVERIFY(!decodeNetbiosName(QByteArrayView(packet).first(size), 0x1122));
    }
    auto wrongCount = packet;
    wrongCount[56] = 33;
    QVERIFY(!decodeNetbiosName(wrongCount, 0x1122));
  }

  void actualEndpointAndNegativeCache() {
    QUdpSocket peer;
    QUdpSocket other;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0, QUdpSocket::DontShareAddress));
    QVERIFY(other.bind(QHostAddress::LocalHost, 0, QUdpSocket::DontShareAddress));
    MediaNetbiosName names;
    QVERIFY(!names.request(QHostAddress::AnyIPv4, peer.localPort(), 1s));
    QVERIFY(names.request(QHostAddress::LocalHost, peer.localPort(), 2s));
    QVERIFY(names.request(QHostAddress::LocalHost, peer.localPort(), 2s));
    QVERIFY(!names.request(QHostAddress::LocalHost, other.localPort(), 2s));
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(), 1500);
    const auto request = peer.receiveDatagram();
    QCOMPARE(request.senderAddress(), QHostAddress(QHostAddress::LocalHost));
    QCOMPARE(request.data().size(), 50);
    const auto transaction = qFromBigEndian<std::uint16_t>(request.data().constData());
    QCOMPARE(request.data(), makeNetbiosNameQuery(transaction));
    QVERIFY(other.writeDatagram(answer(transaction), request.senderAddress(), request.senderPort()) > 0);
    QVERIFY(peer.writeDatagram(answer(transaction ^ 1), request.senderAddress(), request.senderPort()) > 0);
    QTest::qWait(25);
    QVERIFY(!names.name(peer.localPort()));
    QVERIFY(peer.writeDatagram(answer(transaction), request.senderAddress(), request.senderPort()) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(names.name(peer.localPort()), std::optional{QStringLiteral("MEDIA-SERVER")}, 1500);
    QVERIFY(names.request(QHostAddress::LocalHost, peer.localPort(), 1s));
    QVERIFY(names.request(QHostAddress::LocalHost, other.localPort(), 100ms));
    QTRY_VERIFY_WITH_TIMEOUT(other.hasPendingDatagrams(), 1000);
    other.receiveDatagram();
    QTRY_COMPARE_WITH_TIMEOUT(names.name(other.localPort()), std::optional{QString{}}, 1500);
    QVERIFY(names.request(QHostAddress::LocalHost, other.localPort(), 100ms));
    QTest::qWait(25);
    QVERIFY(!peer.hasPendingDatagrams() && !other.hasPendingDatagrams());
    names.clear();
    QVERIFY(!names.name(peer.localPort()) && !names.name(other.localPort()));
  }
};

QTEST_GUILESS_MAIN(MediaNetbiosNameTests)
#include "media_netbios_name_tests.moc"
