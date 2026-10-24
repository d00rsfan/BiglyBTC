#include "managed_storage.hpp"
#include "network_policy.hpp"
#include "torrent_state.hpp"

#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
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
  void pureV2(bool truncated) {
    const auto old=profile/"active"/(hash.hex()+".dat"); body=std::string(16384,'V');
    const auto rootHash=libtorrent::hasher256(body.data(),static_cast<int>(body.size())).final().to_string();
    info="d"+b("file tree")+"d"+b("original.bin")+"d"+b("")+"d"+b("length")+i(16384)+b("pieces root")+b(rootHash)+"eee"
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
void test(std::string_view name,const std::function<void()>& body) { body(); ++families; std::cout<<"case "<<name<<'\n'; }
}
int main() {
 try {
  test("new-independent-payload-root-and-filtered-parameters",[] {
    Fixture f; auto owner=f.prepare(); const auto params=owner.parameters();
    require(fs::exists(f.path())&&fs::file_size(f.path())==0,"missing linear payload created, no fabricated length");
    require(params.save_path==f.save.string()&&params.renamed_files.at(libtorrent::file_index_t{0})=="saved.bin","saved filename mapping preserved");
    require(params.file_priorities.size()==1&&params.file_priorities[0]==libtorrent::download_priority_t{4},"original ordinary priority rank");
    require(bool(params.flags&libtorrent::torrent_flags::paused)&&!bool(params.flags&libtorrent::torrent_flags::auto_managed),"owner explicitly controls start");
    require(params.trackers.empty(),"isolated policy removes tracker without any request");
    auto state=f.state(); const auto* data=state.find("resume")->find("data"); require(data->find("valid")->integer==0&&data->find("resume data")->bytes==std::string(2,'\2'),"prepared original RECHECK durable");
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
    { Fixture f(false,false,{},"l1:Ce"); deniedPrepare(f,Code::Unsupported); require(!fs::exists(f.save),"unsupported storage creates no payload directory"); }
    { Fixture f; write(f.path(),std::string(32769,'X')); deniedPrepare(f,Code::UnsafePath); require(fs::file_size(f.path())==32769,"oversized bytes not truncated"); }
    { Fixture f; fs::create_directories(f.path().parent_path()); write(f.root/"foreign","untouched"); require(::symlink((f.root/"foreign").c_str(),f.path().c_str())==0,"fixture symlink"); deniedPrepare(f,Code::UnsafePath); require(read(f.root/"foreign")=="untouched","symlink target untouched"); }
    { Fixture f; write(f.path(),f.body); require(::link(f.path().c_str(),(f.root/"alias").c_str())==0,"fixture unexpected link"); deniedPrepare(f,Code::UnsafePath); }
    { Fixture f; auto state=f.state(); auto attrs=core::Document::parse(encode(*state.find("attributes"))); require(attrs.has_value(),"fixture link source");
      const auto link="0\n"+f.path().string()+"\n"+(f.root/"external.bin").string(); require(attrs->replaceValue("filelinks2","l"+b(link)+"e").has_value(),"fixture external link");
      require(state.replaceValue("attributes",attrs->serialize()).has_value(),"fixture linked state"); write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize()); deniedPrepare(f,Code::UnsafePath); }
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
  std::cout<<"PASS "<<families<<" managed-storage families; "<<assertions<<" assertions; generated profiles and files only; no engine activation or Java-runtime equality credit\n";
  return 0;
 } catch (const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
