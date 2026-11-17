// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_session.hpp"
#include "tag_assignment_owner.hpp"
#include "profile_tag_rates.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
using namespace biglybtc;using namespace std::chrono_literals;
namespace {
void require(bool value,const char* reason){test::require(value,reason);}
std::string b32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,buffer=0;for(auto v:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(v);bits+=8;while(bits>=5){bits-=5;out+=chars[(buffer>>bits)&31];}}return out;}
struct Area {
 test::OwnedEngineFixture fixture{false};std::optional<storage::ProfileLease> lease;std::optional<storage::TagManagerStartupMode> mode;
 std::unique_ptr<storage::ManualTagStore> store;storage::ManualTagSnapshot source;
 std::unique_ptr<engine::PersistentSession> engine;std::unique_ptr<storage::TagAssignmentOwner> actor;
 explicit Area(int tags=1,int existing=0){
  libtorrent::entry saved(libtorrent::entry::dictionary_type{});for(int id=1;id<=tags;++id){auto& row=saved["3"][std::to_string(id)];row["n"]="Generated rate tag";row["c"]["rl.down"]=16384;row["o"]=libtorrent::entry::list_type{};if(id<=existing){row["o"].list().emplace_back(b32(*fixture.hash));}}
  test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(saved)));
  auto held=storage::ProfileLease::acquire(fixture.native());require(bool(held),"Actual source lease");lease.emplace(std::move(*held));auto captured=storage::TagManagerStartupMode::capture(*lease);require(bool(captured),"Actual startup mode");mode.emplace(std::move(*captured));
  auto opened=storage::ManualTagStore::open(*lease);require(bool(opened),"Actual source store");store=std::move(*opened);source=store->snapshot().value();auto owner=engine::PersistentSession::create({701,702,703});require(bool(owner),"Qualified actual engine");engine=std::move(*owner);require(bool(engine->configureTagManagerStartup(*mode)),"Actual engine startup mode");require(bool(engine->startIdle(*lease)),"Actual idle engine");auto created=storage::TagAssignmentOwner::activate(*lease,702,*mode);require(bool(created),"Actual assignment actor");actor=std::move(*created);
 }
 storage::TagAssignmentObservation observe(){return {*fixture.hash,702,lease->sourceRevision(),70,0,0};}
 auto prepare(int id){const std::array ids{id};return engine->prepareTagRateSource(*lease,*store,source,ids);}
 void reload(){source=store->reload().value();}
};
}
class TagRateAssignmentTests final:public QObject {
 Q_OBJECT
private slots:
 void positiveDependencyPlanStagesButNeedsActualPreparedOwner(){
  Area a(2);auto plan=a.store->planMembership(a.source);QVERIFY(plan);
  for(int id:{1,2}){auto next=a.store->extendMembershipPlan(*plan,storage::ManualTagMembershipEdit{core::SavedTagAddMember{id,*a.fixture.hash,123}});QVERIFY(next);plan=std::move(next);}
  const auto before=test::read(a.fixture.profile+"/tag.config");QVERIFY(!a.store->publishMembershipPlan(*plan));QCOMPARE(test::read(a.fixture.profile+"/tag.config"),before);
  const std::array ids{1,2};auto prepared=a.engine->prepareTagRateSource(*a.lease,*a.store,a.source,ids);QVERIFY(prepared);
  auto committed=a.actor->publishPlan(*a.store,a.source,*plan,a.observe(),{},&*prepared);QVERIFY2(committed,committed?"":committed.error().message.c_str());QVERIFY(committed->commit.changed);QVERIFY(!committed->receipt);QVERIFY(a.engine->synchronizeSavedTagRates());
 }

 void scopedActorAndPrivateStore(){
  Area a;auto prepared=a.prepare(1);QVERIFY(prepared);QVERIFY(prepared->matchesAssignment(*a.lease,*a.store,a.source,1,702));QVERIFY(!prepared->matchesAssignment(*a.lease,*a.store,a.source,1,999));
  const auto bytes=test::read(a.fixture.profile+"/tag.config");
  auto denied=a.actor->change(*a.store,a.source,1,a.observe(),true,123);QVERIFY(!denied);QCOMPARE(test::read(a.fixture.profile+"/tag.config"),bytes);
  auto saved=a.actor->change(*a.store,a.source,1,a.observe(),true,123,{},&*prepared);QVERIFY2(saved,saved?"":saved.error().message.c_str());QVERIFY(saved->commit.changed);QVERIFY(!saved->receipt);a.source=saved->commit.snapshot;
  QVERIFY(!prepared->matchesAssignment(*a.lease,*a.store,a.source,1,702));QVERIFY(a.engine->synchronizeSavedTagRates());
  auto current=a.prepare(1);QVERIFY(current);auto removed=a.actor->changeSubjects(*a.store,a.source,1,std::array{a.observe()},false,124,{},&*current);QVERIFY(removed);QVERIFY(removed->commit.changed);QVERIFY(removed->receipts.empty());QVERIFY(a.engine->synchronizeSavedTagRates());
 }
 void rawSdkPreparationCannotGrantAssignmentScope(){
  Area a;auto controller=engine::ProfileTagRates::open(*a.lease,*a.mode);QVERIFY(controller);libtorrent::session sdk(test::localSettings());const std::array ids{1};auto raw=(*controller)->prepareSource(*a.lease,*a.store,a.source,ids,sdk);QVERIFY(raw);QVERIFY(raw->matches(*a.lease,*a.store,a.source,1));QVERIFY(!raw->matchesAssignment(*a.lease,*a.store,a.source,1,702));
  const auto before=test::read(a.fixture.profile+"/tag.config");auto refused=a.actor->change(*a.store,a.source,1,a.observe(),true,123,{},&*raw);QVERIFY(!refused);QCOMPARE(test::read(a.fixture.profile+"/tag.config"),before);
 }
 void prospectiveFanInRefusedBeforeCas(){
  Area a(13,12);const auto before=test::read(a.fixture.profile+"/tag.config"),state=test::read(a.fixture.stateFile);auto prepared=a.prepare(13);QVERIFY(prepared);
  auto result=a.actor->change(*a.store,a.source,13,a.observe(),true,123,{},&*prepared);QVERIFY(!result);QCOMPARE(test::read(a.fixture.profile+"/tag.config"),before);QCOMPARE(test::read(a.fixture.stateFile),state);QVERIFY(!QFile::exists(a.fixture.profile+"/tag.config.bak"));QVERIFY(!QFile::exists(a.fixture.profile+"/tag.config.saving"));
 }
 void postCommitRatesReachActualPausedTarget(){
  Area a;QTRY_VERIFY_WITH_TIMEOUT(a.fixture.ready(),10000);auto started=a.engine->startManaged(*a.lease,{*a.fixture.hash,a.fixture.port(),true,false,true});QVERIFY2(started,started?"":started.error().message.c_str());
  auto prepared=a.prepare(1);QVERIFY(prepared);auto saved=a.actor->change(*a.store,a.source,1,a.observe(),true,123,{},&*prepared);QVERIFY(saved);a.source=saved->commit.snapshot;QVERIFY(a.engine->synchronizeSavedTagRates());QVERIFY(a.engine->pauseManaged(*a.fixture.hash,false));
  const auto received=[&]{return a.engine->managedDownloads().front().received;};
  const auto progressed=[&]{if(!a.engine->poll()){return false;}return received()>4096;};QTRY_VERIFY_WITH_TIMEOUT(progressed(),15000);QVERIFY(received()<test::OwnedEngineFixture::PayloadBytes);
  a.reload();prepared=a.prepare(1);QVERIFY(prepared);auto removed=a.actor->change(*a.store,a.source,1,a.observe(),false,124,{},&*prepared);QVERIFY(removed);a.source=removed->commit.snapshot;QVERIFY(a.engine->synchronizeSavedTagRates());
  const auto complete=[&]{return bool(a.engine->poll())&&a.engine->managedDownloads().front().complete;};QTRY_VERIFY_WITH_TIMEOUT(complete(),15000);
  auto closed=a.engine->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined);QVERIFY(!closed.checkpointFailure);QVERIFY(a.fixture.intact());
 }
};
QTEST_GUILESS_MAIN(TagRateAssignmentTests)
#include "tag_rate_assignment_tests.moc"
