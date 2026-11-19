#include "original_selection_group.hpp"
#include "file_storage_selection_source.hpp"
#include "file_policy_commands.hpp"
#include "encoded_document.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/file_selection_intent.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#include <cstdlib>
#include <tuple>
namespace fs=std::filesystem;
namespace st=biglybtc::storage;namespace core=biglybtc::core;namespace compat=biglybtc::compat;
namespace {
unsigned assertions=0,families=0;
void check(bool value,const char* why){++assertions;if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void done(std::expected<void,st::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
std::string b(std::string_view s){return std::to_string(s.size())+':'+std::string(s);}
std::string i(std::size_t n){return 'i'+std::to_string(n)+'e';}
core::Document doc(std::string s){auto d=core::Document::parse(std::move(s),st::compatibleConfigLimits());check(d.has_value(),"fixture parse");return std::move(*d);}
std::string encode(const core::Node& n){auto e=core::encodeValue(n,st::compatibleConfigLimits());check(e.has_value(),"fixture encode");return std::move(*e);}
void put(core::Document& d,std::string_view key,std::string s){check(d.replaceValue(key,std::move(s)).has_value(),"fixture edit");}
std::string read(const fs::path& path){std::ifstream f(path,std::ios::binary);check(bool(f),"fixture read");return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path& path,std::string_view s){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(s.data(),static_cast<std::streamsize>(s.size()));check(bool(f),"fixture write");}
std::string sha1(std::string_view s){std::array<unsigned char,20> out{};unsigned size=0;check(EVP_Digest(s.data(),s.size(),out.data(),&size,EVP_sha1(),nullptr)==1&&size==out.size(),"fixture hash");return {reinterpret_cast<char*>(out.data()),out.size()};}
std::string gzip(std::string_view s){z_stream stream{};check(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK,"gzip setup");std::string out(compressBound(s.size())+64,'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(s.data()));stream.avail_in=static_cast<uInt>(s.size());stream.next_out=reinterpret_cast<Bytef*>(out.data());stream.avail_out=static_cast<uInt>(out.size());const auto status=deflate(&stream,Z_FINISH);const auto size=stream.total_out;deflateEnd(&stream);check(status==Z_STREAM_END,"gzip finish");out.resize(size);return out;}
struct Area {fs::path root;Area(){char name[]="/tmp/biglybtc-selection-group-XXXXXX";auto* p=::mkdtemp(name);check(p!=nullptr,"fixture area");root=p;}~Area(){std::error_code error;fs::remove_all(root,error);}};
struct Fixture {
  Area area;fs::path profile=area.root/"profile",payloadRoot=area.root/"payload/bundle",state,controls;
  core::TorrentHash hash;std::optional<st::ProfileLease> lease;
  std::string packed=std::string(11920,'A')+std::string(5312,'B'),linear,xData,xControl,originalRow;
  std::vector<compat::FilePolicy> desired{{false,1},{false,0},{false,2}};
  bool compressed,legacy;
  Fixture(bool compressed_=false,bool legacy_=false,bool noResume=false):compressed(compressed_),legacy(legacy_){
    const std::array<std::size_t,3> sizes{70000,50000,11920};std::string files="l";for(std::size_t p=0;p<sizes.size();++p){files+="d6:length"+i(sizes[p])+"4:pathl"+b(std::to_string(p)+".bin")+"ee";}
    auto info=doc("de");put(info,"files",files+'e');put(info,"name",b("bundle"));put(info,"piece length","i16384e");put(info,"pieces",b(std::string(9*20,'\0')));hash=*core::TorrentHash::parse(sha1(info.serialize()));state=profile/"active"/(hash.hex()+".dat");controls=profile/"active"/hash.hex();
    auto attrs=doc("de");put(attrs,"storetypes","l1:X1:C1:Le");put(attrs,"opaque.attr","li7e3:rawe");put(attrs,"resumecomplete","i2e");
    auto data=doc("de");put(data,"valid","i1e");put(data,"resume data",b(std::string(9,'\1')));put(data,"blocks","d1:ai1ee");auto resume=doc("de");put(resume,"data",data.serialize());put(resume,"opaque.resume",b("kept"));auto full=doc("de");put(full,"announce",b("http://generated.invalid/announce"));put(full,"info",info.serialize());put(full,"attributes",attrs.serialize());if(!noResume){put(full,"resume",resume.serialize());}put(full,"opaque.root",b("kept"));write(state,compressed?gzip(full.serialize()):full.serialize());
    const std::vector<compat::FilePolicy> before{{true,0},{true,0},{false,2}};auto entry=doc("de");put(entry,"torrent_hash",b(sha1(info.serialize())));put(entry,"torrent",b(state.string()));put(entry,"save_dir",b((area.root/"payload").string()));put(entry,"save_file",b("bundle"));put(entry,"persistent","i1e");put(entry,"state","i70e");put(entry,"position","i5e");put(entry,"opaque.row",b("kept"));auto policy=compat::encodeOriginalFilePolicy(before);check(policy.has_value(),"fixture initial policy");put(entry,"file_priorities_c",*policy);originalRow=entry.serialize();write(profile/"downloads.config",legacy?"d3:one"+entry.serialize()+"e":"d9:downloadsl"+entry.serialize()+"e11:opaque.rooti123ee");
    write(path(0),"");write(path(1),packed);write(path(2),std::string(11920,'Z'));write(controls/"fmfile1.dat","d6:lengthi50000e7:versioni0ee");write(controls/"fmfile1.dat.bak","d6:lengthi50000e7:versioni0ee");
    lease.emplace(take(st::ProfileLease::acquire(profile)));auto owner=take(st::OriginalControl::open(*lease,hash.hex(),0,st::OriginalStorageKind::ReorderedCompact,false));int fd=::open(path(0).c_str(),O_RDWR|O_CLOEXEC);check(fd>=0,"fixture X fd");{
      auto io=take(st::OriginalFileIo::open(fd,{0,70000,16384},st::OriginalStorageKind::ReorderedCompact,std::nullopt,owner->publisher()));
      for(const auto&[start,size,letter]:std::array<std::tuple<std::size_t,std::size_t,char>,4>{{{32768,16384,'C'},{16384,16384,'B'},{0,16384,'A'},{65536,4464,'E'}}}){std::vector<std::byte> bytes(size,static_cast<std::byte>(letter));done(io.write(start,bytes));}done(io.flush());
    }check(::close(fd)==0,"fixture X close");xData=read(path(0));xControl=read(controls/"fmfile0.dat.2");linear=std::string(50000,'\0');linear.replace(0,packed.size(),packed);linear.replace(44688,5312,std::string(5312,'B'));
  }
  fs::path path(std::size_t index)const{return payloadRoot/(std::to_string(index)+".bin");}
  core::Document metadata()const{auto stateFile=st::EncodedDocument::parse(read(state),st::compatibleConfigLimits());check(stateFile.has_value(),"fixture state envelope");return doc(stateFile->document().serialize());}
  std::string row()const{auto d=doc(read(profile/"downloads.config"));return encode(legacy?d.root().children[1]:d.find("downloads")->children[0]);}
  st::FileOperationRequest request(){auto policy=compat::encodeOriginalFilePolicy(desired);auto mask=compat::encodeOriginalFileSelectionIntent(std::array<std::uint8_t,3>{1,1,0},false);check(policy&&mask,"fixture desired encoding");st::FileOperationRequest r{hash,st::FileOperationKind::SetFilePolicies,{},*policy,row()};r.selectionIntentEncoding=*mask;return r;}
  void cold(){lease.reset();lease.emplace(take(st::ProfileLease::acquire(profile)));}
  void verify(){check(read(path(1))==linear,"C packed bytes expanded exactly");check(read(path(0))==xData&&read(controls/"fmfile0.dat.2")==xControl,"X data/map unchanged including previous st4");check(read(path(2))==std::string(11920,'Z'),"adjacent untouched");check(!fs::exists(controls/"fmfile1.dat")&&!fs::exists(controls/"fmfile1.dat.bak"),"C candidates retired");auto stateFile=metadata();const auto* attrs=stateFile.find("attributes");check(attrs->find("storetypes")->children[0].bytes=="R"&&attrs->find("storetypes")->children[1].bytes=="L"&&attrs->find("storetypes")->children[2].bytes=="L","group requested kinds");check(attrs->find("opaque.attr")&&stateFile.find("opaque.root")->bytes=="kept","opaque state retained");const auto* data=stateFile.find("resume")->find("data");check(data->find("valid")->integer==0&&data->find("resume data")->bytes.size()==9&&data->find("blocks")->children.empty()&&std::ranges::all_of(data->find("resume data")->bytes,[](char c){return c=='\2';})&&attrs->find("resumecomplete")->integer==1,"conservative Original resume");auto entry=doc(row());auto policy=compat::originalFilePolicy(entry.root(),3);check(policy&&*policy==desired&&entry.find("opaque.row")->bytes=="kept"&&entry.find("position")->integer==5,"flags only after group,unrelated row retained");check(!st::hasPendingOriginalCompaction(*lease),"completed global journal retired");}
};
st::OriginalSelectionGroupStage cut=st::OriginalSelectionGroupStage::Completed;int remaining=1;
void interrupt(st::OriginalSelectionGroupStage stage){if(stage==cut&&--remaining==0){throw std::runtime_error("owned group cut");}}
st::OriginalStorageConversionStage conversionCut=st::OriginalStorageConversionStage::Completed;int conversionRemaining=1;
void interruptConversion(st::OriginalStorageConversionStage stage){if(stage==conversionCut&&--conversionRemaining==0){throw std::runtime_error("owned conversion cut");}}
void recover(Fixture& f){f.cold();auto result=take(st::recoverOriginalCompaction(*f.lease));check(result&&result->selectionExpanded&&result->convertedFiles==2&&result->previousPhysicalBytes==70848&&result->physicalBytes==103616,"shared stopped recovery dispatches group");f.verify();}
void exportFixture(const Fixture& f,std::string_view phase){const auto* configured=::getenv("BIGLYBTC_SELECTION_GROUP_EXPORT");if(!configured){return;}const auto target=fs::path(configured)/phase;write(target/"state.dat",read(f.state));for(std::size_t p=0;p<3;++p){write(target/"data"/(std::to_string(p)+".bin"),read(f.path(p)));}fs::create_directories(target/"control");for(const auto& entry:fs::directory_iterator(f.controls)){if(entry.is_regular_file()){write(target/"control"/entry.path().filename(),read(entry.path()));}}}
void normalCommands(){for(bool compressed:{false,true}){for(bool legacy:{false,true}){for(bool noResume:{false,true}){Fixture f(compressed,legacy,noResume);if(!compressed&&!legacy&&!noResume){exportFixture(f,"before");}const std::array<std::uint8_t,3> mask{1,1,0};done(st::setFilePolicies(*f.lease,f.hash,f.desired,st::FileSelectionIntent{mask,false}));f.verify();if(!compressed&&!legacy&&!noResume){exportFixture(f,"after");}++families;}}}}
void absentResumeCuts(){for(bool compressed:{false,true}){Fixture f(compressed,false,true);auto request=f.request();cut=st::OriginalSelectionGroupStage::ResumePublished;remaining=1;check(!st::convertOriginalSelectionGroup(*f.lease,request,interrupt),"absent resume cut after complete conservative publication");auto state=f.metadata();check(state.find("resume")->find("data")->find("resume data")->bytes==std::string(9,'\2')&&f.row()==f.originalRow,"complete derived piece map durable before physical conversion or flags");recover(f);++families;}
 Fixture f;auto metadata=f.metadata();auto resume=doc(encode(*metadata.find("resume")));auto data=doc(encode(*resume.find("data")));put(data,"resume data",b(std::string(3,'\1')));put(resume,"data",data.serialize());put(metadata,"resume",resume.serialize());write(f.state,metadata.serialize());auto request=f.request();check(take(st::convertOriginalSelectionGroup(*f.lease,request)),"short old map replaced with complete unverified census");f.verify();++families;
}

void groupCuts(){for(bool compressed:{false,true}){for(const auto stage:{st::OriginalSelectionGroupStage::SavingSynced,st::OriginalSelectionGroupStage::JournalSynced,st::OriginalSelectionGroupStage::ResumePublished,st::OriginalSelectionGroupStage::FileConverted,st::OriginalSelectionGroupStage::PoliciesPublished,st::OriginalSelectionGroupStage::Completed}){for(int nth=1;nth<=(stage==st::OriginalSelectionGroupStage::FileConverted?2:1);++nth){Fixture f(compressed);cut=stage;remaining=nth;auto request=f.request();auto result=st::convertOriginalSelectionGroup(*f.lease,request,interrupt);check(!result,"group cut reached");
    if(stage==st::OriginalSelectionGroupStage::FileConverted&&nth==1){auto state=f.metadata();check(state.find("attributes")->find("storetypes")->children[1].bytes=="L"&&state.find("attributes")->find("storetypes")->children[0].bytes=="X"&&f.row()==f.originalRow,"C before lower-index X and before flags");}
    if(stage!=st::OriginalSelectionGroupStage::Completed){check(st::hasPendingOriginalCompaction(*f.lease),"interrupted group fenced");check(!st::setFilePolicies(*f.lease,f.hash,f.desired),"ordinary command cannot bypass pending group");recover(f);}else{f.verify();}++families;
  }}}}
void conversionCuts(){for(bool compressed:{false,true}){for(const auto&[stage,nth]:std::array<std::pair<st::OriginalStorageConversionStage,int>,9>{{{st::OriginalStorageConversionStage::IntentPublished,1},{st::OriginalStorageConversionStage::PayloadSynced,1},{st::OriginalStorageConversionStage::TypePublished,1},{st::OriginalStorageConversionStage::ControlRetired,1},{st::OriginalStorageConversionStage::Completed,1},{st::OriginalStorageConversionStage::ResumePublished,1},{st::OriginalStorageConversionStage::ControlPublished,1},{st::OriginalStorageConversionStage::TypePublished,2},{st::OriginalStorageConversionStage::Completed,2}}}){Fixture f(compressed);conversionCut=stage;conversionRemaining=nth;auto request=f.request();check(!st::convertOriginalSelectionGroup(*f.lease,request,nullptr,interruptConversion),"primitive cut reached in group");check(f.row()==f.originalRow,"primitive partial receipts never change flags");recover(f);++families;}}}
void refusalAndStale(){for(unsigned mode=0;mode<8;++mode){Fixture f;auto request=f.request();const auto state=read(f.state),inventory=read(f.profile/"downloads.config"),c=read(f.path(1));
  if(mode==0){fs::remove(f.path(0));}
  if(mode==1){fs::remove(f.controls/"fmfile1.dat");fs::remove(f.controls/"fmfile1.dat.bak");}
  if(mode==2){write(f.controls/"fmfile0.dat.2","d3:len3:bade");}
  if(mode==3){fs::remove(f.path(2));check(::link(f.path(1).c_str(),f.path(2).c_str())==0,"fixture hardlink");}
  if(mode==4){auto metadata=f.metadata();auto attrs=doc(encode(*metadata.find("attributes")));put(attrs,"dnd_sf",b("dnd"));put(metadata,"attributes",attrs.serialize());write(f.state,metadata.serialize());}
  if(mode==5){auto metadata=f.metadata();auto attrs=doc(encode(*metadata.find("attributes")));put(attrs,"filelinks2","l"+b("2\n"+f.path(2).string()+"\n"+f.path(1).string())+'e');put(metadata,"attributes",attrs.serialize());write(f.state,metadata.serialize());}
  if(mode==6){write(f.controls/"fmfile1.dat.bak","d6:lengthi50001e7:versioni0ee");}
  if(mode==7){request.selectionIntentEncoding=*compat::encodeOriginalFileSelectionIntent(std::array<std::uint8_t,3>{0,1,0},false);}
  const auto beforeState=read(f.state);auto result=st::convertOriginalSelectionGroup(*f.lease,request);check(!result,"unsupported complete preflight refuses");check(read(f.state)==beforeState&&read(f.profile/"downloads.config")==inventory&&read(f.path(1))==c&&!st::hasPendingOriginalCompaction(*f.lease),"refusal before any group mutation");++families;
 }
 for(unsigned mode=0;mode<7;++mode){Fixture f;auto request=f.request();cut=st::OriginalSelectionGroupStage::JournalSynced;remaining=1;check(!st::convertOriginalSelectionGroup(*f.lease,request,interrupt),"stale journal setup");const auto originalC=read(f.path(1));
  if(mode==0){auto current=read(f.path(1));fs::remove(f.path(1));write(f.path(1),current);}
  if(mode==1){auto current=read(f.path(1));current[0]='!';write(f.path(1),current);}
  if(mode==2){auto metadata=f.metadata();auto attrs=doc(encode(*metadata.find("attributes")));put(attrs,"opaque.attr","i99e");put(metadata,"attributes",attrs.serialize());write(f.state,metadata.serialize());}
  if(mode==3){write(f.profile/"biglybt.config","d26:File.Character.Conversions3:a=be");}
  if(mode==4){write(f.controls/"fmfile0.dat.2",f.xControl+"trailing");}
  if(mode==5||mode==6){auto metadata=f.metadata();auto attrs=doc(encode(*metadata.find("attributes")));if(mode==5){struct stat file{};check(::stat(f.path(2).c_str(),&file)==0,"foreign progress fixture inode");put(attrs,"BiglyBTC.compactConversion","ld5:indexi2e6:device"+i(file.st_dev)+"5:inode"+i(file.st_ino)+"ee");}else{put(attrs,"resumecomplete","i9e");}put(metadata,"attributes",attrs.serialize());write(f.state,metadata.serialize());}
  f.cold();const auto state=read(f.state),saved=read(f.profile/"downloads.config");check(!st::recoverOriginalCompaction(*f.lease),"stale source refuses cold recovery");check(read(f.state)==state&&read(f.profile/"downloads.config")==saved&&st::hasPendingOriginalCompaction(*f.lease),"stale refusal leaves metadata unchanged and group fenced");if(mode!=1){check(read(f.path(1))==originalC,"other group payload untouched on stale recovery");}++families;
 }
}

void archiveAliases(){for(bool backup:{false,true}){Fixture f;const auto physical=backup?f.area.root/"archive.torrent.bak":f.path(1);
  if(backup){fs::rename(f.path(1),physical);auto metadata=f.metadata();auto attrs=doc(encode(*metadata.find("attributes")));put(attrs,"filelinks2","l"+b("1\n"+f.path(1).string()+"\n"+physical.string())+'e');put(metadata,"attributes",attrs.serialize());write(f.state,metadata.serialize());}
  auto saved=doc(f.row());put(saved,"torrent",b((backup?f.area.root/"archive.torrent":physical).string()));auto inventory=doc(read(f.profile/"downloads.config"));put(inventory,"downloads","l"+saved.serialize()+'e');write(f.profile/"downloads.config",inventory.serialize());auto request=f.request();const auto beforeState=read(f.state),beforeSaved=read(f.profile/"downloads.config"),beforeData=read(physical);
  check(!st::convertOriginalSelectionGroup(*f.lease,request),"primary/backup archive alias refuses before mutation");check(read(f.state)==beforeState&&read(f.profile/"downloads.config")==beforeSaved&&read(physical)==beforeData&&!st::hasPendingOriginalCompaction(*f.lease),"archive aliased payload and state remain exact");++families;
}}
void journalTampering(){for(unsigned mode=0;mode<8;++mode){Fixture f;auto request=f.request();cut=st::OriginalSelectionGroupStage::JournalSynced;remaining=1;check(!st::convertOriginalSelectionGroup(*f.lease,request,interrupt),"journal tamper setup");const auto path=f.profile/".BiglyBTC.linear-to-compact";constexpr std::string_view magic="BIGLYBTC-UNSKIP-GROUP-1\n";const auto journal=read(path);check(journal.starts_with(magic),"group magic");auto outer=doc(journal.substr(magic.size()));auto body=doc(std::string(outer.find("payload")->bytes));
  if(mode==0||mode==1||mode==5){const auto rows=body.find("files")->children;std::string list="l";for(std::size_t p=0;p<rows.size();++p){if(p==1){auto row=doc(encode(rows[p]));if(mode==0){put(row,"path",b(f.path(2).string()));}if(mode==1){put(row,"offset","i70001e");}if(mode==5){put(row,"prefixHash",b(std::string(64,'0')));}list+=row.serialize();}else{list+=encode(rows[p]);}}put(body,"files",list+'e');}
  if(mode==2){auto attrs=doc(std::string(body.find("attributes")->bytes));put(attrs,"storetypes","l1:X1:L1:Le");put(body,"attributes",b(attrs.serialize()));}
  if(mode==3){auto policy=f.desired;policy[2].priority=-2;auto value=compat::encodeOriginalFilePolicy(policy);check(value.has_value(),"tampered policy");put(body,"policy",b(*value));}
  if(mode==4){auto saved=doc(std::string(body.find("row")->bytes));put(saved,"opaque.row",b("changed"));put(body,"row",b(saved.serialize()));}
  if(mode==6){const auto controls=body.find("controls")->children;std::string list="l";for(std::size_t p=0;p<controls.size();++p){if(p==0){auto row=doc(encode(controls[p]));put(row,"path",b((f.area.root/"unowned-control").string()));list+=row.serialize();}else{list+=encode(controls[p]);}}put(body,"controls",list+'e');}
  if(mode==7){put(body,"profile",b("/tmp"));}
  put(outer,"payload",b(body.serialize()));put(outer,"sha256",b(st::fileop::digest(body.serialize())));write(path,std::string(magic)+outer.serialize());f.cold();const auto state=read(f.state),saved=read(f.profile/"downloads.config"),c=read(f.path(1)),x=read(f.path(0));check(!st::recoverOriginalCompaction(*f.lease),"semantically changed journal refuses despite recomputed checksum");check(read(f.state)==state&&read(f.profile/"downloads.config")==saved&&read(f.path(1))==c&&read(f.path(0))==x&&st::hasPendingOriginalCompaction(*f.lease),"tampered journal grants no writes and remains fenced");++families;
}}

}
int main(){try{normalCommands();groupCuts();absentResumeCuts();conversionCuts();refusalAndStale();archiveAliases();journalTampering();std::cout<<"PASS_ORIGINAL_SELECTION_GROUP "<<families<<" families; "<<assertions<<" assertions; generated stopped profiles only\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL_ORIGINAL_SELECTION_GROUP "<<e.what()<<" after "<<families<<" families; "<<assertions<<" assertions\n";return 1;}}
