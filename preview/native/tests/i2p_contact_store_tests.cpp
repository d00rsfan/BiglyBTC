// SPDX-License-Identifier: GPL-2.0-or-later
#include "contact_store.hpp"
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace s=biglybtc::storage;namespace i=biglybtc::services::i2p;namespace d=i::dht;
namespace fs=std::filesystem;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
std::string bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);check(bool(in),"Missing generated file");return std::string(std::istreambuf_iterator<char>(in),{});}
void put(const fs::path& path,std::string_view data){std::ofstream out(path,std::ios::binary|std::ios::trunc);check(bool(out),"Cannot open generated file");out.write(data.data(),static_cast<std::streamsize>(data.size()));out.close();check(bool(out),"Cannot write generated file");}
std::string wire(std::span<const d::SavedContact> rows){auto encoded=d::encodeSavedContacts(rows);check(bool(encoded),"Generated secure contact encoding failed");return std::string(reinterpret_cast<const char*>(encoded->data()),encoded->size());}
std::vector<d::SavedContact> rows(std::size_t count){
  std::vector<d::SavedContact> result;
  for(std::size_t n=0;n<count;++n){d::SavedContact row;row.contact.hash.fill(static_cast<i::Byte>(n+1));row.contact.port=static_cast<std::uint16_t>(20000+n);auto id=d::makeNodeId(row.contact.hash,row.contact.port,i::Bytes(14,static_cast<i::Byte>(n+2)));check(bool(id),"Actual secure NodeInfo fixture failed");row.contact.id=*id;row.timeAlive=static_cast<std::int64_t>(n)-16;row.protocolVersion=static_cast<std::int8_t>(n);result.push_back(row);}return result;
}
struct Fixture {
  fs::path root,profile,helper;
  std::optional<s::ProfileLease> lease;
  Fixture(){std::array<char,64> name{};const std::string pattern="/tmp/biglybtc-i2p-contacts-XXXXXX";std::copy(pattern.begin(),pattern.end(),name.begin());const auto* made=::mkdtemp(name.data());check(made!=nullptr,"Cannot create generated isolated directory");root=made;profile=root/"profile";helper=profile/"plugins/azneti2phelper";fs::create_directories(helper);lease.emplace(take(s::ProfileLease::acquire(profile)));}
  ~Fixture(){lease.reset();std::error_code error;fs::remove_all(root,error);}
  Fixture(const Fixture&)=delete;
  fs::path cache(unsigned index=0,bool secondary=false)const{return helper/("dhtdata"+std::string(index?"1":"")+(secondary?"_s":""));}
  fs::path key(unsigned index=0,bool secondary=false)const{return helper/("dest_key"+std::string(index?"1":"")+(secondary?"_s":"")+".dat");}
  std::shared_ptr<s::I2pContacts> open(unsigned index=0,bool secondary=false){return take(s::I2pContacts::open(*lease,index,secondary));}
};
std::map<std::string,std::string> files(const fs::path& root){std::map<std::string,std::string> result;for(const auto& entry:fs::recursive_directory_iterator(root)){if(entry.is_regular_file()){result.emplace(fs::relative(entry.path(),root).string(),bytes(entry.path()));}}return result;}
void allOriginalSelectorsAreIndependentAndRoundTrip(){
  Fixture f;const auto maximum=rows(32);
  for(unsigned index=0;index<2;++index){for(bool secondary:{false,true}){fs::create_directory(f.cache(index,secondary));put(f.key(index,secondary),"opaque generated key marker");put(f.cache(index,secondary)/"unrelated","preserve cache sibling");}}
  put(f.helper/"dht.config","independent config");put(f.helper/"future.data","unknown plugin bytes");
  for(unsigned index=0;index<2;++index){for(bool secondary:{false,true}){
    const auto before=files(f.profile);auto owner=f.open(index,secondary);check(owner->index()==index&&owner->secondary()==secondary,"Selected original path index changed");check(owner->profileOwnership()->sharesOwnership(*f.lease),"Cache did not retain the same actual OFD");
    auto empty=take(owner->snapshot());check(empty->writable&&empty->contacts.empty(),"Absent cache is not owned empty writable state");
    check(bool(owner->save(maximum)),"Maximum32 save failed");auto full=take(owner->snapshot());check(full->writable&&wire(full->contacts)==wire(maximum),"Maximum ordered contact rows changed");check(bytes(f.cache(index,secondary)/"contacts.dat")==wire(maximum),"Original cache bytes differ");check(!fs::exists(f.cache(index,secondary)/"contacts.saving"),"Saving input retained after durable publication");
    const auto after=files(f.profile);for(const auto& [name,data]:before){check(after.at(name)==data,"Unselected cache/key/config sibling changed");}check(after.size()==before.size()+1,"Unexpected publication leaf");
    owner.reset();owner=f.open(index,secondary);check(wire(take(owner->snapshot())->contacts)==wire(maximum),"Reopened original cache differs");
    check(bool(owner->save({})),"Valid empty cache publication failed");check(take(owner->snapshot())->contacts.empty()&&bytes(f.cache(index,secondary)/"contacts.dat")==std::string(4,'\0'),"Empty original count was not persisted");check(full->contacts.size()==32,"Snapshot borrowed mutable owner rows");
  }}
  check(!s::I2pContacts::open(*f.lease,2),"Unsupported original index created a path");check(!fs::exists(f.helper/"dhtdata2"),"Invalid selector mutated original tree");
}
void savingRecoveryAndAmbiguousInputsPreserveOriginalSelection(){
  const auto selected=rows(2);const auto encoded=wire(selected);
  {Fixture f;fs::create_directory(f.cache());put(f.cache()/"contacts.saving",encoded);struct stat before{},after{};check(::stat((f.cache()/"contacts.saving").c_str(),&before)==0,"Missing saving inode");auto owner=f.open();check(::stat((f.cache()/"contacts.dat").c_str(),&after)==0,"Missing recovered inode");check(before.st_dev==after.st_dev&&before.st_ino==after.st_ino,"Recovery copied or replaced the held saving inode");check(!fs::exists(f.cache()/"contacts.saving")&&bytes(f.cache()/"contacts.dat")==encoded,"Recovery did not preserve original bytes");check(take(owner->snapshot())->writable,"Recovered cache unexpectedly read-only");}
  {Fixture f;fs::create_directory(f.cache());put(f.cache()/"contacts.dat",encoded);put(f.cache()/"contacts.saving","unknown incomplete pending input");const auto before=files(f.profile);auto owner=f.open();auto snapshot=take(owner->snapshot());check(!snapshot->writable&&wire(snapshot->contacts)==encoded,"Primary did not win ambiguous input selection");auto saved=owner->save(rows(1));check(!saved&&saved.error().code==s::ErrorCode::Unsupported,"Ambiguous input was made writable");check(files(f.profile)==before,"Ambiguous input was removed or rewritten");}
  for(const auto& malformed:std::array<std::string,3>{std::string{},std::string(4,'\xff'),std::string("bad")}){
    Fixture f;fs::create_directory(f.cache());put(f.cache()/"contacts.dat",malformed);put(f.cache()/"contacts.saving",encoded);const auto before=files(f.profile);auto failed=s::I2pContacts::open(*f.lease,0);check(!failed&&failed.error().code==s::ErrorCode::InvalidFormat,"Corrupt existing primary fell back to saving");check(files(f.profile)==before,"Malformed primary was rewritten");
  }
}
void unsafeOriginalPathsAreNeverFollowed(){
  for(const auto* name:{"contacts.dat","contacts.saving"}){for(unsigned kind=0;kind<4;++kind){
    Fixture f;fs::create_directory(f.cache());const auto path=f.cache()/name;put(f.root/"sentinel","do not change");
    if(kind==0){check(::symlink((f.root/"sentinel").c_str(),path.c_str())==0,"Symlink setup failed");}
    else if(kind==1){check(::link((f.root/"sentinel").c_str(),path.c_str())==0,"Hardlink setup failed");}
    else if(kind==2){check(::mkfifo(path.c_str(),0600)==0,"FIFO setup failed");}
    else{fs::create_directory(path);}
    check(!s::I2pContacts::open(*f.lease,0),"Unsafe contacts input accepted");check(bytes(f.root/"sentinel")=="do not change","Unsafe target was changed");check(fs::symlink_status(path).type()!=fs::file_type::not_found,"Unsafe input removed");
  }}
  for(unsigned level=0;level<3;++level){Fixture f;fs::create_directory(f.cache());const auto selected=level==0?f.profile/"plugins":level==1?f.helper:f.cache();const auto moved=f.root/"retained";fs::rename(selected,moved);check(::symlink(moved.c_str(),selected.c_str())==0,"Directory symlink setup failed");check(!s::I2pContacts::open(*f.lease,0),"Symlinked plugin/helper/cache directory accepted");check(!fs::exists(moved/"contacts.dat"),"Symlinked directory mutated");}
  {Fixture f;fs::remove_all(f.profile/"plugins");check(!s::I2pContacts::open(*f.lease,0),"Missing plugin parent was invented");check(!fs::exists(f.profile/"plugins"),"Owner created plugin parents");}
}
void externalFileParentKeyAndLockChangesAreRefused(){
  for(unsigned kind=0;kind<3;++kind){Fixture f;auto owner=f.open();check(bool(owner->save(rows(1))),"Initial save failed");const auto path=f.cache()/"contacts.dat";
    if(kind==0){put(path,"external bytes");}else if(kind==1){fs::rename(path,f.cache()/"old.dat");put(path,wire(rows(1)));}else{fs::remove(path);}
    const auto before=files(f.profile);check(!owner->validate()&&!owner->snapshot()&&!owner->save(rows(2)),"Changed contact input was blessed");check(files(f.profile)==before&&!fs::exists(f.cache()/"contacts.saving"),"Changed contact input overwritten");
  }
  for(unsigned level=0;level<4;++level){Fixture f;auto owner=f.open();const auto path=level==0?f.cache():level==1?f.helper:level==2?f.profile/"plugins":f.profile;const auto moved=f.root/"old-parent";fs::rename(path,moved);fs::create_directory(path);const auto before=files(f.root);check(!owner->validate()&&!owner->save(rows(1)),"Replaced named parent accepted");check(files(f.root)==before,"Replaced parent caused orphan/publication write");}
  for(unsigned kind=0;kind<4;++kind){Fixture f;if(kind!=0){put(f.key(),"opaque original key marker");}auto owner=f.open();
    if(kind==0){put(f.key(),"new key marker");}else if(kind==1){put(f.key(),"changed key marker");}else if(kind==2){fs::rename(f.key(),f.helper/"old-key");put(f.key(),"opaque original key marker");}else{fs::remove(f.key());}
    const auto before=files(f.profile);check(!owner->validate()&&!owner->save(rows(1)),"Changed key presence/inode/stat accepted");check(files(f.profile)==before,"Key conflict altered original cache");
  }
  {Fixture f;auto owner=f.open();fs::rename(f.profile/".azlock",f.profile/"old-lock");put(f.profile/".azlock","");const auto before=files(f.profile);check(!owner->save(rows(1)),"Replaced named OFD lock accepted");check(files(f.profile)==before,"Lock conflict wrote cache");}
  {Fixture f;put(f.root/"target","opaque");check(::symlink((f.root/"target").c_str(),f.key().c_str())==0,"Key symlink setup failed");check(!s::I2pContacts::open(*f.lease,0),"Symlinked key accepted");}
  {Fixture f;put(f.root/"target","opaque");check(::link((f.root/"target").c_str(),f.key().c_str())==0,"Key hardlink setup failed");check(!s::I2pContacts::open(*f.lease,0),"Hardlinked key accepted");}
}
void cancellationRetainedLeaseAndIndependentProperties(){
  Fixture f;put(f.key(),"opaque original key marker");auto owner=f.open();std::stop_source stop;stop.request_stop();const auto before=files(f.profile);
  auto cancelled=owner->save(rows(1),stop.get_token());check(!cancelled&&cancelled.error().code==s::ErrorCode::Cancelled,"Pre-write cancellation ignored");auto expired=owner->save(rows(1),{},std::chrono::steady_clock::now());check(!expired&&expired.error().code==s::ErrorCode::Cancelled,"Pre-write absolute expiry ignored");check(files(f.profile)==before&&bool(owner->validate()),"Cancelled pre-write operation changed filesystem or poisoned owner");
  auto invalid=rows(33);check(!owner->save(invalid)&&files(f.profile)==before,"Invalid oversized export began publication");
  put(f.helper/"dht.config","new independent port/nid properties");put(f.helper/"dest_key1.dat","unselected identity bytes");check(bool(owner->validate())&&bool(owner->save(rows(1))),"Independent DHT properties/sibling identity revoked cache owner");
  f.lease.reset();auto blocked=s::ProfileLease::acquire(f.profile);check(!blocked&&blocked.error().code==s::ErrorCode::Busy,"Retained cache released original OFD exclusion");check(bool(owner->validate()),"Cache borrowed caller lease lifetime");owner.reset();auto released=s::ProfileLease::acquire(f.profile);check(bool(released),"Last cache owner retained leaked lock");
}
}
int main(){try{allOriginalSelectorsAreIndependentAndRoundTrip();savingRecoveryAndAmbiguousInputsPreserveOriginalSelection();unsafeOriginalPathsAreNeverFollowed();externalFileParentKeyAndLockChangesAreRefused();cancellationRetainedLeaseAndIndependentProperties();std::cout<<"PASS five isolated original I2P contact-store families\n";return 0;}catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}}
