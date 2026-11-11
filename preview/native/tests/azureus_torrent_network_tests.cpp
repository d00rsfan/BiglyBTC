// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_service.hpp"
#include "persistent_session.hpp"
#include "azureus_contacts.hpp"
#include "original_torrent_identity_lt.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <set>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTest>
#include <QProcess>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include "discovery.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>
using namespace biglybtc;using namespace std::chrono_literals;
namespace {
namespace udp=services::azureus_udp;namespace nodes=services::azureus_nodes;namespace lt=libtorrent;
void ensure(bool okay,const char* why){if(!okay){throw std::runtime_error(why);}}
std::string encoded(const lt::entry& entry){std::string out;lt::bencode(std::back_inserter(out),entry);return out;}
void write(const QString& path,const std::string& bytes){ensure(QDir().mkpath(QFileInfo(path).absolutePath()),"Generated parent");QFile file(path);ensure(file.open(QIODevice::WriteOnly)&&file.write(bytes.data(),bytes.size())==static_cast<qint64>(bytes.size()),"Generated write");}
std::string read(const QString& path){QFile file(path);ensure(file.open(QIODevice::ReadOnly),"Generated read");return file.readAll().toStdString();}
struct Wall final:services::azureus_db_value::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}};
nodes::Contact contact(udp::Peer peer){nodes::ResolvedEndpoint endpoint;endpoint.address.port=peer.port;endpoint.address.length=peer.address.protocol()==QAbstractSocket::IPv4Protocol?4:16;
 if(endpoint.address.length==4){const auto ip=peer.address.toIPv4Address();for(int i=0;i<4;++i){endpoint.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}else{const auto ip=peer.address.toIPv6Address();std::copy_n(ip.c,16,endpoint.address.bytes.begin());}
 nodes::NodeCache cache;const auto resolved=cache.lookup(endpoint,55);ensure(bool(resolved),"Owned identity");nodes::Contact out;out.address=endpoint.address;out.protocol=55;out.id=resolved->id;return out;
}
class ShardedDht final:public QObject {
 QUdpSocket socket_;services::azureus_nodes::NodeCache cache_;
public:
 services::msgsync::DiscoveryKey base{};std::optional<services::azureus_values::Value> member;
 unsigned baseReads=0,shardReads=0,stores=0,rootStores=0;std::int8_t type=2;std::set<std::string> storedKeys;
 ShardedDht(){if(!socket_.bind(QHostAddress("127.122.31.1"),0)){throw std::runtime_error("Owned sharded seed");}connect(&socket_,&QUdpSocket::readyRead,this,[this]{drain();});}
 udp::Peer endpoint()const{return {socket_.localAddress(),socket_.localPort()};}
 void drain(){while(socket_.hasPendingDatagrams()){
  auto datagram=socket_.receiveDatagram();const std::string bytes=datagram.data().toStdString();services::azureus_dht::Context context;context.network=0;context.localVersion=55;context.now=QDateTime::currentMSecsSinceEpoch();auto header=services::azureus_dht::decodeHeader(services::azureus_dht::Kind::Request,bytes,context);if(!header){continue;}const auto& request=header->fields();services::azureus_dht::EncodeFields reply;reply.kind=services::azureus_dht::Kind::Reply;reply.network=0;reply.localVersion=55;reply.remoteVersion=request.originatorVersion;reply.connection=request.connection;reply.transaction=request.transaction;reply.now=context.now;reply.receivedAt=context.now;reply.instance=42;
  std::optional<std::string> wire;
  if(request.action==1028){reply.action=1029;udp::find::ReplyFields fields;fields.header=reply;fields.randomId=123;const std::vector<std::optional<services::azureus_positions::WirePosition>> positions;fields.positions=positions;const std::vector<services::azureus_nodes::Snapshot> contacts;fields.contacts=contacts;auto encoded=udp::find::encodeReply(fields);if(encoded){wire=std::move(*encoded);}}
  else if(request.action==1030){udp::lookup::Context query;query.header=context;auto decoded=udp::lookup::decode(services::azureus_dht::Kind::Request,bytes,query,cache_);if(!decoded){continue;}const auto* body=std::get_if<udp::lookup::RequestBody>(&decoded->body());if(!body){continue;}reply.action=1031;udp::lookup::ReplyFields fields;fields.header=reply;const std::string target=body->target,root(reinterpret_cast<const char*>(base.data()),base.size());std::vector<std::optional<services::azureus_values::Snapshot>> values;
   if(target==root){++baseReads;fields.diversification=type;}else{for(unsigned i=0;i<10;++i){auto key=services::msgsync::diversifiedDiscoveryKey(base,i);if(key&&target==std::string(reinterpret_cast<const char*>(key->data()),key->size())){++shardReads;if(member){values.push_back(services::azureus_values::Snapshot{member->version,member->created,member->bytes,services::azureus_nodes::Snapshot{true,member->originator.protocol,member->originator.address},member->flags,0,-1});}break;}}}fields.values=std::span<const std::optional<services::azureus_values::Snapshot>>(values);auto encoded=udp::lookup::encodeReply(fields);if(encoded){wire=std::move(*encoded);}
  }else if(request.action==1026){++stores;udp::storage::Context input;input.header=context;auto decoded=udp::storage::decode(services::azureus_dht::Kind::Request,bytes,input,cache_);if(!decoded){continue;}const auto* body=std::get_if<udp::storage::RequestBody>(&decoded->body());if(!body){continue;}for(const auto& key:body->keys){if(key==std::string(reinterpret_cast<const char*>(base.data()),20)){++rootStores;}else{storedKeys.insert(key);}}reply.action=1027;udp::storage::ReplyFields fields;fields.header=reply;const std::string types(body->keys.size(),'\1');fields.diversification=types;const auto encoded=udp::storage::encodeReply(fields);if(encoded){wire=std::move(*encoded);}}
  if(wire){socket_.writeDatagram(QByteArray::fromStdString(*wire),datagram.senderAddress(),datagram.senderPort());}
 }}
};

struct Seed {
 std::int32_t network=0;std::unique_ptr<udp::OwnedPingTransport> actor;std::shared_ptr<services::azureus_db_runtime::Owner> database;
 std::unique_ptr<QProcess> java;QTemporaryDir javaRoot;udp::Peer javaPeer;std::optional<udp::Peer> fixtureEndpoint;
 udp::Peer endpoint()const{return fixtureEndpoint?*fixtureEndpoint:actor?actor->endpoint():javaPeer;}
 ~Seed(){if(java){java->write("EXIT\n");if(!java->waitForFinished(2500)){java->kill();java->waitForFinished();}}}
 Seed(std::int32_t number,bool ipv6,const QString& command,const QString& classpath):network(number){
  QUdpSocket reserve;ensure(reserve.bind(ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0),"Reserve Java port");javaPeer={ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,reserve.localPort()};reserve.close();
  java=std::make_unique<QProcess>();java->setProcessChannelMode(QProcess::SeparateChannels);java->start(command,{"--add-opens=java.base/java.net=ALL-UNNAMED","-cp",classpath,"AzureusTorrentPeer",javaRoot.path(),QString::number(network),QString::number(javaPeer.port)});ensure(java->waitForStarted(),"Original Java started");bool ready=false;QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<10000&&java->state()!=QProcess::NotRunning){java->waitForReadyRead(100);while(java->canReadLine()){if(java->readLine().startsWith("READY ")){ready=true;break;}}if(ready){break;}}if(!ready){qWarning().noquote()<<java->readAllStandardError();}ensure(ready,"Original Java UDP ready");
 }
 explicit Seed(std::int32_t number,bool ipv6):network(number){udp::Config config;config.network=network;config.servePings=true;config.routingScope=udp::RoutingScope::OwnedLoopback;config.maximumPeers=256;
  actor=std::make_unique<udp::OwnedPingTransport>(std::move(config));ensure(actor->start(ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost).has_value(),"Owned router socket");
  const auto local=contact(actor->endpoint());auto router=services::azureus_router::RouterOwner::create(local.id);auto db=services::azureus_db_runtime::Owner::create();ensure(bool(router)&&bool(db),"Owned router/database");database=std::move(*db);ensure(database->bindLocal(local).has_value(),"Owned DB origin");
  ensure(actor->serveDatabase(std::move(*router),database,[]{return true;},[](auto done){done(true);return true;}).has_value(),"Owned DB service");
 }
 bool has(const nodes::NodeId& key,quint16 tcp){if(java){const auto hex=QByteArray(reinterpret_cast<const char*>(key.data()),20).toHex();java->write("HAS "+hex+" "+QByteArray::number(tcp)+"\n");java->waitForBytesWritten(1000);if(!java->canReadLine()){java->waitForReadyRead(1000);}return java->readLine().trimmed()=="HAS 1";}Wall clock;const auto found=database->read(std::string(reinterpret_cast<const char*>(key.data()),20),contact(actor->endpoint()),64,0,clock);if(!found||!*found){return false;}
  for(const auto& value:(*found)->values){ensure(value.bytes.find("generated-secret")==std::string::npos&&value.bytes.find("http")==std::string::npos,"Tracker credentials never enter DHT values");if(value.bytes==std::to_string(tcp)||value.bytes.starts_with(std::to_string(tcp)+";")){return true;}}return false;
 }
};
struct Client {
 QTemporaryDir root{QStringLiteral("/tmp/biglybtc-azureus-engine-XXXXXX")};QString profile,payload,state;
 std::optional<core::TorrentHash> hash;storage::OriginalFullInfoHashes full;QByteArray body;
 std::unique_ptr<QTcpServer> reserveTcp;std::unique_ptr<QUdpSocket> reserveUdp;quint16 port=0;bool v6=false,crypto=false;
 std::optional<storage::ProfileLease> lease;std::unique_ptr<engine::PersistentSession> engine;std::unique_ptr<app::GeneralChats> chats;
 Client(bool complete,bool ipv6,bool encrypted,std::set<unsigned> avoid,bool privateTorrent=false):v6(ipv6),crypto(encrypted){ensure(root.isValid(),"Generated root");profile=root.path()+"/profile";payload=root.path()+"/payload";ensure(QDir().mkpath(profile+"/active"),"Generated profile");
  for(int tries=0;tries<100;++tries){reserveTcp=std::make_unique<QTcpServer>();const auto address=v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost;ensure(reserveTcp->listen(address,0),"Reserve TCP");port=reserveTcp->serverPort();if(avoid.contains(port%(v6?8:5))){continue;}reserveUdp=std::make_unique<QUdpSocket>();if(reserveUdp->bind(address,port)){break;}reserveUdp.reset();}ensure(reserveUdp&&reserveUdp->state()==QAbstractSocket::BoundState,"Reserve actual UDP");
  body=QByteArray(131072,'\0');for(qsizetype n=0;n<body.size();++n){body[n]=static_cast<char>((n*17+31)%251);}std::string pieces;for(int at=0;at<body.size();at+=16384){pieces+=QCryptographicHash::hash(body.mid(at,16384),QCryptographicHash::Sha1).toStdString();}
  lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]["name"]="generated.bin";torrent["info"]["length"]=body.size();torrent["info"]["piece length"]=16384;torrent["info"]["pieces"]=pieces;
  if(privateTorrent){torrent["info"]["private"]=1;}torrent["announce"]="https://tracker.invalid/announce?passkey=generated-secret";torrent["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public")};torrent["attributes"]["peersources"]=lt::entry::list_type{lt::entry("DHT")};torrent["attributes"]["storetypes"]=lt::entry::list_type{lt::entry("L")};torrent["attributes"]["pluginoptions"]["mldht"]["enableannounce"]=0;torrent["azureus_properties"]["dht_backup_enable"]=1;
  torrent["resume"]["data"]["valid"]=1;torrent["resume"]["data"]["blocks"]=lt::entry::dictionary_type{};torrent["resume"]["data"]["resume data"]=std::string(complete?8:4,'\1')+std::string(complete?0:4,'\0');
  auto document=core::Document::parse(encoded(torrent));lt::error_code error;const auto raw=encoded(torrent);lt::torrent_info info(lt::span<const char>(raw.data(),raw.size()),error,lt::from_span);ensure(document&&!error,"Generated metadata");const auto identity=storage::originalTorrentIdentityFromLibtorrent(*document,info);ensure(bool(identity),"Generated identity");hash=identity->originalKey;full=identity->full;
  write(payload+"/generated.bin",(complete?body:body.first(65536)+QByteArray(65536,'\0')).toStdString());state=profile+"/active/"+QString::fromStdString(hash->hex())+".dat";write(state,raw);
  lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=std::string(reinterpret_cast<const char*>(hash->bytes().data()),hash->bytes().size());row["torrent"]=state.toStdString();row["save_dir"]=payload.toStdString();row["save_file"]="generated.bin";row["state"]=0;row["persistent"]=1;row["forceStart"]=1;row["position"]=1;row["downloaded"]=complete?131072:65536;row["uploaded"]=0;
  lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{row};write(profile+"/downloads.config",encoded(inventory));write(profile+"/tables.config","de");
  lt::entry config(lt::entry::dictionary_type{});config["TCP.Listen.Port"]=port;config["UDP.Listen.Port"]=port;config["UDP.NonData.Listen.Port.Same"]=1;config["IPV6 Enable Support"]=v6?1:0;config["Plugin.DHT.dht.enabled"]=1;config["Ip Filter Enabled"]=0;config["network.transport.encrypted.require"]=crypto?1:0;config["Plugin.DHT Tracker.dhttracker.tracknormalwhenoffline"]=0;write(profile+"/biglybt.config",encoded(config));
  auto held=storage::ProfileLease::acquire(profile.toStdString());ensure(bool(held),"Generated lease");lease=std::move(*held);
 }
 ~Client(){try{close();}catch(...){}}
 void open(){reserveTcp.reset();reserveUdp.reset();auto created=engine::PersistentSession::create({101,202,303});ensure(bool(created),"Current SDK owner");engine=std::move(*created);engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;policy.ipv6=v6;policy.bindAddresses={v6?"::1":"127.0.0.1"};policy.listenPort=port;policy.requireEncryption=crypto;policy.incomingUtp=true;policy.outgoingUtp=true;ensure(engine->configureNetwork(policy).has_value()&&engine->startIdle(*lease).has_value(),"Owned normal engine");auto started=engine->startManaged(*lease,{*hash,0,true,false});ensure(bool(started),started?"":started.error().message.c_str());}
 bool ready(){if(!engine||!engine->poll()){return false;}auto status=engine->managedDownloads();return status.size()==1&&status[0].checked&&(status[0].phase==engine::ManagedPhase::Seeding||status[0].phase==engine::ManagedPhase::Downloading);}
 void network(std::span<Seed*> seeds){for(auto* seed:seeds){const auto peer=seed->endpoint();const auto c=contact(peer);auto contacts=storage::AzureusContacts::open(*lease,seed->network,c.address.length,storage::AzureusContactScope::OwnedLoopback);ensure(bool(contacts),"Original seed cache");const storage::AzureusSavedContact saved{1000,55,c.address.length,c.address.bytes,c.address.port};ensure((*contacts)->save(std::span(&saved,1)).has_value(),"Original contact record");}
  auto factory=engine->chatTransport(*lease,true);ensure(bool(factory),factory?"":factory.error().message.c_str());auto opened=app::GeneralChats::create(std::move(*factory),99);ensure(bool(opened),"Shared Public worker");chats=std::move(*opened);pump();}
 void pump(){if(!engine){return;}static_cast<void>(engine->poll());if(chats){const auto peers=chats->takeTorrentPeers();if(!peers.empty()){const auto accepted=engine->acceptAzureusPeers(peers);ensure(bool(accepted),"Fresh peer delivery");chats->recordDeliveredTorrentPeers(*accepted);}chats->updateTorrents(engine->azureusTorrentTargets());}}
 bool complete(){pump();const auto status=engine->managedDownloads();return status.size()==1&&status[0].complete;}
 void close(){if(chats){chats->requestStop();chats->join();chats.reset();}if(engine){const auto result=engine->close(std::chrono::steady_clock::now()+5s);ensure(result.sessionJoined&&result.retirement.storageReleased,"Joined engine/storage");engine.reset();}}
 nodes::NodeId key()const{const auto raw=hash->bytes();const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(raw.data()),20),QCryptographicHash::Sha1);nodes::NodeId out{};std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data()),20,out.begin());return out;}
};
}
class AzureusTorrentNetworkTests final:public QObject {
 Q_OBJECT
private slots:
 void actualDhtAloneDiscoversAndTransfers_data(){QTest::addColumn<bool>("v6");QTest::addColumn<bool>("crypto");QTest::addColumn<bool>("original");QTest::newRow("IPv4")<<false<<false<<false;QTest::newRow("IPv6")<<true<<false<<false;QTest::newRow("IPv4-crypto")<<false<<true<<false;
  if(qEnvironmentVariableIsSet("BIGLYBTC_TORRENT_DHT_JAVA")&&qEnvironmentVariableIsSet("BIGLYBTC_TORRENT_DHT_CLASSPATH")){QTest::newRow("Original-IPv4")<<false<<false<<true;QTest::newRow("Original-IPv6")<<true<<false<<true;QTest::newRow("Original-BiglyBT4")<<false<<false<<true;}}
 void actualDhtAloneDiscoversAndTransfers(){
  QFETCH(bool,v6);QFETCH(bool,crypto);QFETCH(bool,original);const auto number=QByteArray(QTest::currentDataTag())=="Original-BiglyBT4"?4:(v6?3:0);
  std::unique_ptr<Seed> owner;if(original){owner=std::make_unique<Seed>(number,v6,qEnvironmentVariable("BIGLYBTC_TORRENT_DHT_JAVA"),qEnvironmentVariable("BIGLYBTC_TORRENT_DHT_CLASSPATH"));}else{owner=std::make_unique<Seed>(number,v6);}auto& seed=*owner;std::unique_ptr<Seed> bigly;if(!v6&&!original){bigly=std::make_unique<Seed>(4,false);}std::vector<Seed*> seeds{&seed};if(bigly){seeds.push_back(bigly.get());}
  std::set<unsigned> avoided;for(const auto* node:seeds){avoided.insert(node->endpoint().port%(v6?8:5));}Client a(true,v6,crypto,avoided);a.open();QTRY_VERIFY_WITH_TIMEOUT(a.ready(),15000);a.network(seeds);
  for(auto* node:seeds){if(!node->actor){continue;}QVERIFY(node->actor->attachKnownPeer({v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,a.port},55,std::chrono::steady_clock::now()+15s));static_cast<void>(node->actor->integrateKnownRouting(5s));}
  QTRY_VERIFY_WITH_TIMEOUT((a.pump(),seed.has(a.key(),a.port)),20000);if(bigly){QTRY_VERIFY_WITH_TIMEOUT((a.pump(),bigly->has(a.key(),a.port)),20000);}
  avoided.insert(a.port%(v6?8:5));Client b(false,v6,crypto,avoided);QVERIFY(a.hash==b.hash);b.open();QTRY_VERIFY_WITH_TIMEOUT(b.ready(),15000);b.network(seeds);
  for(auto* node:seeds){if(!node->actor){continue;}QVERIFY(node->actor->attachKnownPeer({v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,b.port},55,std::chrono::steady_clock::now()+15s));static_cast<void>(node->actor->integrateKnownRouting(5s));}
  QTRY_VERIFY_WITH_TIMEOUT((a.pump(),b.complete()),30000);QCOMPARE(read(b.payload+"/generated.bin"),b.body.toStdString());QVERIFY(b.chats->snapshot()->torrentDht.discoveredPeers>0);QTRY_VERIFY_WITH_TIMEOUT((b.pump(),b.chats->snapshot()->torrentDht.deliveredPeers>0),3000);
  if(!v6&&!crypto&&!original){
   QTest::qWait(2700);a.pump();a.chats->requestStop();a.chats->join();QVERIFY(a.chats->snapshot()->torrentDht.contactSaves>0);a.chats.reset();
   a.network({});QTRY_VERIFY_WITH_TIMEOUT((a.pump(),a.chats->snapshot()->torrentDht.publications>0),10000);
  }
  const auto old=a.engine->azureusTorrentTargets();QVERIFY(!old.empty());QVERIFY(a.engine->pauseManaged(*a.hash,true));QVERIFY(!old[0].live->load());a.pump();
  QTRY_VERIFY_WITH_TIMEOUT((a.pump(),!seed.has(a.key(),a.port)),15000);
  const auto profile=a.profile;a.close();b.close();a.lease.reset();b.lease.reset();auto held=storage::ProfileLease::acquire(profile.toStdString());QVERIFY(held); // All worker aliases released before the next client.
 }
 void frequencyAndSizeDiversificationDiscoverAndPublish_data(){QTest::addColumn<int>("type");QTest::newRow("Frequency")<<2;QTest::newRow("Size")<<3;}
 void frequencyAndSizeDiversificationDiscoverAndPublish(){
  QFETCH(int,type);ShardedDht sharded;sharded.type=type;Seed seed(0,false);seed.fixtureEndpoint=sharded.endpoint();Client receiver(false,false,false,{});receiver.open();QTRY_VERIFY_WITH_TIMEOUT(receiver.ready(),15000);Client publisher(true,false,false,{unsigned(receiver.port%5)});publisher.open();QTRY_VERIFY_WITH_TIMEOUT(publisher.ready(),15000);QCOMPARE(*receiver.hash,*publisher.hash);
  sharded.base=publisher.key();sharded.member=services::azureus_values::Value{1,QDateTime::currentMSecsSinceEpoch(),std::to_string(receiver.port),contact({QHostAddress::LocalHost,receiver.port}),1,0,-1};std::array<Seed*,1> seeds{&seed};publisher.network(seeds);
  QTimer monitor;connect(&monitor,&QTimer::timeout,this,[&]{const auto f=publisher.chats->snapshot()->torrentDht;qInfo("fork root=%u shard=%u stores=%u keys=%zu lookups=%llu pubs=%llu peers=%llu delivered=%llu pending=%zu",sharded.baseReads,sharded.shardReads,sharded.stores,sharded.storedKeys.size(),(unsigned long long)f.lookups,(unsigned long long)f.publications,(unsigned long long)f.discoveredPeers,(unsigned long long)f.deliveredPeers,f.pending);});if(qEnvironmentVariableIsSet("BIGLYBTC_TORRENT_TRACE")){monitor.start(1000);}
  QTRY_VERIFY_WITH_TIMEOUT((publisher.pump(),receiver.complete()),15000);QCOMPARE(read(receiver.payload+"/generated.bin"),receiver.body.toStdString());
  QTRY_COMPARE_WITH_TIMEOUT((publisher.pump(),sharded.storedKeys.size()),std::size_t(type==2?10:2),15000);QVERIFY(sharded.baseReads>0&&sharded.shardReads>0);QCOMPARE(sharded.rootStores,0u);QVERIFY(publisher.chats->snapshot()->torrentDht.discoveredPeers>0);publisher.close();receiver.close();
 }

 void privateTorrentCannotSendEvenWithAForgedActiveTicket(){
  Seed seed(0,false);Client a(true,false,false,{unsigned(seed.endpoint().port%5)},true);a.open();QTRY_VERIFY_WITH_TIMEOUT(a.ready(),15000);std::array<Seed*,1> seeds{&seed};a.network(seeds);
  QTRY_VERIFY_WITH_TIMEOUT(a.chats->snapshot()->torrentDht.readyNetworks>0,5000);QVERIFY(a.engine->azureusTorrentTargets().empty());const auto before=seed.database->snapshot();QVERIFY(before);
  engine::AzureusTorrentTarget forged{*a.hash,a.full,1,a.port,a.port,true,false,false,{},0,std::make_shared<std::atomic<bool>>(true)};
  QVERIFY(a.chats->updateTorrents({forged}));QTest::qWait(2200);const auto after=seed.database->snapshot();QVERIFY(after);QCOMPARE(after->reads,before->reads);QCOMPARE(after->stores,before->stores);QCOMPARE(a.chats->snapshot()->torrentDht.lookups,std::uint64_t(0));QVERIFY(!seed.has(a.key(),a.port));a.close();
 }
 void publishedValueIsNotServedAfterTheSourceWithdrawsDhtPermission(){
  Seed seed(0,false);Client a(true,false,false,{unsigned(seed.endpoint().port%5)});a.open();QTRY_VERIFY_WITH_TIMEOUT(a.ready(),15000);std::array<Seed*,1> seeds{&seed};a.network(seeds);
  QTRY_VERIFY_WITH_TIMEOUT((a.pump(),seed.has(a.key(),a.port)),10000);
  const auto previous=a.engine->azureusTorrentTargets();QVERIFY(previous.size()==1);auto bytes=read(a.state);const auto at=bytes.find("17:dht_backup_enablei1e");QVERIFY(at!=std::string::npos);bytes.replace(at,23,"17:dht_backup_enablei0e");write(a.state,bytes);
  std::optional<udp::FindValueOutcome> response;QVERIFY(seed.actor->attachKnownPeer({QHostAddress::LocalHost,a.port},55,std::chrono::steady_clock::now()+5s));
  QVERIFY(seed.actor->findValue({QHostAddress::LocalHost,a.port},55,a.key(),1,30,this,[&](auto reply){response.emplace(std::move(reply));},5s));QTRY_VERIFY_WITH_TIMEOUT(response.has_value(),6000);QVERIFY(*response);
  const auto* body=std::get_if<udp::lookup::ReplyBody>(&(**response).body());QVERIFY(body);if(const auto* found=std::get_if<udp::lookup::FoundValues>(&body->result)){for(const auto& value:found->values){QVERIFY(value.originator.address.port!=a.port);}}
  engine::AzureusPeerBatch stale{*a.hash,previous[0].admission,previous[0].live,{{}}};auto accepted=a.engine->acceptAzureusPeers(std::span(&stale,1));QVERIFY(accepted);QCOMPARE(*accepted,std::size_t(0));a.close();
 }

};
QTEST_GUILESS_MAIN(AzureusTorrentNetworkTests)
#include "azureus_torrent_network_tests.moc"
