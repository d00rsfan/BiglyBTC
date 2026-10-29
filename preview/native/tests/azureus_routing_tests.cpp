// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_azureus_service.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QNetworkDatagram>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <future>
#include <memory>
#include <QScopeGuard>
#include <cerrno>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace udp=services::azureus_udp;
namespace control=services::azureus_control;
bool terminal(const app::OwnedAzureusService& owner,std::uint64_t id){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==id&&value.lastResult->terminal;}
bool success(const app::OwnedAzureusService& owner,std::uint64_t id){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==id&&value.lastResult->terminal&&!value.lastResult->error&&!value.lastResult->transportFailure;}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Cannot read owned routing source");}return file.readAll();}
// Missing is an actual source state: open() does not reserve a version.
// Capture BOTH canonical leaves; every other read/stat failure still refuses.
std::optional<QByteArray> optionalRead(const QString& path){
  struct stat info{};const auto name=QFile::encodeName(path);
  if(::lstat(name.constData(),&info)!=0){if(errno==ENOENT){return {};}throw std::runtime_error("Cannot inspect owned routing version source");}
  if(!S_ISREG(info.st_mode)){throw std::runtime_error("Unexpected owned routing version source type");}return read(path);
}
std::array<std::optional<QByteArray>,2> versionSource(const QString& primary){return {optionalRead(primary),optionalRead(primary.chopped(4)+".saving")};}
void write(const QString& path,const QByteArray& bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(bytes)!=bytes.size()||!file.flush()){throw std::runtime_error("Cannot write owned routing source");}}
services::azureus_nodes::Contact contact(const udp::Peer& peer,std::int8_t protocol=55){
  services::azureus_nodes::Contact value;value.address.port=peer.port;value.protocol=protocol;
  if(peer.address.protocol()==QAbstractSocket::IPv4Protocol){value.address.length=4;const auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){value.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}
  else{value.address.length=16;const auto ip=peer.address.toIPv6Address();std::copy_n(ip.c,16,value.address.bytes.begin());}
  services::azureus_nodes::NodeCache cache;auto id=cache.lookup({value.address,{}},protocol);if(!id){throw std::runtime_error("Cannot derive actual routing fixture ID");}value.id=id->id;return value;
}
struct Normal {
  QTemporaryDir root;std::optional<storage::ProfileLease> lease;std::shared_ptr<storage::AzureusValueVersions> versions;std::unique_ptr<app::OwnedAzureusService> owner;
  QString address;udp::RoutingScope scope;std::size_t maximumPeers;std::int8_t version;
  explicit Normal(const char* ip,udp::RoutingScope policy=udp::RoutingScope::OwnedLoopback,std::size_t peers=32,std::int8_t protocol=55):address(QString::fromLatin1(ip)),scope(policy),maximumPeers(peers),version(protocol){
    if(!root.isValid()){throw std::runtime_error("Owned routing profile");}auto acquired=storage::ProfileLease::acquire(root.path().toStdString());if(!acquired){throw std::runtime_error(acquired.error().message);}lease.emplace(std::move(*acquired));
    auto config=storage::ConfigFile::createMissing(*lease,"biglybt.config","d22:Plugin.DHT.dht.enabledi1ee");if(!config){throw std::runtime_error(config.error().message);}start();
  }
  void start(){auto opened=storage::AzureusValueVersions::open(*lease,0);if(!opened){throw std::runtime_error(opened.error().message);}versions=*opened;app::AzureusServiceOptions options;options.enabled=true;options.servePings=true;options.serveValues=true;options.network=0;options.bindAddress=QHostAddress(address);options.routingScope=scope;options.maximumPeers=maximumPeers;options.version=version;options.valueVersions=versions;auto made=app::OwnedAzureusService::create(options);if(!made){throw std::runtime_error("Cannot start normal routing owner");}owner=std::move(*made);}
  void stop(){if(owner&&!owner->close()){throw std::runtime_error("Cannot close normal routing owner");}owner.reset();versions.reset();}
  QString versionPath()const{return root.path()+"/dht/version.dat";}
};
QByteArray nodeReply(const QNetworkDatagram& datagram,std::span<const services::azureus_nodes::Snapshot> contacts={},bool wrongTransaction=false){
  const auto bytes=datagram.data();udp::find::Context context;context.header.network=0;context.header.now=QDateTime::currentMSecsSinceEpoch();services::azureus_nodes::NodeCache cache;
  auto request=udp::find::decode(udp::packet::udp::Kind::Request,std::string(bytes.constData(),static_cast<std::size_t>(bytes.size())),context,cache);if(!request){throw std::runtime_error("Cannot decode actual NODE fixture request");}
  const auto& header=request->header().fields();udp::find::ReplyFields reply;reply.header.network=0;reply.header.localVersion=55;reply.header.remoteVersion=header.originatorVersion;reply.header.connection=header.connection;reply.header.transaction=wrongTransaction?header.transaction^1:header.transaction;reply.header.now=context.header.now;reply.header.receivedAt=context.header.now;reply.contacts=contacts;
  // Protocol>=10 requires an engaged positions collection, including empty.
  // This is the same zero-provider payload used by the normal owner.
  reply.positions=std::span<const std::optional<udp::find::positions::WirePosition>>{};
  auto encoded=udp::find::encodeReply(reply);if(!encoded){throw std::runtime_error("Cannot encode owned NODE reply: codec="+std::to_string(static_cast<unsigned>(encoded.error().code))+", partial="+std::to_string(encoded.error().partial.size()));}
  auto decoded=udp::find::decode(udp::packet::udp::Kind::Reply,*encoded,context,cache);
  const auto* body=decoded?std::get_if<udp::find::ReplyBody>(&decoded->body()):nullptr;
  if(!decoded||decoded->consumed()!=encoded->size()||!decoded->trailing().empty()||!body||body->providerFailures||!body->positions.empty()||body->contacts.size()!=contacts.size()||decoded->header().fields().connection!=reply.header.connection||decoded->header().fields().transaction!=reply.header.transaction){throw std::runtime_error("Owned NODE fixture reply failed exact framing check");}
  return QByteArray(encoded->data(),static_cast<qsizetype>(encoded->size()));
}
void send(QUdpSocket& server,const QNetworkDatagram& datagram,const QByteArray& bytes){if(datagram.senderPort()<=0||datagram.senderPort()>65535||server.writeDatagram(bytes,datagram.senderAddress(),static_cast<quint16>(datagram.senderPort()))!=bytes.size()){throw std::runtime_error("Cannot send owned NODE fixture reply");}}
}
class AzureusRoutingTests final:public QObject {
  Q_OBJECT
private slots:
  void explicitKnownAttachmentUsesActualPingAndReplacesOnlyItsProtocolHint_data(){
    QTest::addColumn<int>("hint");QTest::addColumn<int>("actual");QTest::newRow("hint51-actual55")<<51<<55;QTest::newRow("hint55-actual51")<<55<<51;
  }
  void explicitKnownAttachmentUsesActualPingAndReplacesOnlyItsProtocolHint(){
    QFETCH(int,hint);QFETCH(int,actual);Normal a("127.71.0.1"),b("127.72.0.1",udp::RoutingScope::OwnedLoopback,32,static_cast<std::int8_t>(actual));
    const auto beforeA=versionSource(a.versionPath()),beforeB=versionSource(b.versionPath());auto access=b.lease->sourceAccess();QVERIFY(access);auto blocked=std::make_unique<storage::ProfileSourceAccess>(std::move(*access));const auto release=qScopeGuard([&]{blocked.reset();});
    const auto request=a.owner->allowPeer(b.owner->endpoint(),static_cast<std::int8_t>(hint));QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*request),3000);
    QTRY_VERIFY_WITH_TIMEOUT(b.owner->observation().snapshot->transport().counters().requests>0&&b.owner->observation().profileIoRunning==1,3000);
    const auto held=a.owner->observation();QVERIFY(held.routingSnapshot&&held.routerSnapshot);QCOMPARE(held.routingSnapshot->contactCount,std::size_t{1});QCOMPARE(held.routingSnapshot->contacts[0].protocolSource,udp::RoutingProtocolSource::ProbeHint);QCOMPARE(held.routingSnapshot->contacts[0].contact.protocol,static_cast<std::int8_t>(hint));QCOMPARE(held.routerSnapshot->counts()[4],std::uint64_t{1});QCOMPARE(held.routerSnapshot->counts()[5],std::uint64_t{1});QVERIFY(held.routingSnapshot->pings>0&&!held.routingSnapshot->seeded);QCOMPARE(held.snapshot->transport().allowedPeers(),std::size_t{1});
    blocked.reset();QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().routingSnapshot&&a.owner->observation().routingSnapshot->contacts[0].protocolSource==udp::RoutingProtocolSource::Packet,3000);const auto answered=a.owner->observation();QCOMPARE(answered.routingSnapshot->contacts[0].contact.protocol,static_cast<std::int8_t>(actual));QCOMPARE(answered.routerSnapshot->counts()[4],std::uint64_t{2});QVERIFY(!answered.routingSnapshot->seeded);
    const auto repeated=a.owner->allowPeer(b.owner->endpoint(),static_cast<std::int8_t>(hint));QVERIFY(repeated);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*repeated),3000);const auto duplicate=a.owner->observation();QCOMPARE(duplicate.routingSnapshot->contactCount,std::size_t{1});QCOMPARE(duplicate.routingSnapshot->contacts[0].protocolSource,udp::RoutingProtocolSource::Packet);QCOMPARE(duplicate.routingSnapshot->contacts[0].contact.protocol,static_cast<std::int8_t>(actual));QCOMPARE(duplicate.routingSnapshot->pings,answered.routingSnapshot->pings);
    const auto integration=a.owner->integrateRouting(b.owner->endpoint());QVERIFY(integration);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*integration),10000);const auto integrated=a.owner->observation().routingSnapshot;QVERIFY(integrated&&integrated->seeded&&!integrated->failure&&integrated->lookup);QVERIFY(std::ranges::any_of(integrated->lookup->nodes,[](const auto& node){return node.state==control::LookupNodeState::Replied;}));QCOMPARE(versionSource(a.versionPath()),beforeA);QCOMPARE(versionSource(b.versionPath()),beforeB);QVERIFY(a.owner->close());QVERIFY(b.owner->close());
  }
  void knownPreparationRefusesAliasInvalidHintAndFullCombinedPeerEnvelope(){
    Normal a("127.73.0.1");QUdpSocket first,alias;const QHostAddress address(QStringLiteral("127.74.0.1"));QVERIFY(first.bind(address,0,QUdpSocket::DontShareAddress));const udp::Peer peer{first.localAddress(),first.localPort()};const auto otherPort=static_cast<quint16>(peer.port<=65530?peer.port+5:peer.port-5);QVERIFY(alias.bind(address,otherPort,QUdpSocket::DontShareAddress));QVERIFY(contact(peer).id==contact({address,otherPort}).id);
    const auto accepted=a.owner->allowPeer(peer,51);QVERIFY(accepted);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*accepted),3000);QTRY_VERIFY_WITH_TIMEOUT(first.hasPendingDatagrams(),3000);const auto before=a.owner->observation();QVERIFY(before.routingSnapshot&&before.routerSnapshot);const auto refused=a.owner->allowPeer({address,otherPort},55);QVERIFY(refused);QTRY_VERIFY_WITH_TIMEOUT(terminal(*a.owner,*refused),3000);QCOMPARE(a.owner->observation().lastResult->transportFailure,std::optional{udp::Failure::Authority});const auto after=a.owner->observation();QCOMPARE(after.routingSnapshot->contactCount,before.routingSnapshot->contactCount);QCOMPARE(after.routerSnapshot->counts(),before.routerSnapshot->counts());QCOMPARE(after.snapshot->transport().allowedPeers(),before.snapshot->transport().allowedPeers());QVERIFY(!alias.hasPendingDatagrams());
    QVERIFY(!a.owner->allowPeer(peer,50));QVERIFY(!a.owner->allowPeer(peer,56));QVERIFY(!a.owner->allowPeer({QHostAddress::LocalHostIPv6,32001},55));QVERIFY(!a.owner->allowPeer({QHostAddress(QStringLiteral("203.0.113.1")),32001},55));QVERIFY(a.owner->close());
    Normal full("127.75.0.1");std::vector<std::unique_ptr<QUdpSocket>> sockets;
    // Select16 genuinely bound IDs on each root half before admission: the
    // default k20 router then retains all32, independently of random UDP ports.
    std::array<unsigned,2> halves{};std::vector<services::azureus_nodes::NodeId> ids;
    for(int n=1;n<=128&&sockets.size()<32;++n){auto socket=std::make_unique<QUdpSocket>();QVERIFY(socket->bind(QHostAddress(QString("127.150.%1.1").arg(n)),0,QUdpSocket::DontShareAddress));const udp::Peer selected{socket->localAddress(),socket->localPort()};const auto selectedId=contact(selected).id;const auto half=static_cast<unsigned>(selectedId[0]>>7);if(halves[half]==16||std::ranges::find(ids,selectedId)!=ids.end()){continue;}const auto command=full.owner->allowPeer(selected,55);QVERIFY(command);QTRY_VERIFY_WITH_TIMEOUT(success(*full.owner,*command),3000);++halves[half];ids.push_back(selectedId);sockets.push_back(std::move(socket));}QCOMPARE(sockets.size(),std::size_t{32});
    const auto saturated=full.owner->observation();QVERIFY(saturated.routingSnapshot&&saturated.routerSnapshot);QCOMPARE(saturated.routingSnapshot->contactCount,std::size_t{32});QCOMPARE(saturated.snapshot->transport().allowedPeers(),std::size_t{32});QUdpSocket overflow;QVERIFY(overflow.bind(QHostAddress(QStringLiteral("127.182.0.1")),0,QUdpSocket::DontShareAddress));const auto over=full.owner->allowPeer({overflow.localAddress(),overflow.localPort()},55);QVERIFY(over);QTRY_VERIFY_WITH_TIMEOUT(terminal(*full.owner,*over),3000);QCOMPARE(full.owner->observation().lastResult->transportFailure,std::optional{udp::Failure::Capacity});const auto unchanged=full.owner->observation();QCOMPARE(unchanged.routingSnapshot->contactCount,saturated.routingSnapshot->contactCount);QCOMPARE(unchanged.routerSnapshot->counts()[2]+unchanged.routerSnapshot->counts()[3],saturated.routerSnapshot->counts()[2]+saturated.routerSnapshot->counts()[3]);QCOMPARE(unchanged.snapshot->transport().allowedPeers(),std::size_t{32});QVERIFY(!overflow.hasPendingDatagrams());QVERIFY(!unchanged.routingSnapshot->seeded);QVERIFY(full.owner->close());
  }
  void knownAttachmentCannotCommitAfterSourceDisableCancelOrClose_data(){QTest::addColumn<int>("change");QTest::newRow("disable")<<0;QTest::newRow("cancel")<<1;QTest::newRow("close")<<2;}
  void knownAttachmentCannotCommitAfterSourceDisableCancelOrClose(){
    QFETCH(int,change);Normal a("127.76.0.1");QUdpSocket peer;QVERIFY(peer.bind(QHostAddress(QStringLiteral("127.77.0.1")),0,QUdpSocket::DontShareAddress));const auto before=a.owner->observation();auto access=a.lease->sourceAccess();QVERIFY(access);auto blocked=std::make_unique<storage::ProfileSourceAccess>(std::move(*access));const auto release=qScopeGuard([&]{blocked.reset();});const auto command=a.owner->allowPeer({peer.localAddress(),peer.localPort()},51);QVERIFY(command);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().profileIoRunning==1,3000);
    std::future<std::expected<void,app::AzureusServiceError>> closed;
    const auto join=qScopeGuard([&]{blocked.reset();if(closed.valid()){closed.wait();}});
    if(change==0){auto config=storage::ConfigFile::load(*a.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().setInteger("Plugin.DHT.dht.enabled",0));QVERIFY(config->save(*a.lease));}
    else if(change==1){const auto cancelled=a.owner->cancel(*command);QVERIFY(cancelled);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().outstandingCommands==0,3000);}
    else{closed=std::async(std::launch::async,[&]{return a.owner->close();});QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().state==app::AzureusServiceState::Closed,3000);QVERIFY(closed.wait_for(0ms)!=std::future_status::ready);}
    blocked.reset();if(change==2){QVERIFY(closed.get());}else{QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().outstandingCommands==0,3000);const auto result=a.owner->observation().lastResult;QVERIFY(result);if(change==0){QVERIFY(result->error);}else{QCOMPARE(result->command,app::AzureusServiceCommand::Cancel);QVERIFY(!result->error);}}
    const auto after=a.owner->observation();QCOMPARE(after.snapshot->transport().counters().sent,before.snapshot->transport().counters().sent);QCOMPARE(after.snapshot->transport().allowedPeers(),std::size_t{0});QVERIFY(!after.routingSnapshot||after.routingSnapshot->contactCount==0);QVERIFY(!after.routingSnapshot||!after.routingSnapshot->seeded);QVERIFY(!peer.hasPendingDatagrams());QCOMPARE(after.routerSnapshot->counts()[2],std::uint64_t{1});QVERIFY(a.owner->close());
  }
  void realSelfLookupDiscoversThirdNormalOwnerThenAuthorisesActualCachedForward(){
    Normal origin("127.51.0.1"),cache("127.52.0.1"),receiver("127.53.0.1");
    auto pair=[&](Normal& a,Normal& b){const auto first=a.owner->allowPeer(b.owner->endpoint()),second=b.owner->allowPeer(a.owner->endpoint());if(!first||!second){return false;}QElapsedTimer wait;wait.start();while(wait.elapsed()<3000&&(!success(*a.owner,*first)||!success(*b.owner,*second))){QCoreApplication::processEvents(QEventLoop::AllEvents,10);}return success(*a.owner,*first)&&success(*b.owner,*second);};
    QVERIFY(pair(origin,cache));QVERIFY(pair(cache,receiver));
    const auto published=origin.owner->publishValue(cache.owner->endpoint(),"normal-routing-key","original-owned-value",5s,app::AzureusPublicationFlags::Anonymous);QVERIFY(published);QTRY_VERIFY_WITH_TIMEOUT(terminal(*origin.owner,*published),5000);QCOMPARE(origin.owner->observation().publishSnapshot->state,app::AzureusPublishState::Complete);const auto actualOrigin=origin.owner->observation().publishSnapshot->localValue;QVERIFY(actualOrigin&&actualOrigin->isLocal());QCOMPARE(cache.owner->observation().databaseSnapshot->stores,std::uint64_t{1});
    const auto cacheVersion=versionSource(cache.versionPath()),receiverVersion=versionSource(receiver.versionPath());QVERIFY(!cacheVersion[0]&&!cacheVersion[1]&&!receiverVersion[0]&&!receiverVersion[1]);const auto rejected=cache.owner->forwardCachedValue(receiver.owner->endpoint(),"normal-routing-key");QVERIFY(rejected);QTRY_VERIFY_WITH_TIMEOUT(terminal(*cache.owner,*rejected),5000);QCOMPARE(cache.owner->observation().publishSnapshot->state,app::AzureusPublishState::Failed);QCOMPARE(cache.owner->observation().publishSnapshot->error,std::optional{app::AzureusServiceError::Readback});QCOMPARE(receiver.owner->observation().databaseSnapshot->stores,std::uint64_t{0});QCOMPARE(versionSource(cache.versionPath()),cacheVersion);
    const auto request=receiver.owner->integrateRouting(cache.owner->endpoint());QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(terminal(*receiver.owner,*request),10000);QVERIFY(success(*receiver.owner,*request));const auto routing=receiver.owner->observation().routingSnapshot;QVERIFY(routing&&routing->finished&&routing->seeded&&routing->lookup);QCOMPARE(routing->lookup->options.kind,control::LookupKind::Node);QVERIFY(routing->lookup->results.empty());
    const auto original=contact(origin.owner->endpoint());const auto discovered=std::ranges::find(routing->lookup->nodes,original.id,[](const auto& node){return node.contact.id;});QVERIFY(discovered!=routing->lookup->nodes.end());QVERIFY(discovered->parent!=0);QCOMPARE(discovered->state,control::LookupNodeState::Replied);QTRY_VERIFY_WITH_TIMEOUT(receiver.owner->observation().routingSnapshot->pings>0,3000);
    // No receiver<->origin allowPeer call occurred. The response path and its
    // actual PING/NODE completions, not a fixture contact grant, prove discovery.
    const auto forwarded=cache.owner->forwardCachedValue(receiver.owner->endpoint(),"normal-routing-key");QVERIFY(forwarded);QTRY_VERIFY_WITH_TIMEOUT(terminal(*cache.owner,*forwarded),5000);const auto proof=cache.owner->observation().publishSnapshot;QCOMPARE(proof->state,app::AzureusPublishState::Complete);QVERIFY(proof->localValue&&!proof->localValue->isLocal());QCOMPARE(proof->localValue->version(),actualOrigin->version());QCOMPARE(proof->localValue->originator().get()->id,actualOrigin->originator().get()->id);QCOMPARE(proof->readback.size(),std::size_t{1});QCOMPARE(proof->readback[0].bytes,std::string("original-owned-value"));QCOMPARE(proof->readback[0].flags,std::uint8_t{0x10});QCOMPARE(receiver.owner->observation().databaseSnapshot->stores,std::uint64_t{1});QCOMPARE(receiver.owner->observation().databaseSnapshot->localPuts,std::uint64_t{0});QCOMPARE(versionSource(cache.versionPath()),cacheVersion);QCOMPARE(versionSource(receiver.versionPath()),receiverVersion);
    const auto generation=routing->generation;origin.stop();cache.stop();receiver.stop();origin.start();cache.start();receiver.start();QVERIFY(pair(origin,cache));QVERIFY(pair(cache,receiver));
    const auto newValue=origin.owner->publishValue(cache.owner->endpoint(),"restart-routing-key","after-normal-restart");QVERIFY(newValue);QTRY_VERIFY_WITH_TIMEOUT(terminal(*origin.owner,*newValue),5000);QCOMPARE(origin.owner->observation().publishSnapshot->state,app::AzureusPublishState::Complete);
    const auto cold=cache.owner->forwardCachedValue(receiver.owner->endpoint(),"restart-routing-key");QVERIFY(cold);QTRY_VERIFY_WITH_TIMEOUT(terminal(*cache.owner,*cold),5000);QCOMPARE(cache.owner->observation().publishSnapshot->state,app::AzureusPublishState::Failed);QCOMPARE(receiver.owner->observation().databaseSnapshot->stores,std::uint64_t{0});
    const auto reseed=receiver.owner->integrateRouting(cache.owner->endpoint());QVERIFY(reseed);QTRY_VERIFY_WITH_TIMEOUT(success(*receiver.owner,*reseed),10000);QVERIFY(receiver.owner->observation().routingSnapshot->generation!=generation);QVERIFY(receiver.owner->observation().routingSnapshot->seeded);
  }
  void exactPeerDefaultStillRejectsUnlistedRequestsAndRoutingActivation(){
    Normal exact("127.54.0.1",udp::RoutingScope::ExactPeers);udp::Config options;options.network=0;udp::OwnedPingTransport client(std::move(options));QVERIFY(client.start(QHostAddress(QStringLiteral("127.55.0.1"))));QVERIFY(client.allowPeer(exact.owner->endpoint()));QObject receiver;std::optional<udp::FindNodeOutcome> result;
    QVERIFY(client.findNode(exact.owner->endpoint(),55,contact(exact.owner->endpoint()).id,&receiver,[&](auto outcome){result.emplace(std::move(outcome));},100ms));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),3000);QVERIFY(!*result);QCOMPARE(result->error(),udp::Failure::Timeout);QTRY_VERIFY_WITH_TIMEOUT(exact.owner->observation().snapshot->transport().counters().unowned>0,3000);
    const auto integration=exact.owner->integrateRouting(client.endpoint());QVERIFY(!integration);QCOMPARE(integration.error(),app::AzureusServiceError::Invalid);QVERIFY(!exact.owner->observation().routingSnapshot);QVERIFY(exact.owner->close());client.close();
  }
  void heldNodeReplyCannotSeedAfterCancelClosePermissionOrVersionWithdrawal_data(){
    QTest::addColumn<int>("change");QTest::newRow("cancel")<<0;QTest::newRow("close")<<1;QTest::newRow("DHT-permission")<<2;QTest::newRow("version-source")<<3;
  }
  void heldNodeReplyCannotSeedAfterCancelClosePermissionOrVersionWithdrawal(){
    QFETCH(int,change);Normal owner("127.56.0.1");QUdpSocket server;QVERIFY(server.bind(QHostAddress(QStringLiteral("127.57.0.1")),0,QUdpSocket::DontShareAddress));const udp::Peer peer{server.localAddress(),server.localPort()};const auto request=owner.owner->integrateRouting(peer,2s);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(),3000);const auto held=server.receiveDatagram();const auto reply=nodeReply(held);
    if(change==0){const auto cancel=owner.owner->cancel(*request);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(owner.owner->observation().outstandingCommands==0,3000);}
    else if(change==1){QVERIFY(owner.owner->close());QVERIFY(owner.owner->observation().joined);}
    else if(change==2){auto config=storage::ConfigFile::load(*owner.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().setInteger("Plugin.DHT.dht.enabled",0));QVERIFY(config->save(*owner.lease));}
    else{write(owner.versionPath(),"d4:nexti1ee");}
    send(server,held,reply);
    if(change<2){QVERIFY(!owner.owner->observation().routingSnapshot||!owner.owner->observation().routingSnapshot->seeded);}
    else{QTRY_VERIFY_WITH_TIMEOUT(terminal(*owner.owner,*request),3000);QVERIFY(owner.owner->observation().lastResult->error);QVERIFY(owner.owner->observation().routingSnapshot&&!owner.owner->observation().routingSnapshot->seeded);}
    if(change==0){QTRY_VERIFY_WITH_TIMEOUT(owner.owner->observation().snapshot->transport().counters().foreign>0,3000);const auto fresh=owner.owner->integrateRouting(peer,2s);QVERIFY(fresh);QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(),3000);const auto next=server.receiveDatagram();send(server,held,reply);QTRY_VERIFY_WITH_TIMEOUT(owner.owner->observation().snapshot->transport().counters().foreign>1,3000);QVERIFY(!owner.owner->observation().routingSnapshot->seeded);const auto cancel=owner.owner->cancel(*fresh);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(owner.owner->observation().outstandingCommands==0,3000);QVERIFY(next.isValid());}
    QVERIFY(owner.owner->close());if(change==3){QCOMPARE(read(owner.versionPath()),QByteArray("d4:nexti1ee"));}
  }
  void foreignAndMalformedRepliesDoNotGrantAndOutOfScopeContactsNeverDispatch(){
    Normal owner("127.58.0.1");QUdpSocket server;QVERIFY(server.bind(QHostAddress(QStringLiteral("127.59.0.1")),0,QUdpSocket::DontShareAddress));const auto request=owner.owner->integrateRouting({server.localAddress(),server.localPort()},3s);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(),3000);const auto held=server.receiveDatagram();
    send(server,held,nodeReply(held,{},true));QTRY_VERIFY_WITH_TIMEOUT(owner.owner->observation().snapshot->transport().counters().foreign>0,3000);QVERIFY(!owner.owner->observation().routingSnapshot->seeded);
    send(server,held,QByteArray(16,'\0'));QTRY_VERIFY_WITH_TIMEOUT(owner.owner->observation().snapshot->transport().counters().malformed>0,3000);
    const auto self=contact(owner.owner->endpoint());const auto publicPeer=contact({QHostAddress(QStringLiteral("1.2.3.4")),32111});const auto badVersion=contact({QHostAddress(QStringLiteral("127.60.0.1")),32112},0);std::array<services::azureus_nodes::Snapshot,3> contacts{{{true,55,self.address},{true,55,publicPeer.address},{true,0,badVersion.address}}};send(server,held,nodeReply(held,contacts));
    QTRY_VERIFY_WITH_TIMEOUT(terminal(*owner.owner,*request),3000);QVERIFY(success(*owner.owner,*request));const auto observed=owner.owner->observation().routingSnapshot;QVERIFY(observed&&observed->seeded);QCOMPARE(observed->lookup->nodes.size(),std::size_t{1});QCOMPARE(observed->rejected,std::uint64_t{2});QCOMPARE(owner.owner->observation().snapshot->transport().allowedPeers(),std::size_t{1});QVERIFY(owner.owner->close());
  }
  void actualContactCapacityWithdrawalCannotSetSeededOrDispatchOverflowCandidates(){
    Normal owner("127.63.0.1",udp::RoutingScope::OwnedLoopback,2);QUdpSocket server;QVERIFY(server.bind(QHostAddress(QStringLiteral("127.64.0.1")),0,QUdpSocket::DontShareAddress));const auto request=owner.owner->integrateRouting({server.localAddress(),server.localPort()},3s);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(),3000);const auto held=server.receiveDatagram();
    const auto one=contact({QHostAddress(QStringLiteral("127.65.0.1")),32001}),two=contact({QHostAddress(QStringLiteral("127.66.0.1")),32002});const std::array<services::azureus_nodes::Snapshot,2> contacts{{{true,55,one.address},{true,55,two.address}}};send(server,held,nodeReply(held,contacts));
    QTRY_VERIFY_WITH_TIMEOUT(terminal(*owner.owner,*request),3000);QVERIFY(owner.owner->observation().lastResult->error);const auto result=owner.owner->observation().routingSnapshot;QVERIFY(result&&result->finished&&!result->seeded);QCOMPARE(result->failure,std::optional{udp::Failure::Capacity});QCOMPARE(result->pings,std::uint64_t{0});QCOMPARE(owner.owner->observation().databaseSnapshot->stores,std::uint64_t{0});QVERIFY(owner.owner->close());
  }
  void contactOnlyControllerCannotCompleteThroughValueResults(){
    control::LookupController controller;control::LookupOptions options;options.kind=control::LookupKind::Node;options.localId=contact({QHostAddress(QStringLiteral("127.61.0.1")),31000}).id;options.target=options.localId;const std::array seed{control::AdmittedContact{contact({QHostAddress(QStringLiteral("127.62.0.1")),31001}),true}};QVERIFY(controller.start(1,1,options,seed,0));auto intents=controller.takeIntents(1);QVERIFY(intents&&intents->size()==1);QVERIFY(controller.sent(intents->front().node,1,2));const auto wrong=controller.values(intents->front().node,{},false,1,3);QVERIFY(!wrong);QCOMPARE(wrong.error(),control::LookupError::Unsupported);QCOMPARE(controller.snapshot()->active,std::size_t{1});QVERIFY(controller.contacts(intents->front().node,{},false,4));auto result=controller.snapshot();QVERIFY(result&&control::validLookupSnapshot(*result));QCOMPARE(result->state,control::LookupState::Complete);QVERIFY(result->results.empty());
  }
};
QTEST_GUILESS_MAIN(AzureusRoutingTests)
#include "azureus_routing_tests.moc"
