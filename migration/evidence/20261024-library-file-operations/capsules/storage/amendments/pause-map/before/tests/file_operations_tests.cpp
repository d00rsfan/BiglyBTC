#include "file_operations.hpp"
#include "encoded_document.hpp"
#include "file_operation_io.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace core=biglybtc::core;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error>&& value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
core::Document doc(std::string bytes){auto out=core::Document::parse(std::move(bytes),st::compatibleConfigLimits());check(out.has_value(),"parse fixture");return std::move(*out);}
void put(core::Document& d,std::string_view key,std::string value){check(d.replaceValue(key,std::move(value)).has_value(),"edit fixture");}
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);check(bool(input),"read fixture");return {std::istreambuf_iterator<char>(input),{}};}
void write(const fs::path& path,std::string_view bytes){fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(output),"write fixture");}
std::string sha1(std::string_view bytes){std::array<unsigned char,EVP_MAX_MD_SIZE> raw{};unsigned size=0;check(EVP_Digest(bytes.data(),bytes.size(),raw.data(),&size,EVP_sha1(),nullptr)==1&&size==20,"fixture sha1");return {reinterpret_cast<const char*>(raw.data()),size};}
struct Area{fs::path root;Area(){char path[]="/tmp/biglybtc-fileops-XXXXXX";const auto* made=::mkdtemp(path);check(made,"mkdtemp");root=made;}~Area(){std::error_code e;fs::remove_all(root,e);}};
struct Record{core::TorrentHash hash;std::string row,metadata;fs::path state,archive,save;std::vector<fs::path> files;std::vector<std::string> values;};
Record record(const fs::path& profile,const fs::path& save,std::string name,bool multi){
  Record out;out.save=save;out.values=multi?std::vector<std::string>{"hello","world!"}:std::vector<std::string>{"hello"};std::string all;for(const auto& v:out.values){all+=v;}std::string pieces;for(std::size_t i=0;i<all.size();i+=4){pieces+=sha1(std::string_view(all).substr(i,std::min<std::size_t>(4,all.size()-i)));}
  std::string info="d";if(multi){info+="5:filesl";for(std::size_t i=0;i<out.values.size();++i){const auto file=i?"second.bin":"first.bin";info+="d6:lengthi"+std::to_string(out.values[i].size())+"e4:pathl"+b(file)+"ee";out.files.push_back(save/name/file);}info+='e';}else{info+="6:lengthi5e";out.files.push_back(save/name);}info+="4:name"+b(name)+"12:piece lengthi4e6:pieces"+b(pieces)+'e';auto hash=core::TorrentHash::parse(sha1(info));check(hash.has_value(),"fixture hash");out.hash=*hash;
  auto state=doc("d4:info"+info+"e");std::string stores="l";for(std::size_t i=0;i<out.values.size();++i){stores+="1:L";}auto attrs=doc("de");put(attrs,"storetypes",stores+'e');put(attrs,"opaque.state","li7e3:rawe");put(attrs,"resumecomplete","i2e");put(state,"attributes",attrs.serialize());auto data=doc("de");put(data,"resume data",b(std::string(pieces.size()/20,'\1')));put(data,"valid","i1e");auto resume=doc("de");put(resume,"data",data.serialize());put(state,"resume",resume.serialize());out.metadata=state.serialize();out.state=profile/"active"/(out.hash.hex()+".dat");out.archive=profile.parent_path()/(name+".torrent");write(out.state,out.metadata);write(out.archive,out.metadata);
  for(std::size_t i=0;i<out.files.size();++i){write(out.files[i],out.values[i]);}auto row=doc("de");put(row,"torrent",b(out.archive.string()));put(row,"torrent_hash",b(sha1(info)));put(row,"save_dir",b(save.string()));put(row,"save_file",b(name));put(row,"state","i0e");put(row,"forceStart","i1e");put(row,"downloaded","i11e");put(row,"file_priorities",multi?"li1ei2ee":"li1ee");put(row,"opaque.row","d4:keep3:rawe");out.row=row.serialize();return out;
}
struct Fixture{Area area;fs::path profile,source,destination;Record selected;st::ProfileLease lease;
  explicit Fixture(bool multi=false):profile(area.root/"profile"),source(area.root/"source"),destination(area.root/"destination"),selected(record(profile,source,multi?"bundle":"file.bin",multi)),lease(take(st::ProfileLease::acquire(profile))){fs::create_directories(destination);inventory();}
  void inventory(std::string extra={}){write(profile/"downloads.config","d9:downloadsl"+selected.row+extra+"e6:opaque3:rawe");}
  st::PreparedFileOperation plan(st::FileOperationKind kind){return take(st::prepareFileOperation(lease,{selected.hash,kind,kind==st::FileOperationKind::Relocate||kind==st::FileOperationKind::Relink?std::optional<fs::path>(destination):std::nullopt}));}
};
// Actual source and destination bytes use real filesystem IO. Only the link
// syscall result is injected, to deterministically exercise the EXDEV copy path.
bool forceCopy=false;dev_t copyFromDev=0,copyToDev=0;ino_t copyFromIno=0,copyToIno=0;unsigned copyHits=0;
st::FileOperationStage interruptAt=st::FileOperationStage::Completed;bool interruptArmed=false;
void interrupt(st::FileOperationStage stage){if(interruptArmed&&stage==interruptAt){interruptArmed=false;throw std::runtime_error("injected stage interruption");}}
void keepAndDelete(){
  {Fixture f;const auto bytes=read(f.selected.files[0]),archive=read(f.selected.archive);auto p=f.plan(st::FileOperationKind::RemoveKeepData);check(p.preview().presentFiles==1&&p.preview().presentBytes==5,"keep preview facts");auto confirmation=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));auto result=take(st::applyFileOperation(f.lease,std::move(p),confirmation));check(result.filesDeleted==0&&read(f.selected.files[0])==bytes&&read(f.selected.archive)==archive,"keep preserves bytes/archive");check(!fs::exists(f.selected.state)&&!st::hasPendingFileOperation(f.lease),"active state removed last and journal gone");auto inventory=doc(read(f.profile/"downloads.config"));check(inventory.find("downloads")->children.empty()&&inventory.find("opaque")->bytes=="raw","selected row removed unknown preserved");}
  {Fixture f(true);write(f.source/"bundle/foreign.txt","foreign");auto p=f.plan(st::FileOperationKind::RemoveDeleteData);check(p.preview().presentFiles==2,"delete plan exact leaf count");check(!st::confirmFileOperation(p,p.preview().approvalScopeDigest,false),"delete requires explicit confirmation");check(fs::exists(f.selected.files[0]),"rejection no mutation");auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));auto r=take(st::applyFileOperation(f.lease,std::move(p),c));check(r.filesDeleted==2&&!fs::exists(f.selected.files[0])&&!fs::exists(f.selected.files[1])&&read(f.source/"bundle/foreign.txt")=="foreign"&&fs::exists(f.selected.archive),"delete touches only owned listed leaves");}
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
void realCopyBranch(){
  Fixture f;struct stat from{},to{};check(::stat(f.source.c_str(),&from)==0&&::stat(f.destination.c_str(),&to)==0,"copy fixture dirs");copyFromDev=from.st_dev;copyFromIno=from.st_ino;copyToDev=to.st_dev;copyToIno=to.st_ino;copyHits=0;forceCopy=true;auto p=f.plan(st::FileOperationKind::Relocate);auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));auto result=st::applyFileOperation(f.lease,std::move(p),c);forceCopy=false;take(std::move(result));check(copyHits>0&&read(f.destination/"file.bin")=="hello"&&!fs::exists(f.source/"file.bin"),"EXDEV fallback copies actual bytes and removes source only after publication");
}
}
extern "C" int __real_linkat(int,const char*,int,const char*,int);
extern "C" int __wrap_linkat(int a,const char* x,int b,const char* y,int flags){struct stat from{},to{};if(forceCopy&&std::string_view(x)=="file.bin"&&std::string_view(y)=="file.bin"&&::fstat(a,&from)==0&&::fstat(b,&to)==0&&from.st_dev==copyFromDev&&from.st_ino==copyFromIno&&to.st_dev==copyToDev&&to.st_ino==copyToIno){++copyHits;errno=EXDEV;return -1;}return __real_linkat(a,x,b,y,flags);}
int main(){try{keepAndDelete();relocationRecoveryPreservesBothUntilPublication();relinkAndScope();protectionAndConflicts();realCopyBranch();std::cout<<"five generated file-operation families passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
