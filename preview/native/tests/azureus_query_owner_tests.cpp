// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
#include "chat_owner_fixture.hpp"
using namespace generated_cache;
class AzureusQueryOwnerTests final:public QObject {Q_OBJECT
private slots:
 void actualSourceBrokerRechecksGeneratedProfile_data(){QTest::addColumn<bool>("revoked");QTest::newRow("normal-profile-query")<<false;QTest::newRow("dht-disabled-before-query")<<true;}
 void actualSourceBrokerRechecksGeneratedProfile(){
  QFETCH(bool,revoked);Actor seed("127.147.81.1",20);generated_chat::GeneratedClient target(QHostAddress("127.148.81.1"),seed.transport.endpoint());QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);const udp::Peer destination{QHostAddress("127.148.81.1"),target.port};const auto local=contact(destination);seed.grant([&]{QVERIFY(seed.transport.attachKnownPeer(destination,55,std::chrono::steady_clock::now()+3s));QVERIFY(seed.transport.integrateKnownRouting(2s));});QTRY_VERIFY_WITH_TIMEOUT(seed.transport.routingSnapshot()&&seed.transport.routingSnapshot()->finished&&seed.transport.routingSnapshot()->seeded,3000);
  const std::string key(reinterpret_cast<const char*>(local.id.data()),local.id.size());std::optional<udp::DetailedFindNodeOutcome> token;seed.grant([&]{QVERIFY(seed.transport.requestStoreToken(destination,55,this,[&](auto out){token.emplace(std::move(out));},2s));});QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(*token);const auto* decoded=std::get_if<udp::find::Decoded>(&**token);QVERIFY(decoded);udp::StoreRequest request;request.randomId=std::get<udp::find::ReplyBody>(decoded->body()).randomId;request.keys={key};request.valueSets={{{3,QDateTime::currentMSecsSinceEpoch(),"generated profile query value",seed.local,0x21,72,0x12}}};std::optional<udp::StoreOutcome> stored;seed.grant([&]{QVERIFY(seed.transport.store(destination,55,std::move(request),this,[&](auto out){stored.emplace(std::move(out));},2s));});QTRY_VERIFY_WITH_TIMEOUT(stored.has_value(),3000);QVERIFY(*stored);
  if(revoked){auto access=target.lease->sourceAccess(true);QVERIFY(access);auto config=storage::ConfigFile::load(*target.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().replaceValue("Plugin.DHT.dht.enabled","i0e"));QVERIFY(config->save(*target.lease));}
  udp::query::RequestBody query;query.groups={{"",{key.substr(0,6),std::string(6,'X'),key.substr(0,6)}}};std::optional<udp::QueryStoreOutcome> result;seed.grant([&]{QVERIFY(seed.transport.queryStorage(destination,55,query,this,[&](auto out){result.emplace(std::move(out));},700ms));});QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1500);
  if(revoked){QVERIFY(!*result);}else{QVERIFY(*result);const auto* response=std::get_if<udp::query::Decoded>(&**result);QVERIFY(response);const auto& rows=std::get<udp::query::ReplyBody>(response->body()).entries;QCOMPARE(rows.size(),std::size_t{3});QCOMPARE(rows[0],std::optional<std::string>(key.substr(18,2)));QVERIFY(!rows[1]);QCOMPARE(rows[2],rows[0]);}
  target.chats->requestStop();target.chats->join();QVERIFY(target.chats->snapshot()->closed);const auto received=seed.transport.statistics().received;QTest::qWait(20);QCOMPARE(seed.transport.statistics().received,received);
 }
};
QTEST_GUILESS_MAIN(AzureusQueryOwnerTests)
#include "azureus_query_owner_tests.moc"
