// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_transport.hpp"
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QtTest>
#include <bit>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
namespace udp=biglybtc::services::azureus_udp;
namespace packet=udp::packet;
using namespace std::chrono_literals;
namespace {
bool bind(QUdpSocket& socket,const QHostAddress& address=QHostAddress::LocalHost) {
  socket.setProxy(QNetworkProxy::NoProxy);return socket.bind(address,0,QUdpSocket::DontShareAddress);
}
udp::Peer endpoint(const QUdpSocket& socket) { return {socket.localAddress(),socket.localPort()}; }
bool send(QUdpSocket& socket,const std::string& bytes,const udp::Peer& target) {
  return socket.writeDatagram(bytes.data(),static_cast<qint64>(bytes.size()),target.address,target.port)==static_cast<qint64>(bytes.size());
}
QNetworkDatagram take(QUdpSocket& socket) {
  if (!QTest::qWaitFor([&] { return socket.hasPendingDatagrams(); },2000)) { return {}; }
  return socket.receiveDatagram(8192);
}
std::expected<packet::Decoded,packet::Error> request(const QNetworkDatagram& datagram,std::int32_t network=4) {
  packet::Context context;context.header.network=network;const auto bytes=datagram.data();
  return packet::decode(packet::udp::Kind::Request,{bytes.constData(),static_cast<std::size_t>(bytes.size())},context);
}
std::expected<std::string,packet::Error> reply(const packet::Decoded& request,std::int32_t network=4,bool badConnection=false,bool badTransaction=false) {
  const auto& h=request.header().fields();packet::ReplyFields fields;fields.header.network=network;
  fields.header.connection=h.connection^(badConnection?1:0);fields.header.transaction=h.transaction^(badTransaction?1:0);
  fields.header.remoteVersion=h.originatorVersion;packet::positions::Positions empty;fields.positions=empty;
  return packet::encodeReply(fields);
}
}
class OwnedUdpTests final : public QObject {
  Q_OBJECT
private slots:
  void passiveSnapshotIsOwnedBoundedAndNeverStartsActor() {
    static_assert(std::is_trivially_copyable_v<udp::TransportStatisticsSnapshot>);
    static_assert(sizeof(udp::TransportStatisticsSnapshot)<=512);
    udp::Config config;config.network=23;config.version=54;udp::OwnedPingTransport client(std::move(config));
    const auto first=client.statisticsSnapshot();QVERIFY(first);QVERIFY(first->generation()!=0);QCOMPARE(first->publication(),std::uint64_t{1});
    QCOMPARE(first->network(),std::int32_t{23});QCOMPARE(first->protocolVersion(),std::int8_t{54});
    QCOMPARE(first->family(),udp::TransportStatisticsFamily::Unbound);QCOMPARE(first->state(),udp::TransportStatisticsState::NotStarted);
    QCOMPARE(first->pending()+first->completions()+first->allowedPeers(),std::size_t{0});
    QCOMPARE(first->counters().sent+first->counters().received+first->counters().readTurns+first->counters().deliveryTurns,std::uint64_t{0});
    QCOMPARE(client.endpoint().port,quint16{0});QVERIFY(!client.deadlineActive());
    udp::OwnedPingTransport separate(udp::Config{});const auto other=separate.statisticsSnapshot();QVERIFY(other);QVERIFY(other->generation()!=first->generation());
    bool refused=false;std::jthread wrongOwner([&]{const auto result=client.statisticsSnapshot();refused=!result && result.error()==udp::Failure::Thread;});wrongOwner.join();QVERIFY(refused);
    const auto second=client.statisticsSnapshot();QVERIFY(second);QCOMPARE(second->generation(),first->generation());QCOMPARE(second->publication(),std::uint64_t{2});
    QCOMPARE(first->publication(),std::uint64_t{1});QCOMPARE(client.endpoint().port,quint16{0});QVERIFY(!client.deadlineActive());
  }
  void numericEndpointsAndBounds() {
    udp::Config config;config.maximumPeers=1;udp::OwnedPingTransport client(std::move(config));
    auto invalid=client.start(QHostAddress(QStringLiteral("192.0.2.1")));QVERIFY(!invalid);QCOMPARE(invalid.error(),udp::Failure::Peer);
    QVERIFY(client.start());const auto port=client.endpoint().port;QVERIFY(port!=0);QVERIFY(!client.start());QCOMPARE(client.endpoint().port,port);
    QUdpSocket first,second;QVERIFY(bind(first));QVERIFY(bind(second));QVERIFY(client.allowPeer(endpoint(first)));QVERIFY(client.allowPeer(endpoint(first)));
    auto full=client.allowPeer(endpoint(second));QVERIFY(!full);QCOMPARE(full.error(),udp::Failure::Capacity);
    QVERIFY(!client.allowPeer({QHostAddress::LocalHost,0}));
    auto publicPeer=client.allowPeer({QHostAddress(QStringLiteral("192.0.2.1")),first.localPort()});QVERIFY(!publicPeer);QCOMPARE(publicPeer.error(),udp::Failure::Peer);
    QObject receiver;auto callback=[](udp::Outcome){};
    auto deadline=client.ping(endpoint(first),55,&receiver,callback,0ns);QVERIFY(!deadline);QCOMPARE(deadline.error(),udp::Failure::Deadline);
    QVERIFY(!client.ping(endpoint(first),55,&receiver,callback,31s));QVERIFY(!client.ping(endpoint(first),50,&receiver,callback));
    QVERIFY(!client.ping(endpoint(first),55,nullptr,callback));QVERIFY(!client.ping(endpoint(first),55,&receiver,{}));
    const auto ownedPeer=endpoint(first);bool wrongThread=false;std::jthread thread([&] { auto r=client.allowPeer(ownedPeer);wrongThread=!r && r.error()==udp::Failure::Thread; });thread.join();QVERIFY(wrongThread);
    client.close();QCOMPARE(client.endpoint().port,quint16(0));QVERIFY(!client.allowPeer(endpoint(first)));QVERIFY(!client.deadlineActive());
    udp::Config bad;bad.maximumPending=257;udp::OwnedPingTransport rejected(std::move(bad));QVERIFY(!rejected.start());
  }
  void nativeRoundTrips_data() {
    QTest::addColumn<bool>("ipv6");QTest::addColumn<int>("network");QTest::addColumn<int>("version");QTest::addColumn<int>("remote");
    QTest::newRow("ipv4-bigly")<<false<<4<<55<<55;
    QTest::newRow("ipv4-azureus")<<false<<0<<54<<54;
    QTest::newRow("ipv4-originator-version-upgrade")<<false<<4<<55<<51;
    QTest::newRow("ipv6-azureus")<<true<<3<<54<<54;
    QTest::newRow("ipv6-bigly")<<true<<4<<55<<55;
  }
  void nativeRoundTrips() {
    QFETCH(bool,ipv6);QFETCH(int,network);QFETCH(int,version);QFETCH(int,remote);
    const QHostAddress address=ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost;
    udp::Config serverConfig;serverConfig.servePings=true;serverConfig.network=network;serverConfig.version=static_cast<std::int8_t>(version);serverConfig.instance=42;
    udp::Config clientConfig;clientConfig.network=network;clientConfig.version=static_cast<std::int8_t>(version);
    udp::OwnedPingTransport server(std::move(serverConfig)),client(std::move(clientConfig));QVERIFY(server.start(address));QVERIFY(client.start(address));
    QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));QObject receiver;
    int count=0;std::optional<packet::Decoded> received;std::optional<udp::Failure> error;
    auto ticket=client.ping(server.endpoint(),static_cast<std::int8_t>(remote),&receiver,[&](udp::Outcome result) {
      ++count;if (result) { received.emplace(std::move(*result)); }else { error=result.error(); }
    },2s);QVERIFY(ticket);QTRY_COMPARE_WITH_TIMEOUT(count,1,3000);QVERIFY(!error);QVERIFY(received);
    QCOMPARE(received->header().fields().network,network);QCOMPARE(received->header().fields().version,version);
    QCOMPARE(received->header().fields().instance,42);QVERIFY(received->header().fields().connection<0);QVERIFY(received->trailing().empty());
    QCOMPARE(client.pendingCount(),std::size_t(0));QCOMPARE(client.completionCount(),std::size_t(0));QVERIFY(!client.deadlineActive());
    QCOMPARE(client.statistics().replies,std::uint64_t(1));QCOMPARE(server.statistics().requests,std::uint64_t(1));
    const auto clientSnapshot=client.statisticsSnapshot(),serverSnapshot=server.statisticsSnapshot();QVERIFY(clientSnapshot && serverSnapshot);
    QCOMPARE(clientSnapshot->state(),udp::TransportStatisticsState::Bound);QCOMPARE(serverSnapshot->state(),udp::TransportStatisticsState::Bound);
    QCOMPARE(clientSnapshot->network(),network);QCOMPARE(serverSnapshot->network(),network);
    const auto family=ipv6?udp::TransportStatisticsFamily::IPv6:udp::TransportStatisticsFamily::IPv4;
    QCOMPARE(clientSnapshot->family(),family);QCOMPARE(serverSnapshot->family(),family);QVERIFY(clientSnapshot->generation()!=serverSnapshot->generation());
    QCOMPARE(clientSnapshot->counters().sent,std::uint64_t{1});QCOMPARE(clientSnapshot->counters().received,std::uint64_t{1});
    QCOMPARE(clientSnapshot->counters().replies,std::uint64_t{1});QCOMPARE(serverSnapshot->counters().requests,std::uint64_t{1});
    QCOMPARE(clientSnapshot->pending()+clientSnapshot->completions(),std::size_t{0});QCOMPARE(clientSnapshot->allowedPeers(),std::size_t{1});
  }
  void spoofMalformedOversizeAndDuplicate() {
    udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer,spoof;QVERIFY(bind(peer));QVERIFY(bind(spoof));QVERIFY(client.allowPeer(endpoint(peer)));
    QObject receiver;int calls=0;bool success=false;
    QVERIFY(client.ping(endpoint(peer),55,&receiver,[&](udp::Outcome outcome) { ++calls;success=outcome.has_value(); },2s));
    const auto datagram=take(peer);QVERIFY(datagram.isValid());const auto decoded=request(datagram);QVERIFY(decoded);const auto valid=reply(*decoded);QVERIFY(valid);
    QVERIFY(send(spoof,*valid,client.endpoint()));QTRY_COMPARE(client.statistics().unowned,std::uint64_t(1));
    QVERIFY(client.allowPeer(endpoint(spoof)));QVERIFY(send(spoof,*valid,client.endpoint()));
    const auto connection=reply(*decoded,4,true),transaction=reply(*decoded,4,false,true),network=reply(*decoded,23);QVERIFY(connection && transaction && network);
    QVERIFY(send(peer,*connection,client.endpoint()));QVERIFY(send(peer,*transaction,client.endpoint()));QVERIFY(send(peer,*network,client.endpoint()));
    auto wrongAction=*valid;wrongAction[3]=9;QVERIFY(send(peer,wrongAction,client.endpoint()));
    QVERIFY(send(peer,std::string(1401,'x'),client.endpoint()));QVERIFY(send(peer,"",client.endpoint()));QVERIFY(send(peer,valid->substr(0,12),client.endpoint()));
    QTRY_VERIFY(client.statistics().foreign>=3);QTRY_VERIFY(client.statistics().malformed>=4);QTRY_COMPARE(client.statistics().oversized,std::uint64_t(1));
    QCOMPARE(client.pendingCount(),std::size_t(1));QCOMPARE(calls,0);
    QVERIFY(send(peer,*valid,client.endpoint()));QVERIFY(send(peer,*valid,client.endpoint()));QTRY_COMPARE(calls,1);QVERIFY(success);
    QTRY_VERIFY(client.statistics().foreign>=4);QCOMPARE(calls,1);QVERIFY(!client.deadlineActive());
    const auto observation=client.statisticsSnapshot();QVERIFY(observation);QCOMPARE(observation->counters().foreign,client.statistics().foreign);
    QCOMPARE(observation->counters().malformed,client.statistics().malformed);QCOMPARE(observation->counters().oversized,std::uint64_t{1});
    const auto receives=client.statistics().received;QVERIFY(client.statisticsSnapshot());QCOMPARE(client.statistics().received,receives);
  }
  void deadlinesAndLateReplies() {
    udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));
    QObject receiver;int calls=0;std::optional<udp::Failure> failure;QElapsedTimer elapsed;elapsed.start();
    QVERIFY(client.ping(endpoint(peer),55,&receiver,[&](udp::Outcome outcome) { ++calls;if (!outcome) { failure=outcome.error(); } },30ms));
    QTRY_COMPARE_WITH_TIMEOUT(calls,1,2000);QVERIFY(failure);QCOMPARE(*failure,udp::Failure::Timeout);QVERIFY(elapsed.elapsed()>=30);QVERIFY(!client.deadlineActive());
    const auto first=take(peer);QVERIFY(first.isValid());calls=0;failure.reset();
    QVERIFY(client.ping(endpoint(peer),55,&receiver,[&](udp::Outcome outcome) { ++calls;if (!outcome) { failure=outcome.error(); } },30ms));
    const auto second=take(peer);QVERIFY(second.isValid());const auto decoded=request(second);QVERIFY(decoded);const auto valid=reply(*decoded);QVERIFY(valid);
    QVERIFY(send(peer,*valid,client.endpoint()));std::this_thread::sleep_for(50ms);
    QTRY_COMPARE_WITH_TIMEOUT(calls,1,2000);QVERIFY(failure);QCOMPARE(*failure,udp::Failure::Timeout);QVERIFY(!client.deadlineActive());
    const auto observation=client.statisticsSnapshot();QVERIFY(observation);QCOMPARE(observation->counters().timeouts,std::uint64_t{2});
    QCOMPARE(observation->counters().sent,std::uint64_t{2});QCOMPARE(observation->counters().replies,std::uint64_t{0});
    QCOMPARE(observation->pending()+observation->completions(),std::size_t{0});
  }
  void cancellationAndCompletionCapacity() {
    udp::Config config;config.maximumPending=1;udp::OwnedPingTransport client(std::move(config));QVERIFY(client.start());
    QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));QObject receiver;int calls=0;std::optional<udp::Failure> error;
    auto callback=[&](udp::Outcome outcome) { ++calls;if (!outcome) { error=outcome.error(); } };
    const auto first=client.ping(endpoint(peer),55,&receiver,callback,2s);QVERIFY(first);
    const auto activeLimit=client.ping(endpoint(peer),55,&receiver,callback);QVERIFY(!activeLimit);QCOMPARE(activeLimit.error(),udp::Failure::Capacity);
    QVERIFY(client.cancel(*first));QVERIFY(!client.cancel(*first));
    QCOMPARE(client.pendingCount(),std::size_t(0));QCOMPARE(client.completionCount(),std::size_t(1));QVERIFY(!client.deadlineActive());
    const auto queuedObservation=client.statisticsSnapshot();QVERIFY(queuedObservation);
    QCOMPARE(queuedObservation->pending(),std::size_t{0});QCOMPARE(queuedObservation->completions(),std::size_t{1});
    QCOMPARE(queuedObservation->counters().cancelled,std::uint64_t{1});
    const auto blocked=client.ping(endpoint(peer),55,&receiver,callback);QVERIFY(!blocked);QCOMPARE(blocked.error(),udp::Failure::Capacity);
    QTRY_COMPARE(calls,1);QVERIFY(error);QCOMPARE(*error,udp::Failure::Cancelled);
    QVERIFY(client.ping(endpoint(peer),55,&receiver,callback,2s));client.close();QTRY_COMPARE(calls,2);QCOMPARE(*error,udp::Failure::Closed);
    QCOMPARE(client.pendingCount()+client.completionCount(),std::size_t(0));QVERIFY(!client.deadlineActive());
    const auto closedObservation=client.statisticsSnapshot();QVERIFY(closedObservation);
    QCOMPARE(closedObservation->state(),udp::TransportStatisticsState::Closed);QCOMPARE(closedObservation->family(),udp::TransportStatisticsFamily::IPv4);
    QCOMPARE(closedObservation->pending()+closedObservation->completions()+closedObservation->allowedPeers(),std::size_t{0});
    QCOMPARE(queuedObservation->completions(),std::size_t{1}); // Earlier value remains independent after actual delivery/close.
    QVERIFY(closedObservation->publication()>queuedObservation->publication());
  }
  void receiverAndActorLifetimes() {
    udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));
    int calls=0;auto* receiver=new QObject;
    QVERIFY(client.ping(endpoint(peer),55,receiver,[&](udp::Outcome) { ++calls; },2s));delete receiver;
    QCOMPARE(client.pendingCount(),std::size_t(0));QVERIFY(!client.deadlineActive());
    receiver=new QObject;const auto ticket=client.ping(endpoint(peer),55,receiver,[&](udp::Outcome) { ++calls; },2s);QVERIFY(ticket);QVERIFY(client.cancel(*ticket));delete receiver;
    QTRY_COMPARE(client.completionCount(),std::size_t(0));QCOMPARE(calls,0);QVERIFY(client.statistics().abandoned>=2);
    const auto abandoned=client.statisticsSnapshot();QVERIFY(abandoned);QVERIFY(abandoned->counters().abandoned>=2);
    QCOMPARE(abandoned->pending()+abandoned->completions(),std::size_t{0});
    QObject survivor;auto actor=std::make_unique<udp::OwnedPingTransport>(udp::Config{});QVERIFY(actor->start());QVERIFY(actor->allowPeer(endpoint(peer)));
    const auto queued=actor->ping(endpoint(peer),55,&survivor,[&](udp::Outcome) { ++calls; },2s);QVERIFY(queued);QVERIFY(actor->cancel(*queued));actor.reset();
    QTest::qWait(20);QCOMPARE(calls,0);
  }
  void callbackCanDeleteActor() {
    udp::Config config;config.servePings=true;udp::OwnedPingTransport server(std::move(config));QVERIFY(server.start());
    auto* client=new udp::OwnedPingTransport(udp::Config{});QPointer<udp::OwnedPingTransport> guard(client);QVERIFY(client->start());
    QVERIFY(server.allowPeer(client->endpoint()));QVERIFY(client->allowPeer(server.endpoint()));QObject receiver;int calls=0;bool success=false;
    std::optional<udp::TransportStatisticsSnapshot> retained;
    QVERIFY(client->ping(server.endpoint(),55,&receiver,[&](udp::Outcome outcome) { success=outcome.has_value();++calls;const auto copy=client->statisticsSnapshot();if(copy){retained=*copy;}delete client; },2s));
    QTRY_COMPARE(calls,1);QVERIFY(success);QVERIFY(!guard);
    QVERIFY(retained);QVERIFY(retained->generation()!=0);QCOMPARE(retained->counters().replies,std::uint64_t{1});QCOMPARE(retained->pending(),std::size_t{0});
  }
  void reentrantAndThrowingCallbacks() {
    udp::Config config;config.servePings=true;udp::OwnedPingTransport server(std::move(config)),client(udp::Config{});QVERIFY(server.start());QVERIFY(client.start());
    QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));QObject receiver;int calls=0;bool valid=true;
    std::function<void(udp::Outcome)> handler;
    handler=[&](udp::Outcome outcome) {
      valid=valid && outcome.has_value();++calls;
      if (calls<3) { valid=valid && client.ping(server.endpoint(),55,&receiver,[&](udp::Outcome next) { handler(std::move(next)); },2s).has_value(); }
      QCoreApplication::processEvents();
    };
    QVERIFY(client.ping(server.endpoint(),55,&receiver,[&](udp::Outcome outcome) { handler(std::move(outcome)); },2s));QTRY_COMPARE(calls,3);QVERIFY(valid);
    QVERIFY(client.ping(server.endpoint(),55,&receiver,[](udp::Outcome) { throw std::runtime_error("Owned callback test"); },2s));
    QTRY_COMPARE(client.statistics().callbackErrors,std::uint64_t(1));QVERIFY(!client.deadlineActive());
    QVERIFY(client.ping(server.endpoint(),55,&receiver,[&](udp::Outcome outcome) { valid=valid && outcome.has_value();++calls;client.close(); },2s));
    QTRY_COMPARE(calls,4);QVERIFY(valid);QCOMPARE(client.pendingCount(),std::size_t(0));
  }
  void collisionAndRandomFailureAreBounded() {
    std::uint64_t words=0;bool fresh=false;udp::Config config;config.randomWord=[&] { ++words;return fresh?words:0; };
    udp::OwnedPingTransport client(std::move(config));QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));
    QObject receiver;int calls=0;const auto first=client.ping(endpoint(peer),55,&receiver,[&](udp::Outcome) { ++calls; },2s);QVERIFY(first);
    const auto collision=client.ping(endpoint(peer),55,&receiver,[](udp::Outcome){},2s);QVERIFY(!collision);QCOMPARE(collision.error(),udp::Failure::Capacity);QVERIFY(words<40);
    QCOMPARE(client.pendingCount(),std::size_t(1));QVERIFY(client.cancel(*first));QTRY_COMPARE(calls,1);fresh=true;
    const auto next=client.ping(endpoint(peer),55,&receiver,[&](udp::Outcome) { ++calls; },2s);QVERIFY(next);QVERIFY(client.cancel(*next));QTRY_COMPARE(calls,2);
    udp::Config bad;bad.randomWord=[]() -> std::uint64_t { throw std::runtime_error("Owned random failure"); };
    udp::OwnedPingTransport broken(std::move(bad));QVERIFY(broken.start());QVERIFY(broken.allowPeer(endpoint(peer)));
    const auto failure=broken.ping(endpoint(peer),55,&receiver,[](udp::Outcome){});QVERIFY(!failure);QCOMPARE(failure.error(),udp::Failure::Random);QVERIFY(!broken.deadlineActive());
  }
  void globalProxyIsBypassed() {
    QTcpServer rejector;QVERIFY(rejector.listen(QHostAddress::LocalHost,0));
    const auto previous=QNetworkProxy::applicationProxy();QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,QStringLiteral("127.0.0.1"),rejector.serverPort()));
    struct Restore { QNetworkProxy proxy;~Restore(){QNetworkProxy::setApplicationProxy(proxy);} } restore{previous};
    udp::Config config;config.servePings=true;udp::OwnedPingTransport server(std::move(config)),client(udp::Config{});QVERIFY(server.start());QVERIFY(client.start());
    QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));QObject receiver;bool finished=false,success=false;
    QVERIFY(client.ping(server.endpoint(),55,&receiver,[&](udp::Outcome outcome) { success=outcome.has_value();finished=true; },2s));
    QTRY_VERIFY(finished);QVERIFY(success);QVERIFY(!rejector.hasPendingConnections());
  }
  void boundedDrainAndOriginatorIsNotReplyRoute() {
    udp::Config config;config.servePings=true;udp::OwnedPingTransport server(std::move(config));QVERIFY(server.start());QUdpSocket peer,other;QVERIFY(bind(peer));QVERIFY(bind(other));QVERIFY(server.allowPeer(endpoint(peer)));
    for (int i=0;i<80;++i) { QVERIFY(send(peer,"!",server.endpoint())); }
    QTRY_VERIFY(server.statistics().received>=65);QVERIFY(server.statistics().readTurns>=2);
    packet::RequestFields fields;fields.header.network=4;fields.header.connection=-1;fields.header.transaction=17;packet::udp::Address fake;fake.bytes[0]=127;fake.bytes[3]=1;fake.port=other.localPort();fields.header.originator=fake;
    const auto wire=packet::encodeRequest(fields);QVERIFY(wire);QVERIFY(send(peer,*wire,server.endpoint()));const auto answer=take(peer);QVERIFY(answer.isValid());QVERIFY(!other.hasPendingDatagrams());
    packet::Context context;context.header.network=4;const auto data=answer.data();const auto decoded=packet::decode(packet::udp::Kind::Reply,{data.constData(),static_cast<std::size_t>(data.size())},context);QVERIFY(decoded);QCOMPARE(decoded->header().fields().transaction,17);
  }
};
QTEST_GUILESS_MAIN(OwnedUdpTests)
#include "owned_transport_tests.moc"
