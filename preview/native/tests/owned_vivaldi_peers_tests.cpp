// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_udp/owned_transport.hpp"
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QtTest>
#include <bit>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace udp=biglybtc::services::azureus_udp;
namespace packet=udp::packet;
using namespace std::chrono_literals;
namespace {
udp::Config observing(){udp::Config config;config.servePings=true;config.enableVivaldi=true;config.observeOperations=true;return config;}
bool startPair(udp::OwnedPingTransport& client,udp::OwnedPingTransport& server,unsigned lastOctet){
  return server.start(QHostAddress(QStringLiteral("127.0.0.%1").arg(lastOctet)))&&server.allowPeer(client.endpoint())&&client.allowPeer(server.endpoint());
}
bool endpointIs(const biglybtc::services::azureus_dht::Address& value,const udp::Peer& peer){
  if(value.length!=4||value.port!=peer.port){return false;}const auto ip=peer.address.toIPv4Address();
  for(unsigned i=0;i<4;++i){if(value.bytes[i]!=static_cast<std::uint8_t>(ip>>(24-8*i))){return false;}}return true;
}
bool ping(udp::OwnedPingTransport& client,udp::OwnedPingTransport& server,QObject& receiver,int& replies,std::optional<udp::Ticket>& ticket){
  const auto before=replies;const auto sent=client.ping(server.endpoint(),55,&receiver,[&replies](udp::Outcome result){if(result){++replies;}},2s);
  if(!sent){return false;}ticket=*sent;return QTest::qWaitFor([&]{return replies==before+1;},3000);
}
// An explicitly owned raw peer supplies protocol boundary inputs. It is not
// claimed as a physical coordinate provider or original-Java execution.
bool report(udp::OwnedPingTransport& client,QUdpSocket& peer,QObject& receiver,packet::positions::Positions positions,int& replies){
  const auto before=replies;const auto ticket=client.ping({peer.localAddress(),peer.localPort()},55,&receiver,[&replies](udp::Outcome result){if(result){++replies;}},2s);if(!ticket){return false;}
  if(!QTest::qWaitFor([&]{return peer.hasPendingDatagrams();},2000)){return false;}
  const auto incoming=peer.receiveDatagram(8192);if(!incoming.isValid()){return false;}const auto data=incoming.data();
  packet::Context context;context.header.network=4;const auto request=packet::decode(packet::udp::Kind::Request,{data.constData(),static_cast<std::size_t>(data.size())},context);if(!request){return false;}
  const auto& header=request->header().fields();packet::ReplyFields fields;fields.header.network=4;fields.header.remoteVersion=header.originatorVersion;fields.header.connection=header.connection;fields.header.transaction=header.transaction;fields.positions=positions;
  const auto encoded=packet::encodeReply(fields);if(!encoded){return false;}
  if(peer.writeDatagram(encoded->data(),static_cast<qint64>(encoded->size()),client.endpoint().address,client.endpoint().port)!=static_cast<qint64>(encoded->size())){return false;}
  return QTest::qWaitFor([&]{return replies==before+1;},3000);
}
packet::positions::Positions position(udp::VivaldiValue value){
  return {packet::positions::vivaldiV1({std::bit_cast<std::uint32_t>(value.coordinates.x),std::bit_cast<std::uint32_t>(value.coordinates.y),std::bit_cast<std::uint32_t>(value.coordinates.h),std::bit_cast<std::uint32_t>(value.error)})};
}
}
class OwnedVivaldiPeersTests final:public QObject{
 Q_OBJECT
private slots:
 void emptyGetterNeverActivatesAndDisabledOwnerRetainsNoReports(){
   udp::OwnedPingTransport initial(observing());const auto empty=initial.dhtStatisticsSnapshot();QVERIFY(empty);
   QVERIFY(empty->peerPositions().empty());QCOMPARE(empty->droppedPeerPositions(),std::uint64_t{0});QVERIFY(!empty->peerPositionsLost());
   QCOMPARE(empty->transport().state(),udp::TransportStatisticsState::NotStarted);QCOMPARE(initial.endpoint().port,quint16{0});QVERIFY(!initial.deadlineActive());
   bool refused=false;std::jthread other([&]{const auto result=initial.dhtStatisticsSnapshot();refused=!result&&result.error()==udp::Failure::Thread;});other.join();QVERIFY(refused);
   udp::OwnedPingTransport disabled(udp::Config{}),remote(observing());QVERIFY(disabled.start());QVERIFY(startPair(disabled,remote,2));QObject receiver;int replies=0;std::optional<udp::Ticket> ticket;
   QVERIFY(ping(disabled,remote,receiver,replies,ticket));const auto ignored=disabled.dhtStatisticsSnapshot();QVERIFY(ignored);QVERIFY(!ignored->vivaldiEnabled());QVERIFY(ignored->peerPositions().empty());QVERIFY(!ignored->remotePosition());
 }
 void actualDistinctOwnersRefreshOnePeerAndPreserveLegacyAndClosedSnapshots(){
   udp::OwnedPingTransport client(observing()),first(observing()),second(observing());QVERIFY(client.start());QVERIFY(startPair(client,first,2));QVERIFY(startPair(client,second,3));
   QObject receiver;int warm=0,replies=0;std::optional<udp::Ticket> ticket;
   // Genuine PING observations let each remote owner compute its own report;
   // no synthetic reported coordinate is installed in these owners.
   QVERIFY(ping(first,client,receiver,warm,ticket));QVERIFY(ping(second,client,receiver,warm,ticket));
   const auto firstOwner=first.dhtStatisticsSnapshot(),secondOwner=second.dhtStatisticsSnapshot();QVERIFY(firstOwner&&secondOwner);
   QVERIFY(firstOwner->coordinateObservations()>0&&secondOwner->coordinateObservations()>0);
   QVERIFY(ping(client,first,receiver,replies,ticket));const auto firstTicket=*ticket;
   const auto one=client.dhtStatisticsSnapshot();QVERIFY(one);QCOMPARE(one->peerPositions().size(),std::size_t{1});
   const auto& reportOne=one->peerPositions()[0];QVERIFY(endpointIs(reportOne.endpoint,first.endpoint()));QVERIFY(reportOne.position==firstOwner->localPosition());
   QCOMPARE(reportOne.generation,one->transport().generation());QCOMPARE(reportOne.sequence,std::uint64_t{1});QCOMPARE(reportOne.ticket,firstTicket.value);
   QVERIFY(reportOne.rttNanoseconds>0&&reportOne.observedSteadyNanoseconds>=0&&reportOne.localUpdateAccepted);
   QVERIFY(one->pingObservation());QCOMPARE(one->pingObservation()->kind(),udp::PingObservationKind::Reply);QVERIFY(one->pingObservation()->rttNanoseconds());
   QVERIFY(ping(client,second,receiver,replies,ticket));const auto two=client.dhtStatisticsSnapshot();QVERIFY(two);QCOMPARE(two->peerPositions().size(),std::size_t{2});
   QVERIFY(endpointIs(two->peerPositions()[1].endpoint,second.endpoint()));QVERIFY(two->peerPositions()[1].position==secondOwner->localPosition());
   QVERIFY(two->peerPositions()[0].sequence<two->peerPositions()[1].sequence);QCOMPARE(one->peerPositions().size(),std::size_t{1});QCOMPARE(one->peerPositions()[0].sequence,std::uint64_t{1});
   QVERIFY(ping(first,client,receiver,warm,ticket));const auto updatedFirst=first.dhtStatisticsSnapshot();QVERIFY(updatedFirst);
   QVERIFY(ping(client,first,receiver,replies,ticket));const auto refreshed=client.dhtStatisticsSnapshot();QVERIFY(refreshed);QCOMPARE(refreshed->peerPositions().size(),std::size_t{2});
   QVERIFY(endpointIs(refreshed->peerPositions()[0].endpoint,second.endpoint()));const auto& latest=refreshed->peerPositions()[1];QVERIFY(endpointIs(latest.endpoint,first.endpoint()));
   QCOMPARE(latest.sequence,std::uint64_t{3});QCOMPARE(latest.ticket,ticket->value);QVERIFY(latest.position==updatedFirst->localPosition());
   QVERIFY(refreshed->remotePosition()&&*refreshed->remotePosition()==latest.position);QCOMPARE(refreshed->lastRttNanoseconds(),std::optional<std::int64_t>{latest.rttNanoseconds});
   QCOMPARE(refreshed->droppedPeerPositions(),std::uint64_t{0});QVERIFY(!refreshed->peerPositionsLost());
   client.close();const auto closed=client.dhtStatisticsSnapshot();QVERIFY(closed);QCOMPARE(closed->transport().state(),udp::TransportStatisticsState::Closed);
   QCOMPARE(closed->peerPositions().size(),std::size_t{2});QCOMPARE(closed->peerPositions()[1].sequence,latest.sequence);QVERIFY(closed->peerPositions()[1].position==latest.position);
   QCOMPARE(one->peerPositions()[0].ticket,firstTicket.value);QCOMPARE(client.pendingCount()+client.completionCount(),std::size_t{0});QVERIFY(!client.deadlineActive());
 }
 void sixtyFifthActualPeerEvictsOldestReportAndRefreshDoesNotGrowOrDrop(){
   udp::OwnedPingTransport client(observing());QVERIFY(client.start());QObject receiver;int replies=0;std::optional<udp::Ticket> ticket;
   std::vector<std::unique_ptr<udp::OwnedPingTransport>> peers;peers.reserve(65);
   for(unsigned i=0;i<65;++i){auto server=std::make_unique<udp::OwnedPingTransport>(observing());QVERIFY(startPair(client,*server,i+2));peers.push_back(std::move(server));}
   for(std::size_t i=0;i<64;++i){QVERIFY(ping(client,*peers[i],receiver,replies,ticket));}
   const auto full=client.dhtStatisticsSnapshot();QVERIFY(full);QCOMPARE(full->peerPositions().size(),udp::OwnedDhtStatisticsSnapshot::MaximumPeers);QCOMPARE(full->droppedPeerPositions(),std::uint64_t{0});
   QCOMPARE(full->peerPositions().front().sequence,std::uint64_t{1});QVERIFY(endpointIs(full->peerPositions().front().endpoint,peers[0]->endpoint()));
   QVERIFY(ping(client,*peers[64],receiver,replies,ticket));const auto bounded=client.dhtStatisticsSnapshot();QVERIFY(bounded);QCOMPARE(bounded->peerPositions().size(),std::size_t{64});QCOMPARE(bounded->droppedPeerPositions(),std::uint64_t{1});
   QCOMPARE(bounded->peerPositions().front().sequence,std::uint64_t{2});QCOMPARE(bounded->peerPositions().back().sequence,std::uint64_t{65});QVERIFY(endpointIs(bounded->peerPositions().back().endpoint,peers[64]->endpoint()));
   for(const auto& value:bounded->peerPositions()){QVERIFY(!endpointIs(value.endpoint,peers[0]->endpoint()));QVERIFY(udp::validVivaldiValue(value.position));QVERIFY(value.rttNanoseconds>0&&value.ticket!=0);}
   QVERIFY(ping(client,*peers[1],receiver,replies,ticket));const auto refreshed=client.dhtStatisticsSnapshot();QVERIFY(refreshed);QCOMPARE(refreshed->peerPositions().size(),std::size_t{64});QCOMPARE(refreshed->droppedPeerPositions(),std::uint64_t{1});
   QCOMPARE(refreshed->peerPositions().front().sequence,std::uint64_t{3});QCOMPARE(refreshed->peerPositions().back().sequence,std::uint64_t{66});QVERIFY(endpointIs(refreshed->peerPositions().back().endpoint,peers[1]->endpoint()));
   QCOMPARE(full->peerPositions().front().sequence,std::uint64_t{1});QCOMPARE(full->droppedPeerPositions(),std::uint64_t{0});QVERIFY(!refreshed->peerPositionsLost());
 }
 void actualInvalidOrUnsupportedReportsDoNotReplaceValidPeerOrMintCoordinates(){
   udp::OwnedPingTransport client(observing());QVERIFY(client.start());QUdpSocket peer;peer.setProxy(QNetworkProxy::NoProxy);QVERIFY(peer.bind(QHostAddress(QStringLiteral("127.0.0.2")),0,QUdpSocket::DontShareAddress));QVERIFY(client.allowPeer({peer.localAddress(),peer.localPort()}));
   QObject receiver;int replies=0;const udp::VivaldiValue actual{{1,2,3},1};QVERIFY(report(client,peer,receiver,position(actual),replies));const auto before=client.dhtStatisticsSnapshot();QVERIFY(before);QCOMPARE(before->peerPositions().size(),std::size_t{1});
   const std::array<udp::VivaldiValue,4> invalid{{{{std::numeric_limits<float>::quiet_NaN(),0,0},1},{{0,std::numeric_limits<float>::infinity(),0},1},{{0,0,30001},1},{{0,0,0},std::numeric_limits<float>::infinity()}}};
   for(const auto& value:invalid){QVERIFY(report(client,peer,receiver,position(value),replies));const auto next=client.dhtStatisticsSnapshot();QVERIFY(next);QCOMPARE(next->peerPositions().size(),std::size_t{1});QCOMPARE(next->peerPositions()[0].sequence,before->peerPositions()[0].sequence);QVERIFY(next->peerPositions()[0].position==actual);QCOMPARE(next->coordinateObservations(),before->coordinateObservations());}
   packet::positions::Positions unsupported{packet::positions::WirePosition{2,4,"v2??"}};QVERIFY(report(client,peer,receiver,std::move(unsupported),replies));QVERIFY(report(client,peer,receiver,{},replies));
   const auto after=client.dhtStatisticsSnapshot();QVERIFY(after);QCOMPARE(after->peerPositions().size(),std::size_t{1});QCOMPARE(after->peerPositions()[0].sequence,std::uint64_t{1});QVERIFY(after->remotePosition()&&*after->remotePosition()==*before->remotePosition());
   QCOMPARE(after->lastRttNanoseconds(),before->lastRttNanoseconds());QVERIFY(after->pingObservation());QCOMPARE(after->pingObservation()->sequence(),std::uint64_t{7});QCOMPARE(after->pingObservation()->kind(),udp::PingObservationKind::Reply);
 }
 void validReceivedReportRemainsObservableWhenSourceLocalUpdateDeclines(){
   udp::OwnedPingTransport client(observing());QVERIFY(client.start());QUdpSocket peer;peer.setProxy(QNetworkProxy::NoProxy);QVERIFY(peer.bind(QHostAddress(QStringLiteral("127.0.0.2")),0,QUdpSocket::DontShareAddress));QVERIFY(client.allowPeer({peer.localAddress(),peer.localPort()}));QObject receiver;int replies=0;
   // Original validity requires finite error; local.error10 +remote.error-10
   // then rejects the update. This real delivered report must not masquerade
   // as an accepted local coordinate update or disappear from the peer cloud.
   const udp::VivaldiValue finite{{1,2,3},-10};QVERIFY(udp::validVivaldiValue(finite));QVERIFY(report(client,peer,receiver,position(finite),replies));
   const auto value=client.dhtStatisticsSnapshot();QVERIFY(value);QCOMPARE(value->peerPositions().size(),std::size_t{1});QVERIFY(value->peerPositions()[0].position==finite);QVERIFY(!value->peerPositions()[0].localUpdateAccepted);QVERIFY(value->peerPositions()[0].rttNanoseconds>0);
   QCOMPARE(value->coordinateObservations(),std::uint64_t{0});QVERIFY(!value->remotePosition()&&!value->lastRttNanoseconds());QVERIFY(value->pingObservation());QCOMPARE(value->pingObservation()->kind(),udp::PingObservationKind::Reply);
 }
};
QTEST_GUILESS_MAIN(OwnedVivaldiPeersTests)
#include "owned_vivaldi_peers_tests.moc"
