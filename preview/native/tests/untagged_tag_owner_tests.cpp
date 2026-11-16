// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_constraint_worker.hpp"
#include "owned_engine_fixture.hpp"
#include <QCoreApplication>
#include <iostream>
#include <cstddef>
namespace b=biglybtc;namespace a=b::app;namespace c=b::core;namespace s=b::storage;namespace test=b::test;
namespace {
using test::require;
std::string base32(const c::TorrentHash& hash){constexpr auto alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";unsigned value=0,bits=0;std::string out;for(auto byte:hash.bytes()){value=(value<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;out+=alphabet[(value>>bits)&31];}}if(bits){out+=alphabet[(value<<(5-bits))&31];}return out;}
libtorrent::entry tag(int id,bool untagged,bool member,const c::TorrentHash& hash){libtorrent::entry row(libtorrent::entry::dictionary_type{});row["n"]="U"+std::to_string(id);row["o"]=libtorrent::entry::list_type{};if(member){row["o"].list().emplace_back(base32(hash));}if(untagged){row["c"]["pp.untagged"]=1;}return row;}
struct Owner {
 test::OwnedEngineFixture& fixture;s::ProfileLease lease;std::unique_ptr<s::ManualTagStore> store;s::ManualTagSnapshot snapshot;std::unique_ptr<a::TagConstraintWorker> worker;
 std::shared_ptr<b::ui::LibraryPresentation> library=std::make_shared<b::ui::LibraryPresentation>();a::ConstraintWorkerFrame frame;bool positiveConsumer=false;
 static s::ProfileLease acquire(test::OwnedEngineFixture& fixture){auto result=s::ProfileLease::acquire(fixture.native());require(bool(result),"Actual Untagged OFD");return std::move(*result);}
 explicit Owner(test::OwnedEngineFixture& f):fixture(f),lease(acquire(f)){auto opened=s::ManualTagStore::open(lease);require(bool(opened),"Actual Untagged Store");store=std::move(*opened);auto source=store->snapshot();require(bool(source),"Actual Untagged snapshot");snapshot=*source;auto owner=a::TagConstraintWorker::activate(lease,88);require(bool(owner),"Actual Untagged worker");worker=std::move(*owner);
  library->revision=9;b::ui::LibraryItem row;row.hash=QString::fromStdString(fixture.hash->hex());row.bytes=test::OwnedEngineFixture::PayloadBytes;library->items.push_back(row);frame.session=88;frame.library=library;
 }
 a::ConstraintWorkerUpdate turn(){++frame.sequence;frame.sourceRevision=lease.sourceRevision();auto result=worker->step(*store,snapshot,frame);require(result&&result->evaluated<=32,"Bounded current Untagged turn");if(result->changed){snapshot=result->snapshot;}if(result->tag==7&&result->evaluated&&!result->kept){positiveConsumer=true;}return std::move(*result);}
 void drain(){for(unsigned i=0;i<16;++i){turn();}}
 std::size_t count(int id)const{for(const auto& row:snapshot.tags()->tags){if(row.id==id){return row.uniqueHashes.size();}}return 999;}
 void member(int id,bool add){const c::SavedTagEdit edit=add?c::SavedTagEdit{c::SavedTagAddMember{id,*fixture.hash,100}}:c::SavedTagEdit{c::SavedTagRemoveMember{id,*fixture.hash}};auto saved=store->apply(snapshot,edit);require(saved&&saved->changed,"Actual ordinary membership source change");snapshot=saved->snapshot;}
 void flag(int id,bool enabled){auto doc=c::Document::parse(std::string(*snapshot.rawBytes()));require(bool(doc),"Untagged property source");auto type=c::Document::parse(c::encodeValue(*doc->find("3")).value());auto record=c::Document::parse(c::encodeValue(*type->find(std::to_string(id))).value());const auto* old=record->find("c");auto config=c::Document::parse(old?c::encodeValue(*old).value():"de");require(config&&config->replaceValue("pp.untagged",enabled?"i1e":"i0e")&&record->replaceValue("c",config->serialize())&&type->replaceValue(std::to_string(id),record->serialize())&&doc->replaceValue("3",type->serialize()),"Generated actual boolean change");test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(doc->serialize()));auto fresh=store->reload();require(bool(fresh),"Reload actual Untagged property");snapshot=*fresh;}
 ~Owner(){worker->close();(void)store->close();}
};
void write(test::OwnedEngineFixture& fixture,const libtorrent::entry& value){test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(value)));}
void payload(const test::OwnedEngineFixture& fixture){for(const auto& file:fixture.files){require(test::read(fixture.payload+'/'+file.relative)==QByteArray(file.expected.size(),char(0)),"Untagged work touched payload");}}
void startupCurrentCountsBatchAndPositiveSource(){
 test::OwnedEngineFixture f(false);const auto before=test::read(f.stateFile);libtorrent::entry defs(libtorrent::entry::dictionary_type{});defs["3"]["1"]=tag(1,false,false,*f.hash);defs["3"]["2"]=tag(2,true,false,*f.hash);defs["3"]["3"]=tag(3,true,false,*f.hash);
 defs["3"]["7"]["n"]="Remove-only consumer";defs["3"]["7"]["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry("hasTag(\"U2\")"),libtorrent::entry("am=2;")};defs["opaque-root"]=std::string("x\0y",3);write(f,defs);
 {Owner o(f);const auto generation=o.snapshot.generation();auto start=o.turn();require(start.changed&&o.count(2)==1&&o.count(3)==1&&o.snapshot.generation()==generation+1,"Startup did not publish both Untagged memberships in one actual CAS");o.drain();require(o.positiveConsumer&&o.count(7)==0,"Completed Untagged source remained unknown or manufactured an ordinary member");
  o.member(1,true);o.drain();require(o.count(2)==0&&o.count(3)==0,"Known ordinary current membership did not withdraw Untagged");o.member(1,false);o.drain();require(o.count(2)==1&&o.count(3)==1,"Last ordinary removal did not restore Untagged");
  auto noop=o.turn();require(!noop.changed&&o.snapshot.tags()->source->find("opaque-root")->bytes==std::string("x\0y",3),"Stable Untagged source rewrote membership or opaque bytes");
 }require(test::read(f.stateFile)==before,"Untagged batch rewrote original state/resume");payload(f);
}
void repeatedEnableDoesNotDuplicateCounts(){
 test::OwnedEngineFixture f(false);libtorrent::entry defs(libtorrent::entry::dictionary_type{});for(int id:{1,4}){defs["3"][std::to_string(id)]=tag(id,false,true,*f.hash);}for(int id:{2,3}){defs["3"][std::to_string(id)]=tag(id,true,true,*f.hash);}write(f,defs);
 {Owner o(f);o.drain();require(o.count(2)==0&&o.count(3)==0,"Current ordinary members did not exclude saved Untagged");o.flag(2,false);o.flag(3,false);o.drain();o.flag(2,true);o.drain();o.member(1,false);o.drain();require(o.count(4)==1&&o.count(2)==0,"Native repeated original listener counter bug despite B remaining tagged");o.member(4,false);o.drain();require(o.count(2)==1&&o.count(3)==0,"Re-enabled rule or disabled-rule retirement lost current membership");
  o.member(3,true);o.drain();require(o.count(3)==1&&o.count(2)==0,"Retired disabled-rule event kept clearing a now-ordinary manual tag");
 }payload(f);
}
void incompleteSourcesRetainButPositiveWitnessStillRemoves(){
 test::OwnedEngineFixture f(false);libtorrent::entry defs(libtorrent::entry::dictionary_type{});auto foreign=c::TorrentHash::parse(std::string(20,'Q'));require(bool(foreign),"Foreign declared hash");defs["3"]["1"]=tag(1,false,true,*foreign);for(int id:{2,3}){defs["3"][std::to_string(id)]=tag(id,true,true,*f.hash);}defs["3"]["4"]=tag(4,false,false,*f.hash);defs["3"]["4"]["c"]["rl.up"]=1024;write(f,defs);
 {Owner o(f);o.drain();require(o.count(2)==1&&o.count(3)==1,"Unknown orphan/unsupported source removed existing Untagged members");o.member(2,false);o.member(3,false);o.drain();require(o.count(2)==0&&o.count(3)==0,"Incomplete census manufactured absence and added Untagged");o.member(2,true);o.member(3,true);
  auto created=o.store->apply(o.snapshot,c::SavedTagCreate{"Known positive",{}});require(created&&created->changed&&created->createdId,"Actual known-witness tag creation");o.snapshot=created->snapshot;o.member(*created->createdId,true);o.drain();require(o.count(2)==0&&o.count(3)==0,"Unknown sibling hid a real known positive manual witness");
 }payload(f);
}
void manyTargetsUseBoundedMultiSubjectCas(){
 test::OwnedEngineFixture f(false);const auto initial=test::read(f.stateFile);auto state=c::Document::parse(initial.toStdString());require(bool(state),"Generated original multi-subject state");libtorrent::entry inventory(libtorrent::entry::dictionary_type{});inventory["downloads"]=libtorrent::entry::list_type{};std::vector<c::TorrentHash> hashes{*f.hash};std::vector<std::pair<QString,QByteArray>> original{{f.stateFile,initial}};
 for(unsigned i=0;i<35;++i){QString file=f.stateFile;QByteArray rawHash=QByteArray::fromHex(QByteArray::fromStdString(f.hash->hex()));
  if(i){auto copy=c::Document::parse(initial.toStdString());auto info=c::Document::parse(c::encodeValue(*copy->find("info")).value());const auto name="generated-"+std::to_string(i)+".bin";require(info&&bool(info->replaceValue("name",std::to_string(name.size())+':'+name)),"Generated unique info name");rawHash=QCryptographicHash::hash(QByteArray::fromStdString(info->serialize()),QCryptographicHash::Sha1);auto hash=c::TorrentHash::parse(rawHash.toStdString());require(bool(hash),"Generated current inventory hash");hashes.push_back(*hash);require(bool(copy->replaceValue("info",info->serialize())),"Generated independent metadata");file=f.profile+"/active/"+QString::fromStdString(hash->hex())+".dat";const auto bytes=QByteArray::fromStdString(copy->serialize());test::write(file,bytes);original.emplace_back(file,bytes);}
  libtorrent::entry row(libtorrent::entry::dictionary_type{});row["torrent_hash"]=rawHash.toStdString();row["torrent"]=file.toStdString();row["persistent"]=1;row["state"]=75;row["position"]=i+1;row["save_dir"]=f.payload.toStdString();row["save_file"]="sample.bin";inventory["downloads"].list().push_back(std::move(row));
 }
 write(f,[]{libtorrent::entry defs(libtorrent::entry::dictionary_type{});defs["3"]["2"]["n"]="U2";defs["3"]["2"]["c"]["pp.untagged"]=1;return defs;}());test::write(f.profile+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));const auto inventoryBefore=test::read(f.profile+"/downloads.config");
 {Owner o(f);for(std::size_t i=1;i<hashes.size();++i){b::ui::LibraryItem row;row.hash=QString::fromStdString(hashes[i].hex());o.library->items.push_back(row);}const auto generation=o.snapshot.generation();auto first=o.turn();require(first.changed&&first.evaluated==32&&o.count(2)==32&&o.snapshot.generation()==generation+1,"First Untagged turn did not save32 subjects with one CAS");auto second=o.turn();require(second.changed&&o.count(2)==35&&o.snapshot.generation()==generation+2,"Deferred Untagged subjects did not complete in the next batch");o.drain();require(o.count(2)==35,"Stable multi-subject membership was lost");}
 for(const auto& [path,bytes]:original){require(test::read(path)==bytes,"Multi-subject batch rewrote original metadata");}require(test::read(f.profile+"/downloads.config")==inventoryBefore,"Multi-subject batch rewrote inventory");payload(f);
}
void oversizedEffectsAndMalformedNamesCannotInferAbsence(){
 {test::OwnedEngineFixture f(false);libtorrent::entry defs(libtorrent::entry::dictionary_type{});for(int id=1;id<=33;++id){defs["3"][std::to_string(id)]=tag(id,true,false,*f.hash);}write(f,defs);const auto before=test::read(f.profile+"/tag.config");Owner o(f);o.drain();require(test::read(f.profile+"/tag.config")==before,"Oversized Untagged effects published a prefix");payload(f);}
 {test::OwnedEngineFixture f(false);libtorrent::entry defs(libtorrent::entry::dictionary_type{});defs["3"]["1"]["o"]=libtorrent::entry::list_type{};defs["3"]["2"]=tag(2,true,false,*f.hash);write(f,defs);Owner o(f);o.drain();require(o.count(2)==0,"Nameless unresolved manual source became known absence");payload(f);}
 {test::OwnedEngineFixture f(false);libtorrent::entry defs(libtorrent::entry::dictionary_type{});defs["3"]["1"]=tag(1,false,true,*f.hash);defs["3"]["1"]["n"]=std::string(1,char(0xff));defs["3"]["2"]=tag(2,true,true,*f.hash);write(f,defs);Owner o(f);o.drain();require(o.count(2)==1,"Invalid UTF8 unresolved manual source became known positive");payload(f);}
}
void cancelledStaleAndClosedTurnsCannotSave(){
 test::OwnedEngineFixture f(false);libtorrent::entry defs(libtorrent::entry::dictionary_type{});defs["3"]["2"]=tag(2,true,false,*f.hash);write(f,defs);
 {Owner o(f);const auto before=test::read(f.profile+"/tag.config");std::stop_source cancelled;cancelled.request_stop();o.frame.sequence=1;o.frame.sourceRevision=o.lease.sourceRevision();auto result=o.worker->step(*o.store,o.snapshot,o.frame,cancelled.get_token());require(!result&&result.error().code==s::ErrorCode::Cancelled,"Cancelled Untagged turn accepted");o.frame.sequence=2;o.frame.sourceRevision=o.lease.sourceRevision()+1;result=o.worker->step(*o.store,o.snapshot,o.frame);require(!result&&result.error().code==s::ErrorCode::Conflict&&test::read(f.profile+"/tag.config")==before,"Stale Untagged source changed profile");o.drain();require(o.count(2)==1,"Refused turn lost current Untagged work");o.worker->close();++o.frame.sequence;result=o.worker->step(*o.store,o.snapshot,o.frame);require(!result,"Closed Untagged owner accepted more work");
 }payload(f);
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{startupCurrentCountsBatchAndPositiveSource();repeatedEnableDoesNotDuplicateCounts();incompleteSourcesRetainButPositiveWitnessStillRemoves();manyTargetsUseBoundedMultiSubjectCas();oversizedEffectsAndMalformedNamesCannotInferAbsence();cancelledStaleAndClosedTurnsCannotSave();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS actual Untagged owner: current census,batch CAS,re-enable correction,unknown and positive sources,cancel/stale/close,raw state and payload preservation\n";return 0;}
