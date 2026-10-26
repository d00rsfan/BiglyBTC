#include "managed_storage.hpp"
#include "torrent_intake.hpp"
#include "original_disk_io.hpp"
#include "network_policy.hpp"
#include "torrent_state.hpp"

#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <clocale>
#include <langinfo.h>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include "biglybtc/torrent_intake_options.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

namespace {
using namespace biglybtc;
namespace fs=std::filesystem;
using Storage=engine::detail::ManagedStorage;
using Code=storage::ErrorCode;
std::size_t assertions=0,families=0;
void require(bool condition,std::string_view message) { ++assertions; if (!condition) { throw std::runtime_error(std::string(message)); } }
std::string b(std::string_view value) { return std::to_string(value.size())+":"+std::string(value); }
std::string i(std::int64_t value) { return "i"+std::to_string(value)+"e"; }
std::string encode(const core::Node& value) { auto result=core::encodeValue(value,storage::compatibleConfigLimits()); require(result.has_value(),"fixture encode"); return std::move(*result); }
std::string read(const fs::path& path) {
  const int file=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC); require(file>=0,"fixture read open");
  std::string result; std::array<char,8192> buffer{};
  for (;;) { auto count=::read(file,buffer.data(),buffer.size()); if (count<0&&errno==EINTR) { continue; }
    if (count<0) { ::close(file); throw std::runtime_error("fixture read"); } if (!count) { break; } result.append(buffer.data(),static_cast<std::size_t>(count)); }
  require(::close(file)==0,"fixture close"); return result;
}
void write(const fs::path& path,std::string_view bytes) {
  fs::create_directories(path.parent_path()); const int file=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600); require(file>=0,"fixture write open");
  std::size_t offset=0; while (offset<bytes.size()) { auto count=::write(file,bytes.data()+offset,bytes.size()-offset); if (count<0&&errno==EINTR) { continue; }
    if (count<=0) { ::close(file); throw std::runtime_error("fixture write"); } offset+=static_cast<std::size_t>(count); }
  require(::close(file)==0,"fixture write close");
}
std::string gzip(std::string_view input) {
  z_stream stream{}; require(::deflateInit2(&stream,Z_BEST_SPEED,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK,"fixture gzip init");
  std::string result(::deflateBound(&stream,static_cast<uLong>(input.size())),'\0');
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data())); stream.avail_in=static_cast<uInt>(input.size());
  stream.next_out=reinterpret_cast<Bytef*>(result.data()); stream.avail_out=static_cast<uInt>(result.size());
  const int status=::deflate(&stream,Z_FINISH); const auto count=stream.total_out; ::deflateEnd(&stream); require(status==Z_STREAM_END,"fixture gzip finish"); result.resize(count); return result;
}
struct Fixture {
  fs::path root,profile,save;
  bool multi,skipped;
  std::string suffix,info,body,attrs,stateBytes;
  core::TorrentHash hash;
  std::optional<storage::ProfileLease> lease;
  Fixture(bool multiple=false,bool skip=false,std::string ending={},std::string stores={}) : multi(multiple),skipped(skip),suffix(std::move(ending)) {
    char pattern[]="/tmp/biglybtc-managed-storage-XXXXXX"; auto* value=::mkdtemp(pattern); require(value!=nullptr,"owned fixture root"); root=value;
    profile=root/"profile"; fs::create_directory(profile); require(::chmod(profile.c_str(),0700)==0,"private profile mode"); fs::create_directory(profile/"active");
    save=root/"independent-download-root"/"nested";
    body=std::string(multi?20000:32768,'A')+(multi?std::string(12768,'B'):std::string{});
    std::string pieces; for (std::size_t offset=0;offset<body.size();offset+=16384) { const auto count=std::min<std::size_t>(16384,body.size()-offset); pieces+=libtorrent::hasher(body.data()+offset,static_cast<int>(count)).final().to_string(); }
    info="d";
    if (multi) { info+=b("files")+"ld"+b("length")+i(20000)+b("path")+"l"+b("a.bin")+"eed"+b("length")+i(12768)+b("path")+"l"+b("b.bin")+"eee"; }
    else { info+=b("length")+i(32768); }
    info+=b("name")+b(multi?"bundle":"original.bin")+b("piece length")+i(16384)+b("pieces")+b(pieces)+"e";
    auto key=core::TorrentHash::parse(libtorrent::hasher(info.data(),static_cast<int>(info.size())).final().to_string()); require(key.has_value(),"fixture info hash"); hash=*key;
    if (stores.empty()) { stores=multi?"l1:L1:Le":"l1:Le"; }
    attrs="d"+b("storetypes")+stores+b("networks")+"l6:Publice"+b("resumecomplete")+"i2e"+b("unknown")+"li-7e3:keye"
      +(suffix.empty()?"":b("incompfilesuffix")+b(suffix))+"e";
    stateBytes="d"+b("announce")+b("http://127.0.0.1:1234/announce")+b("info")+info+b("attributes")+attrs
      +b("azureus_properties")+"d"+b("dht_backup_enable")+"i0ee"+b("resume")+"d"+b("data")+"d"+b("resume data")+b(std::string(2,'\1'))+b("valid")+"i1e"+b("blocks")+"deee"+"e";
    write(profile/"active"/(hash.hex()+".dat"),stateBytes); write(profile/"downloads.config",inventory());
    auto owned=storage::ProfileLease::acquire(profile); require(owned.has_value(),"fixture lease"); lease.emplace(std::move(*owned));
  }
  ~Fixture() { lease.reset(); std::error_code error; fs::remove_all(root,error); }
  Fixture(const Fixture&)=delete; Fixture& operator=(const Fixture&)=delete;
  std::string inventory(int state=70,int forced=0,int position=1,std::int64_t received=9,std::int64_t sent=11,bool paused=false) const {
    const auto key=hash.bytes(); const std::string raw(reinterpret_cast<const char*>(key.data()),key.size());
    return "d"+b("downloads")+"ld"+b("torrent_hash")+b(raw)+b("torrent")+b("/not-opened/source.torrent")+b("save_dir")+b(save.string())
      +b("save_file")+b(multi?"renamed-root":"saved.bin")+b("state")+i(state)+b("forceStart")+i(forced)+b("position")+i(position)+b("downloaded")+i(received)+b("uploaded")+i(sent)
      +(skipped?b("file_priorities")+(multi?"li1ei0ee":"li0ee"):"")+b("private_row_value")+"li44ee"+"ee"
      +(paused?b("pause_data")+"ld"+b("hash")+b(raw)+b("force")+"i0e"+b("owner")+"i41eee":"")+b("opaque_root")+"d1:xi9ee"+"e";
  }
  fs::path path(std::size_t index=0,bool incomplete=true) const {
    return multi?save/"renamed-root"/((index?"b.bin":"a.bin")+(incomplete?suffix:std::string{}))
      :save/("saved.bin"+(incomplete?suffix:std::string{}));
  }
  void pureV2(bool truncated,int bytes=16384) {
    require(bytes>0&&bytes<=16384,"Bounded one-block pure-v2 fixture");
    const auto old=profile/"active"/(hash.hex()+".dat"); body=std::string(static_cast<std::size_t>(bytes),'V');
    const auto rootHash=libtorrent::hasher256(body.data(),static_cast<int>(body.size())).final().to_string();
    info="d"+b("file tree")+"d"+b("original.bin")+"d"+b("")+"d"+b("length")+i(bytes)+b("pieces root")+b(rootHash)+"eee"
      +b("meta version")+"i2e"+b("name")+b("original.bin")+b("piece length")+i(16384)+"e";
    auto digest=libtorrent::hasher256(info.data(),static_cast<int>(info.size())).final().to_string(); if (truncated) { digest.resize(20); }
    const auto parsed=core::TorrentHash::parse(digest); require(parsed.has_value(),"fixture pure-v2 key"); hash=*parsed;
    auto document=core::Document::parse(stateBytes); require(document.has_value(),"fixture pure-v2 state");
    require(document->replaceValue("info",info).has_value(),"fixture pure-v2 info");
    const auto resume="d"+b("data")+"d"+b("resume data")+b(std::string(1,'\1'))+b("valid")+"i1e"+b("blocks")+"deee";
    require(document->replaceValue("resume",resume).has_value(),"fixture pure-v2 resume"); stateBytes=document->serialize();
    write(profile/"active"/(hash.hex()+".dat"),stateBytes); write(profile/"downloads.config",inventory()); require(fs::remove(old),"remove obsolete fixture v1 state");
  }
  Storage prepare(const engine::NetworkPolicy& policy={}) { auto result=Storage::prepare(*lease,hash,policy); require(result.has_value(),result?"prepare":result.error().message); return std::move(*result); }
  void fullPayload() const { if (multi) { write(path(0),std::string_view(body).substr(0,20000)); write(path(1),std::string_view(body).substr(20000)); } else { write(path(),body); } }
  core::Document state() const { auto parsed=core::Document::parse(read(profile/"active"/(hash.hex()+".dat")),storage::compatibleConfigLimits()); require(parsed.has_value(),"fixture state readback"); return std::move(*parsed); }
  core::Document saved() const { auto parsed=core::Document::parse(read(profile/"downloads.config"),storage::compatibleConfigLimits()); require(parsed.has_value(),"fixture inventory readback"); return std::move(*parsed); }
};

void deniedPrepare(Fixture& fixture,Code code) {
  const auto state=read(fixture.profile/"active"/(fixture.hash.hex()+".dat")); const auto inventory=read(fixture.profile/"downloads.config");
  auto result=Storage::prepare(*fixture.lease,fixture.hash,{}); require(!result&&result.error().code==code,"prepare refusal code");
  require(read(fixture.profile/"active"/(fixture.hash.hex()+".dat"))==state&&read(fixture.profile/"downloads.config")==inventory,"unsupported preparation leaves state/inventory bytes");
}
void checkpoint(Storage& owner,Fixture& fixture,std::array<std::uint8_t,2> pieces,bool complete,std::int64_t received=0,std::int64_t sent=0) {
  auto saved=owner.checkpoint(*fixture.lease,pieces,complete,received,sent); require(saved.has_value(),saved?"checkpoint":saved.error().message);
}
const core::Node& row(const core::Document& doc) { const auto* list=doc.find("downloads"); require(list&&list->children.size()==1,"fixture single inventory row"); return list->children[0]; }
using Purpose=engine::detail::ManagedStoragePurpose;
fs::path existingLeaf(const Fixture& f,std::size_t index=0){return f.multi?f.save/"bundle"/(index?"b.bin":"a.bin"):f.save/"original.bin";}
void existingFixture(Fixture& f,bool payload=true){
  auto inventory=f.saved();auto first=core::Document::parse(encode(row(inventory)));require(first.has_value(),"Original existing-only row");require(first->replaceValue("save_file",b(f.multi?"bundle":"original.bin")).has_value()&&inventory.replaceValue("downloads","l"+first->serialize()+'e').has_value(),"Existing-only original canonical root");write(f.profile/"downloads.config",inventory.serialize());
  if(payload){if(f.multi){write(existingLeaf(f,0),std::string_view(f.body).substr(0,20000));write(existingLeaf(f,1),std::string_view(f.body).substr(20000));}else{write(existingLeaf(f),f.body);}}
}
auto prepareExisting(Fixture& f,std::span<const engine::detail::ManagedPayloadClaim> claimed={}){return Storage::prepare(*f.lease,f.hash,{},claimed,{}, {},Purpose::VerifyExistingReadOnly);}
void existingAttribute(Fixture& f,std::string_view key,std::string value){auto state=f.state();auto attrs=core::Document::parse(encode(*state.find("attributes")));require(attrs.has_value()&&attrs->replaceValue(key,std::move(value)).has_value()&&state.replaceValue("attributes",attrs->serialize()).has_value(),"Existing-only negative attribute");write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());}


void test(std::string_view name,const std::function<void()>& body) { body(); ++families; std::cout<<"case "<<name<<'\n'; }
}
int main() {
 try {
  require(std::setlocale(LC_CTYPE, "")!=nullptr,"Initialize actual filesystem character locale");
  std::string filesystemEncoding(::nl_langinfo(CODESET));
  std::erase(filesystemEncoding,'-');
  std::ranges::transform(filesystemEncoding,filesystemEncoding.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
  require(filesystemEncoding=="utf8","Actual generated Unicode fixtures require an installed UTF8 locale");

  test("actual-intake-save-name-drives-descriptors-suffix-and-completion",[] {
    for(const bool multi:{false,true}){
      Fixture f(multi,false,".part");const auto profile=f.root/"custom-import-profile",destination=f.root/"custom-import-payload";fs::create_directories(profile);write(profile/"downloads.config","d9:downloadsle6:futurei7ee");
      auto lease=storage::ProfileLease::acquire(profile);require(lease.has_value(),"New held import profile");const auto source=storage::inspectTorrentIntake(f.profile/"active"/(f.hash.hex()+".dat"));require(source.has_value(),"Actual sealed original source");
      const std::string leaf=multi?"chosen-root":"chosen.bin";std::vector<compat::FilePolicy> policy(source->layout().files.size(),{false,0});
      const auto imported=storage::importTorrentWithFileOptions(*lease,*source,destination,true,policy,nullptr,false,{}, {},false,false,false,{},leaf);require(imported&&*imported==f.hash,"Actual intake saves chosen root without changing identity");
      const auto path=[&](std::size_t n,bool incomplete){return multi?destination/leaf/((n?"b.bin":"a.bin")+(incomplete?f.suffix:std::string{})):destination/(leaf+(incomplete?f.suffix:std::string{}));};
      {auto owner=Storage::prepare(*lease,f.hash,{});require(owner.has_value(),owner?"prepare":owner.error().message);const auto params=owner->parameters();
        for(std::size_t n=0;n<source->layout().files.size();++n){require(params.renamed_files.at(libtorrent::file_index_t{static_cast<int>(n)})==path(n,true).lexically_relative(destination).string()&&fs::exists(path(n,true)),"Actual authenticated descriptor uses chosen root plus one incomplete suffix");}
        if(multi){write(path(0,true),std::string_view(f.body).substr(0,20000));write(path(1,true),std::string_view(f.body).substr(20000));}else{write(path(0,true),f.body);}
        const auto checkpoint=owner->checkpoint(*lease,std::array<std::uint8_t,2>{1,1},true,32768,0);require(checkpoint.has_value(),checkpoint?"checkpoint":checkpoint.error().message);
      }
      require(!fs::exists(path(0,true))&&fs::exists(path(0,false))&&!fs::exists(destination/(multi?"bundle":"original.bin")),"Completion durably renames chosen paths only");
      auto owner=Storage::prepare(*lease,f.hash,{});require(owner.has_value(),owner?"reopen":owner.error().message);require(owner->parameters().renamed_files.at(libtorrent::file_index_t{0})==path(0,false).lexically_relative(destination).string(),"Reopened real mapping does not append suffix twice");
      auto state=core::Document::parse(read(profile/"active"/(f.hash.hex()+".dat")),storage::compatibleConfigLimits());require(state&&encode(*state->find("info"))==f.info&&state->find("attributes")->find("displayname")->bytes==leaf,"Exact original raw identity and manual name survive completion");require(read(path(0,false))+(multi?read(path(1,false)):std::string{})==f.body,"Actual named payload bytes remain complete");
    }
  });

  test("existing-only-complete-linear-preparation-is-read-only-and-still-unverified",[] {
    for(const bool multi:{false,true}){
      Fixture f(multi);existingFixture(f);std::vector<struct stat> before(multi?2:1);for(std::size_t i=0;i<before.size();++i){require(::stat(existingLeaf(f,i).c_str(),&before[i])==0,"Actual complete existing leaf");}
      auto owner=prepareExisting(f);require(owner.has_value(),owner?"existing-only prepare":owner.error().message);require(owner->purpose()==Purpose::VerifyExistingReadOnly&&owner->mappedFiles()&&owner->mappedFiles()->access()==engine::detail::OriginalDiskAccess::ReadOnly,"Linear existing-only always uses immutable descriptor route");
      const auto params=owner->parameters();require(bool(params.flags&libtorrent::torrent_flags::paused)&&!bool(params.flags&libtorrent::torrent_flags::auto_managed)&&!bool(params.flags&libtorrent::torrent_flags::seed_mode),"Storage never releases queue or assumes piece hashes");const auto prepared=f.state();const auto* resume=prepared.find("resume")->find("data");require(resume->find("valid")->integer==0&&resume->find("resume data")->bytes==std::string(2,'\2'),"Existing size/source complete bits cannot mint verified resume; all original pieces require recheck");
      std::vector<char> actual(f.body.size());require(owner->mappedFiles()->read(0,actual).has_value()&&std::string(actual.begin(),actual.end())==f.body,"Actual authenticated read-only cross-file descriptor bytes");require(owner->mappedFiles()->flush().has_value()&&owner->validateExistingSnapshot().has_value(),"Read-only flush and owner snapshot validate without mutable codec publication");
      for(std::size_t i=0;i<before.size();++i){struct stat after{};require(::stat(existingLeaf(f,i).c_str(),&after)==0&&after.st_dev==before[i].st_dev&&after.st_ino==before[i].st_ino&&after.st_size==before[i].st_size&&after.st_mtim.tv_sec==before[i].st_mtim.tv_sec&&after.st_mtim.tv_nsec==before[i].st_mtim.tv_nsec&&after.st_ctim.tv_sec==before[i].st_ctim.tv_sec&&after.st_ctim.tv_nsec==before[i].st_ctim.tv_nsec,"Preparation/read/flush never creates, replaces, truncates or changes payload timestamps");}
      require(f.state().find("attributes")->find("unknown")&&row(f.saved()).find("private_row_value"),"Original unknown state/row preserved");
      // Utility checkpoint receives explicit caller observations only; actual
      // same-admission LT all-piece authority is intentionally a later owner task.
      const std::array<storage::OriginalTorrentIdentity,1> retained{owner->identity()};const auto beforeRetained=read(f.profile/"active"/(f.hash.hex()+".dat"));const auto duplicate=Storage::prepare(*f.lease,f.hash,{}, {},retained,{},Purpose::VerifyExistingReadOnly);require(!duplicate&&duplicate.error().code==Code::Conflict&&read(f.profile/"active"/(f.hash.hex()+".dat"))==beforeRetained,"Existing-only cannot bypass an actual retained full-identity claim");
      auto saved=owner->checkpoint(*f.lease,std::array<std::uint8_t,2>{0,0},false,0,0);require(saved.has_value(),saved?"existing-only partial checkpoint":saved.error().message);require(f.state().find("resume")->find("data")->find("valid")->integer==0,"Retirement without supplied verified pieces remains original recheck, never fake complete");
    }
    Fixture ordinary;auto owner=ordinary.prepare();require(owner.purpose()==Purpose::Download&&!owner.mappedFiles()&&fs::exists(ordinary.path()),"Absent/default purpose retains legacy linear creation and mmap route");
  });
  test("existing-only-pure-v2-reuses-exact-tail-projection-with-no-pad-descriptor",[] {
    for(const bool tail:{false,true}){
      Fixture f;f.pureV2(true,tail?7:16384);existingFixture(f);const auto source=f.state();const auto originalInfo=encode(*source.find("info"));
      auto owner=prepareExisting(f);require(owner.has_value(),owner?"existing-only v2":owner.error().message);const auto params=owner->parameters();
      require(params.ti->info_hashes().has_v2()&&!params.ti->info_hashes().has_v1()&&params.ti->num_files()==(tail?2:1),"Actual pure-v2 engine-only slot count");
      require(owner->purpose()==Purpose::VerifyExistingReadOnly&&owner->mappedFiles()->access()==engine::detail::OriginalDiskAccess::ReadOnly,"V2 uses existing read-only descriptor route");
      std::vector<char> bytes(static_cast<std::size_t>(params.ti->total_size()),'X');require(owner->mappedFiles()->read(0,bytes).has_value(),"Read actual real file and virtual engine pad");
      require(std::string(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(f.body.size()))==f.body&&std::ranges::all_of(std::span(bytes).subspan(f.body.size()),[](char byte){return byte=='\0';}),"Engine tail is zero without physical payload");
      fs::path pad;if(tail){const auto index=libtorrent::file_index_t{1};pad=f.save/params.ti->files().file_path(index);require(params.ti->files().pad_file_at(index)&&params.file_priorities[1]==libtorrent::download_priority_t{0}&&!fs::exists(pad),"Only authenticated synthetic pad is unselected and absent");}
      require(owner->validateExistingSnapshot().has_value()&&owner->mappedFiles()->flush().has_value(),"V2 read-only snapshots do not normalize controls");
      const auto completed=owner->checkpoint(*f.lease,std::array<std::uint8_t,1>{1},true,0,0);require(completed.has_value(),completed?"v2 checkpoint":completed.error().message);
      const auto state=f.state();require(encode(*state.find("info"))==originalInfo&&state.find("attributes")->find("filedownloaded")->find("downloaded")->children.size()==1
          &&state.find("attributes")->find("filedownloaded")->find("downloaded")->children[0].integer==static_cast<std::int64_t>(f.body.size()),"Profile progress retains exactly the original real slot and raw info");
      require(read(existingLeaf(f))==f.body&&(!tail||!fs::exists(pad)),"Checkpoint never creates virtual padding or changes real bytes");
    }
  });
  test("existing-only-unsupported-or-incomplete-scope-refuses-before-effects",[] {
    for(int kind=0;kind<14;++kind){
      Fixture f(true);existingFixture(f,kind!=0);std::array<engine::detail::ManagedPayloadClaim,1> claims{};
      if(kind==1){require(fs::remove(existingLeaf(f,1)),"Missing existing sibling");}
      if(kind==2){write(existingLeaf(f),std::string(19999,'A'));}
      if(kind==3){write(existingLeaf(f),std::string(20001,'A'));}
      if(kind==4){const auto external=f.root/"external-unchanged";write(external,std::string(20000,'A'));require(fs::remove(existingLeaf(f)),"Replace owned test leaf with symlink");fs::create_symlink(external,existingLeaf(f));}
      if(kind==5){const auto moved=f.root/"real-parent";fs::rename(f.save,moved);fs::create_directory_symlink(moved,f.save);}
      if(kind==6){fs::create_hard_link(existingLeaf(f),f.root/"unapproved-alias");}
      if(kind==7){auto inventory=f.saved();auto selected=core::Document::parse(encode(row(inventory)));require(selected&&selected->replaceValue("file_priorities","li0ei1ee").has_value()&&inventory.replaceValue("downloads","l"+selected->serialize()+'e').has_value(),"Skipped existing file fixture");write(f.profile/"downloads.config",inventory.serialize());}
      if(kind==8){existingAttribute(f,"storetypes","l1:R1:Le");}
      if(kind==9){auto inventory=f.saved();auto selected=core::Document::parse(encode(row(inventory)));require(selected&&selected->replaceValue("save_file",b("custom-root")).has_value()&&inventory.replaceValue("downloads","l"+selected->serialize()+'e').has_value(),"Renamed existing root is outside first contract");write(f.profile/"downloads.config",inventory.serialize());}
      if(kind==10){existingAttribute(f,"filelinks2","l"+b("0\n"+existingLeaf(f).string()+"\n"+(f.root/"external").string())+'e');}
      if(kind==11){existingAttribute(f,"incompfilesuffix",b(".part"));}
      if(kind==12){struct stat stamp{};require(::stat(existingLeaf(f).c_str(),&stamp)==0,"Pending completion identity");auto item=core::Document::parse("de");require(item.has_value(),"Pending completion item");for(const auto& [key,value]:std::array<std::pair<std::string,std::string>,5>{{{"index","i0e"},{"source",b("bundle/a.bin")},{"target",b("bundle/a.done")},{"device",i(stamp.st_dev)},{"inode",i(stamp.st_ino)}}}){require(item->replaceValue(key,value).has_value(),"Pending original completion field");}existingAttribute(f,"BiglyBTC.completion","d7:versioni2e5:filesl"+item->serialize()+"ee");}
      if(kind==13){claims[0]={existingLeaf(f),0,0};}
      const auto state=read(f.profile/"active"/(f.hash.hex()+".dat"));const auto inventory=read(f.profile/"downloads.config");const auto result=prepareExisting(f,kind==13?std::span<const engine::detail::ManagedPayloadClaim>(claims):std::span<const engine::detail::ManagedPayloadClaim>{});require(!result,"Missing/short/oversized/symlink/alias/skipped/nonlinear/renamed/linked/suffix/pending/retained scope refuses");require(read(f.profile/"active"/(f.hash.hex()+".dat"))==state&&read(f.profile/"downloads.config")==inventory,"All rejected scope leaves original state/inventory bytes before beginUpdate");require(!fs::exists(f.profile/"active"/f.hash.hex()),"Refusal creates no control directory");if(kind==0){require(!fs::exists(f.save),"Missing full payload creates no parent or file");}if(kind==1){require(!fs::exists(existingLeaf(f,1)),"Missing sibling is not repaired");}if(kind==2){require(fs::file_size(existingLeaf(f))==19999,"Short leaf is not extended");}if(kind==3){require(fs::file_size(existingLeaf(f))==20001,"Oversized leaf is not truncated");}if(kind==4){require(read(f.root/"external-unchanged")==std::string(20000,'A')&&fs::is_symlink(existingLeaf(f)),"Symlink target and link retained");}if(kind==6){require(fs::equivalent(existingLeaf(f),f.root/"unapproved-alias"),"Unrecognized hardlink remains unmodified");}
    }
  });
  test("existing-only-read-and-checkpoint-refuse-late-replacement-or-content-change",[] {
    for(const bool replacement:{false,true}){
      Fixture f;existingFixture(f);auto owner=prepareExisting(f);require(owner.has_value(),"Actual existing-only descriptor owner");const auto prepared=read(f.profile/"active"/(f.hash.hex()+".dat"));const auto path=existingLeaf(f);struct stat before{};require(::stat(path.c_str(),&before)==0,"Original existing leaf stat");
      if(replacement){fs::rename(path,f.root/"retained-original");write(path,f.body);}else{auto changed=f.body;changed[0]='!';write(path,changed);const struct timespec times[]{before.st_atim,before.st_mtim};require(::utimensat(AT_FDCWD,path.c_str(),times,0)==0,"Restore mtime for actual content-change negative");}
      const auto bytes=read(path);std::array<char,1> output{};require(!owner->validateExistingSnapshot()&&!owner->mappedFiles()->read(0,output),"Named inode/ctime proof refuses late change before reading actual payload");require(!owner->checkpoint(*f.lease,std::array<std::uint8_t,2>{1,1},true,0,0),"External changes cannot publish supplied verified resume");require(read(path)==bytes&&read(f.profile/"active"/(f.hash.hex()+".dat"))==prepared,"Refusal never repairs/replaces changed payload or saves complete state");
    }
  });
  test("original-canonical-latin1-paths-preserve-raw-identity-and-checkpoint",[] {
    Fixture f(true);const auto oldPath=f.profile/"active"/(f.hash.hex()+".dat");
    auto metadata=f.state();auto info=core::Document::parse(f.info);require(info.has_value(),"Latin1 original info");
    const std::string first("na\xefve.bin",9),second("caf\xe9.bin",8);
    const auto entries="ld6:lengthi20000e4:pathl"+b(first)+"eed6:lengthi12768e4:pathl"+b(second)+"eee";
    require(info->replaceValue("files",entries).has_value()&&info->replaceValue("name",b(std::string("caf\xe9",4))).has_value(),"Original Latin1 values");
    f.info=info->serialize();f.hash=*core::TorrentHash::parse(libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string());
    require(metadata.replaceValue("info",f.info).has_value()&&metadata.replaceValue("encoding",b("ISO-8859-1")).has_value(),"Selected original charset");
    f.stateBytes=metadata.serialize();write(f.profile/"active"/(f.hash.hex()+".dat"),f.stateBytes);write(f.profile/"downloads.config",f.inventory());
    require(fs::remove(oldPath),"Remove obsolete generated hash state");
    const auto a=f.save/"renamed-root"/"na\xc3\xafve.bin",z=f.save/"renamed-root"/"caf\xc3\xa9.bin";
    write(a,std::string_view(f.body).substr(0,20000));write(z,std::string_view(f.body).substr(20000));
    struct stat before{};require(::lstat(a.c_str(),&before)==0,"Actual UTF8 filesystem inode");
    auto owner=f.prepare();const auto params=owner.parameters();
    const auto subject=params.ti->info_section();
    require(std::string_view(subject.data(),static_cast<std::size_t>(subject.size()))==f.info
        &&params.ti->info_hashes().has_v1()&&!params.ti->info_hashes().has_v2()
        &&params.ti->info_hashes().v1.to_string()==libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string(),
        "Runtime codec projection retains complete original raw info and v1 hash");
    require(params.renamed_files.at(libtorrent::file_index_t{0})==(fs::path("renamed-root")/"na\xc3\xafve.bin").string()
        &&params.renamed_files.at(libtorrent::file_index_t{1})==(fs::path("renamed-root")/"caf\xc3\xa9.bin").string(),"Real backend receives original decoded relative paths");
    checkpoint(owner,f,{1,1},true);
    const auto saved=f.state();require(saved.find("encoding")->bytes=="ISO-8859-1"&&encode(*saved.find("info"))==f.info,"Checkpoint keeps raw legacy metadata and selected codec");
    require(saved.find("attributes")->find("unknown")&&read(a)+read(z)==f.body,"Unknown state and actual supplied payload preserved");
    struct stat after{};require(::lstat(a.c_str(),&after)==0&&after.st_dev==before.st_dev&&after.st_ino==before.st_ino,"Preparation/checkpoint never substitutes decoded file inode");
    require(!fs::exists(f.path(0))&&!fs::exists(f.path(1)),"No accidental basic-name payload files created");
  });
  test("windows1251-v1-paths-use-authenticated-runtime-projection-and-exact-original-checkpoint",[] {
    for(const auto* encoding:{"windows-1251","cp1251","cp5347","ansi-1251"}){
      for(const bool overrides:{false,true}){
      Fixture f(true);const auto old=f.profile/"active"/(f.hash.hex()+".dat");auto metadata=f.state();auto info=core::Document::parse(f.info);require(info.has_value(),"Actual cp1251 info");
      const std::string first("\xf4\xe0\xe9\xeb.bin",8),second("\xf2\xe5\xf1\xf2.bin",8);
      const auto firstRow="d6:lengthi20000e4:pathl"+b(first)+"e"+(overrides?"10:path.utf-8l15:wrong-first.bine":"")+'e';
      const auto entries="l"+firstRow+"d6:lengthi12768e4:pathl"+b(second)+"eee";
      require(info->replaceValue("files",entries).has_value()&&info->replaceValue("name",b(std::string("\xef\xe0\xef\xea\xe0",5))).has_value(),"Original Windows-1251 raw names");
      if(overrides){require(info->replaceValue("name.utf-8",b("wrong-root")).has_value(),"Original partial UTF8 override set");}
      f.info=info->serialize();f.hash=*core::TorrentHash::parse(libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string());
      require(metadata.replaceValue("info",f.info).has_value()&&metadata.replaceValue("encoding",b(encoding)).has_value(),"Explicit original Windows codec");
      f.stateBytes=metadata.serialize();write(f.profile/"active"/(f.hash.hex()+".dat"),f.stateBytes);write(f.profile/"downloads.config",f.inventory());require(fs::remove(old),"Remove obsolete raw info key");
      const auto a=f.save/"renamed-root"/"файл.bin",z=f.save/"renamed-root"/"тест.bin";write(a,std::string_view(f.body).substr(0,20000));write(z,std::string_view(f.body).substr(20000));
      struct stat before{};require(::lstat(a.c_str(),&before)==0,"Actual decoded cp1251 inode");auto owner=f.prepare();const auto params=owner.parameters();const auto subject=params.ti->info_section();
      require(std::string_view(subject.data(),static_cast<std::size_t>(subject.size()))==f.info&&params.ti->info_hashes().has_v1()&&!params.ti->info_hashes().has_v2()
          &&params.ti->info_hashes().v1.to_string()==libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string(),"Complete actual raw info/hash stays exact after runtime filename projection");
      require(params.renamed_files.at(libtorrent::file_index_t{0})==(fs::path("renamed-root")/"файл.bin").string()
          &&params.renamed_files.at(libtorrent::file_index_t{1})==(fs::path("renamed-root")/"тест.bin").string(),"Backend owns exact original decoded indexed slots");
      checkpoint(owner,f,{1,1},true);const auto after=f.state();
      require(after.find("encoding")->bytes==encoding&&encode(*after.find("info"))==f.info&&after.find("attributes")->find("unknown")&&read(a)+read(z)==f.body,"Checkpoint preserves codec, raw info, unknown state and actual payload");
      struct stat current{};require(::lstat(a.c_str(),&current)==0&&current.st_dev==before.st_dev&&current.st_ino==before.st_ino,"No decoded payload inode substitution");
      require(!fs::exists(f.path(0))&&!fs::exists(f.path(1))&&!fs::exists(f.save/"wrong-root")&&!fs::exists(f.save/"renamed-root/wrong-first.bin"),"No sanitized/basic/override aliases created");
      }
    }
    for(const unsigned value:{0x98U,0x1fU}){
      Fixture f;auto metadata=f.state();auto info=core::Document::parse(f.info);require(info.has_value(),"Undefined-byte raw info fixture");
      require(info->replaceValue("name",b("a"+std::string(1,static_cast<char>(value))+"z")).has_value(),"Explicit undefined/control input");
      f.info=info->serialize();f.hash=*core::TorrentHash::parse(libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string());
      require(metadata.replaceValue("info",f.info).has_value()&&metadata.replaceValue("encoding",b("windows-1251")).has_value(),"Bind rejected codec input to actual hash");
      write(f.profile/"active"/(f.hash.hex()+".dat"),metadata.serialize());write(f.profile/"downloads.config",f.inventory());deniedPrepare(f,Code::InvalidFormat);require(!fs::exists(f.save),"Codec refusal precedes payload/state effects");
    }
    {Fixture f;f.pureV2(true);auto metadata=f.state();require(metadata.replaceValue("encoding",b("windows-1251")).has_value(),"V2 codec guard");write(f.profile/"active"/(f.hash.hex()+".dat"),metadata.serialize());deniedPrepare(f,Code::InvalidFormat);require(!fs::exists(f.save),"No legacy v2 projection");}
  });
  test("koi8r-original-paths-retain-raw-hashes-owned-slots-and-checkpoints",[] {
    for(const auto encoding:std::array<std::string_view,4>{"KOI8-R","koi8_r","koi8","cskoi8r"}){
      for(const bool overrides:{false,true}){
        Fixture f(true);const auto old=f.profile/"active"/(f.hash.hex()+".dat");auto metadata=f.state();auto info=core::Document::parse(f.info);require(info.has_value(),"Actual Cyrillic v1 info");
        const auto first=std::string("\xc6\xc1\xca\xcc.bin",8),second=std::string("\xd4\xc5\xd3\xd4.bin",8);
        const auto root=std::string("\xd0\xc1\xd0\xcb\xc1",5);
        const auto firstRow="d6:lengthi20000e4:pathl"+b(first)+"e"+(overrides?"10:path.utf-8l15:wrong-first.bine":"")+'e';
        require(info->replaceValue("files","l"+firstRow+"d6:lengthi12768e4:pathl"+b(second)+"eee").has_value()&&info->replaceValue("name",b(root)).has_value(),"Actual raw original names");
        if(overrides){require(info->replaceValue("name.utf-8",b("wrong-root")).has_value(),"Ignored partial UTF8 root");}
        f.info=info->serialize();f.hash=*core::TorrentHash::parse(libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string());
        require(metadata.replaceValue("info",f.info).has_value()&&metadata.replaceValue("encoding",b(encoding)).has_value(),"Explicit original charset identity");
        f.stateBytes=metadata.serialize();write(f.profile/"active"/(f.hash.hex()+".dat"),f.stateBytes);write(f.profile/"downloads.config",f.inventory());require(fs::remove(old),"Retire previous fixture hash");
        const auto a=f.save/"renamed-root"/"файл.bin",z=f.save/"renamed-root"/"тест.bin";write(a,std::string_view(f.body).substr(0,20000));write(z,std::string_view(f.body).substr(20000));struct stat before{};require(::lstat(a.c_str(),&before)==0,"Actual decoded file identity");
        auto owner=f.prepare();const auto params=owner.parameters();const auto subject=params.ti->info_section();
        require(std::string_view(subject.data(),static_cast<std::size_t>(subject.size()))==f.info&&params.ti->info_hashes().has_v1()&&!params.ti->info_hashes().has_v2()&&params.ti->info_hashes().v1.to_string()==libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string(),"Original full raw info and v1 hash remain exact");
        require(params.renamed_files.at(libtorrent::file_index_t{0})==(fs::path("renamed-root")/"файл.bin").string()&&params.renamed_files.at(libtorrent::file_index_t{1})==(fs::path("renamed-root")/"тест.bin").string(),"Same original indices reach actual backend names");
        checkpoint(owner,f,{1,1},true);const auto after=f.state();require(after.find("encoding")->bytes==encoding&&encode(*after.find("info"))==f.info&&after.find("attributes")->find("unknown")&&read(a)+read(z)==f.body,"Checkpoint retains codec/raw info/unknowns/actual payload");
        struct stat current{};require(::lstat(a.c_str(),&current)==0&&current.st_dev==before.st_dev&&current.st_ino==before.st_ino&&!fs::exists(f.path(0))&&!fs::exists(f.path(1))&&!fs::exists(f.save/"wrong-root"),"No payload substitution/sanitized/override paths");
      }
    }
    for(const auto* encoding:{"KOI8-R"}){Fixture f;f.pureV2(true);auto source=f.state();require(source.replaceValue("encoding",b(encoding)).has_value(),"Legacy v2 explicit codec");write(f.profile/"active"/(f.hash.hex()+".dat"),source.serialize());deniedPrepare(f,Code::InvalidFormat);require(!fs::exists(f.save),"No legacy v2 runtime path projection");}
  });
  test("ibm866-default-original-registry-context-remains-unsupported-without-effects",[] {
    for(const auto* encoding:{"IBM866","cp866","ibm866","ibm-866","866","csIBM866"}){Fixture f;auto source=f.state();require(source.replaceValue("encoding",b(encoding)).has_value(),"Deferred original decoder fixture");write(f.profile/"active"/(f.hash.hex()+".dat"),source.serialize());deniedPrepare(f,Code::InvalidFormat);require(!fs::exists(f.save),"Deferred decoder refusal precedes payload effects");}
  });
  test("original-global-utf8-key-fallback-is-authenticated-before-runtime-names",[] {
    for(const int mode:{0,1,2}){
      Fixture f(true);const auto old=f.profile/"active"/(f.hash.hex()+".dat");
      auto metadata=f.state();auto info=core::Document::parse(f.info);require(info.has_value(),"Global override info");
      auto first=core::Document::parse(encode(info->find("files")->children[0]));
      auto second=core::Document::parse(encode(info->find("files")->children[1]));
      require(first&&second&&first->replaceValue("path.utf-8","l"+b("wrong-first.bin")+'e').has_value(),"First per-file override");
      if(mode!=1){require(second->replaceValue("path.utf-8","l"+b("wrong-second.bin")+'e').has_value(),"Second per-file override");}
      require(info->replaceValue("files","l"+first->serialize()+second->serialize()+'e').has_value(),"Original indexed file list");
      if(mode!=2){require(info->replaceValue("name.utf-8",b("wrong-root")).has_value(),"Original root override");}
      // mode0: explicit Latin1 ignores all overrides; mode1: missing one path
      // disables ALL overrides; mode2: paths alone cannot enable root override.
      if(mode==0){require(metadata.replaceValue("encoding",b("ISO-8859-1")).has_value(),"Explicit original legacy codec");}
      f.info=info->serialize();f.hash=*core::TorrentHash::parse(libtorrent::hasher(f.info.data(),static_cast<int>(f.info.size())).final().to_string());
      require(metadata.replaceValue("info",f.info).has_value(),"Original override raw info");f.stateBytes=metadata.serialize();
      write(f.profile/"active"/(f.hash.hex()+".dat"),f.stateBytes);write(f.profile/"downloads.config",f.inventory());require(fs::remove(old),"Retire old fixture identity");
      f.fullPayload();const auto inventory=f.saved();const auto unknown=encode(*row(inventory).find("private_row_value"));
      auto owner=f.prepare();const auto params=owner.parameters();const auto subject=params.ti->info_section();
      require(std::string_view(subject.data(),static_cast<std::size_t>(subject.size()))==f.info&&params.ti->info_hashes().has_v1()&&!params.ti->info_hashes().has_v2(),"Global fallback preserves original hashed bytes");
      require(params.ti->files().file_path(libtorrent::file_index_t{0})=="bundle/a.bin"
          &&params.ti->files().file_path(libtorrent::file_index_t{1})=="bundle/b.bin"
          &&params.renamed_files.at(libtorrent::file_index_t{0})=="renamed-root/a.bin"
          &&params.renamed_files.at(libtorrent::file_index_t{1})=="renamed-root/b.bin", "All original indices and backend paths use one accepted global policy");
      checkpoint(owner,f,{1,1},true);const auto after=f.state();const auto saved=f.saved();
      require(encode(*after.find("info"))==f.info&&after.find("attributes")->find("unknown")
          &&encode(*row(saved).find("private_row_value"))==unknown&&read(f.path(0))+read(f.path(1))==f.body
          &&!fs::exists(f.save/"wrong-root")&&!fs::exists(f.save/"renamed-root/wrong-first.bin"),"Checkpoint preserves raw info/unknowns and only actual original payload paths");
    }
  });
  test("filename-projection-never-admits-changed-identity-or-legacy-v2",[] {
    {
      Fixture f(true);auto metadata=f.state();auto info=core::Document::parse(f.info);require(info.has_value(),"Wrong identity fixture");
      require(info->replaceValue("name.utf-8",b("other-root")).has_value()&&metadata.replaceValue("info",info->serialize()).has_value(),"Changed original info under old saved hash");
      write(f.profile/"active"/(f.hash.hex()+".dat"),metadata.serialize());deniedPrepare(f,Code::Conflict);require(!fs::exists(f.save),"Identity refusal precedes payload effects");
    }
    {
      Fixture f;f.pureV2(true);auto metadata=f.state();require(metadata.replaceValue("encoding",b("ISO-8859-1")).has_value(),"Legacy v2 fixture");
      write(f.profile/"active"/(f.hash.hex()+".dat"),metadata.serialize());deniedPrepare(f,Code::InvalidFormat);require(!fs::exists(f.save),"No v2 legacy name projection or payload effects");
    }
  });
  test("unported-filename-charset-refuses-before-original-state-and-payload-effects",[] {
    Fixture f;auto metadata=f.state();require(metadata.replaceValue("encoding",b("KOI8-U")).has_value(),"Explicit unported charset");
    write(f.profile/"active"/(f.hash.hex()+".dat"),metadata.serialize());deniedPrepare(f,Code::InvalidFormat);
    require(!fs::exists(f.save),"No payload preparation on unsupported original charset");
  });
  // Insert in managed_storage_tests.cpp main alongside existing test(...).
  test("negative-original-links-use-authenticated-target-and-original-base",[] {
    Fixture f;
    auto state=f.state(); auto attrs=core::Document::parse(encode(*state.find("attributes")));
    require(attrs.has_value(),"negative-link attributes");
    const auto external=f.root/"external.bin"; write(external,f.body);
    const auto original=f.path().string();
    const auto links="l"+b("0\n/stale/indexed-source\n"+(f.root/"not-used").string())
        +b(" -2147483648\t\n"+original+"\n"+external.string())
        +b("-1\n"+external.string()+"\n"+(f.root/"never-followed").string())+'e';
    require(attrs->replaceValue("filelinks2",links).has_value(),"negative-link rows");
    require(state.replaceValue("attributes",attrs->serialize()).has_value(),"negative-link state");
    write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    auto owner=f.prepare(); require(owner.mappedFiles()!=nullptr,"migrated external link uses descriptor backend");
    std::string bytes(f.body.size(),'x');
    require(owner.mappedFiles()->read(0,std::span<char>(bytes)).has_value()&&bytes==f.body,
        "negative row selects actual authenticated external payload; linked destination is never a migration base");
    require(!fs::exists(f.path())&&!fs::exists(f.root/"never-followed"),"negative migration never scans or creates fallback paths");
  });

  test("new-independent-payload-root-and-filtered-parameters",[] {
    Fixture f; auto owner=f.prepare(); const auto params=owner.parameters();
    require(fs::exists(f.path())&&fs::file_size(f.path())==0,"missing linear payload created, no fabricated length");
    require(params.save_path==f.save.string()&&params.renamed_files.at(libtorrent::file_index_t{0})=="saved.bin","saved filename mapping preserved");
    require(params.file_priorities.size()==1&&params.file_priorities[0]==libtorrent::download_priority_t{4},"original ordinary priority rank");
    require(bool(params.flags&libtorrent::torrent_flags::paused)&&!bool(params.flags&libtorrent::torrent_flags::auto_managed),"owner explicitly controls start");
    require(params.trackers.empty(),"isolated policy removes tracker without any request");
    auto state=f.state(); const auto* data=state.find("resume")->find("data"); require(data->find("valid")->integer==0&&data->find("resume data")->bytes==std::string(2,'\2'),"prepared original RECHECK durable");
  });
  test("original-sequential-flags-project-before-admission-and-survive-checkpoint",[] {
    for(const auto& value:std::array<std::string,7>{"","i0e","i1e","i8192e","i8193e","i-9223372036854767616e","i9223372036854775807e"}){
      Fixture f;auto source=f.state();auto attrs=core::Document::parse(encode(*source.find("attributes")),storage::compatibleConfigLimits());require(bool(attrs),"Own flags attributes");
      if(!value.empty()){require(attrs->replaceValue("flags",value).has_value(),"Original signed flags edit");}
      require(source.replaceValue("attributes",attrs->serialize()).has_value(),"Own flags state");write(f.profile/"active"/(f.hash.hex()+".dat"),source.serialize());
      const auto flags=core::originalDownloadFlags(source.find("attributes"));require(flags.has_value(),"Valid signed original flags");
      auto owner=f.prepare();const auto params=owner.parameters();
      require(bool(params.flags&libtorrent::torrent_flags::sequential_download)==bool(*flags&core::OriginalSequentialDownloadFlag),"Exact original bit projects to native sequential flag");
      checkpoint(owner,f,{0,0},false);const auto after=f.state();require(core::originalDownloadFlags(after.find("attributes"))==flags,"Checkpoint preserves every saved flags bit");
      require(after.find("attributes")->find("unknown"),"Other original attributes retained");
      if(value.empty()){require(!after.find("attributes")->find("flags"),"Missing flags stay absent");}
    }
    for(const auto* value:{"3:bad","le","de"}){
      Fixture f;auto source=f.state();auto attrs=core::Document::parse(encode(*source.find("attributes")),storage::compatibleConfigLimits());require(bool(attrs),"Own malformed attributes");
      require(attrs->replaceValue("flags",value).has_value()&&source.replaceValue("attributes",attrs->serialize()).has_value(),"Own malformed flags state");
      write(f.profile/"active"/(f.hash.hex()+".dat"),source.serialize());deniedPrepare(f,Code::InvalidFormat);require(!fs::exists(f.save),"Malformed flags refuse before payload writes");
    }
    const auto duplicate=core::Document::parse("d5:flagsi0e5:flagsi8192ee");require(!duplicate&&duplicate.error().code=="duplicate_key","Duplicate original flags refuse in the shared parser, never first-wins");
  });
  test("per-torrent-intake-byte-limits-reach-actual-admission-parameters",[] {
    for(const auto& limits:std::array<std::pair<std::int64_t,std::int64_t>,3>{{{0,0},{65536,32768},{2147483647,2147483647}}}){
      Fixture f;auto source=f.saved();auto selected=core::Document::parse(encode(row(source)),storage::compatibleConfigLimits());require(selected.has_value(),"Own actual rate row");
      require(selected->replaceValue("maxdl",i(limits.first)).has_value()&&selected->replaceValue("maxul",i(limits.second)).has_value(),"Original rate row edit");
      require(source.replaceValue("downloads","l"+selected->serialize()+'e').has_value(),"Replace actual rate row");write(f.profile/"downloads.config",source.serialize());
      auto owner=f.prepare();const auto params=owner.parameters();
      require(params.download_limit==(limits.first?limits.first:-1)&&params.upload_limit==(limits.second?limits.second:-1),"Exact actual LT admission limits, zero maps to LT unlimited");
      checkpoint(owner,f,{0,0},false);const auto after=f.saved();
      require(row(after).find("maxdl")->integer==limits.first&&row(after).find("maxul")->integer==limits.second,"Checkpoint retains exact rate choices");
      require(encode(*row(after).find("private_row_value"))==encode(*row(source).find("private_row_value")),"Unrelated original row remains raw-preserved");
    }
  });
  test("partial-piece-progress-and-fresh-command-preservation",[] {
    Fixture f; auto owner=f.prepare(); write(f.path(),std::string_view(f.body).substr(0,16384));
    write(f.profile/"downloads.config",f.inventory(75,1,17,9,11,true)); checkpoint(owner,f,{1,0},false,5,7);
    auto inventory=f.saved(); const auto& item=row(inventory);
    require(item.find("state")->integer==75&&item.find("forceStart")->integer==1&&item.find("position")->integer==17,"new command fields survive checkpoint");
    require(inventory.find("pause_data")->children[0].find("owner")->integer==41,"new pause owner survives");
    require(item.find("downloaded")->integer==14&&item.find("uploaded")->integer==18&&item.find("completedbytes")->integer==16384,"actual admission deltas added once");
    require(item.find("allocated")->integer==0,"partial allocation is not fabricated full");
    auto state=f.state(); const auto* attrs=state.find("attributes"); const auto* resume=state.find("resume")->find("data");
    require(attrs->find("resumecomplete")->integer==1&&resume->find("valid")->integer==0&&resume->find("resume data")->bytes==std::string(2,'\2'),"partial checkpoint stays RECHECK");
    require(attrs->find("filedownloaded")->find("downloaded")->children[0].integer==16384,"verified linear file bytes");
    auto twice=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{1,0},false,5,7); require(!twice&&twice.error().code==Code::Conflict,"one checkpoint per admission");
  });
  test("skipped-linear-sparse-file-and-boundary-piece-counts",[] {
    Fixture f(true,true); auto owner=f.prepare(); auto params=owner.parameters();
    require(params.file_priorities[0]==libtorrent::download_priority_t{4}&&params.file_priorities[1]==libtorrent::download_priority_t{0},"skipped is separate priority0");
    require(fs::file_size(f.path(0))==0&&fs::file_size(f.path(1))==12768,"skipped file allocated linearly rather than LT partfile");
    f.fullPayload(); checkpoint(owner,f,{0,1},false,3,2); auto state=f.state(); const auto* values=state.find("attributes")->find("filedownloaded")->find("downloaded");
    require(values->children.size()==2&&values->children[0].integer==3616&&values->children[1].integer==12768,"actual verified boundary-piece intersections per file");
    require(row(f.saved()).find("completedbytes")->integer==16384,"partial total includes only verified bytes");
    require(!fs::exists(f.save/".parts"),"fixture has no synthetic partfile");
  });
  test("single-completion-suffix-and-unknown-state-fields",[] {
    Fixture f(false,false,".part"); auto owner=f.prepare(); require(fs::exists(f.path())&&!fs::exists(f.path(0,false)),"incomplete mapping published before allocation");
    f.fullPayload(); checkpoint(owner,f,{1,1},true,7,2);
    require(fs::exists(f.path(0,false))&&!fs::exists(f.path()),"journal completes single rename without deleting data");
    require(read(f.path(0,false))==f.body,"completed payload exact"); auto state=f.state(); const auto* attrs=state.find("attributes");
    require(attrs->find("resumecomplete")->integer==2&&attrs->find("filelinks2")->children.empty()&&attrs->find("BiglyBTC.completion")->children.empty(),"completion durable and journal cleared");
    require(encode(*attrs->find("unknown"))=="li-7e3:keye","unknown original state value preserved");
    require(state.find("resume")->find("data")->find("resume data")->bytes==std::string(2,'\1'),"only actual all-piece completion is marked done");
  });
  test("multifile-completion-renames-only-owned-actual-paths",[] {
    Fixture f(true,false,".part"); auto owner=f.prepare(); f.fullPayload(); checkpoint(owner,f,{1,1},true,12,4);
    require(!fs::exists(f.path(0))&&!fs::exists(f.path(1))&&fs::exists(f.path(0,false))&&fs::exists(f.path(1,false)),"multifile final mappings");
    require(read(f.path(0,false))+read(f.path(1,false))==f.body,"multifile bytes exact");
    auto state=f.state(); const auto* counts=state.find("attributes")->find("filedownloaded")->find("downloaded");
    require(counts->children[0].integer==20000&&counts->children[1].integer==12768,"whole-files completion only after all pieces");
  });
  test("completed-target-collision-retains-incomplete-source",[] {
    Fixture f(false,false,".part"); auto owner=f.prepare(); f.fullPayload(); write(f.path(0,false),"foreign target");
    checkpoint(owner,f,{1,1},true); require(read(f.path(0,false))=="foreign target"&&read(f.path())==f.body,"completion never overwrites unrelated target");
    auto state=f.state(); const auto* links=state.find("attributes")->find("filelinks2"); require(links->children.size()==1&&links->children[0].bytes.ends_with("saved.bin.part"),"retained complete source remains original-readable");
  });
  test("existing-single-journal-and-recognized-hardlink-recovery",[] {
    Fixture f(false,false,".part"); f.fullPayload(); require(::link(f.path().c_str(),f.path(0,false).c_str())==0,"owned recovery aliases");
    struct stat info{}; require(::lstat(f.path().c_str(),&info)==0,"fixture journal identity"); auto state=f.state();
    auto attrs=core::Document::parse(encode(*state.find("attributes"))); require(attrs.has_value(),"fixture attributes");
    const auto record="d"+b("version")+"i1e"+b("source")+b("saved.bin.part")+b("target")+b("saved.bin")+b("device")+i(static_cast<std::int64_t>(info.st_dev))+b("inode")+i(static_cast<std::int64_t>(info.st_ino))+"e";
    require(attrs->replaceValue("BiglyBTC.completion",record).has_value(),"fixture journal edit");
    const auto link="0\n"+(f.save/"saved.bin").string()+"\n"+f.path().string(); require(attrs->replaceValue("filelinks2","l"+b(link)+"e").has_value(),"fixture link edit");
    require(state.replaceValue("attributes",attrs->serialize()).has_value(),"fixture state edit"); write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    auto owner=f.prepare(); checkpoint(owner,f,{1,1},true); require(!fs::exists(f.path())&&read(f.path(0,false))==f.body,"existing journal recovered idempotently");
  });
  test("unsafe-links-storetypes-oversize-and-symlinks-fail-before-state",[] {
    { Fixture f(false,false,{},"l1:Ze"); deniedPrepare(f,Code::Unsupported); require(!fs::exists(f.save),"unsupported storage creates no payload directory"); }
    { Fixture f; write(f.path(),std::string(32769,'X')); deniedPrepare(f,Code::UnsafePath); require(fs::file_size(f.path())==32769,"oversized bytes not truncated"); }
    { Fixture f; fs::create_directories(f.path().parent_path()); write(f.root/"foreign","untouched"); require(::symlink((f.root/"foreign").c_str(),f.path().c_str())==0,"fixture symlink"); deniedPrepare(f,Code::UnsafePath); require(read(f.root/"foreign")=="untouched","symlink target untouched"); }
    { Fixture f; write(f.path(),f.body); require(::link(f.path().c_str(),(f.root/"alias").c_str())==0,"fixture unexpected link"); deniedPrepare(f,Code::UnsafePath); }
    { Fixture f; auto state=f.state(); auto attrs=core::Document::parse(encode(*state.find("attributes"))); require(attrs.has_value(),"fixture link source");
      const auto link="0\n"+f.path().string()+"\n"+(f.profile/"forbidden.bin").string(); require(attrs->replaceValue("filelinks2","l"+b(link)+"e").has_value(),"fixture external link");
      require(state.replaceValue("attributes",attrs->serialize()).has_value(),"fixture linked state"); write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize()); deniedPrepare(f,Code::UnsafePath); }
  });
  test("selected-compact-expands-with-original-storetype-and-no-stale-control",[] {
    Fixture f(false,false,{},"l1:Ce");auto owner=f.prepare();require(owner.mappedFiles()!=nullptr,"compact admission uses original disk owner");
    const auto state=f.state();require(state.find("attributes")->find("storetypes")->children[0].bytes=="L","selected compact original conversion");
    require(!fs::exists(f.profile/"active"/f.hash.hex()/"fmfile0.dat"),"compact marker retired before linear writes");
    require(state.find("resume")->find("data")->find("valid")->integer==0,"interrupted native owner forces original recheck");
    require(owner.mappedFiles()->write(0,std::span<const char>(f.body)).has_value(),"mapped write after compact selection");
    require(owner.mappedFiles()->flush().has_value(),"mapped data durable");checkpoint(owner,f,{1,1},true);require(read(f.path(0,false))==f.body,"selected compact completed data intact");
  });
  test("external-explicit-link-uses-owned-descriptor-with-original-path-preserved",[] {
    Fixture f;auto state=f.state();auto attrs=core::Document::parse(encode(*state.find("attributes")));require(attrs.has_value(),"external fixture attributes");
    const auto external=f.root/"external.bin";write(external,f.body);const auto link="0\n"+f.path().string()+"\n"+external.string();
    require(attrs->replaceValue("filelinks2","l"+b(link)+"e").has_value(),"external fixture link");require(state.replaceValue("attributes",attrs->serialize()).has_value(),"external state edit");write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    auto owner=f.prepare();require(owner.mappedFiles()!=nullptr,"external link descriptor backend");std::string bytes(f.body.size(),'x');require(owner.mappedFiles()->read(0,std::span<char>(bytes)).has_value()&&bytes==f.body,"external data read from authenticated fd");
    checkpoint(owner,f,{1,1},true);require(read(external)==f.body&&!fs::exists(f.path()),"external mapping never copied to metadata path");require(f.state().find("attributes")->find("filelinks2")->children[0].bytes==link,"external original link retained");
  });
// Insert these two families inside the existing managed_storage_tests.cpp main
// try block. Reuses Fixture/b/i/read/write/checkpoint; no new target or harness.
// Snapshot phases model durable boundaries, not power-loss or fault injection.
  test("compact-conversion-replays-original-edges-at-each-durable-boundary",[] {
    // File0 has 20000 bytes at torrent offset0. Original compact storage keeps
    // just its final3616-byte fragment, which shares real piece1 with file1.
    // Four snapshots: intent before expansion; expanded while C; L before
    // control retirement; L after retirement but before clearing the intent.
    for (const int phase : {0,1,2,3}) {
      Fixture f(true,false,{},phase<2?"l1:C1:Le":"l1:L1:Le");
      const std::string edge(3616,'A');
      std::string expanded(20000,'\0'); expanded.replace(0,edge.size(),edge);
      expanded.replace(16384,edge.size(),edge);
      write(f.path(0),phase==0?edge:expanded);
      write(f.path(1),std::string_view(f.body).substr(20000));
      struct stat identity{}; require(::lstat(f.path(0).c_str(),&identity)==0,"actual compact replay inode");
      const auto directory=f.profile/"active"/f.hash.hex();
      const auto marker=directory/"fmfile0.dat";
      const auto control="d"+b("version")+"i0e"+b("length")+i(20000)+b("opaque")+"li7e3:keye"+"e";
      if (phase!=3) {
        write(marker,control);
        // Retirement must clear both original resilient candidates. A crash
        // after unlinking primary is equivalent to a valid backup-only state.
        write(directory/"fmfile0.dat.bak",control);
      }
      auto state=f.state(); auto attributes=core::Document::parse(encode(*state.find("attributes")));
      require(attributes.has_value(),"compact replay attributes");
      const auto intent="ld"+b("index")+"i0e"+b("device")+i(static_cast<std::int64_t>(identity.st_dev))
          +b("inode")+i(static_cast<std::int64_t>(identity.st_ino))+"ee";
      require(attributes->replaceValue("BiglyBTC.compactConversion",intent).has_value(),"persist exact replay identity");
      require(state.replaceValue("attributes",attributes->serialize()).has_value(),"persist replay state");
      write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
      {
        auto owner=f.prepare(); require(owner.mappedFiles()!=nullptr,"replay uses authenticated mapped descriptors");
        const auto actual=read(f.path(0));
        require(actual==expanded,"replay preserves packed edge and copies its exact original tail once");
        require(read(f.path(1))==std::string_view(f.body).substr(20000),"replay leaves adjacent selected file unchanged");
        struct stat after{}; require(::lstat(f.path(0).c_str(),&after)==0&&after.st_dev==identity.st_dev
            &&after.st_ino==identity.st_ino,"conversion retains exact admitted payload inode");
        const auto saved=f.state(); const auto* attrs=saved.find("attributes");
        require(attrs->find("storetypes")->children[0].bytes=="L"
            &&attrs->find("BiglyBTC.compactConversion")->children.empty(),"store L is durable before intent clears");
        require(!fs::exists(marker)&&!fs::exists(directory/"fmfile0.dat.bak"),"no compact marker can redirect subsequent Java linear reads");
        require(encode(*attrs->find("unknown"))=="li-7e3:keye","unknown original attribute remains raw-compatible");
        const auto* resume=saved.find("resume")->find("data");
        require(resume->find("valid")->integer==0&&resume->find("resume data")->bytes==std::string(2,'\2'),
            "new sparse zero interior remains unverified RECHECK, never complete");
        std::string tail(edge.size(),'x');
        require(owner.mappedFiles()->read(16384,std::span<char>(tail)).has_value()&&tail==edge,
            "replayed actual original tail remains logically readable");
        // Only actual piece1 can exist initially. Do not claim a synthetic
        // verified flag here: real Java/native piece checking is in run.py.
      }
      auto reopened=f.prepare();
      require(reopened.parameters().ti!=nullptr,"replayed owner retains actual original metadata");
      require(read(f.path(0))==expanded&&read(f.path(1))==std::string_view(f.body).substr(20000),
          "second prepare after cleared intent is idempotent and retains adjacent bytes");
      const auto resumed=f.state();
      require(resumed.find("resume")->find("data")->find("valid")->integer==0,"reopen still requires actual hash checks");
    }
  });
  test("compact-conversion-refuses-replaced-journal-inode-before-any-mutation",[] {
    Fixture f(true,false,{},"l1:C1:Le");
    write(f.path(0),std::string(3616,'A')); write(f.path(1),std::string_view(f.body).substr(20000));
    struct stat original{}; require(::lstat(f.path(0).c_str(),&original)==0,"old journal inode exists");
    const auto marker=f.profile/"active"/f.hash.hex()/"fmfile0.dat";
    write(marker,"d"+b("version")+"i0e"+b("length")+i(20000)+"e");
    auto state=f.state(); auto attrs=core::Document::parse(encode(*state.find("attributes")));
    require(attrs.has_value(),"replaced journal attributes");
    const auto intent="ld"+b("index")+"i0e"+b("device")+i(static_cast<std::int64_t>(original.st_dev))
        +b("inode")+i(static_cast<std::int64_t>(original.st_ino))+"ee";
    require(attrs->replaceValue("BiglyBTC.compactConversion",intent).has_value(),"original exact inode intent");
    require(state.replaceValue("attributes",attrs->serialize()).has_value(),"original intent state");
    write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    // Keep the old inode alive under an owned alternate name so replacement
    // cannot accidentally reuse it, then install a distinct current leaf.
    const auto retired=f.root/"old-compact-inode"; fs::rename(f.path(0),retired);
    write(f.path(0),std::string(3616,'Z'));
    struct stat replacement{}; require(::lstat(f.path(0).c_str(),&replacement)==0
        &&replacement.st_ino!=original.st_ino,"actual replacement is a different inode");
    const auto stateBefore=read(f.profile/"active"/(f.hash.hex()+".dat")), inventoryBefore=read(f.profile/"downloads.config");
    const auto markerBefore=read(marker), payloadBefore=read(f.path(0)), siblingBefore=read(f.path(1));
    auto rejected=Storage::prepare(*f.lease,f.hash,{});
    require(!rejected&&rejected.error().code==Code::Conflict,"stale conversion inode is refused");
    require(read(f.profile/"active"/(f.hash.hex()+".dat"))==stateBefore&&read(f.profile/"downloads.config")==inventoryBefore
        &&read(marker)==markerBefore&&read(f.path(0))==payloadBefore&&read(f.path(1))==siblingBefore,
        "rejected replay does not alter state/control/current or adjacent payload");
    require(read(retired)==std::string(3616,'A'),"original unmatched inode also stays untouched");
  });
  test("unchanged-reordered-map-still-authenticates-control-at-checkpoint",[] {
    // This short reordered fixture uses source-linear data but still has its
    // original .dat.2 format marker. No new allocation or map edit is needed.
    Fixture f(false,false,{},"l1:Re"); f.fullPayload(); auto owner=f.prepare();
    require(owner.mappedFiles()!=nullptr&&owner.mappedFiles()->flush().has_value(),"establish acknowledged original control");
    const auto marker=f.profile/"active"/f.hash.hex()/"fmfile0.dat.2";
    const auto bytes=read(marker); struct stat original{};
    require(::lstat(marker.c_str(),&original)==0,"acknowledged actual control inode");
    const auto replacement=fs::path(marker.string()+".replacement"); write(replacement,bytes);
    struct stat next{}; require(::lstat(replacement.c_str(),&next)==0&&next.st_ino!=original.st_ino,
        "same-byte control replacement has a different real inode");
    fs::rename(replacement,marker);
    const auto stateBefore=read(f.profile/"active"/(f.hash.hex()+".dat")), inventoryBefore=read(f.profile/"downloads.config");
    auto rejected=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{0,0},false,0,0);
    require(!rejected&&rejected.error().code==Code::Conflict,"unchanged cached map cannot bypass control identity validation");
    require(read(marker)==bytes&&read(f.path())==f.body,"refusal does not replace control or payload");
    require(read(f.profile/"active"/(f.hash.hex()+".dat"))==stateBefore&&read(f.profile/"downloads.config")==inventoryBefore,
        "uncertain original control prevents progress/checkpoint publication");
  });
  test("external-file-completion-is-per-file-and-applies-original-dnd-prefix-rule",[] {
    // Normal completion, replay of an actual two-name v4 intent, and DND
    // completion retaining its collision-prevention prefix. Piece0 stays
    // unverified; real fixture piece1 wholly completes only the second file.
    for (const int scenario : {0,1,2}) {
      const bool skipped=scenario==2, interrupted=scenario==1;
      Fixture f(true,skipped,".part"); write(f.path(0),std::string_view(f.body).substr(0,20000));
      const auto source=f.root/"external"/"dnd_b.bin.part";
      const auto target=source.parent_path()/(skipped?"dnd_b.bin":"b.bin");
      write(source,std::string_view(f.body).substr(20000));
      struct stat original{}; require(::lstat(source.c_str(),&original)==0,"actual external completion inode");
      auto state=f.state(); auto attributes=core::Document::parse(encode(*state.find("attributes")));
      require(attributes.has_value(),"external completion attributes");
      require(attributes->replaceValue("dnd_pfx",b("dnd_")).has_value(),"source DND prefix");
      const auto link="1\n"+f.path(1,false).string()+"\n"+source.string();
      require(attributes->replaceValue("filelinks2","l"+b(link)+"e").has_value(),"actual external original link");
      if (interrupted) {
        require(::link(source.c_str(),target.c_str())==0,"actual external interrupted completion aliases");
        const auto journal="d"+b("version")+"i4e"+b("files")+"ld"+b("index")+"i1e"+b("source")+b(source.string())
            +b("target")+b(target.string())+b("device")+i(static_cast<std::int64_t>(original.st_dev))
            +b("inode")+i(static_cast<std::int64_t>(original.st_ino))+"eee";
        require(attributes->replaceValue("BiglyBTC.completion",journal).has_value(),"source-bound external v4 intent");
      }
      require(state.replaceValue("attributes",attributes->serialize()).has_value(),"external completion state");
      write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
      auto owner=f.prepare(); require(owner.mappedFiles()!=nullptr,"external completion keeps descriptor-backed ownership");
      checkpoint(owner,f,{0,1},false);
      require(!fs::exists(source)&&read(target)==std::string_view(f.body).substr(20000),"completed external file gets source suffix/prefix mapping");
      struct stat completedIdentity{}; require(::lstat(target.c_str(),&completedIdentity)==0&&completedIdentity.st_dev==original.st_dev&&completedIdentity.st_ino==original.st_ino,
          "per-file completion retains the exact authenticated inode");
      require(fs::exists(f.path(0))&&!fs::exists(f.path(0,false))
          &&read(f.path(0))==std::string_view(f.body).substr(0,20000),"incomplete adjacent file keeps suffix and bytes");
      require(!fs::exists(f.path(1))&&!fs::exists(f.path(1,false)),"external completion does not create a metadata-path copy");
      const auto saved=f.state(); const auto* attrs=saved.find("attributes");
      const auto* links=attrs->find("filelinks2"); bool mapped=false;
      for (const auto& item:links->children) { if(item.bytes=="1\n"+f.path(1,false).string()+"\n"+target.string()){mapped=true;} }
      require(mapped&&attrs->find("BiglyBTC.completion")->children.empty(),"final absolute original link is saved before journal clears");
      require(attrs->find("resumecomplete")->integer==1&&saved.find("resume")->find("data")->find("valid")->integer==0,
          "per-file completion cannot imply whole-torrent completion");
      const auto* counts=attrs->find("filedownloaded")->find("downloaded");
      require(counts->children[0].integer==3616&&counts->children[1].integer==12768,
          "per-file completion is driven by actual verified-piece intersections");
    }
  });

// Insert inside the existing managed_storage_tests.cpp main try block, before
// its final PASS output. Reuses the real generated Fixture, no engine/network.
  test("retained-current-path-rejected-before-compact-state-or-data-effects",[] {
    Fixture f(false,false,{},"l1:Ce");write(f.path(),"");
    const auto statePath=f.profile/"active"/(f.hash.hex()+".dat");
    const auto stateBefore=read(statePath),inventoryBefore=read(f.profile/"downloads.config");
    struct stat before{};require(::lstat(f.path().c_str(),&before)==0,"claim fixture real inode");
    const std::array<engine::detail::ManagedPayloadClaim,1> claimed{{{f.path(),static_cast<std::uint64_t>(before.st_dev),static_cast<std::uint64_t>(before.st_ino)}}};
    auto denied=Storage::prepare(*f.lease,f.hash,{},claimed);
    require(!denied&&denied.error().code==Code::Conflict,"retained path prevents selected compact conversion");
    struct stat after{};require(::lstat(f.path().c_str(),&after)==0&&after.st_dev==before.st_dev&&after.st_ino==before.st_ino&&after.st_size==0,"collision never expands or replaces compact payload");
    require(read(statePath)==stateBefore&&read(f.profile/"downloads.config")==inventoryBefore,"collision leaves all original state and inventory bytes unchanged");
    require(!fs::exists(statePath.string()+".bak")&&!fs::exists(f.profile/"active"/f.hash.hex())&&!fs::exists(f.profile/"active"/"cache.dat"),"collision creates no control, cache, or state backup");
  });
  test("retained-inode-rejected-despite-distinct-reserved-path",[] {
    Fixture f;f.fullPayload();struct stat actual{};require(::lstat(f.path().c_str(),&actual)==0,"real inode for separate claim name");
    const auto statePath=f.profile/"active"/(f.hash.hex()+".dat");
    const auto stateBefore=read(statePath),inventoryBefore=read(f.profile/"downloads.config");
    // Explicit caller-policy fixture: a retained identity whose prior path has
    // changed. No hard link is created, so the inode gate itself must reject.
    const std::array<engine::detail::ManagedPayloadClaim,1> claimed{{{f.root/"previous-owner-name",static_cast<std::uint64_t>(actual.st_dev),static_cast<std::uint64_t>(actual.st_ino)}}};
    auto denied=Storage::prepare(*f.lease,f.hash,{},claimed);
    require(!denied&&denied.error().code==Code::Conflict,"known retained inode cannot be admitted through another name");
    require(read(f.path())==f.body&&read(statePath)==stateBefore&&read(f.profile/"downloads.config")==inventoryBefore,"inode conflict preserves actual data and original documents");
  });
  test("future-completion-and-parent-child-claims-block-before-creation",[] {
    for(const int scenario:{0,1,2}) {
      Fixture f(false,false,scenario==0?".part":"");
      const auto statePath=f.profile/"active"/(f.hash.hex()+".dat");
      const auto stateBefore=read(statePath),inventoryBefore=read(f.profile/"downloads.config");
      const auto reserved=scenario==0?f.path(0,false):(scenario==1?f.save:f.path()/"child");
      const std::array<engine::detail::ManagedPayloadClaim,1> claimed{{{reserved,0,0}}};
      auto denied=Storage::prepare(*f.lease,f.hash,{},claimed);
      require(!denied&&denied.error().code==Code::Conflict,"future suffix target and both ancestor directions are reserved");
      require(!fs::exists(f.save),"reserved collision creates neither directory nor payload");
      require(read(statePath)==stateBefore&&read(f.profile/"downloads.config")==inventoryBefore,"future collision precedes state ownership and writes");
    }
  });
  test("disjoint-real-owner-claims-and-combined-bound",[] {
    Fixture a(false,false,".part");auto owner=a.prepare();
    require(owner.claims().size()==2,"actual incomplete and forecast complete paths are both retained");
    struct stat actual{};require(::lstat(a.path().c_str(),&actual)==0,"retained owner real file");
    for(const auto& claim:owner.claims()) {
      require(claim.absolutePath.is_absolute()&&claim.absolutePath.lexically_normal()==claim.absolutePath,"retained claim path is absolute and normalized");
      require(claim.device==static_cast<std::uint64_t>(actual.st_dev)&&claim.inode==static_cast<std::uint64_t>(actual.st_ino),"created payload identity fills every reserved alias");
    }
    Fixture b(true);auto accepted=Storage::prepare(*b.lease,b.hash,{},owner.claims());
    require(accepted.has_value()&&accepted->claims().size()==2,"disjoint admission accepts real retained claim set");
    Fixture c;const auto statePath=c.profile/"active"/(c.hash.hex()+".dat");const auto before=read(statePath);
    // Exact cap of existing entries leaves no room for this new file. Duplicate
    // reservations intentionally isolate aggregate accounting before indexing.
    std::vector<engine::detail::ManagedPayloadClaim> full(engine::detail::MaximumManagedPayloadClaims,{c.root/"unrelated-reservation",0,0});
    auto denied=Storage::prepare(*c.lease,c.hash,{},full);
    require(!denied&&denied.error().code==Code::Unsupported,"combined retained and new claims cannot exceed bounded capacity");
    require(!fs::exists(c.save)&&read(statePath)==before,"aggregate bound fails before payload or state effects");
  });

  test("authoritative-cache-policy-is-preflighted-without-mutation",[] {
    Fixture f; auto attrs=core::Document::parse(f.attrs); require(attrs.has_value(),"fixture cache attributes"); require(attrs->replaceValue("networks","l3:I2Pe").has_value(),"fixture cache network");
    const auto hash=f.hash.bytes(); const std::string raw(reinterpret_cast<const char*>(hash.data()),hash.size());
    const auto bytes=gzip("d"+b("state")+"ld"+b("hash")+b(raw)+b("attributes")+attrs->serialize()+"eee"); write(f.profile/"active"/"cache.dat",bytes);
    deniedPrepare(f,Code::Unsupported); require(read(f.profile/"active"/"cache.dat")==bytes&&!fs::exists(f.save),"cache and payload untouched on policy refusal");
  });
  test("policy-permitted-trackers-retained-without-engine-start",[] {
    Fixture f; engine::NetworkPolicyOptions options; options.mode=engine::NetworkMode::Normal;
    auto configured=engine::readNetworkPolicy(nullptr,options); require(configured.has_value(),"pure normal policy"); configured->httpTrackers=true;
    auto owner=f.prepare(*configured); const auto params=owner.parameters(); require(params.trackers.size()==1&&params.trackers[0]=="http://127.0.0.1:1234/announce","allowed actual metadata tracker retained");
  });
  test("invalid-checkpoints-never-publish-complete-flags",[] {
    Fixture f; auto owner=f.prepare(); const auto before=read(f.profile/"active"/(f.hash.hex()+".dat"));
    for (const auto bits: {std::array<std::uint8_t,2>{1,0},std::array<std::uint8_t,2>{2,1}}) {
      auto result=owner.checkpoint(*f.lease,bits,true,0,0); require(!result&&result.error().code==Code::InvalidFormat,"allComplete requires every actual piece");
    }
    auto overflow=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{0,0},false,std::numeric_limits<std::int64_t>::max(),0);
    require(!overflow&&overflow.error().code==Code::InvalidFormat,"delta plus original counter overflow");
    auto negative=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{0,0},false,-1,0); require(!negative&&negative.error().code==Code::InvalidFormat,"negative actual delta");
    require(read(f.profile/"active"/(f.hash.hex()+".dat"))==before,"invalid checkpoints leave prepared state exact");
    checkpoint(owner,f,{0,0},false); // Prevalidation failures do not consume the one safe attempt.
  });
  test("verified-late-piece-needs-actual-file-extent",[] {
    Fixture f; auto owner=f.prepare(); write(f.path(),std::string_view(f.body).substr(0,16384)); const auto before=read(f.profile/"active"/(f.hash.hex()+".dat"));
    auto result=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{0,1},false,0,0);
    require(!result&&result.error().code==Code::Conflict,"verified count alone cannot certify nonexistent late extent");
    require(read(f.profile/"active"/(f.hash.hex()+".dat"))==before,"extent refusal leaves RECHECK");
  });
  test("payload-replacement-and-cumulative-rebase-refuse",[] {
    { Fixture f; auto owner=f.prepare(); require(::unlink(f.path().c_str())==0,"fixture remove named inode"); write(f.path(),f.body);
      auto result=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{1,1},true,0,0); require(!result&&result.error().code==Code::Conflict,"different named inode rejected"); }
    { Fixture f; auto owner=f.prepare(); write(f.profile/"downloads.config",f.inventory(75,0,1,100,11));
      auto result=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{0,0},false,1,0); require(!result&&result.error().code==Code::Conflict,"concurrent cumulative counter is not overwritten/rebased"); }
  });
  test("original-pure-v2-truncated-and-explicit-full-keys",[] {
    for (const bool truncated:{true,false}) {
      Fixture f; f.pureV2(truncated); auto owner=f.prepare(); const auto params=owner.parameters();
      require(params.ti->info_hashes().has_v2()&&!params.ti->info_hashes().has_v1(),"actual pure-v2 metadata retained");
      require(f.hash.bytes().size()==(truncated?20U:32U),"original20/full32 fixture identity");
      f.fullPayload(); auto result=owner.checkpoint(*f.lease,std::array<std::uint8_t,1>{1},true,0,0); require(result.has_value(),"pure-v2 checkpoint");
      auto state=f.state(); require(state.find("attributes")->find("filedownloaded")->find("downloaded")->children[0].integer==16384,"pure-v2 exact linear bytes");
    }
  });
  test("wrong-profile-and-moved-storage-refuse",[] {
    Fixture f; auto owner=f.prepare(); Fixture other; auto result=owner.checkpoint(*other.lease,std::array<std::uint8_t,2>{0,0},false,0,0);
    require(!result&&result.error().code==Code::UnsafePath,"different held lease rejected"); auto moved=std::move(owner);
    auto old=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{0,0},false,0,0); require(!old&&old.error().code==Code::Conflict,"moved owner rejected");
    checkpoint(moved,f,{0,0},false);
  });
  test("legacy-path-roots-are-captured-before-payload-creation",[] {
    for(const bool legacyKey:{true,false}) {
      Fixture f;f.fullPayload();auto inventory=f.saved();const auto& original=row(inventory);std::string migrated="d";
      for(std::size_t i=0;i<original.children.size();i+=2){const auto& key=original.children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}migrated+=encode(key)+encode(original.children[i+1]);}
      migrated+=b(legacyKey?"path":"save_dir")+b(f.path().string())+'e';require(inventory.replaceValue("downloads","l"+migrated+'e').has_value(),"legacy file path row");write(f.profile/"downloads.config",inventory.serialize());
      auto owner=f.prepare();require(owner.parameters().save_path==f.save.string()&&owner.parameters().renamed_files.at(libtorrent::file_index_t{0})=="saved.bin","legacy file path has original interpretation");
      auto current=f.saved();require(row(current).find("save_dir")->bytes==f.save.string()&&row(current).find("save_file")->bytes=="saved.bin"&&row(current).find("private_row_value")&&current.find("opaque_root"),"canonical fields saved before payload changes, unknowns retained");
      checkpoint(owner,f,{1,1},true);require(row(f.saved()).find("downloaded")->integer==9,"legacy migration checkpoint has captured cumulative baseline once");
    }
    Fixture f(true);const auto path=f.save/"bundle";auto inventory=f.saved();const auto& original=row(inventory);std::string migrated="d";
    for(std::size_t i=0;i<original.children.size();i+=2){const auto& key=original.children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}migrated+=encode(key)+encode(original.children[i+1]);}
    migrated+=b("path")+b(path.string())+'e';require(inventory.replaceValue("downloads","l"+migrated+'e').has_value(),"legacy nonexistent multifile root");write(f.profile/"downloads.config",inventory.serialize());
    auto owner=f.prepare();require(owner.parameters().save_path==path.string()&&fs::exists(path/"bundle/a.bin"),"missing directory whose name matches metadata is captured as parent");auto current=f.saved();require(row(current).find("save_dir")->bytes==path.string()&&row(current).find("save_file")->bytes=="bundle","new parent existence cannot reinterpret legacy root on restart");
    checkpoint(owner,f,{0,0},false);
  });
  test("missing-store-types-use-held-original-reorder-defaults",[] {
    for(const bool reorder:{false,true}) {
      Fixture f;auto state=f.state();auto attrs=core::Document::parse("de");require(attrs.has_value(),"default attributes");require(attrs->replaceValue("opaque.default","li-3e3:rawe").has_value(),"default unknown attribute");require(state.replaceValue("attributes",attrs->serialize()).has_value(),"missing store types");write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
      write(f.profile/"biglybt.config",std::string("d")+b("Enable reorder storage mode")+(reorder?"i1e":"i0e")+b("Reorder storage mode min MB")+"i0ee");
      auto owner=f.prepare();const auto current=f.state();require(current.find("attributes")->find("storetypes")->children[0].bytes==(reorder?"R":"L"),"source default policy materialized");require(current.find("attributes")->find("opaque.default"),"default synthesis retains opaque state");
      require(bool(owner.mappedFiles())==reorder,"R default uses actual original mapped owner");if(reorder){require(current.find("attributes")->find("reordermb")->integer==0,"source global threshold captured");}
      checkpoint(owner,f,{0,0},false);
    }
  });
  test("missing-types-never-treat-compact-control-as-linear",[] {
    Fixture f(true,true);auto inventory=f.saved();auto first=core::Document::parse(encode(row(inventory)));require(first&&first->replaceValue("file_priorities","li0ei1ee").has_value(),"first compact file explicitly skipped");require(inventory.replaceValue("downloads","l"+first->serialize()+'e').has_value(),"skipped compact row");write(f.profile/"downloads.config",inventory.serialize());write(f.path(0),std::string(3616,'A'));write(f.path(1),std::string_view(f.body).substr(20000));auto state=f.state();auto attrs=core::Document::parse("de");require(attrs.has_value(),"compact default attrs");require(state.replaceValue("attributes",attrs->serialize()).has_value(),"compact missing stores");write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    const auto control=f.profile/"active"/f.hash.hex()/"fmfile0.dat";write(control,"d7:versioni0e6:lengthi20000ee");
    auto owner=f.prepare();require(owner.mappedFiles()!=nullptr&&fs::file_size(f.path(0))==20000&&!fs::exists(control),"default L expands actual C even for skipped file");
    const auto bytes=read(f.path(0));require(bytes.substr(16384)==std::string(3616,'A'),"actual compact edge is relocated to original linear tail");require(f.state().find("attributes")->find("storetypes")->children[0].bytes=="L","postconversion type is durable L");checkpoint(owner,f,{0,0},false);
  });
  std::cout<<"PASS "<<families<<" managed-storage families; "<<assertions<<" assertions; generated profiles and files only; no engine activation or Java-runtime equality credit\n";
  return 0;
 } catch (const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
