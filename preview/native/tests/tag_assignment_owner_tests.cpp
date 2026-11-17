// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_assignment_owner.hpp"
#include "manual_tag_identity.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace s=biglybtc::storage;namespace b=biglybtc::core;namespace a=b::tags::assignment;
namespace {
void need(bool ok,const char* error){if(!ok){throw std::runtime_error(error);}}
template<class T>T take(std::expected<T,s::Error> result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
std::string bytes(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string dict(std::map<std::string,std::string> values){std::string out="d";for(const auto& [key,value]:values){out+=bytes(key)+value;}return out+'e';}
std::string raw(const b::TorrentHash& hash){return {reinterpret_cast<const char*>(hash.bytes().data()),hash.bytes().size()};}
std::string base32(const b::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,buffer=0;for(auto value:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(value);bits+=8;while(bits>=5){bits-=5;out+=chars[(buffer>>bits)&31];}}if(bits){out+=chars[(buffer<<(5-bits))&31];}return out;}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);need(bool(file),"Generated action source read");return {std::istreambuf_iterator<char>(file),{}};}
struct Root{
 std::filesystem::path path;
 Root(){std::string pattern="/tmp/biglybtc-tag-assignment-XXXXXX";auto name=::mkdtemp(pattern.data());need(name,"Generated private action directory");path=name;need(::chmod(path.c_str(),0700)==0,"Generated action mode");}
 ~Root(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Area{
 Root root;std::optional<s::ProfileLease> lease;std::optional<s::TagManagerStartupMode> mode;
 std::unique_ptr<s::ManualTagStore> store;std::unique_ptr<s::TagAssignmentOwner> owner;
 b::TorrentHash hash;std::string metainfo;std::filesystem::path state;
 Area(std::map<std::string,std::string> config,bool member=false,bool enabled=true,std::optional<std::string> flags={} ){
  auto golden=b::Document::parse(read(TAG_OWNER_HASH_FIXTURE));need(bool(golden),"Actual original hash fixture");const auto& row=golden->find("rows")->children.front();
  hash=*b::TorrentHash::parse(row.find("selected")->bytes);metainfo=row.find("metainfo")->bytes;
  if(flags){auto doc=b::Document::parse(metainfo);need(doc&&doc->replaceValue("attributes",dict({{"flags",*flags}})),"Generated metadata flags");metainfo=doc->serialize();}
  std::filesystem::create_directories(root.path/"active");state=std::filesystem::path("active")/(hash.hex()+".dat");
  write(state,metainfo);write("tag.config",tags(config,member));
  write("biglybt.config",dict({{"locale",bytes("en_GB")},{"tagmanager.enable",enabled?"i1e":"i0e"}}));
  write("downloads.config",dict({{"downloads","l"+dict({{"persistent","i1e"},{"position","i1e"},{"save_dir",bytes((root.path/"absent-payload").string())},{"state","i75e"},{"torrent",bytes((root.path/state).string())},{"torrent_hash",bytes(raw(hash))}})+"e"}}));
  lease.emplace(take(s::ProfileLease::acquire(root.path)));mode.emplace(take(s::TagManagerStartupMode::capture(*lease)));
  store=take(s::ManualTagStore::open(*lease));if(enabled){owner=take(s::TagAssignmentOwner::activate(*lease,77,*mode));}
 }
 void write(const std::filesystem::path& name,std::string_view value){std::ofstream file(root.path/name,std::ios::binary|std::ios::trunc);need(bool(file),"Generated action source open");file.write(value.data(),std::streamsize(value.size()));file.close();need(bool(file),"Generated action write");}
 std::string tags(const std::map<std::string,std::string>& config,bool member){return dict({{"3",dict({{"1",dict({{"c",dict(config)},{"n",bytes("Generated action tag")},{"o",member?"l"+bytes(base32(hash))+"e":"le"}})}})}});}
 s::ManualTagSnapshot snapshot(){return take(store->snapshot());}
 s::TagAssignmentObservation observed(int stateValue=70)const{return {hash,77,lease->sourceRevision(),stateValue};}
 void preserved(std::string_view inventory,std::string_view original)const{
  need(read(root.path/"downloads.config")==inventory&&read(root.path/state)==original&&!std::filesystem::exists(root.path/"absent-payload"),"Assignment changed inventory/state or accessed payload");
 }
};
bool member(const s::ManualTagSnapshot& source){return !source.tags()->tags.front().uniqueHashes.empty();}
void actualCommittedReceiptAndOnceOnlyCommands(){
 Area area(std::map<std::string,std::string>{{"pp.2","i1e"},{"pp.8","i1e"}});auto before=area.snapshot();const auto inventory=read(area.root.path/"downloads.config"),state=read(area.root.path/area.state);
 need(!area.store->apply(before,b::SavedTagAddMember{1,area.hash,100}),"Generic writer acquired lifecycle action permission");
 auto changed=take(area.owner->change(*area.store,before,1,area.observed(),true,100));
 need(changed.commit.changed&&member(changed.commit.snapshot)&&changed.receipt&&!changed.receipt->done(),"Actual fresh commit lost its dispatch receipt");
 auto first=take(area.owner->next(*area.store,changed.commit.snapshot,*changed.receipt));need(first.hash==area.hash&&first.commands.count==1&&first.commands.values[0]==a::Effect::Queue,"Actual assignment queue command differs");
 auto second=take(area.owner->next(*area.store,changed.commit.snapshot,*changed.receipt));need(second.commands.count==1&&second.commands.values[0]==a::Effect::ForceStart,"Actual queued force action differs");
 need(changed.receipt->done()&&!area.owner->next(*area.store,changed.commit.snapshot,*changed.receipt),"Completed receipt replayed commands");area.preserved(inventory,state);
}
void noOpRemovalAndMetadataHaveNoDispatch(){
 Area existing(std::map<std::string,std::string>{{"pp.8","i1e"}},true);auto unchanged=take(existing.owner->change(*existing.store,existing.snapshot(),1,existing.observed(),true,100));need(!unchanged.commit.changed&&!unchanged.receipt,"No-op assignment replayed action");
 auto removed=take(existing.owner->change(*existing.store,unchanged.commit.snapshot,1,existing.observed(),false,100));need(removed.commit.changed&&!removed.receipt&&!member(removed.commit.snapshot),"Removal dispatched assignment action");
 Area metadata(std::map<std::string,std::string>{{"pp.2","i1e"},{"pp.8","i1e"}},false,true,"i512e");auto changed=take(metadata.owner->change(*metadata.store,metadata.snapshot(),1,metadata.observed(),true,100));need(changed.commit.changed&&!changed.receipt,"Metadata download received lifecycle action");
 Area running(std::map<std::string,std::string>{{"pp.2","i1e"}});auto noTask=take(running.owner->change(*running.store,running.snapshot(),1,running.observed(60),true,100));need(noTask.commit.changed&&!noTask.receipt,"Running download was queued by start assignment");
}
void strictCapabilitiesAndCancelledPublication(){
 for(const auto* key:{"rl.up","rl.down"}){for(const auto* encoded:{"i0e","i4294967296e","i-9223372036854775808e"}){
   Area unlimited(std::map<std::string,std::string>{{"pp.8","i1e"},{key,encoded}});auto saved=take(unlimited.owner->change(*unlimited.store,unlimited.snapshot(),1,unlimited.observed(),true,100));need(saved.commit.changed&&saved.receipt,"Known unlimited rate metadata blocked actual lifecycle owner");
 }}

 for(const auto& config:{std::map<std::string,std::string>{{"pp.2","i1e"},{"pp.32","i1e"}},{{"pp.2","i1e"},{"future-effect","i0e"}},{{"pp.2","i1e"},{"rl.up","i1024e"}}}){
  Area area(config);const auto rawTags=read(area.root.path/"tag.config");need(!area.owner->change(*area.store,area.snapshot(),1,area.observed(),true,100)&&read(area.root.path/"tag.config")==rawTags,"Unsupported assignment effects were dropped during publication");
 }
 Area disabled(std::map<std::string,std::string>{{"pp.2","i1e"}},false,false);need(!s::TagAssignmentOwner::activate(*disabled.lease,77,*disabled.mode),"Disabled startup mode activated actor");
 Area badMetadata(std::map<std::string,std::string>{{"pp.2","i1e"}},false,true,bytes("future"));need(!badMetadata.owner->change(*badMetadata.store,badMetadata.snapshot(),1,badMetadata.observed(),true,100),"Unknown metadata flag became an action grant");
 Area area(std::map<std::string,std::string>{{"pp.2","i1e"}});auto before=area.snapshot();const auto bytesBefore=read(area.root.path/"tag.config");std::stop_source stopped;stopped.request_stop();need(!area.owner->change(*area.store,before,1,area.observed(),true,100,stopped.get_token())&&read(area.root.path/"tag.config")==bytesBefore,"Cancelled assignment wrote a prefix");
 std::array<s::ManualTagMembershipEdit,1> batch{b::SavedTagAddMember{1,area.hash,100}};need(!area.store->applyMembershipBatch(before,batch),"Generic batch acquired lifecycle actor support");
}
void currentScopeAndForeignReceipts(){
 Area area(std::map<std::string,std::string>{{"pp.2","i1e"}}),foreign(std::map<std::string,std::string>{{"pp.2","i1e"}});auto before=area.snapshot();auto wrong=area.observed();++wrong.session;need(!area.owner->change(*area.store,before,1,wrong,true,100),"Foreign session assigned member");wrong=area.observed();++wrong.sourceRevision;need(!area.owner->change(*area.store,before,1,wrong,true,100),"Stale source revision assigned member");
 need(!area.owner->change(*foreign.store,foreign.snapshot(),1,foreign.observed(),true,100),"Foreign profile assigned member");
 auto thread=std::async(std::launch::async,[&]{return !area.owner->change(*area.store,before,1,area.observed(),true,100);});need(thread.get(),"Foreign worker assigned member");
 auto renamed=take(area.store->apply(before,b::SavedTagRename{1,"Generated renamed"}));need(!area.owner->change(*area.store,before,1,area.observed(),true,100),"Stale snapshot assigned member");
 auto changed=take(area.owner->change(*area.store,renamed.snapshot,1,area.observed(),true,100));need(changed.receipt.has_value(),"Current actual assignment produced no receipt");
 need(!foreign.owner->next(*foreign.store,foreign.snapshot(),*changed.receipt)&&!changed.receipt->done(),"Foreign actor consumed another receipt");
 auto foreignThread=std::async(std::launch::async,[&]{return !area.owner->next(*area.store,changed.commit.snapshot,*changed.receipt);});need(foreignThread.get()&&!changed.receipt->done(),"Foreign worker consumed a receipt");
 auto secondStore=take(s::ManualTagStore::open(*area.lease));
 need(!area.owner->next(*secondStore,take(secondStore->snapshot()),*changed.receipt)&&!changed.receipt->done(),"Another store in the same profile consumed the receipt");
 need(bool(secondStore->close()),"Second generated store close");
 need(bool(area.owner->next(*area.store,changed.commit.snapshot,*changed.receipt)),"Correct owner lost receipt after foreign attempts");
}
void executionUsesCurrentFlagsAndCapturedForce(){
 Area area(std::map<std::string,std::string>{{"pp.2","i1e"},{"pp.8","i1e"}});auto changed=take(area.owner->change(*area.store,area.snapshot(),1,area.observed(),true,100));need(changed.receipt.has_value(),"Dynamic source receipt missing");
 area.write("tag.config",area.tags({{"pp.2","i0e"},{"pp.8","i0e"}},true));auto current=take(area.store->reload());
 auto first=take(area.owner->next(*area.store,current,*changed.receipt));need(first.commands.values[0]==a::Effect::Resume,"Scheduled Start did not re-read current flag");
 auto force=take(area.owner->next(*area.store,current,*changed.receipt));need(force.commands.values[0]==a::Effect::ForceStart,"Scheduled force boolean changed after enqueue");
 Area stopped(std::map<std::string,std::string>{{"pp.4","i1e"}});auto stop=take(stopped.owner->change(*stopped.store,stopped.snapshot(),1,stopped.observed(60),true,100));need(stop.receipt.has_value(),"Stop assignment receipt missing");
 stopped.write("tag.config",stopped.tags({{"pp.4","i0e"},{"pp.32768","i1e"}},true));auto currentStop=take(stopped.store->reload());auto queue=take(stopped.owner->next(*stopped.store,currentStop,*stop.receipt));need(queue.commands.count==2&&queue.commands.values[0]==a::Effect::QueueAfterStop&&queue.commands.values[1]==a::Effect::CheckMaximum,"Scheduled Stop did not preserve original runtime Queue precedence");
}
void recoveryPendingAndFailureNeverPublish(){
 for(unsigned scenario=0;scenario<3;++scenario){Area area(std::map<std::string,std::string>{{"pp.2","i1e"}});auto before=area.snapshot();const auto rawTags=read(area.root.path/"tag.config");
  if(scenario==0){area.write("downloads.config.saving",read(area.root.path/"downloads.config"));}
  if(scenario==1){std::filesystem::rename(area.root.path/area.state,area.root.path/(area.state.string()+".bak"));}
  if(scenario==2){area.write("tag.config.saving",rawTags);}
  need(!area.owner->change(*area.store,before,1,area.observed(),true,100)&&read(area.root.path/"tag.config")==rawTags,"Recovered/pending source published assignment");
 }
 Area area(std::map<std::string,std::string>{{"pp.2","i1e"}});auto changed=take(area.owner->change(*area.store,area.snapshot(),1,area.observed(),true,100));need(changed.receipt.has_value(),"Failure control receipt missing");area.write("downloads.config.saving",read(area.root.path/"downloads.config"));need(!area.owner->next(*area.store,changed.commit.snapshot,*changed.receipt)&&changed.receipt->done(),"Failed dispatch source could be retried automatically");
}
void closeDoesNotLeaveReceiptsHoldingTheProfile(){
 {Area closed(std::map<std::string,std::string>{{"pp.2","i1e"}});auto result=take(closed.owner->change(*closed.store,closed.snapshot(),1,closed.observed(),true,100));need(result.receipt.has_value(),"Store-close receipt missing");need(bool(closed.store->close())&&result.receipt->done(),"Closed store left receipt live");auto fresh=take(s::ManualTagStore::open(*closed.lease));need(!closed.owner->next(*fresh,take(fresh->snapshot()),*result.receipt),"Reopened same-profile store revived receipt");}

 Area area(std::map<std::string,std::string>{{"pp.2","i1e"}});auto changed=take(area.owner->change(*area.store,area.snapshot(),1,area.observed(),true,100));need(changed.receipt.has_value(),"Lifetime receipt missing");area.owner->close();need(changed.receipt->done(),"Closed actor left receipt live");need(bool(area.store->close()),"Actual source store close");area.mode.reset();area.lease.reset();auto reopened=s::ProfileLease::acquire(area.root.path);need(bool(reopened),"Receipt retained an extra profile lease");
 auto mode=take(s::TagManagerStartupMode::capture(*reopened));auto store=take(s::ManualTagStore::open(*reopened));auto owner=take(s::TagAssignmentOwner::activate(*reopened,77,mode));need(!owner->next(*store,take(store->snapshot()),*changed.receipt),"Reopened owner revived old receipt");
}
void boundedPendingReceiptsNeverSaveAnUnscheduledPrefix(){
 Area area(std::map<std::string,std::string>{{"pp.8","i1e"}});auto source=area.snapshot();std::vector<s::TagAssignmentReceipt> pending;pending.reserve(32);
 for(unsigned count=0;count<32;++count){auto added=take(area.owner->change(*area.store,source,1,area.observed(),true,100));need(added.receipt.has_value(),"Bounded receipt lost");pending.push_back(std::move(*added.receipt));auto removed=take(area.owner->change(*area.store,added.commit.snapshot,1,area.observed(),false,100));source=removed.commit.snapshot;}
 const auto before=read(area.root.path/"tag.config");need(!area.owner->change(*area.store,source,1,area.observed(),true,100)&&read(area.root.path/"tag.config")==before&&!member(source),"Queue-full actor saved an unscheduled assignment");
 auto resolved=take(area.owner->next(*area.store,source,pending.front()));need(resolved.commands.values[0]==a::Effect::ForceStart,"Pending receipt resolution differs");auto fresh=take(area.owner->change(*area.store,source,1,area.observed(),true,100));need(fresh.commit.changed&&fresh.receipt,"Consumed receipt did not release bounded capacity");
}
}
namespace {
void atomicSameSubjectBatchesAndNoPrefix(){
 Area area(std::map<std::string,std::string>{{"pp.2","i1e"},{"pp.8","i1e"}});
 std::map<std::string,std::string> records;std::vector<s::ManualTagMembershipEdit> edits;
 for(int id=1;id<=32;++id){records[std::to_string(id)]=dict({{"n",bytes("Generated batch tag "+std::to_string(id))},{"c",dict({{"pp.2","i1e"},{"pp.8","i1e"}})},{"o","le"}});edits.emplace_back(b::SavedTagAddMember{id,area.hash,100});}
 area.write("tag.config",dict({{"3",dict(records)}}));auto before=take(area.store->reload());const auto original=read(area.root.path/"tag.config");
 auto duplicate=edits;duplicate[1]=duplicate[0];need(!area.owner->changeBatch(*area.store,before,duplicate,area.observed()),"Duplicate tag sequence saved a batch prefix");
 auto foreign=edits;auto other=*b::TorrentHash::parse(std::string(20,'q'));foreign[1]=b::SavedTagAddMember{2,other,100};need(!area.owner->changeBatch(*area.store,before,foreign,area.observed()),"Foreign subject saved a batch prefix");
 auto oversized=edits;oversized.push_back(b::SavedTagAddMember{33,area.hash,100});need(!area.owner->changeBatch(*area.store,before,oversized,area.observed()),"Oversized batch saved a prefix");
 std::stop_source cancel;cancel.request_stop();need(!area.owner->changeBatch(*area.store,before,edits,area.observed(),cancel.get_token()),"Cancelled batch saved a prefix");
 need(read(area.root.path/"tag.config")==original&&!std::filesystem::exists(area.root.path/"tag.config.bak"),"Refused batch changed physical source");
 auto changed=take(area.owner->changeBatch(*area.store,before,edits,area.observed()));need(changed.commit.changed&&changed.receipt&&changed.commit.snapshot.generation()==before.generation()+1,"32 assignments did not use exactly one source generation");
 need(read(area.root.path/"tag.config.bak")==original,"Atomic assignment backup contains a committed prefix");
 for(const auto& row:changed.commit.snapshot.tags()->tags){need(row.uniqueHashes.size()==1&&row.uniqueHashes.front()==area.hash,"Batch omitted a complete membership");}
 for(unsigned task=0;task<64;++task){auto command=take(area.owner->next(*area.store,changed.commit.snapshot,*changed.receipt));need(command.commands.count==1&&command.commands.values[0]==(task%2?a::Effect::ForceStart:a::Effect::Queue),"Batch receipt changed original tag/task order");}
 need(changed.receipt->done(),"64-task batch did not retire receipt");
 auto noop=take(area.owner->changeBatch(*area.store,changed.commit.snapshot,edits,area.observed()));need(!noop.commit.changed&&!noop.receipt,"Restored/no-op batch replayed actions");
 records["32"]=dict({{"n",bytes("Unsupported generated last tag")},{"c",dict({{"pp.32","i1e"}})},{"o","le"}});area.write("tag.config",dict({{"3",dict(records)}}));before=take(area.store->reload());const auto unsupported=read(area.root.path/"tag.config");need(!area.owner->changeBatch(*area.store,before,edits,area.observed())&&read(area.root.path/"tag.config")==unsupported,"Unsupported last tag published supported earlier assignments");
}
void immutableLifecyclePlanIsDataUntilItsActualOwnerPublishes(){
 Area area(std::map<std::string,std::string>{{"pp.2","i1e"},{"pp.8","i1e"}});auto before=area.snapshot();const auto original=read(area.root.path/"tag.config");
 auto base=take(area.store->planMembership(before));auto staged=take(area.store->extendMembershipPlan(base,b::SavedTagAddMember{1,area.hash,100}));
 need(staged.edits()==1&&!area.store->publishMembershipPlan(staged)&&read(area.root.path/"tag.config")==original,"Passive lifecycle plan became a public writer grant");
 Area other(std::map<std::string,std::string>{{"pp.2","i1e"}});need(!other.owner->publishPlan(*area.store,before,staged,area.observed()),"Foreign owner published lifecycle plan");
 std::array<s::ManualTagMembershipEdit,1> edits{b::SavedTagAddMember{1,area.hash,100}};
 need(!area.owner->changeBatch(*area.store,before,edits,area.observed(),{},std::string_view("de"))&&read(area.root.path/"tag.config")==original,"Prepared-byte mismatch wrote a prefix");
 auto saved=take(area.owner->publishPlan(*area.store,before,staged,area.observed()));need(saved.commit.changed&&saved.receipt&&*saved.commit.snapshot.rawBytes()==*staged.rawBytes(),"Actual plan receipt changed its exact replay");
 need(!area.owner->publishPlan(*area.store,saved.commit.snapshot,staged,area.observed()),"Committed plan base replayed");
}
}
int main(){try{actualCommittedReceiptAndOnceOnlyCommands();noOpRemovalAndMetadataHaveNoDispatch();strictCapabilitiesAndCancelledPublication();currentScopeAndForeignReceipts();executionUsesCurrentFlagsAndCapturedForce();recoveryPendingAndFailureNeverPublish();closeDoesNotLeaveReceiptsHoldingTheProfile();boundedPendingReceiptsNeverSaveAnUnscheduledPrefix();atomicSameSubjectBatchesAndNoPrefix();immutableLifecyclePlanIsDataUntilItsActualOwnerPublishes();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
 std::cout<<"PASS10 actual assignment owner/profile families;fresh CAS receipts,dynamic flags,once-only commands,strict public writers,scope,recovery,cancellation,close and32-receipt backpressure\n";
}
