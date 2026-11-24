// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_constraint_worker.hpp"
#include "peer_set_tag_turn.hpp"
#include "owned_engine_fixture.hpp"
#include "network_policy.hpp"
#include <QtTest>
#include <future>
#include <thread>
namespace {
using namespace biglybtc;using namespace std::chrono_literals;namespace sets=core::tags::peer_sets;
std::string encode(const lt::entry& value){std::string out;lt::bencode(std::back_inserter(out),value);return out;}
void definitions(const test::OwnedEngineFixture& fixture,std::string expression,bool ipv6=false,
    std::string_view selectorOverride={}){
 const auto selector=selectorOverride.empty()?(ipv6?"IPv6":"127.0.0.0/8"):selectorOverride;
 lt::entry config(lt::entry::dictionary_type{});config["speed.limit.handler.schedule.lines"]=lt::entry::list_type{lt::entry("peer_set Generated peers="+std::string(selector))};config["speed.limit.handler.ipset_n.I5SW4ZLSMF2GKZBAOBSWK4TT"]=42;config["unrelated"]="Generated opaque config";test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(encode(config)));
 lt::entry tags(lt::entry::dictionary_type{});tags["3"]["1"]["n"]="Generated consumer";tags["3"]["1"]["c"]["pp.constraint"]=lt::entry::list_type{lt::entry(expression)};tags["3"]["1"]["o"]=lt::entry::list_type{};test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(encode(tags)));
}
ui::Presentation library(const test::OwnedEngineFixture& fixture,std::uint64_t admission=0){auto rows=std::make_shared<ui::LibraryPresentation>();rows->revision=7;ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;item.providerAdmission=admission;item.providerGeneration=admission?88:0;rows->items.push_back(item);return rows;}
bool member(const storage::ManualTagSnapshot& snapshot){for(const auto& row:snapshot.tags()->tags){if(row.id==1){return !row.uniqueHashes.empty();}}return false;}
}
class PeerSetOwnerTests final:public QObject{
 Q_OBJECT
private slots:
 void actualSdkAdmissionProducesGuardedPeerMembership_data(){
  QTest::addColumn<bool>("ipv6");QTest::addColumn<bool>("negative");QTest::addColumn<bool>("combined");
  QTest::newRow("IPv4")<<false<<false<<false;
  QTest::newRow("IPv6")<<true<<false<<false;
  QTest::newRow("IPv4-nonmatching-peer-negative")<<false<<true<<false;
  QTest::newRow("combined-selector-matches-CIDR-on-owned-IPv4")<<false<<false<<true;
  QTest::newRow("combined-selector-matches-network-on-owned-IPv6")<<true<<false<<true;
 }
 void actualSdkAdmissionProducesGuardedPeerMembership(){
  QFETCH(bool,ipv6);QFETCH(bool,negative);QFETCH(bool,combined);
  test::OwnedEngineFixture fixture(false,true);
  QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
  const std::string directCoordinates =
      "hasTagAge(\"Generated peers\")==-1&&tagPosition(\"Generated peers\")==-1"
      "&&tagPosition(\"Generated peers\",2)==-1";
  definitions(fixture, negative ? "!hasTag(\"Generated peers\")" :
      "hasTag(\"Generated peers\")&&"+directCoordinates, ipv6||negative,
      combined?"127.0.0.0/8;IPv6":"");
  auto config=core::Document::parse(test::read(fixture.profile+"/biglybt.config").toStdString());
  test::require(config&&config->replaceValue("locale","5:en_GB"),"Generated known source locale");
  test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(config->serialize()));
  if(ipv6){
   // Normal existing-library mode correctly refuses the old isolated fixture's
   // absent resume map. Its generated payload is all missing,so supply that
   // exact clean original map rather than relaxing the production recheck fence.
   auto state=core::Document::parse(test::read(fixture.stateFile).toStdString());test::require(bool(state),"Generated clean resume state");
   const std::string pieces(test::OwnedEngineFixture::PayloadBytes/16384,'\0');test::require(bool(state->replaceValue("resume","d4:datad6:blocksde11:resume data"+std::to_string(pieces.size())+":"+pieces+"5:validi1eee")),"Generated all-missing resume");
   auto attrs=core::Document::parse(*core::encodeValue(*state->find("attributes")));test::require(bool(attrs)&&bool(attrs->replaceValue("resumecomplete","i1e"))&&bool(state->replaceValue("attributes",attrs->serialize())),"Generated original resume marker");test::write(fixture.stateFile,QByteArray::fromStdString(state->serialize()));
  }
  auto task=std::async(std::launch::async,[&]{
   auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Generated peer profile");auto allocated=engine::PersistentSession::create({1,88,1});test::require(bool(allocated),"Actual peer engine");auto engine=std::move(*allocated);std::unique_ptr<lt::session> ipv6Seed;lt::torrent_handle ipv6Handle;bool connected=false;
   if(ipv6){engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;policy.ipv6=true;policy.bindAddresses={"::1"};test::require(bool(engine->configureNetwork(policy))&&bool(engine->startIdle(*lease)),"Owned IPv6 normal provider");auto settings=test::localSettings();settings.set_str(lt::settings_pack::listen_interfaces,"[::1]:0");settings.set_str(lt::settings_pack::outgoing_interfaces,"::1");ipv6Seed=std::make_unique<lt::session>(settings);const auto raw=test::read(fixture.stateFile);lt::error_code error;auto metadata=std::make_shared<lt::torrent_info>(lt::span<const char>(raw.constData(),std::size_t(raw.size())),error,lt::from_span);test::require(!error,"Generated IPv6 metadata");metadata->clear_trackers();lt::add_torrent_params add;add.ti=std::move(metadata);add.save_path=(fixture.root.path()+"/seed").toStdString();add.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);add.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;ipv6Handle=ipv6Seed->add_torrent(add,error);test::require(!error,"Owned IPv6 partial peer");}
   auto started=engine->startManaged(*lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=ipv6?static_cast<unsigned short>(0):fixture.port(),.forceStart=true});test::require(bool(started),started?"":started.error().message.c_str());
   std::optional<engine::ManagedDownloadStatus> managed;std::optional<engine::PeerSetEndpoints> endpoints;const auto end=std::chrono::steady_clock::now()+15s;
   while(std::chrono::steady_clock::now()<end){test::require(bool(engine->poll()),"Actual engine polling");if(ipv6&&!connected&&engine->tcpListenPort()&&ipv6Handle.status().num_pieces==test::OwnedEngineFixture::PayloadBytes/16384-1){ipv6Handle.connect_peer(lt::tcp::endpoint(lt::address_v6::loopback(),*engine->tcpListenPort()));connected=true;}for(const auto& value:engine->managedDownloads()){if(value.hash==*fixture.hash){managed=value;}}if(managed&&managed->checked){auto actual=engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission);if(actual&&!actual->endpoints.empty()&&managed->received>=test::OwnedEngineFixture::PayloadBytes-16384){endpoints=std::move(*actual);break;}}std::this_thread::sleep_for(10ms);}
   test::require(managed&&endpoints&&std::ranges::any_of(endpoints->endpoints,[&](const auto& peer){return peer.network==sets::Network::Public&&peer.family==(ipv6?sets::Family::Ipv6:sets::Family::Ipv4)&&(ipv6||peer.ipv4==0x7f000001U);}),"Actual SDK connected endpoint not observed");
   test::require(!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission+1)&&!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission,0)&&!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission,513),"Admission/maximum source fence");
   auto wrongThread=std::async(std::launch::async,[&]{return bool(engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission));});test::require(!wrongThread.get(),"Foreign thread entered SDK peer source");
   auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual peer tag writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual peer tag snapshot");auto worker=app::TagConstraintWorker::activate(*lease,88);test::require(bool(worker),"Actual peer constraint worker");app::ConstraintWorkerFrame frame;frame.session=88;frame.library=library(fixture,managed->admission);frame.managed.push_back(*managed);frame.peerEngineIdentity=endpoints->identity;
   auto mode=storage::TagManagerStartupMode::capture(*lease);
   test::require(bool(mode),"Actual peer startup mode");
   auto internal=app::InternalTagNamespace::activate(*lease,88,true,*mode);
   test::require(bool(internal),"Actual peer namespace");
   auto source=(*internal)->snapshot();
   test::require(source&&*source,"Actual peer names");
   frame.internalNamespace=**source;

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
   auto accepted=app::runConstraintPeerTurn(**worker,**store,*snapshot,frame,*lease,engine.get());test::require(frame.sequence==firstSequence+1,"Actual Host handoff did not consume immediately");const auto detail=accepted ? (accepted->kept ? accepted->kept->detail : "Missing matched membership") : accepted.error().message;
   test::require(accepted&&accepted->changed&&!accepted->peerRequest&&member(accepted->snapshot),detail.c_str());test::require(accepted->snapshot.generation()==snapshot->generation()+1,"Peer membership did not use single saved generation");snapshot=accepted->snapshot;
   test::require(test::read(fixture.profile+"/biglybt.config")==configBefore&&test::read(fixture.stateFile)==stateBefore&&test::read(fixture.profile+"/downloads.config")==inventoryBefore,"Tag source changed unrelated original metadata");
   (*worker)->close();test::require(bool((*internal)->close()),"Peer names close");test::require(bool((*store)->close()),"Peer writer close");auto closed=engine->close(std::chrono::steady_clock::now()+5s);test::require(closed.sessionJoined&&!engine->inspectPeerSetEndpoints(*fixture.hash,managed->admission),"Closed peer engine provided observations");return true;
  });QVERIFY(task.get());
 }
 void completeCountUsesTwoOwnedTorrentsAtTheSameAddress(){
  test::OwnedEngineFixture first(false,true),second(true,true);
  QTRY_VERIFY_WITH_TIMEOUT(first.ready()&&second.ready(),30000);
  definitions(first,"countTag(\"Generated peers\")==2",false,"127.0.0.0/8;IPv6");
  const auto settingsPath=first.profile+"/biglybt.config";
  auto config=core::Document::parse(test::read(settingsPath).toStdString());
  test::require(config&&config->replaceValue("locale","5:en_GB"),"Known count census locale");
  test::write(settingsPath,QByteArray::fromStdString(config->serialize()));
  const auto firstInventory=first.profile+"/downloads.config";
  const auto secondInventory=second.profile+"/downloads.config";
  auto inventory=core::Document::parse(test::read(firstInventory).toStdString());
  const auto secondary=core::Document::parse(test::read(secondInventory).toStdString());
  test::require(inventory&&secondary&&inventory->find("downloads")&&secondary->find("downloads"),
      "Generated two-torrent schemas");
  const auto& firstRow=inventory->find("downloads")->children.at(0);
  const auto& secondRow=secondary->find("downloads")->children.at(0);
  auto firstBytes=core::encodeValue(firstRow),secondBytes=core::encodeValue(secondRow);
  test::require(firstBytes&&secondBytes,"Generated two-torrent rows");
  auto ranked=core::Document::parse(*secondBytes);
  test::require(ranked&&ranked->setInteger("position",2),"Exact second queue order");
  test::require(bool(inventory->replaceValue("downloads","l"+*firstBytes+ranked->serialize()+'e')),"Combined held inventory");
  test::write(firstInventory,QByteArray::fromStdString(inventory->serialize()));
  const auto secondState=first.profile+"/active/"+QString::fromStdString(second.hash->hex())+".dat";
  test::write(secondState,test::read(second.stateFile));

  auto task=std::async(std::launch::async,[&]{
   auto lease=storage::ProfileLease::acquire(first.native());
   test::require(bool(lease),"Actual held two-torrent profile");
   auto foreign=storage::ProfileLease::acquire(second.native());
   test::require(bool(foreign),"Independent fixture lease");
   auto owner=engine::PersistentSession::create({1,88,1});
   test::require(bool(owner),"Actual complete peer counter engine");
   auto engine=std::move(*owner);
   const auto one=engine->startManaged(*lease,{.hash=*first.hash,.ownedLoopbackPeerPort=first.port(),.forceStart=true});
   test::require(bool(one),one?"":one.error().message.c_str());
   const auto two=engine->startManaged(*lease,{.hash=*second.hash,.ownedLoopbackPeerPort=second.port(),.forceStart=true});
   test::require(bool(two),two?"":two.error().message.c_str());
   std::optional<engine::PeerSetCensus> observed;
   const auto end=std::chrono::steady_clock::now()+15s;
   while(std::chrono::steady_clock::now()<end){
    test::require(bool(engine->poll()),"Drive both owned torrents");
    auto census=engine->inspectPeerSetCensus(*lease);
    if(census&&census->torrents.size()==2&&std::ranges::all_of(census->torrents,[](const auto& row){
       return row.endpoints.size()==1&&row.endpoints.front().ipv4==0x7f000001U;})){
     observed=std::move(*census);
     break;
    }
    std::this_thread::sleep_for(10ms);
   }
   test::require(observed.has_value(),"Complete authenticated cohort with two same-IP connections");
   test::require(!engine->inspectPeerSetCensus(*foreign)&&!engine->inspectPeerSetCensus(*lease,1),
       "Peer census admitted another lease or a truncated cohort");
   auto otherThread=std::async(std::launch::async,[&]{return bool(engine->inspectPeerSetCensus(*lease));});
   test::require(!otherThread.get(),"Complete peer census admitted another thread");
   auto store=storage::ManualTagStore::open(*lease);
   test::require(bool(store),"Actual saved named-count tag store");
   auto snapshot=(*store)->snapshot();
   auto worker=app::TagConstraintWorker::activate(*lease,88);
   auto startup=storage::TagManagerStartupMode::capture(*lease);
   test::require(snapshot&&worker&&startup,"Actual eligible named-count worker");
   auto internal=app::InternalTagNamespace::activate(*lease,88,true,*startup);
   test::require(bool(internal),"Actual count namespace");
   auto names=(*internal)->snapshot();
   test::require(names&&*names,"Actual complete names");
   auto view=std::make_shared<ui::LibraryPresentation>();
   view->revision=7;
   app::ConstraintWorkerFrame frame;
   frame.session=88;
   frame.sequence=1;
   frame.sourceRevision=lease->sourceRevision();
   frame.library=view;
   frame.managed=engine->managedDownloads();
   frame.peerEngineIdentity=observed->identity;
   frame.internalNamespace=**names;
   for(const auto& row:frame.managed){
    ui::LibraryItem item;
    item.hash=QString::fromStdString(row.hash.hex());
    item.bytes=row.bytes;
    item.providerAdmission=row.admission;
    item.providerGeneration=88;
    view->items.push_back(std::move(item));
   }
   const auto request=(*worker)->step(**store,*snapshot,frame);
   test::require(request&&request->peerRequest&&request->peerRequest->scope==app::ConstraintPeerRequest::Scope::Complete
       &&!request->changed,"Named count did not request the complete peer census");
   frame.peerCensus=app::ConstraintPeerCensusObservation{*request->peerRequest,*observed,std::chrono::steady_clock::now()};
   frame.peerCensus->value->torrents.pop_back();
   ++frame.sequence;
   const auto partial=(*worker)->step(**store,*snapshot,frame);
   test::require(partial&&partial->evaluated&&!partial->changed,"Incomplete cohort supplied a named peer count");
   frame.peerCensus.reset();
   const auto safeBefore=test::read(firstInventory);
   const auto turn=[&] {
    ++frame.sequence;
    frame.sourceRevision=lease->sourceRevision();
    auto value=app::runConstraintPeerTurn(**worker,**store,*snapshot,frame,*lease,engine.get());
    test::require(bool(value),value?"":value.error().message.c_str());
    return std::move(*value);
   };
   auto early=turn();
   test::require(!early.changed&&!early.evaluated&&!early.peerRequest,
       "Peer rule ran again before the original cadence");
   std::this_thread::sleep_for(5100ms);
   auto applied=turn();
   const auto detail=applied.kept?applied.kept->detail:"Two actual distinct peer handles did not reach the named tag rule";
   test::require(applied.changed&&member(applied.snapshot),detail.c_str());
   test::require(test::read(firstInventory)==safeBefore,"Peer count tag update rewrote the inventory");
   snapshot=applied.snapshot;
   for(unsigned remaining=0;remaining<8;++remaining){
    const auto next=turn();
    if(next.changed){snapshot=next.snapshot;}
    if(!next.evaluated){break;}
   }
   const auto memberCount=[&] {
    for(const auto& tag:snapshot->tags()->tags){if(tag.id==1){return tag.uniqueHashes.size();}}
    return std::size_t{0};
   };
   test::require(memberCount()==2,"The first complete peer cache did not wake both matching downloads");
   const auto echo=[&](const auto& change){
    std::this_thread::sleep_for(5100ms);
    ++frame.sequence;
    frame.sourceRevision=lease->sourceRevision();
    const auto tick=(*worker)->step(**store,*snapshot,frame);
    test::require(tick&&tick->peerRequest&&tick->peerRequest->scope==app::ConstraintPeerRequest::Scope::Complete,
        "Original peer-cache tick did not request current SDK data");
    auto current=engine->inspectPeerSetCensus(*lease);
    test::require(current&&current->torrents.size()==2&&
        std::ranges::all_of(current->torrents,[](const auto& row){return row.endpoints.size()==1;}),
        "Both actual Public routes stayed current for the controlled membership changes");
    change(*current);
    frame.peerCensus=app::ConstraintPeerCensusObservation{*tick->peerRequest,
        std::move(*current),std::chrono::steady_clock::now()};
    ++frame.sequence;
    auto decision=(*worker)->step(**store,*snapshot,frame);
    test::require(bool(decision),decision?"":decision.error().message.c_str());
    return std::move(*decision);
   };
   auto settings=storage::ConfigFile::load(*lease,"biglybt.config");
   test::require(settings&&settings->document().replaceValue("unrelated","7:updated")&&settings->save(*lease),
       "Change only the unrelated original setting under the same held profile");
   const auto anotherOnSameDownload=echo([](auto& sample){
    auto& connected=sample.torrents.front().endpoints;
    connected.push_back(connected.front());
   });
   test::require(!anotherOnSameDownload.changed&&!anotherOnSameDownload.evaluated&&memberCount()==2,
       "A raw peer count change woke a download whose peer-set membership stayed equal");
   const auto unassigned=echo([](auto& sample){sample.torrents.front().endpoints.clear();});
   test::require(unassigned.changed&&unassigned.hash==observed->torrents.front().hash&&unassigned.evaluated==1,
       "Only the download losing its last matching peer should be rechecked");
   snapshot=unassigned.snapshot;
   test::require(memberCount()==1,"The unchanged second download lost its rule membership");
   (*worker)->close();
   test::require(bool((*internal)->close())&&bool((*store)->close()),"Complete peer source owners release");
   const auto closed=engine->close(std::chrono::steady_clock::now()+5s);
   test::require(closed.sessionJoined&&!engine->inspectPeerSetCensus(*lease),"Closed engine retained a peer census");
   return true;
  });
  QVERIFY(task.get());
 }
 void unavailableOrForeignProviderCannotCreatePeerMembership_data(){QTest::addColumn<int>("bad");for(int i=0;i<5;++i){QTest::newRow(qPrintable(QString::number(i)))<<i;}}
 void unavailableOrForeignProviderCannotCreatePeerMembership(){
  QFETCH(int,bad);test::OwnedEngineFixture fixture(false);definitions(fixture,"hasTag(\"Generated peers\")");auto task=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Generated guard profile");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Guard writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Guard snapshot");auto worker=app::TagConstraintWorker::activate(*lease,88);test::require(bool(worker),"Guard worker");
   engine::ManagedDownloadStatus managed{*fixture.hash};managed.admission=11;managed.retained=managed.checked=true;managed.phase=engine::ManagedPhase::Seeding;managed.bytes=managed.completed=test::OwnedEngineFixture::PayloadBytes;managed.complete=true;app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library(fixture,11);frame.managed={managed};frame.peerEngineIdentity=engine::AccountingIdentity{1,88,1};auto requested=(*worker)->step(**store,*snapshot,frame);test::require(requested&&requested->peerRequest,"Guard request");
   app::ConstraintPeerObservation reply{*requested->peerRequest,engine::PeerSetEndpoints{{1,88,1},*fixture.hash,11,{{sets::Network::Public,sets::Family::Ipv4,0x7f000001U}}},std::chrono::steady_clock::now()};if(bad==0){reply.value.reset();}if(bad==1){++reply.value->identity.engineEpoch;}if(bad==2){++reply.value->admission;}if(bad==3){reply.value->endpoints={{}};}if(bad==4){reply.value->endpoints=std::vector<sets::Endpoint>(513,{sets::Network::Public,sets::Family::Ipv4,0x7f000001U});}frame.peerObservations={reply};++frame.sequence;auto denied=(*worker)->step(**store,*snapshot,frame);test::require(denied&&!denied->changed&&!member(*snapshot),"Unavailable/foreign source made membership");(*worker)->close();test::require(bool((*store)->close()),"Guard close");return true;});QVERIFY(task.get());
 }
 void nonPeerFunctionsAndInactiveProvidersDoNotRequestSdk_data(){QTest::addColumn<QString>("expression");for(const auto* expression:{"true","hasTag(\"Unknown\")","hasTagAge(\"Generated peers\")>0","countTag(\"Generated peers\")>0","hasTagGroup(\"Generated peers\")"}){QTest::newRow(expression)<<QString::fromLatin1(expression);}}
 void nonPeerFunctionsAndInactiveProvidersDoNotRequestSdk(){
  QFETCH(QString,expression);test::OwnedEngineFixture fixture(false);definitions(fixture,expression.toStdString());auto task=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"No-demand profile");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"No-demand writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"No-demand snapshot");auto worker=app::TagConstraintWorker::activate(*lease,88);test::require(bool(worker),"No-demand worker");app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library(fixture);auto result=(*worker)->step(**store,*snapshot,frame);test::require(result&&!result->peerRequest,"Unneeded SDK peer demand");(*worker)->close();test::require(bool((*store)->close()),"No-demand close");return true;});QVERIFY(task.get());
 }
 void hasTagDemandMetadataIsExact(){auto expression=core::tags::constraints::compile(u"hasTag(\"One\") && (hasTagAge(\"Age\")>0 || hasTag(\"One\") || hasTagGroup(\"Group\"))");QVERIFY(expression);QCOMPARE(expression->hasTagNames().size(),std::size_t{1});QCOMPARE(expression->hasTagNames()[0],std::u16string(u"One"));}
};
QTEST_GUILESS_MAIN(PeerSetOwnerTests)
#include "peer_set_owner_tests.moc"
