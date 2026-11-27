// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
#include <QElapsedTimer>
#include <QScopeGuard>
#include <libtorrent/extensions.hpp>
#include <future>
#include <set>
#include "chat_owner_fixture.hpp"
#include "key_block_fixture.hpp"
using namespace generated_cache;using generated_key_blocks::Data;
namespace block=services::azureus_key_block;
namespace lt=libtorrent;
class AzureusIdentityRebindOwnerTests final : public QObject {
 Q_OBJECT
 std::int32_t token(Actor& seed,udp::Peer target){std::optional<udp::DetailedFindNodeOutcome> result;seed.grant([&]{need(bool(seed.transport.allowPeer(target)),"Generated reconnect target");need(bool(seed.transport.requestStoreToken(target,55,this,[&](auto r){result.emplace(std::move(r));},1s)),"Generated reconnect token request");});need(spin([&]{return result.has_value();},1500)&&bool(*result),"Generated reconnect token reply");const auto* response=std::get_if<udp::find::Decoded>(&**result);need(response!=nullptr,"Generated reconnect token kind");return std::get<udp::find::ReplyBody>(response->body()).randomId;}
 void store(Actor& seed,udp::Peer target,std::string key){udp::StoreRequest request;request.randomId=token(seed,target);request.keys={std::move(key)};request.valueSets={{{17,QDateTime::currentMSecsSinceEpoch(),"retained generated remote value",seed.local,0x21,72,-1}}};std::optional<udp::StoreOutcome> reply;seed.grant([&]{need(bool(seed.transport.store(target,55,std::move(request),this,[&](auto r){reply.emplace(std::move(r));},1s)),"Generated pre-rebind STORE");});need(spin([&]{return reply.has_value();},1500)&&bool(*reply)&&std::get_if<udp::storage::Decoded>(&**reply),"Generated pre-rebind STORE reply");}
 udp::DetailedFindValueOutcome lookup(Actor& seed,udp::Peer target,std::string key){nodes::NodeId id;std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()),20,id.begin());std::optional<udp::DetailedFindValueOutcome> reply;seed.grant([&]{need(bool(seed.transport.allowPeer(target)),"Generated rebind read target");need(bool(seed.transport.findValueDetailed(target,55,id,0,8,this,[&](auto r){reply.emplace(std::move(r));},1s)),"Generated rebind read");});need(spin([&]{return reply.has_value();},1500),"Generated rebind read response");return std::move(*reply);}
 std::set<std::uint64_t> completed_;
 bool completed(app::GeneralChats& chats,std::uint64_t id){for(const auto& result:chats.takeResults()){need(!result.failure,"Generated chat command completion");completed_.insert(result.request);}return completed_.contains(id);}
private slots:
 void actualInterfaceAndPortChangesRetainCacheTranscriptAndIdentityOrder(){
  Actor seed("127.171.75.1",20);Data data;const std::array records{block::Record{data.request,data.signature,std::int32_t(QDateTime::currentSecsSinceEpoch()),true}};auto empty=core::Document::parse("de");QVERIFY(empty);auto signedMap=block::encodeSaved(empty->root(),records);QVERIFY(signedMap);
  generated_chat::GeneratedClient target(QHostAddress("127.172.75.1"),seed.transport.endpoint(),false,false,0ms,0ms,{},engine::OwnedKeyBlockFixtureKey{data.modulus,data.exponent},*signedMap,{},50ms);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);const auto generation=target.chats->snapshot()->transports[0]->transport().generation();udp::Peer current{QHostAddress("127.172.75.1"),target.port};const std::string key(20,'Z');store(seed,current,key);
  const std::string room="Generated reconnect no-history";auto joined=target.chats->joinRoom(room);QVERIFY(joined);QTRY_VERIFY_WITH_TIMEOUT(completed(*target.chats,*joined),3000);auto selected=target.chats->selectRoom(room);QVERIFY(selected);QTRY_VERIFY_WITH_TIMEOUT(completed(*target.chats,*selected),3000);QTRY_VERIFY_WITH_TIMEOUT(!target.chats->snapshot()->rooms.empty()&&target.chats->snapshot()->rooms[0].canSend,3000);QVERIFY(!target.chats->snapshot()->rooms[0].settings.saveHistory);auto sent=target.chats->send(room,"retained generated transcript");QVERIFY(sent);QTRY_VERIFY_WITH_TIMEOUT(completed(*target.chats,*sent),3000);QTRY_COMPARE_WITH_TIMEOUT(target.chats->snapshot()->messages.size(),std::size_t{1},3000);const auto message=target.chats->snapshot()->messages[0];
  lt::settings_pack changed;changed.set_str(lt::settings_pack::listen_interfaces,"127.173.75.1:"+std::to_string(target.port));target.sdk->apply_settings(changed);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0]&&target.chats->snapshot()->transports[0]->transport().generation()!=generation,5000);QVERIFY(!target.chats->snapshot()->closing);current.address=QHostAddress("127.173.75.1");auto read=lookup(seed,current,key);QVERIFY(read);const auto* values=std::get_if<udp::lookup::Decoded>(&*read);QVERIFY(values);const auto* found=std::get_if<udp::lookup::FoundValues>(&std::get<udp::lookup::ReplyBody>(values->body()).result);QVERIFY(found&&found->values.size()==1);QCOMPARE(found->values[0].bytes,std::string("retained generated remote value"));QCOMPARE(found->values[0].originator.address,seed.local.address);QTRY_COMPARE_WITH_TIMEOUT(target.chats->snapshot()->messages.size(),std::size_t{1},2000);QCOMPARE(target.chats->snapshot()->messages[0].id,message.id);
  QFile blocks(target.root.path()+"/dht/net4/block.dat");QVERIFY(blocks.open(QIODevice::ReadOnly));auto blockDoc=core::Document::parse(blocks.readAll().toStdString());QVERIFY(blockDoc);auto after=block::decodeSaved(blockDoc->root(),QDateTime::currentMSecsSinceEpoch(),data.verifier());QVERIFY(after);QVERIFY(after->records.empty());QFile addressFile(target.root.path()+"/dht/net4/addresses.dat");QVERIFY(addressFile.open(QIODevice::ReadOnly));auto addressDoc=core::Document::parse(addressFile.readAll().toStdString());QVERIFY(addressDoc);QCOMPARE(addressDoc->find("most_recent")->bytes,std::string_view("127.173.75.1"));
  const auto blockToken=token(seed,current);std::optional<udp::KeyBlockOutcome> freshBlock;seed.grant([&]{QVERIFY(seed.transport.sendKeyBlock(current,55,{blockToken,data.request,data.signature},this,[&](auto response){freshBlock.emplace(std::move(response));},2s));});QTRY_VERIFY_WITH_TIMEOUT(freshBlock,3000);QVERIFY(*freshBlock);QVERIFY(std::get_if<block::Decoded>(&**freshBlock));
  const auto reboundGeneration=target.chats->snapshot()->transports[0]->transport().generation();QUdpSocket reserve;QVERIFY(reserve.bind(current.address,0));const auto nextPort=reserve.localPort();reserve.close();auto config=storage::ConfigFile::load(*target.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().replaceValue("UDP.Listen.Port","i"+std::to_string(nextPort)+"e"));QVERIFY(config->save(*target.lease));changed.set_str(lt::settings_pack::listen_interfaces,"127.173.75.1:"+std::to_string(nextPort));target.sdk->apply_settings(changed);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0]&&target.chats->snapshot()->transports[0]->transport().generation()!=reboundGeneration,5000);current.port=nextPort;auto second=lookup(seed,current,key);QVERIFY(second);QVERIFY(std::get_if<udp::lookup::Decoded>(&*second));QTRY_COMPARE_WITH_TIMEOUT(target.chats->snapshot()->messages.size(),std::size_t{1},2000);QCOMPARE(target.chats->snapshot()->messages[0].id,message.id);QVERIFY(!target.chats->snapshot()->closing);QFile afterPort(target.root.path()+"/dht/net4/block.dat");QVERIFY(afterPort.open(QIODevice::ReadOnly));auto portBlockDoc=core::Document::parse(afterPort.readAll().toStdString());QVERIFY(portBlockDoc);auto portBlocks=block::decodeSaved(portBlockDoc->root(),QDateTime::currentMSecsSinceEpoch(),data.verifier());QVERIFY(portBlocks&&portBlocks->records.empty());
 }
 void aSlowBindingQueryKeepsTheRoomAndListener(){
  Actor seed("127.178.75.1",20);
  generated_chat::GeneratedClient target(QHostAddress("127.179.75.1"),seed.transport.endpoint(),
      false,false,0ms,0ms,{},{},{},{},50ms);
  const std::string room="Generated room with delayed UDP query";
  QVERIFY(target.chats->joinRoom(room));
  const auto roomReady=[&] {
    const auto state=target.chats->snapshot();
    return state->transports[0] && !state->rooms.empty() && state->rooms[0].canSend;
  };
  QTRY_VERIFY_WITH_TIMEOUT(roomReady(),5000);
  const auto generation=target.chats->snapshot()->transports[0]->transport().generation();
  struct Pause final:lt::plugin {
    std::promise<void> entered;
    std::promise<void> resume;
    std::future<void> released=resume.get_future();
    void added(const lt::session_handle&) override {
      entered.set_value();
      static_cast<void>(released.wait_for(4s));
    }
  };
  const auto pause=std::make_shared<Pause>();
  auto entered=pause->entered.get_future();
  auto unblock=qScopeGuard([&]{pause->resume.set_value();});
  target.sdk->add_extension(pause);
  QTRY_VERIFY_WITH_TIMEOUT(entered.wait_for(0ms)==std::future_status::ready,2000);
  // The 50 ms binding interval reaches the blocked SDK for multiple queries.
  QTest::qWait(1200);
  pause->resume.set_value();
  unblock.dismiss();
  static_cast<void>(token(seed,{QHostAddress("127.179.75.1"),target.port}));
  QVERIFY(roomReady());
  const auto sent=target.chats->send(room,"Generated message after the delayed binding query");
  QVERIFY(sent);
  QTRY_VERIFY_WITH_TIMEOUT(completed(*target.chats,*sent),3000);
  const auto current=target.chats->snapshot();
  QVERIFY(current->transports[0]);
  QCOMPARE(current->transports[0]->transport().generation(),generation);
 }
 void addingIpv6ListenerCreatesAnActualSecondLane(){Actor seed("127.176.75.1",20);generated_chat::GeneratedClient target(QHostAddress("127.177.75.1"),seed.transport.endpoint(),false,false,0ms,0ms,{},{},{},{},50ms,true);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);QVERIFY(!target.chats->snapshot()->transports[1]);Actor v6("::1",20,(target.port+1)%8);lt::settings_pack changed;changed.set_str(lt::settings_pack::listen_interfaces,"127.177.75.1:"+std::to_string(target.port)+",[::1]:"+std::to_string(target.port));target.sdk->apply_settings(changed);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[1],5000);QCOMPARE(target.chats->snapshot()->transports[1]->transport().family(),udp::TransportStatisticsFamily::IPv6);static_cast<void>(token(v6,{QHostAddress("::1"),target.port}));QVERIFY(!target.chats->snapshot()->closing);}
 void autoSpeedUsesFreshProbesAcrossListenerReplacement(){
  Actor ipv4("127.180.75.1",20);
  const QHostAddress local4("127.181.75.1");
  generated_chat::GeneratedClient target(local4,ipv4.transport.endpoint(),false,false,0ms,0ms,
      {},{},{},{},50ms,true);
  Actor ipv6("::1",20,(target.port+1)%8);
  const auto address6=ipv6.local.address;
  auto cache=storage::AzureusContacts::open(*target.lease,3,16,storage::AzureusContactScope::OwnedLoopback);
  QVERIFY(cache);
  const storage::AzureusSavedContact saved{1000,55,address6.length,address6.bytes,address6.port};
  QVERIFY((*cache)->save(std::span(&saved,1)));

  using Family=udp::TransportStatisticsFamily;
  constexpr std::uint64_t probeEpoch=71;
  const auto usable=[&](Family family){
    const auto current=target.chats->snapshot()->autoSpeedProbe;
    return current&&current->epoch==probeEpoch&&current->family==family
        &&std::ranges::any_of(current->rtt,[](auto value){return value>=0;});
  };
  QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);
  static_cast<void>(token(ipv4,{local4,target.port}));
  target.chats->setAutoSpeedProbeEpoch(probeEpoch);
  QTRY_VERIFY_WITH_TIMEOUT(usable(Family::IPv4),10000);
  const auto first=*target.chats->snapshot()->autoSpeedProbe;

  lt::settings_pack changed;
  changed.set_str(lt::settings_pack::listen_interfaces,"");
  target.sdk->apply_settings(changed);
  QTRY_VERIFY_WITH_TIMEOUT(!target.chats->snapshot()->transports[0]
      &&!target.chats->snapshot()->autoSpeedProbe,5000);
  QTest::qWait(100);
  QVERIFY(!target.chats->snapshot()->autoSpeedProbe);

  const auto local6="[::1]:"+std::to_string(target.port);
  changed.set_str(lt::settings_pack::listen_interfaces,local6);
  target.sdk->apply_settings(changed);
  QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[1],5000);
  static_cast<void>(token(ipv6,{QHostAddress::LocalHostIPv6,target.port}));
  QTRY_VERIFY_WITH_TIMEOUT(usable(Family::IPv6),10000);
  const auto fallback=*target.chats->snapshot()->autoSpeedProbe;
  QVERIFY(fallback.sequence>first.sequence && fallback.route!=first.route);
  QCOMPARE(fallback.replacements,0u);

  changed.set_str(lt::settings_pack::listen_interfaces,
      local4.toString().toStdString()+":"+std::to_string(target.port)+","+local6);
  target.sdk->apply_settings(changed);
  QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0]
      &&target.chats->snapshot()->transports[1],5000);
  static_cast<void>(token(ipv4,{local4,target.port}));
  QTRY_VERIFY_WITH_TIMEOUT(usable(Family::IPv4),10000);
  const auto restored=*target.chats->snapshot()->autoSpeedProbe;
  QVERIFY(restored.sequence>fallback.sequence && restored.route!=fallback.route);
  QCOMPARE(restored.replacements,0u);
 }
 void stopWhileListenerIsUnavailableDoesNotWaitForRetry(){Actor seed("127.174.75.1",20);generated_chat::GeneratedClient target(QHostAddress("127.175.75.1"),seed.transport.endpoint(),false,false,0ms,0ms,{},{},{},{},50ms);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);lt::settings_pack changed;changed.set_str(lt::settings_pack::listen_interfaces,"");target.sdk->apply_settings(changed);QTRY_VERIFY_WITH_TIMEOUT(!target.chats->snapshot()->transports[0],3000);QElapsedTimer elapsed;elapsed.start();target.chats->requestStop();target.chats->join();QVERIFY(elapsed.elapsed()<2000);QVERIFY(target.chats->snapshot()->closed);}
};
QTEST_GUILESS_MAIN(AzureusIdentityRebindOwnerTests)
#include "azureus_identity_rebind_owner_tests.moc"
