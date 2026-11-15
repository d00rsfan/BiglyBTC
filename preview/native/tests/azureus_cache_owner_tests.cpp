// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
#include "chat_owner_fixture.hpp"
#include <QFile>
using namespace generated_cache;
class AzureusCacheOwnerTests final:public QObject {
 Q_OBJECT
private slots:
 void sourceBrokeredCacheForwardingHonoursCurrentProfile_data(){QTest::addColumn<bool>("revoked");QTest::newRow("actual-generated-profile")<<false;QTest::newRow("profile-disabled-before-age")<<true;}
 void sourceBrokeredCacheForwardingHonoursCurrentProfile(){
  QFETCH(bool,revoked);Actor seed("127.145.81.1",20);generated_chat::GeneratedClient target(QHostAddress("127.146.81.1"),seed.transport.endpoint(),false,false,500ms);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);const udp::Peer destination{QHostAddress("127.146.81.1"),target.port};const auto local=contact(destination);seed.grant([&]{QVERIFY(seed.transport.attachKnownPeer(destination,55,std::chrono::steady_clock::now()+3s));QVERIFY(seed.transport.integrateKnownRouting(2s));});QTRY_VERIFY_WITH_TIMEOUT(seed.transport.routingSnapshot()&&seed.transport.routingSnapshot()->finished&&seed.transport.routingSnapshot()->seeded,3000);
  const std::string key(reinterpret_cast<const char*>(local.id.data()),local.id.size());std::optional<udp::DetailedFindNodeOutcome> token;seed.grant([&]{QVERIFY(seed.transport.requestStoreToken(destination,55,this,[&](auto reply){token.emplace(std::move(reply));},2s));});QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(*token);const auto* decoded=std::get_if<udp::find::Decoded>(&**token);QVERIFY(decoded);udp::StoreRequest request;request.randomId=std::get<udp::find::ReplyBody>(decoded->body()).randomId;request.keys={key};request.valueSets={{{9,QDateTime::currentMSecsSinceEpoch(),"generated profile cache value",seed.local,0x21,72,-1}}};std::optional<udp::StoreOutcome> stored;seed.grant([&]{QVERIFY(seed.transport.store(destination,55,std::move(request),this,[&](auto reply){stored.emplace(std::move(reply));},2s));});QTRY_VERIFY_WITH_TIMEOUT(stored.has_value(),3000);QVERIFY(*stored);
  if(revoked){auto access=target.lease->sourceAccess(true);QVERIFY(access);auto config=storage::ConfigFile::load(*target.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().replaceValue("Plugin.DHT.dht.enabled","i0e"));QVERIFY(config->save(*target.lease));}
  if(revoked){QTest::qWait(1200);QCOMPARE(seed.database->snapshot()->values,std::size_t{0});}
  else{QTRY_COMPARE_WITH_TIMEOUT(seed.database->snapshot()->values,std::size_t{1},8000);Clock clock;auto result=seed.database->read(key,local,32,0,clock);QVERIFY(result&&*result);QCOMPARE((**result).values.size(),std::size_t{1});QCOMPARE((**result).values[0].bytes,std::string("generated profile cache value"));QCOMPARE((**result).values[0].version,9);QCOMPARE((**result).values[0].originator.id,seed.local.id);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->cacheMaintenance[0].operation>0,2000);}
  target.chats->requestStop();target.chats->join();QVERIFY(target.chats->snapshot()->closed);const auto received=seed.transport.statistics().received;QTest::qWait(50);QCOMPARE(seed.transport.statistics().received,received);
 }
};
QTEST_GUILESS_MAIN(AzureusCacheOwnerTests)
#include "azureus_cache_owner_tests.moc"
