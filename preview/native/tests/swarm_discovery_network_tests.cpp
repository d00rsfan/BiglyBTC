// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_service.hpp"
#include "azureus_contacts.hpp"
#include "original_torrent_identity_lt.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QElapsedTimer>
#include <algorithm>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUdpSocket>
#include <QTest>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/torrent_info.hpp>
using namespace biglybtc;using namespace std::chrono_literals;
namespace {
namespace udp=services::azureus_udp;namespace nodes=services::azureus_nodes;namespace lt=libtorrent;namespace rc=services::related_content;
void need(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
std::string bytes(const lt::entry& entry){std::string result;lt::bencode(std::back_inserter(result),entry);return result;}
void write(const QString& path,std::string_view value){need(QDir().mkpath(QFileInfo(path).absolutePath()),"Generated parent");QFile file(path);need(file.open(QIODevice::WriteOnly)&&file.write(value.data(),value.size())==qint64(value.size()),"Generated write");}
std::string read(const QString& path){QFile file(path);need(file.open(QIODevice::ReadOnly),"Generated read");return file.readAll().toStdString();}
nodes::Contact contact(udp::Peer peer){nodes::ResolvedEndpoint endpoint;if(peer.address.protocol()==QAbstractSocket::IPv6Protocol){endpoint.address.length=16;const auto ip=peer.address.toIPv6Address();std::copy(std::begin(ip.c),std::end(ip.c),endpoint.address.bytes.begin());}else{endpoint.address.length=4;const auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){endpoint.address.bytes[i]=std::uint8_t(ip>>(24-8*i));}}endpoint.address.port=peer.port;nodes::NodeCache cache;const auto found=cache.lookup(endpoint,55);need(bool(found),"Generated contact");nodes::Contact out;out.address=endpoint.address;out.protocol=55;out.id=found->id;return out;}
struct Wall final:services::azureus_db_value::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}};
struct Seed {
 std::unique_ptr<udp::OwnedPingTransport> actor;std::shared_ptr<services::azureus_db_runtime::Owner> database;std::int32_t network;
 std::unique_ptr<QProcess> java;QTemporaryDir javaRoot;udp::Peer javaPeer;
 udp::Peer endpoint()const{return actor?actor->endpoint():javaPeer;}
 Seed(const QString& command,const QString& classpath,int net):network(net){
  QUdpSocket reserve;const auto ip=network==3?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost;need(reserve.bind(ip,0),"Reserve original Java port");javaPeer={ip,reserve.localPort()};reserve.close();java=std::make_unique<QProcess>();java->start(command,{"--add-opens=java.base/java.net=ALL-UNNAMED","-cp",classpath,"AzureusTorrentPeer",javaRoot.path(),QString::number(network),QString::number(javaPeer.port),"rcm"});need(java->waitForStarted(),"Original RCM transport starts");bool ready=false;QElapsedTimer timer;timer.start();while(timer.elapsed()<10000&&java->state()!=QProcess::NotRunning){java->waitForReadyRead(100);while(java->canReadLine()){if(java->readLine().startsWith("READY ")){ready=true;break;}}if(ready){break;}}if(!ready){qWarning().noquote()<<java->readAllStandardError();}need(ready,"Original RCM transport ready");
 }
 ~Seed(){if(java){java->write("EXIT\n");if(!java->waitForFinished(2000)){java->kill();java->waitForFinished();}}}
 bool originalCommand(const QByteArray& command,const QByteArray& reply){java->write(command+"\n");java->waitForBytesWritten(1000);if(!java->canReadLine()){java->waitForReadyRead(1500);}return java->readLine().trimmed()==reply;}
 void originalFinding(std::string_view parent,std::string_view target){const auto key=rc::associationKey(parent);need(originalCommand("ADD "+QByteArray(reinterpret_cast<const char*>(key.data()),20).toHex()+" "+QByteArray(target.data(),qsizetype(target.size())).toHex(),"ADDED"),"Original Java generated finding");}
 explicit Seed(std::int32_t net=0):network(net){udp::Config config;config.network=net;config.servePings=true;config.routingScope=udp::RoutingScope::OwnedLoopback;actor=std::make_unique<udp::OwnedPingTransport>(std::move(config));need(actor->start(net==3?QHostAddress::LocalHostIPv6:QHostAddress("127.126.33.1")).has_value(),"Seed starts");auto local=contact(actor->endpoint());auto router=services::azureus_router::RouterOwner::create(local.id);auto db=services::azureus_db_runtime::Owner::create();need(bool(router)&&bool(db),"Seed database");database=std::move(*db);need(database->bindLocal(local).has_value(),"Seed local identity");need(actor->serveDatabase(std::move(*router),database,[]{return true;},[](auto done){done(true);return true;}).has_value(),"Seed serves original database");}
 bool contains(std::string_view parent,std::string_view target){const auto key=rc::associationKey(parent);if(java){return originalCommand("HAS "+QByteArray(reinterpret_cast<const char*>(key.data()),20).toHex()+" "+QByteArray(target.data(),qsizetype(target.size())).toHex(),"HAS 1");}Wall clock;auto values=database->read(std::string(reinterpret_cast<const char*>(key.data()),20),contact(actor->endpoint()),30,0,clock);if(!values||!*values){return false;}for(const auto& record:(**values).values){need(record.flags==0,"Original RCM single-value flags");need(record.bytes.find("generated-secret")==std::string::npos&&record.bytes.find("passkey")==std::string::npos,"No tracker key in any RCM publication");auto parsed=rc::decodeAssociation(record.bytes,parent,1,false,1000);if(parsed&&parsed->hash==target){return true;}}return false;}
};
struct Client {
 QTemporaryDir root{QStringLiteral("/tmp/biglybtc-swarm-network-XXXXXX")};std::optional<storage::ProfileLease> lease;std::unique_ptr<lt::session> sdk;std::unique_ptr<app::GeneralChats> service;std::string common,unique,privateHash;quint16 port=0;QHostAddress ip;
 Client(int index,Seed& seed,bool privateOnly=false):ip(seed.network==3?"::1":seed.java?"127.0.0.1":index==1?"127.126.33.2":"127.126.33.3"){
  need(root.isValid(),"Generated root");QDir().mkpath(root.path()+"/active");QUdpSocket reservation;need(reservation.bind(ip,0),"Owned port reservation");port=reservation.localPort();reservation.close();lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,(seed.network==3?"["+ip.toString().toStdString()+"]":ip.toString().toStdString())+":"+std::to_string(port));settings.set_bool(lt::settings_pack::enable_dht,false);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);sdk=std::make_unique<lt::session>(settings);
  lt::entry config(lt::entry::dictionary_type{});config["UDP.Listen.Port"]=port;config["TCP.Listen.Port"]=port;config["UDP.Listen.Port.Enable"]=1;config["UDP.NonData.Listen.Port.Same"]=1;config["IPV6 Enable Support"]=seed.network==3?1:0;config["Ip Filter Enabled"]=0;config["Plugin.DHT.dht.enabled"]=1;config["rcm.max_results"]=17;config["Plugin.aercm.sources.setlist"]=lt::entry::list_type{lt::entry("*")};write(root.path()+"/biglybt.config",bytes(config));
  lt::entry downloads(lt::entry::list_type{});for(int slot=0;slot<3;++slot){lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]["name"]=slot==0?"Generated shared.bin":slot==1?(index==1?"Generated alpha.bin":"Generated beta.bin"):"Generated private.bin";torrent["info"]["length"]=17;torrent["info"]["piece length"]=16384;torrent["info"]["pieces"]=std::string(20,'x');if(slot==2||privateOnly){torrent["info"]["private"]=1;}torrent["announce"]="udp://tracker.invalid/announce?passkey=generated-secret";torrent["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public")};torrent["attributes"]["parameters"]["rand"]=slot+7;const auto raw=bytes(torrent);auto document=core::Document::parse(raw);lt::error_code error;lt::torrent_info info(lt::span<const char>(raw.data(),raw.size()),error,lt::from_span);need(bool(document)&&!error,"Generated torrent");const auto identity=storage::originalTorrentIdentityFromLibtorrent(*document,info);need(bool(identity),"Original selected identity");const auto h=identity->originalKey.bytes();std::string hash(reinterpret_cast<const char*>(h.data()),h.size());if(slot==0){common=hash;}else if(slot==1){unique=hash;}else{privateHash=hash;}const auto path=root.path()+"/active/"+QString::fromStdString(identity->originalKey.hex())+".dat";write(path,raw);lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=hash;row["torrent"]=path.toStdString();row["save_dir"]=(root.path()+"/payload").toStdString();row["save_file"]="generated.bin";row["persistent"]=1;row["state"]=70;row["position"]=slot+1;downloads.list().push_back(std::move(row));}
  lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=downloads;write(root.path()+"/downloads.config",bytes(inventory));auto held=storage::ProfileLease::acquire(root.path().toStdString());need(bool(held),"Generated profile lease");lease=std::move(*held);auto cache=storage::AzureusContacts::open(*lease,seed.network,seed.network==3?16:4);need(bool(cache),"Original contact owner");const auto c=contact(seed.endpoint());storage::AzureusSavedContact saved{1000,55,c.address.length,c.address.bytes,c.address.port};need((*cache)->save(std::span(&saved,1)).has_value(),"Original seed cache");auto factory=engine::ChatTransportFactory::owned(*sdk,*lease,port,true);need(bool(factory),"Shared SDK factory");auto opened=app::GeneralChats::create(std::move(*factory),std::uint64_t(index));need(bool(opened),"Shared Public owner");service=std::move(*opened);if(seed.actor){need(seed.actor->attachKnownPeer({ip,port},55,std::chrono::steady_clock::now()+15s).has_value(),"Seed knows owned caller");static_cast<void>(seed.actor->integrateKnownRouting(5s));}
 }
 ~Client(){close();}
 bool ready(){const auto value=service->snapshot();return value&&value->discoveries&&value->torrentDht.readyNetworks>0;}
 bool found(std::string_view hash){const auto snapshot=service->snapshot();return snapshot&&snapshot->discoveries&&std::ranges::any_of(snapshot->discoveries->items,[&](const rc::Item& item){return item.hash==hash;});}
 void close(){if(service){service->requestStop();service->join();service.reset();}sdk.reset();}
};
}
class SwarmDiscoveryNetworkTests final:public QObject {
 Q_OBJECT
private slots:
 void sharedOriginalAssociationExchangePersistsAndRespectsPrivacy_data(){QTest::addColumn<int>("network");QTest::newRow("IPv4")<<0;QTest::newRow("IPv6")<<3;}
 void sharedOriginalAssociationExchangePersistsAndRespectsPrivacy(){
  QFETCH(int,network);Seed seed(network);Client a(1,seed);QTRY_VERIFY_WITH_TIMEOUT(a.ready(),10000);QVERIFY(a.service->discoverNow());QTRY_VERIFY_WITH_TIMEOUT(seed.contains(a.common,a.unique),20000);QVERIFY(!seed.contains(a.common,a.privateHash));
  Client b(2,seed);QTRY_VERIFY_WITH_TIMEOUT(b.ready(),10000);QCOMPARE(a.common,b.common);const auto inventory=read(b.root.path()+"/downloads.config");QVERIFY(b.service->lookupDiscoveries(b.common));QTRY_VERIFY_WITH_TIMEOUT(b.found(a.unique),15000);QVERIFY(!b.found(a.privateHash));QVERIFY(!b.found(b.common));QVERIFY(!b.found(b.unique));
  QVERIFY(b.service->discoverNow());QTRY_VERIFY_WITH_TIMEOUT(seed.contains(b.common,b.unique),20000);QVERIFY(a.service->lookupDiscoveries(a.common));QTRY_VERIFY_WITH_TIMEOUT(a.found(b.unique),15000);
  const auto snapshot=b.service->snapshot()->discoveries;QVERIFY(snapshot->unread>0);app::SwarmDiscoveryChanges changes;for(const auto& item:snapshot->items){changes.read.push_back(item.key);}QVERIFY(b.service->changeDiscoveries(changes));QTRY_VERIFY_WITH_TIMEOUT(b.service->snapshot()->discoveries->unread==0,5000);
  b.close();QVERIFY(read(b.root.path()+"/downloads.config")==inventory);QVERIFY(!QFileInfo::exists(b.root.path()+"/payload"));auto saved=app::SwarmDiscoveryProfile::create(*b.lease);QVERIFY(saved);auto cold=(*saved)->refresh();QVERIFY(cold);QVERIFY(std::ranges::any_of((*cold)->items,[&](const rc::Item& item){return item.hash==a.unique;}));QCOMPARE((*cold)->unread,std::size_t(0));saved->reset();
  const auto root=b.root.path();b.lease.reset();auto held=storage::ProfileLease::acquire(root.toStdString());QVERIFY(held);a.close();
 }
 void actualOriginalJavaRcmTransportPublishesAndReturnsOriginalValues_data(){QTest::addColumn<int>("network");QTest::newRow("Java-IPv4")<<0;QTest::newRow("Java-IPv6")<<3;}
 void actualOriginalJavaRcmTransportPublishesAndReturnsOriginalValues(){
  QFETCH(int,network);
  if(!qEnvironmentVariableIsSet("BIGLYBTC_RCM_NETWORK_JAVA")){QSKIP("Original Java reference not configured");}
  Seed seed(qEnvironmentVariable("BIGLYBTC_RCM_NETWORK_JAVA"),qEnvironmentVariable("BIGLYBTC_RCM_NETWORK_CLASSPATH"),network);Client a(1,seed);QTRY_VERIFY_WITH_TIMEOUT(a.ready(),12000);QVERIFY(a.service->discoverNow());QTRY_VERIFY_WITH_TIMEOUT(seed.contains(a.common,a.unique),20000);QVERIFY(!seed.contains(a.common,a.privateHash));const std::string generated(20,'J');seed.originalFinding(a.common,generated);QVERIFY(a.service->lookupDiscoveries(a.common));QTRY_VERIFY_WITH_TIMEOUT(a.found(generated),15000);const auto snapshot=a.service->snapshot()->discoveries;const auto found=std::ranges::find(snapshot->items,generated,&rc::Item::hash);QVERIFY(found!=snapshot->items.end());QCOMPARE(found->title,std::string("Generated Java finding"));QCOMPARE(found->seeds(),4);a.close();
 }
 void aKnownPrivateHashCannotBeQueriedOrPublished(){
  Seed seed;Client a(1,seed,true);QTRY_VERIFY_WITH_TIMEOUT(a.ready(),10000);QVERIFY(a.service->lookupDiscoveries(a.common));QVERIFY(a.service->discoverNow());QTest::qWait(2200);QCOMPARE(a.service->snapshot()->discoveryNetwork.lookups,std::uint64_t(0));QCOMPARE(a.service->snapshot()->discoveryNetwork.publications,std::uint64_t(0));QVERIFY(!seed.contains(a.common,a.unique));a.close();
 }
 void disablingRcmRevokesLocalServingAndFutureRequests(){
  Seed seed;Client a(1,seed);QTRY_VERIFY_WITH_TIMEOUT(a.ready(),10000);QVERIFY(a.service->discoverNow());QTRY_VERIFY_WITH_TIMEOUT(seed.contains(a.common,a.unique),20000);auto cfg=storage::ConfigFile::load(*a.lease,"biglybt.config");QVERIFY(cfg);QVERIFY(cfg->document().replaceValue("rcm.overall.enabled","i0e"));QVERIFY(cfg->save(*a.lease));
  const auto key=rc::associationKey(a.common);std::optional<udp::FindValueOutcome> response;QVERIFY(seed.actor->attachKnownPeer({a.ip,a.port},55,std::chrono::steady_clock::now()+5s));QVERIFY(seed.actor->findValue({a.ip,a.port},55,key,0,30,this,[&](auto result){response.emplace(std::move(result));},5s));QTRY_VERIFY_WITH_TIMEOUT(response.has_value(),6000);QVERIFY(*response);const auto* body=std::get_if<udp::lookup::ReplyBody>(&(**response).body());QVERIFY(body);if(const auto* values=std::get_if<udp::lookup::FoundValues>(&body->result)){for(const auto& v:values->values){QVERIFY(v.originator.address.port!=a.port);}}
  const auto before=a.service->snapshot()->discoveryNetwork.lookups;QVERIFY(a.service->lookupDiscoveries(a.common));QTest::qWait(1500);QCOMPARE(a.service->snapshot()->discoveryNetwork.lookups,before);a.close();
 }
};
QTEST_GUILESS_MAIN(SwarmDiscoveryNetworkTests)
#include "swarm_discovery_network_tests.moc"
