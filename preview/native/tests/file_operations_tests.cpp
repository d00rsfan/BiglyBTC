#include "file_operations.hpp"
#include "file_policy_commands.hpp"
#include "encoded_document.hpp"
#include "file_operation_io.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <dirent.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace core=biglybtc::core;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error>&& value){if(!value){throw std::runtime_error(value.error().message);}if constexpr(!std::is_void_v<T>){return std::move(*value);}}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
core::Document doc(std::string bytes){auto out=core::Document::parse(std::move(bytes),st::compatibleConfigLimits());check(out.has_value(),"parse fixture");return std::move(*out);}
void put(core::Document& d,std::string_view key,std::string value){check(d.replaceValue(key,std::move(value)).has_value(),"edit fixture");}
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);check(bool(input),"read fixture");return {std::istreambuf_iterator<char>(input),{}};}
void write(const fs::path& path,std::string_view bytes){fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(output),"write fixture");}
std::string sha1(std::string_view bytes){std::array<unsigned char,EVP_MAX_MD_SIZE> raw{};unsigned size=0;check(EVP_Digest(bytes.data(),bytes.size(),raw.data(),&size,EVP_sha1(),nullptr)==1&&size==20,"fixture sha1");return {reinterpret_cast<const char*>(raw.data()),size};}
struct Area{fs::path root;Area(){char path[]="/tmp/biglybtc-fileops-XXXXXX";const auto* made=::mkdtemp(path);check(made,"mkdtemp");root=made;}~Area(){std::error_code e;fs::remove_all(root,e);}};
struct Record{core::TorrentHash hash;std::string row,metadata;fs::path state,archive,save;std::vector<fs::path> files;std::vector<std::string> values;};
Record record(const fs::path& profile,const fs::path& save,std::string name,bool multi,std::string second="world!"){
  Record out;out.save=save;out.values=multi?std::vector<std::string>{"hello",std::move(second)}:std::vector<std::string>{"hello"};std::string all;for(const auto& v:out.values){all+=v;}std::string pieces;for(std::size_t i=0;i<all.size();i+=4){pieces+=sha1(std::string_view(all).substr(i,std::min<std::size_t>(4,all.size()-i)));}
  std::string info="d";if(multi){info+="5:filesl";for(std::size_t i=0;i<out.values.size();++i){const auto file=i?"second.bin":"first.bin";info+="d6:lengthi"+std::to_string(out.values[i].size())+"e4:pathl"+b(file)+"ee";out.files.push_back(save/name/file);}info+='e';}else{info+="6:lengthi5e";out.files.push_back(save/name);}info+="4:name"+b(name)+"12:piece lengthi4e6:pieces"+b(pieces)+'e';auto hash=core::TorrentHash::parse(sha1(info));check(hash.has_value(),"fixture hash");out.hash=*hash;
  auto state=doc("d4:info"+info+"e");std::string stores="l";for(std::size_t i=0;i<out.values.size();++i){stores+="1:L";}auto attrs=doc("de");put(attrs,"storetypes",stores+'e');put(attrs,"opaque.state","li7e3:rawe");put(attrs,"resumecomplete","i2e");put(state,"attributes",attrs.serialize());auto data=doc("de");put(data,"resume data",b(std::string(pieces.size()/20,'\1')));put(data,"valid","i1e");auto resume=doc("de");put(resume,"data",data.serialize());put(state,"resume",resume.serialize());out.metadata=state.serialize();out.state=profile/"active"/(out.hash.hex()+".dat");out.archive=profile.parent_path()/(name+".torrent");write(out.state,out.metadata);write(out.archive,out.metadata);
  for(std::size_t i=0;i<out.files.size();++i){write(out.files[i],out.values[i]);}auto row=doc("de");put(row,"torrent",b(out.archive.string()));put(row,"torrent_hash",b(sha1(info)));put(row,"save_dir",b(save.string()));put(row,"save_file",b(name));put(row,"state","i0e");put(row,"forceStart","i1e");put(row,"downloaded","i11e");put(row,"file_priorities",multi?"li1ei2ee":"li1ee");put(row,"opaque.row","d4:keep3:rawe");out.row=row.serialize();return out;
}
struct Fixture{Area area;fs::path profile,source,destination;Record selected;st::ProfileLease lease;
  explicit Fixture(bool multi=false,bool threePieceSecond=false):profile(area.root/"profile"),source(area.root/"source"),destination(area.root/"destination"),selected(record(profile,source,multi?"bundle":"file.bin",multi,threePieceSecond?"world!tail":"world!")),lease(take(st::ProfileLease::acquire(profile))){fs::create_directories(destination);inventory();}
  void inventory(std::string extra={}){write(profile/"downloads.config","d9:downloadsl"+selected.row+extra+"e6:opaque3:rawe");}
  st::PreparedFileOperation plan(st::FileOperationKind kind){return take(st::prepareFileOperation(lease,{selected.hash,kind,kind==st::FileOperationKind::Relocate||kind==st::FileOperationKind::Relink?std::optional<fs::path>(destination):std::nullopt}));}
};
// Actual source and destination bytes use real filesystem IO. Only the link
// syscall result is injected, to deterministically exercise the EXDEV copy path.
bool forceCopy=false;dev_t copyFromDev=0,copyToDev=0;ino_t copyFromIno=0,copyToIno=0;unsigned copyHits=0;
// Actual payload reads, identified by opened dev/inode, including fortified
// libc calls. Profile/journal reads are excluded; no synthetic byte estimates.
struct ReadCounter {dev_t device=0;ino_t inode=0;std::uint64_t bytes=0;};
std::array<ReadCounter,64> readCounters{};std::size_t watchedReads=0;
bool countingReads=false,readCounterFailed=false;
std::size_t watchReads(const fs::path& path){
  struct stat stamp{};check(::stat(path.c_str(),&stamp)==0&&watchedReads<readCounters.size(),"read counter fixture identity");
  const auto index=watchedReads++;readCounters[index]={stamp.st_dev,stamp.st_ino,0};return index;
}
void resetReads(){countingReads=false;readCounters={};watchedReads=0;readCounterFailed=false;}
void countRead(int fd,ssize_t count){
  if(!countingReads||count<=0){return;}struct stat held{};
  if(::fstat(fd,&held)!=0){readCounterFailed=true;return;}
  for(std::size_t i=0;i<watchedReads;++i){if(held.st_dev==readCounters[i].device&&held.st_ino==readCounters[i].inode){readCounters[i].bytes+=static_cast<std::uint64_t>(count);}}
}
std::uint64_t reads(std::size_t index){check(!readCounterFailed,"actual read accounting failed");return readCounters[index].bytes;}
st::FileOperationStage interruptAt=st::FileOperationStage::Completed;bool interruptArmed=false;
void interrupt(st::FileOperationStage stage){if(interruptArmed&&stage==interruptAt){interruptArmed=false;throw std::runtime_error("injected stage interruption");}}
std::string hashBytes(const core::TorrentHash& hash){const auto bytes=hash.bytes();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
std::string pauseRow(const core::TorrentHash& hash,bool force,std::int64_t owner){auto row=doc("de");put(row,"hash",b(hashBytes(hash)));put(row,"force",force?"i1e":"i0e");put(row,"owner","i"+std::to_string(owner)+'e');put(row,"opaque.pause","li7e3:rawe");return row.serialize();}
void keepAndDelete(){
  {Fixture f;const auto bytes=read(f.selected.files[0]),archive=read(f.selected.archive);auto p=f.plan(st::FileOperationKind::RemoveKeepData);check(p.preview().presentFiles==1&&p.preview().presentBytes==5,"keep preview facts");auto confirmation=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));auto result=take(st::applyFileOperation(f.lease,std::move(p),confirmation));check(result.filesDeleted==0&&read(f.selected.files[0])==bytes&&read(f.selected.archive)==archive,"keep preserves bytes/archive");check(!fs::exists(f.selected.state)&&!st::hasPendingFileOperation(f.lease),"active state removed last and journal gone");auto inventory=doc(read(f.profile/"downloads.config"));check(inventory.find("downloads")->children.empty()&&inventory.find("opaque")->bytes=="raw","selected row removed unknown preserved");}
  {Fixture f(true);write(f.source/"bundle/foreign.txt","foreign");auto p=f.plan(st::FileOperationKind::RemoveDeleteData);check(p.preview().presentFiles==2,"delete plan exact leaf count");check(!st::confirmFileOperation(p,p.preview().approvalScopeDigest,false),"delete requires explicit confirmation");check(fs::exists(f.selected.files[0]),"rejection no mutation");auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));auto r=take(st::applyFileOperation(f.lease,std::move(p),c));check(r.filesDeleted==2&&!fs::exists(f.selected.files[0])&&!fs::exists(f.selected.files[1])&&read(f.source/"bundle/foreign.txt")=="foreign"&&fs::exists(f.selected.archive),"delete touches only owned listed leaves");}
  // Mixed original pause encodings: remove only this torrent's dictionary and
  // legacy byte ownership, retaining exact survivor rows and opaque fields.
  for(const auto kind:{st::FileOperationKind::RemoveKeepData,st::FileOperationKind::RemoveDeleteData}){
    Fixture f;const auto other=record(f.profile,f.area.root/"other","survivor.bin",false);f.inventory(other.row);auto inventory=doc(read(f.profile/"downloads.config"));const auto survivors=b(hashBytes(other.hash))+pauseRow(other.hash,false,19);put(inventory,"pause_data","l"+pauseRow(f.selected.hash,true,7)+b(hashBytes(f.selected.hash))+survivors+'e');write(f.profile/"downloads.config",inventory.serialize());auto p=f.plan(kind);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));take(st::applyFileOperation(f.lease,std::move(p),c));auto after=doc(read(f.profile/"downloads.config"));check(core::encodeValue(*after.find("pause_data")).value()=="l"+survivors+'e',"remove mixed target pauses preserves exact survivor/opaque bytes");auto parsed=core::readLibrary(after);check(parsed&&parsed->downloads.size()==1&&parsed->downloads[0].hash==other.hash&&parsed->paused.size()==2&&std::ranges::all_of(parsed->paused,[&](const auto& row){return row.hash==other.hash;}),"removed target has no orphan pause in either encoding");check(after.find("opaque")->bytes=="raw"&&read(other.files[0])==other.values[0],"pause removal preserves inventory unknown and survivor data");
  }
  {
    Fixture f;const auto other=record(f.profile,f.area.root/"other","survivor.bin",false);f.inventory(other.row);auto inventory=doc(read(f.profile/"downloads.config"));const auto target=pauseRow(f.selected.hash,true,7)+b(hashBytes(f.selected.hash));put(inventory,"pause_data","l"+target+pauseRow(other.hash,false,19)+'e');write(f.profile/"downloads.config",inventory.serialize());auto p=f.plan(st::FileOperationKind::RemoveKeepData);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),c,interrupt)&&st::hasPendingFileOperation(f.lease),"pause recovery has durable confirmed journal before publication");
    auto changed=doc(read(f.profile/"downloads.config"));put(changed,"pause_data","l"+pauseRow(f.selected.hash,false,7)+b(hashBytes(f.selected.hash))+pauseRow(other.hash,false,19)+'e');write(f.profile/"downloads.config",changed.serialize());auto conflict=st::recoverFileOperation(f.lease);check(!conflict&&conflict.error().code==st::ErrorCode::Conflict&&st::hasPendingFileOperation(f.lease)&&fs::exists(f.selected.state)&&read(f.selected.files[0])=="hello","changed modern target pause refuses recovery before effects");
    const auto survivorPauses=b(hashBytes(other.hash))+pauseRow(other.hash,true,29);put(changed,"pause_data","l"+target+survivorPauses+'e');auto otherRow=doc(other.row);put(otherRow,"downloaded","i99e");put(changed,"downloads","l"+f.selected.row+otherRow.serialize()+'e');write(f.profile/"downloads.config",changed.serialize());auto recovered=take(st::recoverFileOperation(f.lease));check(recovered&&recovered->recovered&&!st::hasPendingFileOperation(f.lease),"sparse recovery removes selected mixed pauses");auto after=doc(read(f.profile/"downloads.config"));check(core::encodeValue(*after.find("pause_data")).value()=="l"+survivorPauses+'e',"recovery preserves independently changed survivor pause/opaque fields");auto parsed=core::readLibrary(after);check(parsed&&parsed->downloads.size()==1&&parsed->downloads[0].hash==other.hash&&parsed->downloads[0].downloaded==99&&parsed->paused.size()==2&&std::ranges::all_of(parsed->paused,[&](const auto& row){return row.hash==other.hash;}),"sparse recovery retains unrelated row checkpoint and no target ownership");check(after.find("opaque")->bytes=="raw"&&read(f.selected.files[0])=="hello"&&read(other.files[0])==other.values[0],"recovered Keep preserves both payloads and unknown root");
  }

}
void relocationRecoveryPreservesBothUntilPublication(){
  Fixture f(true);
  const std::string emptyGzip("\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\x4b\x49\x05\x00\x8b\x29\x90\x7d\x02\x00\x00\x00",22);
  auto cache=take(st::EncodedDocument::parse(emptyGzip).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"cache fixture"};}));
  auto original=doc(f.selected.metadata);auto attributes=doc(*core::encodeValue(*original.find("attributes")));put(attributes,"opaque.cache","li99e3:rawe");
  const auto key=f.selected.hash.bytes();const std::string hash(reinterpret_cast<const char*>(key.data()),key.size());
  check(cache.replaceValue("state","ld4:hash"+b(hash)+"10:attributes"+attributes.serialize()+"ee").has_value(),"cache target fixture");
  check(cache.replaceValue("unrelated.checkpoint","i1e").has_value(),"cache unknown fixture");
  write(f.profile/"active/cache.dat",*cache.serialize());auto p=f.plan(st::FileOperationKind::Relocate);const auto scope=p.preview().approvalScopeDigest;auto c=take(st::confirmFileOperation(p,scope,false));interruptAt=st::FileOperationStage::DestinationPublished;interruptArmed=true;auto failed=st::applyFileOperation(f.lease,std::move(p),c,interrupt);check(!failed&&st::hasPendingFileOperation(f.lease),"interrupted move retains journal");for(std::size_t i=0;i<f.selected.files.size();++i){check(read(f.selected.files[i])==f.selected.values[i],"old Java root remains complete before publication");}
  auto cacheNow=take(st::EncodedDocument::parse(read(f.profile/"active/cache.dat")).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"cache reload"};}));
  check(cacheNow.replaceValue("unrelated.checkpoint","i88e").has_value(),"unrelated cache checkpoint");write(f.profile/"active/cache.dat",*cacheNow.serialize());
  auto other=record(f.profile,f.area.root/"other","other.bin",false);auto inventory=doc(read(f.profile/"downloads.config"));auto rows=std::string("l");for(const auto& row:inventory.find("downloads")->children){rows+=*core::encodeValue(row,st::compatibleConfigLimits());}auto updatedOther=doc(other.row);put(updatedOther,"downloaded","i99e");rows+=updatedOther.serialize()+'e';put(inventory,"downloads",rows);write(f.profile/"downloads.config",inventory.serialize());
  auto recovered=take(st::recoverFileOperation(f.lease));check(recovered&&recovered->recovered&&recovered->filesMoved==2,"move recovery succeeds");for(std::size_t i=0;i<f.selected.files.size();++i){check(!fs::exists(f.selected.files[i])&&read(f.destination/f.selected.files[i].lexically_relative(f.source))==f.selected.values[i],"destination published before source removal");}
  auto afterCache=take(st::EncodedDocument::parse(read(f.profile/"active/cache.dat")).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"cache final"};}));
  check(afterCache.document().find("state")->children.empty()&&afterCache.document().find("unrelated.checkpoint")->integer==88,"sparse cache recovery keeps unrelated checkpoint");
  auto after=doc(read(f.profile/"downloads.config"));auto library=core::readLibrary(after);check(library&&library->downloads.size()==2&&library->downloads[0].saveDirectory==f.destination.string()&&library->downloads[1].downloaded==99,"sparse recovery keeps unrelated checkpoint");auto state=take(st::EncodedDocument::parse(read(f.selected.state)).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"state parse"};}));check(state.document().find("resume")->find("data")->find("valid")->integer==0&&state.document().find("attributes")->find("opaque.state")&&state.document().find("attributes")->find("opaque.cache"),"move preserves unknown state and requires recheck");
}
void relinkAndScope(){
  Fixture f;write(f.destination/"file.bin","other");auto p=f.plan(st::FileOperationKind::Relink);check(p.preview().requiresRecheck&&p.preview().presentFiles==1,"relink facts");auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),c));check(read(f.source/"file.bin")=="hello"&&read(f.destination/"file.bin")=="other","relink does not move/delete data");
  Fixture growing;write(growing.source/"file.bin","he");auto first=growing.plan(st::FileOperationKind::Relocate);const auto accepted=first.preview().approvalScopeDigest,oldCas=first.preview().executionCasDigest;write(growing.source/"file.bin","hello");auto inventory=doc(read(growing.profile/"downloads.config"));auto row=doc(growing.selected.row);put(row,"downloaded","i55e");put(inventory,"downloads","l"+row.serialize()+'e');write(growing.profile/"downloads.config",inventory.serialize());auto fresh=growing.plan(st::FileOperationKind::Relocate);check(fresh.preview().approvalScopeDigest==accepted&&fresh.preview().executionCasDigest!=oldCas,"scope tolerates same-inode growth/counter checkpoint");check(st::confirmFileOperation(fresh,accepted,false).has_value(),"fresh CAS accepts stable approved scope");std::stop_source stop;stop.request_stop();auto confirmation=take(st::confirmFileOperation(fresh,accepted,false));check(!st::applyFileOperation(growing.lease,std::move(fresh),confirmation,nullptr,stop.get_token())&&!st::hasPendingFileOperation(growing.lease),"cancel before journal leaves no mutation");
}
void protectionAndConflicts(){
  Fixture f;auto state=doc(read(f.selected.state));auto attrs=doc(*core::encodeValue(*state.find("attributes")));put(attrs,"flags","i64e");put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveDeleteData,{}}),"source0x40 flag forbids deletion");
  Fixture shared;auto inventory=doc(read(shared.profile/"downloads.config"));auto row=doc(shared.selected.row);put(row,"torrent_hash",b(std::string(20,'z')));put(inventory,"downloads","l"+shared.selected.row+row.serialize()+'e');write(shared.profile/"downloads.config",inventory.serialize());check(!st::prepareFileOperation(shared.lease,{shared.selected.hash,st::FileOperationKind::RemoveDeleteData,{}}),"overlapping library root rejects");
  Fixture unsafe;fs::remove(unsafe.selected.files[0]);fs::create_symlink(unsafe.selected.archive,unsafe.selected.files[0]);check(!st::prepareFileOperation(unsafe.lease,{unsafe.selected.hash,st::FileOperationKind::RemoveDeleteData,{}}),"payload symlink rejects without following");
  Fixture collision;write(collision.destination/"file.bin","foreign");check(!st::prepareFileOperation(collision.lease,{collision.selected.hash,st::FileOperationKind::Relocate,collision.destination}),"move never overwrites destination");
}

void attributes(Fixture& f,std::string stores,std::optional<std::string> links={},std::string suffix={}){
  auto state=doc(read(f.selected.state));auto attrs=doc(*core::encodeValue(*state.find("attributes")));put(attrs,"storetypes",std::move(stores));if(links){put(attrs,"filelinks2",*links);}if(!suffix.empty()){put(attrs,"incompfilesuffix",b(suffix));}put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
}
fs::path control(const Fixture& f,std::size_t index,bool reorder=false){return f.profile/"active"/f.selected.hash.hex()/("fmfile"+std::to_string(index)+".dat"+(reorder?".2":""));}
void nonlinearControlsAndRecovery(){
  // offset5, length10, piece4: three reordered slots may occupy3+2*4=11
  // physical bytes. These bytes are moved opaquely, not decoded as linear data.
  {
    Fixture f(true,true);attributes(f,"l1:L1:Re");fs::resize_file(f.selected.files[1],11);const std::string physical=std::string("world!tail")+std::string(1,'\0');const auto c=control(f,1,true);write(c,"opaque reordered control");write(fs::path(c.string()+".bak"),"prior control");write(c.parent_path()/"foreign.dat","foreign state");
    auto plan=f.plan(st::FileOperationKind::Relocate);check(plan.preview().files[1].logicalBytes==10&&plan.preview().files[1].actualBytes==11&&plan.preview().presentBytes==16&&plan.preview().presentFiles==2,"nonlinear preview separates logical/physical payload and excludes control leaves");auto confirmation=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));interruptAt=st::FileOperationStage::DestinationPublished;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(plan),confirmation,interrupt)&&st::hasPendingFileOperation(f.lease),"reorder move has durable recoverable publication");
    auto other=record(f.profile,f.area.root/"unrelated","other.bin",false);auto inv=doc(read(f.profile/"downloads.config"));auto changed=doc(other.row);put(changed,"downloaded","i97e");put(inv,"downloads","l"+f.selected.row+changed.serialize()+'e');write(f.profile/"downloads.config",inv.serialize());
    auto result=take(st::recoverFileOperation(f.lease));check(result&&result->filesMoved==2&&result->recovered&&read(f.destination/"bundle/second.bin")==physical&&!fs::exists(f.selected.files[1]),"reorder move recovery preserves complete physical bytes");check(read(c)=="opaque reordered control"&&read(fs::path(c.string()+".bak"))=="prior control"&&read(c.parent_path()/"foreign.dat")=="foreign state","profile-anchored controls and foreign state never relocate");auto library=take(core::readLibrary(doc(read(f.profile/"downloads.config"))).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"library"};}));check(library.downloads[1].downloaded==97,"nonlinear recovery keeps unrelated checkpoint");
  }
  {
    Fixture f;attributes(f,"l1:Ce");write(f.selected.files[0],"x");const auto c=control(f,0);write(c,"opaque compact control");write(fs::path(c.string()+".bak"),"prior compact");write(c.parent_path()/"foreign","untouched");auto p=f.plan(st::FileOperationKind::RemoveKeepData);auto accepted=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),accepted));check(read(f.selected.files[0])=="x"&&!fs::exists(c)&&!fs::exists(fs::path(c.string()+".bak"))&&read(c.parent_path()/"foreign")=="untouched","keep data removes only indexed compact control state, without recursive cleanup");
  }
  {
    Fixture f(true,true);attributes(f,"l1:L1:Xe");write(f.selected.files[1],"123456789AB");write(f.destination/"bundle/first.bin","hello");write(f.destination/"bundle/second.bin","abcdefghijk");const auto c=control(f,1,true);write(c,"opaque X control");auto p=f.plan(st::FileOperationKind::Relink);auto confirmation=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),confirmation));check(read(c)=="opaque X control"&&read(f.selected.files[1])=="123456789AB"&&read(f.destination/"bundle/second.bin")=="abcdefghijk","relink preserves profile control and both physical trees");auto remove=take(st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveDeleteData,{}}));auto approved=take(st::confirmFileOperation(remove,remove.preview().approvalScopeDigest,true));take(st::applyFileOperation(f.lease,std::move(remove),approved));check(!fs::exists(c)&&!fs::exists(f.destination/"bundle/second.bin")&&read(f.selected.files[1])=="123456789AB","delete follows current nonlinear mapping and removes known control only");
  }
}
void nonlinearBoundaryRefusals(){
  {Fixture f(true);attributes(f,"l1:L1:Re");write(f.selected.files[1],"1234567");check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveDeleteData,{}})&&read(f.selected.files[1])=="1234567"&&!st::hasPendingFileOperation(f.lease),"two-piece reorder remains linear and rejects physical bytes beyond logical length");}

  for(const auto intent:{"ld5:indexi0eee","i1e"}){Fixture f;auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));put(attrs,"BiglyBTC.compactConversion",intent);put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());const auto before=read(f.selected.state);check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}})&&read(f.selected.state)==before&&read(f.selected.files[0])=="hello"&&!st::hasPendingFileOperation(f.lease),"pending or malformed compact-conversion intent refuses before effects");}

  {Fixture f(true,true);attributes(f,"l1:L1:Re");write(f.selected.files[1],"123456789ABC");check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveDeleteData,{}})&&read(f.selected.files[1])=="123456789ABC"&&!st::hasPendingFileOperation(f.lease),"reordered bytes above exact geometry bound refuse before mutation");}
  {Fixture f;attributes(f,"l1:Ce");write(f.selected.files[0],"x");const auto c=control(f,0);write(fs::path(c.string()+".saving"),"pending");check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}})&&read(f.selected.files[0])=="x","unresolved control saving refuses even Keep");}
  {Fixture f;const auto c=control(f,0);fs::create_directories(c.parent_path());fs::create_symlink(f.selected.archive,c);check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}})&&fs::exists(f.selected.state),"control symlink is never followed/deleted");}
  {Fixture f;attributes(f,"l1:Re");const auto c=control(f,0,true);write(c,"before");auto p=f.plan(st::FileOperationKind::RemoveDeleteData);auto confirmation=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));write(c,"changed");auto result=st::applyFileOperation(f.lease,std::move(p),confirmation);check(!result&&result.error().code==st::ErrorCode::Conflict&&!st::hasPendingFileOperation(f.lease)&&read(f.selected.files[0])=="hello"&&read(c)=="changed","control CAS refuses changed state before any payload deletion");}
}
void authenticatedExternalLinks(){
// Insert both blocks inside authenticatedExternalLinks() in file_operations_tests.cpp.
  {
    Fixture f; const auto external=f.area.root/"external/negative"; write(external,"hello");
    const auto original=f.selected.files[0].string();
    attributes(f,"l1:Le","l"+b("0\n/stale-indexed-from\n"+(f.area.root/"unused").string())
        +b("-1\n"+original+"\n"+external.string())
        +b("-1\n"+external.string()+"\n"+(f.area.root/"not-authorized").string())+'e');
    auto plan=f.plan(st::FileOperationKind::RemoveKeepData);
    check(plan.preview().files.size()==1&&plan.preview().files[0].source==external,
        "file-operation preview shares exact original-base negative migration, not chained links");
    auto confirmation=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));
    take(st::applyFileOperation(f.lease,std::move(plan),confirmation));
    check(read(external)=="hello"&&read(f.selected.files[0])=="hello"&&!fs::exists(f.selected.state)
        &&!fs::exists(f.area.root/"not-authorized"),
        "RemoveKeep uses authenticated migrated source and preserves original and external payload bytes");
  }
  {
    Fixture f; const auto forbidden=f.profile/"not-payload"; write(forbidden,"hello");
    attributes(f,"l1:Le","l"+b("-1\n"+f.selected.files[0].string()+"\n"+forbidden.string())+'e');
    const auto state=read(f.selected.state),inventory=read(f.profile/"downloads.config");
    check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}})
        &&read(forbidden)=="hello"&&read(f.selected.state)==state&&read(f.profile/"downloads.config")==inventory
        &&!st::hasPendingFileOperation(f.lease),"negative index never bypasses held-profile exclusion or mutates on failed authority");
  }

  {Fixture f;auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));put(attrs,"filelinks","l3:rawe");put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());auto p=f.plan(st::FileOperationKind::Relocate);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),c));auto after=doc(read(f.selected.state));check(core::encodeValue(*after.find("attributes")->find("filelinks")).value()=="l3:rawe"&&read(f.destination/"file.bin")=="hello","deprecated filelinks remains opaque and never overrides actual base/filelinks2 mapping");}

  for(const auto kind:{st::FileOperationKind::RemoveKeepData,st::FileOperationKind::RemoveDeleteData}){
    Fixture f;const auto external=f.area.root/"external/actual";write(external,"hello");attributes(f,"l1:Le","l"+b("0\n"+f.selected.files[0].string()+"\n"+external.string())+'e');auto p=f.plan(kind);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));take(st::applyFileOperation(f.lease,std::move(p),c));check(fs::exists(external)==(kind==st::FileOperationKind::RemoveKeepData)&&read(f.selected.files[0])=="hello","Keep/Delete target only authenticated external leaf, not abandoned logical base");
  }
  {
    Fixture f;const auto external=f.area.root/"external/actual";write(external,"hello");write(f.destination/"file.bin","there");attributes(f,"l1:Le","l"+b("0\n"+f.selected.files[0].string()+"\n"+external.string())+'e');auto p=f.plan(st::FileOperationKind::Relink);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),c));check(read(external)=="hello"&&read(f.destination/"file.bin")=="there","external relink leaves old actual leaf intact and binds normal destination");
  }

  {
    Fixture f;const auto external=f.area.root/"external/renamed.az!";write(external,"hello");fs::remove(f.selected.files[0]);const auto links="l"+b("0\n"+f.selected.files[0].string()+"\n"+external.string())+'e';attributes(f,"l1:Le",links,".az!");auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));put(attrs,"dnd_sf",b("not-scanned"));put(attrs,"dnd_pfx",b("raw-prefix"));put(attrs,"dnd_al",b((f.area.root/"not-created").string()));put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());auto p=f.plan(st::FileOperationKind::Relocate);check(p.preview().files[0].source==external&&p.preview().files[0].destination==f.destination/"file.bin.az!","external source approval maps to normal destination plus incomplete suffix");auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));interruptAt=st::FileOperationStage::DestinationPublished;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),c,interrupt),"external relocation interruption retained");auto result=take(st::recoverFileOperation(f.lease));check(result&&result->filesMoved==1&&!fs::exists(external)&&read(f.destination/"file.bin.az!")=="hello","external exact source recovery uses no parent traversal");auto movedState=doc(read(f.selected.state));check(movedState.find("attributes")->find("dnd_sf")->bytes=="not-scanned"&&movedState.find("attributes")->find("dnd_pfx")->bytes=="raw-prefix"&&!fs::exists(f.area.root/"not-created"),"DND policies preserved opaquely, never guessed as extra payload roots");
  }
  {
    Fixture f;const auto external=f.area.root/"external/one";write(external,"hello");attributes(f,"l1:Le","l"+b("0\n"+f.selected.files[0].string()+"\n"+external.string())+'e');auto other=record(f.profile,f.area.root/"other","other.bin",false);auto state=doc(read(other.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));put(attrs,"filelinks2","l"+b("0\n"+other.files[0].string()+"\n"+external.string())+'e');put(state,"attributes",attrs.serialize());write(other.state,state.serialize());f.inventory(other.row);check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveDeleteData,{}})&&read(external)=="hello","another torrent explicit link shares source and blocks destructive operation");
  }
  for(const auto scope:{"profile","git","symlink"}){
    Fixture f;fs::path external;if(std::string_view(scope)=="profile"){external=f.profile/"unrelated-data";write(external,"hello");}else if(std::string_view(scope)=="git"){external=f.area.root/".git/owned";write(external,"hello");}else{const auto real=f.area.root/"real";write(real/"owned","hello");fs::create_directory_symlink(real,f.area.root/"alias");external=f.area.root/"alias/owned";}attributes(f,"l1:Le","l"+b("0\n"+f.selected.files[0].string()+"\n"+external.string())+'e');check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}})&&fs::exists(f.selected.state),"external profile/git/symlink boundary refuses even non-mutating Keep preparation");
  }
}

void legacyRootsAndDefaultStoreRecovery(){
  for(const bool dictionary:{false,true}) {
    Fixture f;auto row=doc(f.selected.row);std::string legacy="d";
    for(std::size_t i=0;i<row.root().children.size();i+=2){const auto& key=row.root().children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}legacy+=*core::encodeValue(key)+*core::encodeValue(row.root().children[i+1]);}
    legacy+=b("path")+b(f.selected.files[0].string())+'e';
    write(f.profile/"downloads.config",dictionary?"d"+b("legacy.download")+legacy+b("opaque")+b("raw")+'e':"d9:downloadsl"+legacy+"e6:opaque3:rawe");
    auto state=doc(read(f.selected.state));auto attrs=doc("d12:opaque.stateli7e3:rawee");put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
    const auto inventoryBefore=read(f.profile/"downloads.config"),stateBefore=read(f.selected.state);auto p=f.plan(st::FileOperationKind::Relocate);check(read(f.profile/"downloads.config")==inventoryBefore&&read(f.selected.state)==stateBefore,"legacy/default prepare stays read-only");check(p.preview().sourceRoot==f.selected.files[0]&&p.preview().files[0].source==f.selected.files[0],"legacy exact root in preview");auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));interruptAt=st::FileOperationStage::DestinationPublished;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),c,interrupt)&&st::hasPendingFileOperation(f.lease),"legacy move durable before source removal");
    write(f.profile/"biglybt.config","d27:Enable reorder storage modei1e27:Reorder storage mode min MBi0ee");
    const auto result=take(st::recoverFileOperation(f.lease));check(result&&result->filesMoved==1&&read(f.destination/"file.bin")=="hello"&&!fs::exists(f.selected.files[0]),"recovery uses captured physical policy rather than changed config");auto inventory=doc(read(f.profile/"downloads.config"));auto library=core::readLibrary(inventory);check(library&&library->downloads.size()==1&&library->legacyMap==dictionary&&library->downloads[0].saveDirectory==f.destination.string()&&library->downloads[0].saveFile=="file.bin"&&inventory.find("opaque")->bytes=="raw","target migration retains inventory form and opaque data");
    const auto current=doc(read(f.selected.state));check(current.find("attributes")->find("storetypes")->children[0].bytes=="L"&&current.find("attributes")->find("opaque.state"),"move writes captured desired defaults and preserves unknown attributes");
  }
  {Fixture f;auto row=doc(f.selected.row);std::string legacy="d";for(std::size_t i=0;i<row.root().children.size();i+=2){const auto& key=row.root().children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}legacy+=*core::encodeValue(key)+*core::encodeValue(row.root().children[i+1]);}legacy+=b("save_dir")+b(f.source.string())+'e';write(f.profile/"downloads.config","d"+b("old")+legacy+b("opaque")+b("raw")+'e');auto p=f.plan(st::FileOperationKind::RemoveKeepData);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),c));auto after=doc(read(f.profile/"downloads.config"));check(!after.find("old")&&after.find("opaque")->bytes=="raw"&&read(f.selected.files[0])=="hello","legacy dictionary keep removes only selected row and retains data");}
}

void pendingReorderedCompletionGuards(){
  const auto redo=[](std::string_view field){return "d"+b("BiglyBTC.reorderedCompletion")+std::string(field)+'e';};
  const std::vector<std::string> unsafe={redo("d7:versioni1ee"),redo("i0e"),redo("le"),"d"+b("BiglyBTC.reorderedCompletion")};
  for(const auto kind:{st::FileOperationKind::RemoveKeepData,st::FileOperationKind::RemoveDeleteData,st::FileOperationKind::Relocate,st::FileOperationKind::Relink}){
    for(const bool backup:{false,true}){for(const auto& raw:unsafe){Fixture f;attributes(f,"l1:Re");if(kind==st::FileOperationKind::Relink){write(f.destination/"file.bin","hello");}auto path=control(f,0,true);if(backup){path+=std::string(".bak");}write(path,raw);const auto inventory=read(f.profile/"downloads.config"),state=read(f.selected.state);auto result=st::prepareFileOperation(f.lease,{f.selected.hash,kind,kind==st::FileOperationKind::Relocate||kind==st::FileOperationKind::Relink?std::optional<fs::path>(f.destination):std::nullopt});check(!result&&result.error().code==st::ErrorCode::Conflict&&result.error().message.find("reordered completion")!=std::string::npos,"native redo in authoritative primary/backup refuses all operation kinds");check(read(f.selected.files[0])=="hello"&&read(path)==raw&&read(f.profile/"downloads.config")==inventory&&read(f.selected.state)==state&&!st::hasPendingFileOperation(f.lease),"redo preparation refuses before payload/document/journal effects");}}
  }
  const auto cleared=[](){std::string map(4,'\0');map+=std::string(4,static_cast<char>(0xff));return "d3:leni5e4:nexti1e6:pieces"+b(map)+b("BiglyBTC.reorderedCompletion")+"dee";};
  {Fixture f;attributes(f,"l1:Re");const auto c=control(f,0,true);write(c,cleared());write(c.string()+".bak",redo("d7:versioni1ee"));auto p=f.plan(st::FileOperationKind::RemoveKeepData);const auto accept=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),accept));check(read(f.selected.files[0])=="hello"&&!fs::exists(c)&&!fs::exists(c.string()+".bak"),"valid cleared primary permits obsolete pending backup and exact cleanup");}
  {Fixture f;attributes(f,"l1:Re");const auto c=control(f,0,true);write(c,"opaque malformed primary");write(c.string()+".bak",redo("d7:versioni1ee"));auto result=st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}});check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[0])=="hello","malformed primary cannot authorize pending backup transport");}
  {Fixture f(true,true);attributes(f,"l1:L1:Re");const auto c=control(f,1,true);std::string map(8,'\0');map+=std::string(4,static_cast<char>(0xff));write(c,"d3:leni10e4:nexti1e6:pieces"+b(map)+b("BiglyBTC.reorderedCompletion")+"dee");write(c.string()+".bak",redo("d7:versioni1ee"));auto result=st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}});check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[1])=="world!tail","invalid duplicate map cannot supersede pending backup even with cleared native key");}
  for(const bool recovery:{false,true}){Fixture f;attributes(f,"l1:Re");const auto c=control(f,0,true);write(c,cleared());auto p=f.plan(st::FileOperationKind::RemoveDeleteData);const auto accept=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));
    if(recovery){interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),accept,interrupt)&&st::hasPendingFileOperation(f.lease),"accepted pre-effect removal journal retained");write(c,redo("d7:versioni1ee"));auto result=st::recoverFileOperation(f.lease);check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[0])=="hello"&&fs::exists(f.selected.state)&&st::hasPendingFileOperation(f.lease),"recovery independently refuses newly pending native redo before effects");}
    else{write(c,redo("d7:versioni1ee"));auto result=st::applyFileOperation(f.lease,std::move(p),accept);check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[0])=="hello"&&!st::hasPendingFileOperation(f.lease),"apply revalidates redo before creating operation journal");}
  }
}

void selectionPoliciesMoveAndRecover(){
  const auto setDnd=[](Fixture& f){
    auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));
    put(attrs,"dnd_sf",b("dnd"));put(attrs,"dnd_pfx",b("skip_"));put(attrs,"incompfilesuffix",b(".part"));
    put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
  };
  {
    Fixture f;setDnd(f);const auto old=f.selected.files[0],dnd=old.parent_path()/"dnd/skip_file.bin.part";
    take(st::setFilePolicies(f.lease,f.selected.hash,std::vector<biglybtc::compat::FilePolicy>{{true,0}}));
    check(!fs::exists(old)&&read(dnd)=="hello"&&!st::hasPendingFileOperation(f.lease),"Skip moves authenticated incomplete bytes into original DND subfolder");
    auto state=doc(read(f.selected.state));const auto* attrs=state.find("attributes");
    check(attrs->find("storetypes")->children[0].bytes=="L"&&attrs->find("opaque.state")
        &&attrs->find("filelinks2")->children[0].bytes.ends_with(dnd.string()),"Plain skip retains linear storage, unknown attributes and exact physical link");
    take(st::setFilePolicies(f.lease,f.selected.hash,std::vector<biglybtc::compat::FilePolicy>{{false,0}}));
    const auto restored=old.parent_path()/"skip_file.bin.part";
    check(!fs::exists(dnd)&&read(restored)=="hello"&&!fs::exists(old),"Unskip restores parent while retaining name; completion alone removes DND prefix/suffix");
  }
  {
    Fixture f(true);auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));
    const auto alternate=f.area.root/"alternate";put(attrs,"dnd_al",b(alternate.string()));put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
    const auto first=read(f.selected.files[0]),second=read(f.selected.files[1]);
    take(st::setFilePolicies(f.lease,f.selected.hash,std::vector<biglybtc::compat::FilePolicy>{{false,0},{true,0}}));
    const auto target=alternate/f.selected.files[1].filename();
    check(read(f.selected.files[0])==first&&read(target)==second&&!fs::exists(f.selected.files[1]),"Alternate DND moves only selected actual file, preserving sibling payload");
    take(st::setFilePolicies(f.lease,f.selected.hash,std::vector<biglybtc::compat::FilePolicy>{{false,0},{false,0}}));
    check(read(f.selected.files[1])==second&&!fs::exists(target),"Unskip restores alternate DND payload using exact original file parent");
  }
  for(const auto point:{st::FileOperationStage::DestinationPublished,st::FileOperationStage::StatePublished,
      st::FileOperationStage::InventoryPublished,st::FileOperationStage::SourceRemoved}){
    Fixture f;setDnd(f);auto other=record(f.profile,f.area.root/"other","survivor.bin",false);f.inventory(other.row);
    auto inventory=doc(read(f.profile/"downloads.config"));const auto original=*core::encodeValue(inventory.find("downloads")->children[0]);
    auto p=take(st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::SetFilePolicies,{},"d1:01:0e",original}));
    auto confirmation=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));
    interruptAt=point;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),confirmation,interrupt)
        &&st::hasPendingFileOperation(f.lease),"Selection interruption keeps exact existing recovery journal");
    inventory=doc(read(f.profile/"downloads.config"));auto survivor=doc(*core::encodeValue(inventory.find("downloads")->children[1]));
    put(survivor,"downloaded","i97e");put(inventory,"downloads","l"+*core::encodeValue(inventory.find("downloads")->children[0])+survivor.serialize()+'e');
    write(f.profile/"downloads.config",inventory.serialize());
    auto recovered=take(st::recoverFileOperation(f.lease));const auto target=f.selected.files[0].parent_path()/"dnd/skip_file.bin.part";
    check(recovered&&recovered->kind==st::FileOperationKind::SetFilePolicies&&read(target)=="hello"
        &&!fs::exists(f.selected.files[0])&&!st::hasPendingFileOperation(f.lease),"DND recovery settles exact moved bytes without duplicate movement");
    const auto after=doc(read(f.profile/"downloads.config"));auto selected=biglybtc::compat::originalFilePolicy(after.find("downloads")->children[0],1);
    check(selected&&selected->front().skipped&&after.find("downloads")->children[1].find("downloaded")->integer==97
        &&read(other.files[0])==other.values[0],"Selection sparse recovery preserves unrelated checkpoint and survivor payload");
  }
}

void selectionCollisionAndStaleSource(){
  {
    Fixture f;attributes(f,"l1:Ce");write(f.selected.files[0],"x");const auto marker=control(f,0);write(marker,"d7:versioni0e6:lengthi5ee");
    auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));put(attrs,"dnd_sf",b("dnd"));put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
    const auto before=read(f.selected.state),raw=read(f.profile/"downloads.config");auto inventory=doc(raw);
    auto p=take(st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::SetFilePolicies,{},"d1:01:0e",*core::encodeValue(inventory.find("downloads")->children[0])}));
    auto accepted=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));interruptAt=st::FileOperationStage::DestinationPublished;interruptArmed=true;
    check(!st::applyFileOperation(f.lease,std::move(p),accepted,interrupt),"nonlinear selection interrupted with control inventory retained");
    check(fs::remove(marker),"owned control removed to test recovery conflict");auto recovered=st::recoverFileOperation(f.lease);
    check(!recovered&&recovered.error().code==st::ErrorCode::Conflict&&st::hasPendingFileOperation(f.lease)
        &&read(f.selected.state)==before&&read(f.profile/"downloads.config")==raw&&read(f.selected.files[0])=="x",
        "Selection recovery never uses removal's absent-control shortcut");
  }
  {
    Fixture f;auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));
    put(attrs,"dnd_sf",b("dnd"));put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
    const auto target=f.selected.files[0].parent_path()/"dnd/file.bin";write(target,"other");
    take(st::setFilePolicies(f.lease,f.selected.hash,std::vector<biglybtc::compat::FilePolicy>{{true,0}}));
    check(read(target)=="other"&&read(f.selected.files[0])=="hello","Occupied DND destination is never overwritten; original source stays put");
  }
  {
    Fixture f;auto inventory=doc(read(f.profile/"downloads.config"));const auto original=*core::encodeValue(inventory.find("downloads")->children[0]);
    auto changed=doc(original);put(changed,"position","i9e");put(inventory,"downloads","l"+changed.serialize()+'e');write(f.profile/"downloads.config",inventory.serialize());
    const auto before=read(f.selected.state),raw=read(f.profile/"downloads.config");
    auto result=st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::SetFilePolicies,{},"d1:01:0e",original});
    check(!result&&result.error().code==st::ErrorCode::Conflict&&read(f.selected.state)==before
        &&read(f.profile/"downloads.config")==raw&&read(f.selected.files[0])=="hello"&&!st::hasPendingFileOperation(f.lease),
        "Stale selected-row source rejects before state, policy or payload changes");
  }
  {
    Fixture f;auto state=doc(read(f.selected.state)),attrs=doc(*core::encodeValue(*state.find("attributes")));
    put(attrs,"dnd_sf",b("../outside"));put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
    const auto before=read(f.selected.state),raw=read(f.profile/"downloads.config");
    auto result=st::setFilePolicies(f.lease,f.selected.hash,std::vector<biglybtc::compat::FilePolicy>{{true,0}});
    check(!result&&read(f.selected.state)==before&&read(f.profile/"downloads.config")==raw&&read(f.selected.files[0])=="hello"
        &&!st::hasPendingFileOperation(f.lease),"DND path traversal never becomes relocation authority");
  }
}

st::PreparedFileOperation archivePlan(Fixture& f,st::FileOperationKind kind){
  st::FileOperationRequest request{f.selected.hash,kind,{}};request.deleteTorrentFile=true;
  return take(st::prepareFileOperation(f.lease,request));
}
void archiveControlsAndRecovery(){
  {
    Fixture f;check(take(st::torrentArchiveDeletionPreference(f.lease)),"original archive preference defaults true without materializing config");
    check(!fs::exists(f.profile/"biglybt.config"),"archive preference read is sparse/read-only");
    for(const std::int64_t value:{0LL,1LL,2LL,-1LL,4294967296LL,-4294967296LL}){auto config=doc("de");put(config,"def.deletetorrent","i"+std::to_string(value)+'e');put(config,"opaque.preference","li7e3:rawe");write(f.profile/"biglybt.config",config.serialize());
      const auto before=read(f.profile/"biglybt.config");check(take(st::torrentArchiveDeletionPreference(f.lease))==(static_cast<std::uint32_t>(value)!=0)&&read(f.profile/"biglybt.config")==before,"original Boolean preference uses exact Long.intValue nonzero semantics");}
    auto config=doc("de");put(config,"def.deletetorrent","3:bad");write(f.profile/"biglybt.config",config.serialize());
    check(!st::torrentArchiveDeletionPreference(f.lease)&&read(f.profile/"biglybt.config")==config.serialize(),"malformed preference remains explicit and unmodified");
  }
  for(const auto kind:{st::FileOperationKind::RemoveKeepData,st::FileOperationKind::RemoveDeleteData}){
    Fixture f;const auto backup=fs::path(f.selected.archive.string()+".bak");write(backup,f.selected.metadata);const auto archive=read(f.selected.archive),payload=read(f.selected.files[0]);auto p=archivePlan(f,kind);
    check(p.preview().torrentArchive&&p.preview().torrentArchive->path==f.selected.archive&&p.preview().torrentArchive->present&&!p.preview().torrentArchive->internalState,"review exposes exact separate archive metadata");
    check(!st::confirmFileOperation(p,p.preview().approvalScopeDigest,true),"archive deletion requires its own reviewed confirmation even with payload permission");
    const auto confirmation=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,kind==st::FileOperationKind::RemoveDeleteData,true));
    const auto result=take(st::applyFileOperation(f.lease,std::move(p),confirmation));
    check(result.torrentArchiveDeleted&&!fs::exists(f.selected.archive)&&!fs::exists(backup)&&!fs::exists(f.selected.state)&&!st::hasPendingFileOperation(f.lease),"requested archive deletion follows durable state cleanup");
    check(kind==st::FileOperationKind::RemoveDeleteData?!fs::exists(f.selected.files[0]):read(f.selected.files[0])==payload,"archive choice is independent of payload removal choice");
    check(!archive.empty(),"archive fixture contains actual original metadata");
  }
  {
    Fixture f;auto first=archivePlan(f,st::FileOperationKind::RemoveKeepData);const auto scope=first.preview().approvalScopeDigest;
    auto changed=doc(read(f.selected.archive));put(changed,"opaque.archive","7:changed");write(f.selected.archive,changed.serialize());
    const auto confirmation=take(st::confirmFileOperation(first,scope,false,true));const auto inventory=read(f.profile/"downloads.config");
    const auto rejected=st::applyFileOperation(f.lease,std::move(first),confirmation);
    check(!rejected&&read(f.profile/"downloads.config")==inventory&&fs::exists(f.selected.state)&&!st::hasPendingFileOperation(f.lease),"archive raw/stat CAS refuses changed opaque metadata before effects");
    auto fresh=archivePlan(f,st::FileOperationKind::RemoveKeepData);check(fresh.preview().approvalScopeDigest!=scope&&!st::confirmFileOperation(fresh,scope,false,true),"archive raw bytes belong to reviewed approval, not just infohash");
  }
  {
    Fixture f;fs::remove(f.selected.archive);auto p=archivePlan(f,st::FileOperationKind::RemoveKeepData);
    check(p.preview().torrentArchive&&!p.preview().torrentArchive->present,"missing saved archive is explicitly reviewed as absent");
    const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false,true));write(f.selected.archive,f.selected.metadata);
    check(!st::applyFileOperation(f.lease,std::move(p),c)&&fs::exists(f.selected.state)&&fs::exists(f.selected.archive)&&!st::hasPendingFileOperation(f.lease),"new archive cannot be deleted under missing-file approval");
  }
  {
    Fixture f;const auto other=record(f.profile,f.area.root/"other","survivor.bin",false);auto row=doc(other.row);put(row,"torrent",b(f.selected.archive.string()));f.inventory(row.serialize());
    st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}};request.deleteTorrentFile=true;
    check(!st::prepareFileOperation(f.lease,request)&&fs::exists(f.selected.archive)&&fs::exists(other.state)&&!st::hasPendingFileOperation(f.lease),"shared library archive reference refuses before deletion");
  }
  for(const bool recover:{false,true}){
    Fixture f;const auto other=record(f.profile,f.area.root/"other","survivor.bin",false);
    const auto protectedBackup=fs::path(other.archive.string()+".bak");write(protectedBackup,f.selected.metadata);
    auto selectedRow=doc(f.selected.row);put(selectedRow,"torrent",b(protectedBackup.string()));
    f.selected.row=selectedRow.serialize();f.selected.archive=protectedBackup;
    if(!recover){
      f.inventory(other.row);const auto before=read(f.profile/"downloads.config");
      st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}};request.deleteTorrentFile=true;
      const auto rejected=st::prepareFileOperation(f.lease,request);
      check(!rejected&&rejected.error().code==st::ErrorCode::Conflict&&rejected.error().message=="Torrent archive is referenced by another library item"
          &&read(f.profile/"downloads.config")==before&&read(protectedBackup)==f.selected.metadata
          &&fs::exists(f.selected.state)&&fs::exists(other.state)&&!st::hasPendingFileOperation(f.lease),"another library archive backup is protected at preview before any effects");
    }else{
      f.inventory();auto p=archivePlan(f,st::FileOperationKind::RemoveKeepData);
      const auto confirmation=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false,true));
      interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;
      check(!st::applyFileOperation(f.lease,std::move(p),confirmation,interrupt)&&!interruptArmed&&st::hasPendingFileOperation(f.lease),"backup-reference recovery has an actual durable pre-effect journal");
      f.inventory(other.row);const auto before=read(f.profile/"downloads.config");
      const auto rejected=st::recoverFileOperation(f.lease);
      check(!rejected&&rejected.error().code==st::ErrorCode::Conflict&&rejected.error().message=="Torrent archive is referenced by another library item"
          &&read(f.profile/"downloads.config")==before&&read(protectedBackup)==f.selected.metadata
          &&fs::exists(f.selected.state)&&fs::exists(other.state)&&st::hasPendingFileOperation(f.lease),"recovery rechecks newly added backup references before sparse publication or archive deletion");
    }
    check(read(f.selected.files[0])=="hello"&&read(other.files[0])==other.values[0]&&read(other.archive)==other.metadata,"backup reference refusal preserves selected and survivor payload/archive bytes");
  }
  {
    Fixture f;auto p=archivePlan(f,st::FileOperationKind::RemoveKeepData);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false,true));
    const auto backup=fs::path(f.selected.archive.string()+".bak");write(backup,f.selected.metadata);
    check(!st::applyFileOperation(f.lease,std::move(p),c)&&fs::exists(f.selected.archive)&&fs::exists(backup)&&fs::exists(f.selected.state)&&!st::hasPendingFileOperation(f.lease),"previously absent backup cannot be deleted without a new review");
  }
  {
    Fixture f;const auto other=record(f.profile,f.area.root/"other","different.bin",false);write(f.selected.archive,other.metadata);
    st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}};request.deleteTorrentFile=true;
    check(!st::prepareFileOperation(f.lease,request)&&fs::exists(f.selected.state)&&!st::hasPendingFileOperation(f.lease),"archive must match actual selected full info identity");
  }
  for(const bool link:{false,true}){
    Fixture f;const auto alias=f.area.root/"alias";if(link){fs::create_hard_link(f.selected.archive,alias);}else{fs::rename(f.selected.archive,alias);fs::create_symlink(alias,f.selected.archive);}
    st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}};request.deleteTorrentFile=true;
    check(!st::prepareFileOperation(f.lease,request)&&fs::exists(f.selected.state)&&fs::exists(alias),"archive symlinks and hardlinks do not grant delete authority");
  }
  {
    Fixture f;auto row=doc(f.selected.row);put(row,"torrent",b(f.selected.state.string()));f.selected.row=row.serialize();f.inventory();
    auto p=archivePlan(f,st::FileOperationKind::RemoveKeepData);check(p.preview().torrentArchive->internalState,"exact selected internal metadata is coalesced with state cleanup");
    const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false,true));take(st::applyFileOperation(f.lease,std::move(p),c));
    check(!fs::exists(f.selected.state)&&fs::exists(f.selected.archive)&&read(f.selected.files[0])=="hello","coalescing never deletes an unrelated separate archive or payload");
  }
  for(const auto point:{st::FileOperationStage::JournalSynced,st::FileOperationStage::StateRemoved,st::FileOperationStage::ArchiveFileRemoved,st::FileOperationStage::ArchiveRemoved}){
    Fixture f;const auto backup=fs::path(f.selected.archive.string()+".bak");write(backup,f.selected.metadata);auto p=archivePlan(f,st::FileOperationKind::RemoveKeepData);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false,true));
    interruptAt=point;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),c,interrupt)&&!interruptArmed&&st::hasPendingFileOperation(f.lease),"archive transaction interruption keeps exact durable journal");
    check(fs::exists(f.selected.archive)==(point!=st::FileOperationStage::ArchiveFileRemoved&&point!=st::FileOperationStage::ArchiveRemoved)&&fs::exists(backup)==(point!=st::FileOperationStage::ArchiveRemoved),"archive is removed only at its final acknowledged stage");
    const auto recovered=take(st::recoverFileOperation(f.lease));check(recovered&&recovered->recovered&&!fs::exists(f.selected.archive)&&!fs::exists(backup)&&!fs::exists(f.selected.state)
        &&read(f.selected.files[0])=="hello"&&!st::hasPendingFileOperation(f.lease),"recovery revalidates and finishes archive deletion idempotently");
  }
  {
    Fixture f;auto p=archivePlan(f,st::FileOperationKind::RemoveKeepData);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false,true));
    interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),c,interrupt),"archive conflict recovery fixture");
    auto changed=doc(read(f.selected.archive));put(changed,"opaque.replacement","3:new");write(f.selected.archive,changed.serialize());
    check(!st::recoverFileOperation(f.lease)&&read(f.selected.archive)==changed.serialize()&&fs::exists(f.selected.state)&&st::hasPendingFileOperation(f.lease),"recovery never deletes changed archive or mutates state before archive CAS");
  }
}

fs::path mutationPath;struct stat mutationBefore{};
st::FileOperationStage mutationStage=st::FileOperationStage::Completed;bool mutationArmed=false;
void mutateWithRestoredMtime(st::FileOperationStage stage){
  if(!mutationArmed||stage!=mutationStage){return;}mutationArmed=false;
  write(mutationPath,"HELLO");
  const struct timespec times[]{mutationBefore.st_atim,mutationBefore.st_mtim};
  check(::utimensat(AT_FDCWD,mutationPath.c_str(),times,0)==0,"restore actual source mtime");
  struct stat now{};check(::stat(mutationPath.c_str(),&now)==0&&st::fileop::identity(now,mutationBefore)
      &&now.st_size==mutationBefore.st_size&&now.st_mtim.tv_sec==mutationBefore.st_mtim.tv_sec
      &&now.st_mtim.tv_nsec==mutationBefore.st_mtim.tv_nsec
      &&(now.st_ctim.tv_sec!=mutationBefore.st_ctim.tv_sec||now.st_ctim.tv_nsec!=mutationBefore.st_ctim.tv_nsec),
      "negative fixture must really preserve inode/size/mtime while changing ctime");
}
std::size_t payloadDescriptors(const struct stat& identity){
  // Count this owned inode only, not unrelated process descriptors or temporary
  // directory streams. This also works after a successful unlink of its name.
  std::size_t count=0;DIR* entries=::opendir("/proc/self/fd");check(entries,"open owned process descriptor inventory");
  while(const auto* entry=::readdir(entries)){
    const std::string_view name=entry->d_name;int fd=-1;
    const auto parsed=std::from_chars(name.data(),name.data()+name.size(),fd);
    if(parsed.ec!=std::errc{}||parsed.ptr!=name.data()+name.size()){continue;}
    struct stat held{};if(::fstat(fd,&held)==0&&st::fileop::identity(held,identity)){++count;}
  }
  check(::closedir(entries)==0,"close descriptor inventory");return count;
}
void digestReceiptsEndWithApply(){
  for(const bool fail:{false,true}){
    Fixture f;struct stat identity{};check(::stat(f.selected.files[0].c_str(),&identity)==0,"receipt-lifetime payload identity");
    auto prepared=f.plan(st::FileOperationKind::Relocate);const auto confirmation=take(st::confirmFileOperation(prepared,prepared.preview().approvalScopeDigest,false));
    const auto baseline=payloadDescriptors(identity);
    interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=fail;
    const auto result=st::applyFileOperation(f.lease,std::move(prepared),confirmation,interrupt);
    check(bool(result)!=fail&&!interruptArmed,"actual successful/failed apply completed the intended path");
    check(prepared.preview().hash==f.selected.hash,"caller still retains the attempted prepared object");
    check(payloadDescriptors(identity)==baseline,"apply releases all transient digest descriptors before returning while prepared lives");
    check(st::hasPendingFileOperation(f.lease)==fail,"receipt cleanup does not alter durable recovery state");
    if(fail){const auto recovered=take(st::recoverFileOperation(f.lease));check(recovered&&recovered->recovered,"failed apply still recovers independently");}
    check(read(f.destination/"file.bin")=="hello"&&!fs::exists(f.selected.files[0])
        &&payloadDescriptors(identity)==baseline,"successful/recovered move preserves data and no caller-owned receipt fd");
  }
}
void payloadDigestWorkAndInterference(){
  {
    Fixture f;auto p=f.plan(st::FileOperationKind::Relocate);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));
    resetReads();const auto source=watchReads(f.selected.files[0]);countingReads=true;
    const auto result=take(st::applyFileOperation(f.lease,std::move(p),c));countingReads=false;
    check(result.filesMoved==1&&reads(source)==10&&read(f.destination/"file.bin")=="hello"&&!fs::exists(f.selected.files[0]),
        "same-device move hashes once before link, then rehashes after link ctime transition");
    std::cout<<"payload-work same-device-move source="<<reads(source)<<" bytes for 5-byte payload (previous schedule15)\n";
  }
  {
    Fixture f;write(f.destination/"file.bin","other");auto p=f.plan(st::FileOperationKind::Relink);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));
    resetReads();const auto source=watchReads(f.selected.files[0]),target=watchReads(f.destination/"file.bin");countingReads=true;
    take(st::applyFileOperation(f.lease,std::move(p),c));countingReads=false;
    check(reads(source)==0&&reads(target)==5&&read(f.selected.files[0])=="hello"&&read(f.destination/"file.bin")=="other",
        "unchanged relink hashes actual destination exactly once, never old source");
    std::cout<<"payload-work relink target="<<reads(target)<<" bytes (previous schedule10)\n";
  }
  {
    Fixture f;auto p=f.plan(st::FileOperationKind::RemoveDeleteData);
    check(!st::confirmFileOperation(p,p.preview().approvalScopeDigest,false),"optimized delete still requires explicit confirmation");
    const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));
    resetReads();const auto source=watchReads(f.selected.files[0]);countingReads=true;
    const auto result=take(st::applyFileOperation(f.lease,std::move(p),c));countingReads=false;
    check(result.filesDeleted==1&&reads(source)==5&&!fs::exists(f.selected.files[0])&&!st::hasPendingFileOperation(f.lease),
        "unchanged delete uses same pinned full-stat proof, one actual hash");
    std::cout<<"payload-work delete source="<<reads(source)<<" bytes (previous schedule10)\n";
  }
  {
    Fixture f;struct stat from{},to{};check(::stat(f.source.c_str(),&from)==0&&::stat(f.destination.c_str(),&to)==0,"copy work directories");
    copyFromDev=from.st_dev;copyFromIno=from.st_ino;copyToDev=to.st_dev;copyToIno=to.st_ino;copyHits=0;
    auto p=f.plan(st::FileOperationKind::Relocate);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));
    resetReads();const auto source=watchReads(f.selected.files[0]);countingReads=true;forceCopy=true;
    auto result=st::applyFileOperation(f.lease,std::move(p),c);forceCopy=false;countingReads=false;take(std::move(result));
    check(copyHits==1&&reads(source)==10&&read(f.destination/"file.bin")=="hello"&&!fs::exists(f.selected.files[0]),
        "copy still reads real source for transfer and independently validates scratch; redundant source hashes absent");
    std::cout<<"payload-work copy source="<<reads(source)<<" bytes (previous source schedule25; separate scratch5 retained)\n";
  }
  for(const auto point:{st::FileOperationStage::JournalSynced,st::FileOperationStage::DestinationPublished}){
    Fixture f;auto p=f.plan(st::FileOperationKind::Relocate);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));
    interruptAt=point;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),c,interrupt)&&st::hasPendingFileOperation(f.lease),"work fixture interrupts actual durable move");
    resetReads();const auto source=watchReads(f.selected.files[0]);countingReads=true;
    const auto recovered=take(st::recoverFileOperation(f.lease));countingReads=false;
    check(recovered&&recovered->recovered&&recovered->filesMoved==1&&reads(source)==(point==st::FileOperationStage::JournalSynced?10u:5u)
        &&read(f.destination/"file.bin")=="hello"&&!fs::exists(f.selected.files[0]),
        "recovery starts with no cached authority and rehashes actual named bytes before reuse");
  }
  for(const auto point:{st::FileOperationStage::JournalSynced,st::FileOperationStage::DestinationPublished}){
    Fixture f;auto p=f.plan(st::FileOperationKind::Relocate);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));
    mutationPath=f.selected.files[0];check(::stat(mutationPath.c_str(),&mutationBefore)==0,"mutation source stamp");mutationStage=point;mutationArmed=true;
    resetReads();const auto source=watchReads(f.selected.files[0]);countingReads=true;
    const auto rejected=st::applyFileOperation(f.lease,std::move(p),c,mutateWithRestoredMtime);countingReads=false;
    check(!rejected&&rejected.error().code==st::ErrorCode::Conflict&&!mutationArmed&&reads(source)==10
        &&read(f.selected.files[0])=="HELLO"&&st::hasPendingFileOperation(f.lease),
        "same inode/length/restored mtime cannot hide changed ctime/content before link or final removal");
    check(fs::exists(f.destination/"file.bin")== (point==st::FileOperationStage::DestinationPublished),"refusal does not create an unapproved destination");
    check(!st::recoverFileOperation(f.lease)&&read(f.selected.files[0])=="HELLO","new recovery proof also refuses the changed bytes");
  }
  {
    Fixture f;auto p=f.plan(st::FileOperationKind::RemoveDeleteData);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));
    const auto inventory=read(f.profile/"downloads.config"),state=read(f.selected.state);auto changed=doc(inventory);put(changed,"opaque","7:changed");write(f.profile/"downloads.config",changed.serialize());
    resetReads();const auto source=watchReads(f.selected.files[0]);countingReads=true;const auto rejected=st::applyFileOperation(f.lease,std::move(p),c);countingReads=false;
    check(!rejected&&reads(source)==0&&read(f.selected.files[0])=="hello"&&read(f.selected.state)==state&&!st::hasPendingFileOperation(f.lease),
        "whole-document CAS still refuses before payload hash/journal/delete");
  }
  {
    Area area;st::fileop::PayloadDigestCache cache;std::array<struct stat,st::fileop::PayloadDigestCache::Capacity+1> stamps{};
    resetReads();
    for(std::size_t i=0;i<stamps.size();++i){const auto path=area.root/std::to_string(i);write(path,"hello");check(::stat(path.c_str(),&stamps[i])==0,"bounded cache fixture");watchReads(path);}
    countingReads=true;std::string original;
    for(std::size_t i=0;i<stamps.size();++i){const auto value=cache.digest(area.root/std::to_string(i),stamps[i]);if(i==0){original=value;}check(value==original,"actual equal-content cache fixture");}
    check(cache.digest(area.root/"0",stamps[0])==original,"evicted receipt still rehashes exact original data");countingReads=false;
    check(reads(0)==10,"ninth live receipt evicts oldest pinned descriptor and requires real rehash");
    fs::rename(area.root/"0",area.root/"old");write(area.root/"0","hello");bool refused=false;
    try{static_cast<void>(cache.digest(area.root/"0",stamps[0]));}catch(const st::fileop::Failure& error){refused=error.error.code==st::ErrorCode::Conflict;}
    check(refused&&read(area.root/"old")=="hello","equal-content pathname replacement cannot borrow retained inode proof");
  }
  resetReads();
}


// More files than the unchanged eight receipt descriptors. This fixture uses
// real distinct inodes and unequal lengths, with deliberately nonlexical names;
// profile/control/journal IO never contributes to the existing inode counters.
struct ManyPayloads {
  Area area;fs::path profile=area.root/"profile",source=area.root/"source",destination=area.root/"destination";
  std::vector<fs::path> files,targets;std::vector<std::string> values,targetValues;std::vector<bool> present;
  core::TorrentHash hash;std::optional<st::ProfileLease> lease;fs::path state;
  ManyPayloads(std::size_t count,bool sparse,st::FileOperationKind kind){
    std::string entries="l",all;fs::create_directories(destination);
    for(std::size_t i=0;i<count;++i){
      const auto leaf="leaf-"+std::to_string((i*7)%count)+'-'+std::to_string(i)+".bin";
      values.emplace_back(7+3*i,static_cast<char>('A'+i));targetValues.emplace_back(7+3*i,static_cast<char>('a'+i));all+=values.back();
      files.push_back(source/"bundle"/leaf);targets.push_back(destination/"bundle"/leaf);
      present.push_back(!sparse||(i!=0&&i!=4&&i!=9&&i!=16));
      entries+="d6:lengthi"+std::to_string(values.back().size())+"e4:pathl"+b(leaf)+"ee";
      // Relink validates destination content, and leaves every source alone.
      if(kind==st::FileOperationKind::Relink||present.back()){write(files.back(),values.back());}
      if(kind==st::FileOperationKind::Relink&&present.back()){write(targets.back(),targetValues.back());}
    }
    std::string pieces;for(std::size_t i=0;i<all.size();i+=4){pieces+=sha1(std::string_view(all).substr(i,std::min<std::size_t>(4,all.size()-i)));}
    const auto info="d5:files"+entries+"e4:name6:bundle12:piece lengthi4e6:pieces"+b(pieces)+'e';hash=take(core::TorrentHash::parse(sha1(info)).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"Many-file fixture identity"};}));
    state=profile/"active"/(hash.hex()+".dat");std::string stores="l",priorities="l";for(std::size_t i=0;i<count;++i){stores+="1:L";priorities+="i1e";}
    write(state,"d4:info"+info+"10:attributesd10:storetypes"+stores+"e14:resumecompletei2e12:opaque.stateli7e3:raweee");
    const auto archive=area.root/"retained.torrent";write(archive,read(state));
    write(profile/"downloads.config","d9:downloadsld12:torrent_hash"+b(sha1(info))+"7:torrent"+b(archive.string())+"8:save_dir"+b(source.string())+"9:save_file6:bundle5:statei70e10:forceStarti0e15:file_priorities"+priorities+"e10:opaque.rowd4:keep3:raweee6:opaque3:rawe");
    lease.emplace(take(st::ProfileLease::acquire(profile)));
  }
  st::PreparedFileOperation plan(st::FileOperationKind kind){return take(st::prepareFileOperation(*lease,{hash,kind,kind==st::FileOperationKind::Relink||kind==st::FileOperationKind::Relocate?std::optional<fs::path>{destination}:std::nullopt}));}
};
struct ManyTrace {
  ManyPayloads& fixture;st::FileOperationKind kind;
  std::vector<struct stat> identities;std::vector<std::size_t> presentIndices;
  std::vector<st::FileOperationStage> stages;std::size_t destinations=0,removed=0;std::string scope,cas;
  std::optional<std::size_t> mutateIndex{};bool mutated=false,stopAfterJournal=false,stopAfterDestination=false;
  explicit ManyTrace(ManyPayloads& f,st::FileOperationKind k):fixture(f),kind(k){for(std::size_t i=0;i<f.files.size();++i){if(!f.present[i]){continue;}presentIndices.push_back(i);struct stat stamp{};const auto& path=k==st::FileOperationKind::Relink?f.targets[i]:f.files[i];check(::stat(path.c_str(),&stamp)==0,"Many-file actual watched inode");identities.push_back(stamp);check(payloadDescriptors(stamp)==0,"Fixture starts without retained payload descriptors");}}
  void noDescriptors()const{for(const auto& stamp:identities){check(payloadDescriptors(stamp)==0,"Attempted caller/fresh recovery retains no digest descriptor after return");}}
  void bytesIntact()const{for(std::size_t i=0;i<fixture.files.size();++i){if(kind==st::FileOperationKind::Relink){check(read(fixture.files[i])==fixture.values[i],"Relink preserves every source byte");if(fixture.present[i]){check(read(fixture.targets[i])==fixture.targetValues[i],"Relink retains actual selected target bytes");}else{check(!fs::exists(fixture.targets[i]),"Relink cannot invent missing target");}}else if(kind==st::FileOperationKind::Relocate){check(!fs::exists(fixture.files[i]),"Move removes each present source and does not create missing sources");if(fixture.present[i]){check(read(fixture.targets[i])==fixture.values[i],"Move preserves every actual original payload byte");}else{check(!fs::exists(fixture.targets[i]),"Move cannot invent a missing source");}}else{check(!fs::exists(fixture.files[i]),"Delete removes only the original listed present leaves");}}}
};
ManyTrace* manyTrace=nullptr;
struct ObserveMany {ManyTrace& trace;explicit ObserveMany(ManyTrace& value):trace(value){check(!manyTrace,"Single owned observer");manyTrace=&trace;}~ObserveMany(){manyTrace=nullptr;countingReads=false;}};
void observeMany(st::FileOperationStage stage){
  auto& trace=*manyTrace;auto& f=trace.fixture;trace.stages.push_back(stage);
  if(stage==st::FileOperationStage::JournalSynced){
    const auto outer=doc(read(f.profile/"BiglyBTC.file-operation"));const auto* bytes=outer.find("payload");check(bytes&&bytes->type==core::Node::Type::Bytes,"Actual durable journal payload");const auto journal=doc(std::string(bytes->bytes));if(!trace.scope.empty()){check(journal.find("scope")->bytes==trace.scope&&journal.find("cas")->bytes==trace.cas,"Actual journal preserves accepted approval and execution CAS");}const auto* rows=journal.find("files");const auto* runtime=journal.find("runtime");
    check(rows&&runtime&&rows->children.size()==f.files.size()&&runtime->children.size()==f.files.size(),"Journal retains original bounded file vector");
    for(std::size_t i=0;i<f.files.size();++i){const auto& row=rows->children[i];check(row.find("i")->integer==static_cast<std::int64_t>(i)&&row.find("s")->bytes==f.files[i].string(),"Journal indices and source paths keep original order");const auto* hash=runtime->children[i].find("sha");check(hash&&hash->bytes==(f.present[i]?st::fileop::digest(trace.kind==st::FileOperationKind::Relink?f.targetValues[i]:f.values[i]):std::string{}),"Journal digest stays attached to the actual original index");}
    std::size_t retained=0;for(const auto& stamp:trace.identities){retained+=payloadDescriptors(stamp);}check(retained<=8,"Pre-journal proof retains no more than unchanged eight payload descriptors");
    if(trace.mutateIndex){const auto index=*trace.mutateIndex;const auto& path=trace.kind==st::FileOperationKind::Relink?f.targets[index]:f.files[index];struct stat before{};check(::stat(path.c_str(),&before)==0,"Mutation actual identity");auto changed=trace.kind==st::FileOperationKind::Relink?f.targetValues[index]:f.values[index];changed[0]='!';write(path,changed);const struct timespec times[]{before.st_atim,before.st_mtim};check(::utimensat(AT_FDCWD,path.c_str(),times,0)==0,"Restore actual mtime after callback mutation");struct stat after{};check(::stat(path.c_str(),&after)==0&&st::fileop::identity(before,after)&&before.st_size==after.st_size&&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec&&(before.st_ctim.tv_sec!=after.st_ctim.tv_sec||before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec),"Callback really changes ctime/content while keeping inode/size/mtime");trace.mutated=true;}
    if(trace.stopAfterJournal){throw std::runtime_error("Owned many-file durable interruption");}
  }
  if(stage==st::FileOperationStage::DestinationPublished){check(trace.destinations<trace.presentIndices.size(),"No invented destination notification");const auto index=trace.presentIndices[trace.destinations++];check(fs::exists(f.targets[index]),"Destination notifications remain forward and follow actual publication");for(std::size_t n=trace.destinations;n<trace.presentIndices.size();++n){check(!fs::exists(f.targets[trace.presentIndices[n]]),"Later destinations are not published early");}if(trace.stopAfterDestination){throw std::runtime_error("Owned first-destination interruption");}}
  if(stage==st::FileOperationStage::SourceRemoved){check(trace.removed<trace.presentIndices.size(),"No invented source removal notification");const auto index=trace.presentIndices[trace.removed++];check(!fs::exists(f.files[index]),"Source removal notification follows original forward leaf");for(std::size_t n=trace.removed;n<trace.presentIndices.size();++n){check(fs::exists(f.files[trace.presentIndices[n]]),"Later sources remain until their original mutation turn");}}
}
void manyPayloadDigestTraversal(){
  for(const auto kind:{st::FileOperationKind::Relink,st::FileOperationKind::RemoveDeleteData,st::FileOperationKind::Relocate}){for(int shape=0;shape<3;++shape){
    const std::size_t count=shape==0?9:17;const bool sparse=shape==2;ManyPayloads f(count,sparse,kind);auto p=f.plan(kind);const auto scope=p.preview().approvalScopeDigest,cas=p.preview().executionCasDigest;ManyTrace trace(f,kind);trace.scope=scope;trace.cas=cas;ObserveMany observed(trace);
    check(p.preview().presentFiles==trace.presentIndices.size()&&p.preview().missingFiles==count-trace.presentIndices.size(),"Exact present subset in original index order");
    const auto c=take(st::confirmFileOperation(p,scope,kind==st::FileOperationKind::RemoveDeleteData));check(p.preview().approvalScopeDigest==scope&&p.preview().executionCasDigest==cas,"Confirmation cannot reorder or alter approval/CAS");
    const auto foreign=f.source/"bundle"/"foreign.keep";write(foreign,"foreign data");resetReads();std::vector<std::size_t> watched;for(const auto index:trace.presentIndices){watched.push_back(watchReads(kind==st::FileOperationKind::Relink?f.targets[index]:f.files[index]));}std::vector<std::size_t> oldSources;if(kind==st::FileOperationKind::Relink){for(const auto& path:f.files){oldSources.push_back(watchReads(path));}}
    countingReads=true;const auto result=take(st::applyFileOperation(*f.lease,std::move(p),c,observeMany));countingReads=false;
    std::uint64_t total=0;for(std::size_t rank=0;rank<watched.size();++rank){const auto index=trace.presentIndices[rank];const auto copies=(kind==st::FileOperationKind::Relocate?2u:1u)+(rank>=8?1u:0u);const auto actual=reads(watched[rank]);check(actual==copies*f.values[index].size(),"Actual per-inode reads match retained-prefix/tail expectation without skipping content proofs");total+=actual;}for(const auto index:oldSources){check(reads(index)==0,"Relink never hashes old source payload");}
    // Independent literal sums for 7+3*i bytes (9/17 files; sparse omits0,4,9,16).
    const auto expected=kind==st::FileOperationKind::Relocate?std::array<std::uint64_t,3>{373,1441,1054}[shape]:std::array<std::uint64_t,3>{202,914,642}[shape];check(total==expected,"Observed unequal-size total must match literal fixture expectation");
    check(result.filesMoved==(kind==st::FileOperationKind::Relocate?trace.presentIndices.size():0)&&result.filesDeleted==(kind==st::FileOperationKind::RemoveDeleteData?trace.presentIndices.size():0),"Exact effects do not include missing or unlisted leaves");check(!st::hasPendingFileOperation(*f.lease)&&trace.stages.front()==st::FileOperationStage::JournalSynced&&trace.stages.back()==st::FileOperationStage::Completed,"Durable journal/notification endpoints unchanged");check(trace.destinations==(kind==st::FileOperationKind::Relocate?trace.presentIndices.size():0)&&trace.removed==(kind==st::FileOperationKind::Relink?0:trace.presentIndices.size()),"Original forward mutation/notification counts unchanged");trace.noDescriptors();check(p.preview().hash==f.hash,"Attempted caller remains alive without receipts");trace.bytesIntact();check(read(foreign)=="foreign data","Foreign unlisted data never removed");
    const auto inventory=doc(read(f.profile/"downloads.config"));check(inventory.find("opaque")->bytes=="raw","Unknown inventory retained");if(kind!=st::FileOperationKind::RemoveDeleteData){check(doc(read(f.state)).find("attributes")->find("opaque.state"),"Unknown state retained");}
    std::cout<<"payload-work many kind="<<static_cast<int>(kind)<<" files="<<count<<" present="<<trace.presentIndices.size()<<" returned-read-bytes="<<total<<"\n";
  }}

  for(const bool changedDocument:{false,true}){
    ManyPayloads f(17,false,st::FileOperationKind::RemoveDeleteData);auto p=f.plan(st::FileOperationKind::RemoveDeleteData);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));ManyTrace trace(f,st::FileOperationKind::RemoveDeleteData);
    if(changedDocument){auto inventory=doc(read(f.profile/"downloads.config"));put(inventory,"opaque","7:changed");write(f.profile/"downloads.config",inventory.serialize());}else{c.deleteConfirmed=false;}
    resetReads();std::vector<std::size_t> watched;for(const auto& path:f.files){watched.push_back(watchReads(path));}countingReads=true;const auto failed=st::applyFileOperation(*f.lease,std::move(p),c);countingReads=false;
    check(!failed&&failed.error().code==st::ErrorCode::Conflict&&!st::hasPendingFileOperation(*f.lease),"Rejected confirmation/document CAS precedes all many-file hashing and journal effects");for(const auto index:watched){check(reads(index)==0,"Refusal must perform zero observed payload reads");}for(std::size_t i=0;i<f.files.size();++i){check(read(f.files[i])==f.values[i],"Pre-journal refusal preserves all17 source leaves");}trace.noDescriptors();
  }
  for(const auto kind:{st::FileOperationKind::Relink,st::FileOperationKind::RemoveDeleteData,st::FileOperationKind::Relocate}){for(const std::size_t index:{0u,16u}){
    ManyPayloads f(17,false,kind);auto p=f.plan(kind);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,kind==st::FileOperationKind::RemoveDeleteData));ManyTrace trace(f,kind);trace.mutateIndex=index;ObserveMany observed(trace);
    const auto failed=st::applyFileOperation(*f.lease,std::move(p),c,observeMany);check(!failed&&failed.error().code==st::ErrorCode::Conflict&&trace.mutated&&st::hasPendingFileOperation(*f.lease),"Retained-prefix and evicted-tail callback changes both refuse accepted content mismatch");trace.noDescriptors();const auto& path=kind==st::FileOperationKind::Relink?f.targets[index]:f.files[index];auto changed=kind==st::FileOperationKind::Relink?f.targetValues[index]:f.values[index];changed[0]='!';check(read(path)==changed,"Refusal never deletes or replaces changed callback data");check(!st::recoverFileOperation(*f.lease)&&read(path)==changed,"Fresh cold recovery independently refuses changed actual content");trace.noDescriptors();
  }}
  for(const bool afterDestination:{false,true}){
    ManyPayloads f(17,false,st::FileOperationKind::Relocate);auto p=f.plan(st::FileOperationKind::Relocate);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));ManyTrace trace(f,st::FileOperationKind::Relocate);trace.stopAfterJournal=!afterDestination;trace.stopAfterDestination=afterDestination;ObserveMany observed(trace);
    check(!st::applyFileOperation(*f.lease,std::move(p),c,observeMany)&&st::hasPendingFileOperation(*f.lease),"Many-file actual durable interruption");trace.noDescriptors();check(p.preview().hash==f.hash,"Failed attempted plan still owned by caller");resetReads();std::vector<std::size_t> watched;for(const auto& path:f.files){watched.push_back(watchReads(path));}countingReads=true;const auto result=take(st::recoverFileOperation(*f.lease));countingReads=false;
    check(result&&result->recovered&&result->filesMoved==17&&!st::hasPendingFileOperation(*f.lease),"Fresh recovery completes exact original many-file scope");std::uint64_t total=0;for(std::size_t i=0;i<watched.size();++i){const auto actual=reads(watched[i]);check(actual==2*f.values[i].size(),"Recovery proves cold actual named content before reuse and post-link unlink");total+=actual;}check(total==1054,"Cold recovery actual unequal-size total");trace.noDescriptors();trace.bytesIntact();
    std::cout<<"payload-work many cold-recovery after-destination="<<afterDestination<<" files=17 returned-read-bytes="<<total<<"\n";
  }
  resetReads();
}

void realCopyBranch(){
  Fixture f;struct stat from{},to{};check(::stat(f.source.c_str(),&from)==0&&::stat(f.destination.c_str(),&to)==0,"copy fixture dirs");copyFromDev=from.st_dev;copyFromIno=from.st_ino;copyToDev=to.st_dev;copyToIno=to.st_ino;copyHits=0;forceCopy=true;auto p=f.plan(st::FileOperationKind::Relocate);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));auto result=st::applyFileOperation(f.lease,std::move(p),c);forceCopy=false;take(std::move(result));check(copyHits>0&&read(f.destination/"file.bin")=="hello"&&!fs::exists(f.source/"file.bin"),"EXDEV fallback copies actual bytes and removes source only after publication");
}
}
extern "C" ssize_t __real_read(int,void*,std::size_t);
extern "C" ssize_t __real___read_chk(int,void*,std::size_t,std::size_t);
extern "C" ssize_t __wrap_read(int fd,void* data,std::size_t bytes){const auto count=__real_read(fd,data,bytes);countRead(fd,count);return count;}
extern "C" ssize_t __wrap___read_chk(int fd,void* data,std::size_t bytes,std::size_t bound){const auto count=__real___read_chk(fd,data,bytes,bound);countRead(fd,count);return count;}
extern "C" int __real_linkat(int,const char*,int,const char*,int);
extern "C" int __wrap_linkat(int a,const char* x,int b,const char* y,int flags){struct stat from{},to{};if(forceCopy&&std::string_view(x)=="file.bin"&&std::string_view(y)=="file.bin"&&::fstat(a,&from)==0&&::fstat(b,&to)==0&&from.st_dev==copyFromDev&&from.st_ino==copyFromIno&&to.st_dev==copyToDev&&to.st_ino==copyToIno){++copyHits;errno=EXDEV;return -1;}return __real_linkat(a,x,b,y,flags);}
void canonicalLatin1MoveUsesActualDecodedLeaves() {
  Fixture f(true);auto state=doc(f.selected.metadata);auto info=doc(take(core::encodeValue(*state.find("info"),st::compatibleConfigLimits()).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"fixture encode"};})));
  const std::string first("na\xefve.bin",9),second("caf\xe9.bin",8);
  put(info,"files","ld6:lengthi5e4:pathl"+b(first)+"eed6:lengthi6e4:pathl"+b(second)+"eee");
  put(info,"name",b(std::string("caf\xe9",4)));const auto rawInfo=info.serialize();const auto old=f.selected.state;
  f.selected.hash=*core::TorrentHash::parse(sha1(rawInfo));f.selected.state=f.profile/"active"/(f.selected.hash.hex()+".dat");
  put(state,"info",rawInfo);put(state,"encoding",b("ISO-8859-1"));f.selected.metadata=state.serialize();write(f.selected.state,f.selected.metadata);write(f.selected.archive,f.selected.metadata);
  auto row=doc(f.selected.row);put(row,"torrent_hash",b(sha1(rawInfo)));f.selected.row=row.serialize();f.inventory();check(fs::remove(old),"Remove obsolete generated identity");
  const std::array<std::string,2> names{"na\xc3\xafve.bin","caf\xc3\xa9.bin"};
  for(std::size_t i=0;i<names.size();++i){const auto renamed=f.source/"bundle"/names[i];fs::rename(f.selected.files[i],renamed);f.selected.files[i]=renamed;}
  auto plan=f.plan(st::FileOperationKind::Relocate);check(plan.preview().presentFiles==2,"Original decoded Unicode payload leaves found");
  const auto confirmed=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(plan),confirmed));
  for(std::size_t i=0;i<names.size();++i){check(!fs::exists(f.selected.files[i])&&read(f.destination/"bundle"/names[i])==f.selected.values[i],"Move follows decoded actual leaf without interpreting raw Latin1 as filesystem bytes");}
  const auto after=doc(read(f.selected.state));check(after.find("encoding")->bytes=="ISO-8859-1"&&take(core::encodeValue(*after.find("info"),st::compatibleConfigLimits()).transform_error([](auto){return st::Error{st::ErrorCode::InvalidFormat,"fixture encode"};}))==rawInfo,"Move keeps raw info identity and original charset");
  check(after.find("attributes")->find("opaque.state"),"Move retains unrelated original state");
}
void individualFileMappingsAndRevert(){
  Fixture f(true);const auto inventory=read(f.profile/"downloads.config"),rawInfo=std::string(*core::encodeValue(*doc(read(f.selected.state)).find("info")));
  // Mapping never rewrites the hashed subject, priorities, force or saved root.
  const auto first=f.selected.files[0],second=f.selected.files[1],renamed=f.destination/"renamed.bin";
  st::FileOperationRequest move{f.selected.hash,st::FileOperationKind::MoveFile,{}};move.fileIndex=0;move.fileDestination=renamed;
  auto plan=take(st::prepareFileOperation(f.lease,move));check(plan.preview().files.size()==1&&plan.preview().files[0].index==0&&read(f.profile/"downloads.config")==inventory,"one-file preparation preserves exact inventory");
  const auto accepted=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(plan),accepted));
  check(read(renamed)=="hello"&&!fs::exists(first)&&read(second)=="world!"&&read(f.profile/"downloads.config")==inventory,"move only one original file; original row unchanged");
  auto state=doc(read(f.selected.state));const auto* links=state.find("attributes")->find("filelinks2");check(links&&links->children.size()==1&&links->children[0].bytes=="0\n"+first.string()+"\n"+renamed.string(),"original indexed link points from canonical to new file");
  check(*core::encodeValue(*state.find("info"))==rawInfo&&state.find("attributes")->find("opaque.state"),"raw info and opaque state survive per-file movement");
  const auto existing=f.destination/"existing.bin";write(existing,"broken");st::FileOperationRequest target{f.selected.hash,st::FileOperationKind::RetargetFile,{}};target.fileIndex=1;target.fileDestination=existing;
  auto retarget=take(st::prepareFileOperation(f.lease,target));const auto allowed=take(st::confirmFileOperation(retarget,retarget.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(retarget),allowed));
  state=doc(read(f.selected.state));check(read(second)=="world!"&&read(existing)=="broken"&&state.find("attributes")->find("filelinks2")->children.size()==2,"retarget keeps old bytes and maps existing untrusted content");
  const auto* resume=state.find("resume")->find("data");check(resume->find("valid")->integer==0&&std::ranges::all_of(resume->find("resume data")->bytes,[](char c){return c==2;}),"retarget invalidates all original piece proof instead of trusting size");
  st::FileOperationRequest revert{f.selected.hash,st::FileOperationKind::RevertFile,{}};revert.fileIndex=0;
  auto back=take(st::prepareFileOperation(f.lease,revert));const auto consent=take(st::confirmFileOperation(back,back.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(back),consent));
  state=doc(read(f.selected.state));check(read(first)=="hello"&&!fs::exists(renamed)&&state.find("attributes")->find("filelinks2")->children.size()==1,"revert restores original bytes and removes only selected link");
  revert.fileIndex=1;check(!st::prepareFileOperation(f.lease,revert)&&read(second)=="world!"&&read(existing)=="broken","revert never overwrites the original extant file");
}
void individualFileMappingRecoveryAndRefusals(){
  for(const auto stage:{st::FileOperationStage::JournalSynced,st::FileOperationStage::DestinationPublished,st::FileOperationStage::StatePublished,st::FileOperationStage::InventoryPublished,st::FileOperationStage::SourceRemoved}){
    Fixture f(true);const auto before=read(f.profile/"downloads.config");const auto destination=f.destination/"new.bin";
    st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::MoveFile,{}};request.fileIndex=1;request.fileDestination=destination;
    auto plan=take(st::prepareFileOperation(f.lease,request));const auto consent=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));interruptAt=stage;interruptArmed=true;
    check(!st::applyFileOperation(f.lease,std::move(plan),consent,interrupt)&&st::hasPendingFileOperation(f.lease),"per-file interrupted transaction remains explicit");
    const auto recovered=take(st::recoverFileOperation(f.lease));check(recovered&&recovered->filesMoved==1&&read(destination)=="world!"&&read(f.selected.files[0])=="hello"&&!fs::exists(f.selected.files[1])&&read(f.profile/"downloads.config")==before,"one-file recovery preserves sibling and saved intent");
  }
  Fixture f(true);st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::MoveFile,{}};request.fileIndex=0;request.fileDestination=f.selected.files[1];
  const auto inventory=read(f.profile/"downloads.config"),state=read(f.selected.state);check(!st::prepareFileOperation(f.lease,request),"same torrent destination collision refused");
  request.fileIndex=10000;request.fileDestination=f.destination/"new.bin";check(!st::prepareFileOperation(f.lease,request),"out-of-range index refused");
  request.fileIndex=0;fs::create_symlink(f.selected.files[1],*request.fileDestination);check(!st::prepareFileOperation(f.lease,request),"symlink destination refused");fs::remove(*request.fileDestination);
  auto plan=take(st::prepareFileOperation(f.lease,request));const auto consent=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));write(f.selected.files[0],"other");check(!st::applyFileOperation(f.lease,std::move(plan),consent)&&!st::hasPendingFileOperation(f.lease),"stale bytes refuse before journal");
  check(read(f.profile/"downloads.config")==inventory&&read(f.selected.state)==state&&read(f.selected.files[1])=="world!","all failed mappings preserve saved/other data");
}
void ownedTrashPublicationAndRecovery(){
  {Fixture f;check(take(st::payloadRecyclePreference(f.lease))&&!fs::exists(f.profile/"biglybt.config"),"original recycle default true stays sparse");
    for(const std::int64_t value:{0LL,1LL,-1LL,4294967296LL}){auto config=doc("de");put(config,"Move Deleted Data To Recycle Bin","i"+std::to_string(value)+'e');write(f.profile/"biglybt.config",config.serialize());check(take(st::payloadRecyclePreference(f.lease))==(static_cast<std::uint32_t>(value)!=0)&&read(f.profile/"biglybt.config")==config.serialize(),"recycle preference preserves Java low32 semantics and unknown config");}}
  for(const auto stage:{st::FileOperationStage::JournalSynced,st::FileOperationStage::DestinationPublished,st::FileOperationStage::TrashInfoPublished,st::FileOperationStage::InventoryPublished,st::FileOperationStage::SourceRemoved}){
    Fixture f(true);const auto trash=f.area.root/"data/Trash";write(f.source/"bundle/foreign","foreign");st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::RemoveRecycleData,{}};request.trashRoot=trash;
    auto plan=take(st::prepareFileOperation(f.lease,request));check(!fs::exists(trash)&&plan.preview().files.size()==2,"trash preparation does not create directories or move payload");
    const auto files=plan.preview().files;check(!st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false),"recycling still requires explicit data consent");const auto consent=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,true));interruptAt=stage;interruptArmed=true;
    check(!st::applyFileOperation(f.lease,std::move(plan),consent,interrupt)&&st::hasPendingFileOperation(f.lease),"recycle interruption leaves durable confirmed intent");
    const auto result=take(st::recoverFileOperation(f.lease));check(result&&result->filesRecycled==2&&result->filesDeleted==0&&read(f.source/"bundle/foreign")=="foreign","recovery recycles only named leaves with no permanent fallback");
    for(const auto& file:files){check(!fs::exists(file.source)&&read(file.destination)==f.selected.values[file.index],"trash retains exact actual payload bytes");const auto info=read(trash/"info"/(file.destination.filename().string()+".trashinfo"));check(info.starts_with("[Trash Info]\nPath="+file.source.string()+"\nDeletionDate=")&&info.back()=='\n',"recoverable info retains exact original path and actual deletion date");}
    check(!fs::exists(f.selected.state)&&fs::exists(f.selected.archive)&&doc(read(f.profile/"downloads.config")).find("downloads")->children.empty()&&!st::hasPendingFileOperation(f.lease),"recycle retains archive and finishes original inventory/state cleanup");
  }
  {Fixture f;const auto trash=f.area.root/"Trash";fs::create_directory(trash);check(::chmod(trash.c_str(),0755)==0,"set unowned-mode fixture");st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::RemoveRecycleData,{}};request.trashRoot=trash;
    check(!st::prepareFileOperation(f.lease,request)&&read(f.selected.files[0])=="hello"&&!st::hasPendingFileOperation(f.lease),"nonprivate trash refuses before mutation");}
}

fs::path swapTrashFiles,retainedTrashFiles;bool swapTrashArmed=false;
void swapTrashDirectory(st::FileOperationStage stage){if(swapTrashArmed&&stage==st::FileOperationStage::InventoryPublished){swapTrashArmed=false;fs::rename(swapTrashFiles,retainedTrashFiles);fs::create_directory(swapTrashFiles);check(::chmod(swapTrashFiles.c_str(),0700)==0,"private replacement trash fixture");}}
void trashDirectoryReplacementNeverFallsBackToDelete(){
  Fixture f;const auto trash=f.area.root/"Trash";st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::RemoveRecycleData,{}};request.trashRoot=trash;
  auto plan=take(st::prepareFileOperation(f.lease,request));const auto destination=plan.preview().files[0].destination;const auto consent=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,true));swapTrashFiles=trash/"files";retainedTrashFiles=trash/"held-files";swapTrashArmed=true;
  const auto failed=st::applyFileOperation(f.lease,std::move(plan),consent,swapTrashDirectory);check(!failed&&failed.error().code==st::ErrorCode::Conflict&&st::hasPendingFileOperation(f.lease),"published trash identity mismatch retains explicit recovery");
  check(read(f.selected.files[0])=="hello"&&read(retainedTrashFiles/destination.filename())=="hello"&&fs::is_empty(swapTrashFiles),"uncertain recycle keeps both source and actual copied data; never permanent fallback");
  check(!st::recoverFileOperation(f.lease)&&read(f.selected.files[0])=="hello","cold recovery independently refuses replaced trash directory");
  check(fs::remove(swapTrashFiles),"remove exact empty owned replacement");fs::rename(retainedTrashFiles,swapTrashFiles);const auto result=take(st::recoverFileOperation(f.lease));check(result&&result->filesRecycled==1&&read(destination)=="hello"&&!fs::exists(f.selected.files[0]),"restored exact directory permits idempotent original recycle intent");
}
void perFileNonlinearMoveKeepsControlAndRetargetRefuses(){
  // Offset0/length5/piece4 compact stores only the final one-byte tail.
  Fixture f;attributes(f,"l1:Ce");write(f.selected.files[0],"o");const auto marker=control(f,0);const std::string bytes="d7:versioni0e6:lengthi5ee";write(marker,bytes);
  st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::MoveFile,{}};request.fileIndex=0;request.fileDestination=f.destination/"packed.bin";
  auto plan=take(st::prepareFileOperation(f.lease,request));const auto consent=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(plan),consent));
  check(read(*request.fileDestination)=="o"&&read(marker)==bytes&&doc(read(f.selected.state)).find("attributes")->find("storetypes")->children[0].bytes=="C","per-file move preserves real compact physical bytes and indexed controls without relabeling");
  write(f.destination/"other.bin","hello");request.kind=st::FileOperationKind::RetargetFile;request.fileDestination=f.destination/"other.bin";const auto before=read(f.selected.state);
  check(!st::prepareFileOperation(f.lease,request)&&read(f.selected.state)==before&&read(marker)==bytes,"existing-data retarget does not reuse unrelated nonlinear maps");
}
void explicitReplacementPreservesReviewedPriorInodeAndOriginalIntent(){
  Fixture f(true);const auto target=f.destination/"occupied.bin";write(target,"prior bytes longer than source");struct stat before{},after{};check(::stat(target.c_str(),&before)==0,"prior destination observed");const auto inventory=read(f.profile/"downloads.config");
  st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::ReplaceFile,{}};request.fileIndex=0;request.fileDestination=target;
  auto plan=take(st::prepareFileOperation(f.lease,request));check(plan.preview().replacement&&plan.preview().replacement->bytes==30&&plan.preview().replacement->sha256.size()==64,"explicit prior destination size/digest/path are in preview");const auto backup=plan.preview().replacement->path;check(!fs::exists(backup.parent_path())&&read(target)=="prior bytes longer than source","replacement preview has no effects");
  check(!st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false),"replacement needs explicit reviewed data consent");const auto confirmation=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,true));const auto result=take(st::applyFileOperation(f.lease,std::move(plan),confirmation));
  check(result.retainedPreimage&&*result.retainedPreimage==backup&&read(backup)=="prior bytes longer than source"&&read(target)=="hello"&&!fs::exists(f.selected.files[0])&&read(f.selected.files[1])=="world!","replacement retains prior bytes and moves only selected source");
  check(::stat(backup.c_str(),&after)==0&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&after.st_nlink==1,"exact prior inode is retained privately");check(read(f.profile/"downloads.config")==inventory&&doc(read(f.selected.state)).find("attributes")->find("opaque.state"),"replacement preserves row intent and opaque state");
}
void explicitReplacementColdRecoveryAndUnknownSources(){
  for(const auto stage:{st::FileOperationStage::JournalSynced,st::FileOperationStage::PreimageDirectorySealed,st::FileOperationStage::PreimagePreserved,st::FileOperationStage::DestinationPublished,st::FileOperationStage::StatePublished,st::FileOperationStage::InventoryPublished,st::FileOperationStage::SourceRemoved}){
    Fixture f;const auto target=f.destination/"occupied.bin";write(target,"old");st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::ReplaceFile,{}};request.fileIndex=0;request.fileDestination=target;
    auto plan=take(st::prepareFileOperation(f.lease,request));const auto backup=plan.preview().replacement->path;const auto confirmation=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,true));interruptAt=stage;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(plan),confirmation,interrupt)&&st::hasPendingFileOperation(f.lease),"replacement interruption retains confirmed journal");const auto recovered=take(st::recoverFileOperation(f.lease));check(recovered&&recovered->retainedPreimage==backup&&read(backup)=="old"&&read(target)=="hello"&&!fs::exists(f.selected.files[0])&&!st::hasPendingFileOperation(f.lease),"cold recovery uses only exact prior inode and source identities");
  }
  {Fixture f;const auto target=f.destination/"occupied.bin";write(target,"old");const auto state=read(f.selected.state);st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::ReplaceFile,{}};request.fileIndex=0;request.fileDestination=target;
    auto plan=take(st::prepareFileOperation(f.lease,request));const auto backup=plan.preview().replacement->path;const auto confirmation=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,true));interruptAt=st::FileOperationStage::PreimageDirectorySealed;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(plan),confirmation,interrupt),"interrupt before prior destination move");fs::remove(f.selected.files[0]);write(f.selected.files[0],"other");check(!st::recoverFileOperation(f.lease)&&read(f.selected.files[0])=="other"&&read(target)=="old"&&!fs::exists(backup)&&read(f.selected.state)==state,"unknown replacement source refuses without moving prior destination or publishing state");}
  {Fixture f;const auto target=f.destination/"occupied.bin";write(target,"old");st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::ReplaceFile,{}};request.fileIndex=0;request.fileDestination=target;
    auto plan=take(st::prepareFileOperation(f.lease,request));const auto confirmation=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,true));write(target,"changed");check(!st::applyFileOperation(f.lease,std::move(plan),confirmation)&&!st::hasPendingFileOperation(f.lease)&&read(target)=="changed"&&read(f.selected.files[0])=="hello","changed reviewed destination refuses before journal/effects");}
}

fs::path unsealedDirectory;bool createUnsealed=false;
void interruptWithUnsealedDirectory(st::FileOperationStage stage){
  if(createUnsealed&&stage==st::FileOperationStage::JournalSynced){createUnsealed=false;fs::create_directory(unsealedDirectory);write(unsealedDirectory/"unknown","left untouched");throw std::runtime_error("simulated interruption before directory identity publication");}
}
void interruptedUnsealedReplacementNeedsFreshReviewAndJournalCas(){
  Fixture f(true);const auto target=f.destination/"occupied.bin";write(target,"old");const auto state=read(f.selected.state),inventory=read(f.profile/"downloads.config");
  st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::ReplaceFile,{}};request.fileIndex=0;request.fileDestination=target;
  auto initial=take(st::prepareFileOperation(f.lease,request));const auto oldScope=initial.preview().approvalScopeDigest;unsealedDirectory=initial.preview().replacement->path.parent_path();createUnsealed=true;const auto consent=take(st::confirmFileOperation(initial,oldScope,true));
  check(!st::applyFileOperation(f.lease,std::move(initial),consent,interruptWithUnsealedDirectory)&&!st::recoverFileOperation(f.lease),"unsealed directory cannot be adopted by normal recovery");
  const auto pending=f.profile/"BiglyBTC.file-operation";
  const auto originalJournal=read(pending);auto review=take(st::prepareInterruptedReplacementReview(f.lease));const auto fresh=review.preview().replacement->path;
  check(review.preview().unsealedPreimageDirectory==unsealedDirectory&&fresh.parent_path()!=unsealedDirectory&&!fs::exists(fresh.parent_path()),"review shows a new absent backup name and abandoned directory");
  check(read(pending)==originalJournal&&read(target)=="old"&&read(f.selected.files[0])=="hello"&&read(f.selected.state)==state&&read(f.profile/"downloads.config")==inventory&&read(unsealedDirectory/"unknown")=="left untouched","review performs no profile or payload effects");
  check(!st::confirmFileOperation(review,oldScope,true)&&!st::confirmFileOperation(review,review.preview().approvalScopeDigest,false),"old consent and absent replacement consent cannot transfer");
  const auto accepted=take(st::confirmFileOperation(review,review.preview().approvalScopeDigest,true));std::stop_source cancelled;cancelled.request_stop();check(!st::applyFileOperation(f.lease,std::move(review),accepted,nullptr,cancelled.get_token())&&read(pending)==originalJournal&&!fs::exists(fresh.parent_path()),"cancel leaves the original journal and unknown directory intact");
  auto stale=take(st::prepareInterruptedReplacementReview(f.lease));const auto staleAccepted=take(st::confirmFileOperation(stale,stale.preview().approvalScopeDigest,true));fs::rename(pending,f.profile/"retained-journal-before-review");write(pending,originalJournal);check(::chmod(pending.c_str(),0600)==0,"private replacement journal fixture");check(!st::applyFileOperation(f.lease,std::move(stale),staleAccepted)&&!fs::exists(fresh.parent_path()),"same-byte replaced journal inode/ctime cannot borrow old review token");
  auto finalPlan=take(st::prepareInterruptedReplacementReview(f.lease));const auto finalAccepted=take(st::confirmFileOperation(finalPlan,finalPlan.preview().approvalScopeDigest,true));interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;
  check(!st::applyFileOperation(f.lease,std::move(finalPlan),finalAccepted,interrupt)&&read(pending)!=originalJournal&&!fs::exists(fresh.parent_path()),"new confirmed journal is durable before any new payload effect");
  const auto result=take(st::recoverFileOperation(f.lease));check(result&&result->retainedPreimage==fresh&&read(fresh)=="old"&&read(target)=="hello"&&!fs::exists(f.selected.files[0])&&read(unsealedDirectory/"unknown")=="left untouched"&&!st::hasPendingFileOperation(f.lease),"cold recovery uses fresh sealed backup and never adopts/deletes uncertain directory");
}
void interruptedReplacementReviewRefusesChangedPreimages(){
  for(unsigned changed=0;changed<4;++changed){Fixture f;const auto target=f.destination/"occupied.bin";write(target,"old");st::FileOperationRequest request{f.selected.hash,st::FileOperationKind::ReplaceFile,{}};request.fileIndex=0;request.fileDestination=target;
    auto initial=take(st::prepareFileOperation(f.lease,request));unsealedDirectory=initial.preview().replacement->path.parent_path();createUnsealed=true;const auto accepted=take(st::confirmFileOperation(initial,initial.preview().approvalScopeDigest,true));check(!st::applyFileOperation(f.lease,std::move(initial),accepted,interruptWithUnsealedDirectory),"owned unsealed interruption");
    if(changed==0){write(f.selected.files[0],"other");}else if(changed==1){write(target,"new");}else if(changed==2){auto state=doc(read(f.selected.state));put(state,"unrelated.new","i1e");write(f.selected.state,state.serialize());}else{write(control(f,0),"new control");}
    const auto source=read(f.selected.files[0]),prior=read(target),state=read(f.selected.state),inventory=read(f.profile/"downloads.config");check(!st::prepareInterruptedReplacementReview(f.lease)&&read(f.selected.files[0])==source&&read(target)==prior&&read(f.selected.state)==state&&read(f.profile/"downloads.config")==inventory&&read(unsealedDirectory/"unknown")=="left untouched"&&st::hasPendingFileOperation(f.lease),"changed source/destination/document/control refuses explicit review without effects");
  }
}


void showAllFileOperationBindsOriginalRootAndRecoveryContext(){
  Fixture f;auto state=doc(f.selected.metadata),info=doc(*core::encodeValue(*state.find("info")));put(info,"name",b(std::string(1,'\x80')));const auto rawInfo=info.serialize();const auto oldState=f.selected.state,oldFile=f.selected.files[0];
  f.selected.hash=*core::TorrentHash::parse(sha1(rawInfo));f.selected.state=f.profile/"active"/(f.selected.hash.hex()+".dat");put(state,"info",rawInfo);put(state,"encoding",b("IBM866"));f.selected.metadata=state.serialize();write(f.selected.state,f.selected.metadata);write(f.selected.archive,f.selected.metadata);check(fs::remove(oldState),"Retire generated old source identity");
  auto row=doc(f.selected.row);put(row,"torrent_hash",b(sha1(rawInfo)));put(row,"save_file",b("А"));f.selected.row=row.serialize();f.inventory();f.selected.files[0]=f.source/"А";fs::rename(oldFile,f.selected.files[0]);
  check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::Relocate,f.destination}),"Unregistered IBM866 cannot authorize a move");write(f.profile/"biglybt.config","d20:File.Decoder.ShowAlli1ee");auto plan=f.plan(st::FileOperationKind::Relocate);const auto accepted=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(plan),accepted,interrupt),"Own accepted actual original charset move");
  const auto journal=doc(read(f.profile/"BiglyBTC.file-operation"));auto body=doc(std::string(journal.find("payload")->bytes));check(body.find("decoderShowAll")&&body.find("decoderShowAll")->integer==1,"File operation persists explicit registry context");const auto inventory=read(f.profile/"downloads.config"),source=read(f.selected.files[0]);write(f.profile/"biglybt.config","d20:File.Decoder.ShowAlli0ee");check(!st::recoverFileOperation(f.lease)&&read(f.profile/"downloads.config")==inventory&&read(f.selected.files[0])==source&&!fs::exists(f.destination/"А"),"Permission change refuses before file operation effects");write(f.profile/"biglybt.config","d20:File.Decoder.ShowAlli1ee");const auto result=take(st::recoverFileOperation(f.lease));check(result&&read(f.destination/"А")==source&&!fs::exists(f.selected.files[0])&&*core::encodeValue(*doc(read(f.selected.state)).find("info"))==rawInfo,"Cold move replay preserves actual decoded root and exact info");
}


void coldPerFileJournalKeepsRootAndAuthenticatesEachDestination(){
  for(const auto kind:{st::FileOperationKind::MoveFile,st::FileOperationKind::RetargetFile,st::FileOperationKind::RevertFile,st::FileOperationKind::ReplaceFile}){
    Fixture f;const auto original=f.selected.files[0];const auto bytes=read(original);auto destination=f.destination/"mapped.bin";
    if(kind==st::FileOperationKind::RevertFile){
      st::FileOperationRequest move{f.selected.hash,st::FileOperationKind::MoveFile,{}};move.fileIndex=0;move.fileDestination=destination;
      auto initial=take(st::prepareFileOperation(f.lease,move));const auto accepted=take(st::confirmFileOperation(initial,initial.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(initial),accepted));destination=original;
    }else if(kind==st::FileOperationKind::RetargetFile){write(destination,bytes);}
    else if(kind==st::FileOperationKind::ReplaceFile){write(destination,"prior destination");}
    st::FileOperationRequest request{f.selected.hash,kind,{}};request.fileIndex=0;if(kind!=st::FileOperationKind::RevertFile){request.fileDestination=destination;}
    auto plan=take(st::prepareFileOperation(f.lease,request));check(plan.preview().sourceRoot==plan.preview().targetRoot&&plan.preview().files.size()==1&&plan.preview().files[0].destination==destination,"One-file operation retains the torrent root and authenticates a distinct leaf");const auto source=plan.preview().files[0].source;const auto backup=plan.preview().replacement?std::optional{plan.preview().replacement->path}:std::nullopt;
    const auto consent=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,kind==st::FileOperationKind::ReplaceFile));interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(plan),consent,interrupt),"Own confirmed one-file cold-recovery interval");
    const auto outer=doc(read(f.profile/"BiglyBTC.file-operation"));const auto body=doc(std::string(outer.find("payload")->bytes));check(body.find("source")->bytes==body.find("target")->bytes,"Actual durable journal records the unchanged torrent root");
    const auto result=take(st::recoverFileOperation(f.lease));check(result&&result->kind==kind&&read(destination)==bytes&&!st::hasPendingFileOperation(f.lease),"Cold replay accepts the root invariant and publishes only the reviewed leaf");
    if(kind==st::FileOperationKind::RetargetFile){check(read(source)==bytes&&result->filesMoved==0,"Retarget preserves the old source during cold replay");}
    else{check(!fs::exists(source)&&result->filesMoved==1,"Move/revert/replacement retire the exact source after publication");}
    if(backup){check(read(*backup)=="prior destination"&&result->retainedPreimage==backup,"Replacement cold replay retains the reviewed prior target");}
    const auto inventory=doc(read(f.profile/"downloads.config"));const auto& row=inventory.find("downloads")->children[0];check(row.find("forceStart")->integer==1&&row.find("state")->integer==0&&row.find("downloaded")->integer==11&&row.find("opaque.row"),"Cold per-file replay preserves saved intent/counters/opaque state");
  }
}

int main(){try{coldPerFileJournalKeepsRootAndAuthenticatesEachDestination();showAllFileOperationBindsOriginalRootAndRecoveryContext();interruptedUnsealedReplacementNeedsFreshReviewAndJournalCas();interruptedReplacementReviewRefusesChangedPreimages();explicitReplacementPreservesReviewedPriorInodeAndOriginalIntent();explicitReplacementColdRecoveryAndUnknownSources();trashDirectoryReplacementNeverFallsBackToDelete();perFileNonlinearMoveKeepsControlAndRetargetRefuses();individualFileMappingsAndRevert();individualFileMappingRecoveryAndRefusals();ownedTrashPublicationAndRecovery();manyPayloadDigestTraversal();canonicalLatin1MoveUsesActualDecodedLeaves();archiveControlsAndRecovery();digestReceiptsEndWithApply();payloadDigestWorkAndInterference();selectionPoliciesMoveAndRecover();selectionCollisionAndStaleSource();keepAndDelete();relocationRecoveryPreservesBothUntilPublication();relinkAndScope();protectionAndConflicts();realCopyBranch();nonlinearControlsAndRecovery();nonlinearBoundaryRefusals();authenticatedExternalLinks();legacyRootsAndDefaultStoreRecovery();pendingReorderedCompletionGuards();std::cout<<"ten generated file-operation families passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
