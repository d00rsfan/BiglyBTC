// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
using namespace generated_cache;

class AzureusCacheNetworkTests final:public QObject {
 Q_OBJECT
private slots:
 void actualSeededReceiversGetOnlyRemoteValuesInMtuBoundedPackets_data(){QTest::addColumn<bool>("ipv6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void actualSeededReceiversGetOnlyRemoteValuesInMtuBoundedPackets(){
  QFETCH(bool,ipv6);Fixture f(20,ipv6);f.seed(this);f.start();QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.cacheMaintenanceFacts().active,2500);const auto facts=f.a.transport.cacheMaintenanceFacts();QCOMPARE(facts.tokens,std::uint64_t{2});QCOMPARE(facts.packets,std::uint64_t{2});QCOMPARE(facts.values,std::uint64_t{4});QCOMPARE(facts.failed,std::uint64_t{0});QCOMPARE(f.a.database->snapshot()->reads,std::uint64_t{0});QCOMPARE(f.a.database->snapshot()->values,std::size_t{3});Clock clock;for(auto* target:{&f.b,&f.c}){auto read=target->database->read(f.key,f.a.local,32,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).values.size(),std::size_t{2});for(const auto& value:(**read).values){QCOMPARE(value.bytes.size(),std::size_t{512});QVERIFY(value.bytes==std::string(512,'a')||value.bytes==std::string(512,'b'));QCOMPARE(value.flags,std::uint8_t{0x21});QCOMPARE(value.lifeHours,std::uint8_t{72});QCOMPARE(value.replication,std::int8_t{-1});}}
 }
 void withdrawalDuringTheFinalKeyPermitCannotReleaseCapturedValues(){
  Fixture f;f.seed(this);f.a.onKey=[&](std::string_view key){auto receipt=f.a.database->cacheReceipt(key);if(receipt&&*receipt){need(f.a.database->eraseCachedMapping(key,**receipt).value(),"Generated source withdrawal");}};f.start();QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.cacheMaintenanceFacts().active,2500);QCOMPARE(f.a.transport.cacheMaintenanceFacts().packets,std::uint64_t{0});QCOMPARE(f.b.database->snapshot()->values,std::size_t{0});QCOMPARE(f.c.database->snapshot()->values,std::size_t{0});
 }
 void lostPermitCloseExpiryAndForeignCallbacksCannotSend_data(){QTest::addColumn<int>("reason");QTest::newRow("outside-held-permit")<<0;QTest::newRow("source-revoked")<<1;QTest::newRow("closed")<<2;QTest::newRow("expired")<<3;QTest::newRow("foreign-thread")<<4;}
 void lostPermitCloseExpiryAndForeignCallbacksCannotSend(){
  QFETCH(int,reason);Fixture f;f.seed(this);f.a.hold=true;f.start(reason==3?20ms:2s);QCOMPARE(f.a.pending.size(),std::size_t{2});if(reason==1){f.a.current=false;}if(reason==2){f.a.transport.close();}if(reason==3){QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.cacheMaintenanceFacts().active,500);}
  for(auto& done:f.a.pending){if(reason==0){done(true);}else if(reason==4){std::thread thread([&]{done(true);});thread.join();}else{f.a.grant([&]{done(true);});}}QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.cacheMaintenanceFacts().active,1000);QCOMPARE(f.a.transport.cacheMaintenanceFacts().tokens,std::uint64_t{0});QCOMPARE(f.a.transport.cacheMaintenanceFacts().packets,std::uint64_t{0});QCOMPARE(f.b.database->snapshot()->values,std::size_t{0});
 }
 void duplicatePermitCallbacksDoNotDuplicatePackets(){
  Fixture f;f.seed(this);f.a.hold=true;f.start();std::size_t at=0;QDeadlineTimer end(2000);while(f.a.transport.cacheMaintenanceFacts().active&&!end.hasExpired()){while(at<f.a.pending.size()){auto done=std::move(f.a.pending[at++]);f.a.grant([&]{done(true);done(true);});}QTest::qWait(1);}QVERIFY(!f.a.transport.cacheMaintenanceFacts().active);QCOMPARE(f.a.transport.cacheMaintenanceFacts().tokens,std::uint64_t{2});QCOMPARE(f.a.transport.cacheMaintenanceFacts().packets,std::uint64_t{2});
 }
 void lastForwardPrecedesConditionalPurgeAndLateChangesSurvive_data(){QTest::addColumn<bool>("changed");QTest::newRow("original-final-forward-drop")<<false;QTest::newRow("late-new-value-retained")<<true;}
 void lastForwardPrecedesConditionalPurgeAndLateChangesSurvive(){
  // Populate an existing cache directly: new incoming stores correctly refuse
  // keys outside the current closest set. The positive case above covers UDP intake.
  QFETCH(bool,changed);Fixture f(2);f.chooseOutsideKey();Clock initialClock;for(auto* origin:{&f.b,&f.c}){services::azureus_values::Value value{1,QDateTime::currentMSecsSinceEpoch(),std::string(512,origin==&f.b?'a':'b'),origin->local,0x21,72,-1};QVERIFY(f.a.database->store(f.key,origin->local,std::span(&value,1),initialClock));}unsigned permits=0;f.a.onKey=[&](std::string_view key){if(++permits==3&&changed){Clock clock;services::azureus_values::Value value{9,QDateTime::currentMSecsSinceEpoch(),"late current value",f.c.local,0,72,-1};need(bool(f.a.database->store(key,f.c.local,std::span(&value,1),clock)),"Generated late cache replacement");}};f.start();QTRY_VERIFY_WITH_TIMEOUT(!f.a.transport.cacheMaintenanceFacts().active,2500);QCOMPARE(f.a.transport.cacheMaintenanceFacts().packets,std::uint64_t{2});QCOMPARE(f.a.database->contains(f.key).value(),changed);QCOMPARE(f.a.transport.cacheMaintenanceFacts().purged,std::uint64_t(changed?0:1));
 }
};
QTEST_GUILESS_MAIN(AzureusCacheNetworkTests)
#include "azureus_cache_network_tests.moc"
