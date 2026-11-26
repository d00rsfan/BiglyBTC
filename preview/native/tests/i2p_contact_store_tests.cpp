// SPDX-License-Identifier: GPL-2.0-or-later
#include "contact_store.hpp"
#include "state_file_io.hpp"
#include "../platform/utf8_path.hpp"
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace s=biglybtc::storage;namespace i=biglybtc::services::i2p;namespace d=i::dht;
namespace fs=std::filesystem;
namespace io=s::detail;
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
  Fixture(){
#ifdef _WIN32
    const auto id = std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    root = fs::canonical(fs::temp_directory_path()) / ("biglybtc-i2p-contacts-" + id);
    check(fs::create_directory(root), "Cannot create generated isolated directory");
    profile = root / biglybtc::platform::pathFromUtf8("profile-δοκιμή");
#else
    std::array<char,64> name{};
    const std::string pattern="/tmp/biglybtc-i2p-contacts-XXXXXX";
    std::copy(pattern.begin(),pattern.end(),name.begin());
    const auto* made=::mkdtemp(name.data());
    check(made!=nullptr,"Cannot create generated isolated directory");
    root=fs::canonical(made);
    profile=root/"profile";
#endif
    helper=profile/"plugins/azneti2phelper";
    fs::create_directories(helper);
    lease.emplace(take(s::ProfileLease::acquire(profile)));
  }
  ~Fixture(){lease.reset();std::error_code error;fs::remove_all(root,error);}
  Fixture(const Fixture&)=delete;
  fs::path cache(unsigned index=0,bool secondary=false)const{return helper/("dhtdata"+std::string(index?"1":"")+(secondary?"_s":""));}
  fs::path key(unsigned index=0,bool secondary=false)const{return helper/("dest_key"+std::string(index?"1":"")+(secondary?"_s":"")+".dat");}
  std::shared_ptr<s::I2pContacts> open(unsigned index=0,bool secondary=false){return take(s::I2pContacts::open(*lease,index,secondary));}
};
std::map<std::string,std::string> files(const fs::path& root) {
  std::map<std::string,std::string> result;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
#ifdef _WIN32
    if (entry.path().filename() == ".azlock") { continue; }
#endif
    if(entry.is_regular_file()){result.emplace(biglybtc::platform::pathToUtf8(fs::relative(entry.path(),root)),bytes(entry.path()));}
  }
  return result;
}

bool symlink(const fs::path& target, const fs::path& path, bool directory = false) {
  std::error_code error;
  if (directory) { fs::create_directory_symlink(target, path, error); }
  else { fs::create_symlink(target, path, error); }
#ifdef _WIN32
  if (error.value() == ERROR_PRIVILEGE_NOT_HELD || error.value() == ERROR_CALL_NOT_IMPLEMENTED
      || error.value() == ERROR_NOT_SUPPORTED || error == std::errc::operation_not_permitted) {
    std::cout << "SKIP symlink creation unavailable on this Windows runner\n";
    return false;
  }
#endif
  check(!error, "Symlink setup failed");
  return true;
}

io::StateStamp stamp(const fs::path& file) {
  const auto directory = take(io::openStatePathDirectory(file.parent_path()));
  const auto stamp = take(io::stateStampAt(directory.get(), file.filename()));
  check(stamp.has_value(), "Missing generated file identity");
  return *stamp;
}
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
  {Fixture f;fs::create_directory(f.cache());put(f.cache()/"contacts.saving",encoded);const auto before=stamp(f.cache()/"contacts.saving");auto owner=f.open();const auto after=stamp(f.cache()/"contacts.dat");check(io::sameStateIdentity(before,after),"Recovery copied or replaced the held saving inode");check(!fs::exists(f.cache()/"contacts.saving")&&bytes(f.cache()/"contacts.dat")==encoded,"Recovery did not preserve original bytes");check(take(owner->snapshot())->writable,"Recovered cache unexpectedly read-only");}
  {Fixture f;fs::create_directory(f.cache());put(f.cache()/"contacts.dat",encoded);put(f.cache()/"contacts.saving","unknown incomplete pending input");const auto before=files(f.profile);auto owner=f.open();auto snapshot=take(owner->snapshot());check(!snapshot->writable&&wire(snapshot->contacts)==encoded,"Primary did not win ambiguous input selection");auto saved=owner->save(rows(1));check(!saved&&saved.error().code==s::ErrorCode::Unsupported,"Ambiguous input was made writable");check(files(f.profile)==before,"Ambiguous input was removed or rewritten");}
  for(const auto& malformed:std::array<std::string,3>{std::string{},std::string(4,'\xff'),std::string("bad")}){
    Fixture f;fs::create_directory(f.cache());put(f.cache()/"contacts.dat",malformed);put(f.cache()/"contacts.saving",encoded);const auto before=files(f.profile);auto failed=s::I2pContacts::open(*f.lease,0);check(!failed&&failed.error().code==s::ErrorCode::InvalidFormat,"Corrupt existing primary fell back to saving");check(files(f.profile)==before,"Malformed primary was rewritten");
  }
}
void unsafeOriginalPathsAreNeverFollowed(){
  for(const auto* name:{"contacts.dat","contacts.saving"}){for(unsigned kind=0;kind<4;++kind){
    Fixture f;fs::create_directory(f.cache());const auto path=f.cache()/name;put(f.root/"sentinel","do not change");
    if(kind==0){if(!symlink(f.root/"sentinel",path)){continue;}}
    else if(kind==1){fs::create_hard_link(f.root/"sentinel", path);}
    else if(kind==2){
#ifdef _WIN32
      continue;
#else
      check(::mkfifo(path.c_str(),0600)==0,"FIFO setup failed");
#endif
    }
    else{fs::create_directory(path);}
    check(!s::I2pContacts::open(*f.lease,0),"Unsafe contacts input accepted");check(bytes(f.root/"sentinel")=="do not change","Unsafe target was changed");check(fs::symlink_status(path).type()!=fs::file_type::not_found,"Unsafe input removed");
  }}
  for(unsigned level=0;level<3;++level){Fixture f;fs::create_directory(f.cache());const auto selected=level==0?f.profile/"plugins":level==1?f.helper:f.cache();const auto moved=f.root/"retained";fs::rename(selected,moved);if(!symlink(moved,selected,true)){continue;}check(!s::I2pContacts::open(*f.lease,0),"Symlinked plugin/helper/cache directory accepted");check(!fs::exists(moved/"contacts.dat"),"Symlinked directory mutated");}
  {Fixture f;fs::remove_all(f.profile/"plugins");check(!s::I2pContacts::open(*f.lease,0),"Missing plugin parent was invented");check(!fs::exists(f.profile/"plugins"),"Owner created plugin parents");}
}
void externalFileParentKeyAndLockChangesAreRefused(){
  for(unsigned kind=0;kind<3;++kind){Fixture f;auto owner=f.open();check(bool(owner->save(rows(1))),"Initial save failed");const auto path=f.cache()/"contacts.dat";
    if(kind==0){put(path,"external bytes");}else if(kind==1){fs::rename(path,f.cache()/"old.dat");put(path,wire(rows(1)));}else{fs::remove(path);}
    const auto before=files(f.profile);check(!owner->validate()&&!owner->snapshot()&&!owner->save(rows(2)),"Changed contact input was blessed");check(files(f.profile)==before&&!fs::exists(f.cache()/"contacts.saving"),"Changed contact input overwritten");
  }
  for(unsigned level=0;level<4;++level){Fixture f;auto owner=f.open();const auto path=level==0?f.cache():level==1?f.helper:level==2?f.profile/"plugins":f.profile;const auto moved=f.root/"old-parent";std::error_code status;fs::rename(path,moved,status);
#ifdef _WIN32
    if(status){check(status==std::errc::permission_denied||status.value()==ERROR_SHARING_VIOLATION,"Unexpected held-directory rename error");check(bool(owner->validate()),"Denied replacement changed the held directory");continue;}
#endif
    check(!status,"Cannot replace generated directory");fs::create_directory(path);const auto before=files(f.root);check(!owner->validate()&&!owner->save(rows(1)),"Replaced named parent accepted");check(files(f.root)==before,"Replaced parent caused orphan/publication write");}
  for(unsigned kind=0;kind<4;++kind){Fixture f;if(kind!=0){put(f.key(),"opaque original key marker");}auto owner=f.open();
    if(kind==0){put(f.key(),"new key marker");}else if(kind==1){put(f.key(),"changed key marker");}else if(kind==2){fs::rename(f.key(),f.helper/"old-key");put(f.key(),"opaque original key marker");}else{fs::remove(f.key());}
    const auto before=files(f.profile);check(!owner->validate()&&!owner->save(rows(1)),"Changed key presence/inode/stat accepted");check(files(f.profile)==before,"Key conflict altered original cache");
  }
  {Fixture f;auto owner=f.open();std::error_code status;fs::rename(f.profile/".azlock",f.profile/"old-lock",status);
#ifdef _WIN32
    if(status){check(status==std::errc::permission_denied||status.value()==ERROR_SHARING_VIOLATION,"Unexpected held-lock rename error");check(bool(owner->validate()),"Denied replacement changed the held lock");}
    else
#endif
    {check(!status,"Cannot replace generated lock");put(f.profile/".azlock","");const auto before=files(f.profile);check(!owner->save(rows(1)),"Replaced named OFD lock accepted");check(files(f.profile)==before,"Lock conflict wrote cache");}}
  {Fixture f;put(f.root/"target","opaque");if(symlink(f.root/"target",f.key())){check(!s::I2pContacts::open(*f.lease,0),"Symlinked key accepted");}}
  {Fixture f;put(f.root/"target","opaque");fs::create_hard_link(f.root/"target", f.key());check(!s::I2pContacts::open(*f.lease,0),"Hardlinked key accepted");}
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
