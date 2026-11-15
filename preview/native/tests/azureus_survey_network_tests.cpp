// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
using namespace generated_cache;
namespace {
void populate(Fixture& f,std::size_t values=1){Clock clock;std::vector<services::azureus_values::Value> entries;for(std::size_t i=0;i<values;++i){auto origin=i%2?f.c.local:f.b.local;entries.push_back({std::int32_t(i+1),QDateTime::currentMSecsSinceEpoch(),std::string(512,char('a'+i)),origin,0x21,72,3});}need(bool(f.a.database->store(f.key,f.b.local,entries,clock)),"Generated explicit survey values");}
std::uint64_t start(Fixture& f,std::chrono::milliseconds duration=5s){std::uint64_t operation=0;f.a.grant([&]{auto result=f.a.transport.surveyCachedValues(duration);need(bool(result),"Generated seeded survey start");operation=*result;});return operation;}
}
class AzureusSurveyNetworkTests final:public QObject {
 Q_OBJECT
private slots:
 void realQueriesRepairMissingCopies_data(){QTest::addColumn<bool>("ipv6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void realQueriesRepairMissingCopies(){
  QFETCH(bool,ipv6);Fixture f(20,ipv6);populate(f,2);const auto reads=f.a.database->snapshot()->reads;start(f);QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.surveyMaintenanceFacts().active,6500);const auto facts=f.a.transport.surveyMaintenanceFacts();QVERIFY(!facts.failure);QCOMPARE(facts.eligible,std::uint64_t{1});QVERIFY(facts.lookups>=1);QCOMPARE(facts.queries,std::uint64_t{2});QCOMPARE(facts.replies,std::uint64_t{2});QCOMPARE(facts.tokens,std::uint64_t{2});QCOMPARE(facts.packets,std::uint64_t{2});QCOMPARE(facts.values,std::uint64_t{4});QCOMPARE(f.a.database->snapshot()->reads,reads);
  for(auto* recipient:{&f.b,&f.c}){Clock clock;auto result=recipient->database->read(f.key,f.a.local,32,0,clock);QVERIFY(result&&*result);QCOMPARE((**result).values.size(),std::size_t{2});for(const auto& value:(**result).values){QCOMPARE(value.flags,std::uint8_t{0x21});QCOMPARE(value.lifeHours,std::uint8_t{72});QCOMPARE(value.replication,std::int8_t{3});QCOMPARE(value.bytes.size(),std::size_t{512});}}
  start(f);QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.surveyMaintenanceFacts().active,6500);const auto warm=f.a.transport.surveyMaintenanceFacts();QVERIFY(!warm.failure);QCOMPARE(warm.queries,std::uint64_t{2});QCOMPARE(warm.packets,std::uint64_t{0});QCOMPARE(warm.tokens,std::uint64_t{0});
 }

 void actualRemoteRestartInvalidatesWarmKnowledge(){
  Fixture f;populate(f,2);start(f);QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.surveyMaintenanceFacts().active,6500);QVERIFY(!f.a.transport.surveyMaintenanceFacts().failure);const auto old=f.b.transport.endpoint();f.b.transport.close();Actor replacement("127.142.71.1",20,0,old.port,99);QCOMPARE(replacement.local.id,f.b.local.id);
  replacement.grant([&]{QVERIFY(replacement.transport.attachKnownPeer(f.a.transport.endpoint(),55,std::chrono::steady_clock::now()+3s));QVERIFY(replacement.transport.attachKnownPeer(f.c.transport.endpoint(),55,std::chrono::steady_clock::now()+3s));QVERIFY(replacement.transport.integrateKnownRouting(2s));});QTRY_VERIFY_WITH_TIMEOUT(replacement.transport.routingSnapshot()&&replacement.transport.routingSnapshot()->finished&&replacement.transport.routingSnapshot()->seeded,3000);
  start(f);QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.surveyMaintenanceFacts().active,6500);const auto facts=f.a.transport.surveyMaintenanceFacts();QVERIFY(!facts.failure);QCOMPARE(facts.queries,std::uint64_t{2});QCOMPARE(facts.packets,std::uint64_t{1});QCOMPARE(facts.tokens,std::uint64_t{1});QCOMPARE(replacement.database->snapshot()->values,std::size_t{2});
 }
 void unseededActorsCannotInventSurveyAuthority(){
  Actor actor("127.151.71.1",20);actor.grant([&]{auto denied=actor.transport.surveyCachedValues(1s);QVERIFY(!denied&&denied.error()==udp::Failure::Authority);});QCOMPARE(actor.transport.surveyMaintenanceFacts().operation,std::uint64_t{0});QCOMPARE(actor.transport.statistics().sent,std::uint64_t{0});
 }
 void defaultValuesStayOnTheOriginalCachePath(){
  Fixture f;Clock clock;const services::azureus_values::Value value{1,QDateTime::currentMSecsSinceEpoch(),"default",f.b.local,0,72,-1};QVERIFY(f.a.database->store(f.key,f.b.local,std::span(&value,1),clock));start(f);QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.surveyMaintenanceFacts().active,500);QCOMPARE(f.a.transport.surveyMaintenanceFacts().eligible,std::uint64_t{0});QCOMPARE(f.a.transport.surveyMaintenanceFacts().queries,std::uint64_t{0});QCOMPARE(f.a.transport.surveyMaintenanceFacts().packets,std::uint64_t{0});QCOMPARE(f.b.database->snapshot()->values,std::size_t{0});
 }
 void currentKeyRevocationPreventsRepairPackets(){
  Fixture f;populate(f);f.a.keyGate=[](std::string,auto done){done(false);return true;};start(f);QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.surveyMaintenanceFacts().active,6500);QVERIFY(f.a.transport.surveyMaintenanceFacts().failure.has_value());QCOMPARE(f.a.transport.surveyMaintenanceFacts().packets,std::uint64_t{0});QCOMPARE(f.b.database->snapshot()->values,std::size_t{0});QCOMPARE(f.c.database->snapshot()->values,std::size_t{0});
 }
 void heldSourceTimesOutWithoutSendingAndLateGrantIsInert(){
  Fixture f;populate(f);f.a.hold=true;const auto before=f.a.transport.statistics().sent;start(f,300ms);QTRY_VERIFY_WITH_TIMEOUT(!f.a.pending.empty(),500);QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.surveyMaintenanceFacts().active,1000);QVERIFY(f.a.transport.surveyMaintenanceFacts().failure==udp::Failure::Timeout);QCOMPARE(f.a.transport.statistics().sent,before);for(auto& late:f.a.pending){f.a.grant([&]{late(true);});}QTest::qWait(10);QCOMPARE(f.a.transport.statistics().sent,before);
 }
 void cancelAndCloseReleaseTheirActualPendingWork(){
  Fixture f;populate(f);f.a.hold=true;auto operation=start(f);QTRY_VERIFY_WITH_TIMEOUT(!f.a.pending.empty(),500);QVERIFY(!f.a.transport.cancelSurveyValues(operation+1));QVERIFY(f.a.transport.cancelSurveyValues(operation));QVERIFY(!f.a.transport.surveyMaintenanceFacts().active);for(auto& late:f.a.pending){f.a.grant([&]{late(true);});}f.a.pending.clear();f.a.hold=false;operation=start(f);QVERIFY(operation>1);f.a.transport.close();QVERIFY(!f.a.transport.surveyMaintenanceFacts().active);QVERIFY(f.a.transport.surveyMaintenanceFacts().failure==udp::Failure::Closed);
 }
 void startupDisableGateCannotPerformMaintenance(){
  udp::Config config;config.routingScope=udp::RoutingScope::OwnedLoopback;udp::OwnedPingTransport actor(std::move(config));QVERIFY(actor.start(QHostAddress::LocalHost,0));auto result=actor.surveyCachedValues(1s);QVERIFY(!result&&result.error()==udp::Failure::State);QCOMPARE(actor.surveyMaintenanceFacts().operation,std::uint64_t{0});QCOMPARE(actor.statistics().sent,std::uint64_t{0});actor.close();
 }
};
QTEST_GUILESS_MAIN(AzureusSurveyNetworkTests)
#include "azureus_survey_network_tests.moc"
