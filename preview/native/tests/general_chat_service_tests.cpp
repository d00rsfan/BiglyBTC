// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_service.hpp"
#include "azureus_contacts.hpp"
#include "azureus_value_versions.hpp"
#include "discovery.hpp"
#include <QCryptographicHash>
#include <QNetworkDatagram>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <QTest>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTimer>
#include <QFile>
#include <thread>
using namespace biglybtc;using namespace std::chrono_literals;
namespace udp=services::azureus_udp;namespace lt=libtorrent;
namespace {
class ShardedDht final:public QObject {
 QUdpSocket socket_;services::azureus_nodes::NodeCache cache_;
public:
 services::msgsync::DiscoveryKey base{};std::optional<services::azureus_values::Value> member;
 unsigned baseReads=0,shardReads=0,stores=0;
 ShardedDht(){if(!socket_.bind(QHostAddress("127.122.31.1"),0)){throw std::runtime_error("Owned sharded seed");}connect(&socket_,&QUdpSocket::readyRead,this,[this]{drain();});}
 udp::Peer endpoint()const{return {socket_.localAddress(),socket_.localPort()};}
 void drain(){while(socket_.hasPendingDatagrams()){
  auto datagram=socket_.receiveDatagram();const std::string bytes=datagram.data().toStdString();services::azureus_dht::Context context;context.network=4;context.localVersion=55;context.now=QDateTime::currentMSecsSinceEpoch();auto header=services::azureus_dht::decodeHeader(services::azureus_dht::Kind::Request,bytes,context);if(!header){continue;}const auto& request=header->fields();services::azureus_dht::EncodeFields reply;reply.kind=services::azureus_dht::Kind::Reply;reply.network=4;reply.localVersion=55;reply.remoteVersion=request.originatorVersion;reply.connection=request.connection;reply.transaction=request.transaction;reply.now=context.now;reply.receivedAt=context.now;reply.instance=42;
  std::optional<std::string> wire;
  if(request.action==1028){reply.action=1029;udp::find::ReplyFields fields;fields.header=reply;fields.randomId=123;const std::vector<services::azureus_nodes::Snapshot> contacts;fields.contacts=contacts;auto encoded=udp::find::encodeReply(fields);if(encoded){wire=std::move(*encoded);}}
  else if(request.action==1030){udp::lookup::Context query;query.header=context;auto decoded=udp::lookup::decode(services::azureus_dht::Kind::Request,bytes,query,cache_);if(!decoded){continue;}const auto* body=std::get_if<udp::lookup::RequestBody>(&decoded->body());if(!body){continue;}reply.action=1031;udp::lookup::ReplyFields fields;fields.header=reply;const std::string target=body->target,root(reinterpret_cast<const char*>(base.data()),base.size());std::vector<std::optional<services::azureus_values::Snapshot>> values;
   if(target==root){++baseReads;fields.diversification=2;}else{for(unsigned i=0;i<10;++i){auto key=services::msgsync::diversifiedDiscoveryKey(base,i);if(key&&target==std::string(reinterpret_cast<const char*>(key->data()),key->size())){++shardReads;if(member){values.push_back(services::azureus_values::Snapshot{member->version,member->created,member->bytes,services::azureus_nodes::Snapshot{true,member->originator.protocol,member->originator.address},0,0,-1});}break;}}}fields.values=std::span<const std::optional<services::azureus_values::Snapshot>>(values);auto encoded=udp::lookup::encodeReply(fields);if(encoded){wire=std::move(*encoded);}
  }else if(request.action==1026){++stores;}
  if(wire){socket_.writeDatagram(QByteArray::fromStdString(*wire),datagram.senderAddress(),datagram.senderPort());}
 }}
};
struct GeneratedClient {
 QTemporaryDir root;std::optional<storage::ProfileLease> lease;std::unique_ptr<lt::session> sdk;quint16 port=0;
 std::unique_ptr<app::GeneralChats> chats;
 explicit GeneratedClient(const QHostAddress& address,udp::Peer seed,bool delayed=false){
  if(!root.isValid()){throw std::runtime_error("Generated profile");}auto held=storage::ProfileLease::acquire(root.path().toStdString());if(!held){throw std::runtime_error("Generated profile lease");}lease=std::move(*held);
  QUdpSocket reserve;if(!reserve.bind(address,0)){throw std::runtime_error("Owned port");}port=reserve.localPort();reserve.close();
  lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,delayed?std::string{}:address.toString().toStdString()+":"+std::to_string(port));settings.set_bool(lt::settings_pack::enable_dht,false);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);sdk=std::make_unique<lt::session>(settings);
  auto cfg=storage::ConfigFile::createMissing(*lease,"biglybt.config","de");if(!cfg){throw std::runtime_error("Config");}if(!cfg->document().replaceValue("UDP.Listen.Port","i"+std::to_string(port)+"e")||!cfg->document().replaceValue("UDP.Listen.Port.Enable","i1e")||!cfg->document().replaceValue("Ip Filter Enabled","i0e")||!cfg->document().replaceValue("Plugin.DHT.dht.enabled","i1e")){throw std::runtime_error("Generated policy fields");}if(!cfg->save(*lease)){throw std::runtime_error("Config save");}
  auto cache=storage::AzureusContacts::open(*lease,4,4);if(!cache){throw std::runtime_error("Cache");}storage::AzureusSavedContact saved;const auto ip=seed.address.toIPv4Address();for(unsigned i=0;i<4;++i){saved.address[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}saved.port=seed.port;saved.protocol=55;if(!(*cache)->save(std::span(&saved,1))){throw std::runtime_error("Cache save");}
  const auto end=std::chrono::steady_clock::now()+3s;while(!delayed&&!sdk->is_listening()&&std::chrono::steady_clock::now()<end){QTest::qWait(5);}
  auto factory=engine::ChatTransportFactory::owned(*sdk,*lease,port);if(!factory){throw std::runtime_error("Factory");}auto service=app::GeneralChats::create(std::move(*factory),1);if(!service){throw std::runtime_error(service.error().message);}chats=std::move(*service);
 }
 ~GeneratedClient(){if(chats){chats->requestStop();chats->join();chats.reset();}sdk.reset();}
};
}
class GeneralChatServiceTests final:public QObject {
 Q_OBJECT
private slots:
 void independentRoomsDiscoverExchangeAndRestoreHistory(){
  udp::Config config;config.network=4;config.servePings=true;config.routingScope=udp::RoutingScope::OwnedLoopback;config.maximumPeers=256;
  udp::OwnedPingTransport seed(std::move(config));QVERIFY(seed.start(QHostAddress("127.122.21.1")));
  const auto actual=seed.endpoint();services::azureus_nodes::ResolvedEndpoint endpoint;const auto ip=actual.address.toIPv4Address();for(unsigned i=0;i<4;++i){endpoint.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}endpoint.address.port=actual.port;
  services::azureus_nodes::NodeCache cache;auto id=cache.lookup(endpoint,55);QVERIFY(id);auto router=services::azureus_router::RouterOwner::create(id->id);auto database=services::azureus_db_runtime::Owner::create();QVERIFY(router);QVERIFY(database);QVERIFY((*database)->bindLocal({.protocol=55,.address=endpoint.address,.id=id->id}));QVERIFY(seed.serveDatabase(std::move(*router),std::move(*database),[]{return true;},[](auto done){done(true);return true;}));
  GeneratedClient a(QHostAddress("127.122.21.2"),actual),b(QHostAddress("127.122.21.3"),actual,true);
  QTimer::singleShot(200,this,[session=lt::session_handle(*b.sdk),port=b.port]()mutable{if(session.is_valid()){lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.122.21.3:"+std::to_string(port));session.apply_settings(settings);}});
  QVERIFY(seed.attachKnownPeer({QHostAddress("127.122.21.2"),a.port},55,std::chrono::steady_clock::now()+10s));QVERIFY(seed.attachKnownPeer({QHostAddress("127.122.21.3"),b.port},55,std::chrono::steady_clock::now()+10s));QVERIFY(seed.integrateKnownRouting(3s));
  const auto ready=[](const auto& client){auto s=client.chats->snapshot();return s&&s->availability=="Public chats connected";};
  {const auto end=std::chrono::steady_clock::now()+5s;while(!(ready(a)&&ready(b))&&std::chrono::steady_clock::now()<end){QTest::qWait(10);}}QVERIFY2(ready(a)&&ready(b),(a.chats->snapshot()->availability+" / "+b.chats->snapshot()->availability).c_str());
  QVERIFY(a.chats->joinRoom("Generated general room"));QVERIFY(b.chats->joinRoom("Generated general room"));
  const auto joined=[](const auto& client){auto s=client.chats->snapshot();return s&&s->rooms.size()==1&&s->rooms[0].canSend;};QTRY_VERIFY_WITH_TIMEOUT(joined(a)&&joined(b),6000);
  QVERIFY(a.chats->options("Generated general room",true,true));QTRY_VERIFY_WITH_TIMEOUT(a.chats->snapshot()->rooms[0].settings.saveHistory,4000);
  QVERIFY(a.chats->nickname("Generated general room","Generated author",true));QTRY_COMPARE_WITH_TIMEOUT(a.chats->snapshot()->rooms[0].settings.nickname,std::string("Generated author"),4000);
  QVERIFY(a.chats->send("Generated general room","Generated Unicode message: Привіт 🌍"));
  // No direct peer/UID injection: membership must travel through original
  // FIND_VALUE / token / STORE and the independently owned DHT seed.
  QTRY_VERIFY_WITH_TIMEOUT(!b.chats->snapshot()->messages.empty(),25000);
  QCOMPARE(b.chats->snapshot()->messages.back().text,std::string("Generated Unicode message: Привіт 🌍"));QCOMPARE(b.chats->snapshot()->messages.back().nickname,std::string("Generated author"));
  // Selection alone must not consume unread messages in a hidden chat tab.
  QTRY_COMPARE_WITH_TIMEOUT(b.chats->snapshot()->rooms[0].unread,1u,3000);
  QVERIFY(b.chats->selectRoom("Generated general room"));QTest::qWait(100);QCOMPARE(b.chats->snapshot()->rooms[0].unread,1u);
  QVERIFY(b.chats->setReadingRoom("Generated general room"));QTRY_COMPARE_WITH_TIMEOUT(b.chats->snapshot()->rooms[0].unread,0u,3000);
  QVERIFY(a.chats->send("Generated general room","Generated visible message"));QTRY_COMPARE_WITH_TIMEOUT(b.chats->snapshot()->messages.size(),2u,10000);QCOMPARE(b.chats->snapshot()->rooms[0].unread,0u);
  QVERIFY(b.chats->setReadingRoom({}));QVERIFY(a.chats->send("Generated general room","Generated background message"));QTRY_COMPARE_WITH_TIMEOUT(b.chats->snapshot()->messages.size(),3u,10000);QTRY_COMPARE_WITH_TIMEOUT(b.chats->snapshot()->rooms[0].unread,1u,3000);
  QVERIFY(b.chats->setReadingRoom("Generated general room"));QTRY_COMPARE_WITH_TIMEOUT(b.chats->snapshot()->rooms[0].unread,0u,3000);QVERIFY(b.chats->setReadingRoom({}));
  QVERIFY(b.chats->send("Generated general room","Generated reply"));QTRY_VERIFY_WITH_TIMEOUT(a.chats->snapshot()->messages.size()==4,10000);QCOMPARE(b.chats->snapshot()->rooms[0].unread,0u);
  a.chats->requestStop();a.chats->join();QVERIFY(a.chats->snapshot()->closed);a.chats.reset();
  auto profile=app::ChatProfile::open(*a.lease);QVERIFY(profile);auto history=(*profile)->history("Generated general room");QVERIFY(history);QVERIFY(*history);
  auto identity=(*profile)->identity("Generated general room",false);QVERIFY(identity);auto room=services::msgsync::Room::open("Generated general room",identity->uid,std::move(identity->signer),"de");QVERIFY(room);auto imported=room->importHistory(**history,QDateTime::currentMSecsSinceEpoch());QVERIFY(imported);QCOMPARE(room->retainedMessages(),4u);
  b.chats->requestStop();b.chats->join();
 }
 void aFrequencyDiversifiedRoomFindsMembersThroughDerivedDhtKeys(){
  ShardedDht seed;GeneratedClient a(QHostAddress("127.122.31.2"),seed.endpoint()),b(QHostAddress("127.122.31.3"),seed.endpoint());const std::string key="Generated sharded room";const auto span=std::span(reinterpret_cast<const std::uint8_t*>(key.data()),key.size());auto keys=plugins::msgsync::deriveChannelKeys(span);QVERIFY(keys);const auto root=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(keys->listen.data()),keys->listen.size()),QCryptographicHash::Sha1);std::copy_n(reinterpret_cast<const std::uint8_t*>(root.data()),20,seed.base.begin());
  QVERIFY(b.chats->joinRoom(key));QTRY_VERIFY_WITH_TIMEOUT(!b.chats->snapshot()->rooms.empty()&&b.chats->snapshot()->rooms[0].canSend,6000);auto profile=app::ChatProfile::open(*b.lease);QVERIFY(profile);auto identity=(*profile)->identity(key,false);QVERIFY(identity);services::azureus_values::Value value;value.version=1;value.created=QDateTime::currentMSecsSinceEpoch();value.bytes="d1:u8:"+std::string(reinterpret_cast<const char*>(identity->uid.data()),identity->uid.size())+"e";value.originator.protocol=55;value.originator.address.length=4;value.originator.address.bytes={127,122,31,3};value.originator.address.port=b.port;seed.member=std::move(value);
  QVERIFY(b.chats->send(key,"Generated message behind diversification"));QVERIFY(a.chats->joinRoom(key));QTRY_VERIFY_WITH_TIMEOUT(!a.chats->snapshot()->messages.empty(),20000);QCOMPARE(a.chats->snapshot()->messages.back().text,std::string("Generated message behind diversification"));QVERIFY(seed.baseReads>0&&seed.shardReads>0);QCOMPARE(seed.stores,0u);
  a.chats->requestStop();a.chats->join();b.chats->requestStop();b.chats->join();
 }
 void boundedCommandsAndBusyProfileDoNotBlockShutdownRequest(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);QVERIFY(storage::ConfigFile::createMissing(*lease,"biglybt.config","de"));
  lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.122.22.1:0");settings.set_bool(lt::settings_pack::enable_dht,false);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);lt::session sdk(settings);
  auto factory=engine::ChatTransportFactory::owned(sdk,*lease,0);QVERIFY(factory);auto chats=app::GeneralChats::create(std::move(*factory),2);QVERIFY(chats);
  unsigned accepted=0;for(unsigned i=0;i<100;++i){accepted+=bool((*chats)->joinRoom("Generated bounded room"));}QCOMPARE(accepted,64u);(*chats)->requestStop();QVERIFY(!(*chats)->joinRoom("Generated late room"));(*chats)->join();
 }
};
QTEST_GUILESS_MAIN(GeneralChatServiceTests)
#include "general_chat_service_tests.moc"
