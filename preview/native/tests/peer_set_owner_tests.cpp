// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_constraint_worker.hpp"
#include "peer_set_tag_turn.hpp"
#include "owned_engine_fixture.hpp"
#include "network_policy.hpp"
#include <QtTest>
#include <future>
#include <thread>
namespace {
using namespace biglybtc;using namespace std::chrono_literals;
std::string encode(const lt::entry& value){std::string out;lt::bencode(std::back_inserter(out),value);return out;}
void definitions(const test::OwnedEngineFixture& fixture,std::string expression){
 lt::entry config(lt::entry::dictionary_type{});config["speed.limit.handler.schedule.lines"]=lt::entry::list_type{lt::entry("peer_set Generated peers=127.0.0.0/8")};config["speed.limit.handler.ipset_n.I5SW4ZLSMF2GKZBAOBSWK4TT"]=42;config["unrelated"]="Generated opaque config";test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(encode(config)));
 lt::entry tags(lt::entry::dictionary_type{});tags["3"]["1"]["n"]="Generated consumer";tags["3"]["1"]["c"]["pp.constraint"]=lt::entry::list_type{lt::entry(expression)};tags["3"]["1"]["o"]=lt::entry::list_type{};test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(encode(tags)));
}
ui::Presentation library(const test::OwnedEngineFixture& fixture,std::uint64_t admission=0){auto rows=std::make_shared<ui::LibraryPresentation>();rows->revision=7;ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;item.providerAdmission=admission;item.providerGeneration=admission?88:0;rows->items.push_back(item);return rows;}
bool member(const storage::ManualTagSnapshot& snapshot){for(const auto& row:snapshot.tags()->tags){if(row.id==1){return !row.uniqueHashes.empty();}}return false;}
}
class PeerSetOwnerTests final:public QObject{Q_OBJECT
private slots:
 void actualSdkAdmissionProducesGuardedPeerMembership(){
  test::OwnedEngineFixture fixture(false,true);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);definitions(fixture,"hasTag(\"Generated peers\")");
  auto task=std::async(std::launch::async,[&]{
   auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Generated peer profile");auto allocated=engine::PersistentSession::create({1,88,1});test::require(bool(allocated),"Actual peer engine");auto engine=std::move(*allocated);auto started=engine->startManaged(*lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=fixture.port(),.forceStart=true});test::require(bool(started),"Actual owned peer admission");
   std::optional<engine::ManagedDownloadStatus> managed;std::optional<engine::PeerSetEndpoints> endpoints;const auto end=std::chrono::steady_clock::now()+15s;
   while(std::chrono::steady_clock::now()<end){test::require(bool(engine->poll()),"Actual engine polling");for(const auto& value:engine->managedDownloads()){if(value.hash==*fixture.hash){managed=value;}}if(managed&&managed->checked){auto actual=engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission);if(actual&&!actual->publicIpv4.empty()){endpoints=std::move(*actual);break;}}std::this_thread::sleep_for(10ms);}
   test::require(managed&&endpoints&&std::ranges::find(endpoints->publicIpv4,std::optional<std::uint32_t>{0x7f000001U})!=endpoints->publicIpv4.end(),"Actual SDK connected endpoint not observed");
   test::require(!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission+1)&&!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission,0)&&!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission,513),"Admission/maximum source fence");
   auto wrongThread=std::async(std::launch::async,[&]{return bool(engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission));});test::require(!wrongThread.get(),"Foreign thread entered SDK peer source");
   auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual peer tag writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual peer tag snapshot");auto worker=app::TagConstraintWorker::activate(*lease,88);test::require(bool(worker),"Actual peer constraint worker");app::ConstraintWorkerFrame frame;frame.session=88;frame.library=library(fixture,managed->admission);frame.managed.push_back(*managed);frame.peerEngineIdentity=endpoints->identity;
   auto turn=[&]{++frame.sequence;frame.sourceRevision=lease->sourceRevision();return (*worker)->step(**store,*snapshot,frame);};
   const auto configBefore=test::read(fixture.profile+"/biglybt.config"),stateBefore=test::read(fixture.stateFile),inventoryBefore=test::read(fixture.profile+"/downloads.config");
   auto request=turn();test::require(request&&request->peerRequest&&!request->changed&&!member(*snapshot),"First peer demand did not defer without mutation");
   // An independent source waiter can acquire the gate after step returns.
   auto gateFree=std::async(std::launch::async,[&]{auto gate=lease->sourceAccess();return bool(gate);});test::require(gateFree.wait_for(1s)==std::future_status::ready&&gateFree.get(),"SDK query would retain the source gate");
   for(int bad=0;bad<5;++bad){app::ConstraintPeerObservation observation{*request->peerRequest,*endpoints,std::chrono::steady_clock::now()};
    if(bad==0){++observation.request.sourceRevision;}if(bad==1){++observation.request.stamp.binding.providerAdmission;}if(bad==2){++observation.request.stamp.binding.target.generation;}if(bad==3){observation.request.definitionsDigest="Generated stale definitions";}if(bad==4){observation.sampled-=4s;}
    frame.peerObservations={observation};request=turn();test::require(request&&request->peerRequest&&!request->changed&&!member(*snapshot),"Stale source receipt created membership");
   }
   frame.peerObservations.clear();++frame.sequence;const auto firstSequence=frame.sequence;
   auto accepted=app::runConstraintPeerTurn(**worker,**store,*snapshot,frame,engine.get());test::require(frame.sequence==firstSequence+1,"Actual Host handoff did not consume immediately");test::require(accepted&&accepted->changed&&!accepted->peerRequest&&member(accepted->snapshot),"Actual current peer did not satisfy normal membership CAS");test::require(accepted->snapshot.generation()==snapshot->generation()+1,"Peer membership did not use single saved generation");snapshot=accepted->snapshot;
   test::require(test::read(fixture.profile+"/biglybt.config")==configBefore&&test::read(fixture.stateFile)==stateBefore&&test::read(fixture.profile+"/downloads.config")==inventoryBefore,"Tag source changed unrelated original metadata");
   (*worker)->close();test::require(bool((*store)->close()),"Peer writer close");auto closed=engine->close(std::chrono::steady_clock::now()+5s);test::require(closed.sessionJoined&&!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission),"Closed peer engine provided observations");return true;
  });QVERIFY(task.get());
 }
 void unavailableOrForeignProviderCannotCreatePeerMembership_data(){QTest::addColumn<int>("bad");for(int i=0;i<5;++i){QTest::newRow(qPrintable(QString::number(i)))<<i;}}
 void unavailableOrForeignProviderCannotCreatePeerMembership(){
  QFETCH(int,bad);test::OwnedEngineFixture fixture(false);definitions(fixture,"hasTag(\"Generated peers\")");auto task=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Generated guard profile");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Guard writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Guard snapshot");auto worker=app::TagConstraintWorker::activate(*lease,88);test::require(bool(worker),"Guard worker");
   engine::ManagedDownloadStatus managed{*fixture.hash};managed.admission=11;managed.retained=managed.checked=true;managed.phase=engine::ManagedPhase::Seeding;managed.bytes=managed.completed=test::OwnedEngineFixture::PayloadBytes;managed.complete=true;app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library(fixture,11);frame.managed={managed};frame.peerEngineIdentity=engine::AccountingIdentity{1,88,1};auto requested=(*worker)->step(**store,*snapshot,frame);test::require(requested&&requested->peerRequest,"Guard request");
   app::ConstraintPeerObservation reply{*requested->peerRequest,engine::PeerSetEndpoints{{1,88,1},*fixture.hash,11,{0x7f000001U}},std::chrono::steady_clock::now()};if(bad==0){reply.value.reset();}if(bad==1){++reply.value->identity.engineEpoch;}if(bad==2){++reply.value->admission;}if(bad==3){reply.value->publicIpv4={std::nullopt};}if(bad==4){reply.value->publicIpv4=std::vector<std::optional<std::uint32_t>>(513,0x7f000001U);}frame.peerObservations={reply};++frame.sequence;auto denied=(*worker)->step(**store,*snapshot,frame);test::require(denied&&!denied->changed&&!member(*snapshot),"Unavailable/foreign source made membership");(*worker)->close();test::require(bool((*store)->close()),"Guard close");return true;});QVERIFY(task.get());
 }
 void nonPeerFunctionsAndInactiveProvidersDoNotRequestSdk_data(){QTest::addColumn<QString>("expression");for(const auto* expression:{"true","hasTag(\"Unknown\")","hasTagAge(\"Generated peers\")>0","countTag(\"Generated peers\")>0","hasTagGroup(\"Generated peers\")"}){QTest::newRow(expression)<<QString::fromLatin1(expression);}}
 void nonPeerFunctionsAndInactiveProvidersDoNotRequestSdk(){
  QFETCH(QString,expression);test::OwnedEngineFixture fixture(false);definitions(fixture,expression.toStdString());auto task=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"No-demand profile");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"No-demand writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"No-demand snapshot");auto worker=app::TagConstraintWorker::activate(*lease,88);test::require(bool(worker),"No-demand worker");app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library(fixture);auto result=(*worker)->step(**store,*snapshot,frame);test::require(result&&!result->peerRequest,"Unneeded SDK peer demand");(*worker)->close();test::require(bool((*store)->close()),"No-demand close");return true;});QVERIFY(task.get());
 }
 void hasTagDemandMetadataIsExact(){auto expression=core::tags::constraints::compile(u"hasTag(\"One\") && (hasTagAge(\"Age\")>0 || hasTag(\"One\") || hasTagGroup(\"Group\"))");QVERIFY(expression);QCOMPARE(expression->hasTagNames().size(),std::size_t{1});QCOMPARE(expression->hasTagNames()[0],std::u16string(u"One"));}
};
QTEST_GUILESS_MAIN(PeerSetOwnerTests)
#include "peer_set_owner_tests.moc"
