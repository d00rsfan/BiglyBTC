// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_udp/owned_transport.hpp"
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QtTest>
#include <limits>

namespace udp=biglybtc::services::azureus_udp;
namespace packet=udp::packet;
using namespace std::chrono_literals;
namespace {
bool bind(QUdpSocket& socket){socket.setProxy(QNetworkProxy::NoProxy);return socket.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress);}
udp::Peer endpoint(const QUdpSocket& socket){return {socket.localAddress(),socket.localPort()};}
QNetworkDatagram take(QUdpSocket& socket){if(!QTest::qWaitFor([&]{return socket.hasPendingDatagrams();},2000)){return {}; }return socket.receiveDatagram(8192);}
qint64 send(QUdpSocket& socket,const QByteArray& payload,const udp::Peer& target){return socket.writeDatagram(payload.constData(),payload.size(),target.address,target.port);}
}
class OwnedDhtBytesTests final:public QObject{
 Q_OBJECT
private slots:
 void checkedAdditionKeepsActualZeroAndMakesOverflowStickyUnknown(){
   udp::Statistics value{};QVERIFY(value.sentPayloadBytes&&value.receivedPayloadBytes);
   QCOMPARE(*value.sentPayloadBytes,std::uint64_t{0});QCOMPARE(*value.receivedPayloadBytes,std::uint64_t{0});
   udp::observeSocketPayloadBytes(value.sentPayloadBytes,-1);udp::observeSocketPayloadBytes(value.sentPayloadBytes,0);
   QCOMPARE(*value.sentPayloadBytes,std::uint64_t{0});
   value.sentPayloadBytes=std::numeric_limits<std::uint64_t>::max()-2;
   udp::observeSocketPayloadBytes(value.sentPayloadBytes,2);QCOMPARE(*value.sentPayloadBytes,std::numeric_limits<std::uint64_t>::max());
   udp::observeSocketPayloadBytes(value.sentPayloadBytes,1);QVERIFY(!value.sentPayloadBytes);
   udp::observeSocketPayloadBytes(value.sentPayloadBytes,0);udp::observeSocketPayloadBytes(value.sentPayloadBytes,7);QVERIFY(!value.sentPayloadBytes);
   udp::observeSocketPayloadBytes(value.receivedPayloadBytes,7);QVERIFY(value.receivedPayloadBytes==std::optional<std::uint64_t>{7});
   QCOMPARE(value.sent+value.received+value.requests+value.replies+value.ioErrors,std::uint64_t{0});
 }
 void actualNumericLoopbackCountsDifferentPayloadLengthsAndRejectedDatagrams(){
   udp::OwnedPingTransport client(udp::Config{});const auto empty=client.statisticsSnapshot();QVERIFY(empty);
   QCOMPARE(empty->state(),udp::TransportStatisticsState::NotStarted);QVERIFY(empty->counters().sentPayloadBytes&&empty->counters().receivedPayloadBytes);
   QCOMPARE(*empty->counters().sentPayloadBytes,std::uint64_t{0});QCOMPARE(*empty->counters().receivedPayloadBytes,std::uint64_t{0});
   QVERIFY(client.start());QUdpSocket peer,stranger;QVERIFY(bind(peer));QVERIFY(bind(stranger));QVERIFY(client.allowPeer(endpoint(peer)));
   QObject receiver;int completed=0;bool successful=false;
   QVERIFY(client.ping(endpoint(peer),55,&receiver,[&](udp::Outcome outcome){++completed;successful=outcome.has_value();},2s));
   const auto first=take(peer);QVERIFY(first.isValid());QVERIFY(!first.data().isEmpty());
   const auto txFirst=static_cast<std::uint64_t>(first.data().size());const auto afterSend=client.statisticsSnapshot();QVERIFY(afterSend);
   QCOMPARE(*afterSend->counters().sentPayloadBytes,txFirst);QCOMPARE(*afterSend->counters().receivedPayloadBytes,std::uint64_t{0});
   packet::Context context;context.header.network=4;const auto firstData=first.data();
   auto request=packet::decode(packet::udp::Kind::Request,{firstData.constData(),static_cast<std::size_t>(firstData.size())},context);QVERIFY(request);
   const auto& incoming=request->header().fields();packet::ReplyFields fields;fields.header.network=4;
   fields.header.remoteVersion=incoming.originatorVersion;fields.header.connection=incoming.connection;fields.header.transaction=incoming.transaction;
   packet::positions::Positions positions;fields.positions=positions;const auto encoded=packet::encodeReply(fields);QVERIFY(encoded);
   const QByteArray response(encoded->data(),static_cast<qsizetype>(encoded->size()));const auto responseSent=send(peer,response,client.endpoint());QCOMPARE(responseSent,response.size());
   QTRY_COMPARE_WITH_TIMEOUT(completed,1,3000);QVERIFY(successful);
   const auto afterReply=client.statisticsSnapshot();QVERIFY(afterReply);QCOMPARE(*afterReply->counters().receivedPayloadBytes,static_cast<std::uint64_t>(responseSent));
   QCOMPARE(afterReply->counters().sent,std::uint64_t{1});QCOMPARE(afterReply->counters().received,std::uint64_t{1});QCOMPARE(afterReply->counters().replies,std::uint64_t{1});
   int cancelled=0;const auto second=client.findValue(endpoint(peer),55,udp::lookup::nodes::NodeId{},0,255,&receiver,[&](udp::FindValueOutcome outcome){if(!outcome&&outcome.error()==udp::Failure::Cancelled){++cancelled;}},2s);QVERIFY(second);
   const auto different=take(peer);QVERIFY(different.isValid());QVERIFY(different.data().size()!=first.data().size());
   const auto txTotal=txFirst+static_cast<std::uint64_t>(different.data().size());QCOMPARE(*client.statistics().sentPayloadBytes,txTotal);
   QVERIFY(client.cancel(*second));QTRY_COMPARE(cancelled,1);QCOMPARE(client.pendingCount()+client.completionCount(),std::size_t{0});QVERIFY(!client.deadlineActive());
   const auto malformedSent=send(peer,QByteArray("bad",3),client.endpoint());QCOMPARE(malformedSent,qint64{3});
   const auto unownedSent=send(stranger,QByteArray("unowned",7),client.endpoint());QCOMPARE(unownedSent,qint64{7});
   const auto zeroSent=send(peer,QByteArray{},client.endpoint());QCOMPARE(zeroSent,qint64{0});
   QTRY_COMPARE(client.statistics().received,std::uint64_t{4});QCOMPARE(client.statistics().unowned,std::uint64_t{1});QCOMPARE(client.statistics().malformed,std::uint64_t{2});
   const auto actual=client.statisticsSnapshot();QVERIFY(actual);QVERIFY(actual->counters().receivedPayloadBytes&&actual->counters().sentPayloadBytes);
   const auto rxTotal=static_cast<std::uint64_t>(responseSent+malformedSent+unownedSent+zeroSent);
   QCOMPARE(*actual->counters().receivedPayloadBytes,rxTotal);QCOMPARE(*actual->counters().sentPayloadBytes,txTotal);
   QCOMPARE(actual->counters().sent,std::uint64_t{2});QCOMPARE(actual->counters().cancelled,std::uint64_t{1});QCOMPARE(actual->counters().replies,std::uint64_t{1});
   QCOMPARE(*afterSend->counters().sentPayloadBytes,txFirst);QCOMPARE(*afterSend->counters().receivedPayloadBytes,std::uint64_t{0});
   // The actor's reply write is measured independently by the peer's actual
   // received payload, covering the responder path without a size constant.
   udp::Config answering;answering.servePings=true;udp::OwnedPingTransport server(std::move(answering));QVERIFY(server.start());QVERIFY(server.allowPeer(endpoint(peer)));
   QCOMPARE(send(peer,firstData,server.endpoint()),static_cast<qint64>(firstData.size()));const auto answer=take(peer);QVERIFY(answer.isValid());
   const auto answered=server.statisticsSnapshot();QVERIFY(answered);QCOMPARE(answered->counters().requests,std::uint64_t{1});QCOMPARE(answered->counters().sent,std::uint64_t{1});
   QCOMPARE(*answered->counters().receivedPayloadBytes,static_cast<std::uint64_t>(firstData.size()));QCOMPARE(*answered->counters().sentPayloadBytes,static_cast<std::uint64_t>(answer.data().size()));
   client.close();const auto closed=client.statisticsSnapshot();QVERIFY(closed);QCOMPARE(closed->state(),udp::TransportStatisticsState::Closed);
   QCOMPARE(closed->counters().receivedPayloadBytes,actual->counters().receivedPayloadBytes);QCOMPARE(closed->counters().sentPayloadBytes,actual->counters().sentPayloadBytes);
 }
 void boundedOversizeDiscardLeavesUnknownReceiveTotalWithoutAffectingPacketCounters(){
   udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));
   const auto before=client.statisticsSnapshot();QVERIFY(before);QCOMPARE(*before->counters().receivedPayloadBytes,std::uint64_t{0});
   QCOMPARE(send(peer,QByteArray(1401,'x'),client.endpoint()),qint64{1401});QTRY_COMPARE(client.statistics().oversized,std::uint64_t{1});
   const auto dropped=client.statisticsSnapshot();QVERIFY(dropped);QCOMPARE(dropped->counters().received,std::uint64_t{1});QVERIFY(!dropped->counters().receivedPayloadBytes);
   QVERIFY(dropped->counters().sentPayloadBytes);QCOMPARE(*dropped->counters().sentPayloadBytes,std::uint64_t{0});
   QCOMPARE(send(peer,QByteArray("xx",2),client.endpoint()),qint64{2});QTRY_COMPARE(client.statistics().received,std::uint64_t{2});QCOMPARE(client.statistics().malformed,std::uint64_t{1});
   QVERIFY(!client.statistics().receivedPayloadBytes);QCOMPARE(*before->counters().receivedPayloadBytes,std::uint64_t{0});
   QCOMPARE(client.pendingCount()+client.completionCount(),std::size_t{0});QVERIFY(!client.deadlineActive());
 }
};
QTEST_GUILESS_MAIN(OwnedDhtBytesTests)
#include "owned_dht_bytes_tests.moc"
