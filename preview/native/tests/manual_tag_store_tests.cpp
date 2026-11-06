// SPDX-License-Identifier: GPL-2.0-or-later
#include "manual_tag_store.hpp"
#include <array>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
namespace s=biglybtc::storage;namespace c=biglybtc::core;
namespace {
void check(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
std::string bytes(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string dict(std::initializer_list<std::pair<std::string,std::string>> rows){std::string out="d";for(const auto& [key,value]:rows){out+=bytes(key)+value;}return out+'e';}
std::string list(std::initializer_list<std::string> rows){std::string out="l";for(const auto& value:rows){out+=value;}return out+'e';}
struct Area {
  std::filesystem::path root;std::optional<s::ProfileLease> lease;
  Area(){std::string path="/tmp/biglybtc-manual-tag-store-XXXXXX";const auto* made=::mkdtemp(path.data());check(made,"Private fixture creation failed");root=made;check(::chmod(root.c_str(),0700)==0,"Private fixture mode failed");auto held=s::ProfileLease::acquire(root);check(bool(held),"Actual generated profile lease required");lease.emplace(std::move(*held));}
  ~Area(){lease.reset();(void)::chmod(root.c_str(),0700);std::error_code error;std::filesystem::remove_all(root,error);}
  void write(const char* name,std::string_view value){const auto path=root/name;std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(value.data(),static_cast<std::streamsize>(value.size()));out.close();check(bool(out),"Fixture write failed");check(::chmod(path.c_str(),0600)==0,"Fixture ownership mode failed");}
  std::string read(const char* name)const{std::ifstream in(root/name,std::ios::binary);check(bool(in),"Fixture read failed");return {std::istreambuf_iterator<char>(in),{}};}
};
std::unique_ptr<s::ManualTagStore> opened(const Area& area){auto value=s::ManualTagStore::open(*area.lease);check(bool(value),"Expected manual-tag store open");return std::move(*value);}
s::ManualTagSnapshot snapshot(s::ManualTagStore& store){auto value=store.snapshot();check(bool(value),"Expected owning snapshot");return std::move(*value);}
s::ManualTagCommit apply(s::ManualTagStore& store,const c::SavedTagEdit& edit){auto before=snapshot(store);auto value=store.apply(before,edit);check(bool(value),"Expected compatible store commit");return std::move(*value);}
void refused(s::ManualTagStore& store,const s::ManualTagSnapshot& expected,const c::SavedTagEdit& edit,s::ErrorCode code){auto value=store.apply(expected,edit);check(!value&&value.error().code==code,"Unexpected store refusal");}
std::string one(std::string_view name="one"){return dict({{"3",dict({{"1",dict({{"n",bytes(name)}})}})}});}
std::string encoded(const s::ManualTagSnapshot& value){check(value.tags()&&value.tags()->source,"Expected owning tag projection");return value.tags()->source->serialize();}
c::TorrentHash hash(){std::string value;for(unsigned i=0;i<20;++i){value.push_back(static_cast<char>(i));}auto parsed=c::TorrentHash::parse(value);check(bool(parsed),"Typed hash fixture invalid");return std::move(*parsed);}
struct FileStamp {std::uint64_t dev,ino,size;std::int64_t msec,mnano,csec,cnano;bool operator==(const FileStamp&)const=default;};
FileStamp stamp(const std::filesystem::path& path){struct stat v{};check(::stat(path.c_str(),&v)==0,"Fixture stamp failed");return {v.st_dev,v.st_ino,static_cast<std::uint64_t>(v.st_size),v.st_mtim.tv_sec,v.st_mtim.tv_nsec,v.st_ctim.tv_sec,v.st_ctim.tv_nsec};}
void absenceFirstCreateAndSessionCounter(){
  Area area;auto store=opened(area);auto initial=snapshot(*store);check(initial.valid()&&initial.missing()&&!initial.rawBytes()&&encoded(initial)=="de","Missing state fabricated a physical preimage");
  check(!std::filesystem::exists(area.root/"tag.config")&&!std::filesystem::exists(area.root/"tag.config.bak")&&!std::filesystem::exists(area.root/"tag.config.saving"),"Read created state");
  auto first=apply(*store,c::SavedTagCreate{"first",{}});check(first.changed&&first.createdId==1&&first.createdUid==12884901889LL&&!first.snapshot.missing()&&first.snapshot.recoverySource()==s::RecoverySource::Primary,"First create id/uid/source mismatch");
  check(area.read("tag.config")==encoded(first.snapshot)&&!std::filesystem::exists(area.root/"tag.config.bak"),"First create invented backup or lost exact bytes");struct stat mode{};check(::stat((area.root/"tag.config").c_str(),&mode)==0&&(mode.st_mode&0777)==0600,"New original-compatible file must be0600");
  const auto second=apply(*store,c::SavedTagCreate{"second",{}});check(second.createdId==2&&area.read("tag.config.bak")==encoded(first.snapshot),"Retained store allocator or normal backup rotation differs");
  check(bool(store->close()),"Worker close refused");store=opened(area);check(apply(*store,c::SavedTagCreate{"reloaded",{}}).createdId==4,"Explicit reopen must use original loaded max+2 counter");
}
void unknownBytesMembershipNoopAndBackup(){
  Area area;const auto opaque=dict({{"z",bytes("future")},{"a",list({"i-9e"})}});const auto member=bytes("unresolved-future-id");const auto properties=dict({{"a","i42e"},{"u",opaque}});const auto original=dict({{"future",opaque},{"3",dict({{"c",opaque},{"9",dict({{"n",bytes("untouched")},{"u",opaque}})},{"1",dict({{"u",opaque},{"n",bytes("tag.original")},{"o",list({member})},{"p",list({properties})}})}})}});
  area.write("tag.config",original);auto store=opened(area);const auto renamed=apply(*store,c::SavedTagRename{1,"renamed"});check(renamed.changed&&area.read("tag.config.bak")==original,"Save lost original pristine preimage or backup");
  const auto* manual=renamed.snapshot.tags()->source->find("3");check(manual->find("1")->find("c")->find("oname")->bytes=="tag.original","Original oname not persisted");
  const auto added=apply(*store,c::SavedTagAddMember{1,hash(),123});const auto& doc=*added.snapshot.tags()->source;const auto* row=doc.find("3")->find("1");check(row->find("o")->children[0].bytes=="unresolved-future-id"&&row->find("o")->children[1].bytes=="AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT","Raw unresolved or canonical original member changed");
  check(doc.originalBytes().substr(doc.find("future")->begin,doc.find("future")->end-doc.find("future")->begin)==opaque,"Unknown root value rewritten");check(row->find("p")->children[1].find("a")->integer==123,"Original parallel added-time lost");
  const auto before=area.read("tag.config");const auto beforeBackup=area.read("tag.config.bak");const auto identity=stamp(area.root/"tag.config");const auto generation=snapshot(*store).generation();
  check(!apply(*store,c::SavedTagAddMember{1,hash(),999}).changed,"Existing member should be no-op");check(area.read("tag.config")==before&&area.read("tag.config.bak")==beforeBackup&&stamp(area.root/"tag.config")==identity&&snapshot(*store).generation()==generation,"No-op rewrote files or advanced snapshot");
  check(apply(*store,c::SavedTagRemoveMember{1,hash()}).changed,"Member removal refused");check(apply(*store,c::SavedTagVisibility{1,false}).changed,"Visibility change refused");check(apply(*store,c::SavedTagRemove{1}).changed&&snapshot(*store).tags()->tags.size()==1,"Selected record remove affected sibling");
}
void staleGenerationAndExactCandidateCas(){
  Area area;area.write("tag.config",one());auto store=opened(area);auto stale=snapshot(*store);auto fresh=store->reload();check(bool(fresh)&&fresh->generation()>stale.generation(),"Explicit reload must invalidate snapshot token");refused(*store,stale,c::SavedTagRename{1,"stale"},s::ErrorCode::Conflict);
  auto other=opened(area);refused(*store,snapshot(*other),c::SavedTagRename{1,"foreign token"},s::ErrorCode::Conflict);
  auto before=snapshot(*store);area.write("tag.config",one("external"));refused(*store,before,c::SavedTagRename{1,"overwrite"},s::ErrorCode::Conflict);check(area.read("tag.config")==one("external"),"Stale raw CAS overwrote external state");
  check(bool(store->reload()),"Explicit reload failed");before=snapshot(*store);area.write("replacement",area.read("tag.config"));std::filesystem::rename(area.root/"replacement",area.root/"tag.config");refused(*store,before,c::SavedTagRename{1,"same bytes new inode"},s::ErrorCode::Conflict);
  check(bool(store->reload()),"Replacement reload failed");before=snapshot(*store);area.write("tag.config.bak",one("external backup"));refused(*store,before,c::SavedTagRename{1,"ignore backup"},s::ErrorCode::Conflict);
  check(bool(store->reload()),"Backup reload failed");before=snapshot(*store);area.write("tag.config.saving",one("crash"));refused(*store,before,c::SavedTagRename{1,"ignore saving"},s::ErrorCode::Conflict);
}
void recoveryCandidatesStayReadOnly(){
  for(unsigned which=0;which<4;++which){
    Area area;const auto original=one("recovery");
    if(which==0){area.write("tag.config.saving",original);}
    if(which==1){area.write("tag.config.bak",original);}
    if(which==2){area.write("tag.config","broken");area.write("tag.config.bak",original);}
    if(which==3){area.write("tag.config",original);area.write("tag.config.saving",one("pending"));}
    auto store=opened(area);const auto old=snapshot(*store);check(!old.missing(),"Recovery mistaken for absent state");refused(*store,old,c::SavedTagRename{1,"repair"},s::ErrorCode::Conflict);
    if(which==0){check(area.read("tag.config.saving")==original&&!std::filesystem::exists(area.root/"tag.config"),"Saving-only candidate implicitly promoted");}
    if(which==2){check(area.read("tag.config")=="broken"&&area.read("tag.config.bak")==original,"Corrupt primary implicitly repaired");}
    if(which==3){check(area.read("tag.config.saving")==one("pending")&&area.read("tag.config")==original,"Ambiguous saving overwritten");}
  }
  Area corrupt;corrupt.write("tag.config.saving","broken");check(!s::ManualTagStore::open(*corrupt.lease),"Corrupt existing recovery became absent create authority");
  Area unsafe;unsafe.write("outside",one());std::filesystem::create_symlink(unsafe.root/"outside",unsafe.root/"tag.config");auto denied=s::ManualTagStore::open(*unsafe.lease);check(!denied&&denied.error().code==s::ErrorCode::UnsafePath,"Symlink candidate followed");
  Area linked;linked.write("outside",one());std::filesystem::create_hard_link(linked.root/"outside",linked.root/"tag.config");denied=s::ManualTagStore::open(*linked.lease);check(!denied&&denied.error().code==s::ErrorCode::UnsafePath,"Aliased candidate accepted");
}
void cancellationMalformedAndFirstCreateRace(){
  Area area;auto store=opened(area);const auto before=snapshot(*store);std::stop_source stop;stop.request_stop();auto cancelled=store->apply(before,c::SavedTagCreate{"cancelled",{}},stop.get_token());check(!cancelled&&cancelled.error().code==s::ErrorCode::Cancelled&&!std::filesystem::exists(area.root/"tag.config.saving")&&!std::filesystem::exists(area.root/"tag.config"),"Cancellation published state");
  check(!s::ManualTagStore::open(*area.lease,stop.get_token()),"Stopped open accepted");area.write("tag.config.saving",one("race"));refused(*store,before,c::SavedTagCreate{"overwrite recovery",{}},s::ErrorCode::Conflict);check(area.read("tag.config.saving")==one("race")&&!std::filesystem::exists(area.root/"tag.config"),"First create overwrote raced recovery");
  for(const auto& raw:{dict({{"3",dict({{"1",dict({{"n",bytes("one")}})},{"01",dict({{"n",bytes("alias")}})}})}}),dict({{"3",dict({{"1",dict({{"n",bytes("one")},{"o",bytes("bad")}})}})}})}){
    Area invalid;invalid.write("tag.config",raw);auto openedStore=opened(invalid);const auto stampBefore=stamp(invalid.root/"tag.config");const auto edit=raw.find("alias")!=raw.npos?c::SavedTagEdit(c::SavedTagRename{1,"no"}):c::SavedTagEdit(c::SavedTagAddMember{1,hash(),0});refused(*openedStore,snapshot(*openedStore),edit,s::ErrorCode::InvalidFormat);check(invalid.read("tag.config")==raw&&stamp(invalid.root/"tag.config")==stampBefore&&!std::filesystem::exists(invalid.root/"tag.config.saving"),"Malformed data was rewritten");
  }
  Area valid;auto owned=opened(valid);refused(*owned,snapshot(*owned),c::SavedTagCreate{"foreign allocator",3},s::ErrorCode::Unsupported);
}
void uncertainSavePoisonsCounter(){
  check(::geteuid()!=0,"These owned filesystem controls require the normal unprivileged test worker");
  Area area;auto store=opened(area);check(apply(*store,c::SavedTagCreate{"first",{}}).createdId==1,"Initial allocator setup failed");const auto before=area.read("tag.config");const auto expected=snapshot(*store);check(::chmod(area.root.c_str(),0500)==0,"Cannot make owned publication fail");
  const auto failed=store->apply(expected,c::SavedTagCreate{"not committed",{}});check(::chmod(area.root.c_str(),0700)==0,"Cannot restore fixture mode");check(!failed,"Unwritable directory unexpectedly published");auto stale=store->snapshot();check(!stale&&stale.error().code==s::ErrorCode::Conflict,"Uncertain attempted save did not poison store");refused(*store,expected,c::SavedTagCreate{"replay",{}},s::ErrorCode::Conflict);check(area.read("tag.config")==before,"Failure changed original bytes");check(bool(store->close()),"Poisoned store close failed");store=opened(area);check(apply(*store,c::SavedTagCreate{"new explicit session",{}}).createdId==3,"Failed allocator attempt was replayed after explicit reopen");
}
void workerAndOfdLifetime(){
  Area area;auto store=opened(area);auto ownedSnapshot=snapshot(*store);area.lease.reset();auto blocked=s::ProfileLease::acquire(area.root);check(!blocked&&blocked.error().code==s::ErrorCode::Busy,"Store failed to retain exact actual OFD");
  s::ErrorCode foreign=s::ErrorCode::Io;std::thread thread([&]{auto result=store->snapshot();if(!result){foreign=result.error().code;}});thread.join();check(foreign==s::ErrorCode::Unsupported,"Foreign thread entered store");
  check(bool(store->close()),"Worker close failed");auto released=s::ProfileLease::acquire(area.root);check(bool(released)&&ownedSnapshot.valid()&&ownedSnapshot.tags(),"Owning snapshot retained profile OFD or lost its bytes");
  Area replacement;replacement.write("tag.config",one());auto current=opened(replacement);auto expected=snapshot(*current);replacement.write("new-lock","");std::filesystem::rename(replacement.root/"new-lock",replacement.root/".azlock");auto refusedLock=current->apply(expected,c::SavedTagRename{1,"must refuse"});check(!refusedLock&&refusedLock.error().code==s::ErrorCode::Conflict&&replacement.read("tag.config")==one(),"Changed actual named OFD lock accepted");
}
void actualConfiguredGroupMembershipRefusesWithoutPublication(){
  Area area;const auto grouped=dict({{"3",dict({{"c",dict({{"I4",dict({{"x","i1e"}})}})},{"1",dict({{"n",bytes("Grouped")},{"c",dict({{"gr",bytes("G")}})},{"o",list({bytes("AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT")})}})}})}});
  area.write("tag.config",grouped);auto store=opened(area);const auto expected=snapshot(*store);const auto before=area.read("tag.config");const auto identity=stamp(area.root/"tag.config");
  refused(*store,expected,c::SavedTagRemoveMember{1,hash()},s::ErrorCode::Unsupported);
  const auto another=c::TorrentHash::parse(std::string(20,'Q'));check(bool(another),"Actual distinct typed member identity");
  refused(*store,expected,c::SavedTagAddMember{1,*another,1},s::ErrorCode::Unsupported);
  check(!apply(*store,c::SavedTagAddMember{1,hash(),1}).changed&&!apply(*store,c::SavedTagRemoveMember{1,*another}).changed,"No-event group membership no-op was refused");
  check(area.read("tag.config")==before&&stamp(area.root/"tag.config")==identity&&!std::filesystem::exists(area.root/"tag.config.bak")&&!std::filesystem::exists(area.root/"tag.config.saving")&&snapshot(*store).generation()==expected.generation(),"Group policy refusal wrote/rotated sources or changed lineage");
  check(bool(store->close()),"Actual grouped writer close");
}
void actualConfiguredTagChangesRefuseButNoEventNoopsStayUnchanged(){
  for(const auto& config:{dict({{"rl.up","i1024e"}}),dict({{"rl.down","i512e"}}),dict({{"pp.2","i1e"}}),dict({{"pp.tracker_templates",list({bytes("apply:template")})}}),dict({{"pp.trackers",list({bytes("tracker")})}}),dict({{"future-effect","i0e"}})}){
    Area area;const auto before=dict({{"future",bytes("keep")},{"3",dict({{"1",dict({{"n",bytes("Configured")},{"c",config},{"o",list({bytes("AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT")})}})}})}});area.write("tag.config",before);
    auto store=opened(area);const auto expected=snapshot(*store);const auto identity=stamp(area.root/"tag.config");
    refused(*store,expected,c::SavedTagRemoveMember{1,hash()},s::ErrorCode::Unsupported);
    const auto another=c::TorrentHash::parse(std::string(20,'Q'));check(bool(another),"Distinct typed member fixture");refused(*store,expected,c::SavedTagAddMember{1,*another,1},s::ErrorCode::Unsupported);
    check(!apply(*store,c::SavedTagAddMember{1,hash(),2}).changed&&!apply(*store,c::SavedTagRemoveMember{1,*another}).changed,"No-event membership no-op was rejected or mutated");
    check(area.read("tag.config")==before&&stamp(area.root/"tag.config")==identity&&snapshot(*store).generation()==expected.generation()&&!std::filesystem::exists(area.root/"tag.config.bak")&&!std::filesystem::exists(area.root/"tag.config.saving"),"Refused configured effect changed actual profile source/lineage");
    check(bool(store->close()),"Configured actual store close");
  }
}
void actualGroupSaveNoopStaleAndReopenPreserveOriginalBytes(){
  Area area;const auto before=dict({{"future",bytes("keep")},{"3",dict({{"1",dict({{"n",bytes("Tag")},{"future-row",list({"i7e"})}})}})}});area.write("tag.config",before);
  auto store=opened(area);const auto old=snapshot(*store);auto saved=apply(*store,c::SavedTagSetGroup{1,std::string("Work")});check(saved.changed&&area.read("tag.config.bak")==before,"Actual ordinary group was not durably saved");
  const auto identity=stamp(area.root/"tag.config");const auto bytesBefore=area.read("tag.config");const auto backup=area.read("tag.config.bak");const auto generation=snapshot(*store).generation();
  check(!apply(*store,c::SavedTagSetGroup{1,std::string("Work")}).changed&&area.read("tag.config")==bytesBefore&&area.read("tag.config.bak")==backup&&stamp(area.root/"tag.config")==identity&&snapshot(*store).generation()==generation,"Group no-op changed sources/lineage");
  refused(*store,old,c::SavedTagSetGroup{1,std::string("Stale")},s::ErrorCode::Conflict);check(bool(store->close()),"Group writer close");store=opened(area);
  auto reopened=snapshot(*store);check(reopened.tags()->source->find("3")->find("1")->find("c")->find("gr")->bytes=="Work","Group did not survive actual reopen");
  auto cleared=apply(*store,c::SavedTagSetGroup{1,{}});check(cleared.changed&&!cleared.snapshot.tags()->source->find("3")->find("1")->find("c")->find("gr")&&cleared.snapshot.tags()->source->find("future")->bytes=="keep","Group clear lost original unknown source");
  check(bool(store->close()),"Group clear writer close");
}
void actualColourCanonicalSaveClearRefusalAndReopen(){
  Area area;const auto before=dict({{"future",bytes("keep")},{"3",dict({{"1",dict({{"n",bytes("Tag")},{"c",dict({{"col.rgb",bytes("+01,2,3,,")}})}})}})}});area.write("tag.config",before);auto store=opened(area);const auto stale=snapshot(*store);
  auto saved=apply(*store,c::SavedTagSetColour{1,c::SavedTagRgb{1,2,3}});check(saved.changed&&area.read("tag.config.bak")==before&&saved.snapshot.tags()->source->find("3")->find("1")->find("c")->find("col.rgb")->bytes=="1,2,3","Actual setter failed canonical string save");
  const auto identity=stamp(area.root/"tag.config"),beforeStamp=identity;const auto bytesBefore=area.read("tag.config"),backup=area.read("tag.config.bak");const auto generation=snapshot(*store).generation();check(!apply(*store,c::SavedTagSetColour{1,c::SavedTagRgb{1,2,3}}).changed&&stamp(area.root/"tag.config")==identity&&snapshot(*store).generation()==generation&&area.read("tag.config.bak")==backup,"Colour exact no-op changed file/lineage");
  refused(*store,stale,c::SavedTagSetColour{1,{}},s::ErrorCode::Conflict);std::stop_source stop;stop.request_stop();auto cancelled=store->apply(snapshot(*store),c::SavedTagSetColour{1,{}},stop.get_token());check(!cancelled&&cancelled.error().code==s::ErrorCode::Cancelled&&area.read("tag.config")==bytesBefore&&stamp(area.root/"tag.config")==beforeStamp,"Cancelled colour edit wrote source");
  check(bool(store->close()),"Colour writer close");store=opened(area);auto restored=snapshot(*store);auto colour=c::readSavedTagColour(*restored.tags()->source->find("3")->find("1"));check(colour&&*colour&&**colour==c::SavedTagRgb{1,2,3},"Colour not retained after real reopen");
  auto member=apply(*store,c::SavedTagAddMember{1,hash(),42});check(member.changed&&member.snapshot.tags()->source->find("3")->find("1")->find("c")->find("col.rgb")->bytes=="1,2,3","Benign colour metadata blocked the real ordinary Store membership path");
  auto cleared=apply(*store,c::SavedTagSetColour{1,{}});check(cleared.changed&&!cleared.snapshot.tags()->source->find("3")->find("1")->find("c")->find("col.rgb")&&cleared.snapshot.tags()->source->find("future")->bytes=="keep","Real default clear damaged unknown source");check(!apply(*store,c::SavedTagSetColour{1,{}}).changed,"Repeated default clear wrote source");check(bool(store->close()),"Default writer close");
  for(const auto& config:{dict({{"col.rgb",bytes("bad")}}),dict({{"col.rgb","i7e"}}),dict({{"col.rgb",bytes("1,2,3")},{"rl.up","i1024e"}}),dict({{"col.rgb",bytes("1,2,3")},{"pp.2","i1e"}})}){
    Area rejected;const auto original=dict({{"3",dict({{"1",dict({{"n",bytes("Configured")},{"c",config}})}})}});rejected.write("tag.config",original);auto held=opened(rejected);const auto expected=snapshot(*held);const auto stampBefore=stamp(rejected.root/"tag.config");
    const bool malformed=!c::readSavedTagColour(*expected.tags()->source->find("3")->find("1"));
    if(malformed){refused(*held,expected,c::SavedTagSetColour{1,{}},s::ErrorCode::InvalidFormat);}else{refused(*held,expected,c::SavedTagAddMember{1,hash(),1},s::ErrorCode::Unsupported);}
    check(rejected.read("tag.config")==original&&stamp(rejected.root/"tag.config")==stampBefore&&snapshot(*held).generation()==expected.generation()&&!std::filesystem::exists(rejected.root/"tag.config.bak")&&!std::filesystem::exists(rejected.root/"tag.config.saving"),"Malformed colour/configured effect refusal changed actual profile");check(bool(held->close()),"Rejected colour writer close");
  }
}
} // namespace
int main(){try{absenceFirstCreateAndSessionCounter();unknownBytesMembershipNoopAndBackup();staleGenerationAndExactCandidateCas();recoveryCandidatesStayReadOnly();cancellationMalformedAndFirstCreateRace();uncertainSavePoisonsCounter();workerAndOfdLifetime();actualConfiguredGroupMembershipRefusesWithoutPublication();actualConfiguredTagChangesRefuseButNoEventNoopsStayUnchanged();actualGroupSaveNoopStaleAndReopenPreserveOriginalBytes();actualColourCanonicalSaveClearRefusalAndReopen();std::cout<<"PASS eleven isolated worker manual-tag store families\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
