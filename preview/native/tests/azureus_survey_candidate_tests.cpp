// SPDX-License-Identifier: GPL-2.0-or-later
#include "database.hpp"
#include <QTest>
#include <set>
namespace db=biglybtc::services::azureus_db_runtime;namespace nodes=biglybtc::services::azureus_nodes;namespace stats=biglybtc::services::azureus_storage_stats;
namespace {
struct Clock final:db::lifecycle::WallClock {std::int64_t now=1000;unsigned calls=0;std::int64_t nowMilliseconds()noexcept override{++calls;return now;}};
struct Inputs final:stats::DiversificationControl {std::expected<std::int64_t,stats::Error> expiryJitterMilliseconds()noexcept override{return 0;}std::int64_t suspendedUntilMilliseconds()const noexcept override{return 0;}};
nodes::Contact contact(unsigned n){nodes::ResolvedEndpoint ep;ep.address.bytes[0]=1;ep.address.bytes[1]=std::uint8_t(n);ep.address.bytes[3]=1;ep.address.port=4000;nodes::NodeCache cache;auto id=cache.lookup(ep,55);if(!id){throw std::runtime_error("Generated survey contact");}nodes::Contact c;c.protocol=55;c.address=ep.address;c.id=id->id;return c;}
struct Fixture {
 Clock clock;std::unique_ptr<db::Owner> owner;std::string key=std::string(20,'P');nodes::Contact local=contact(1),remote=contact(2);
 Fixture(){db::Limits limits;limits.maximumKeys=128;limits.maximumEntriesPerKey=256;limits.storageStats=db::StorageStatsMode::OriginalDiversification;limits.diversificationControl=std::make_shared<Inputs>();auto made=db::Owner::create(limits);if(!made){throw std::runtime_error("Generated survey DB");}owner=std::move(*made);if(!owner->bindLocal(local)){throw std::runtime_error("Generated survey binding");}}
 biglybtc::services::azureus_values::Value value(std::string bytes="value",std::int32_t version=1,std::int8_t replication=3,nodes::Contact origin=contact(2)){return {version,1000,std::move(bytes),origin,0x21,72,replication};}
 void store(const biglybtc::services::azureus_values::Value& value){if(!owner->store(key,remote,std::span(&value,1),clock)){throw std::runtime_error("Generated survey store");}}
};
}
class AzureusSurveyCandidateTests final:public QObject {
 Q_OBJECT
private slots:
 void aLargeNamespaceUsesBoundedPagesAndOneExpiryClock(){
  Fixture f;std::vector<biglybtc::services::azureus_values::Value> values;for(unsigned n=2;n<131;++n){values.push_back(f.value("",1,3,contact(n)));}for(unsigned n=0;n<65;++n){auto key=std::string(20,'K');key[0]=char(n+1);QVERIFY(f.owner->store(key,f.remote,values,f.clock));}
  const auto before=f.clock.calls;auto start=f.owner->beginSurvey(f.clock);QVERIFY(start);QCOMPARE(start->live.size(),std::size_t{65});QCOMPARE(f.clock.calls,before+1);std::size_t cursor=0,total=0;std::set<std::uint64_t> identities;
  for(;;){auto page=f.owner->surveyPage(start->cycle,cursor,8,256);QVERIFY(page);QVERIFY(page->next>cursor);cursor=page->next;std::size_t held=0;for(const auto& mapping:page->mappings){QVERIFY(identities.insert(mapping.subject.incarnation).second);QCOMPARE(mapping.values.size(),std::size_t{129});for(const auto& value:mapping.values){QVERIFY(value.payload().view()->empty());QVERIFY(!value.isLocal());}held+=mapping.values.size();}QVERIFY(held<=256);total+=held;if(!page->more){break;}}
  QCOMPARE(total,std::size_t{8385});QCOMPARE(identities.size(),std::size_t{65});QCOMPARE(f.clock.calls,before+1);QCOMPARE(f.owner->snapshot()->reads,std::uint64_t{0});QVERIFY(f.owner->endSurvey(start->cycle));QVERIFY(!f.owner->surveyPage(start->cycle,0));
 }
 void rawLocalDuplicateAndEmptyValuesRemainOwningAfterDeletion(){
  Fixture f;QVERIFY(f.owner->putLocal(f.key,"own",1,f.clock));const std::vector values{f.value("same",1,3,contact(2)),f.value("same",1,3,contact(3)),f.value("",1,3,contact(4))};QVERIFY(f.owner->store(f.key,f.remote,values,f.clock));auto start=f.owner->beginSurvey(f.clock);QVERIFY(start);auto page=f.owner->surveyPage(start->cycle,0);QVERIFY(page&&page->mappings.size()==1);auto frozen=std::move(page->mappings[0]);QCOMPARE(frozen.values.size(),std::size_t{4});unsigned own=0,same=0,empty=0;for(const auto& value:frozen.values){own+=value.isLocal();same+=*value.payload().view()=="same";empty+=value.payload().view()->empty();}QCOMPARE(own,1u);QCOMPARE(same,2u);QCOMPARE(empty,1u);
  auto current=f.owner->surveyMapping(f.key,frozen.subject.incarnation,frozen.receipt);QVERIFY(current&&*current);QVERIFY(f.owner->eraseCachedMapping(f.key,frozen.receipt).value());QVERIFY(!f.owner->surveyMapping(f.key,frozen.subject.incarnation,frozen.receipt)->has_value());f.owner->close();for(const auto& value:frozen.values){QVERIFY(value.payload().view().has_value());}QCOMPARE(frozen.subject.key,f.key);
 }
 void changedDeletedAndRecreatedRecordsCannotReuseFrozenSubjects(){
  Fixture f;std::vector<std::string> keys;for(char c='A';c<='C';++c){f.key=std::string(20,c);keys.push_back(f.key);f.store(f.value());}auto start=f.owner->beginSurvey(f.clock);QVERIFY(start);auto page=f.owner->surveyPage(start->cycle,0,1);QVERIFY(page&&page->mappings.size()==1&&page->more);const auto oldB=f.owner->cacheReceipt(keys[1]);QVERIFY(oldB&&*oldB);const auto oldC=f.owner->mappingIdentity(keys[2]);QVERIFY(oldC&&*oldC);f.clock.now=1001;f.key=keys[1];f.store(f.value("same-version ignored"));QVERIFY(!f.owner->surveyMapping(keys[1],start->live[1].incarnation,**oldB)->has_value());auto receiptC=f.owner->cacheReceipt(keys[2]);QVERIFY(receiptC&&*receiptC);QVERIFY(f.owner->eraseCachedMapping(keys[2],**receiptC).value());f.key=keys[2];f.store(f.value("recreated",2));const auto currentC=f.owner->cacheReceipt(keys[2]);QVERIFY(currentC&&*currentC);QVERIFY(!f.owner->surveyMapping(keys[2],**oldC,**currentC)->has_value());f.key=std::string(20,'D');f.store(f.value("new"));auto remaining=f.owner->surveyPage(start->cycle,page->next);QVERIFY(remaining&&remaining->mappings.empty()&&!remaining->more);QCOMPARE(start->live.size(),std::size_t{3});auto newer=f.owner->beginSurvey(f.clock);QVERIFY(newer&&newer->live.size()==4);QVERIFY(!f.owner->surveyPage(start->cycle,0));QVERIFY(!f.owner->endSurvey(start->cycle));QVERIFY(f.owner->endSurvey(newer->cycle));
 }
 void surveyAndCacheCyclesRemainIndependentWithoutFutureTimeRepair(){
  Fixture f;f.store(f.value("default",1,-1));const auto receipt=f.owner->cacheReceipt(f.key);QVERIFY(receipt&&*receipt);f.clock.now=500;auto survey=f.owner->beginSurvey(f.clock);QVERIFY(survey);auto page=f.owner->surveyPage(survey->cycle,0);QVERIFY(page&&page->mappings.size()==1);QCOMPARE(page->mappings[0].values[0].storeTime(),std::int64_t{1000});QCOMPARE(f.owner->cacheReceipt(f.key),receipt);f.clock.now=3000;auto cache=f.owner->beginCachedRepublish(3000,1000,f.clock);QVERIFY(cache);QVERIFY(f.owner->surveyPage(survey->cycle,0));QVERIFY(f.owner->endSurvey(survey->cycle));auto cached=f.owner->cachedRepublishPage(*cache,0);QVERIFY(cached&&cached->mappings.size()==1);QVERIFY(f.owner->endCachedRepublish(*cache));
 }
 void surveyRespectsTheOriginalNonForcedExpiryInterval(){
  Fixture f;auto value=f.value();value.lifeHours=1;f.store(value);f.clock.now=4501000;QVERIFY(f.owner->expire(f.clock,true));QCOMPARE(f.owner->snapshot()->values,std::size_t{1});++f.clock.now;auto start=f.owner->beginSurvey(f.clock);QVERIFY(start);auto page=f.owner->surveyPage(start->cycle,0);QVERIFY(page&&page->mappings.size()==1);QCOMPARE(page->mappings[0].values.size(),std::size_t{1});QVERIFY(f.owner->endSurvey(start->cycle));QVERIFY(f.owner->expire(f.clock,true));QCOMPARE(f.owner->snapshot()->values,std::size_t{0});
 }
 void cachedDiversificationAndBoundaryRefusalsHaveNoReadEffect(){
  Fixture f;std::vector<biglybtc::services::azureus_values::Value> values;for(unsigned n=2;n<67;++n){values.push_back(f.value(std::string(512,'x'),1,3,contact(n)));}QVERIFY(f.owner->store(f.key,f.remote,values,f.clock));auto start=f.owner->beginSurvey(f.clock);QVERIFY(start);const auto revision=f.owner->localDiversificationRevision();const auto calls=f.clock.calls;auto page=f.owner->surveyPage(start->cycle,0);QVERIFY(page&&page->mappings.size()==1);QCOMPARE(page->mappings[0].diversification,std::int8_t{3});QCOMPARE(page->mappings[0].values.size(),std::size_t{65});QCOMPARE(f.owner->localDiversificationRevision(),revision);QCOMPARE(f.clock.calls,calls);QCOMPARE(f.owner->snapshot()->reads,std::uint64_t{0});QVERIFY(!f.owner->surveyPage(start->cycle,0,0));QVERIFY(!f.owner->surveyPage(start->cycle,0,1,255));QVERIFY(!f.owner->surveyPage(start->cycle,999));QVERIFY(!f.owner->surveyMapping("short",1,{}));QVERIFY(!f.owner->surveyMapping(f.key,0,{}));f.owner->close();QVERIFY(!f.owner->surveyPage(start->cycle,0));QVERIFY(!f.owner->endSurvey(start->cycle));auto absent=db::Owner::create();QVERIFY(absent);QVERIFY(!(*absent)->beginSurvey(f.clock));QCOMPARE(f.clock.calls,calls);
 }
};
QTEST_GUILESS_MAIN(AzureusSurveyCandidateTests)
#include "azureus_survey_candidate_tests.moc"
