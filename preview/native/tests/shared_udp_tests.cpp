// SPDX-License-Identifier: GPL-2.0-or-later
#include "provider.hpp"
#include "room.hpp"
#include "session.hpp"
#include "azureus_public_policy.hpp"
#include "chat_profile.hpp"
#include <QTemporaryDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <QTest>
#include <QUdpSocket>
#include <QScopeGuard>
#include <stdexcept>
#include <thread>
using namespace biglybtc;using namespace std::chrono_literals;
namespace udp=services::azureus_udp;namespace shared=services::shared_udp;namespace lt=libtorrent;
namespace {
void need(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
struct Actor {
  lt::session session;std::unique_ptr<shared::Provider> provider;
  udp::OwnedPingTransport transport;QObject receiver;bool permit=true;std::uint64_t revision=1;
  unsigned delivered=0;
  Actor(const QHostAddress& address,bool v6,quint16 requestedPort=0):session([&]{
    QUdpSocket reservation;need(reservation.bind(address,requestedPort),"Owned shared UDP port");const auto port=reservation.localPort();reservation.close();
    lt::session_params p;p.settings.set_str(lt::settings_pack::listen_interfaces,
      (v6?"["+address.toString().toStdString()+"]":address.toString().toStdString())+":"+std::to_string(port));
    p.settings.set_bool(lt::settings_pack::enable_dht,false);p.settings.set_bool(lt::settings_pack::enable_lsd,false);p.settings.set_bool(lt::settings_pack::enable_upnp,false);p.settings.set_bool(lt::settings_pack::enable_natpmp,false);return p;
  }()),transport([&]{udp::Config c;c.network=v6?3:0;c.servePings=true;c.routingScope=udp::RoutingScope::OwnedLoopback;return c;}()){
    const auto end=std::chrono::steady_clock::now()+3s;
    // Session settings give the actual selected listener without inventing a
    // second port. The fixture reserves that port only until SDK construction.
    while(!provider&&std::chrono::steady_clock::now()<end){
      const auto port=session.listen_port();if(port>0){auto attached=shared::Provider::attach(session,static_cast<quint16>(port));if(attached){provider=std::move(*attached);break;}}
      QTest::qWait(5);
    }
    need(bool(provider),"Qualified shared provider attachment");
    const auto local=provider->bindings();auto found=std::ranges::find_if(local,[&](const auto& v){return v.address==address;});need(found!=local.end(),"Exact actual SDK listener");
    auto channel=provider->channel(*found,*found,[this]{return permit;});need(bool(channel),"Shared channel");need(bool(transport.startShared(std::move(*channel))),"Shared actor start");
    provider->receive([this](auto local,auto peer,std::string data){return local==transport.endpoint()&&transport.receiveShared(std::move(peer),std::move(data));});
    services::azureus_nodes::ResolvedEndpoint endpoint;endpoint.address.length=v6?16:4;endpoint.address.port=transport.endpoint().port;
    if(v6){const auto ip=address.toIPv6Address();std::copy_n(ip.c,16,endpoint.address.bytes.begin());}else{const auto ip=address.toIPv4Address();for(unsigned i=0;i<4;++i){endpoint.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}
    services::azureus_nodes::NodeCache cache;const auto node=cache.lookup(endpoint,55);need(bool(node),"Actor contact");auto router=services::azureus_router::RouterOwner::create(node->id);auto database=services::azureus_db_runtime::Owner::create();need(bool(router)&&bool(database),"Actor router/database");
    need(bool((*database)->bindLocal(services::azureus_nodes::Contact{.protocol=55,.address=endpoint.address,.id=node->id})),"Actual local contact binding");
    need(bool(transport.serveDatabase(std::move(*router),std::move(*database),[]{return true;},[](auto completion){completion(true);return true;})),"Actual actor service");
  }
  bool room(std::string key,std::function<std::string(std::string)> answer){return bool(transport.registerTransferHandler(std::move(key),true,&receiver,[this,answer=std::move(answer)](auto request){++delivered;need(bool(transport.answerTransfer(request->receipt,answer(request->request))),"Actor answer");},[this]()->std::optional<std::uint64_t>{return revision;}));}
};
}
class SharedUdpTests final:public QObject {
 Q_OBJECT
private slots:
 void actualSdkActorRoundtrip_data(){QTest::addColumn<bool>("v6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void actualSdkActorRoundtrip(){
  QFETCH(bool,v6);Actor a(QHostAddress(v6?"::1":"127.121.11.1"),v6),b(QHostAddress(v6?"::1":"127.121.11.2"),v6);
  QVERIFY(a.transport.allowPeer(b.transport.endpoint()));QVERIFY(b.transport.allowPeer(a.transport.endpoint()));
  const udp::Peer invented{QHostAddress(v6?"2001:db8::1":"198.51.100.1"),a.transport.endpoint().port};
  QVERIFY(!a.provider->channel(a.transport.endpoint(),invented,[]{return true;}));
  const std::string key(20,'G');QVERIFY(a.room(key,[](std::string s){return s;}));QVERIFY(b.room(key,[](std::string s){return "reply:"+s;}));
  std::optional<udp::TransferOutcome> answer;const std::string payload(6000,'q');
  QVERIFY(a.transport.callTransfer(b.transport.endpoint(),55,key,payload,&a.receiver,[&](auto out){answer=std::move(out);},std::chrono::steady_clock::now()+5s));
  QTRY_VERIFY_WITH_TIMEOUT(answer.has_value(),5000);QVERIFY(*answer);QCOMPARE(**answer,"reply:"+payload);QCOMPARE(b.delivered,1u);
  std::optional<udp::Outcome> ping;QVERIFY(b.transport.ping(a.transport.endpoint(),55,&b.receiver,[&](auto out){ping=std::move(out);},1s));QTRY_VERIFY_WITH_TIMEOUT(ping.has_value(),1500);QVERIFY(*ping);
  // A stale source permission does not send through a still-live SDK binding.
  a.permit=false;QVERIFY(!a.transport.ping(b.transport.endpoint(),55,&a.receiver,[](auto){},1s));
  auto denied=a.provider->channel(a.transport.endpoint(),a.transport.endpoint(),[]{return false;});QVERIFY(denied);
  QVERIFY(!denied->send("invalid",b.transport.endpoint(),std::chrono::steady_clock::now()+1s));
  a.provider->close();QVERIFY(!denied->current());QVERIFY(!a.transport.receiveShared(b.transport.endpoint(),"stale"));
 }
 void actualEncryptedRoomCatchupOverSharedSdk_data(){QTest::addColumn<bool>("v6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void actualEncryptedRoomCatchupOverSharedSdk(){
  QFETCH(bool,v6);Actor a(QHostAddress(v6?"::1":"127.121.21.1"),v6),b(QHostAddress(v6?"::1":"127.121.21.2"),v6);
  QVERIFY(a.transport.allowPeer(b.transport.endpoint()));QVERIFY(b.transport.allowPeer(a.transport.endpoint()));
  namespace chat=services::msgsync;namespace crypto=plugins::msgsync;
  const auto make=[](std::uint8_t n){auto identity=crypto::generateMessageIdentity();need(bool(identity),"Room identity");auto signer=crypto::MessageSigner::open(identity->publicKey,identity->privateKey);need(bool(signer),"Room signer");chat::Uid uid{};uid[0]=n;auto room=chat::Room::open("Generated SDK room",uid,std::move(*signer),"de");need(bool(room),"Room core");return std::make_shared<chat::Room>(std::move(*room));};
  auto left=make(1),right=make(2);
  const auto hooks=[this]{chat::RoomSessionHooks h;h.revision=[]()->std::optional<std::uint64_t>{return 1;};
    // Generated actor preflight has no disk gate. The operational owner must
    // instead queue until its real source job is Released.
    h.afterRelease=[this](std::move_only_function<void()> callback){return QMetaObject::invokeMethod(this,[callback=std::move(callback)]()mutable{callback();},Qt::QueuedConnection);};
    h.withPermit=[](auto end,std::move_only_function<void()> callback){if(std::chrono::steady_clock::now()>=end){return false;}callback();return true;};return h;};
  std::array<udp::OwnedPingTransport*,1> aa{&a.transport},bb{&b.transport};auto first=chat::RoomSession::open(left,aa,hooks()),second=chat::RoomSession::open(right,bb,hooks());QVERIFY(first&&second);
  auto ac=chat::RoomSession::contactMap(a.transport.endpoint()),bc=chat::RoomSession::contactMap(b.transport.endpoint());QVERIFY(ac&&bc);
  QVERIFY((*first)->addPeer({0,b.transport.endpoint(),55,right->uid(),*bc,false,0}));QVERIFY((*second)->addPeer({0,a.transport.endpoint(),55,left->uid(),*ac,false,0}));
  const auto now=QDateTime::currentMSecsSinceEpoch();for(int i=0;i<24;++i){QVERIFY((*first)->send("Generated history "+std::to_string(i)+std::string(180,'x'),"Generated",now));}
  QElapsedTimer elapsed;elapsed.start();std::uint64_t tick=0;
  while(elapsed.elapsed()<5000&&right->retainedMessages()<24){(*second)->tick(++tick,QDateTime::currentMSecsSinceEpoch());QTest::qWait(2);}
  QCOMPARE(right->retainedMessages(),std::size_t{24});QVERIFY((*second)->facts().exchanges>0);QVERIFY((*second)->facts().active<=6);
  QVERIFY((*second)->send("Reply & Unicode: Привет","Generated reply",QDateTime::currentMSecsSinceEpoch()));
  QVERIFY((*first)->tick(1,QDateTime::currentMSecsSinceEpoch()));QTRY_COMPARE_WITH_TIMEOUT(left->retainedMessages(),std::size_t{25},3000);
  auto messages=left->messages(QDateTime::currentMSecsSinceEpoch());QVERIFY(messages);QVERIFY(std::ranges::any_of(*messages,[](const auto& m){return m.text=="Reply & Unicode: Привет";}));
  const udp::Peer unadmitted{QHostAddress(v6?"::1":"127.121.21.9"),54321};auto raw=chat::RoomSession::contactMap(unadmitted);QVERIFY(raw);chat::NodeHint hint;hint.uid[0]=9;hint.encodedContact=*raw;auto suggested=chat::RoomSession::contactHint(0,hint);QVERIFY(suggested);QVERIFY(!(*second)->addPeer(*suggested));QCOMPARE((*second)->facts().peers,std::size_t{1});
  QCOMPARE(chat::RoomSession::syncFactor(119),1u);QCOMPARE(chat::RoomSession::syncFactor(120),2u);QCOMPARE(chat::RoomSession::syncFactor(300),3u);QCOMPARE(chat::RoomSession::syncFactor(3600),1u);QCOMPARE(chat::RoomSession::syncFactor(300,true),6u);
  (*second)->close();QVERIFY(!(*second)->tick(1,QDateTime::currentMSecsSinceEpoch()));QVERIFY(!(*second)->send("late","closed",now));
 }
 void pingRespondsToAnotherEndpointWithTheSameDerivedNodeId(){
  Actor a(QHostAddress("::1"),true);services::azureus_nodes::NodeCache cache;
  const auto id=[&](quint16 port){services::azureus_nodes::ResolvedEndpoint e;e.address.length=16;e.address.bytes[15]=1;e.address.port=port;auto result=cache.lookup(e,55);need(bool(result),"IPv6 fixture node ID");return result->id;};
  const auto first=id(a.transport.endpoint().port);quint16 chosen=0;
  for(unsigned delta=1;delta<65535;++delta){const auto port=static_cast<quint16>((a.transport.endpoint().port+delta)%65535);if(!port||port==a.transport.endpoint().port||id(port)!=first){continue;}QUdpSocket reserved;if(reserved.bind(QHostAddress("::1"),port)){chosen=port;break;}}
  QVERIFY(chosen);Actor b(QHostAddress("::1"),true,chosen);QCOMPARE(id(b.transport.endpoint().port),first);
  QVERIFY(a.transport.allowPeer(b.transport.endpoint()));QVERIFY(b.transport.allowPeer(a.transport.endpoint()));
  std::optional<udp::Outcome> result;QVERIFY(b.transport.ping(a.transport.endpoint(),55,this,[&](auto out){result=std::move(out);},1s));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1500);QVERIFY(*result);
 }
 void addressProbeLearnsThroughASeedAndNeedsTwoDifferentReporters(){
  Actor owner(QHostAddress("127.121.30.1"),false),b(QHostAddress("127.121.30.2"),false),c(QHostAddress("127.121.30.3"),false);
  QVERIFY(b.transport.allowPeer(owner.transport.endpoint()));QVERIFY(c.transport.allowPeer(owner.transport.endpoint()));
  udp::Config config;config.network=0;udp::FindNodeReply snapshot;
  for(const auto& peer:{b.transport.endpoint(),c.transport.endpoint()}){
    services::azureus_dht::Address address;address.length=4;address.port=peer.port;const auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}
    snapshot.contacts.push_back({true,55,address});
  }
  config.findNodeReply=snapshot;udp::OwnedPingTransport seed(std::move(config));QVERIFY(seed.start(QHostAddress("127.121.30.4")));QVERIFY(seed.allowPeer(owner.transport.endpoint()));
  const auto permit=[](auto end,std::move_only_function<void()> callback){if(std::chrono::steady_clock::now()>=end){return false;}callback();return true;};
  std::optional<shared::AddressResult> result;
  QVERIFY(owner.provider->probeAddress(owner.transport.endpoint(),0,{{seed.endpoint(),55}},true,permit,[]{return true;},[&](auto out){result=std::move(out);}));
  QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),5000);QVERIFY(*result);QCOMPARE((*result)->external,owner.transport.endpoint());QCOMPARE((*result)->candidates.size(),std::size_t{3});
  // One correlated reply, even when repeated through another UDP port on the
  // same remote IP, is insufficient to publish an address observation.
  Actor sameIp(QHostAddress("127.121.30.2"),false);QVERIFY(sameIp.transport.allowPeer(owner.transport.endpoint()));result.reset();
  QVERIFY(owner.provider->probeAddress(owner.transport.endpoint(),0,{{b.transport.endpoint(),55},{sameIp.transport.endpoint(),55}},true,permit,[]{return true;},[&](auto out){result=std::move(out);}));
  QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),3000);QVERIFY(!*result);
 }
 void anOrdinarySeedMustYieldContactsWithoutTurningItsClaimIntoOtherPeersVotes(){
  Actor owner(QHostAddress("127.121.32.1"),false),seed(QHostAddress("127.121.32.2"),false),other(QHostAddress("127.121.32.3"),false);
  QVERIFY(seed.transport.allowPeer(owner.transport.endpoint()));QVERIFY(other.transport.allowPeer(owner.transport.endpoint()));QVERIFY(other.transport.allowPeer(seed.transport.endpoint()));QVERIFY(seed.transport.attachKnownPeer(other.transport.endpoint(),55,std::chrono::steady_clock::now()+3s));
  std::optional<shared::AddressResult> result;const auto permit=[](auto end,std::move_only_function<void()> callback){if(std::chrono::steady_clock::now()>=end){return false;}callback();return true;};
  // Only one normal seed is supplied. It challenges the placeholder first;
  // the corrected follow-up yields another reporter. That reporter must see
  // a placeholder again, rather than silently accepting the first claim.
  QVERIFY(owner.provider->probeAddress(owner.transport.endpoint(),0,{{seed.transport.endpoint(),55}},true,permit,[]{return true;},[&](auto out){result=std::move(out);}));
  QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),5000);QVERIFY(*result);QCOMPARE((*result)->external,owner.transport.endpoint());QVERIFY((*result)->candidates.size()>=2);
 }
 void aSharedRouterHandlesLargeContactRepliesWithoutOptionalPingBacklogFailure(){
  Actor owner(QHostAddress("127.121.41.1"),false);std::vector<std::unique_ptr<udp::OwnedPingTransport>> peers;udp::FindNodeReply advertised;
  for(unsigned i=1;i<=96;++i){udp::Config config;config.network=0;config.servePings=true;config.findNodeReply=udp::FindNodeReply{};auto peer=std::make_unique<udp::OwnedPingTransport>(std::move(config));QVERIFY(peer->start(QHostAddress(QStringLiteral("127.%1.42.1").arg(i+20))));QVERIFY(peer->allowPeer(owner.transport.endpoint()));services::azureus_dht::Address address;address.length=4;address.port=peer->endpoint().port;const auto ip=peer->endpoint().address.toIPv4Address();for(unsigned j=0;j<4;++j){address.bytes[j]=static_cast<std::uint8_t>(ip>>(24-8*j));}advertised.contacts.push_back({true,55,address});peers.push_back(std::move(peer));}
  udp::Config config;config.network=0;config.servePings=true;config.findNodeReply=advertised;udp::OwnedPingTransport seed(std::move(config));QVERIFY(seed.start(QHostAddress("127.121.43.1")));QVERIFY(seed.allowPeer(owner.transport.endpoint()));
  QVERIFY(owner.transport.attachKnownPeer(seed.endpoint(),55,std::chrono::steady_clock::now()+5s));QVERIFY(owner.transport.integrateKnownRouting(5s));
  QTRY_VERIFY_WITH_TIMEOUT(owner.transport.routingSnapshot()&&owner.transport.routingSnapshot()->finished,5000);const auto snapshot=owner.transport.routingSnapshot();QVERIFY(snapshot->seeded);QVERIFY(!snapshot->failure);QVERIFY2(snapshot->contactCount>72,qPrintable(QString::number(snapshot->contactCount)));
 }
 void bootstrapActorCannotServeRoomsOrIssueValueOperations(){
  Actor owner(QHostAddress("127.121.31.1"),false);udp::Config cfg;cfg.network=0;cfg.bootstrapOnly=true;cfg.routingScope=udp::RoutingScope::OwnedLoopback;
  udp::OwnedPingTransport probe(std::move(cfg));auto channel=owner.provider->channel(owner.transport.endpoint(),owner.transport.endpoint(),[]{return true;});QVERIFY(channel);channel->advertised.address=QHostAddress(QHostAddress::AnyIPv4);QVERIFY(probe.startShared(std::move(*channel)));
  const udp::Peer peer{QHostAddress("127.121.31.2"),54321};QVERIFY(probe.allowPeer(peer));services::azureus_nodes::NodeId key{};
  QVERIFY(!probe.findValue(peer,55,key,0,1,this,[](auto){},1s));QVERIFY(!probe.serveDatabase({}, {},[]{return true;}));
  QVERIFY(!probe.registerTransferHandler(std::string(20,'H'),true,this,[](auto){},[]()->std::optional<std::uint64_t>{return 1;}));QCOMPARE(probe.statistics().sent,std::uint64_t{0});
 }
 void sharedProfilePolicyIgnoresUnrelatedWritesAndScopesIdentityChanges(){
  Actor actor(QHostAddress("127.121.24.1"),false);QTemporaryDir root;QVERIFY(root.isValid());
  auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto cfg=storage::ConfigFile::createMissing(*lease,"biglybt.config","de");QVERIFY(cfg);
  QVERIFY(cfg->document().replaceValue("Plugin.DHT.dht.enabled","i1e"));QVERIFY(cfg->document().replaceValue("Ip Filter Enabled","i0e"));QVERIFY(cfg->document().replaceValue("UDP.Listen.Port","i"+std::to_string(actor.transport.endpoint().port)+"e"));QVERIFY(cfg->save(*lease));
  auto profile=app::ChatProfile::open(*lease);QVERIFY(profile);auto first=(*profile)->identity("Generated policy A",true),second=(*profile)->identity("Generated policy B",true);QVERIFY(first&&second);
  auto versions=storage::AzureusValueVersions::open(*lease,4);QVERIFY(versions);storage::AzureusPublicBind bind;bind.network=4;bind.family=4;bind.port=actor.transport.endpoint().port;
  const auto ip=actor.transport.endpoint().address.toIPv4Address();for(unsigned i=0;i<4;++i){bind.address[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}
  auto policy=storage::AzureusPublicPolicy::openOwnedShared(*lease,*versions,bind,actor.provider->witness());QVERIFY(policy);QVERIFY((*policy)->validate());
  const auto firstKey=app::ChatProfile::configKey(first->settings.key),secondKey=app::ChatProfile::configKey(second->settings.key);
  QVERIFY((*policy)->validateChatIdentity(firstKey,first->sourceStamp));
  auto updated=storage::ConfigFile::load(*lease,"biglybt.config");QVERIFY(updated);QVERIFY(updated->document().replaceValue("Generated unrelated statistics","i123e"));QVERIFY(updated->document().replaceValue("azbuddy.chat.shared_nick","7:Changed"));QVERIFY(updated->save(*lease));
  QVERIFY((*policy)->validate());QVERIFY((*policy)->validateChatIdentity(firstKey,first->sourceStamp));
  updated=storage::ConfigFile::load(*lease,"biglybt.config");QVERIFY(updated);auto identity=core::Document::parse(core::encodeValue(*updated->document().find(firstKey)).value());QVERIFY(identity);auto changed=services::msgsync::Uid{};changed[0]=0x7e;const std::string uid(reinterpret_cast<const char*>(changed.data()),changed.size());QVERIFY(identity->replaceValue("uid",std::to_string(uid.size())+":"+uid));QVERIFY(updated->document().replaceValue(firstKey,identity->serialize()));QVERIFY(updated->save(*lease));
  QVERIFY((*policy)->validate());QVERIFY(!(*policy)->validateChatIdentity(firstKey,first->sourceStamp));QVERIFY((*policy)->current());QVERIFY((*policy)->validateChatIdentity(secondKey,second->sourceStamp));
  updated=storage::ConfigFile::load(*lease,"biglybt.config");QVERIFY(updated);const auto otherPort=bind.port==65535?65534:bind.port+1;QVERIFY(updated->document().replaceValue("UDP.Listen.Port","i"+std::to_string(otherPort)+"e"));QVERIFY(updated->save(*lease));QVERIFY(!(*policy)->validate());QVERIFY(!(*policy)->current());
  updated=storage::ConfigFile::load(*lease,"biglybt.config");QVERIFY(updated);QVERIFY(updated->document().replaceValue("UDP.Listen.Port","i"+std::to_string(bind.port)+"e"));QVERIFY(updated->save(*lease));QVERIFY(!(*policy)->validate());
  auto fresh=storage::AzureusPublicPolicy::openOwnedShared(*lease,*versions,bind,actor.provider->witness());QVERIFY(fresh);QVERIFY((*fresh)->current());actor.provider->close();QVERIFY(!(*fresh)->current());QVERIFY(!(*fresh)->validate());
 }
 void sharedIngressIsBoundedAndDoesNotGrantPeerAdmission(){
  Actor a(QHostAddress("127.121.12.1"),false);const udp::Peer unknown{QHostAddress("127.121.12.2"),54321};
  unsigned accepted=0;for(unsigned i=0;i<1000;++i){accepted+=a.transport.receiveShared(unknown,std::string(20,'x'));}QCOMPARE(accepted,64u);
  QTRY_COMPARE_WITH_TIMEOUT(a.transport.statistics().received,std::uint64_t{64},1000);
  QCOMPARE(a.transport.statistics().unowned,std::uint64_t{64});QVERIFY(!a.transport.isRoutingPeer(unknown));
  a.transport.close();QVERIFY(!a.transport.receiveShared(unknown,"late"));
 }
 void sdkCallbacksDeliverOnTheQtOwnerAndOldAttachmentCanBeReplaced(){
  Actor a(QHostAddress("127.121.13.1"),false);auto old=a.provider->channel(a.transport.endpoint(),a.transport.endpoint(),[]{return true;});QVERIFY(old);
  a.provider->close();QVERIFY(!old->current());
  const bool existed=qEnvironmentVariableIsSet("LD_PRELOAD");const auto saved=qgetenv("LD_PRELOAD");
  {const auto restored=qScopeGuard([&]{if(existed){qputenv("LD_PRELOAD",saved);}else{qunsetenv("LD_PRELOAD");}});
    qputenv("LD_PRELOAD","generated-declared-preload-policy");
    QVERIFY(!shared::Provider::attach(a.session,a.transport.endpoint().port));
  }
  auto replacement=shared::Provider::attach(a.session,a.transport.endpoint().port);QVERIFY(replacement);QVERIFY((*replacement)->refresh());
  QUdpSocket sender;QVERIFY(sender.bind(QHostAddress("127.121.13.2"),0));bool delivered=false;
  (*replacement)->receive([&](auto,auto,std::string){delivered=QThread::currentThread()==thread();return true;});
  QByteArray data(32,'x');data[0]=char(0x80);data[8]=0;data[9]=0;data[10]=4;data[11]=0;
  QCOMPARE(sender.writeDatagram(data,a.transport.endpoint().address,a.transport.endpoint().port),qint64(data.size()));QTRY_VERIFY_WITH_TIMEOUT(delivered,1000);
  auto proof=(*replacement)->witness();std::jthread withdraw([proof]{proof->revoke();});withdraw.join();
  const auto local=(*replacement)->bindings().front();QVERIFY(!(*replacement)->channel(local,local,[]{return true;}));
 }
};
QTEST_GUILESS_MAIN(SharedUdpTests)
#include "shared_udp_tests.moc"
