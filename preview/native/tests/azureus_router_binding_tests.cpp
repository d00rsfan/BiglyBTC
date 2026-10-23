#include "owned_azureus_service.hpp"
#include <QUdpSocket>
#include <QtTest>
#include <algorithm>
#include <stdexcept>

namespace {
using namespace biglybtc;
namespace udp=services::azureus_udp;
namespace nodes=services::azureus_nodes;
using namespace std::chrono_literals;

auto make(bool replies=true,const char* address="127.0.0.1"){
  app::AzureusServiceOptions options;options.enabled=true;options.servePings=replies;
  options.bindAddress=QHostAddress(QString::fromLatin1(address));
  auto result=app::OwnedAzureusService::create(options);
  if(!result){throw std::runtime_error("Owned loopback service could not start");}
  return std::move(*result);
}
bool finished(const app::OwnedAzureusService& owner,std::uint64_t request){
  const auto value=owner.observation();
  return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal;
}
bool successful(const app::OwnedAzureusService& owner,std::uint64_t request){
  const auto value=owner.observation();
  return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal
      &&!value.lastResult->error&&!value.lastResult->transportFailure;
}
auto endpoint(const udp::Peer& peer){
  nodes::ResolvedEndpoint value;value.address.port=peer.port;
  if(peer.address.protocol()==QAbstractSocket::IPv4Protocol){
    value.address.length=4;const auto ip=peer.address.toIPv4Address();
    for(unsigned i=0;i<4;++i){value.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}
  }else{
    value.address.length=16;const auto ip=peer.address.toIPv6Address();
    std::copy_n(ip.c,16,value.address.bytes.begin());
  }
  return value;
}
auto identity(const udp::Peer& peer){
  nodes::NodeCache cache;auto result=cache.lookup(endpoint(peer),55);
  if(!result){throw std::runtime_error("Actual numeric endpoint identity unavailable");}
  return result->id;
}
}

class AzureusRouterBindingTests final:public QObject {
  Q_OBJECT
private slots:
  void localRecordUsesTheActualBoundGenerationAndStaysPassive(){
    auto owner=make();const auto initial=owner->observation();
    QVERIFY(initial.current);QVERIFY(initial.snapshot);QVERIFY(initial.routerSnapshot);
    QVERIFY(!initial.routerFailure);
    QCOMPARE(initial.routerSnapshot->generation(),initial.snapshot->transport().generation());
    const auto& counts=initial.routerSnapshot->counts();
    QCOMPARE(counts[0],std::uint64_t{1});QCOMPARE(counts[1],std::uint64_t{1});
    QCOMPARE(counts[2],std::uint64_t{1});QCOMPARE(counts[3],std::uint64_t{0});
    QCOMPARE(counts[4],std::uint64_t{1});QCOMPARE(counts[5],std::uint64_t{0});
    QCOMPARE(counts[6],std::uint64_t{0});
    QCOMPARE(initial.routerSnapshot->buckets().size(),std::size_t{1});
    QVERIFY(initial.routerSnapshot->buckets().front().containsLocal);
    for(unsigned i=0;i<100;++i){
      const auto observed=owner->observation();QCOMPARE(observed.sequence,initial.sequence);
      QVERIFY(observed.routerSnapshot==initial.routerSnapshot);
      QCOMPARE(observed.snapshot->transport().counters().sent,std::uint64_t{0});
    }
    QVERIFY(owner->close());const auto closed=owner->observation();
    QVERIFY(closed.joined);QVERIFY(!closed.current);QVERIFY(closed.routerSnapshot);
    QVERIFY(closed.routerSnapshot==initial.routerSnapshot);
    QCOMPARE(initial.routerSnapshot->counts()[4],std::uint64_t{1});
  }

  void admittedPeerIsUnknownUntilItsActualCorrelatedReply(){
    auto client=make(),server=make(true,"127.0.0.2");
    QVERIFY(identity(client->endpoint())!=identity(server->endpoint()));
    const auto serverAllow=server->allowPeer(client->endpoint());QVERIFY(serverAllow);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*server,*serverAllow),3000);
    const auto known=client->allowPeer(server->endpoint());QVERIFY(known);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*known),3000);
    const auto before=client->observation();QVERIFY(before.routerSnapshot);
    QCOMPARE(before.snapshot->transport().allowedPeers(),std::size_t{1});
    QCOMPARE(before.routerSnapshot->counts()[2],std::uint64_t{2});
    QCOMPARE(before.routerSnapshot->counts()[4],std::uint64_t{1});
    QCOMPARE(before.routerSnapshot->counts()[5],std::uint64_t{1});
    QCOMPARE(before.snapshot->transport().counters().sent,std::uint64_t{0});
    QVERIFY(before.snapshot->events().empty()); // No router probe was executed.
    const auto ping=client->ping(server->endpoint());QVERIFY(ping);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*ping),3000);
    const auto after=client->observation();QVERIFY(after.routerSnapshot);
    QCOMPARE(after.routerSnapshot->counts()[4],std::uint64_t{2});
    QCOMPARE(after.routerSnapshot->counts()[5],std::uint64_t{0});
    QCOMPARE(after.routerSnapshot->counts()[6],std::uint64_t{0});
    QVERIFY(after.routerSnapshot->revision()>before.routerSnapshot->revision());
    QCOMPARE(before.routerSnapshot->counts()[5],std::uint64_t{1});
    QCOMPARE(after.snapshot->transport().counters().sent,std::uint64_t{1});
    QCOMPARE(after.snapshot->transport().counters().replies,std::uint64_t{1});
    // Incoming requests alone have no router callback in this bounded binding.
    QCOMPARE(server->observation().routerSnapshot->counts()[5],std::uint64_t{1});
    const auto reverse=server->ping(client->endpoint());QVERIFY(reverse);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*server,*reverse),3000);
    QCOMPARE(server->observation().routerSnapshot->counts()[4],std::uint64_t{2});
  }

  void actualUnknownPeerTimeoutsUseTheFailureThreshold(){
    auto client=make(),silent=make(false,"127.0.0.2");
    QVERIFY(identity(client->endpoint())!=identity(silent->endpoint()));
    const auto allow=silent->allowPeer(client->endpoint());QVERIFY(allow);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*silent,*allow),3000);
    std::shared_ptr<const services::azureus_router::RouterStatisticsSnapshot> first;
    for(unsigned attempt=0;attempt<3;++attempt){
      const auto ping=client->ping(silent->endpoint(),30ms);QVERIFY(ping);
      QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*ping),3000);
      const auto value=client->observation();QVERIFY(value.routerSnapshot);
      QCOMPARE(value.lastResult->transportFailure,std::optional{udp::Failure::Timeout});
      QCOMPARE(value.routerSnapshot->counts()[4],std::uint64_t{1});
      QCOMPARE(value.routerSnapshot->counts()[5],std::uint64_t{0});
      if(attempt<2){
        QCOMPARE(value.routerSnapshot->counts()[2],std::uint64_t{2});
        QCOMPARE(value.routerSnapshot->counts()[6],std::uint64_t{1});
      }else{
        QCOMPARE(value.routerSnapshot->counts()[2],std::uint64_t{1});
        QCOMPARE(value.routerSnapshot->counts()[6],std::uint64_t{0});
      }
      if(!attempt){first=value.routerSnapshot;}
    }
    QCOMPARE(first->counts()[6],std::uint64_t{1});
    QCOMPARE(client->observation().snapshot->transport().counters().timeouts,std::uint64_t{3});
    QVERIFY(!client->observation().routerFailure);QVERIFY(client->observation().current);
  }

  void cancellationAndJoinedCloseDoNotDeclarePeerDeath(){
    auto client=make(),silent=make(false,"127.0.0.2");
    QVERIFY(identity(client->endpoint())!=identity(silent->endpoint()));
    const auto ping=client->ping(silent->endpoint(),2s);QVERIFY(ping);
    QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);
    const auto cancelled=client->cancel(*ping);QVERIFY(cancelled);
    QTRY_VERIFY_WITH_TIMEOUT(client->observation().outstandingCommands==0,3000);
    const auto afterCancel=client->observation();QVERIFY(afterCancel.routerSnapshot);
    QCOMPARE(afterCancel.snapshot->transport().counters().cancelled,std::uint64_t{1});
    QCOMPARE(afterCancel.routerSnapshot->counts()[5],std::uint64_t{1});
    QCOMPARE(afterCancel.routerSnapshot->counts()[6],std::uint64_t{0});
    const auto pending=client->ping(silent->endpoint(),2s);QVERIFY(pending);
    QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);
    const auto beforeClose=client->observation().routerSnapshot;QVERIFY(beforeClose);
    QVERIFY(client->close());const auto closed=client->observation();
    QVERIFY(closed.joined);QVERIFY(!closed.current);QVERIFY(closed.routerSnapshot);
    QCOMPARE(closed.routerSnapshot->counts()[5],std::uint64_t{1});
    QCOMPARE(closed.routerSnapshot->counts()[6],std::uint64_t{0});
    QVERIFY(closed.routerSnapshot==beforeClose);
    QVERIFY(!client->ping(silent->endpoint()));QVERIFY(client->close());
  }

  void actualFindRepliesMarkOnlyTheExplicitPeerAlive(){
    auto client=make();QUdpSocket indirect;
    QVERIFY(indirect.bind(QHostAddress(QStringLiteral("127.0.0.3")),0));
    const udp::Peer indirectPeer{indirect.localAddress(),indirect.localPort()};
    udp::Config config;config.instance=17;config.findNodeReply=udp::FindNodeReply{};
    config.findNodeReply->contacts.push_back({true,55,endpoint(indirectPeer).address});
    udp::FindValueSlice partial;partial.continuation=true;
    partial.values=std::vector<udp::lookup::values::Value>{};
    udp::FindValueSlice terminal;terminal.values=std::vector<udp::lookup::values::Value>{};
    config.findValueReply=std::vector<udp::FindValueSlice>{partial,terminal};
    udp::OwnedPingTransport server(std::move(config));
    QVERIFY(server.start(QHostAddress(QStringLiteral("127.0.0.2"))));
    QVERIFY(identity(client->endpoint())!=identity(server.endpoint()));
    QVERIFY(server.allowPeer(client->endpoint()));
    const auto find=client->findNode(server.endpoint(),{});QVERIFY(find);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*find),3000);
    const auto first=client->observation();QVERIFY(first.routerSnapshot);
    QCOMPARE(first.routerSnapshot->counts()[2],std::uint64_t{2});
    QCOMPARE(first.routerSnapshot->counts()[4],std::uint64_t{2});
    const auto values=client->findValue(server.endpoint(),{},0,255);QVERIFY(values);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*values),3000);
    const auto second=client->observation();QVERIFY(second.routerSnapshot);
    QCOMPARE(second.routerSnapshot->counts()[2],std::uint64_t{2});
    QCOMPARE(second.routerSnapshot->counts()[4],std::uint64_t{2});
    QCOMPARE(second.routerSnapshot->counts()[5],std::uint64_t{0});
    QCOMPARE(second.routerSnapshot->counts()[6],std::uint64_t{0});
    QCOMPARE(second.snapshot->transport().counters().replies,std::uint64_t{3});
    QCOMPARE(first.snapshot->transport().counters().replies,std::uint64_t{1});
  }

  void unownedDatagramsAndRejectedCommandsCannotInventContacts(){
    auto client=make();const auto initial=client->observation();
    QUdpSocket foreign;QVERIFY(foreign.bind(QHostAddress(QStringLiteral("127.0.0.3")),0));
    const auto target=client->endpoint();
    QCOMPARE(foreign.writeDatagram("not a reply",11,target.address,target.port),qint64{11});
    QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().counters().unowned==1,3000);
    const auto observed=client->observation();QVERIFY(observed.routerSnapshot);
    QVERIFY(observed.routerSnapshot==initial.routerSnapshot);
    QCOMPARE(observed.routerSnapshot->counts()[2],std::uint64_t{1});
    QCOMPARE(observed.snapshot->transport().allowedPeers(),std::size_t{0});
    QVERIFY(!client->allowPeer({QHostAddress(QStringLiteral("192.0.2.1")),1234}));
    QVERIFY(!client->ping({target.address,0}));
    const auto after=client->observation();QVERIFY(after.routerSnapshot==initial.routerSnapshot);
    QCOMPARE(after.snapshot->transport().counters().sent,std::uint64_t{0});
  }

  void sourceRestrictedIdCollisionIsNotCountedAsAnotherLiveContact(){
    auto client=make();const auto local=client->endpoint();QUdpSocket sameIdPeer;
    // Source IPv4 protocol>=50 identity restricts port to port%5. Bind a real
    // separate owned socket in that class; do not substitute a made-up node ID.
    const auto residue=static_cast<unsigned>(local.port%5);bool bound=false;
    for(unsigned i=0;i<32&&!bound;++i){
      const auto port=static_cast<quint16>(50000+residue+5*i);
      if(port!=local.port){bound=sameIdPeer.bind(local.address,port);}
    }
    QVERIFY(bound);const udp::Peer peer{sameIdPeer.localAddress(),sameIdPeer.localPort()};
    QVERIFY(peer!=local);QVERIFY(identity(peer)==identity(local));
    const auto known=client->allowPeer(peer);QVERIFY(known);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*known),3000);
    const auto observed=client->observation();QVERIFY(observed.routerSnapshot);
    QCOMPARE(observed.snapshot->transport().allowedPeers(),std::size_t{1});
    QCOMPARE(observed.routerSnapshot->counts()[2],std::uint64_t{1});
    QCOMPARE(observed.routerSnapshot->counts()[4],std::uint64_t{1});
    QCOMPARE(observed.routerSnapshot->counts()[5],std::uint64_t{0});
    QCOMPARE(observed.snapshot->transport().counters().sent,std::uint64_t{0});
  }
};
QTEST_GUILESS_MAIN(AzureusRouterBindingTests)
#include "azureus_router_binding_tests.moc"
