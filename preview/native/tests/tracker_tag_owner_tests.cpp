// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_constraint_worker.hpp"
#include "owned_engine_fixture.hpp"
#include <QCoreApplication>
#include <iostream>
#include <cstddef>
#include <chrono>
#include <thread>
namespace b=biglybtc;
namespace a=b::app;
namespace c=b::core;
namespace s=b::storage;
namespace test=b::test;
namespace {
using test::require;
std::string base32(const c::TorrentHash& hash){const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";unsigned accumulator=0,bits=0;std::string out;for(auto value:hash.bytes()){accumulator=(accumulator<<8)|std::to_integer<unsigned char>(value);bits+=8;while(bits>=5){bits-=5;out+=alphabet[(accumulator>>bits)&31];}}if(bits){out+=alphabet[(accumulator<<(5-bits))&31];}return out;}
void sourceUrl(test::OwnedEngineFixture& fixture,std::string url){auto state=c::Document::parse(test::read(fixture.stateFile).toStdString());require(bool(state),"Original state source");require(bool(state->replaceValue("announce",std::to_string(url.size())+':'+url)),"Set generated original tracker URL");test::write(fixture.stateFile,QByteArray::fromStdString(state->serialize()));}
libtorrent::entry definition(int id,bool member,const c::TorrentHash& hash){libtorrent::entry row(libtorrent::entry::dictionary_type{});row["n"]=id==1?"Auto":"Generated "+std::to_string(id);row["o"]=libtorrent::entry::list_type{};if(member){row["o"].list().emplace_back(base32(hash));}row["c"]["pp.trackers"]=libtorrent::entry::list_type{libtorrent::entry("EXAMPLE.TEST")};return row;}
struct Owner {
 test::OwnedEngineFixture& fixture;
 s::ProfileLease lease;
 std::unique_ptr<s::ManualTagStore> store;
 s::ManualTagSnapshot snapshot;
 std::unique_ptr<a::TagConstraintWorker> worker;
 std::shared_ptr<b::ui::LibraryPresentation> library=std::make_shared<b::ui::LibraryPresentation>();
 a::ConstraintWorkerFrame frame;
 explicit Owner(test::OwnedEngineFixture& f):fixture(f),lease(acquire(f)){
  auto opened=s::ManualTagStore::open(lease);require(bool(opened),"Tracker property owning Store");store=std::move(*opened);auto current=store->snapshot();require(bool(current),"Tracker property source snapshot");snapshot=*current;
  auto active=a::TagConstraintWorker::activate(lease,88);require(bool(active),"Actual normal tracker/constraint owner");worker=std::move(*active);
  library->revision=9;b::ui::LibraryItem row;row.hash=QString::fromStdString(fixture.hash->hex());row.bytes=test::OwnedEngineFixture::PayloadBytes;library->items.push_back(row);frame.session=88;frame.library=library;
 }
 static s::ProfileLease acquire(test::OwnedEngineFixture& fixture){auto lease=s::ProfileLease::acquire(fixture.native());require(bool(lease),"Tracker property actual OFD");return std::move(*lease);}
 a::ConstraintWorkerUpdate turn(){++frame.sequence;frame.sourceRevision=lease.sourceRevision();auto result=worker->step(*store,snapshot,frame);require(result&&result->evaluated<=32,"Tracker property bounded normal turn");if(result->changed){snapshot=result->snapshot;}return std::move(*result);}
 void property(int id,std::string encoded){auto source=c::Document::parse(std::string(*snapshot.rawBytes()));require(bool(source),"Actual tag property source");auto manual=c::Document::parse(c::encodeValue(*source->find("3")).value());require(bool(manual),"Actual manual type");auto record=c::Document::parse(c::encodeValue(*manual->find(std::to_string(id))).value());require(bool(record),"Actual tag record");auto config=c::Document::parse(c::encodeValue(*record->find("c")).value());require(bool(config)&&config->replaceValue("pp.trackers",std::move(encoded)),"Actual property value change");require(bool(record->replaceValue("c",config->serialize()))&&manual->replaceValue(std::to_string(id),record->serialize())&&source->replaceValue("3",manual->serialize()),"Actual property source replacement");test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(source->serialize()));auto current=store->reload();require(bool(current),"Reload actual changed property");snapshot=*current;}
 std::size_t count(int id)const{for(const auto& tag:snapshot.tags()->tags){if(tag.id==id){return tag.uniqueHashes.size();}}return 999;}
 a::ConstraintWorkerUpdate changeFor(int id){for(unsigned i=0;i<8;++i){auto result=turn();if(result.changed){require(result.tag==id,"Unrelated tag changed before the expected property effect");return result;}}throw std::runtime_error("Expected property change did not progress within eight bounded turns");}
 a::ConstraintWorkerUpdate unknownFor(int id){for(unsigned i=0;i<8;++i){auto result=turn();require(!result.changed,"Unqualified tracker source changed membership");if(result.tag==id&&result.evaluated&&result.kept){return result;}}throw std::runtime_error("Unknown tracker phase was not actually exercised");}
 void drain(){for(unsigned i=0;i<8;++i){turn();}}
 ~Owner(){worker->close();(void)store->close();}
};
bool payloadUnchanged(const test::OwnedEngineFixture& fixture){
 if(!fixture.intact(false)){return false;}
 for(const auto& file:fixture.files){struct stat current{};const auto path=fixture.payload+'/'+file.relative;
  if(::stat(path.toStdString().c_str(),&current)!=0||current.st_mtim.tv_sec!=file.before.st_mtim.tv_sec||current.st_mtim.tv_nsec!=file.before.st_mtim.tv_nsec||current.st_ctim.tv_sec!=file.before.st_ctim.tv_sec||current.st_ctim.tv_nsec!=file.before.st_ctim.tv_nsec||test::read(path)!=QByteArray(file.expected.size(),char(0))){return false;}}
 return true;
}
void startupChangesClearAndPositiveDependency(){
 test::OwnedEngineFixture fixture(false);sourceUrl(fixture,"http://a.example.test/announce");const auto stateBefore=test::read(fixture.stateFile);
 libtorrent::entry root(libtorrent::entry::dictionary_type{});root["3"]["1"]=definition(1,false,*fixture.hash);root["3"]["2"]=definition(2,true,*fixture.hash);root["3"]["2"]["c"]["pp.trackers"]=libtorrent::entry::list_type{libtorrent::entry("unrelated.invalid")};
 auto& dependent=root["3"]["4"];dependent["n"]="Consumer";dependent["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("hasTag(\"Auto\")")};root["3"]["5"]["n"]="Late consumer";root["3"]["5"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("hasTag(\"Late Auto\")")};root["opaque-root"]=std::string("x\0y",3);test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(root)));
 {Owner owner(fixture);owner.drain();require(owner.count(1)==0&&owner.count(2)==1&&owner.count(4)==0,"Startup reconciled a tracker property or invented a dependency member");
  owner.property(1,"l12:example.teste");auto add=owner.changeFor(1);require(add.changed&&add.tag==1&&owner.count(1)==1,"Actual property-change did not add a matching torrent");owner.drain();require(owner.count(4)==1,"Owned automatic tag did not supply its current positive membership to an ordinary rule");
  // The real original constraint actor retains a one-second add guard.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  auto deferred=owner.store->apply(owner.snapshot,c::SavedTagRemoveMember{4,*fixture.hash});require(deferred&&deferred->changed,"Actual ordinary membership event before equivalent property");owner.snapshot=deferred->snapshot;owner.property(1,"l12:EXAMPLE.TESTe");owner.drain();require(owner.count(4)==1,"Successful no-effect property completion lost a deferred ordinary rule event");
  owner.property(2,"l13:other.invalide");auto remove=owner.changeFor(2);require(remove.changed&&remove.tag==2&&owner.count(2)==0,"Actual changed nonmatching property did not remove current membership first");owner.drain();
  owner.property(1,"le");remove=owner.changeFor(1);require(remove.changed&&remove.tag==1&&owner.count(1)==0,"Actual clear-property event retained matching previous membership");owner.drain();
  owner.property(1,"l12:EXAMPLE.TESTe");std::stop_source cancel;cancel.request_stop();++owner.frame.sequence;owner.frame.sourceRevision=owner.lease.sourceRevision();const auto before=test::read(fixture.profile+"/tag.config");auto denied=owner.worker->step(*owner.store,owner.snapshot,owner.frame,cancel.get_token());require(!denied&&denied.error().code==s::ErrorCode::Cancelled&&test::read(fixture.profile+"/tag.config")==before,"Cancelled tracker source observation changed membership");
  bool staleRefused=false;for(unsigned i=0;i<3;++i){++owner.frame.sequence;owner.frame.sourceRevision=owner.lease.sourceRevision()+1;denied=owner.worker->step(*owner.store,owner.snapshot,owner.frame);require((!denied||!denied->changed)&&test::read(fixture.profile+"/tag.config")==before,"Stale tracker source revision published membership");if(!denied){require(denied.error().code==s::ErrorCode::Conflict,"Unexpected stale source failure");staleRefused=true;break;}}require(staleRefused,"Stale tracker property phase was not actually exercised");owner.drain();require(owner.count(1)==1,"Current property work did not progress after a refused stale/cancelled turn");
  sourceUrl(fixture,"ftp://a.example.test/announce");owner.property(1,"l13:other.invalide");auto unknown=owner.unknownFor(1);require(!unknown.changed&&unknown.kept&&owner.count(1)==1,"Unqualified tracker URL became a destructive negative");
  auto renamed=owner.store->apply(owner.snapshot,c::SavedTagRename{1,"Late Auto"});require(renamed&&renamed->changed,"Actual source name change for uncertain ownership");owner.snapshot=renamed->snapshot;
  auto wake=owner.store->apply(owner.snapshot,c::SavedTagRemoveMember{4,*fixture.hash});require(wake&&wake->changed,"Actual membership callback wakeup for uncertain source");owner.snapshot=wake->snapshot;
  auto checked=owner.turn();require(checked.evaluated>0&&!checked.changed,"Uncertain source was not actually evaluated or produced a changed member");owner.drain();require(owner.count(5)==0,"Uncertain property reconciliation supplied a positive source witness");
  require(owner.snapshot.tags()->source->find("opaque-root")->bytes==std::string("x\0y",3),"Tracker property update changed opaque data");
 }
 // Only the explicit test URL edit above may change the source; automatic
 // tagging never writes a torrent state or opens its payload for hashing.
 auto after=c::Document::parse(test::read(fixture.stateFile).toStdString()),before=c::Document::parse(stateBefore.toStdString());require(after&&before&&c::encodeValue(*after->find("info")).value()==c::encodeValue(*before->find("info")).value()&&c::encodeValue(*after->find("attributes")).value()==c::encodeValue(*before->find("attributes")).value(),"Tracker tagging rewrote original torrent data");require(payloadUnchanged(fixture),"Tracker tagging touched payload bytes or timestamps");
}
void disabledStartupCannotActivateTrackerPropertyWork(){
 test::OwnedEngineFixture fixture(false);sourceUrl(fixture,"http://a.example.test/announce");libtorrent::entry root(libtorrent::entry::dictionary_type{});root["3"]["1"]=definition(1,false,*fixture.hash);test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(root)));
 libtorrent::entry config(libtorrent::entry::dictionary_type{});config["tagmanager.enable"]=0;test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
 {Owner owner(fixture);require(!owner.turn().changed,"Disabled startup activated tracker ownership");owner.property(1,"l12:example.teste");const auto before=test::read(fixture.profile+"/tag.config");config["tagmanager.enable"]=1;test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));owner.drain();require(owner.count(1)==0&&test::read(fixture.profile+"/tag.config")==before,"Startup mode change silently enabled tracker effects");}
 {Owner reopened(fixture);reopened.drain();require(reopened.count(1)==0,"Enabled restart reconciled saved tracker properties at startup");reopened.property(1,"l12:EXAMPLE.TESTe");reopened.drain();require(reopened.count(1)==1,"Fresh enabled owner did not process its actual later property event");}
 require(payloadUnchanged(fixture),"Disabled/reopened tracker owner changed payload");
}
void manyPropertiesMakeOneCommitPerBoundedTurn(){
 test::OwnedEngineFixture fixture(false);sourceUrl(fixture,"udp://a.example.test/announce");libtorrent::entry root(libtorrent::entry::dictionary_type{});for(int id=1;id<=40;++id){root["3"][std::to_string(id)]=definition(id,false,*fixture.hash);}root["3"]["99"]["n"]="Fair consumer";root["3"]["99"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("hasTag(\"Auto\")")};test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(root)));
 {Owner owner(fixture);require(!owner.turn().changed,"Bulk tracker properties reconciled at startup");for(int id=1;id<=40;++id){owner.property(id,"l15:example.test:80e");}
  unsigned writes=0;bool early=false;for(unsigned i=0;i<96;++i){auto result=owner.turn();writes+=result.changed?1U:0U;if(result.changed){require(result.hash==fixture.hash,"Tracker work lost its actual saved hash");if(result.tag==99){early=i<10&&owner.count(40)==0;}}}
  require(writes==41&&early&&owner.count(99)==1,"Tracker work lost progress or starved ordinary constraints");for(int id=1;id<=40;++id){require(owner.count(id)==1,"Bounded property work missed a matching original UDP default port");}
 }require(payloadUnchanged(fixture),"Bulk tracker property changes touched payload");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{startupChangesClearAndPositiveDependency();disabledStartupCannotActivateTrackerPropertyWork();manyPropertiesMakeOneCommitPerBoundedTurn();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS actual source-bound tracker property owner: startup, changes, clear, dependency, cancellation, stale source, unknown URL, bounded40 and payload preservation\n";return 0;}
