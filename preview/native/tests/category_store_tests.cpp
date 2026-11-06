// SPDX-License-Identifier: GPL-2.0-or-later
#include "category_store.hpp"
#include "encoded_document.hpp"
#include <openssl/evp.h>
#include <zlib.h>
#include <array>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>
namespace s=biglybtc::storage;namespace c=biglybtc::core;namespace fs=std::filesystem;
namespace {
void check(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void take(std::expected<void,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
template<class T>void error(const std::expected<T,s::Error>& value,s::ErrorCode code){check(!value&&value.error().code==code,"Wrong store refusal");}
std::string bytes(std::string_view input){return std::to_string(input.size())+':'+std::string(input);}
std::string dict(std::initializer_list<std::pair<std::string,std::string>> fields){std::string value="d";for(const auto& [key,data]:fields){value+=bytes(key)+data;}return value+'e';}
std::string list(std::initializer_list<std::string> fields){std::string value="l";for(const auto& data:fields){value+=data;}return value+'e';}
std::string sha1(std::string_view data){std::string out(20,'\0');unsigned count=0;check(EVP_Digest(data.data(),data.size(),reinterpret_cast<unsigned char*>(out.data()),&count,EVP_sha1(),nullptr)==1&&count==20,"Fixture info hash failed");return out;}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);check(bool(in),"Read generated file");return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& path,std::string_view data){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(data.data(),static_cast<std::streamsize>(data.size()));out.close();check(bool(out),"Write generated file");check(::chmod(path.c_str(),0600)==0,"Generated file mode");}
std::string raw(const c::Document& source,const c::Node* node){check(node,"Missing fixture node");return source.originalBytes().substr(node->begin,node->end-node->begin);}
std::string gzip(const std::string& value){z_stream z{};check(deflateInit2(&z,3,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK,"Gzip fixture init");std::string out(value.size()+1024,'\0');z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(value.data()));z.avail_in=static_cast<uInt>(value.size());z.next_out=reinterpret_cast<Bytef*>(out.data());z.avail_out=static_cast<uInt>(out.size());const auto status=deflate(&z,Z_FINISH);out.resize(z.total_out);deflateEnd(&z);check(status==Z_STREAM_END,"Gzip fixture bound");return out;}
struct Area {
  fs::path root,payload;std::optional<s::ProfileLease> lease;
  std::string info=dict({{"length","i3e"},{"name",bytes("category.bin")},{"piece length","i16384e"},{"pieces",bytes(sha1("abc"))}});
  c::TorrentHash hash=*c::TorrentHash::parse(sha1(info));
  Area(){std::string name="/tmp/biglybtc-category-store-XXXXXX";const auto* made=::mkdtemp(name.data());check(made,"Generated directory");root=made;check(::chmod(root.c_str(),0700)==0,"Generated directory mode");payload=root/"payload.bin";fs::create_directory(root/"active");write(payload,"abc");lease.emplace(take(s::ProfileLease::acquire(root)));}
  ~Area(){lease.reset();std::error_code failure;fs::remove_all(root,failure);}
  fs::path state()const{return root/"active"/(hash.hex()+".dat");}
  void target(std::optional<std::string> category={}){
    std::string attrs="d"+bytes("future")+bytes(std::string("x\0y",3));if(category){attrs+=bytes("category")+bytes(*category);}attrs+='e';
    write(state(),dict({{"info",info},{"attributes",attrs},{"resume",dict({{"data",dict({{"valid","i1e"},{"resume data",bytes("\1")},{"blocks","de"}})}})},{"future-root",bytes("keep")}}));
    const auto row=dict({{"torrent_hash",bytes(std::string(reinterpret_cast<const char*>(hash.bytes().data()),hash.bytes().size()))},{"torrent",bytes((root/"row.torrent").string())},{"save_dir",bytes(root.string())},{"save_file",bytes("payload.bin")},{"state","i70e"},{"persistent","i1e"}});
    write(root/"downloads.config",dict({{"downloads",list({row})}}));write(root/"row.torrent",dict({{"info",info}}));
  }
};
std::unique_ptr<s::CategoryStore> opened(Area& a){return take(s::CategoryStore::open(*a.lease));}
s::CategorySnapshot snap(s::CategoryStore& store){return take(store.snapshot());}
std::map<std::string,std::string> files(const Area& a){std::map<std::string,std::string> result;for(const auto& row:fs::recursive_directory_iterator(a.root)){if(row.is_regular_file()&&!row.is_symlink()){result.emplace(row.path().lexically_relative(a.root).string(),read(row.path()));}}return result;}
void firstCreatePrimaryOnlyAndOriginalSuffixes(){
  Area a;auto store=opened(a);auto empty=snap(*store);check(empty.missing()&&empty.writable(),"Missing definitions state");const auto old=files(a);check(!fs::exists(a.root/"categories.config"),"Read created config");
  auto first=take(store->create(empty,"Alpha"));check(first.changed&&first.createdName=="Alpha"&&!first.snapshot.missing(),"First category not committed");check(read(a.root/"categories.config")==first.snapshot.categories()->source->originalBytes(),"Actual saved projection differs");
  check(!fs::exists(a.root/"categories.config.new")&&!fs::exists(a.root/"categories.config.bak")&&!fs::exists(a.root/"categories.config.saving"),"Generic backup/saving semantics leaked into category file");
  const auto committed=files(a);auto same=take(store->create(first.snapshot,"Alpha"));check(!same.changed&&same.snapshot.generation()==first.snapshot.generation()&&files(a)==committed,"Name no-op changed files/generation");error(store->create(empty,"stale"),s::ErrorCode::Conflict);error(store->remove(first.snapshot,"Alpha"),s::ErrorCode::Conflict);check(files(a)==committed,"Incomplete inventory deletion changed files");
  take(store->close());auto reopened=opened(a);check(!snap(*reopened).missing()&&snap(*reopened).categories()->categories.front().name=="Alpha","Original primary did not reopen");check(read(a.payload)==old.at("payload.bin"),"Definition changed payload");
}
void primaryRecoveryCandidatesNeverFallbackOrDisappear(){
  for(const auto* suffix:{".new",".bak",".saving"}){Area a;write(a.root/(std::string("categories.config")+suffix),"opaque original pending or backup");auto store=opened(a);auto missing=snap(*store);check(missing.missing()&&!missing.writable(),"Missing primary substituted a companion");const auto before=files(a);error(store->create(missing,"blocked"),s::ErrorCode::Conflict);check(files(a)==before,"Recovery candidate was altered");}
  {Area a;write(a.root/"categories.config","bad");write(a.root/"categories.config.bak","de");error(s::CategoryStore::open(*a.lease),s::ErrorCode::InvalidFormat);check(read(a.root/"categories.config")=="bad","Malformed primary was repaired from bak");}
  {Area a;const auto oldRow=dict({{"name",bytes("Original")},{"maxup","i123e"},{"future-row",bytes(std::string("a\0b",3))},{"attr",dict({{"future",bytes("keep")}})}});const auto original=dict({{"future-root",list({"i-3e",bytes("opaque")})},{"categories",list({oldRow})}});write(a.root/"categories.config",original);write(a.root/"categories.config.bak","foreign backup bytes");auto store=opened(a);auto result=take(store->create(snap(*store),"Beta"));check(result.changed&&read(a.root/"categories.config.bak")=="foreign backup bytes","Existing unused bak was overwritten");auto before=c::Document::parse(original),after=c::Document::parse(read(a.root/"categories.config"));check(before&&after&&raw(*before,before->find("future-root"))==raw(*after,after->find("future-root"))&&raw(*after,&after->find("categories")->children.front())==oldRow,"Actual category publication changed unknown root/old row");}
  {Area a;write(a.root/"categories.config","de");write(a.root/"categories.config.new","unresolved .new");auto store=opened(a);check(!snap(*store).writable(),"Pending .new made writable");}
}
void actualSelectedStateAssignmentPreservesInfoResumePayloadAndUnknowns(){
  Area a;a.target();auto store=opened(a);auto definitions=take(store->create(snap(*store),"Alpha")).snapshot;const auto oldState=read(a.state()),oldInventory=read(a.root/"downloads.config"),oldPayload=read(a.payload);
  auto target=take(store->readTarget(definitions,a.hash));check(target.writable()&&!target.assignment()->declaredName&&*target.hash()==a.hash,"Actual target binding differs");
  auto saved=take(store->assign(definitions,target,std::string("Alpha")));check(saved.changed&&saved.snapshot.assignment()->resolved&&saved.snapshot.assignment()->declaredName=="Alpha","Actual state assignment failed");
  auto before=c::Document::parse(oldState),after=c::Document::parse(read(a.state()));check(before&&after,"Saved state codec");for(const auto* key:{"info","resume","future-root"}){check(raw(*before,before->find(key))==raw(*after,after->find(key)),"Assignment changed immutable/non-category state");}check(raw(*before,before->find("attributes")->find("future"))==raw(*after,after->find("attributes")->find("future")),"Unknown attributes changed");
  check(read(a.root/"downloads.config")==oldInventory&&read(a.payload)==oldPayload,"Assignment modified inventory or payload");const auto stable=files(a);auto noOp=take(store->assign(definitions,saved.snapshot,std::string("Alpha")));check(!noOp.changed&&files(a)==stable,"Assignment no-op rewrote data");error(store->assign(definitions,target,std::nullopt),s::ErrorCode::Conflict);
  auto cleared=take(store->assign(definitions,saved.snapshot,std::nullopt));check(cleared.changed&&!cleared.snapshot.assignment()->declaredName,"Actual category removal from target failed");take(store->close());auto reopened=opened(a);auto loaded=take(reopened->readTarget(snap(*reopened),a.hash));check(!loaded.assignment()->declaredName,"Reopened actual state differs");
}
void authoritativeCacheOverlayIsUsedAndEntryRetiredByExistingWriter(){
  Area a;a.target();auto store=opened(a);auto definitions=take(store->create(snap(*store),"Alpha")).snapshot;definitions=take(store->create(definitions,"Beta")).snapshot;
  const auto identity=std::string(reinterpret_cast<const char*>(a.hash.bytes().data()),a.hash.bytes().size());const auto unknown=bytes("unrelated cached value");
  const auto selected=dict({{"hash",bytes(identity)},{"attributes",dict({{"category",bytes("Alpha")},{"future",bytes("authoritative-cache")}})}});
  const auto other=dict({{"hash",bytes(std::string(20,'z'))},{"future",unknown}});write(a.root/"active/cache.dat",gzip(dict({{"state",list({selected,other})},{"unknown-cache",unknown}})));
  auto target=take(store->readTarget(definitions,a.hash));check(target.assignment()->declaredName=="Alpha"&&target.assignment()->resolved,"Cached category was ignored in favor of stale primary");
  auto saved=take(store->assign(definitions,target,std::string("Beta")));check(saved.changed&&saved.snapshot.assignment()->declaredName=="Beta","Cached assignment did not commit");
  auto cache=s::EncodedDocument::parse(read(a.root/"active/cache.dat"));check(cache&&cache->compressed(),"Original compressed cache lost");const auto* remaining=cache->document().find("state");check(remaining&&remaining->children.size()==1&&remaining->children[0].find("hash")->bytes==std::string(20,'z'),"Writer dropped unrelated cached state or left stale selected entry");check(raw(cache->document(),&remaining->children[0])==other&&raw(cache->document(),cache->document().find("unknown-cache"))==unknown,"Unrelated cache data rewritten");
  auto state=c::Document::parse(read(a.state()));check(state&&state->find("attributes")->find("future")->bytes=="authoritative-cache","Authoritative unknown cached attribute was lost");
}
void externalChangesAndForeignSelectionsRefuseWithoutOverwrite(){
  {Area a;auto store=opened(a);auto defs=take(store->create(snap(*store),"Alpha")).snapshot;auto peer=opened(a);error(peer->create(defs,"foreign"),s::ErrorCode::Conflict);write(a.root/"categories.config","de");const auto before=files(a);error(store->create(defs,"stale"),s::ErrorCode::Conflict);check(files(a)==before,"External category definition overwritten");}
  for(unsigned kind=0;kind<4;++kind){Area a;a.target();auto store=opened(a);auto defs=take(store->create(snap(*store),"Alpha")).snapshot;auto target=take(store->readTarget(defs,a.hash));
    if(kind==0){write(a.state(),"changed state bytes");}else if(kind==1){fs::rename(a.state(),a.root/"retired-state");write(a.state(),read(a.root/"retired-state"));}else if(kind==2){write(a.root/"downloads.config","de");}else{write(a.root/"active/cache.dat","changed cache bytes");}
    const auto before=files(a);error(store->assign(defs,target,std::string("Alpha")),s::ErrorCode::Conflict);check(files(a)==before,"External target/inventory/cache data overwritten");
  }
  {Area a;a.target();auto store=opened(a);auto defs=take(store->create(snap(*store),"Alpha")).snapshot;auto other=*c::TorrentHash::parse(std::string(20,'x'));error(store->readTarget(defs,other),s::ErrorCode::Conflict);}
  for(const auto* suffix:{".saving",".bak.saving"}){Area a;a.target();write(a.state().string()+suffix,"pending state");auto store=opened(a);auto defs=take(store->create(snap(*store),"Alpha")).snapshot;auto target=take(store->readTarget(defs,a.hash));check(!target.writable(),"Pending saved state granted writes");const auto before=files(a);error(store->assign(defs,target,std::string("Alpha")),s::ErrorCode::Conflict);check(files(a)==before,"Pending saved state repaired silently");}
}
void pathThreadBudgetAndRetainedLeaseBoundaries(){
  for(const auto* name:{"categories.config","categories.config.new","categories.config.bak"}){Area a;write(a.root/"sentinel","keep");check(::symlink((a.root/"sentinel").c_str(),(a.root/name).c_str())==0,"Symlink fixture");error(s::CategoryStore::open(*a.lease),s::ErrorCode::UnsafePath);check(read(a.root/"sentinel")=="keep","Symlink destination changed");}
  {Area a;write(a.root/"sentinel","keep");check(::link((a.root/"sentinel").c_str(),(a.root/"categories.config").c_str())==0,"Hardlink fixture");error(s::CategoryStore::open(*a.lease),s::ErrorCode::UnsafePath);}
  {Area a;a.target();auto store=opened(a);const auto defs=snap(*store);const auto target=take(store->readTarget(defs,a.hash));std::stop_source cancelled;cancelled.request_stop();const auto before=files(a);error(store->create(defs,"cancelled",cancelled.get_token()),s::ErrorCode::Cancelled);error(store->create(defs,"late",{},s::CategoryStore::Deadline::min()),s::ErrorCode::Cancelled);check(files(a)==before,"Pre-write cancellation changed files");
    std::optional<s::ErrorCode> wrong;std::thread foreign([&]{auto result=store->snapshot();if(!result){wrong=result.error().code;}});foreign.join();check(wrong==s::ErrorCode::Unsupported,"Foreign worker accepted");
    a.lease.reset();error(s::ProfileLease::acquire(a.root),s::ErrorCode::Busy);take(store->close());auto released=s::ProfileLease::acquire(a.root);check(bool(released)&&defs.categories()&&defs.categories()->source->originalBytes()=="de"&&target.assignment()&&target.assignment()->source,"Snapshot retained an OFD or lost owning bytes");}
  {Area a;auto store=opened(a);auto defs=snap(*store);fs::rename(a.root/".azlock",a.root/"old-lock");write(a.root/".azlock","new lock");const auto before=files(a);check(!store->create(defs,"foreign lock"),"Replaced lock was accepted");check(files(a)==before,"Replaced lock caused mutation");}
  {Area a;a.target();auto store=opened(a);auto defs=take(store->create(snap(*store),"Alpha")).snapshot;auto target=take(store->readTarget(defs,a.hash));fs::rename(a.root/"active",a.root/"old-active");fs::create_directory(a.root/"active");const auto before=files(a);error(store->assign(defs,target,std::string("Alpha")),s::ErrorCode::Conflict);check(files(a)==before,"Replaced active directory was written");}
  {Area a;auto store=opened(a);auto defs=snap(*store);const auto moved=a.root.string()+"-held";fs::rename(a.root,moved);fs::create_directory(a.root);
    struct Restore {fs::path root,moved;~Restore(){std::error_code ec;fs::remove_all(root,ec);fs::rename(moved,root,ec);}} restore{a.root,moved};
    auto result=store->create(defs,"outside");check(!result,"Replaced named profile was accepted");check(!fs::exists(fs::path(moved)/"categories.config")&&!fs::exists(a.root/"categories.config"),"Renamed profile mutated");take(store->close());}
}
struct FileLimit {
  struct rlimit old{};struct sigaction action{};bool armed=false;
  FileLimit(){check(::getrlimit(RLIMIT_FSIZE,&old)==0,"Read file resource limit");struct sigaction ignored{};ignored.sa_handler=SIG_IGN;sigemptyset(&ignored.sa_mask);check(::sigaction(SIGXFSZ,&ignored,&action)==0,"Scope file-limit signal");auto limited=old;limited.rlim_cur=64;if(::setrlimit(RLIMIT_FSIZE,&limited)!=0){(void)::sigaction(SIGXFSZ,&action,nullptr);throw std::runtime_error("Lower file resource limit");}armed=true;}
  ~FileLimit(){if(armed){(void)::setrlimit(RLIMIT_FSIZE,&old);(void)::sigaction(SIGXFSZ,&action,nullptr);}}
};
void actualWriteFailurePoisonsAndRequiresExplicitReadReopen(){
  Area a;auto store=opened(a);auto defs=take(store->create(snap(*store),"Alpha")).snapshot;const auto before=read(a.root/"categories.config");
  {FileLimit limit;auto result=store->create(defs,"Must not be replayed");check(!result,"Actual bounded-file write unexpectedly succeeded");}
  check(store->recoveryRequired(),"Attempted write failure did not poison store");error(store->snapshot(),s::ErrorCode::Conflict);error(store->reload(),s::ErrorCode::Conflict);check(read(a.root/"categories.config")==before,"Failed .new write changed original primary");take(store->close());auto reopened=opened(a);check(snap(*reopened).categories()->source->originalBytes()==before,"Explicit reopen replayed an edit");
}
#include "category_delete_journal_tests.inc"
}
int main(){try{firstCreatePrimaryOnlyAndOriginalSuffixes();primaryRecoveryCandidatesNeverFallbackOrDisappear();actualSelectedStateAssignmentPreservesInfoResumePayloadAndUnknowns();authoritativeCacheOverlayIsUsedAndEntryRetiredByExistingWriter();externalChangesAndForeignSelectionsRefuseWithoutOverwrite();pathThreadBudgetAndRetainedLeaseBoundaries();actualWriteFailurePoisonsAndRequiresExplicitReadReopen();actualDeletionAndPrefixRecovery();categoryDeletionExternalChangesAndCancellation();std::cout<<"PASS category worker store: seven generated-profile families\n";return EXIT_SUCCESS;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}}
