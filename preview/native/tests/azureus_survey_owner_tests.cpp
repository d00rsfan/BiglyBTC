// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
#include "chat_owner_fixture.hpp"
using namespace generated_cache;
class AzureusSurveyOwnerTests final:public QObject {
 Q_OBJECT
private slots:
 void actualProfilePolicyControlsScheduledRepair_data(){QTest::addColumn<bool>("disabled");QTest::newRow("normal-profile-repair")<<false;QTest::newRow("saved-feature32-disabled")<<true;}
 void actualProfilePolicyControlsScheduledRepair(){
  QFETCH(bool,disabled);Actor seed("127.152.81.1",20);generated_chat::GeneratedClient target(QHostAddress("127.153.81.1"),seed.transport.endpoint(),false,false,0ms,350ms,disabled?std::int64_t{32}:std::int64_t{0});QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);QCOMPARE(target.chats->snapshot()->surveysEnabled[0],!disabled);const udp::Peer destination{QHostAddress("127.153.81.1"),target.port};const auto local=contact(destination);
  seed.grant([&]{QVERIFY(seed.transport.attachKnownPeer(destination,55,std::chrono::steady_clock::now()+3s));QVERIFY(seed.transport.integrateKnownRouting(2s));});QTRY_VERIFY_WITH_TIMEOUT(seed.transport.routingSnapshot()&&seed.transport.routingSnapshot()->finished&&seed.transport.routingSnapshot()->seeded,3000);
  const std::string key(reinterpret_cast<const char*>(local.id.data()),local.id.size());std::optional<udp::DetailedFindNodeOutcome> token;seed.grant([&]{QVERIFY(seed.transport.requestStoreToken(destination,55,this,[&](auto result){token.emplace(std::move(result));},2s));});QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(*token);const auto* decoded=std::get_if<udp::find::Decoded>(&**token);QVERIFY(decoded);const auto remoteInstance=decoded->header().fields().instance;
  udp::StoreRequest request;request.randomId=std::get<udp::find::ReplyBody>(decoded->body()).randomId;request.keys={key};request.valueSets={{{7,QDateTime::currentMSecsSinceEpoch(),"generated scheduled repair",seed.local,0x21,72,3}}};std::optional<udp::StoreOutcome> stored;seed.grant([&]{QVERIFY(seed.transport.store(destination,55,std::move(request),this,[&](auto result){stored.emplace(std::move(result));},2s));});QTRY_VERIFY_WITH_TIMEOUT(stored.has_value(),3000);QVERIFY(*stored);
  if(disabled){QTest::qWait(1000);QCOMPARE(target.chats->snapshot()->surveyMaintenance[0].operation,std::uint64_t{0});QCOMPARE(seed.database->snapshot()->values,std::size_t{0});QVERIFY(target.chats->snapshot()->transports[0]);}
  else{QTRY_COMPARE_WITH_TIMEOUT(seed.database->snapshot()->values,std::size_t{1},6500);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->surveyMaintenance[0].operation>0,2000);Clock clock;auto cycle=seed.database->beginSurvey(clock);QVERIFY(cycle);auto page=seed.database->surveyPage(cycle->cycle,0);QVERIFY(page&&page->mappings.size()==1&&page->mappings[0].values.size()==1);const auto& value=page->mappings[0].values[0];QCOMPARE(*value.payload().view(),std::string_view("generated scheduled repair"));QCOMPARE(value.version(),7);QCOMPARE(value.originator().get()->id,seed.local.id);QCOMPARE(value.sender().get()->id,local.id);QCOMPARE(value.sender().get()->instance,remoteInstance);QVERIFY(seed.database->endSurvey(cycle->cycle));}
  target.chats->requestStop();target.chats->join();QVERIFY(target.chats->snapshot()->closed);const auto received=seed.transport.statistics().received;QTest::qWait(30);QCOMPARE(seed.transport.statistics().received,received);
 }
};
QTEST_GUILESS_MAIN(AzureusSurveyOwnerTests)
#include "azureus_survey_owner_tests.moc"
