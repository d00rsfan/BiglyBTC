#include "mainline_dht.hpp"
#include "persistent_session.hpp"
#include "magnet_intake.hpp"
#include "managed_storage.hpp"
#include "owned_engine_fixture.hpp"

#include <QNetworkDatagram>
#include <QTcpServer>
#include <QUdpSocket>
#include <QtTest>

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt = libtorrent;

QByteArray bytes(std::string_view value) { return QByteArray(value.data(), static_cast<qsizetype>(value.size())); }
QByteArray string(const QByteArray& value) { return QByteArray::number(value.size()) + ':' + value; }
QByteArray dictionary(const std::map<QByteArray,QByteArray>& values) {
  QByteArray result("d");
  for (const auto& [key,value] : values) { result += string(key); result += value; }
  return result + 'e';
}
QByteArray list(const std::vector<QByteArray>& values) {
  QByteArray result("l"); for (const auto& value : values) { result += value; } return result + 'e';
}
QByteArray hashBytes(const lt::sha1_hash& hash) { return QByteArray(hash.data(), 20); }
lt::sha1_hash hash(char value) { const QByteArray raw(20,value); return lt::sha1_hash(raw.constData()); }
const core::Node* field(const core::Node& value, std::string_view key, core::Node::Type type) {
  const auto* found=value.find(key); return found && found->type==type ? found : nullptr;
}
QByteArray compact(unsigned short port) {
  QByteArray result; result.append(char(127)); result.append(char(0)); result.append(char(0)); result.append(char(1));
  result.append(static_cast<char>(port >> 8)); result.append(static_cast<char>(port & 255)); return result;
}
engine::NetworkPolicy localPolicy() {
  engine::NetworkPolicy result;
  result.mode=engine::NetworkMode::Normal; result.dht=true; result.ipv6=false;
  result.bindAddresses={"127.0.0.1"}; result.mainlineIpv4=true; result.mainlineIpv6=false;
  return result;
}
// Reserve a concrete owned port rather than depending on a production default.
// If the peer already owns its UDP port, that existing bind excludes it too.
unsigned short dhtPort(unsigned short peerPort=0) {
  QUdpSocket peerReservation, reservation;
  if (peerPort) { static_cast<void>(peerReservation.bind(QHostAddress::LocalHost,peerPort,QUdpSocket::DontShareAddress)); }
  test::require(reservation.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Cannot reserve owned DHT port");
  const auto result=reservation.localPort();
  test::require(result!=0 && result!=peerPort,"Owned DHT and advertised peer ports overlap");
  return result;
}

// Independent KRPC wire fixture. The production service supplies neither its
// bencode nor its expected hash/token/port. Local IDs use LT's unchanged BEP42
// local-address exemption (kademlia/node_id.cpp), not a relaxed DHT setting.
class OwnedKrpc final {
  struct Request { QByteArray transaction,hash; QHostAddress address; unsigned short port=0; };
  QUdpSocket socket_;
  QTcpServer decoy_;
  const QByteArray nodeId_=QByteArray(20,'K');
  std::set<QByteArray> allowed_;
  std::map<QByteArray,int> gets_,announces_;
  std::map<QByteArray,QByteArray> tokens_;
  std::vector<Request> held_;
  QByteArray heldHash_;
  unsigned short peerPort_;
  bool noise_=false;

  void fail(const char* text) { if (failure.isEmpty()) { failure=QString::fromLatin1(text); } }
  QByteArray tokenKey(const Request& request) const {
    return request.hash + request.address.toString().toUtf8() + ':' + QByteArray::number(request.port);
  }
  void send(const Request& request,const QByteArray& packet) {
    if (socket_.writeDatagram(packet,request.address,request.port)!=packet.size()) { fail("Owned KRPC reply write failed"); }
  }
  QByteArray response(const Request& request,const std::map<QByteArray,QByteArray>& result) const {
    return dictionary({{"r",dictionary(result)},{"t",string(request.transaction)},{"y",string("r")}});
  }
  void answerPeers(const Request& request) {
    const auto token=QCryptographicHash::hash(tokenKey(request),QCryptographicHash::Sha1).first(8);
    tokens_[tokenKey(request)]=token;
    std::vector<QByteArray> peers{string(compact(peerPort_))};
    if (noise_) {
      // Unsolicited and malformed packets precede a valid owned progress
      // barrier. An unrequested hash in a reply is never lookup authority.
      auto unrelated=request; unrelated.transaction += "unrelated";
      send(unrelated,response(unrelated,{{"id",string(nodeId_)},{"info_hash",string(QByteArray(20,'Z'))},
          {"values",list({string(compact(decoy_.serverPort()))})}}));
      send(request,QByteArray("d1:ri-no-integer-ee"));
      peers.push_back(string({})); peers.push_back(string(QByteArray(5,'x')));
      peers.push_back(string(QByteArray(6,'\0'))); peers.push_back(string(compact(0)));
      ++noiseReplies;
    }
    send(request,response(request,{{"id",string(nodeId_)},{"token",string(token)},{"values",list(peers)}}));
  }
  void receive() {
    while (socket_.hasPendingDatagrams()) {
      const auto packet=socket_.receiveDatagram(4096);
      if (!packet.senderAddress().isLoopback() || packet.senderPort()==0 || packet.data().size()>2048) {
        fail("KRPC query has invalid owned source or size"); continue;
      }
      auto parsed=core::Document::parse(packet.data().toStdString(),{4096,2048,128,8});
      if (!parsed || parsed->root().type!=core::Node::Type::Dictionary) { fail("Invalid KRPC query bencode"); continue; }
      const auto& root=parsed->root();
      const auto* transaction=field(root,"t",core::Node::Type::Bytes);
      const auto* kind=field(root,"y",core::Node::Type::Bytes);
      const auto* query=field(root,"q",core::Node::Type::Bytes);
      const auto* args=field(root,"a",core::Node::Type::Dictionary);
      const auto* id=args?field(*args,"id",core::Node::Type::Bytes):nullptr;
      if (!transaction || transaction->bytes.empty() || transaction->bytes.size()>16 || !kind || kind->bytes!="q"
          || !query || !args || !id || id->bytes.size()!=20 || bytes(id->bytes)==QByteArray(20,'\0')) {
        fail("KRPC query lacks an actual transaction/node identity"); continue;
      }
      const auto sourcePort=packet.senderPort();
      if (sourcePort<1 || sourcePort>65535) { fail("Invalid owned datagram source port"); continue; }
      Request request{bytes(transaction->bytes),{},packet.senderAddress(),static_cast<unsigned short>(sourcePort)};
      if (query->bytes=="ping") {
        ++bootstrapQueries; send(request,response(request,{{"id",string(nodeId_)}}));
      } else if (query->bytes=="find_node") {
        const auto* target=field(*args,"target",core::Node::Type::Bytes);
        if (!target || target->bytes.size()!=20) { fail("Invalid find_node target"); continue; }
        ++bootstrapQueries; send(request,response(request,{{"id",string(nodeId_)},{"nodes",string({})}}));
      } else if (query->bytes=="get_peers" || query->bytes=="announce_peer") {
        const auto* info=field(*args,"info_hash",core::Node::Type::Bytes);
        if (!info || info->bytes.size()!=20 || bytes(info->bytes)==QByteArray(20,'\0')) { fail("Invalid KRPC info_hash"); continue; }
        request.hash=bytes(info->bytes);
        if (query->bytes=="get_peers") {
          // LT can probe a random routing bucket with get_peers. Such a query
          // receives no values; only explicitly registered subjects get peers.
          if (!allowed_.contains(request.hash)) {
            ++bootstrapQueries; send(request,response(request,{{"id",string(nodeId_)},{"nodes",string({})}})); continue;
          }
          ++gets_[request.hash];
          if (request.hash==heldHash_) { held_.push_back(request); } else { answerPeers(request); }
        } else {
          const auto* token=field(*args,"token",core::Node::Type::Bytes);
          const auto* port=field(*args,"port",core::Node::Type::Integer);
          const auto* implied=args->find("implied_port");
          const auto issued=tokens_.find(tokenKey(request));
          if (!allowed_.contains(request.hash) || !token || issued==tokens_.end() || bytes(token->bytes)!=issued->second
              || !port || port->integer!=peerPort_ || peerPort_==request.port
              || (implied && (implied->type!=core::Node::Type::Integer || implied->integer!=0))) {
            fail("announce_peer did not use the issued hash/source token and explicit TCP peer port"); continue;
          }
          ++announces_[request.hash]; send(request,response(request,{{"id",string(nodeId_)}}));
        }
      } else { fail("Unexpected KRPC query outside owned fixture protocol"); }
    }
  }
public:
  QString failure;
  int bootstrapQueries=0,noiseReplies=0;
  explicit OwnedKrpc(unsigned short peerPort) : peerPort_(peerPort) {
    test::require(peerPort_!=0,"Owned KRPC fixture needs a real TCP peer");
    test::require(socket_.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Cannot bind owned KRPC fixture");
    test::require(decoy_.listen(QHostAddress::LocalHost,0),"Cannot bind owned decoy endpoint");
    QObject::connect(&socket_,&QUdpSocket::readyRead,&socket_,[this]{receive();});
  }
  unsigned short port() const { return socket_.localPort(); }
  void allow(const lt::sha1_hash& value) { allowed_.insert(hashBytes(value)); }
  void noise(bool enabled) { noise_=enabled; }
  void hold(const lt::sha1_hash& value) { heldHash_=hashBytes(value); }
  void release() { heldHash_.clear(); auto pending=std::move(held_); held_.clear(); for (const auto& request:pending) { answerPeers(request); } }
  int gets(const lt::sha1_hash& value) const { const auto it=gets_.find(hashBytes(value)); return it==gets_.end()?0:it->second; }
  int announced(const lt::sha1_hash& value) const { const auto it=announces_.find(hashBytes(value)); return it==announces_.end()?0:it->second; }
};
engine::MainlineDhtOptions options(const OwnedKrpc& fixture,unsigned short peerPort) {
  engine::MainlineDhtOptions result;
  result.port=dhtPort(peerPort); result.ipv4=true; result.ipv6=false;
  result.bootstrapNodes={{"127.0.0.1",fixture.port()}}; result.onlyPeerBootstrap=false;
  return result;
}
struct Observations {
  QString failure;
  std::vector<engine::MainlineDhtPeers> values;
  bool pump(engine::MainlineDhtService& service) {
    auto result=service.poll();
    if (!result) { failure=QString::fromStdString(result.error().message); return false; }
    for (auto& row:*result) {
      for (const auto& peer:row.peers) {
        if (!peer.address().is_loopback() || peer.address().is_unspecified() || peer.port()==0) {
          failure=QStringLiteral("Service exposed an invalid/non-owned endpoint");
        }
      }
      values.push_back(std::move(row));
    }
    return true;
  }
  bool contains(const lt::sha1_hash& wanted,unsigned short port) const {
    return std::ranges::any_of(values,[&](const auto& row){
      return row.hash==wanted && std::ranges::any_of(row.peers,[&](const auto& peer){return peer.address().is_loopback() && peer.port()==port;});
    });
  }
};
std::unique_ptr<engine::MainlineDhtService> service(const OwnedKrpc& fixture,unsigned short peerPort) {
  auto created=engine::MainlineDhtService::create(localPolicy(),options(fixture,peerPort));
  test::require(created.has_value(),"Cannot create actual owned Mainline service"); return std::move(*created);
}
struct Client {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-mainline-magnet-XXXXXX")};
  QString profile,payload,sentinel;
  std::optional<storage::ProfileLease> lease;
  Client() {
    test::require(root.isValid(),"Cannot create owned Mainline client");
    profile=root.path()+"/profile"; payload=root.path()+"/payload"; sentinel=root.path()+"/sentinel";
    test::require(QDir().mkpath(profile),"Cannot create owned Mainline profile"); test::write(sentinel,"unchanged-owned-sentinel");
    auto held=storage::ProfileLease::acquire(profile.toStdString()); test::require(held.has_value(),"Cannot hold Mainline profile");
    lease.emplace(std::move(*held));
  }
};
std::unique_ptr<engine::PersistentSession> owner() {
  auto created=engine::PersistentSession::create({7101,7102,7103});
  test::require(created.has_value(),"Mainline owner tests require actual source-bound runtime"); return std::move(*created);
}
std::optional<engine::PendingMagnetStatus> pending(engine::PersistentSession& owner,const core::TorrentHash& wanted) {
  const auto rows=owner.pendingMagnets(); const auto found=std::ranges::find_if(rows,[&](const auto& row){return row.hash==wanted;});
  return found==rows.end()?std::nullopt:std::optional{*found};
}
} // namespace

class MainlineDhtTests final : public QObject {
  Q_OBJECT
private slots:
  void lookupAndTokenAnnounceUseActualDistinctPeerPort() {
    QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost,0));
    OwnedKrpc fixture(peer.serverPort()); const auto wanted=hash('A'); fixture.allow(wanted);
    auto actual=service(fixture,peer.serverPort()); Observations observed;
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.bootstrapQueries>0 || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(actual->lookup(wanted)); // Exactly one request; poll cannot invent a replacement lookup.
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(wanted,peer.serverPort()) || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(observed.contains(wanted,peer.serverPort())); QVERIFY(fixture.gets(wanted)>0);
    const auto udpPort=actual->status().listenPort; QVERIFY(udpPort!=0); QVERIFY(udpPort!=peer.serverPort());
    QVERIFY(actual->announce(wanted,peer.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.announced(wanted)>0 || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(fixture.announced(wanted)>0);
    // A real second explicit round must issue work again: registration alone
    // must not silently coalesce the owner's later 15-minute refresh forever.
    const auto beforeGets=fixture.gets(wanted),beforeAnnounces=fixture.announced(wanted);
    QVERIFY(actual->lookup(wanted));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.gets(wanted)>beforeGets || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(fixture.gets(wanted)>beforeGets); QVERIFY(actual->announce(wanted,peer.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.announced(wanted)>beforeAnnounces || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(fixture.announced(wanted)>beforeAnnounces);
    const auto status=actual->status(); QVERIFY(status.lookups>=2); QVERIFY(status.announces>=2);
    QVERIFY(status.replies>0); QVERIFY(status.deliveredPeers>0); QCOMPARE(status.activeHashes,std::size_t{1}); QVERIFY(!status.observationsLost);
    QVERIFY(actual->close()); QCOMPARE(actual->status().phase,engine::MainlineDhtPhase::Closed);
    QVERIFY(!actual->lookup(wanted)); QVERIFY(!actual->announce(wanted,peer.serverPort()));
    QUdpSocket rebound; QVERIFY(rebound.bind(QHostAddress::LocalHost,udpPort,QUdpSocket::DontShareAddress));
  }
  void malformedUnsolicitedAndInvalidEndpointsCannotBecomePeerHints() {
    QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost,0));
    OwnedKrpc fixture(peer.serverPort()); const auto wanted=hash('B'); fixture.allow(wanted); fixture.noise(true);
    auto actual=service(fixture,peer.serverPort()); Observations observed;
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.bootstrapQueries>0 || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY(actual->lookup(wanted));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(wanted,peer.serverPort()) || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(observed.contains(wanted,peer.serverPort())); QVERIFY(fixture.noiseReplies>0);
    for (const auto& row:observed.values) {
      QCOMPARE(row.hash,wanted);
      QVERIFY(!row.peers.empty());
      for (const auto& endpoint:row.peers) { QCOMPARE(endpoint.port(),peer.serverPort()); QVERIFY(endpoint.address().is_loopback()); }
    }
    QVERIFY(actual->close());
  }
  void forgetRevokesLateReplyBeforeAnotherHashMakesProgress() {
    QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost,0)); OwnedKrpc fixture(peer.serverPort());
    const auto forgotten=hash('C'),next=hash('D'); fixture.allow(forgotten); fixture.allow(next); fixture.hold(forgotten);
    auto actual=service(fixture,peer.serverPort()); Observations observed;
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.bootstrapQueries>0 || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY(actual->lookup(forgotten));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.gets(forgotten)>0 || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(actual->forget(forgotten)); fixture.release(); QVERIFY(actual->lookup(next));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(next,peer.serverPort()) || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(observed.contains(next,peer.serverPort())); QVERIFY(!observed.contains(forgotten,peer.serverPort()));
    QCOMPARE(actual->status().activeHashes,std::size_t{1}); QVERIFY(actual->close());
  }
  void invalidInputsAndUnavailablePoliciesFailBeforeDiscovery() {
    auto policy=localPolicy(); engine::MainlineDhtOptions opts; opts.bootstrapNodes.clear(); opts.port=dhtPort(); opts.ipv6=false;
    auto disabled=policy; disabled.dht=false; QVERIFY(!engine::MainlineDhtService::create(disabled,opts));
    auto isolated=policy; isolated.mode=engine::NetworkMode::IsolatedLoopback; QVERIFY(!engine::MainlineDhtService::create(isolated,opts));
    auto proxied=policy; proxied.proxy.kind=engine::NetworkProxyKind::Socks5; proxied.proxy.host="127.0.0.1"; proxied.proxy.port=1;
    QVERIFY(!engine::MainlineDhtService::create(proxied,opts));
    auto actual=engine::MainlineDhtService::create(policy,opts); QVERIFY(actual);
    QVERIFY(!(*actual)->lookup(lt::sha1_hash{})); QVERIFY(!(*actual)->announce(hash('E'),0));
    QVERIFY(!(*actual)->addNode({"",1})); QVERIFY(!(*actual)->addNode({"0.0.0.0",1})); QVERIFY(!(*actual)->addNode({"127.0.0.1",0}));
    QCOMPARE((*actual)->status().activeHashes,std::size_t{0}); QCOMPARE((*actual)->status().lookups,std::uint64_t{0});
    QVERIFY((*actual)->close());
  }
  void hashOnlyMagnetImportsMetadataFromOwnedMainlineResult() {
    test::OwnedEngineFixture seed(false); QTRY_VERIFY_WITH_TIMEOUT(seed.ready(),15000);
    const auto seedState=test::read(seed.stateFile); const auto seedInventory=test::read(seed.profile+"/downloads.config");
    std::vector<QByteArray> originalPayload; for (const auto& file:seed.files) { originalPayload.push_back(test::read(seed.payload+'/'+file.relative)); }
    OwnedKrpc fixture(seed.port()); Client client; auto policy=localPolicy(); policy.mainlinePort=dhtPort(seed.port());
    auto parsed=engine::parseMagnetIntent("magnet:?xt=urn:btih:"+seed.hash->hex(),client.payload.toStdString(),true,policy);
    QVERIFY(parsed); QVERIFY(parsed->peers.empty()); QVERIFY(parsed->trackers.empty()); QVERIFY(parsed->webSeeds.empty()); QVERIFY(parsed->dhtNodes.empty());
    fixture.allow(parsed->requested.v1);
    auto queued=engine::queuePendingMagnet(*client.lease,*parsed,policy); QVERIFY(queued);
    auto actual=owner(); QVERIFY(actual->configureNetwork(policy));
    QVERIFY(actual->configureMainlineBootstrap({{"127.0.0.1",fixture.port()}})); QVERIFY(actual->startIdle(*client.lease));
    QVERIFY(actual->startMagnet(*client.lease,*queued,client.profile.toStdString()));
    QString pollFailure;
    const auto terminal=[&] {
      if (!actual->poll()) { pollFailure=QStringLiteral("Persistent owner poll failed"); return true; }
      const auto row=pending(*actual,*seed.hash);
      return !fixture.failure.isEmpty() || (row && (row->phase==engine::MagnetPhase::Failed
          || (row->phase==engine::MagnetPhase::Imported && !row->retained)));
    };
    QTRY_VERIFY_WITH_TIMEOUT(terminal(),45000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(pollFailure.isEmpty(),qPrintable(pollFailure));
    const auto row=pending(*actual,*seed.hash); QVERIFY(row);
    QVERIFY2(!row->failure,row->failure?row->failure->message.c_str():"none"); QCOMPARE(row->phase,engine::MagnetPhase::Imported); QVERIFY(!row->retained);
    const auto discovery=actual->mainlineDiscovery(); QCOMPARE(discovery.state,engine::MainlineDiscoveryStatus::State::Running);
    QVERIFY(discovery.lookups>0); QVERIFY(discovery.deliveredPeers>0); QCOMPARE(discovery.announces,std::uint64_t{0});
    QVERIFY(fixture.gets(parsed->requested.v1)>0); QCOMPARE(fixture.announced(parsed->requested.v1),0); QVERIFY(actual->managedDownloads().empty());
    QVERIFY(!QFileInfo::exists(client.payload));
    auto inventory=core::Document::parse(test::read(client.profile+"/downloads.config").toStdString()); QVERIFY(inventory);
    auto library=core::readLibrary(*inventory); QVERIFY(library); QVERIFY(library->issues.empty()); QCOMPARE(library->downloads.size(),std::size_t{1});
    QVERIFY(library->downloads.front().hash==seed.hash);
    QCOMPARE(library->downloads.front().originalState,std::int64_t{70}); QVERIFY(!library->downloads.front().forceStart);
    const auto closed=actual->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined); QVERIFY(closed.retirement.storageReleased); QVERIFY(!closed.checkpointFailure);
    QCOMPARE(actual->mainlineDiscovery().state,engine::MainlineDiscoveryStatus::State::Closed);
    QCOMPARE(test::read(client.sentinel),QByteArray("unchanged-owned-sentinel"));
    QCOMPARE(test::read(seed.stateFile),seedState); QCOMPARE(test::read(seed.profile+"/downloads.config"),seedInventory); QVERIFY(seed.intact(false));
    for (std::size_t i=0;i<seed.files.size();++i) { QCOMPARE(test::read(seed.payload+'/'+seed.files[i].relative),originalPayload[i]); }
  }
  void inactiveOrProxiedOwnerNeverFallsBackToMainline_data() {
    QTest::addColumn<int>("route"); QTest::newRow("isolated")<<0; QTest::newRow("normal-disabled")<<1; QTest::newRow("proxy-unavailable")<<2;
  }
  void inactiveOrProxiedOwnerNeverFallsBackToMainline() {
    QFETCH(int,route); Client client; auto policy=localPolicy(); QTcpServer proxy;
    if (route==0) { policy=engine::NetworkPolicy{}; }
    if (route==1) { policy.dht=false; }
    if (route==2) {
      QVERIFY(proxy.listen(QHostAddress::LocalHost,0)); policy.bindAddresses={"0.0.0.0"};
      policy.proxy.kind=engine::NetworkProxyKind::Socks5; policy.proxy.host="127.0.0.1"; policy.proxy.port=proxy.serverPort(); policy.proxy.peers=true;
    }
    policy.mainlinePort=dhtPort(); auto actual=owner(); QVERIFY(actual->configureNetwork(policy));
    QVERIFY(actual->configureMainlineBootstrap({})); QVERIFY(actual->startIdle(*client.lease)); QVERIFY(actual->poll());
    const auto state=actual->mainlineDiscovery();
    QCOMPARE(state.state,route==2?engine::MainlineDiscoveryStatus::State::Unavailable:engine::MainlineDiscoveryStatus::State::Disabled);
    QCOMPARE(state.lookups,std::uint64_t{0}); QCOMPARE(state.announces,std::uint64_t{0}); QCOMPARE(state.deliveredPeers,std::uint64_t{0});
    if (route==2) { QVERIFY(state.failure); }
    const auto closed=actual->close(std::chrono::steady_clock::now()+5s); QVERIFY(closed.sessionJoined); QVERIFY(closed.retirement.storageReleased);
    QCOMPARE(test::read(client.sentinel),QByteArray("unchanged-owned-sentinel"));
  }
  void originalPerTorrentDhtSourcePermissions_data() {
    QTest::addColumn<QByteArray>("key"); QTest::addColumn<QByteArray>("value"); QTest::addColumn<bool>("allowed");
    QTest::newRow("default-public")<<QByteArray{}<<QByteArray{}<<true;
    QTest::newRow("DHT-source-disabled")<<QByteArray("peersources")<<QByteArray("l7:Trackere")<<false;
    QTest::newRow("DHT-source-denied")<<QByteArray("peersourcesdenied")<<QByteArray("l3:DHTe")<<false;
    QTest::newRow("announce-disabled-scrape-pending")<<QByteArray("pluginoptions")<<QByteArray("d5:mldhtd14:enableannouncei0eee")<<false;
  }
  void originalPerTorrentDhtSourcePermissions() {
    QFETCH(QByteArray,key); QFETCH(QByteArray,value); QFETCH(bool,allowed); test::OwnedEngineFixture fixture(false);
    if (!key.isEmpty()) {
      auto source=core::Document::parse(test::read(fixture.stateFile).toStdString()); QVERIFY(source);
      const auto* attributes=source->find("attributes"); QVERIFY(attributes); auto raw=core::encodeValue(*attributes); QVERIFY(raw);
      auto changed=core::Document::parse(*raw); QVERIFY(changed); QVERIFY(changed->replaceValue(key.toStdString(),value.toStdString()));
      QVERIFY(source->replaceValue("attributes",changed->serialize())); test::write(fixture.stateFile,QByteArray::fromStdString(source->serialize()));
    }
    auto held=storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
    auto prepared=engine::detail::ManagedStorage::prepare(*held,*fixture.hash,localPolicy());
    QVERIFY2(prepared,prepared?"":prepared.error().message.c_str());
    QCOMPARE(prepared->mainlinePolicy().discover,allowed); QCOMPARE(prepared->mainlinePolicy().announce,allowed);
  }
};
QTEST_GUILESS_MAIN(MainlineDhtTests)
#include "mainline_dht_tests.moc"
