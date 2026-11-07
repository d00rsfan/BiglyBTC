// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_fixture.hpp"
#include "metasearch_magnet_admission.hpp"
#include "persistent_session.hpp"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QtTest>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace f=metasearch_fixture;namespace s=biglybtc::search;namespace e=biglybtc::engine;using namespace biglybtc;
namespace {
const std::string V1="magnet:?xt=urn:btih:0123456789012345678901234567890123456789&dn=Owned";
thread_local std::stop_source* cancelAtSave=nullptr;
thread_local int failSync=0,syncCount=0;
std::string raw(const core::Document& doc,const core::Node& node){return doc.originalBytes().substr(node.begin,node.end-node.begin);}
e::NetworkPolicy policy(const f::Profile& area){auto file=storage::ConfigFile::load(*area.lease,"biglybt.config");f::w::require(bool(file),"Cannot read actual policy");auto value=e::readNetworkPolicy(&file->document(),{.mode=e::NetworkMode::Normal});f::w::require(bool(value),"Cannot parse actual policy");return std::move(*value);}
void reply(f::w::Server& server,const std::string& uri){QJsonObject row{{"title","Owned magnet"},{"download",QString::fromStdString(uri)}};server.body=QJsonDocument(QJsonObject{{"rows",QJsonArray{row}}}).toJson(QJsonDocument::Compact);}
s::MetasearchResultKey performSearch(s::SavedMetasearchService& service,f::Profile& area,const std::string& text="query"){
  auto begun=service.beginSearch(*area.lease,301,{area.selector()},text);f::w::require(bool(begun),"Cannot begin actual search");std::vector<s::MetasearchSearchUpdate> updates;QElapsedTimer timer;timer.start();
  while((updates.empty()||!updates.back().done)&&timer.elapsed()<10000){f::pump(service,*area.lease,301,updates);QTest::qWait(1);}
  f::w::require(!updates.empty()&&updates.back().done&&!updates.back().hadFailures&&updates.front().engine.rows.size()==1,"Actual fixture search did not return its owned row");return updates.front().engine.rows.front().key;
}
QString destination(f::Profile& area){const auto path=area.root.filePath("destination");f::w::require(QDir().mkpath(path),"Cannot create actual reviewed destination");return path;}
void noIntake(const f::Profile& area){f::w::require(!QFile::exists(area.path+"/downloads.config")&&!QFile::exists(area.path+"/active"),"Magnet review/queue imported ordinary payload state");}
}
extern "C" int __real_openat(int,const char*,int,...);
extern "C" int __wrap_openat(int directory,const char* name,int flags,...){
  mode_t mode=0;if(flags&O_CREAT){va_list arguments;va_start(arguments,flags);mode=static_cast<mode_t>(va_arg(arguments,int));va_end(arguments);}
  if(cancelAtSave&&name&&std::string_view(name)=="biglybt.config.saving"&&(flags&O_CREAT)){auto* stop=cancelAtSave;cancelAtSave=nullptr;stop->request_stop();}
  return flags&O_CREAT?__real_openat(directory,name,flags,mode):__real_openat(directory,name,flags);
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){if(failSync&&++syncCount==failSync){errno=EIO;return -1;}return __real_fsync(fd);}
class MetasearchMagnetTests final:public QObject {
 Q_OBJECT
private slots:
 void actualRetainedReviewAndTwoQueriesPreserveOriginalPendingRows(){
  f::w::Server server;reply(server,V1);f::Profile area(server.url("/search?q=%s"));f::w::write(area.path+"/biglybt.config",f::dictionary({{"opaque",f::dictionary({{"future",f::w::b("retain-raw")}})}}));const auto before=f::bytes(area.path+"/biglybt.config");const auto dest=destination(area);const auto net=policy(area);int made=0;
  auto service=s::SavedMetasearchService::open(*area.lease,area.path.toStdString(),301,s::MetasearchServiceMode::Normal,f::factory(area,made));QVERIFY(service);
  {
   const auto key=performSearch(**service,area);std::stop_source receiver;auto handle=(*service)->retainMagnetResult(*area.lease,301,key,receiver.get_token());QVERIFY(handle);QVERIFY(!(*service)->retainResult(*area.lease,301,key));auto review=s::MetasearchMagnetAdmission::prepare(*area.lease,area.path.toStdString(),301,*handle,net,receiver.get_token());QVERIFY(review);QCOMPARE((*review)->description().key,key);QCOMPARE((*review)->description().name,std::string("Owned magnet"));QVERIFY((*review)->description().identity.starts_with("btih:"));QCOMPARE(f::bytes(area.path+"/biglybt.config"),before);auto none=e::readPendingMagnets(*area.lease,net);QVERIFY(none&&none->empty());noIntake(area);
   auto committed=(*review)->confirm(*area.lease,301,dest.toStdString(),net,{});QCOMPARE(committed.state,e::PendingMagnetCommitState::Committed);QVERIFY(committed.pending&&!committed.failure);QVERIFY(committed.pending->stopped&&!committed.pending->forceStart);QCOMPARE(committed.pending->network,e::MagnetNetwork::Public);QCOMPARE(committed.pending->uri,V1);QCOMPARE(committed.pending->destination,std::optional{std::filesystem::path(dest.toStdString())});QVERIFY(!handle->current(*area.lease,301));QVERIFY(!(*service)->retainMagnetResult(*area.lease,301,key,receiver.get_token()));
   auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");QVERIFY(config);auto old=core::Document::parse(before.toStdString());QVERIFY(old);QCOMPARE(raw(config->document(),*config->document().find("opaque")),raw(*old,*old->find("opaque")));auto saved=core::Document::parse(committed.pending->sourceEntry);QVERIFY(saved);QCOMPARE(saved->find("args")->bytes,std::string_view(V1).substr(8));QCOMPARE(saved->find("ss")->integer,std::int64_t{3});QCOMPARE(saved->find("hash")->bytes.size(),std::size_t{20});
   const auto firstRow=committed.pending->sourceEntry;reply(server,"magnet:?xt=urn:btih:1111111111111111111111111111111111111111&dn=Second");const auto next=performSearch(**service,area,"second-query");auto second=(*service)->retainMagnetResult(*area.lease,301,next,receiver.get_token());QVERIFY(second);auto nextReview=s::MetasearchMagnetAdmission::prepare(*area.lease,area.path.toStdString(),301,*second,net,receiver.get_token());QVERIFY(nextReview);auto nextCommit=(*nextReview)->confirm(*area.lease,301,dest.toStdString(),net,{});QCOMPARE(nextCommit.state,e::PendingMagnetCommitState::Committed);auto pending=e::readPendingMagnets(*area.lease,net);QVERIFY(pending);QCOMPARE(pending->size(),std::size_t{2});QVERIFY(std::ranges::any_of(*pending,[&](const auto& item){return item.sourceEntry==firstRow;}));noIntake(area);QCOMPARE(made,2);
  }
  QVERIFY((*service)->close());area.lease.reset();auto reacquired=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(reacquired);auto restored=e::readPendingMagnets(*reacquired,net);QVERIFY(restored&&restored->size()==2);QVERIFY(std::ranges::all_of(*restored,[](const auto& p){return p.stopped;}));
 }
 void supportedFullIdentitiesAndUnsupportedRoutes_data(){
  QTest::addColumn<QString>("uri");QTest::addColumn<bool>("handleExpected");QTest::addColumn<bool>("reviewExpected");QTest::addColumn<bool>("v2");
  QTest::newRow("v1")<<QString::fromStdString(V1)<<true<<true<<false;
  QTest::newRow("pure-v2")<<QString::fromStdString("magnet:?xt=urn:btmh:1220"+std::string(64,'a'))<<true<<true<<true;
  QTest::newRow("hybrid-full-identities")<<QString::fromStdString(V1+"&xt=urn:btmh:1220"+std::string(64,'b'))<<true<<true<<true;
  QTest::newRow("http-domain")<<QStringLiteral("https://files.example.org/torrent")<<false<<false<<false;
  QTest::newRow("i2p-route")<<QString::fromStdString(V1+"&net=I2P")<<true<<false<<false;
  QTest::newRow("explicit-metadata-url")<<QString::fromStdString(V1+"&xs=https%3A%2F%2Ffiles.example.org%2Fa")<<true<<false<<false;
  QTest::newRow("conflicting-v1")<<QString::fromStdString(V1+"&xt=urn:btih:"+std::string(40,'a'))<<true<<false<<false;
  QTest::newRow("zero-hash")<<QString::fromStdString("magnet:?xt=urn:btih:"+std::string(40,'0'))<<true<<false<<false;
 }
 void supportedFullIdentitiesAndUnsupportedRoutes(){
  QFETCH(QString,uri);QFETCH(bool,handleExpected);QFETCH(bool,reviewExpected);QFETCH(bool,v2);f::w::Server server;reply(server,uri.toStdString());f::Profile area(server.url("/search?q=%s"));const auto net=policy(area);const auto original=f::bytes(area.path+"/biglybt.config");int made=0;auto service=s::SavedMetasearchService::open(*area.lease,area.path.toStdString(),301,s::MetasearchServiceMode::Normal,f::factory(area,made));QVERIFY(service);const auto key=performSearch(**service,area);std::stop_source receiver;auto handle=(*service)->retainMagnetResult(*area.lease,301,key,receiver.get_token());QCOMPARE(bool(handle),handleExpected);if(handle){auto review=s::MetasearchMagnetAdmission::prepare(*area.lease,area.path.toStdString(),301,*handle,net,receiver.get_token());QCOMPARE(bool(review),reviewExpected);if(review){QCOMPARE((*review)->description().identity.find("btmh:1220")!=std::string::npos,v2);}}QCOMPARE(f::bytes(area.path+"/biglybt.config"),original);noIntake(area);QVERIFY((*service)->close());
 }
 void precommitRevocationUsesActualSourceReceiverAndDestination_data(){QTest::addColumn<int>("mode");QTest::newRow("download-cancel")<<0;QTest::newRow("dispatch-cancel")<<1;QTest::newRow("query-replacement")<<2;QTest::newRow("actual-source-edit")<<3;QTest::newRow("stale-live-policy")<<4;QTest::newRow("other-session")<<5;QTest::newRow("symbolic-destination")<<6;QTest::newRow("detached-download-token")<<7;}
 void precommitRevocationUsesActualSourceReceiverAndDestination(){
  QFETCH(int,mode);f::w::Server server;reply(server,V1);f::Profile area(server.url("/search?q=%s"));const auto net=policy(area);QString dest=destination(area);int made=0;auto service=s::SavedMetasearchService::open(*area.lease,area.path.toStdString(),301,s::MetasearchServiceMode::Normal,f::factory(area,made));QVERIFY(service);const auto key=performSearch(**service,area);std::stop_source receiver,dispatch;auto handle=(*service)->retainMagnetResult(*area.lease,301,key,receiver.get_token());QVERIFY(handle);
  if(mode==7){QVERIFY(!s::MetasearchMagnetAdmission::prepare(*area.lease,area.path.toStdString(),301,*handle,net,{}));QVERIFY(!(*service)->retainMagnetResult(*area.lease,301,key,{}));}
  else{
   auto review=s::MetasearchMagnetAdmission::prepare(*area.lease,area.path.toStdString(),301,*handle,net,receiver.get_token());QVERIFY(review);auto actualNet=net;
   if(mode==0){receiver.request_stop();}else if(mode==1){dispatch.request_stop();}else if(mode==2){QVERIFY((*service)->beginSearch(*area.lease,301,{area.selector()},"replacement"));}else if(mode==3){auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().replaceValue("metasearch.config.proxy.type","i1e")&&config->save(*area.lease));}else if(mode==4){actualNet.outgoingTcp=!actualNet.outgoingTcp;}else if(mode==6){const auto link=area.root.filePath("symbolic");QVERIFY(::symlink(dest.toUtf8().constData(),link.toUtf8().constData())==0);dest=link;}
   const auto before=f::bytes(area.path+"/biglybt.config");auto result=(*review)->confirm(*area.lease,mode==5?302:301,dest.toStdString(),actualNet,dispatch.get_token());QCOMPARE(result.state,e::PendingMagnetCommitState::Rejected);QVERIFY(result.failure&&!result.pending);QCOMPARE(f::bytes(area.path+"/biglybt.config"),before);
  }
  auto pending=e::readPendingMagnets(*area.lease,net);QVERIFY(pending&&pending->empty());QVERIFY(!QFile::exists(area.path+"/biglybt.config.saving"));noIntake(area);QVERIFY((*service)->close());
 }
 void lateCancellationAndPostPublicationFailureRemainTruthful_data(){QTest::addColumn<bool>("uncertain");QTest::newRow("cancel-after-prejournal-boundary")<<false;QTest::newRow("final-directory-sync-failure")<<true;}
 void lateCancellationAndPostPublicationFailureRemainTruthful(){
  QFETCH(bool,uncertain);f::w::Server server;reply(server,V1);f::Profile area(server.url("/search?q=%s"));const auto net=policy(area);const auto dest=destination(area);int made=0;auto service=s::SavedMetasearchService::open(*area.lease,area.path.toStdString(),301,s::MetasearchServiceMode::Normal,f::factory(area,made));QVERIFY(service);const auto key=performSearch(**service,area);std::stop_source receiver;auto handle=(*service)->retainMagnetResult(*area.lease,301,key,receiver.get_token());QVERIFY(handle);auto review=s::MetasearchMagnetAdmission::prepare(*area.lease,area.path.toStdString(),301,*handle,net,receiver.get_token());QVERIFY(review);
  if(uncertain){syncCount=0;failSync=3;}else{cancelAtSave=&receiver;}auto result=(*review)->confirm(*area.lease,301,dest.toStdString(),net,{});failSync=0;cancelAtSave=nullptr;
  QCOMPARE(result.state,uncertain?e::PendingMagnetCommitState::Uncertain:e::PendingMagnetCommitState::Committed);QCOMPARE(bool(result.failure),uncertain);if(!uncertain){QVERIFY(receiver.stop_requested()&&result.pending);}auto pending=e::readPendingMagnets(*area.lease,net);QVERIFY(pending&&pending->size()==1&&pending->front().stopped);QVERIFY(!handle->current(*area.lease,301));const auto bytes=f::bytes(area.path+"/biglybt.config");auto repeat=(*review)->confirm(*area.lease,301,dest.toStdString(),net,{});QCOMPARE(repeat.state,e::PendingMagnetCommitState::Rejected);QCOMPARE(f::bytes(area.path+"/biglybt.config"),bytes);noIntake(area);QVERIFY((*service)->close());
 }
 void actualUnavailableEngineAdmissionKeepsTheCommittedRecoveryIntent(){
  f::w::Server server;reply(server,V1);f::Profile area(server.url("/search?q=%s"));const auto net=policy(area);const auto dest=destination(area);int made=0;auto service=s::SavedMetasearchService::open(*area.lease,area.path.toStdString(),301,s::MetasearchServiceMode::Normal,f::factory(area,made));QVERIFY(service);const auto key=performSearch(**service,area);std::stop_source receiver;auto handle=(*service)->retainMagnetResult(*area.lease,301,key,receiver.get_token());QVERIFY(handle);auto review=s::MetasearchMagnetAdmission::prepare(*area.lease,area.path.toStdString(),301,*handle,net,receiver.get_token());QVERIFY(review);auto result=(*review)->confirm(*area.lease,301,dest.toStdString(),net,{});QCOMPARE(result.state,e::PendingMagnetCommitState::Committed);QVERIFY(result.pending);
  auto owner=e::PersistentSession::create({301,301,1});QVERIFY(owner);const auto closed=(*owner)->close(std::chrono::steady_clock::now()+std::chrono::seconds(2));QVERIFY(closed.sessionJoined);QVERIFY(!(*owner)->managedAvailable());auto admitted=(*owner)->startMagnet(*area.lease,*result.pending,area.path.toStdString());QVERIFY(!admitted);QCOMPARE(admitted.error().code,storage::ErrorCode::Conflict);QCOMPARE(admitted.error().message,std::string("Magnet admission is unavailable"));auto pending=e::readPendingMagnets(*area.lease,net);QVERIFY(pending&&pending->size()==1);QCOMPARE(pending->front().sourceEntry,result.pending->sourceEntry);noIntake(area);QVERIFY((*service)->close());
 }
};
QTEST_GUILESS_MAIN(MetasearchMagnetTests)
#include "metasearch_magnet_tests.moc"
