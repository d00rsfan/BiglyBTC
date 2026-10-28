#include "database.hpp"
#include "storage_stats.hpp"
#include "owned_azureus_service.hpp"
#include "owned_ddb_fixture.hpp"
#include <QtTest>
#include <QDateTime>
#include <QDeadlineTimer>
#include <thread>
#include <algorithm>
#include <limits>
#include <set>
#include <utility>
namespace {
using namespace biglybtc;
namespace db=services::azureus_db_runtime;
namespace udp=services::azureus_udp;
namespace control=services::azureus_control;
using namespace std::chrono_literals;
struct Clock final:db::lifecycle::WallClock {std::int64_t now=1000;std::int64_t nowMilliseconds()noexcept override{return now;}};
services::azureus_nodes::Contact contact(const udp::Peer& peer){
  services::azureus_nodes::NodeCache cache;const auto id=cache.lookup({test::ddbAddress(peer),{}},55);
  if(!id){throw std::runtime_error("Cannot derive actual owned contact");}
  services::azureus_nodes::Contact value;value.protocol=55;value.address=test::ddbAddress(peer);value.id=id->id;return value;
}
services::azureus_values::Value value(services::azureus_nodes::Contact origin,std::string bytes,std::int32_t version=1,std::int64_t created=1000){
  return {version,created,std::move(bytes),origin,0,1,-1};
}
template<class F>bool spin(F ready){QDeadlineTimer deadline(5000);while(!ready()&&!deadline.hasExpired()){QTest::qWait(1);}return ready();}
std::optional<std::int32_t> token(udp::OwnedPingTransport& client,udp::Peer server,QObject* receiver){
  auto result=std::make_shared<std::optional<udp::DetailedFindNodeOutcome>>();
  const auto sent=client.requestStoreToken(server,55,receiver,[result](auto received){result->emplace(std::move(received));},2s);
  if(!sent||!spin([&]{return result->has_value();})||!(*result)->has_value()){return {};}
  const auto* reply=std::get_if<udp::find::Decoded>(&***result);if(!reply){return {};}
  return std::get<udp::find::ReplyBody>(reply->body()).randomId;
}
std::optional<udp::StoreOutcome> store(udp::OwnedPingTransport& client,udp::Peer server,QObject* receiver,
    std::int32_t cookie,const std::string& key,std::vector<services::azureus_values::Value> values){
  auto result=std::make_shared<std::optional<udp::StoreOutcome>>();udp::StoreRequest request;request.randomId=cookie;request.keys={key};request.valueSets={std::move(values)};
  const auto sent=client.store(server,55,std::move(request),receiver,[result](auto received){result->emplace(std::move(received));},2s);
  if(!sent||!spin([&]{return result->has_value();})){return {};}
  return std::move(*result);
}
bool stored(const std::optional<udp::StoreOutcome>& result){return result&&result->has_value()&&std::holds_alternative<udp::storage::Decoded>(**result);}
}
class AzureusDatabaseRuntimeTests final:public QObject {
 Q_OBJECT
private slots:
 void qualifiedMappingOwnsVersionsForwardingAndUnknownBinaryBytes(){
   auto owner=db::Owner::create();QVERIFY(owner);Clock clock;
   auto a=contact({QHostAddress::LocalHost,41001}),b=contact({QHostAddress::LocalHost,41002});
   const std::string key(20,'K'),binary("a\0b\xff",4);
   auto first=value(a,binary,10);QVERIFY((*owner)->store(key,a,{&first,1},clock));
   auto lower=value(a,"lower",9);QVERIFY((*owner)->store(key,a,{&lower,1},clock));
   auto read=(*owner)->read(key,b,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).values.size(),std::size_t{1});QCOMPARE((**read).values.front().bytes,binary);
   clock.now=2000;auto equal=value(a,"different",10);QVERIFY((*owner)->store(key,a,{&equal,1},clock));
   read=(*owner)->read(key,b,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).values.front().bytes,binary);QCOMPARE((**read).values.front().created,std::int64_t{1000});
   // Equal-version reset refreshes store time; creation only clamps backwards.
   clock.now=500;QVERIFY((*owner)->store(key,a,{&equal,1},clock));
   read=(*owner)->read(key,b,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).values.front().bytes,binary);QCOMPARE((**read).values.front().created,std::int64_t{500});clock.now=2000;
   auto forwarded=value(a,"forwarded-newer",11);QVERIFY((*owner)->store(key,b,{&forwarded,1},clock));
   read=(*owner)->read(key,b,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).values.front().bytes,binary);
   auto newer=value(a,"newer",11);QVERIFY((*owner)->store(key,a,{&newer,1},clock));
   read=(*owner)->read(key,b,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).values.front().bytes,std::string("newer"));
   const auto retained=**read;(*owner)->close();QCOMPARE(retained.values.front().bytes,std::string("newer"));QVERIFY(!(*owner)->read(key,b,0,0,clock));
 }
 void normalFindFlagsReturnUnfilteredValuesLikeOriginalMapping(){
   auto owner=db::Owner::create();QVERIFY(owner);Clock clock;
   auto a=contact({QHostAddress::LocalHost,41001});const std::string key(20,'F');auto item=value(a,"downloading-value");item.flags=1;
   QVERIFY((*owner)->store(key,a,{&item,1},clock));
   for(const std::int16_t flags:{0,1,2,3,4,16,128,-32768}){
     const auto result=(*owner)->read(key,a,0,flags,clock);QVERIFY(result&&*result);
     QCOMPARE((**result).values.size(),std::size_t{1});QCOMPARE((**result).values.front().bytes,item.bytes);QCOMPARE((**result).values.front().flags,item.flags);
   }
   const auto stats=(*owner)->read(key,a,0,8,clock);QVERIFY(stats&&*stats);QVERIFY((**stats).values.empty());
 }
 void actualFindFlagsReachTheDatabaseWithoutSeedFiltering(){
   const std::string key(20,'W'),binary("wire\0flags",10);udp::Config config;config.generatedStoreService.emplace();config.generatedStoreService->volatileDatabase.emplace();
   services::azureus_store_policy::Key state;state.bytes=key;state.closest=true;config.generatedStoreService->keyStates={state};
   udp::OwnedPingTransport server(std::move(config)),client(udp::Config{});QVERIFY(server.start());QVERIFY(client.start());QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));
   const auto issued=token(client,server.endpoint(),this);QVERIFY(issued);auto item=value(contact(client.endpoint()),binary,1,QDateTime::currentMSecsSinceEpoch());item.flags=1;
   QVERIFY(stored(store(client,server.endpoint(),this,*issued,key,{item})));udp::lookup::nodes::NodeId target{};std::copy(key.begin(),key.end(),target.begin());
   for(const int flags:{1,2,3,4,16,128}){
     auto result=std::make_shared<std::optional<udp::FindValueOutcome>>();
     const auto sent=client.findValue(server.endpoint(),55,target,flags,0,this,[result](auto received){result->emplace(std::move(received));},2s);
     QVERIFY(sent);QVERIFY(spin([&]{return result->has_value();}));QVERIFY((*result)->has_value());
     const auto& reply=std::get<udp::lookup::ReplyBody>((**result)->body());QVERIFY(!reply.continuation);
     const auto* values=std::get_if<udp::lookup::FoundValues>(&reply.result);QVERIFY(values);QCOMPARE(values->values.size(),std::size_t{1});QCOMPARE(values->values.front().bytes,binary);QCOMPARE(values->values.front().flags,item.flags);
   }
   const auto read=server.databaseState();QVERIFY(read);QCOMPARE(read->reads,std::uint64_t{6});client.close();server.close();
 }
 void statsValueRequiresRealAdapterAndLocalIdentity(){
   db::Limits limits;limits.storageStats=db::StorageStatsMode::SuspendedDiversification;auto owner=db::Owner::create(limits);QVERIFY(owner);Clock clock;
   auto local=contact({QHostAddress::LocalHost,41001}),reader=contact({QHostAddress::LocalHost,41002});QVERIFY((*owner)->bindLocal(local));QVERIFY(!(*owner)->bindLocal(local));
   const std::string key(20,'S');auto item=value(reader,"binary",1);QVERIFY((*owner)->store(key,reader,{&item,1},clock));
   const auto normal=(*owner)->read(key,reader,0,0,clock);QVERIFY(normal&&*normal);QCOMPARE((**normal).values.front().bytes,item.bytes);
   struct Sequence final:db::lifecycle::WallClock {std::array<std::int64_t,3> times{65000,64000,63000};std::size_t at=0;std::int64_t nowMilliseconds()noexcept override{return times[std::min(at++,times.size()-1)];}} sequence;
   const auto result=(*owner)->read(key,reader,0,9,sequence);QVERIFY(result&&*result);QCOMPARE(sequence.at,std::size_t{3});QCOMPARE((**result).values.size(),std::size_t{1});const auto& stat=(**result).values.front();
   QCOMPARE(stat.created,std::int64_t{63000});QCOMPARE(stat.version,0);QCOMPARE(stat.flags,std::uint8_t{8});QCOMPARE(stat.lifeHours,std::uint8_t{0});QCOMPARE(stat.replication,std::int8_t{-1});QVERIFY(stat.originator.id==local.id);
   auto decoded=services::azureus_storage_stats::decode(stat.bytes);QVERIFY(decoded);QCOMPARE(decoded->value.entries,1);QCOMPARE(decoded->value.bytes,6);QCOMPARE(decoded->value.readsPerMinute,0);QCOMPARE(decoded->value.diversification,std::int8_t{1});
   auto invalid=reader;invalid.address.length=3;QVERIFY(!(*owner)->read(key,invalid,0,0,clock));QVERIFY(!(*owner)->read(key,reader,0,8,clock)); // Failed adapter observation never publishes guessed stats.
 }
 void actualStatsReadIsBoundToSocketAndRealMappingEvents(){
   namespace stats=services::azureus_storage_stats;const std::string key(20,'T');udp::Config cfg;cfg.generatedStoreService.emplace();cfg.generatedStoreService->volatileDatabase.emplace();cfg.generatedStoreService->volatileDatabase->storageStats=db::StorageStatsMode::SuspendedDiversification;
   services::azureus_store_policy::Key state;state.bytes=key;state.closest=true;cfg.generatedStoreService->keyStates={state};
   udp::OwnedPingTransport server(std::move(cfg)),client(udp::Config{});QVERIFY(server.start());QVERIFY(client.start(QHostAddress(QStringLiteral("127.0.0.2"))));QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));
   const auto issued=token(client,server.endpoint(),this);QVERIFY(issued);auto item=value(contact(client.endpoint()),std::string("data\0stats",10),1,QDateTime::currentMSecsSinceEpoch());QVERIFY(stored(store(client,server.endpoint(),this,*issued,key,{item})));
   udp::lookup::nodes::NodeId target{};std::copy(key.begin(),key.end(),target.begin());const auto actualLocal=contact(server.endpoint());
   for(const auto& [version,size]:{std::pair{1,10},std::pair{2,3},std::pair{3,0}}){
     if(version!=1){item.version=version;item.bytes=std::string(static_cast<std::size_t>(size),'x');QVERIFY(stored(store(client,server.endpoint(),this,*issued,key,{item})));}
     auto result=std::make_shared<std::optional<udp::FindValueOutcome>>();QVERIFY(client.findValue(server.endpoint(),55,target,9,0,this,[result](auto received){result->emplace(std::move(received));},2s));QVERIFY(spin([&]{return result->has_value();}));QVERIFY((*result)->has_value());
     const auto& reply=std::get<udp::lookup::ReplyBody>((**result)->body());const auto* values=std::get_if<udp::lookup::FoundValues>(&reply.result);QVERIFY(values);QCOMPARE(values->values.size(),std::size_t{1});const auto& stat=values->values.front();
     QCOMPARE(stat.bytes.size(),std::size_t{14});QCOMPARE(stat.version,0);QCOMPARE(stat.flags,std::uint8_t{8});QCOMPARE(stat.lifeHours,std::uint8_t{0});QCOMPARE(stat.replication,std::int8_t{-1});QVERIFY(stat.originator.id==actualLocal.id);QVERIFY(stat.originator.address==actualLocal.address);
     const auto decoded=stats::decode(stat.bytes);QVERIFY(decoded);QCOMPARE(decoded->value.entries,1);QCOMPARE(decoded->value.bytes,size);QCOMPARE(decoded->value.readsPerMinute,0);QCOMPARE(decoded->value.diversification,std::int8_t{1});
   }
   const auto read=server.databaseState();QVERIFY(read);QCOMPARE(read->reads,std::uint64_t{3});client.close();server.close();
 }
 void originalDiversificationRequiresExplicitControlAndRealMappingEvents(){
   namespace stats=services::azureus_storage_stats;
   struct Inputs final:stats::DiversificationControl {
     std::int64_t jitter=0,until=0;unsigned draws=0;bool available=true;
     std::expected<std::int64_t,stats::Error> expiryJitterMilliseconds()noexcept override{++draws;if(!available){return std::unexpected(stats::Error::Invalid);}return jitter;}
     std::int64_t suspendedUntilMilliseconds()const noexcept override{return until;}
   };
   db::Limits limits;limits.storageStats=db::StorageStatsMode::OriginalDiversification;limits.maximumEntriesPerKey=65;limits.originalRepublishMillis=3*24*60*60*1000;
   QVERIFY(!db::Owner::create(limits));auto inputs=std::make_shared<Inputs>();limits.diversificationControl=inputs;auto owner=db::Owner::create(limits);QVERIFY(owner);Clock clock;
   auto local=contact({QHostAddress::LocalHost,41000});QVERIFY((*owner)->bindLocal(local));const std::string key(20,'Z');
   // Protocol55 applies original restricted-ID3: varying only the final IP
   // octet at one port yields25 identities. Vary the unmodded second octet
   // so these are65 genuinely distinct mapping originators, not replacements.
   std::set<decltype(local.id)> origins;
   for(unsigned i=0;i<65;++i){auto origin=contact({QHostAddress(QStringLiteral("1.%1.0.1").arg(i)),41001});
     QVERIFY2(origins.insert(origin.id).second,"Diversification fixture requires a new actual origin ID");
     auto item=value(origin,std::string(512,'x'));item.lifeHours=0;
     const auto added=(*owner)->store(key,origin,{&item,1},clock);QVERIFY(added);QCOMPARE(*added,static_cast<std::int8_t>(i==64?3:1));}
   QCOMPARE(origins.size(),std::size_t{65});
   QCOMPARE(inputs->draws,1U);auto snapshot=(*owner)->snapshot();QVERIFY(snapshot);QCOMPARE(snapshot->values,std::size_t{65});QCOMPARE(snapshot->bytes,std::size_t{33280});
   auto reader=contact({QHostAddress(QStringLiteral("1.2.1.1")),41002});const auto observed=(*owner)->read(key,reader,0,8,clock);QVERIFY(observed&&*observed);QCOMPARE((**observed).values.size(),std::size_t{1});
   auto decoded=stats::decode((**observed).values[0].bytes);QVERIFY(decoded);QCOMPARE(decoded->value.entries,65);QCOMPARE(decoded->value.bytes,33280);QCOMPARE(decoded->value.diversification,std::int8_t{3});
   // Stats serialiseStats lazily expires the adapter key, but source Mapping's
   // cached wire diversification is not refreshed until its next real callback.
   clock.now=1000+stats::DiversifyingKey::MinimumExpiry;auto boundary=(*owner)->read(key,reader,0,8,clock);QVERIFY(boundary&&*boundary);decoded=stats::decode((**boundary).values[0].bytes);QVERIFY(decoded);QCOMPARE(decoded->value.diversification,std::int8_t{3});
   ++clock.now;auto expired=(*owner)->read(key,reader,0,8,clock);QVERIFY(expired&&*expired);QCOMPARE((**expired).diversification,std::int8_t{3});decoded=stats::decode((**expired).values[0].bytes);QVERIFY(decoded);QCOMPARE(decoded->value.diversification,std::int8_t{1});
   const auto normal=(*owner)->read(key,reader,0,0,clock);QVERIFY(normal&&*normal);QCOMPARE((**normal).diversification,std::int8_t{1});QCOMPARE(inputs->draws,1U);
   limits.storageStats=db::StorageStatsMode::SuspendedDiversification;limits.diversificationControl.reset();QVERIFY(!db::Owner::create(limits)); // Legacy64-entry bound retained.
   limits.storageStats=db::StorageStatsMode::OriginalDiversification;inputs=std::make_shared<Inputs>();inputs->available=false;limits.diversificationControl=inputs;auto missing= db::Owner::create(limits);QVERIFY(missing);
   auto item=value(reader,"uncommitted");QVERIFY(!(*missing)->store(key,reader,{&item,1},clock));snapshot=(*missing)->snapshot();QVERIFY(snapshot);QCOMPARE(snapshot->keys,std::size_t{0});QCOMPARE(snapshot->stores,std::uint64_t{0});
 }
 void originalFrequencyUsesActualIpBloomAndSuspensionSamples(){
   namespace stats=services::azureus_storage_stats;
   struct Inputs final:stats::DiversificationControl {
     std::int64_t until=0;unsigned draws=0;
     std::expected<std::int64_t,stats::Error> expiryJitterMilliseconds()noexcept override{++draws;return std::int64_t{0};}
     std::int64_t suspendedUntilMilliseconds()const noexcept override{return until;}
   };
   db::Limits limits;limits.storageStats=db::StorageStatsMode::OriginalDiversification;auto inputs=std::make_shared<Inputs>();limits.diversificationControl=inputs;
   auto owner=db::Owner::create(limits);QVERIFY(owner);Clock clock;auto local=contact({QHostAddress::LocalHost,41000});QVERIFY((*owner)->bindLocal(local));const std::string key(20,'F');auto item=value(local,"actual mapping");QVERIFY((*owner)->store(key,local,{&item,1},clock));
   for(unsigned i=0;i<192;++i){clock.now=i;auto reader=contact({QHostAddress(QStringLiteral("1.2.0.%1").arg(i)),41001});auto read=(*owner)->read(key,reader,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).diversification,std::int8_t{1});}
   clock.now=180001;auto reader=contact({QHostAddress(QStringLiteral("8.8.8.8")),41001});auto read=(*owner)->read(key,reader,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).diversification,std::int8_t{2});
   const auto result=(*owner)->read(key,reader,0,8,clock);QVERIFY(result&&*result);const auto decoded=stats::decode((**result).values[0].bytes);QVERIFY(decoded);QCOMPARE(decoded->value.readsPerMinute,44);QCOMPARE(decoded->value.diversification,std::int8_t{2});QCOMPARE(inputs->draws,1U);
   // Separate actual adapter with the same192 IPs and a genuine future wall
   // deadline retains the measured44RPM while suppressing diversification.
   inputs=std::make_shared<Inputs>();inputs->until=1000000;limits.diversificationControl=inputs;owner=db::Owner::create(limits);QVERIFY(owner);clock.now=1000;QVERIFY((*owner)->bindLocal(local));QVERIFY((*owner)->store(key,local,{&item,1},clock));
   for(unsigned i=0;i<192;++i){clock.now=i;auto peer=contact({QHostAddress(QStringLiteral("1.2.0.%1").arg(i)),41001});QVERIFY((*owner)->read(key,peer,0,0,clock));}
   clock.now=180001;read=(*owner)->read(key,reader,0,0,clock);QVERIFY(read&&*read);QCOMPARE((**read).diversification,std::int8_t{1});
   auto stat=(*owner)->read(key,reader,0,8,clock);QVERIFY(stat&&*stat);const auto suspended=stats::decode((**stat).values[0].bytes);QVERIFY(suspended);QCOMPARE(suspended->value.readsPerMinute,44);QCOMPARE(suspended->value.diversification,std::int8_t{1});
 }
 void borrowedClockCannotReenterFacadeMutation(){
   auto owner=db::Owner::create();QVERIFY(owner);
   struct Reentrant final:db::lifecycle::WallClock {
     db::Owner* owner=nullptr;bool refused=false;
     std::int64_t nowMilliseconds()noexcept override{const auto read=owner->contains("ignored");refused=!read&&read.error()==db::Error::Busy;return 1000;}
   } clock;clock.owner=owner->get();auto a=contact({QHostAddress::LocalHost,41001});auto item=value(a,"payload");
   QVERIFY((*owner)->store(std::string(20,'R'),a,{&item,1},clock));QVERIFY(clock.refused);
 }
 void strictExpiryBoundaryThrottleAndDelayedEmptyMappingRemoval(){
   auto owner=db::Owner::create();QVERIFY(owner);Clock clock;auto a=contact({QHostAddress::LocalHost,41001});const std::string key(20,'E');auto item=value(a,"payload");
   QVERIFY((*owner)->store(key,a,{&item,1},clock));clock.now=4501000;QVERIFY((*owner)->expire(clock,true));
   auto before=(*owner)->snapshot();QVERIFY(before);QCOMPARE(before->values,std::size_t{1});
   clock.now=4501001;QVERIFY((*owner)->expire(clock,false)); // elapsed1ms: original throttle retains it.
   before=(*owner)->snapshot();QVERIFY(before);QCOMPARE(before->values,std::size_t{1});
   QVERIFY((*owner)->expire(clock,true));auto empty=(*owner)->snapshot();QVERIFY(empty);QCOMPARE(empty->values,std::size_t{0});QCOMPARE(empty->keys,std::size_t{1});QCOMPARE(empty->expired,std::uint64_t{1});
   QVERIFY((*owner)->expire(clock,true));auto removed=(*owner)->snapshot();QVERIFY(removed);QCOMPARE(removed->keys,std::size_t{0});
   db::Limits limits;limits.originalRepublishMillis=1000;auto wrapped=db::Owner::create(limits);QVERIFY(wrapped);clock.now=std::numeric_limits<std::int64_t>::max()-100;
   auto edge=value(a,"wrap",1,clock.now);edge.lifeHours=0;QVERIFY((*wrapped)->store(key,a,{&edge,1},clock));QVERIFY((*wrapped)->expire(clock,true));
   const auto overflow=(*wrapped)->snapshot();QVERIFY(overflow);QCOMPARE(overflow->values,std::size_t{0}); // Source signed-long deadline wraps, rather than saturation.
 }
 void quotaChecksExistingFourByteOverheadAndCapacityNeverClobbers(){
   db::Limits limits;limits.payloadQuota=0;limits.maximumKeys=1;auto owner=db::Owner::create(limits);QVERIFY(owner);Clock clock;
   auto a=contact({QHostAddress::LocalHost,41001});auto empty=value(a,"");const std::string key(20,'Q');
   const auto first=(*owner)->store(key,a,{&empty,1},clock);QVERIFY(first);QCOMPARE(*first,std::int8_t{1});
   auto payload=value(a,"must-not-appear",2);const auto quota=(*owner)->store(key,a,{&payload,1},clock);QVERIFY(quota);QCOMPARE(*quota,std::int8_t{3});
   auto snapshot=(*owner)->snapshot();QVERIFY(snapshot);QCOMPARE(snapshot->values,std::size_t{1});QCOMPARE(snapshot->bytes,std::size_t{0});
   bool refused=false;std::thread foreign([&]{refused=!(*owner)->contains(key);});foreign.join();QVERIFY(refused);
   QVERIFY(!(*owner)->store("tiny",a,{&payload,1},clock));const auto absentStats=(*owner)->read(key,a,0,8,clock);QVERIFY(absentStats&&*absentStats);QVERIFY((**absentStats).values.empty());
   limits.payloadQuota=4*1024*1024;auto bounded=db::Owner::create(limits);QVERIFY(bounded);QVERIFY((*bounded)->store(key,a,{&payload,1},clock));
   QVERIFY(!(*bounded)->store(std::string(20,'X'),a,{&payload,1},clock));const auto held=(*bounded)->snapshot();QVERIFY(held);QCOMPARE(held->keys,std::size_t{1});QCOMPARE(held->values,std::size_t{1});
 }
 void actualTokenStoreAndRecursiveGetReadMutatedDatabase(){
   const std::string key(20,'D'),binary("a\0b\xff",4);
   udp::Config config;config.generatedStoreService.emplace();config.generatedStoreService->volatileDatabase.emplace();
   services::azureus_store_policy::Key ownedKey;ownedKey.bytes=key;ownedKey.closest=true;config.generatedStoreService->keyStates={ownedKey};
   udp::OwnedPingTransport server(std::move(config)),first(udp::Config{}),second(udp::Config{});QVERIFY(server.start());QVERIFY(first.start(QHostAddress(QStringLiteral("127.0.0.2"))));QVERIFY(second.start(QHostAddress(QStringLiteral("127.0.0.3"))));
   for(auto* peer:{&first,&second}){QVERIFY(server.allowPeer(peer->endpoint()));QVERIFY(peer->allowPeer(server.endpoint()));}
   const auto firstCookie=token(first,server.endpoint(),this),secondCookie=token(second,server.endpoint(),this);QVERIFY(firstCookie&&secondCookie);
   auto a=value(contact(first.endpoint()),binary,1,QDateTime::currentMSecsSinceEpoch());
   const auto refused=store(first,server.endpoint(),this,*firstCookie^1,key,{a});QVERIFY(stored(refused));
   auto state=server.databaseState();QVERIFY(state);QCOMPARE(state->stores,std::uint64_t{0});QCOMPARE(state->values,std::size_t{0});
   QVERIFY(stored(store(first,server.endpoint(),this,*firstCookie,key,{a})));
   auto b=value(contact(second.endpoint()),"second-actual-wire",1,QDateTime::currentMSecsSinceEpoch());QVERIFY(stored(store(second,server.endpoint(),this,*secondCookie,key,{b})));
   state=server.databaseState();QVERIFY(state);QCOMPARE(state->stores,std::uint64_t{2});QCOMPARE(state->values,std::size_t{2});
   // Original protocol55 compresses same-IP identities to port%5; owned peers need distinct IDs.
   app::AzureusServiceOptions opts;opts.enabled=true;opts.bindAddress=QHostAddress(QStringLiteral("127.0.0.4"));auto lookup=app::OwnedAzureusService::create(opts);QVERIFY(lookup);
   QVERIFY(contact(server.endpoint()).id!=contact((*lookup)->endpoint()).id);QVERIFY(server.allowPeer((*lookup)->endpoint()));
   const auto allowed=(*lookup)->allowPeer(server.endpoint());QVERIFY(allowed);
   QVERIFY(spin([&]{const auto observation=(*lookup)->observation();return observation.lastResult&&observation.lastResult->request==*allowed&&observation.lastResult->terminal;}));
   control::NodeId target{};std::copy(key.begin(),key.end(),target.begin());const auto request=(*lookup)->lookupValue(server.endpoint(),target);QVERIFY(request);
   QVERIFY(spin([&]{const auto observation=(*lookup)->observation();return observation.lookupSnapshot&&observation.lookupSnapshot->lookup==*request&&control::lookupTerminal(observation.lookupSnapshot->state)&&observation.outstandingCommands==0;}));
   const auto observation=(*lookup)->observation();QVERIFY(observation.lookupSnapshot);QVERIFY(observation.snapshot);const auto retained=observation.lookupSnapshot;
   QVERIFY(control::validLookupSnapshot(*retained));QCOMPARE(retained->state,control::LookupState::Complete);QCOMPARE(retained->results.size(),std::size_t{2});QCOMPARE(retained->nodes.size(),std::size_t{1});QCOMPARE(retained->nodes.front().slices,2U);
   QVERIFY(std::ranges::any_of(retained->results,[&](const auto& v){return v.value.bytes==binary&&v.value.originator.id==a.originator.id;}));
   QVERIFY(std::ranges::any_of(retained->results,[&](const auto& v){return v.value.bytes==b.bytes&&v.value.originator.id==b.originator.id;}));
   const auto again=server.databaseState();QVERIFY(again);QCOMPARE(again->reads,std::uint64_t{1});
   const auto serverBytes=server.statistics().sentPayloadBytes,firstBytes=first.statistics().receivedPayloadBytes,secondBytes=second.statistics().receivedPayloadBytes;
   const auto lookupBytes=observation.snapshot->transport().counters().receivedPayloadBytes;
   QVERIFY(serverBytes&&firstBytes&&secondBytes&&lookupBytes);QCOMPARE(*serverBytes,*firstBytes+*secondBytes+*lookupBytes);

   QVERIFY((*lookup)->close());server.close();first.close();second.close();QVERIFY(!server.databaseState());QCOMPARE(retained->results.size(),std::size_t{2});
 }
 void malformedAndWrongNetworkNeverReachTheDatabase(){
   const std::string key(20,'M');udp::Config config;config.generatedStoreService.emplace();config.generatedStoreService->volatileDatabase.emplace();
   services::azureus_store_policy::Key ownedKey;ownedKey.bytes=key;ownedKey.closest=true;config.generatedStoreService->keyStates={ownedKey};
   udp::OwnedPingTransport server(std::move(config)),valid(udp::Config{});QVERIFY(server.start());QVERIFY(valid.start());QVERIFY(server.allowPeer(valid.endpoint()));QVERIFY(valid.allowPeer(server.endpoint()));
   QUdpSocket raw;QVERIFY(raw.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress));QVERIFY(server.allowPeer({QHostAddress::LocalHost,raw.localPort()}));
   const auto malformed=server.statistics().malformed;QByteArray invalid(16,'\0');QCOMPARE(raw.writeDatagram(invalid,server.endpoint().address,server.endpoint().port),qint64{16});
   QVERIFY(spin([&]{return server.statistics().malformed>malformed;}));QVERIFY(token(valid,server.endpoint(),this));
   udp::Config foreignConfig;foreignConfig.network=9;udp::OwnedPingTransport foreign(std::move(foreignConfig));QVERIFY(foreign.start());QVERIFY(server.allowPeer(foreign.endpoint()));QVERIFY(foreign.allowPeer(server.endpoint()));
   auto item=value(contact(foreign.endpoint()),"must-not-store",1,QDateTime::currentMSecsSinceEpoch());
   const auto denied=store(foreign,server.endpoint(),this,0,key,{item});QVERIFY(denied);QVERIFY(!denied->has_value());QCOMPARE(denied->error(),udp::Failure::Timeout);
   const auto state=server.databaseState();QVERIFY(state);QCOMPARE(state->stores,std::uint64_t{0});QCOMPARE(state->values,std::size_t{0});
   foreign.close();valid.close();server.close();
 }
 void legacyModeRemainsNonDatabaseAndUnsafeBindCannotActivateIt(){
   udp::Config config;config.generatedStoreService.emplace();udp::OwnedPingTransport legacy(std::move(config));QVERIFY(legacy.start());QVERIFY(!legacy.databaseState());legacy.close();
   udp::Config refusedConfig;refusedConfig.generatedStoreService.emplace();refusedConfig.generatedStoreService->volatileDatabase.emplace();udp::OwnedPingTransport refused(std::move(refusedConfig));QVERIFY(!refused.start(QHostAddress(QStringLiteral("192.0.2.1"))));QVERIFY(!refused.databaseState());
   udp::Config mixedConfig;mixedConfig.generatedStoreService.emplace();mixedConfig.generatedStoreService->volatileDatabase.emplace();mixedConfig.findValueReply=std::vector<udp::FindValueSlice>{};udp::OwnedPingTransport mixed(std::move(mixedConfig));QVERIFY(!mixed.start());
 }
};
QTEST_GUILESS_MAIN(AzureusDatabaseRuntimeTests)
#include "azureus_database_runtime_tests.moc"
