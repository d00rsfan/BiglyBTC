#include "mainline_dht.hpp"
#include "owned_mainline_krpc.hpp"
#include "queued_mainline_scrape.hpp"
#include "library_commands.hpp"
#include "persistent_session.hpp"
#include "magnet_intake.hpp"
#include "managed_storage.hpp"
#include "mainline_checkpoint.hpp"
#include "owned_engine_fixture.hpp"

#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtTest>
#include <zlib.h>

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
using OwnedKrpc=test::mainline::OwnedKrpc;

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
QByteArray gzipOwned(const QByteArray& input){
  z_stream stream{};test::require(::deflateInit2(&stream,Z_BEST_SPEED,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK,"Owned cache gzip init");
  QByteArray output(static_cast<qsizetype>(::deflateBound(&stream,static_cast<uLong>(input.size()))),'\0');
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));stream.avail_in=static_cast<uInt>(input.size());
  stream.next_out=reinterpret_cast<Bytef*>(output.data());stream.avail_out=static_cast<uInt>(output.size());
  const auto rc=::deflate(&stream,Z_FINISH);const auto size=stream.total_out;::deflateEnd(&stream);
  test::require(rc==Z_STREAM_END,"Owned cache gzip finish");output.resize(static_cast<qsizetype>(size));return output;
}
lt::sha1_hash hash(char value) { const QByteArray raw(20,value); return lt::sha1_hash(raw.constData()); }
const core::Node* field(const core::Node& value, std::string_view key, core::Node::Type type) {
  const auto* found=value.find(key); return found && found->type==type ? found : nullptr;
}
QByteArray compact(unsigned short port) {
  QByteArray result; result.append(char(127)); result.append(char(0)); result.append(char(0)); result.append(char(1));
  result.append(static_cast<char>(port >> 8)); result.append(static_cast<char>(port & 255)); return result;
}
QByteArray originalCache(unsigned short port) {
  // Original f929 cache schema. The stored verification bit is only historical
  // input: the real KRPC fixture must answer before it can supply metadata peers.
  const auto contact=dictionary({{"addr",string(compact(port))},{"id",string(QByteArray(20,'K'))},
      {"created","i800e"},{"lastSeen","i995e"},{"failedCount","i0e"},{"verified","i1e"}});
  return dictionary({{"mainEntries",list(port?std::vector<QByteArray>{contact}:std::vector<QByteArray>{})},
      {"replacements",list({})},{"oldKey",string(QByteArray(20,'R'))},{"timestamp","i1000e"},
      {"opaque",string(QByteArray::fromHex("00ff51"))}});
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
std::unique_ptr<engine::PersistentSession> owner(std::uint64_t epoch=7103) {
  auto created=engine::PersistentSession::create({7101,7102,epoch});
  test::require(created.has_value(),"Mainline owner tests require actual source-bound runtime"); return std::move(*created);
}
std::optional<engine::PendingMagnetStatus> pending(engine::PersistentSession& owner,const core::TorrentHash& wanted) {
  const auto rows=owner.pendingMagnets(); const auto found=std::ranges::find_if(rows,[&](const auto& row){return row.hash==wanted;});
  return found==rows.end()?std::nullopt:std::optional{*found};
}
// Actual incoming TCP/BEP5 peer. A FAST reject_request after PORT frames is an
// ordered network-executor barrier, not a sleep or an owner/provider test hook.
class PortPeer final {
  QTcpSocket socket_;
  QByteArray input_,hash_;
  bool handshake_=false;
  void fail(const char* value){if(failure.isEmpty()){failure=QString::fromLatin1(value);}socket_.abort();}
  static void append32(QByteArray& value,quint32 n){for(int shift=24;shift>=0;shift-=8){value+=static_cast<char>(n>>shift);}}
  static quint32 read32(const QByteArray& value){quint32 n=0;for(int i=0;i<4;++i){n=(n<<8)|static_cast<unsigned char>(value[i]);}return n;}
  void receive(){
    input_+=socket_.readAll();if(input_.size()>256*1024){fail("Owned PORT peer receive bound");return;}
    if(!handshake_){
      if(input_.size()<68){return;}
      if(input_[0]!=19||input_.mid(1,19)!="BitTorrent protocol"||input_.mid(28,20)!=hash_){fail("Owned PORT handshake identity mismatch");return;}
      dhtSupported=(static_cast<unsigned char>(input_[27])&1)!=0;
      if((static_cast<unsigned char>(input_[27])&4)==0){fail("Owned PORT barrier requires actual negotiated FAST");return;}
      input_.remove(0,68);handshake_=true;
    }
    while(input_.size()>=4){
      const auto n=read32(input_);if(n>128*1024){fail("Owned PORT frame bound");return;}
      if(input_.size()<static_cast<qsizetype>(n)+4){return;}
      const auto body=input_.mid(4,n);input_.remove(0,static_cast<qsizetype>(n)+4);
      if(body.isEmpty()){continue;}
      const auto kind=static_cast<unsigned char>(body[0]);
      if(kind==9){
        if(body.size()!=3){fail("Malformed actual outgoing PORT");return;}
        const auto port=static_cast<unsigned short>((static_cast<unsigned char>(body[1])<<8)|static_cast<unsigned char>(body[2]));
        if(port==0){fail("Zero actual outgoing PORT");return;}advertised.push_back(port);
      }else if(kind==16){
        if(body.size()!=13||read32(body.mid(1))!=0||read32(body.mid(5))!=0||read32(body.mid(9))!=16384){fail("Wrong ordered FAST rejection");return;}
        ++barriers;
      }else if(kind==7){fail("Owned empty managed peer unexpectedly supplied payload");return;}
    }
  }
public:
  QString failure;
  bool dhtSupported=false;
  int barriers=0;
  std::vector<unsigned short> advertised;
  PortPeer(unsigned short ownerPort,const core::TorrentHash& selected,QHostAddress source=QHostAddress(QStringLiteral("127.0.0.2"))){
    const auto raw=selected.bytes();test::require(raw.size()>=20,"PORT peer needs actual selected full identity");
    hash_=QByteArray(reinterpret_cast<const char*>(raw.data()),20);
    test::require(socket_.bind(source,0),"Cannot bind owned PORT source");
    QObject::connect(&socket_,&QTcpSocket::connected,&socket_,[this]{
      QByteArray handshake(68,'\0');handshake[0]=19;handshake.replace(1,19,"BitTorrent protocol");
      handshake[27]=5;handshake.replace(28,20,hash_);static unsigned serial=0;const auto peerId=QByteArray("-BCPORT-")+QByteArray::number(++serial).rightJustified(12,'0');
      handshake.replace(48,20,peerId);
      if(socket_.write(handshake)!=handshake.size()){fail("Owned PORT handshake write failed");}
      // Eight real pieces in OwnedEngineFixture; this peer has none.
      frame(QByteArray::fromHex("0500"));
    });
    QObject::connect(&socket_,&QTcpSocket::readyRead,&socket_,[this]{receive();});
    socket_.connectToHost(QHostAddress::LocalHost,ownerPort);
  }
  bool ready()const{return handshake_&&failure.isEmpty();}
  unsigned short port()const{return socket_.localPort();}
  void frame(const QByteArray& body){QByteArray packet;append32(packet,static_cast<quint32>(body.size()));packet+=body;if(socket_.write(packet)!=packet.size()){fail("Owned PORT frame write failed");}}
  void sendPort(unsigned short port,int copies=1){
    QByteArray body(1,char(9));body+=static_cast<char>(port>>8);body+=static_cast<char>(port&255);
    for(int i=0;i<copies;++i){frame(body);}
  }
  void barrier(){QByteArray request(1,char(6));append32(request,0);append32(request,0);append32(request,16384);frame(request);}
};
std::optional<engine::ManagedDownloadStatus> managed(engine::PersistentSession& actual,const core::TorrentHash& key){
  const auto rows=actual.managedDownloads();const auto found=std::ranges::find_if(rows,[&](const auto& row){return row.hash==key;});
  return found==rows.end()?std::nullopt:std::optional{*found};
}

} // namespace

class MainlineDhtTests final : public QObject {
  Q_OBJECT
private slots:

  void queuedScrapeUsesActualLiveNodesExplicitDialectAndNeverAnnounces(){
    test::OwnedEngineFixture saved(false);auto lease=storage::ProfileLease::acquire(saved.native());QVERIFY(lease);
    const auto inventoryBefore=test::read(saved.profile+"/downloads.config"),stateBefore=test::read(saved.stateFile);
    auto authority=engine::readSavedMainlineScrapeTargets(*lease);QVERIFY(authority);QCOMPARE(authority->size(),std::size_t{1});
    engine::QueuedMainlineScrapes consumer;QVERIFY(consumer.reconcile(*authority,0,0));QVERIFY(consumer.checkDue(100000));
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));
    const auto wanted=lt::sha1_hash(reinterpret_cast<const char*>(saved.hash->bytes().data())),other=hash('L');
    OwnedKrpc fixture(peer.serverPort());fixture.allow(wanted);fixture.allow(other);
    auto opts=options(fixture,peer.serverPort());opts.bootstrapNodes.clear();auto created=engine::MainlineDhtService::create(localPolicy(),opts);QVERIFY(created);auto actual=std::move(*created);Observations observed;
    QVERIFY(actual->addNode({"127.0.0.1",fixture.port()}));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.bootstrapQueries>0),15000);QVERIFY(actual->lookup(wanted));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(wanted,peer.serverPort())||!observed.failure.isEmpty()||!fixture.failure.isEmpty()),15000);
    QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    std::vector<engine::MainlineDhtScrape> replies;
    const auto pump=[&]{
      if(!observed.pump(*actual)){return true;}auto rows=actual->takeScrapes();
      if(!rows){observed.failure=QString::fromStdString(rows.error().message);return true;}
      for(auto& row:*rows){replies.push_back(std::move(row));}return !replies.empty();
    };
    auto first=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(first);QVERIFY(!actual->scrape(other,services::mainline_scrape::Dialect::OriginalBFse));
    QVERIFY(consumer.started(authority->front().hash,authority->front().scope,*first,123456));
    QVERIFY(actual->lookup(other));
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    QCOMPARE(replies.size(),std::size_t{1});QCOMPARE(replies[0].round,*first);QCOMPARE(replies[0].hash,wanted);
    QCOMPARE(replies[0].summary.responses,std::size_t{1});QCOMPARE(replies[0].summary.seeds,1);QCOMPARE(replies[0].summary.peers,1);
    auto freshAuthority=engine::readSavedMainlineScrapeTargets(*lease);QVERIFY(freshAuthority);QCOMPARE(freshAuthority->size(),std::size_t{1});
    consumer.completed(*first,replies[0],freshAuthority->front(),100001,0);
    const auto estimates=consumer.estimates();QCOMPARE(estimates.size(),std::size_t{1});QCOMPARE(estimates[0].counts.seeds,1);QCOMPARE(estimates[0].startedWallMillis,123456);
    QCOMPARE(test::read(saved.profile+"/downloads.config"),inventoryBefore);QCOMPARE(test::read(saved.stateFile),stateBefore);QVERIFY(saved.intact(false));
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(other,peer.serverPort())),15000);
    QCOMPARE(actual->status().announces,std::uint64_t{0});QCOMPARE(fixture.announced(wanted),0);QCOMPARE(fixture.announced(other),0);
    replies.clear();fixture.scrapeReply("BFsd");auto refused=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(refused);
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QCOMPARE(replies[0].summary.responses,std::size_t{0});QCOMPARE(replies[0].summary.seeds,0);
    replies.clear();auto explicitLt=actual->scrape(wanted,services::mainline_scrape::Dialect::LibtorrentBFsd);QVERIFY(explicitLt);
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QCOMPARE(replies[0].summary.responses,std::size_t{1});QCOMPARE(replies[0].summary.seeds,1);
    replies.clear();fixture.scrapeReply("BFse",true);auto zero=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(zero);
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QCOMPARE(replies[0].summary.responses,std::size_t{1});QCOMPARE(replies[0].summary.seeds,0);QCOMPARE(replies[0].summary.peers,0);
    // Saved completion controls only this source query preference. Payload
    // remains zero/unverified; no owner admission or verified bit is invented.
    auto completeState=core::Document::parse(stateBefore.toStdString(),storage::compatibleConfigLimits());QVERIFY(completeState);
    auto attributes=core::Document::parse("d10:parametersd8:dndflagsi0eee");QVERIFY(attributes);
    QVERIFY(completeState->replaceValue("attributes",attributes->serialize()));test::write(saved.stateFile,bytes(completeState->serialize()));
    auto inventory=core::Document::parse(inventoryBefore.toStdString(),storage::compatibleConfigLimits());QVERIFY(inventory);
    const auto& oldRow=inventory->find("downloads")->children[0];auto row=core::Document::parse(inventory->originalBytes().substr(oldRow.begin,oldRow.end-oldRow.begin),storage::compatibleConfigLimits());QVERIFY(row);
    QVERIFY(row->replaceValue("completedbytes","i131072e"));QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(saved.profile+"/downloads.config",bytes(inventory->serialize()));
    auto completedAuthority=engine::readSavedMainlineScrapeTargets(*lease);QVERIFY(completedAuthority);QCOMPARE(completedAuthority->size(),std::size_t{1});QVERIFY(completedAuthority->front().noSeeds);
    fixture.scrapeReply();fixture.scrapeNoSeeds(1);replies.clear();auto noSeeds=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse,completedAuthority->front().noSeeds);QVERIFY(noSeeds);
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QCOMPARE(replies[0].summary.responses,std::size_t{1});
    // LT evicts a known endpoint after a correlated changed-ID reply. Keep
    // this destructive control last, after all valid live-node queries.
    const auto beforeWrong=fixture.scrapes();fixture.scrapeNoSeeds(0);
    replies.clear();fixture.scrapeReply("BFse",false,true);auto wrong=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(wrong);
    QTRY_VERIFY_WITH_TIMEOUT(pump(),15000);QCOMPARE(replies[0].summary.responses,std::size_t{0});
    QCOMPARE(fixture.scrapes(),beforeWrong+1);
    test::write(saved.stateFile,stateBefore);test::write(saved.profile+"/downloads.config",inventoryBefore);QVERIFY(saved.intact(false));
    QVERIFY(fixture.scrapes()>=6);QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QVERIFY(actual->close());
  }
  void heldScrapeCancellationCannotPublishIntoSameHashFreshRound(){
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));OwnedKrpc fixture(peer.serverPort());
    const auto wanted=hash('C');fixture.allow(wanted);auto opts=options(fixture,peer.serverPort());opts.bootstrapNodes.clear();
    auto created=engine::MainlineDhtService::create(localPolicy(),opts);QVERIFY(created);auto actual=std::move(*created);Observations observed;
    QVERIFY(actual->addNode({"127.0.0.1",fixture.port()}));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.bootstrapQueries>0),15000);
    QVERIFY(actual->lookup(wanted));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(wanted,peer.serverPort())),15000);
    fixture.hold(wanted);auto old=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(old);
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.scrapes()==1||!fixture.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QVERIFY(actual->cancelScrape(*old));
    auto fresh=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(fresh);QVERIFY(*fresh!=*old);
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.scrapes()==2||!fixture.failure.isEmpty()),15000);
    fixture.release();std::vector<engine::MainlineDhtScrape> replies;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(!observed.pump(*actual)){return true;}auto rows=actual->takeScrapes();if(!rows){return true;}for(auto& row:*rows){replies.push_back(std::move(row));}return !replies.empty();}()),15000);
    QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    QCOMPARE(replies.size(),std::size_t{1});QCOMPARE(replies[0].round,*fresh);QCOMPARE(replies[0].summary.seeds,1);
    QCOMPARE(actual->status().announces,std::uint64_t{0});QVERIFY(actual->close());QVERIFY(!actual->takeScrapes());
  }
  void savedQueuedAuthorityReadsNoPayloadAndRefusesOtherConsumers(){
    test::OwnedEngineFixture fixture(false);auto held=storage::ProfileLease::acquire(fixture.native());QVERIFY(held);
    const auto inventory=test::read(fixture.profile+"/downloads.config"),state=test::read(fixture.stateFile),payload=test::read(fixture.payload+"/sample.bin");
    auto good=engine::readSavedMainlineScrapeTargets(*held);QVERIFY2(good.has_value(),good?"":good.error().message.c_str());QCOMPARE(good->size(),std::size_t{1});QCOMPARE(good->front().hash,*fixture.hash);QCOMPARE(good->front().scope.size(),std::size_t{64});
    engine::QueuedMainlineScrapes current;QVERIFY(current.reconcile(*good,0,0));auto due=current.due(60000);QVERIFY(due);
    QVERIFY(current.started(due->hash,due->scope,19,123456));
    auto changed=core::Document::parse(state.toStdString(),storage::compatibleConfigLimits());QVERIFY(changed);
    // Encode through a real document below; no malformed-fixture permission.
    libtorrent::entry denied(libtorrent::entry::dictionary_type{});denied["networks"]=libtorrent::entry::list_type{libtorrent::entry("Public")};denied["peersourcesdenied"]=libtorrent::entry::list_type{libtorrent::entry("DHT")};
    changed=core::Document::parse(state.toStdString(),storage::compatibleConfigLimits());QVERIFY(changed);QVERIFY(changed->replaceValue("attributes",test::encoded(denied)));test::write(fixture.stateFile,bytes(changed->serialize()));
    auto revoked=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(revoked);QVERIFY(revoked->empty());
    engine::MainlineDhtScrape result{19,lt::sha1_hash(reinterpret_cast<const char*>(fixture.hash->bytes().data())),services::mainline_scrape::Dialect::OriginalBFse,{3,2,0,1},false};
    current.completed(19,result,{},60001,0);QVERIFY(current.estimates().empty());
    test::write(fixture.stateFile,state);
    libtorrent::entry cached(libtorrent::entry::dictionary_type{});cached["hash"]=std::string(reinterpret_cast<const char*>(fixture.hash->bytes().data()),fixture.hash->bytes().size());
    cached["attributes"]=denied;libtorrent::entry cache(libtorrent::entry::dictionary_type{});cache["state"]=libtorrent::entry::list_type{cached};
    const auto cacheBytes=gzipOwned(bytes(test::encoded(cache)));const auto cachePath=fixture.profile+"/active/cache.dat";test::write(cachePath,cacheBytes);
    auto cachedDenied=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(cachedDenied);QVERIFY(cachedDenied->empty());
    QCOMPARE(test::read(cachePath),cacheBytes);QCOMPARE(test::read(fixture.stateFile),state);QVERIFY(QFile::remove(cachePath));
    std::vector<libtorrent::entry> refusedAttributes;
    for(const auto& name:{"networks","flags"}){
      libtorrent::entry value(libtorrent::entry::dictionary_type{});
      if(std::string_view(name)=="networks"){value[name]=libtorrent::entry::list_type{};}
      else{value[name]=std::string_view(name)=="flags"?512:1;}
      refusedAttributes.push_back(std::move(value));
    }
    for(const auto& [name,value]:std::vector<std::pair<std::string,std::string>>{{"networks","public"},{"peersources","dht"}}){
      libtorrent::entry attributes(libtorrent::entry::dictionary_type{});attributes[name]=libtorrent::entry::list_type{libtorrent::entry(value)};refusedAttributes.push_back(std::move(attributes));
    }
    for(const auto& attributes:refusedAttributes){
      auto doc=core::Document::parse(state.toStdString(),storage::compatibleConfigLimits());QVERIFY(doc);
      QVERIFY(doc->replaceValue("attributes",test::encoded(attributes)));test::write(fixture.stateFile,bytes(doc->serialize()));
      auto refused=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(refused);QVERIFY(refused->empty());
    }
    auto doc=core::Document::parse(state.toStdString(),storage::compatibleConfigLimits());QVERIFY(doc);
    libtorrent::entry marker("dht://"+fixture.hash->hex()+".dht/announce");QVERIFY(doc->replaceValue("announce",test::encoded(marker)));test::write(fixture.stateFile,bytes(doc->serialize()));
    auto decentralized=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(decentralized);QCOMPARE(decentralized->size(),std::size_t{1});
    // A configured tracker cannot be inferred to have failed from saved counts.
    libtorrent::entry tracker("http://127.0.0.1:1/announce");doc=core::Document::parse(state.toStdString(),storage::compatibleConfigLimits());QVERIFY(doc);QVERIFY(doc->replaceValue("announce",test::encoded(tracker)));test::write(fixture.stateFile,bytes(doc->serialize()));
    auto unavailable=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(unavailable);QVERIFY(unavailable->empty());
    test::write(fixture.stateFile,state);
    for(const auto& intent:{"BiglyBTC.verifiedSeeding","BiglyBTC.allocationMode"}){
      auto saved=core::Document::parse(inventory.toStdString(),storage::compatibleConfigLimits());QVERIFY(saved);
      const auto& raw=saved->find("downloads")->children[0];auto entry=core::Document::parse(saved->originalBytes().substr(raw.begin,raw.end-raw.begin),storage::compatibleConfigLimits());QVERIFY(entry);
      QVERIFY(entry->replaceValue(intent,"i1e"));QVERIFY(saved->replaceValue("downloads","l"+entry->serialize()+"e"));test::write(fixture.profile+"/downloads.config",bytes(saved->serialize()));
      auto blocked=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(blocked);QVERIFY(blocked->empty());
    }
    test::write(fixture.profile+"/downloads.config",inventory);
    {
      auto saved=core::Document::parse(inventory.toStdString(),storage::compatibleConfigLimits());QVERIFY(saved);const auto& raw=saved->find("downloads")->children[0];
      auto entry=core::Document::parse(saved->originalBytes().substr(raw.begin,raw.end-raw.begin),storage::compatibleConfigLimits());QVERIFY(entry);QVERIFY(entry->replaceValue("forceStart","i1e"));
      QVERIFY(saved->replaceValue("downloads","l"+entry->serialize()+"e"));test::write(fixture.profile+"/downloads.config",bytes(saved->serialize()));
      auto forced=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(forced);QVERIFY(forced->empty());
    }
    auto saved=core::Document::parse(inventory.toStdString(),storage::compatibleConfigLimits());QVERIFY(saved);
    auto rows=core::Document::parse(saved->originalBytes().substr(saved->find("downloads")->children[0].begin,saved->find("downloads")->children[0].end-saved->find("downloads")->children[0].begin),storage::compatibleConfigLimits());QVERIFY(rows);QVERIFY(rows->replaceValue("state","i70e"));QVERIFY(saved->replaceValue("downloads","l"+rows->serialize()+"e"));test::write(fixture.profile+"/downloads.config",bytes(saved->serialize()));
    auto stopped=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(stopped);QVERIFY(stopped->empty());
    test::write(fixture.profile+"/downloads.config",inventory);
    // Real explicit saved Stop→Queue publications are fresh generations. A
    // failed/no-ack command or a temporary empty eligible list is not one.
    current.revoke(*fixture.hash);auto beforeStop=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(beforeStop);QVERIFY(current.reconcile(*beforeStop,70000,0));QVERIFY(!current.due(INT64_MAX));
    current.unavailable();QVERIFY(current.reconcile({},70001,0));QVERIFY(current.reconcile(*beforeStop,70002,0));QVERIFY(!current.due(INT64_MAX));
    auto stoppedCommand=storage::applyLibraryCommand(*held,*fixture.hash,core::DownloadAction::Stop);QVERIFY(stoppedCommand);
    auto stopFacts=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(stopFacts);QVERIFY(stopFacts->empty());current.settled(*fixture.hash);QVERIFY(current.reconcile(*stopFacts,70003,0));
    auto queuedCommand=storage::applyLibraryCommand(*held,*fixture.hash,core::DownloadAction::Queue);QVERIFY(queuedCommand);
    auto queueFacts=engine::readSavedMainlineScrapeTargets(*held);QVERIFY(queueFacts);QCOMPARE(queueFacts->size(),std::size_t{1});current.settled(*fixture.hash);QVERIFY(current.reconcile(*queueFacts,70004,0));QVERIFY(current.due(130004));
    QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.stateFile),state);QCOMPARE(test::read(fixture.payload+"/sample.bin"),payload);QVERIFY(fixture.intact(false));
  }
  void unissuedScrapeCancellationReclaimsWithoutLosingCloseFence(){
    auto policy=localPolicy();engine::MainlineDhtOptions opts;opts.bootstrapNodes.clear();opts.port=dhtPort();opts.ipv6=false;
    auto created=engine::MainlineDhtService::create(policy,opts);QVERIFY(created);auto& actual=**created;
    for(unsigned i=0;i<128;++i){
      auto round=actual.scrape(hash('B'),services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(round);QVERIFY(actual.cancelScrape(*round));
      QCOMPARE(actual.status().scrapeRetainedRounds,std::size_t{0});QCOMPARE(actual.status().scrapePendingProbes,std::size_t{0});
    }
    auto completed=actual.takeScrapes();QVERIFY(completed);QVERIFY(completed->empty());QCOMPARE(actual.status().lookups,std::uint64_t{0});QCOMPARE(actual.status().announces,std::uint64_t{0});
    QVERIFY(actual.close());QVERIFY(!actual.scrape(hash('B'),services::mainline_scrape::Dialect::OriginalBFse));QVERIFY(!actual.cancelScrape(1));
  }
  void completedWireRoundsReclaimBeyondSixtyFourAndUnreadResultsStayBounded(){
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));OwnedKrpc fixture(peer.serverPort());
    const auto wanted=hash('F');fixture.allow(wanted);auto opts=options(fixture,peer.serverPort());opts.bootstrapNodes.clear();
    auto created=engine::MainlineDhtService::create(localPolicy(),opts);QVERIFY(created);auto actual=std::move(*created);Observations observed;
    QVERIFY(actual->addNode({"127.0.0.1",fixture.port()}));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.bootstrapQueries>0),15000);
    QVERIFY(actual->lookup(wanted));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(wanted,peer.serverPort())),15000);
    fixture.duplicateScrapes(true); // Duplicate wire packets cannot create a second actual RPC callback.
    std::uint64_t previous=0;
    for(unsigned i=0;i<96;++i){
      auto round=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY2(round,round?"":round.error().message.c_str());QVERIFY(*round>previous);previous=*round;
      QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),actual->status().scrapeQueuedResults==1||!observed.failure.isEmpty()||!fixture.failure.isEmpty()),15000);
      QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
      auto replies=actual->takeScrapes();QVERIFY(replies);QCOMPARE(replies->size(),std::size_t{1});QCOMPARE(replies->front().round,*round);QCOMPARE(replies->front().hash,wanted);
      QCOMPARE(replies->front().summary.responses,std::size_t{1});QCOMPARE(replies->front().summary.seeds,1);QCOMPARE(replies->front().summary.peers,1);
      QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{0});QCOMPARE(actual->status().scrapePendingProbes,std::size_t{0});QCOMPARE(actual->status().scrapeQueuedResults,std::size_t{0});
    }
    QCOMPARE(fixture.scrapes(),96);fixture.duplicateScrapes(false);
    // Reclaiming terminal cookies must not make the copied-result queue unbounded.
    for(unsigned i=0;i<64;++i){
      auto round=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(round);
      QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),actual->status().scrapeQueuedResults==i+1||!observed.failure.isEmpty()||!fixture.failure.isEmpty()),15000);
      QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{0});
    }
    auto blocked=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(!blocked);QCOMPARE(blocked.error().code,storage::ErrorCode::Busy);
    auto unread=actual->takeScrapes();QVERIFY(unread);QCOMPARE(unread->size(),std::size_t{64});std::set<std::uint64_t> rounds;
    for(const auto& reply:*unread){QVERIFY(rounds.insert(reply.round).second);QCOMPARE(reply.hash,wanted);QCOMPARE(reply.summary.responses,std::size_t{1});}
    auto again=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(again);QVERIFY(actual->cancelScrape(*again));
    QCOMPARE(fixture.scrapes(),160);QCOMPARE(fixture.announced(wanted),0);QCOMPARE(actual->status().announces,std::uint64_t{0});QVERIFY(actual->close());
  }
  void cancelledHeldAndErrorTimeoutProbesRequireActualTerminalConsumption(){
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));OwnedKrpc fixture(peer.serverPort());
    const auto wanted=hash('H');fixture.allow(wanted);auto opts=options(fixture,peer.serverPort());opts.bootstrapNodes.clear();
    auto created=engine::MainlineDhtService::create(localPolicy(),opts);QVERIFY(created);auto actual=std::move(*created);Observations observed;
    QVERIFY(actual->addNode({"127.0.0.1",fixture.port()}));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.bootstrapQueries>0),15000);
    QVERIFY(actual->lookup(wanted));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),observed.contains(wanted,peer.serverPort())),15000);
    fixture.hold(wanted);auto old=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(old);
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.scrapes()==1||!fixture.failure.isEmpty()),15000);QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    QVERIFY(actual->cancelScrape(*old));QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{1});QCOMPARE(actual->status().scrapePendingProbes,std::size_t{1});
    // No poll/terminal consumption: elapsed time and later local cancellations
    // cannot reclaim the held probe, even while zero-issued rounds are reused.
    for(unsigned i=0;i<96;++i){auto local=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(local);QVERIFY(actual->cancelScrape(*local));}
    QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{1});QCOMPARE(actual->status().scrapePendingProbes,std::size_t{1});
    fixture.release();
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),actual->status().scrapeRetainedRounds==0||!observed.failure.isEmpty()||!fixture.failure.isEmpty()),15000);
    QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    auto cancelled=actual->takeScrapes();QVERIFY(cancelled);QVERIFY(cancelled->empty());
    auto fresh=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(fresh);QVERIFY(*fresh>*old);
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),actual->status().scrapeQueuedResults==1),15000);
    auto success=actual->takeScrapes();QVERIFY(success);QCOMPARE(success->size(),std::size_t{1});QCOMPARE(success->front().round,*fresh);QCOMPARE(success->front().summary.seeds,1);
    fixture.errorScrapes(true);auto failed=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(failed);
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),actual->status().scrapeQueuedResults==1),15000);
    auto error=actual->takeScrapes();QVERIFY(error);QCOMPARE(error->size(),std::size_t{1});QCOMPARE(error->front().round,*failed);QCOMPARE(error->front().summary.responses,std::size_t{0});QCOMPARE(actual->status().scrapePendingProbes,std::size_t{0});QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{0});
    fixture.errorScrapes(false);fixture.dropScrapes(true);auto timeout=actual->scrape(wanted,services::mainline_scrape::Dialect::OriginalBFse);QVERIFY(timeout);
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),fixture.scrapes()==4),15000);QVERIFY(actual->cancelScrape(*timeout));QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{1});
    // Wait for the provider's real15s RPC timeout alert, never its duration as
    // a reclamation heuristic. Only consumed typed terminals change this count.
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(*actual),actual->status().scrapePendingProbes==0||!observed.failure.isEmpty()),30000);
    QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{0});
    auto expired=actual->takeScrapes();QVERIFY(expired);QVERIFY(expired->empty());QCOMPARE(fixture.announced(wanted),0);
    QVERIFY(actual->close());QCOMPARE(actual->status().scrapeRetainedRounds,std::size_t{0});
  }
  void queuedSchedulingAndPositivePublicationAreScopeBoundWithoutInventedExpiry(){
    const auto raw=std::string(20,'Q');auto key=core::TorrentHash::parse(raw);QVERIFY(key);
    engine::SavedMainlineScrapeTarget target{*key,{},std::string(64,'a')};
    engine::QueuedMainlineScrapes queue;QVERIFY(queue.reconcile(std::span(&target,1),0,0));
    QVERIFY(!queue.checkDue(99999));QVERIFY(queue.checkDue(100000));QVERIFY(!queue.checkDue(109999));QVERIFY(queue.checkDue(110000));
    QCOMPARE(engine::QueuedMainlineScrapes::delayMillis(true,59),119000);QCOMPARE(engine::QueuedMainlineScrapes::delayMillis(false,599),1799000);
    QVERIFY(!queue.due(59999));QVERIFY(queue.due(60000));QVERIFY(queue.started(*key,target.scope,1,9000));
    engine::MainlineDhtScrape result{1,lt::sha1_hash(raw.data()),services::mainline_scrape::Dialect::OriginalBFse,{4,7,0,1},false};
    queue.completed(1,result,target,120000,0);QCOMPARE(queue.estimates().size(),std::size_t{1});QCOMPARE(queue.estimates()[0].counts.peers,7);
    // The source agsc write throttle is not a result TTL. A zero round retains
    // the previous positive observation rather than fabricating fresh zero.
    QVERIFY(queue.due(1320000));QVERIFY(queue.started(*key,target.scope,2,12000));result.round=2;result.summary={0,0,0,1};queue.completed(2,result,target,1320000,0);
    QCOMPARE(queue.estimates().size(),std::size_t{1});QCOMPARE(queue.estimates()[0].round,std::uint64_t{1});
    QVERIFY(queue.reconcile(std::span(&target,1),99999999,0));QCOMPARE(queue.estimates().size(),std::size_t{1});
    QVERIFY(queue.started(*key,target.scope,3,13000));queue.revoke(*key);result.round=3;result.summary={2,1,0,1};queue.completed(3,result,target,99999999,0);QVERIFY(queue.estimates().empty());
    QVERIFY(queue.reconcile(std::span(&target,1),99999999,0));QVERIFY(!queue.due(INT64_MAX));
    target.scope=std::string(64,'b');QVERIFY(queue.reconcile(std::span(&target,1),99999999,0));QVERIFY(queue.due(INT64_MAX));
    engine::QueuedMainlineScrapes beforeLoad;beforeLoad.revoke(*key);QVERIFY(beforeLoad.reconcile(std::span(&target,1),0,0));QVERIFY(!beforeLoad.due(INT64_MAX));
    beforeLoad.settled(*key);QVERIFY(beforeLoad.reconcile(std::span(&target,1),0,0));QVERIFY(beforeLoad.due(60000));
  }
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
  void checkpointRecreatesActualLookupWithoutRoutersAndOptionallyRestoresWireIdentity() {
    QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost,0)); OwnedKrpc fixture(peer.serverPort());
    const auto firstHash=hash('R'),secondHash=hash('S'),freshHash=hash('T');
    fixture.allow(firstHash); fixture.allow(secondHash); fixture.allow(freshHash);
    auto opts=options(fixture,peer.serverPort()); opts.bootstrapNodes.clear();
    auto first=engine::MainlineDhtService::create(localPolicy(),opts); QVERIFY(first);
    const auto premature=(*first)->checkpoint(); QVERIFY(!premature); QCOMPARE(premature.error().code,storage::ErrorCode::Busy);
    // Configured routers are excluded from LT routing buckets. Add this actual
    // node, then prove a response before asking LT to save its real routing state.
    QVERIFY((*first)->addNode({"127.0.0.1",fixture.port()})); Observations firstObserved;
    QTRY_VERIFY_WITH_TIMEOUT((firstObserved.pump(**first),fixture.bootstrapQueries>0 || !fixture.failure.isEmpty() || !firstObserved.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(firstObserved.failure.isEmpty(),qPrintable(firstObserved.failure));
    QVERIFY((*first)->lookup(firstHash));
    QTRY_VERIFY_WITH_TIMEOUT((firstObserved.pump(**first),firstObserved.contains(firstHash,peer.serverPort()) || !fixture.failure.isEmpty() || !firstObserved.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(firstObserved.failure.isEmpty(),qPrintable(firstObserved.failure));
    QVERIFY(firstObserved.contains(firstHash,peer.serverPort())); QCOMPARE(fixture.clientIds(firstHash).size(),std::size_t{1});
    const auto originalId=*fixture.clientIds(firstHash).begin(); const auto saved=(*first)->checkpoint();
    QVERIFY2(saved,saved?"":saved.error().message.c_str()); QVERIFY(!saved->empty());
    const auto document=core::Document::parse(*saved); QVERIFY(document); QCOMPARE(document->root().children.size(),std::size_t{2});
    const auto* dht=field(document->root(),"dht state",core::Node::Type::Dictionary); QVERIFY(dht);
    const auto* ids=field(*dht,"node-id",core::Node::Type::List); QVERIFY(ids); QCOMPARE(ids->children.size(),std::size_t{1});
    QCOMPARE(bytes(ids->children.front().bytes).first(20),originalId);
    bool actualContact=false;
    for (const auto name:{"nodes","nodes6"}) {
      const auto* contacts=field(*dht,name,core::Node::Type::List); if (!contacts) { continue; }
      for (const auto& contact:contacts->children) { actualContact |= bytes(contact.bytes)==compact(fixture.port()); }
    }
    QVERIFY(actualContact); QVERIFY((*first)->close()); QVERIFY(!(*first)->checkpoint());
    opts.restoredState=*saved; // No bootstrap routers or explicit addNode on either restart.
    auto second=engine::MainlineDhtService::create(localPolicy(),opts); QVERIFY(second); QVERIFY((*second)->lookup(secondHash)); Observations secondObserved;
    QTRY_VERIFY_WITH_TIMEOUT((secondObserved.pump(**second),secondObserved.contains(secondHash,peer.serverPort()) || !fixture.failure.isEmpty() || !secondObserved.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(secondObserved.failure.isEmpty(),qPrintable(secondObserved.failure));
    QVERIFY(secondObserved.contains(secondHash,peer.serverPort())); QCOMPARE(fixture.clientIds(secondHash).size(),std::size_t{1});
    QCOMPARE(*fixture.clientIds(secondHash).begin(),originalId); QVERIFY((*second)->close());
    opts.restoreIdentity=false;
    auto fresh=engine::MainlineDhtService::create(localPolicy(),opts); QVERIFY(fresh); QVERIFY((*fresh)->lookup(freshHash)); Observations freshObserved;
    QTRY_VERIFY_WITH_TIMEOUT((freshObserved.pump(**fresh),freshObserved.contains(freshHash,peer.serverPort()) || !fixture.failure.isEmpty() || !freshObserved.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(freshObserved.failure.isEmpty(),qPrintable(freshObserved.failure));
    QVERIFY(freshObserved.contains(freshHash,peer.serverPort())); QCOMPARE(fixture.clientIds(freshHash).size(),std::size_t{1});
    QVERIFY(*fixture.clientIds(freshHash).begin()!=originalId); QVERIFY((*fresh)->close());
  }
  void injectedSettingsAndMalformedCheckpointCannotConstructAListener_data() {
    QTest::addColumn<QByteArray>("checkpoint");
    QTest::newRow("injected-root-settings")<<dictionary({{"dht state",dictionary({})},{"settings",dictionary({{"enable_dht","i1e"}})}});
    QTest::newRow("injected-nested-settings")<<dictionary({{"dht state",dictionary({{"settings",dictionary({})}})}});
    QTest::newRow("old-bare-node-id")<<dictionary({{"dht state",dictionary({{"node-id",string(QByteArray(20,'A'))}})}});
    QTest::newRow("zero-node-identity")<<dictionary({{"dht state",dictionary({{"node-id",list({string(QByteArray(20,'\0')+QByteArray::fromHex("7f000001"))})}})}});
    QTest::newRow("zero-contact-port")<<dictionary({{"dht state",dictionary({{"nodes",list({string(compact(0))})}})}});
    QTest::newRow("malformed-bencode")<<QByteArray("not-bencode");
  }
  void injectedSettingsAndMalformedCheckpointCannotConstructAListener() {
    QFETCH(QByteArray,checkpoint); auto opts=engine::MainlineDhtOptions{};
    opts.port=dhtPort(); opts.ipv6=false; opts.bootstrapNodes.clear(); opts.restoredState=checkpoint.toStdString();
    const auto refused=engine::MainlineDhtService::create(localPolicy(),opts); QVERIFY(!refused);
    QCOMPARE(refused.error().code,storage::ErrorCode::InvalidFormat);
    QUdpSocket unbound; QVERIFY(unbound.bind(QHostAddress::LocalHost,static_cast<quint16>(opts.port),QUdpSocket::DontShareAddress));
  }
  void untrustedLegacyHintFailuresCannotSuppressThePermittedOwnedNode() {
    QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost,0)); OwnedKrpc fixture(peer.serverPort());
    const auto wanted=hash('U'); fixture.allow(wanted); auto opts=options(fixture,peer.serverPort()); opts.bootstrapNodes.clear();
    opts.restoredNodes={{"non-numeric.invalid",6881},{"127.0.0.1",0},{"0.0.0.0",6881},
        {"224.0.0.1",6881},{"10.0.0.1",6881},{"::1",6881},{"127.0.0.1",fixture.port()}};
    auto actual=engine::MainlineDhtService::create(localPolicy(),opts); QVERIFY(actual); QVERIFY((*actual)->lookup(wanted)); Observations observed;
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(**actual),observed.contains(wanted,peer.serverPort()) || !fixture.failure.isEmpty() || !observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));
    QVERIFY(observed.contains(wanted,peer.serverPort())); QVERIFY(fixture.gets(wanted)>0);
    const auto saved=(*actual)->checkpoint(); QVERIFY(saved); const auto document=core::Document::parse(*saved); QVERIFY(document);
    const auto* dht=field(document->root(),"dht state",core::Node::Type::Dictionary); QVERIFY(dht); std::size_t contacts=0;
    for (const auto name:{"nodes","nodes6"}) {
      const auto* list=field(*dht,name,core::Node::Type::List); if (!list) { continue; }
      for (const auto& contact:list->children) { QCOMPARE(bytes(contact.bytes),compact(fixture.port())); ++contacts; }
    }
    QVERIFY(contacts>0); QVERIFY((*actual)->close());
  }
  void corruptDisposableStateDoesNotRerouteOrDisableTheOwnedTorrentSession() {
    Client client; auto cached=storage::MainlineCheckpoint::load(*client.lease,"mldht"); QVERIFY(cached);
    QVERIFY(cached->save(*client.lease,"not-dht"));
    const auto path=client.profile+"/BiglyBTC-mainline.config"; const auto before=test::read(path);
    auto policy=localPolicy(); policy.mainlinePort=dhtPort(); auto actual=owner();
    QVERIFY(actual->configureNetwork(policy)); QVERIFY(actual->configureMainlineBootstrap({})); QVERIFY(actual->startIdle(*client.lease));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value() && actual->tcpListenPort().has_value()),15000);
    const auto discovery=actual->mainlineDiscovery(); QCOMPARE(discovery.state,engine::MainlineDiscoveryStatus::State::Running);
    QVERIFY(!discovery.checkpointLoaded && discovery.checkpointFailure); QCOMPARE(discovery.legacyHints,std::size_t{0});
    const auto route=actual->liveSettings(); QVERIFY(route);
    QCOMPARE(route->listenInterfaces,std::string("127.0.0.1:0")); QCOMPARE(route->outgoingInterfaces,std::string("127.0.0.1"));
    QCOMPARE(route->proxyType,static_cast<int>(lt::settings_pack::none)); QVERIFY(!route->dht);
    QVERIFY(!QFileInfo::exists(client.payload)); QCOMPARE(test::read(path),before);
    const auto closed=actual->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.checkpointFailure);
    QVERIFY(actual->mainlineDiscovery().checkpointFailure); QVERIFY(!actual->mainlineDiscovery().checkpointSaved);
    QCOMPARE(test::read(path),before); QCOMPARE(test::read(client.sentinel),QByteArray("unchanged-owned-sentinel"));
  }
  void hashOnlyMagnetImportsMetadataFromOwnedMainlineResult() {
    test::OwnedEngineFixture seed(false); QTRY_VERIFY_WITH_TIMEOUT(seed.ready(),15000);
    const auto seedState=test::read(seed.stateFile); const auto seedInventory=test::read(seed.profile+"/downloads.config");
    std::vector<QByteArray> originalPayload; for (const auto& file:seed.files) { originalPayload.push_back(test::read(seed.payload+'/'+file.relative)); }
    OwnedKrpc fixture(seed.port()); Client client; auto policy=localPolicy(); policy.mainlinePort=dhtPort(seed.port());
    const auto originalRoot=client.profile+"/plugins/mldht/";
    const QByteArray originalId("# preserved original ID\n0123456789ABCDEF0123456789ABCDEF01234567\n");
    const auto original4=originalCache(fixture.port()),original6=originalCache(0);
    test::write(originalRoot+"baseID.config",originalId); test::write(originalRoot+"IPv4-table.cache",original4); test::write(originalRoot+"IPv6-table.cache",original6);
    auto parsed=engine::parseMagnetIntent("magnet:?xt=urn:btih:"+seed.hash->hex(),client.payload.toStdString(),true,policy);
    QVERIFY(parsed); QVERIFY(parsed->peers.empty()); QVERIFY(parsed->trackers.empty()); QVERIFY(parsed->webSeeds.empty()); QVERIFY(parsed->dhtNodes.empty());
    fixture.allow(parsed->requested.v1);
    auto queued=engine::queuePendingMagnet(*client.lease,*parsed,policy); QVERIFY(queued);
    auto actual=owner(); QVERIFY(actual->configureNetwork(policy));
    // No public/configured routers: the original-format owned contact is a
    // read-only hint. The restart below loads native state too; the dedicated
    // service case independently proves state-only routing without these hints.
    QVERIFY(actual->configureMainlineBootstrap({})); QVERIFY(actual->startIdle(*client.lease));
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
    QVERIFY(discovery.legacyHints>0);
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
    QVERIFY(actual->mainlineDiscovery().checkpointSaved);
    auto checkpoint=storage::MainlineCheckpoint::load(*client.lease,"mldht"); QVERIFY(checkpoint && checkpoint->state() && !checkpoint->state()->empty());
    test::OwnedEngineFixture secondSeed(true); QTRY_VERIFY_WITH_TIMEOUT(secondSeed.ready(),15000); QVERIFY(secondSeed.hash!=seed.hash);
    const auto secondState=test::read(secondSeed.stateFile),secondInventory=test::read(secondSeed.profile+"/downloads.config");
    fixture.peerPort(secondSeed.port());
    auto secondIntent=engine::parseMagnetIntent("magnet:?xt=urn:btih:"+secondSeed.hash->hex(),client.payload.toStdString(),true,policy);
    QVERIFY(secondIntent); QVERIFY(secondIntent->peers.empty() && secondIntent->trackers.empty() && secondIntent->webSeeds.empty() && secondIntent->dhtNodes.empty());
    fixture.allow(secondIntent->requested.v1);
    auto secondQueued=engine::queuePendingMagnet(*client.lease,*secondIntent,policy); QVERIFY(secondQueued);
    auto restarted=owner(7104); QVERIFY(restarted->configureNetwork(policy)); QVERIFY(restarted->configureMainlineBootstrap({}));
    QVERIFY(restarted->startIdle(*client.lease)); QVERIFY(restarted->mainlineDiscovery().checkpointLoaded);
    QVERIFY(restarted->startMagnet(*client.lease,*secondQueued,client.profile.toStdString()));
    const auto secondTerminal=[&] {
      if (!restarted->poll()) { pollFailure=QStringLiteral("Restarted persistent owner poll failed"); return true; }
      const auto current=pending(*restarted,*secondSeed.hash);
      return !fixture.failure.isEmpty() || (current && (current->phase==engine::MagnetPhase::Failed
          || (current->phase==engine::MagnetPhase::Imported && !current->retained)));
    };
    QTRY_VERIFY_WITH_TIMEOUT(secondTerminal(),45000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure)); QVERIFY2(pollFailure.isEmpty(),qPrintable(pollFailure));
    const auto secondRow=pending(*restarted,*secondSeed.hash); QVERIFY(secondRow);
    QVERIFY2(!secondRow->failure,secondRow->failure?secondRow->failure->message.c_str():"none"); QCOMPARE(secondRow->phase,engine::MagnetPhase::Imported);
    QVERIFY(!secondRow->retained && restarted->managedDownloads().empty()); QVERIFY(fixture.gets(secondIntent->requested.v1)>0);
    QCOMPARE(fixture.announced(secondIntent->requested.v1),0); QVERIFY(!QFileInfo::exists(client.payload));
    const auto secondClosed=restarted->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(secondClosed.sessionJoined && secondClosed.retirement.storageReleased && !secondClosed.checkpointFailure);
    QVERIFY(restarted->mainlineDiscovery().checkpointSaved);
    const auto both=core::Document::parse(test::read(client.profile+"/downloads.config").toStdString()); QVERIFY(both);
    const auto bothLibrary=core::readLibrary(*both); QVERIFY(bothLibrary && bothLibrary->issues.empty()); QCOMPARE(bothLibrary->downloads.size(),std::size_t{2});
    for (const auto& download:bothLibrary->downloads) { QCOMPARE(download.originalState,std::int64_t{70}); QVERIFY(!download.forceStart); }
    QCOMPARE(test::read(originalRoot+"baseID.config"),originalId); QCOMPARE(test::read(originalRoot+"IPv4-table.cache"),original4); QCOMPARE(test::read(originalRoot+"IPv6-table.cache"),original6);
    QCOMPARE(test::read(secondSeed.stateFile),secondState); QCOMPARE(test::read(secondSeed.profile+"/downloads.config"),secondInventory); QVERIFY(secondSeed.intact(false));
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
  void actualPeerPortBootstrapsHashOnlyMetadataAndAdvertisesSeparateListener(){
    test::OwnedEngineFixture source(false),metadata(true);QTRY_VERIFY_WITH_TIMEOUT(metadata.ready(),15000);
    const auto metadataBefore=test::read(metadata.stateFile),metadataInventoryBefore=test::read(metadata.profile+"/downloads.config");
    auto held=storage::ProfileLease::acquire(source.native());QVERIFY(held);
    auto policy=localPolicy();policy.mainlinePeerBootstrapOnly=true;policy.mainlinePort=dhtPort(metadata.port());
    OwnedKrpc fixture(metadata.port(),QHostAddress(QStringLiteral("127.0.0.2")));
    fixture.allowWithoutPeers(lt::sha1_hash(reinterpret_cast<const char*>(source.hash->bytes().data())));
    const auto destination=source.root.path()+"/metadata-only-payload";
    auto intent=engine::parseMagnetIntent("magnet:?xt=urn:btih:"+metadata.hash->hex(),destination.toStdString(),true,policy);QVERIFY(intent);
    QVERIFY(intent->peers.empty()&&intent->trackers.empty()&&intent->dhtNodes.empty()&&intent->webSeeds.empty());fixture.allow(intent->requested.v1);
    auto queued=engine::queuePendingMagnet(*held,*intent,policy);QVERIFY(queued);
    auto actual=owner(7201);QVERIFY(actual->configureNetwork(policy));QVERIFY(actual->configureMainlineBootstrap({}));QVERIFY(actual->startIdle(*held));
    QVERIFY(actual->startManaged(*held,{.hash=*source.hash,.forceStart=true}));
    QVERIFY(actual->startMagnet(*held,*queued,source.profile.toStdString()));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&actual->tcpListenPort()&&managed(*actual,*source.hash)
        &&managed(*actual,*source.hash)->phase==engine::ManagedPhase::Downloading&&actual->mainlineDiscovery().portBridgeAvailable),15000);
    const auto transfer=*actual->tcpListenPort();QVERIFY(transfer!=policy.mainlinePort);fixture.announcePort(transfer);
    QVERIFY(!actual->mainlineDiscovery().checkpointLoaded);QCOMPARE(actual->mainlineDiscovery().legacyHints,std::size_t{0});
    QCOMPARE(actual->mainlineDiscovery().portHintsAccepted,std::uint64_t{0});QCOMPARE(fixture.bootstrapQueries,0);
    PortPeer peer(transfer,*source.hash);
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&(!peer.advertised.empty()||!peer.failure.isEmpty())),15000);
    QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));QVERIFY(peer.ready()&&peer.dhtSupported);
    for(const auto port:peer.advertised){QCOMPARE(port,static_cast<unsigned short>(policy.mainlinePort));QVERIFY(port!=transfer);QVERIFY(port!=peer.port());}
    // The real connected source is127.0.0.2; the KRPC endpoint binds there.
    // A guessed127.0.0.1 source or a TCP-port substitution cannot make progress.
    peer.sendPort(0);peer.barrier();QTRY_VERIFY_WITH_TIMEOUT(peer.barriers==1||!peer.failure.isEmpty(),15000);
    QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));QVERIFY(actual->poll());
    QCOMPARE(actual->mainlineDiscovery().portHintsAccepted,std::uint64_t{0});QCOMPARE(fixture.bootstrapQueries,0);
    peer.sendPort(fixture.port());peer.barrier();QTRY_VERIFY_WITH_TIMEOUT(peer.barriers==2||!peer.failure.isEmpty(),15000);
    QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&(pending(*actual,*metadata.hash)
        &&pending(*actual,*metadata.hash)->phase==engine::MagnetPhase::Imported&&!pending(*actual,*metadata.hash)->retained)),45000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));const auto result=pending(*actual,*metadata.hash);QVERIFY(result&&!result->failure);
    const auto discovery=actual->mainlineDiscovery();QCOMPARE(discovery.portHintsAccepted,std::uint64_t{1});QVERIFY(discovery.portHintsObserved>=1);
    QVERIFY(!discovery.portHintsLost);QVERIFY(discovery.deliveredPeers>0);QVERIFY(fixture.gets(intent->requested.v1)>0);
    QCOMPARE(fixture.announced(intent->requested.v1),0);QCOMPARE(fixture.sourcePorts,std::set<unsigned short>{static_cast<unsigned short>(policy.mainlinePort)});
    QVERIFY(!QFileInfo::exists(destination));const auto saved=core::Document::parse(test::read(source.profile+"/downloads.config").toStdString());QVERIFY(saved);
    const auto library=core::readLibrary(*saved);QVERIFY(library&&library->issues.empty());
    const auto imported=std::ranges::find_if(library->downloads,[&](const auto& row){return row.hash==metadata.hash;});QVERIFY(imported!=library->downloads.end());
    QCOMPARE(imported->originalState,std::int64_t{70});QVERIFY(!imported->forceStart);
    const auto expected=core::Document::parse(metadataBefore.toStdString());QVERIFY(expected);
    const auto received=core::Document::parse(test::read(source.profile+"/active/"+QString::fromStdString(metadata.hash->hex())+".dat").toStdString());QVERIFY(received);
    const auto* expectedInfo=expected->find("info");const auto* receivedInfo=received->find("info");QVERIFY(expectedInfo&&receivedInfo);
    QCOMPARE(received->originalBytes().substr(receivedInfo->begin,receivedInfo->end-receivedInfo->begin),
        expected->originalBytes().substr(expectedInfo->begin,expectedInfo->end-expectedInfo->begin));
    const auto closed=actual->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
    QVERIFY(source.accountingIntact(false));QVERIFY(metadata.intact(false));
    QCOMPARE(test::read(metadata.stateFile),metadataBefore);QCOMPARE(test::read(metadata.profile+"/downloads.config"),metadataInventoryBefore);
  }
  void queuedPeerPortCannotSurviveStopAndReadmission(){
    test::OwnedEngineFixture source(false);auto held=storage::ProfileLease::acquire(source.native());QVERIFY(held);
    QTcpServer unused;QVERIFY(unused.listen(QHostAddress::LocalHost,0));OwnedKrpc fixture(unused.serverPort(),QHostAddress(QStringLiteral("127.0.0.2")));
    fixture.allowWithoutPeers(lt::sha1_hash(reinterpret_cast<const char*>(source.hash->bytes().data())));
    auto policy=localPolicy();policy.mainlinePeerBootstrapOnly=true;policy.mainlinePort=dhtPort();auto actual=owner(7202);
    QVERIFY(actual->configureNetwork(policy));QVERIFY(actual->configureMainlineBootstrap({}));QVERIFY(actual->startIdle(*held));QVERIFY(actual->startManaged(*held,{.hash=*source.hash,.forceStart=true}));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&actual->tcpListenPort()&&managed(*actual,*source.hash)
        &&managed(*actual,*source.hash)->phase==engine::ManagedPhase::Downloading&&actual->mainlineDiscovery().portBridgeAvailable),15000);
    fixture.announcePort(*actual->tcpListenPort());const auto old=managed(*actual,*source.hash)->admission;PortPeer peer(*actual->tcpListenPort(),*source.hash);
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&(!peer.advertised.empty()||!peer.failure.isEmpty())),15000);QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));
    peer.sendPort(fixture.port());peer.barrier();QTRY_VERIFY_WITH_TIMEOUT(peer.barriers==1||!peer.failure.isEmpty(),15000);QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));
    // No owner poll between the ordered wire barrier and manual retirement.
    QVERIFY(actual->stopManaged(*source.hash));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&managed(*actual,*source.hash)&&!managed(*actual,*source.hash)->retained),15000);
    QCOMPARE(actual->mainlineDiscovery().portHintsAccepted,std::uint64_t{0});QCOMPARE(fixture.bootstrapQueries,0);
    QVERIFY(actual->startManaged(*held,{.hash=*source.hash,.forceStart=true}));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&managed(*actual,*source.hash)&&managed(*actual,*source.hash)->phase==engine::ManagedPhase::Downloading),15000);
    QVERIFY(managed(*actual,*source.hash)->admission!=old);QCOMPARE(actual->mainlineDiscovery().portHintsAccepted,std::uint64_t{0});
    PortPeer current(*actual->tcpListenPort(),*source.hash);QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&(!current.advertised.empty()||!current.failure.isEmpty())),15000);
    QVERIFY2(current.failure.isEmpty(),qPrintable(current.failure));current.sendPort(fixture.port());current.barrier();
    QTRY_VERIFY_WITH_TIMEOUT(current.barriers==1||!current.failure.isEmpty(),15000);QVERIFY2(current.failure.isEmpty(),qPrintable(current.failure));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&(fixture.gets(lt::sha1_hash(reinterpret_cast<const char*>(source.hash->bytes().data())))>0||!fixture.failure.isEmpty())),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QCOMPARE(actual->mainlineDiscovery().portHintsAccepted,std::uint64_t{1});
    const auto closed=actual->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(source.accountingIntact(false));
  }
  void actualPortFloodWithdrawsAdmissionBeforeUnrelatedOwnerProgress(){
    test::OwnedEngineFixture source(false);auto held=storage::ProfileLease::acquire(source.native());QVERIFY(held);
    QTcpServer unused;QVERIFY(unused.listen(QHostAddress::LocalHost,0));OwnedKrpc fixture(unused.serverPort(),QHostAddress(QStringLiteral("127.0.0.2")));
    auto policy=localPolicy();policy.mainlinePeerBootstrapOnly=true;policy.mainlinePort=dhtPort();auto actual=owner(7203);
    QVERIFY(actual->configureNetwork(policy));QVERIFY(actual->configureMainlineBootstrap({}));QVERIFY(actual->startIdle(*held));QVERIFY(actual->startManaged(*held,{.hash=*source.hash,.forceStart=true}));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&actual->tcpListenPort()&&managed(*actual,*source.hash)
        &&managed(*actual,*source.hash)->phase==engine::ManagedPhase::Downloading&&actual->mainlineDiscovery().portBridgeAvailable),15000);
    PortPeer peer(*actual->tcpListenPort(),*source.hash);QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&(!peer.advertised.empty()||!peer.failure.isEmpty())),15000);QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));
    peer.sendPort(fixture.port(),65);peer.barrier();QTRY_VERIFY_WITH_TIMEOUT(peer.barriers==1||!peer.failure.isEmpty(),15000);QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));
    QVERIFY(actual->poll());const auto failed=actual->mainlineDiscovery();QVERIFY(failed.portHintsLost);QCOMPARE(failed.portHintsAccepted,std::uint64_t{0});
    QCOMPARE(failed.state,engine::MainlineDiscoveryStatus::State::Running);QCOMPARE(fixture.bootstrapQueries,0);QVERIFY(actual->tcpListenPort());
    // The original transfer listener and same owner remain live; a new real
    // handshake/FAST barrier proves withdrawal did not kill ordinary TCP.
    PortPeer after(*actual->tcpListenPort(),*source.hash,QHostAddress(QStringLiteral("127.0.0.3")));QTRY_VERIFY_WITH_TIMEOUT(after.ready()||!after.failure.isEmpty(),15000);
    QVERIFY2(after.failure.isEmpty(),qPrintable(after.failure));after.barrier();QTRY_VERIFY_WITH_TIMEOUT(after.barriers==1||!after.failure.isEmpty(),15000);
    QVERIFY2(after.failure.isEmpty(),qPrintable(after.failure));QVERIFY(after.advertised.empty());QVERIFY(actual->poll());QVERIFY(actual->mainlineDiscovery().portHintsLost);
    const auto closed=actual->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(source.accountingIntact(false));
  }
  void deniedSourceCannotAdvertiseOrBootstrapPort_data(){
    QTest::addColumn<QByteArray>("key");QTest::addColumn<QByteArray>("value");
    QTest::newRow("source-disabled")<<QByteArray("peersources")<<QByteArray("l7:Trackere");
    QTest::newRow("source-denied")<<QByteArray("peersourcesdenied")<<QByteArray("l3:DHTe");
    QTest::newRow("private")<<QByteArray{}<<QByteArray{};
  }
  void deniedSourceCannotAdvertiseOrBootstrapPort(){
    QFETCH(QByteArray,key);QFETCH(QByteArray,value);test::OwnedEngineFixture source(false);
    auto doc=core::Document::parse(test::read(source.stateFile).toStdString());QVERIFY(doc);
    if(key.isEmpty()){
      const auto* info=doc->find("info");QVERIFY(info);auto encoded=core::encodeValue(*info);QVERIFY(encoded);auto changed=core::Document::parse(*encoded);QVERIFY(changed);
      QVERIFY(changed->replaceValue("private","i1e"));QVERIFY(doc->replaceValue("info",changed->serialize()));
      const auto raw=QCryptographicHash::hash(QByteArray::fromStdString(changed->serialize()),QCryptographicHash::Sha1);
      const auto parsed=core::TorrentHash::parse(std::string_view(raw.constData(),static_cast<std::size_t>(raw.size())));QVERIFY(parsed);
      const auto oldFile=source.stateFile;source.hash=*parsed;source.stateFile=source.profile+"/active/"+QString::fromStdString(parsed->hex())+".dat";
      test::write(source.stateFile,QByteArray::fromStdString(doc->serialize()));QVERIFY(QFile::remove(oldFile));
      auto inventory=core::Document::parse(test::read(source.profile+"/downloads.config").toStdString());QVERIFY(inventory);
      const auto* rows=inventory->find("downloads");QVERIFY(rows&&rows->children.size()==1);auto rowBytes=core::encodeValue(rows->children[0]);QVERIFY(rowBytes);auto row=core::Document::parse(*rowBytes);QVERIFY(row);
      QVERIFY(row->replaceValue("torrent_hash",string(raw).toStdString()));QVERIFY(row->replaceValue("torrent",string(source.stateFile.toUtf8()).toStdString()));
      QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(source.profile+"/downloads.config",QByteArray::fromStdString(inventory->serialize()));
    }else{
      const auto* attrs=doc->find("attributes");QVERIFY(attrs);auto encoded=core::encodeValue(*attrs);QVERIFY(encoded);auto changed=core::Document::parse(*encoded);QVERIFY(changed);
      QVERIFY(changed->replaceValue(key.toStdString(),value.toStdString()));QVERIFY(doc->replaceValue("attributes",changed->serialize()));test::write(source.stateFile,QByteArray::fromStdString(doc->serialize()));
    }
    auto held=storage::ProfileLease::acquire(source.native());QVERIFY(held);auto policy=localPolicy();policy.mainlinePeerBootstrapOnly=true;policy.mainlinePort=dhtPort();auto actual=owner(7204);
    QVERIFY(actual->configureNetwork(policy));QVERIFY(actual->configureMainlineBootstrap({}));QVERIFY(actual->startIdle(*held));QVERIFY(actual->startManaged(*held,{.hash=*source.hash,.forceStart=true}));
    QTRY_VERIFY_WITH_TIMEOUT((actual->poll().has_value()&&actual->tcpListenPort()&&managed(*actual,*source.hash)
        &&managed(*actual,*source.hash)->phase==engine::ManagedPhase::Downloading),15000);
    QVERIFY(!actual->mainlineDiscovery().portBridgeAvailable);
    PortPeer peer(*actual->tcpListenPort(),*source.hash);QTRY_VERIFY_WITH_TIMEOUT(peer.ready()||!peer.failure.isEmpty(),15000);QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));QUdpSocket trap;QVERIFY(trap.bind(QHostAddress(QStringLiteral("127.0.0.2")),0,QUdpSocket::DontShareAddress));
    peer.sendPort(trap.localPort());peer.barrier();QTRY_VERIFY_WITH_TIMEOUT(peer.barriers==1||!peer.failure.isEmpty(),15000);QVERIFY2(peer.failure.isEmpty(),qPrintable(peer.failure));QVERIFY(actual->poll());
    QCOMPARE(actual->mainlineDiscovery().portHintsAccepted,std::uint64_t{0});QVERIFY(peer.advertised.empty());QCOMPARE(actual->mainlineDiscovery().lookups,std::uint64_t{0});
    const auto closed=actual->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(source.accountingIntact(false));
  }
  void peerProbeRequiresRealReplyAndActiveRoutingBeforeProgress(){
    QTcpServer peer;QVERIFY(peer.listen(QHostAddress::LocalHost,0));OwnedKrpc fixture(peer.serverPort());const auto wanted=hash('P');fixture.allow(wanted);fixture.holdPings(true);
    auto opts=options(fixture,peer.serverPort());opts.bootstrapNodes.clear();opts.onlyPeerBootstrap=true;auto actual=engine::MainlineDhtService::create(localPolicy(),opts);QVERIFY(actual);
    QVERIFY((*actual)->lookup(wanted));QVERIFY((*actual)->addPeerNode({"127.0.0.1",fixture.port()}));QCOMPARE((*actual)->status().peerBootstrapProgress,std::uint64_t{0});Observations observed;
    QTRY_VERIFY_WITH_TIMEOUT((observed.pump(**actual),fixture.bootstrapQueries>0||!fixture.failure.isEmpty()||!observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QCOMPARE((*actual)->status().peerBootstrapProgress,std::uint64_t{0});
    fixture.releasePings();QTRY_VERIFY_WITH_TIMEOUT((observed.pump(**actual),(*actual)->status().peerBootstrapProgress==1||!fixture.failure.isEmpty()||!observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QCOMPARE((*actual)->status().peerBootstrapProgress,std::uint64_t{1});
    QVERIFY(!(*actual)->addPeerNode({"127.0.0.1",fixture.port()}));QVERIFY(!(*actual)->addPeerNode({"not-numeric.invalid",1}));QVERIFY(!(*actual)->addPeerNode({"10.0.0.1",1}));
    QVERIFY((*actual)->lookup(wanted));QTRY_VERIFY_WITH_TIMEOUT((observed.pump(**actual),observed.contains(wanted,peer.serverPort())||!fixture.failure.isEmpty()||!observed.failure.isEmpty()),15000);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QVERIFY2(observed.failure.isEmpty(),qPrintable(observed.failure));QVERIFY(observed.contains(wanted,peer.serverPort()));QCOMPARE((*actual)->status().peerBootstrapProgress,std::uint64_t{1});QVERIFY((*actual)->close());
  }
  void peerProbeLifetimeBoundDoesNotIssueUnownedRequests(){
    auto opts=engine::MainlineDhtOptions{};opts.port=dhtPort();opts.ipv6=false;opts.bootstrapNodes.clear();opts.onlyPeerBootstrap=true;
    auto actual=engine::MainlineDhtService::create(localPolicy(),opts);QVERIFY(actual);
    std::vector<std::unique_ptr<QUdpSocket>> owned;
    for(std::size_t i=0;i<engine::MainlineDhtService::MaximumNodes;++i){
      auto socket=std::make_unique<QUdpSocket>();QVERIFY(socket->bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress));
      QVERIFY((*actual)->addPeerNode({"127.0.0.1",socket->localPort()}));owned.push_back(std::move(socket));
    }
    QUdpSocket extra;QVERIFY(extra.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress));
    const auto refused=(*actual)->addPeerNode({"127.0.0.1",extra.localPort()});QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Busy);
    QCOMPARE((*actual)->status().peerBootstrapProgress,std::uint64_t{0});QVERIFY((*actual)->close());
    // No poll/dispatch occurred: all64 bounded registrations retire without
    // issuing a UDP request, and all numeric endpoints stayed owned throughout.
    for(const auto& socket:owned){QVERIFY(!socket->hasPendingDatagrams());}
  }
};
QTEST_GUILESS_MAIN(MainlineDhtTests)
#include "mainline_dht_tests.moc"
