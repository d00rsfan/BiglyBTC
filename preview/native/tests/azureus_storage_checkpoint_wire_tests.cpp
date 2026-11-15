// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_transport.hpp"
#include "database.hpp"
#include <QTest>
#include <QDateTime>
#include <QScopeGuard>
#include <optional>
#include <vector>
#include <thread>
#include <source_location>
using namespace biglybtc;using namespace std::chrono_literals;
namespace {
namespace udp=services::azureus_udp;namespace db=services::azureus_db_runtime;namespace stats=services::azureus_storage_stats;namespace nodes=services::azureus_nodes;
void need(bool valid,std::source_location at=std::source_location::current()){if(!valid){throw std::runtime_error("Generated checkpoint receiver at "+std::to_string(at.line())+":"+std::to_string(at.column()));}}
nodes::Contact contact(udp::Peer peer){nodes::ResolvedEndpoint endpoint;endpoint.address.length=4;endpoint.address.port=peer.port;const auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){endpoint.address.bytes[i]=std::uint8_t(ip>>(24-8*i));}nodes::NodeCache cache;auto id=cache.lookup(endpoint,55);need(bool(id));nodes::Contact out;out.protocol=55;out.id=id->id;out.address=endpoint.address;return out;}
struct Wall final:db::lifecycle::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}};
struct Inputs final:stats::DiversificationControl{std::expected<std::int64_t,stats::Error> expiryJitterMilliseconds()noexcept override{return 0;}std::int64_t suspendedUntilMilliseconds()const noexcept override{return 0;}};
struct Fixture {
 bool current=true,held=false;unsigned readPermits=0;bool withdraw=false;
 udp::OwnedPingTransport server,client;std::shared_ptr<db::Owner> database;
 std::vector<std::move_only_function<void(bool)>> checkpoints;std::string key=std::string(20,'C');
 Fixture():server([]{udp::Config c;c.network=0;c.servePings=true;c.routingScope=udp::RoutingScope::OwnedLoopback;return c;}()),client([]{udp::Config c;c.network=0;c.routingScope=udp::RoutingScope::OwnedLoopback;return c;}()){
  need(bool(server.start(QHostAddress("127.101.0.1")))&&bool(client.start(QHostAddress("127.102.0.1"))));db::Limits limits;limits.maximumEntriesPerKey=256;limits.storageStats=db::StorageStatsMode::OriginalDiversification;limits.diversificationControl=std::make_shared<Inputs>();auto made=db::Owner::create(limits);need(bool(made));database=std::move(*made);need(bool(database->restoreLocalDiversifications({},QDateTime::currentMSecsSinceEpoch())));auto local=contact(server.endpoint());need(bool(database->bindLocal(local)));auto router=services::azureus_router::RouterOwner::create(local.id);need(bool(router));grant([&]{need(bool(server.serveDatabase(std::move(*router),database,[this]{return current&&held;},[this](auto done){grant([&]{done(current);});return true;})));});need(bool(client.allowPeer(server.endpoint())));grant([&]{need(bool(server.attachKnownPeer(client.endpoint(),55,std::chrono::steady_clock::now()+30s)));});
  need(bool(server.setLocalReadPreflight([this](std::string target,auto done){grant([&]{++readPermits;if(withdraw&&readPermits>1){Wall clock;need(bool(database->removeLocal(target,10,clock)));}done(current);});return true;})));
 }
 ~Fixture(){server.close();client.close();}
 template<class F>void grant(F fn){const bool before=held;held=true;const auto reset=qScopeGuard([&]{held=before;});fn();}
 void hook(std::chrono::milliseconds wait=5s){need(bool(server.setStorageCheckpoint([this](auto,auto done){checkpoints.push_back(std::move(done));return true;},wait)));}
 void publish(std::string bytes="generated current value",int version=1){Wall clock;need(bool(database->putLocal(key,bytes,version,clock)));}
 std::optional<std::int32_t> cookie(QObject* receiver){auto result=std::make_shared<std::optional<udp::DetailedFindNodeOutcome>>();auto ticket=client.requestStoreToken(server.endpoint(),55,receiver,[result](auto reply){result->emplace(std::move(reply));},1s);if(!ticket){return {};}QDeadlineTimer end(1500);while(!result->has_value()&&!end.hasExpired()){QTest::qWait(1);}if(!result->has_value()||!**result){return {};}const auto* decoded=std::get_if<udp::find::Decoded>(&***result);if(!decoded){return {};}return std::get<udp::find::ReplyBody>(decoded->body()).randomId;}
 std::shared_ptr<std::optional<udp::StoreOutcome>> put(QObject* receiver,std::int32_t cookie,int version=1){auto output=std::make_shared<std::optional<udp::StoreOutcome>>();udp::StoreRequest request;request.randomId=cookie;request.keys={key};request.valueSets={{{version,QDateTime::currentMSecsSinceEpoch(),"generated remote value",contact(client.endpoint()),0,0,-1}}};need(bool(client.store(server.endpoint(),55,std::move(request),receiver,[output](auto reply){output->emplace(std::move(reply));},1s)));return output;}
 std::shared_ptr<std::vector<udp::FindValueOutcome>> get(QObject* receiver){auto output=std::make_shared<std::vector<udp::FindValueOutcome>>();nodes::NodeId target{};std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()),20,target.begin());need(bool(client.findValue(server.endpoint(),55,target,0,32,receiver,[output](auto reply){output->push_back(std::move(reply));},1s)));return output;}
};
}
class AzureusStorageCheckpointWireTests final:public QObject {
 Q_OBJECT
private slots:
 void storeAcknowledgementWaitsForCheckpointAndCurrentPermit(){
  Fixture f;f.hook();auto cookie=f.cookie(this);QVERIFY(cookie);const auto before=f.server.statistics().sent;auto output=f.put(this,*cookie);QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{1},1000);QVERIFY(!output->has_value());QCOMPARE(f.database->snapshot()->stores,std::uint64_t{1});QCOMPARE(f.server.statistics().sent,before);f.grant([&]{f.checkpoints[0](true);});QTRY_VERIFY_WITH_TIMEOUT(output->has_value(),1000);QVERIFY(**output);QCOMPARE(f.server.statistics().sent,before+1);f.grant([&]{f.checkpoints[0](true);});QTest::qWait(10);QCOMPARE(f.server.statistics().sent,before+1);
 }
 void delayedGetDoesNotReadTwiceAndRechecksItsKeyPermit(){
  Fixture f;f.publish();f.hook();auto output=f.get(this);QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{1},1000);QVERIFY(output->empty());QCOMPARE(f.database->snapshot()->reads,std::uint64_t{1});QCOMPARE(f.readPermits,1U);f.grant([&]{f.checkpoints[0](true);});QTRY_VERIFY_WITH_TIMEOUT(!output->empty(),1000);QVERIFY(output->front());const auto* body=std::get_if<udp::lookup::ReplyBody>(&output->front()->body());QVERIFY(body);const auto* values=std::get_if<udp::lookup::FoundValues>(&body->result);QVERIFY(values&&values->values.size()==1);QCOMPARE(values->values[0].bytes,std::string("generated current value"));QCOMPARE(f.database->snapshot()->reads,std::uint64_t{1});QCOMPARE(f.readPermits,2U);
 }
 void aWithdrawalDuringTheSecondPermitCannotSendCapturedOwnData(){
  Fixture f;f.publish();f.withdraw=true;f.hook();auto output=f.get(this);QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{1},1000);const auto sent=f.server.statistics().sent;f.grant([&]{f.checkpoints[0](true);});QTest::qWait(40);QVERIFY(output->empty());QCOMPARE(f.server.statistics().sent,sent);QCOMPARE(f.readPermits,2U);QCOMPARE(f.database->snapshot()->reads,std::uint64_t{1});
 }
 void absentPermitFailureCloseAndExpiryCannotReleaseOldReplies_data(){QTest::addColumn<int>("reason");QTest::newRow("checkpoint-failure")<<0;QTest::newRow("outside-source-permit")<<1;QTest::newRow("source-revoked")<<2;QTest::newRow("closed")<<3;QTest::newRow("expired")<<4;QTest::newRow("foreign-thread")<<5;}
 void absentPermitFailureCloseAndExpiryCannotReleaseOldReplies(){
  QFETCH(int,reason);Fixture f;f.hook(reason==4?30ms:5s);auto cookie=f.cookie(this);QVERIFY(cookie);auto output=f.put(this,*cookie);QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{1},1000);const auto before=f.server.statistics().sent;
  if(reason==0){f.grant([&]{f.checkpoints[0](false);});}else if(reason==1){f.checkpoints[0](true);}else if(reason==2){f.current=false;f.grant([&]{f.checkpoints[0](true);});}else if(reason==3){f.server.close();f.grant([&]{f.checkpoints[0](true);});}else if(reason==4){QTest::qWait(80);f.grant([&]{f.checkpoints[0](true);});}else{std::thread thread([&]{f.checkpoints[0](true);});thread.join();}
  QTest::qWait(30);QCOMPARE(f.server.statistics().sent,before);QVERIFY(!output->has_value());
 }
 void delayedGetRefusesAReplacementWithTheSameKey(){
  Fixture f;f.publish();f.hook();auto output=f.get(this);QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{1},1000);f.publish("generated replacement",2);const auto sent=f.server.statistics().sent;f.grant([&]{f.checkpoints[0](true);});QTest::qWait(25);QVERIFY(output->empty());QCOMPARE(f.server.statistics().sent,sent);QCOMPARE(f.database->snapshot()->reads,std::uint64_t{1});
 }
 void aPolicyChangeCannotCertifyAnOlderResponse(){
  Fixture f;f.publish();f.hook();auto output=f.get(this);QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{1},1000);Wall clock;for(unsigned n=0;n<65;++n){nodes::Contact peer=contact(f.client.endpoint());peer.id.fill(std::uint8_t(n));peer.address.bytes[0]=std::uint8_t(n+1);services::azureus_values::Value v{1,clock.nowMilliseconds(),std::string(512,'x'),peer,0,0,-1};QVERIFY(f.database->store(f.key,peer,std::span(&v,1),clock));}auto revision=f.database->localDiversificationRevision();QVERIFY(revision&&*revision>1);const auto sent=f.server.statistics().sent;f.grant([&]{f.checkpoints[0](true);});QTest::qWait(25);QVERIFY(output->empty());QCOMPARE(f.server.statistics().sent,sent);
 }
 void boundedWaitersReleaseTheirCapacityAfterFailure(){
  Fixture f;f.hook();auto cookie=f.cookie(this);QVERIFY(cookie);std::vector<std::shared_ptr<std::optional<udp::StoreOutcome>>> outputs;for(unsigned n=0;n<33;++n){outputs.push_back(f.put(this,*cookie,n+1));}QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{32},1000);QVERIFY(f.server.statistics().serviceCapacityDrops>=1);f.grant([&]{f.checkpoints[0](false);});outputs.push_back(f.put(this,*cookie,34));QTRY_COMPARE_WITH_TIMEOUT(f.checkpoints.size(),std::size_t{33},1000);f.grant([&]{f.checkpoints.back()(true);});QTRY_VERIFY_WITH_TIMEOUT(outputs.back()->has_value(),1000);QVERIFY(**outputs.back());
 }
 void callbacksAfterOwnerDestructionHaveNoEffect(){
  auto f=std::make_unique<Fixture>();f->hook();auto cookie=f->cookie(this);QVERIFY(cookie);auto output=f->put(this,*cookie);QTRY_COMPARE_WITH_TIMEOUT(f->checkpoints.size(),std::size_t{1},1000);auto callback=std::move(f->checkpoints[0]);f.reset();callback(true);QTest::qWait(10);QVERIFY(!output->has_value());
 }
 void synchronousDurableGetUsesOnlyTheInitialSourcePermit(){
  Fixture f;f.publish();QVERIFY(f.server.setStorageCheckpoint([](auto,auto done){done(true);return true;}));auto output=f.get(this);QTRY_VERIFY_WITH_TIMEOUT(!output->empty(),1000);QVERIFY(output->front());QCOMPARE(f.readPermits,1U);QCOMPARE(f.database->snapshot()->reads,std::uint64_t{1});
 }
};
QTEST_GUILESS_MAIN(AzureusStorageCheckpointWireTests)
#include "azureus_storage_checkpoint_wire_tests.moc"
