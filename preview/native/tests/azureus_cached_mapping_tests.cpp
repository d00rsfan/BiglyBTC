// SPDX-License-Identifier: GPL-2.0-or-later
#include "database.hpp"
#include <QTest>
using namespace biglybtc;namespace db=services::azureus_db_runtime;namespace nodes=services::azureus_nodes;namespace stats=services::azureus_storage_stats;
namespace {
struct Clock final:db::lifecycle::WallClock {std::int64_t now=1000;unsigned calls=0;std::int64_t nowMilliseconds()noexcept override{++calls;return now;}};
struct Inputs final:stats::DiversificationControl {std::expected<std::int64_t,stats::Error> expiryJitterMilliseconds()noexcept override{return 0;}std::int64_t suspendedUntilMilliseconds()const noexcept override{return 0;}};
nodes::Contact contact(unsigned n){nodes::ResolvedEndpoint ep;ep.address.length=4;ep.address.bytes[0]=1;ep.address.bytes[1]=std::uint8_t(n);ep.address.bytes[3]=1;ep.address.port=4000;nodes::NodeCache cache;auto id=cache.lookup(ep,55);if(!id){throw std::runtime_error("Generated cache contact");}nodes::Contact c;c.protocol=55;c.address=ep.address;c.id=id->id;return c;}
struct Fixture {Clock clock;std::unique_ptr<db::Owner> owner;std::string key=std::string(20,'P');nodes::Contact local=contact(1),remote=contact(2);Fixture(){db::Limits limits;limits.storageStats=db::StorageStatsMode::OriginalDiversification;limits.diversificationControl=std::make_shared<Inputs>();auto made=db::Owner::create(limits);if(!made){throw std::runtime_error("Generated cache DB");}owner=std::move(*made);if(!owner->restoreLocalDiversifications({},clock.now)||!owner->bindLocal(local)){throw std::runtime_error("Generated cache bind");}}
 services::azureus_values::Value value(std::string bytes="generated",std::int32_t version=1,std::int8_t rep=-1,nodes::Contact origin=contact(2)){return {version,clock.now,std::move(bytes),origin,0x21,72,rep};}
 void store(services::azureus_values::Value value){if(!owner->store(key,value.originator,std::span(&value,1),clock)){throw std::runtime_error("Generated cache store");}}
};
}
class AzureusCachedMappingTests final:public QObject {
 Q_OBJECT
private slots:
 void futureClockResetsTheActualEntryWithoutASecondRead(){
  Fixture f;f.store(f.value());f.clock.now=999;const auto calls=f.clock.calls;auto result=f.owner->prepareCachedRepublish(999,1000,f.clock);QVERIFY(result&&result->empty());QCOMPARE(f.clock.calls,calls+1);f.clock.now=1999;result=f.owner->prepareCachedRepublish(1999,1000,f.clock);QVERIFY(result&&result->empty());f.clock.now=2000;result=f.owner->prepareCachedRepublish(2000,1000,f.clock);QVERIFY(result&&result->size()==1);QCOMPARE((*result)[0].values.size(),std::size_t{1});QCOMPARE((*result)[0].values[0].storeTime(),std::int64_t{999});QCOMPARE((*result)[0].values[0].version(),1);QCOMPARE(f.owner->snapshot()->reads,std::uint64_t{0});
 }
 void rawCandidatesRetainDuplicateAndEmptyValues(){
  Fixture f;f.store(f.value("same",1,-1,contact(2)));f.store(f.value("same",2,-1,contact(3)));f.store(f.value("",3,-1,contact(4)));f.clock.now=3000;auto result=f.owner->prepareCachedRepublish(3000,1000,f.clock);QVERIFY(result&&result->size()==1);QCOMPARE((*result)[0].values.size(),std::size_t{3});QCOMPARE((*result)[0].values[0].payload().view()->size(),std::size_t{4});QCOMPARE((*result)[0].values[1].payload().view()->size(),std::size_t{4});QCOMPARE((*result)[0].values[2].payload().view()->size(),std::size_t{0});
 }
 void explicitReplicationRemainsSurveyOnlyUntilMixedWithLocal(){
  Fixture f;f.store(f.value("explicit",1,0));f.clock.now=3000;auto result=f.owner->prepareCachedRepublish(3000,1000,f.clock);QVERIFY(result&&result->size()==1);QVERIFY((*result)[0].surveyOnly);QVERIFY((*result)[0].values.empty());QVERIFY(f.owner->putLocal(f.key,"own",2,f.clock));result=f.owner->prepareCachedRepublish(3000,1000,f.clock);QVERIFY(result&&result->size()==1);QVERIFY(!(*result)[0].surveyOnly);QCOMPARE((*result)[0].values.size(),std::size_t{1});QVERIFY(!(*result)[0].values[0].isLocal());QCOMPARE((*result)[0].values[0].replicationControl(),std::int8_t{0});
 }
 void duplicateVersionResetInvalidatesOnlyTheCacheReceipt(){
  Fixture f;f.store(f.value());f.clock.now=3000;auto result=f.owner->prepareCachedRepublish(3000,1000,f.clock);QVERIFY(result&&result->size()==1);const auto old=(*result)[0].receipt;auto held=(*result)[0].values[0];f.store(f.value("ignored older-version bytes"));auto current=f.owner->cacheReceipt(f.key);QVERIFY(current&&*current);QCOMPARE((**current).content,old.content);QVERIFY((**current).store!=old.store);auto erased=f.owner->eraseCachedMapping(f.key,old);QVERIFY(erased&&!*erased);QCOMPARE(held.storeTime(),std::int64_t{1000});auto recent=f.owner->prepareCachedRepublish(3000,1000,f.clock);QVERIFY(recent&&recent->empty());f.clock.now=5000;auto fresh=f.owner->prepareCachedRepublish(5000,1000,f.clock);QVERIFY(fresh&&fresh->size()==1);QVERIFY(f.owner->eraseCachedMapping(f.key,(*fresh)[0].receipt).value());auto absent=f.owner->cacheReceipt(f.key);QVERIFY(absent&&!absent->has_value());f.store(f.value("recreated",2));QVERIFY(!f.owner->eraseCachedMapping(f.key,(*fresh)[0].receipt).value());QVERIFY(f.owner->contains(f.key).value());
 }
 void removingAnOwnValueKeepsARemoteEmptyEntry(){
  Fixture f;QVERIFY(f.owner->putLocal(f.key,"own",1,f.clock));f.store(f.value("",2));QVERIFY(f.owner->removeLocal(f.key,3,f.clock));QVERIFY(f.owner->contains(f.key).value());QCOMPARE(f.owner->snapshot()->values,std::size_t{1});f.clock.now=3000;auto result=f.owner->prepareCachedRepublish(3000,1000,f.clock);QVERIFY(result&&result->size()==1);QCOMPARE((*result)[0].values.size(),std::size_t{1});QVERIFY((*result)[0].values[0].payload().view()->empty());
 }
 void invalidInputsDoNotSampleClocksOrGrantAReceipt(){
  Fixture f;const auto before=f.clock.calls;QVERIFY(!f.owner->prepareCachedRepublish(1000,-1,f.clock));QCOMPARE(f.clock.calls,before);auto empty=f.owner->cacheReceipt(f.key);QVERIFY(empty&&!empty->has_value());auto absent=db::Owner::create();QVERIFY(absent);QVERIFY(!(*absent)->prepareCachedRepublish(1000,1000,f.clock));QVERIFY(!(*absent)->cacheReceipt(f.key));f.owner->close();QVERIFY(!f.owner->cacheReceipt(f.key));QVERIFY(!f.owner->prepareCachedRepublish(1000,1000,f.clock));QCOMPARE(f.clock.calls,before);
 }
};
QTEST_GUILESS_MAIN(AzureusCachedMappingTests)
#include "azureus_cached_mapping_tests.moc"
