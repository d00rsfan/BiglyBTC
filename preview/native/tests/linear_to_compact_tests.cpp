#include "linear_to_compact.hpp"
#include "encoded_document.hpp"
#include "file_operations.hpp"
#include "file_policy_commands.hpp"
#include "original_file_io.hpp"
#include "biglybtc/file_policy.hpp"

#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace core=biglybtc::core;
namespace compat=biglybtc::compat;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
std::string b(std::string_view bytes){return std::to_string(bytes.size())+':'+std::string(bytes);}
std::string n(std::uint64_t value){return 'i'+std::to_string(value)+'e';}
core::Document doc(std::string bytes){auto d=core::Document::parse(std::move(bytes),st::compatibleConfigLimits());check(d.has_value(),"generated dictionary");return std::move(*d);}
void put(core::Document& d,std::string_view key,std::string bytes){check(d.replaceValue(key,std::move(bytes)).has_value(),"edit fixture field");}
std::string read(const fs::path& path){std::ifstream f(path,std::ios::binary);check(bool(f),"read owned fixture");return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path& path,std::string_view data){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(data.data(),static_cast<std::streamsize>(data.size()));check(bool(f),"write owned fixture");}
std::string sha1(std::string_view bytes){std::array<unsigned char,EVP_MAX_MD_SIZE> raw{};unsigned size=0;check(EVP_Digest(bytes.data(),bytes.size(),raw.data(),&size,EVP_sha1(),nullptr)==1&&size==20,"fixture hash");return {reinterpret_cast<const char*>(raw.data()),size};}
struct Area{fs::path root;Area(){char name[]="/tmp/biglybtc-compact-XXXXXX";auto* p=::mkdtemp(name);check(p,"owned directory");root=p;}~Area(){std::error_code e;fs::remove_all(root,e);}};
struct Fixture{
  Area area;fs::path profile=area.root/"profile",source=area.root/"payload/bundle",state,control;
  core::TorrentHash hash;std::array<std::string,3> contents;st::ProfileLease lease;
  explicit Fixture(std::size_t targetLength=40000,std::uint32_t piece=16384)
      :contents{std::string(3072,'a'),std::string(targetLength,'\0'),std::string(6000,'z')},lease(make(piece)){}
  st::ProfileLease make(std::uint32_t piece){
    for(std::size_t k=0;k<contents[1].size();++k){contents[1][k]=static_cast<char>((k*17+3)%251);}
    std::string files="l",all;for(std::size_t k=0;k<contents.size();++k){files+="d6:length"+n(contents[k].size())+"4:pathl"+b(std::to_string(k)+".bin")+"ee";all+=contents[k];write(source/(std::to_string(k)+".bin"),contents[k]);}
    std::string pieces;for(std::size_t k=0;k<all.size();k+=piece){pieces+=sha1(std::string_view(all).substr(k,std::min<std::size_t>(piece,all.size()-k)));}
    auto info=doc("de");put(info,"files",files+'e');put(info,"name",b("bundle"));put(info,"piece length",n(piece));put(info,"pieces",b(pieces));hash=*core::TorrentHash::parse(sha1(info.serialize()));
    state=profile/"active"/(hash.hex()+".dat");control=profile/"active"/hash.hex()/"fmfile1.dat";
    auto attrs=doc("de");put(attrs,"storetypes","l1:L1:L1:Le");put(attrs,"resumecomplete","i2e");put(attrs,"opaque.state","li7e3:rawe");
    auto counts=doc("de");put(counts,"downloaded","l"+n(contents[0].size())+n(contents[1].size())+n(contents[2].size())+'e');put(counts,"opaque.counts","i91e");put(attrs,"filedownloaded",counts.serialize());
    auto data=doc("de");put(data,"resume data",b(std::string(pieces.size()/20,'\1')));put(data,"valid","i1e");put(data,"blocks","d1:ai1ee");auto resume=doc("de");put(resume,"data",data.serialize());put(resume,"opaque.resume",b("kept"));
    auto metadata=doc("de");put(metadata,"info",info.serialize());put(metadata,"attributes",attrs.serialize());put(metadata,"resume",resume.serialize());put(metadata,"opaque.root",b(std::string("\0\xff",2)));write(state,metadata.serialize());
    auto entry=doc("de");put(entry,"torrent_hash",b(sha1(info.serialize())));put(entry,"torrent",b((area.root/"archive.torrent").string()));write(area.root/"archive.torrent",metadata.serialize());
    put(entry,"save_dir",b((area.root/"payload").string()));put(entry,"save_file",b("bundle"));put(entry,"state","i70e");put(entry,"persistent","i1e");put(entry,"position","i2e");put(entry,"forceStart","i1e");put(entry,"file_priorities","li-1ei-1ei-1ee");
    put(entry,"downloaded","i777777e");put(entry,"uploaded","i123456e");put(entry,"secondsDownloading","i345e");put(entry,"completedbytes",n(all.size()));put(entry,"opaque.row","d4:keep3:rawe");
    write(profile/"downloads.config","d9:downloadsl"+entry.serialize()+"e11:opaque.rooti123ee");
    return take(st::ProfileLease::acquire(profile));
  }
  fs::path target()const{return source/"1.bin";}
  fs::path journal()const{return profile/".BiglyBTC.linear-to-compact";}
  std::string expected()const{
    const std::size_t first=std::min<std::size_t>(contents[1].size(),13312);
    const auto tail=(contents[1].size()-first)%16384;
    return contents[1].substr(0,first)+contents[1].substr(contents[1].size()-tail);
  }
};

enum class Fault{None,PartialWrite,Truncate,Sync,JournalSync};
Fault fault=Fault::None;dev_t device=0;ino_t inode=0;unsigned hits=0;bool halfWritten=false;
bool targetFd(int fd){struct stat s{};return ::fstat(fd,&s)==0&&s.st_dev==device&&s.st_ino==inode;}
void arm(Fixture& f,Fault selected){struct stat s{};check(::stat(f.target().c_str(),&s)==0,"fault identity");device=s.st_dev;inode=s.st_ino;fault=selected;hits=0;halfWritten=false;}
st::OriginalCompactionStage interruptAt=st::OriginalCompactionStage::Completed;bool interruptArmed=false;
void interrupt(st::OriginalCompactionStage stage){if(interruptArmed&&stage==interruptAt){interruptArmed=false;throw std::runtime_error("owned interruption");}}
void verify(Fixture& f){
  const auto packed=f.expected();check(read(f.target())==packed,"preserved first/tail actual bytes");check(read(f.source/"0.bin")==f.contents[0]&&read(f.source/"2.bin")==f.contents[2],"other selected payload unchanged");
  auto ctl=doc(read(f.control));check(ctl.find("version")->integer==0&&ctl.find("length")->integer==static_cast<std::int64_t>(f.contents[1].size()),"original compact control logical length");
  auto state=st::EncodedDocument::parse(read(f.state),st::compatibleConfigLimits());check(state.has_value(),"state parse");const auto* attrs=state->document().find("attributes");
  check(attrs->find("storetypes")->children[1].bytes=="C"&&attrs->find("storetypes")->children[0].bytes=="L"&&attrs->find("storetypes")->children[2].bytes=="L","only requested store changes");
  check(attrs->find("opaque.state")&&attrs->find("filedownloaded")->find("opaque.counts")->integer==91&&state->document().find("opaque.root")->bytes==std::string_view("\0\xff",2),"opaque original state preserved");
  check(attrs->find("filedownloaded")->find("downloaded")->children[1].integer==0,"discarded target is not falsely complete");
  const auto* resume=state->document().find("resume");const auto* data=resume->find("data");check(data->find("valid")->integer==0&&std::ranges::all_of(data->find("resume data")->bytes,[](char c){return c=='\2';})&&data->find("blocks")->children.empty()&&resume->find("opaque.resume")->bytes=="kept","all pieces require actual recheck");
  auto inventory=doc(read(f.profile/"downloads.config"));const auto& entry=inventory.find("downloads")->children[0];auto policy=compat::originalFilePolicy(entry,3);check(policy&&(*policy)[1].skipped&&!(*policy)[0].skipped&&!(*policy)[2].skipped,"explicit compact selects only target Skip");
  check(entry.find("downloaded")->integer==777777&&entry.find("uploaded")->integer==123456&&entry.find("secondsDownloading")->integer==345&&entry.find("forceStart")->integer==1&&entry.find("state")->integer==70&&entry.find("opaque.row"),"lifetime counters/intent/unknown row retained");
  check(entry.find("completedbytes")->integer==9072&&inventory.find("opaque.root")->integer==123,"only discarded per-file progress removed");
  const int fd=::open(f.target().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);check(fd>=0,"open compact reader");
  auto io=st::OriginalFileIo::open(fd,{3072,40000,16384},st::OriginalStorageKind::Compact,read(f.control));
  check(io.has_value(),"actual original compact reader reopens packed bytes");std::vector<std::byte> out(40000);auto observed=(*io).read(0,out);
  check(observed&&observed->missing==std::vector<st::OriginalMissingRange>{{13312,16384}},"discarded interior stays explicitly missing");
  check(std::equal(out.begin(),out.begin()+13312,reinterpret_cast<const std::byte*>(f.contents[1].data()))&&std::equal(out.begin()+29696,out.end(),reinterpret_cast<const std::byte*>(f.contents[1].data()+29696)),"independent codec reads first/tail at original logical positions");
  ::close(fd);check(!st::hasPendingOriginalCompaction(f.lease),"successful compaction retired its journal");
}
fs::path convertedCompactionFixture(Fixture& f,bool configured){
  auto state=doc(read(f.state));const auto* rawInfo=state.find("info");auto encodedInfo=core::encodeValue(*rawInfo,st::compatibleConfigLimits());check(encodedInfo.has_value(),"Own named compaction info");auto info=doc(*encodedInfo);std::string files="l";
  for(std::size_t j=0;j<info.find("files")->children.size();++j){auto encoded=core::encodeValue(info.find("files")->children[j],st::compatibleConfigLimits());check(encoded.has_value(),"Own compaction file");auto item=doc(*encoded);if(j==1){put(item,"path","l5:1\"bine");}files+=item.serialize();}put(info,"files",files+'e');put(state,"info",info.serialize());
  const auto old=f.state;f.hash=*core::TorrentHash::parse(sha1(info.serialize()));f.state=f.profile/"active"/(f.hash.hex()+".dat");f.control=f.profile/"active"/f.hash.hex()/"fmfile1.dat";write(f.state,state.serialize());check(fs::remove(old),"Retire obsolete generated source hash");write(f.area.root/"archive.torrent",state.serialize());
  auto inventory=doc(read(f.profile/"downloads.config"));auto encoded=core::encodeValue(inventory.find("downloads")->children[0],st::compatibleConfigLimits());check(encoded.has_value(),"Own named compaction row");auto row=doc(*encoded);put(row,"torrent_hash",b(sha1(info.serialize())));put(inventory,"downloads","l"+row.serialize()+'e');write(f.profile/"downloads.config",inventory.serialize());
  if(configured){write(f.profile/"biglybt.config","d26:File.Character.Conversions3:\"=Qe");}
  const auto target=f.source/(configured?"1Qbin":"1'bin");fs::rename(f.target(),target);return target;
}
void convertedFilenameCompactionRecoversExactPolicy(){
  for(const bool configured:{false,true}){for(const auto stage:{st::OriginalCompactionStage::JournalSynced,st::OriginalCompactionStage::PayloadSynced,st::OriginalCompactionStage::ControlPublished,st::OriginalCompactionStage::StatePublished,st::OriginalCompactionStage::InventoryPublished}){
    Fixture f;const auto target=convertedCompactionFixture(f,configured);const auto sourceInfo=core::encodeValue(*doc(read(f.state)).find("info"),st::compatibleConfigLimits());check(sourceInfo.has_value(),"Own exact quoted source info");interruptAt=stage;interruptArmed=true;check(!st::compactOriginalFile(f.lease,f.hash,1,{},interrupt)&&st::hasPendingOriginalCompaction(f.lease),"Accepted converted compaction interrupted");
    {auto release=std::move(f.lease);static_cast<void>(release);}f.lease=take(st::ProfileLease::acquire(f.profile));check(take(st::recoverOriginalCompaction(f.lease)).has_value(),"Unchanged named policy recovers every durable stage");check(read(target)==f.expected()&&!fs::exists(f.source/"1\"bin"),"Only exact converted physical file was compacted");auto after=doc(read(f.state));const auto restoredInfo=core::encodeValue(*after.find("info"),st::compatibleConfigLimits());check(restoredInfo&&*restoredInfo==*sourceInfo&&after.find("attributes")->find("storetypes")->children[1].bytes=="C","Raw hash subject/original index survive named compaction");
  }}
  {Fixture f;const auto target=convertedCompactionFixture(f,false);interruptAt=st::OriginalCompactionStage::JournalSynced;interruptArmed=true;check(!st::compactOriginalFile(f.lease,f.hash,1,{},interrupt),"Pending converted scope");const auto before=read(target);write(f.profile/"biglybt.config","d26:File.Character.Conversions3:\"=Xe");check(!st::recoverOriginalCompaction(f.lease)&&read(target)==before&&st::hasPendingOriginalCompaction(f.lease),"Config change refuses before payload packing");}
}
void exactPackAndResume(){Fixture f;const auto result=take(st::compactOriginalFile(f.lease,f.hash,1));check(result.previousPhysicalBytes==40000&&result.physicalBytes==23616&&!result.recovered,"actual compact result");verify(f);}
void interruptedStages(){
  for(const auto stage:{st::OriginalCompactionStage::JournalSynced,st::OriginalCompactionStage::PayloadSynced,st::OriginalCompactionStage::ControlPublished,st::OriginalCompactionStage::StatePublished,st::OriginalCompactionStage::InventoryPublished}){
    Fixture f;interruptAt=stage;interruptArmed=true;check(!st::compactOriginalFile(f.lease,f.hash,1,{},interrupt)&&st::hasPendingOriginalCompaction(f.lease),"durable interruption stays pending");
    // Drop and reacquire the actual lease to model a fresh native process.
    {auto released=std::move(f.lease);static_cast<void>(released);}
    f.lease=take(st::ProfileLease::acquire(f.profile));
    const auto recovered=take(st::recoverOriginalCompaction(f.lease));check(recovered&&recovered->recovered,"fresh held lease replays exact transaction");verify(f);
    check(!take(st::recoverOriginalCompaction(f.lease)).has_value(),"completed recovery is an actual no-op");
  }
}
void failedPayloadIoReopens(){
  for(const auto selected:{Fault::PartialWrite,Fault::Truncate,Fault::Sync}){
    Fixture f;arm(f,selected);auto result=st::compactOriginalFile(f.lease,f.hash,1);fault=Fault::None;
    check(!result&&hits>0&&st::hasPendingOriginalCompaction(f.lease),"actual payload IO failure preserves frozen tail journal");
    const auto recovered=take(st::recoverOriginalCompaction(f.lease));check(recovered&&recovered->recovered,"fresh replay succeeds after real syscall failure");verify(f);
  }
}
void journalCorruptionRefusesBeforePayload(){
  for(const auto mode:{0,1,2}){
    Fixture f;interruptAt=st::OriginalCompactionStage::JournalSynced;interruptArmed=true;check(!st::compactOriginalFile(f.lease,f.hash,1,{},interrupt),"freeze complete journal");const auto before=read(f.target());auto bytes=read(f.journal());
    if(mode==0){bytes.back()=bytes.back()=='0'?'1':'0';}else if(mode==1){bytes.pop_back();}else{bytes+='x';}write(f.journal(),bytes);
    auto result=st::recoverOriginalCompaction(f.lease);check(!result&&result.error().code==st::ErrorCode::InvalidFormat&&read(f.target())==before&&st::hasPendingOriginalCompaction(f.lease),"checksum/exact-body rejection cannot mutate payload");
  }
}
void changedAuthorityAndSourceRefuse(){
  for(const auto mode:{0,1,2,3}){
    Fixture f;interruptAt=st::OriginalCompactionStage::JournalSynced;interruptArmed=true;check(!st::compactOriginalFile(f.lease,f.hash,1,{},interrupt),"journal before authority change");
    if(mode==0){auto d=doc(read(f.profile/"downloads.config"));put(d,"foreign","i1e");write(f.profile/"downloads.config",d.serialize());}
    if(mode==1){auto d=doc(read(f.state));put(d,"foreign","i1e");write(f.state,d.serialize());}
    if(mode==2){const auto old=f.target().string()+".old";fs::rename(f.target(),old);write(f.target(),f.contents[1]);}
    if(mode==3){auto bytes=read(f.target());bytes[0]='x';write(f.target(),bytes);}
    const auto before=read(f.target());auto result=st::recoverOriginalCompaction(f.lease);check(!result&&result.error().code==st::ErrorCode::Conflict&&read(f.target())==before,"stale raw CAS/identity/prefix cannot replay");
  }
}
void missingShortAndAligned(){
  {Fixture f;fs::remove(f.target());auto r=take(st::compactOriginalFile(f.lease,f.hash,1));check(r.physicalBytes==0&&!fs::exists(f.target())&&!fs::exists(f.control),"missing original file remains absent and unverified");}
  {Fixture f;fs::resize_file(f.target(),8000);auto r=take(st::compactOriginalFile(f.lease,f.hash,1));check(r.physicalBytes==8000&&read(f.target())==f.contents[1].substr(0,8000),"short original prefix is never filled");auto io=doc(read(f.control));check(io.find("length")->integer==8000,"original control retains actual old length");}
  {Fixture f;fs::resize_file(f.target(),33000);auto r=take(st::compactOriginalFile(f.lease,f.hash,1));check(r.physicalBytes==16616&&read(f.target())==f.contents[1].substr(0,13312)+f.contents[1].substr(29696,3304),"short available tail preserved without padding to full tail");}
  {Fixture f(32768+13312);auto r=take(st::compactOriginalFile(f.lease,f.hash,1));check(r.physicalBytes==13312&&read(f.target())==f.contents[1].substr(0,13312),"aligned end has no invented last fragment");}
}
void streamedTailAndOrdinarySkip(){
  {Fixture f(359072,131072);const auto r=take(st::compactOriginalFile(f.lease,f.hash,1));check(r.physicalBytes==228000&&read(f.target())==f.contents[1].substr(0,128000)+f.contents[1].substr(259072),"tail larger than fixed scratch streams across chunks");}
  {Fixture f;const std::vector<compat::FilePolicy> policies{{false,0},{true,0},{false,0}};
    auto saved=st::setFilePolicies(f.lease,f.hash,policies);check(saved.has_value(),"ordinary Skip accepted");auto state=doc(read(f.state));
    check(read(f.target())==f.contents[1]&&state.find("attributes")->find("storetypes")->children[1].bytes=="L"&&!fs::exists(f.control)&&!st::hasPendingOriginalCompaction(f.lease),"plain Skip never silently compacts linear payload");}
}
void prepublicationIoAndInterruptedSaving(){
  {Fixture f;arm(f,Fault::JournalSync);auto failed=st::compactOriginalFile(f.lease,f.hash,1);fault=Fault::None;
    check(!failed&&hits==1&&read(f.target())==f.contents[1]&&!st::hasPendingOriginalCompaction(f.lease),"failed journal fsync cannot authorize packing; exact temporary cleaned");
    auto inventory=doc(read(f.profile/"downloads.config"));auto policy=compat::originalFilePolicy(inventory.find("downloads")->children[0],3);
    check(policy&&(*policy)[1].skipped,"accepted Skip may remain after compaction failure");
  }
  {Fixture f;write(f.profile/".BiglyBTC.linear-to-compact.saving","interrupted header");auto result=st::recoverOriginalCompaction(f.lease);
    check(!result&&result.error().code==st::ErrorCode::Conflict&&read(f.target())==f.contents[1]&&st::hasPendingOriginalCompaction(f.lease),"incomplete unpublished journal refuses admission without invented recovery");}
}
void pendingAdmissionGuards(){
  Fixture f;auto planned=take(st::prepareFileOperation(f.lease,{f.hash,st::FileOperationKind::RemoveKeepData,{}}));
  const auto approved=take(st::confirmFileOperation(planned,planned.preview().approvalScopeDigest,false));
  interruptAt=st::OriginalCompactionStage::JournalSynced;interruptArmed=true;check(!st::compactOriginalFile(f.lease,f.hash,1,{},interrupt),"pending compact intent before admission checks");
  const auto before=read(f.target());
  check(!st::prepareFileOperation(f.lease,{f.hash,st::FileOperationKind::RemoveKeepData,{}})
      &&!st::applyFileOperation(f.lease,std::move(planned),approved)&&!st::recoverFileOperation(f.lease),"prepared/new file operations cannot bypass pending compaction");
  const std::vector<compat::FilePolicy> normal(3);check(!st::setFilePolicies(f.lease,f.hash,normal)&&read(f.target())==before,"preference edits cannot invalidate pending intent");
  check(take(st::recoverOriginalCompaction(f.lease)).has_value(),"explicit stopped recovery re-enables original handoff");verify(f);
}
void cancellationAndUnsupportedLeavePayload(){
  Fixture f;std::stop_source stop;stop.request_stop();auto r=st::compactOriginalFile(f.lease,f.hash,1,stop.get_token());check(!r&&r.error().code==st::ErrorCode::Cancelled&&!st::hasPendingOriginalCompaction(f.lease)&&read(f.target())==f.contents[1],"pre-cancel no mutation");
  check(!st::compactOriginalFile(f.lease,f.hash,9)&&read(f.target())==f.contents[1],"invalid index before effects");
  const auto after=take(st::compactOriginalFile(f.lease,f.hash,1));static_cast<void>(after);const auto packed=read(f.target());auto again=st::compactOriginalFile(f.lease,f.hash,1);check(!again&&again.error().code==st::ErrorCode::Unsupported&&read(f.target())==packed,"C/R/X are not silently reconverted");
}
} // namespace

extern "C" ssize_t __real_pwrite(int,const void*,size_t,off_t);
extern "C" int __real_ftruncate(int,off_t);
extern "C" int __real_fsync(int);
extern "C" ssize_t __wrap_pwrite(int fd,const void* data,size_t size,off_t offset){
  if(fault==Fault::PartialWrite&&targetFd(fd)){
    ++hits;if(!halfWritten&&size>1){halfWritten=true;return __real_pwrite(fd,data,size/2,offset);}fault=Fault::None;errno=EIO;return -1;
  }
  return __real_pwrite(fd,data,size,offset);
}
extern "C" int __wrap_ftruncate(int fd,off_t size){if(fault==Fault::Truncate&&targetFd(fd)){++hits;fault=Fault::None;errno=EIO;return -1;}return __real_ftruncate(fd,size);}
extern "C" int __wrap_fsync(int fd){
  if(fault==Fault::JournalSync){
    const auto proc="/proc/self/fd/"+std::to_string(fd);std::array<char,4097> path{};const auto count=::readlink(proc.c_str(),path.data(),path.size());
    if(count>0&&static_cast<std::size_t>(count)<path.size()&&std::string_view(path.data(),static_cast<std::size_t>(count)).ends_with("/.BiglyBTC.linear-to-compact.saving")){++hits;fault=Fault::None;errno=EIO;return -1;}
  }
  if(fault==Fault::Sync&&targetFd(fd)){++hits;fault=Fault::None;errno=EIO;return -1;}return __real_fsync(fd);}


void compactionContextCannotChangeBetweenAcceptanceAndReplay(){
  Fixture f;write(f.profile/"biglybt.config","d20:File.Decoder.ShowAlli1ee");interruptAt=st::OriginalCompactionStage::JournalSynced;interruptArmed=true;check(!st::compactOriginalFile(f.lease,f.hash,1,{},interrupt),"Own actual compaction context journal");const auto before=read(f.target());write(f.profile/"biglybt.config","d20:File.Decoder.ShowAlli0ee");check(!st::recoverOriginalCompaction(f.lease)&&read(f.target())==before&&st::hasPendingOriginalCompaction(f.lease),"Changed registry capability cannot reinterpret accepted compaction");write(f.profile/"biglybt.config","d20:File.Decoder.ShowAlli1ee");check(take(st::recoverOriginalCompaction(f.lease)).has_value(),"Restored exact context replays");verify(f);
}

int main(){
  try{
    compactionContextCannotChangeBetweenAcceptanceAndReplay();
    convertedFilenameCompactionRecoversExactPolicy();
    exactPackAndResume();std::cout<<"PASS exact original compact edges and conservative resume\n";
    interruptedStages();std::cout<<"PASS all durable stages reopen\n";
    failedPayloadIoReopens();std::cout<<"PASS actual short-write truncation sync failures\n";
    journalCorruptionRefusesBeforePayload();std::cout<<"PASS exact journal checksum body bounds\n";
    changedAuthorityAndSourceRefuse();std::cout<<"PASS raw snapshot inode prefix conflicts\n";
    missingShortAndAligned();std::cout<<"PASS absent short aligned original files\n";
    streamedTailAndOrdinarySkip();std::cout<<"PASS streamed tail and no automatic Skip compaction\n";
    prepublicationIoAndInterruptedSaving();std::cout<<"PASS journal fsync and unpublished interruption refusal\n";
    pendingAdmissionGuards();std::cout<<"PASS pending compaction admission/edit fences\n";
    cancellationAndUnsupportedLeavePayload();std::cout<<"PASS cancellation and explicit L-only scope\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
