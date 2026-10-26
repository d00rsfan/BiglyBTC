#include "../adapters/original_torrent_identity_lt.hpp"
#include "../adapters/torrent_intake.hpp"
#include "../adapters/file_operations.hpp"
#include "../adapters/encoded_document.hpp"
#include "../engine/managed_storage.hpp"
#include "../engine/original_layout_projection.hpp"
#include "../engine/original_disk_io.hpp"
#include "../engine/network_policy.hpp"
#include <libtorrent/hasher.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <clocale>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
namespace {
using namespace biglybtc;namespace fs=std::filesystem;namespace lt=libtorrent;
using Code=storage::ErrorCode;using Owner=engine::detail::ManagedStorage;
std::size_t assertions=0,families=0;
void check(bool value,const char* why){++assertions;if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,storage::Error> result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
void done(std::expected<void,storage::Error> result){if(!result){throw std::runtime_error(result.error().message);}}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string hashBytes(const core::TorrentHash& hash){const auto bytes=hash.bytes();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
core::Document doc(std::string value){auto d=core::Document::parse(std::move(value),storage::compatibleConfigLimits());check(d.has_value(),"literal document parse");return std::move(*d);}
void put(core::Document& d,std::string_view key,std::string value){check(d.replaceValue(key,std::move(value)).has_value(),"fixture outside-info edit");}
std::string encoded(const core::Node& n){auto value=core::encodeValue(n,storage::compatibleConfigLimits());check(value.has_value(),"fixture encode");return std::move(*value);}
void write(const fs::path& path,std::string_view bytes){fs::create_directories(path.parent_path());const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);check(fd>=0,"owned fixture write");std::size_t offset=0;while(offset<bytes.size()){const auto n=::write(fd,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR){continue;}if(n<=0){::close(fd);throw std::runtime_error("fixture write failed");}offset+=static_cast<std::size_t>(n);}check(::close(fd)==0,"fixture write close");}
std::string read(const fs::path& path){const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);check(fd>=0,"owned fixture read");std::string value;std::array<char,4096> buffer{};for(;;){const auto n=::read(fd,buffer.data(),buffer.size());if(n<0&&errno==EINTR){continue;}if(n<0){::close(fd);throw std::runtime_error("fixture read failed");}if(!n){break;}value.append(buffer.data(),static_cast<std::size_t>(n));}check(::close(fd)==0,"fixture read close");return value;}
std::string metainfo(bool hybrid,std::optional<std::string> selector={},char payload='x'){
  const auto root=lt::hasher256(&payload,1).final().to_string();const auto piece=lt::hasher(&payload,1).final().to_string();const std::string name="identity.bin";
  std::string info="d"+b("file tree")+"d"+b(name)+"d0:d"+b("length")+"i1e"+b("pieces root")+b(root)+"eee";
  if(hybrid){info+=b("length")+"i1e";}
  info+=b("meta version")+"i2e"+b("name")+b(name)+b("piece length")+"i16384e";
  if(hybrid){info+=b("pieces")+b(piece);}info+='e';
  const auto props="d"+(selector?b("hybrid_hash_v2")+*selector:std::string{})+b("future")+"li7e3:rawee";
  return "d"+b("azureus_private_properties")+props+b("info")+info+b("future.root")+"d1:xi9eee";
}
storage::OriginalTorrentIdentity identity(const std::string& bytes){auto source=doc(bytes);const lt::torrent_info metadata(lt::span<const char>(bytes.data(),bytes.size()),lt::from_span);return take(storage::originalTorrentIdentityFromLibtorrent(source,metadata));}
struct Area{
  fs::path root,profile,input,destination;std::optional<storage::ProfileLease> lease;
  Area(){char pattern[]="/tmp/biglybtc-identity-callers-XXXXXX";const auto* made=::mkdtemp(pattern);check(made,"owned fixture directory");root=made;profile=root/"profile";input=root/"input.torrent";destination=root/"payload-parent";check(::mkdir(profile.c_str(),0700)==0,"private profile");lease.emplace(take(storage::ProfileLease::acquire(profile)));}
  ~Area(){lease.reset();std::error_code error;fs::remove_all(root,error);}
  core::TorrentHash importMetadata(const std::string& source){write(input,source);return take(storage::importTorrent(*lease,input,destination,true));}
  fs::path state(const core::TorrentHash& key)const{return profile/"active"/(key.hex()+".dat");}
  core::LibrarySnapshot library()const{auto parsed=core::readLibrary(doc(read(profile/"downloads.config")));check(parsed&&parsed->issues.empty(),"published original inventory parses");return std::move(*parsed);}
  void noMutation()const{check(!fs::exists(profile/"active")&&!fs::exists(profile/"torrents")&&!fs::exists(profile/"BiglyBTC.torrent-import")&&!fs::exists(destination),"identity refusal precedes all intake/profile/payload effects");}
  void explicitFull32(const core::TorrentHash& from,const core::TorrentHash& to){auto inventory=doc(read(profile/"downloads.config"));const auto* rows=inventory.find("downloads");check(rows&&rows->children.size()==1,"one original selected row");auto row=doc(encoded(rows->children[0]));put(row,"torrent_hash",b(hashBytes(to)));put(inventory,"downloads","l"+row.serialize()+'e');write(state(to),read(state(from)));write(profile/"downloads.config",inventory.serialize());}
};
std::string gzip(std::string_view bytes){z_stream z{};check(::deflateInit2(&z,Z_BEST_SPEED,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK,"fixture gzip init");std::string out(::deflateBound(&z,static_cast<uLong>(bytes.size())),'\0');z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));z.avail_in=static_cast<uInt>(bytes.size());z.next_out=reinterpret_cast<Bytef*>(out.data());z.avail_out=static_cast<uInt>(out.size());const int result=::deflate(&z,Z_FINISH);const auto length=z.total_out;::deflateEnd(&z);check(result==Z_STREAM_END,"fixture gzip final");out.resize(length);return out;}
void interrupted(storage::FileOperationStage stage){if(stage==storage::FileOperationStage::JournalSynced){throw std::runtime_error("synthetic after-journal boundary");}}
void selectedImportAndOfflineOperations(){
  for(const bool hybrid:{false,true}){
    for(const std::optional<std::string>& selector:{std::optional<std::string>{},std::optional<std::string>{"i0e"},std::optional<std::string>{"i1e"},std::optional<std::string>{"i2e"}}){
      Area a;const auto input=metainfo(hybrid,selector);const auto expected=identity(input);const auto key=a.importMetadata(input);check(key==expected.originalKey&&key.bytes().size()==20,"new inventory always uses original selected20 convention");
      const lt::torrent_info subject(lt::span<const char>(input.data(),input.size()),lt::from_span);const auto hashes=subject.info_hashes();const bool useV2=!hybrid||(selector&&*selector=="i1e");const auto* digest=reinterpret_cast<const std::byte*>(useV2?hashes.v2.data():hashes.v1.data());check(std::equal(key.bytes().begin(),key.bytes().end(),digest),"published original key equals independently selected actual full digest prefix");
      auto original=doc(input),saved=doc(read(a.state(key)));check(take(storage::originalTorrentInfoSubject(saved))==take(storage::originalTorrentInfoSubject(original)),"intake retains exact unrewritten info");check(encoded(*saved.find("azureus_private_properties"))==encoded(*original.find("azureus_private_properties"))&&saved.find("future.root"),"selector and unknown properties survive new state");
      const auto plan=storage::prepareFileOperation(*a.lease,{key,storage::FileOperationKind::RemoveKeepData,{}});check(plan.has_value()&&plan->preview().missingFiles==1,"EVP file-operation identity accepts real original selected key read-only");
      const auto before=read(a.state(key));auto owner=Owner::prepare(*a.lease,key,{});
      if(hybrid&&useV2){check(!owner&&owner.error().code==Code::Unsupported&&!fs::exists(a.destination)&&read(a.state(key))==before,"selected-v2 hybrid active admission remains unavailable until swarm policy qualification");}
      else{check(owner.has_value()&&owner->identity().full==expected.full,"managed identity uses every actual LT digest");}
    }
  }
}
void duplicateAliasesAndMalformedSelectors(){
  {
    Area a;const auto first=metainfo(true,"i0e"),second=metainfo(true,"i1e");const auto key=a.importMetadata(first);const auto before=read(a.profile/"downloads.config"),state=read(a.state(key));write(a.input,second);
    const auto result=storage::importTorrent(*a.lease,a.input,a.root/"other",true);check(!result&&result.error().code==Code::Conflict,"alternate original hybrid key cannot duplicate a known full subject");check(read(a.profile/"downloads.config")==before&&read(a.state(key))==state&&!fs::exists(a.profile/"BiglyBTC.torrent-import")&&!fs::exists(a.state(identity(second).originalKey)),"alternate selected import fails before journal/new state and preserves first row");
  }
  {
    Area a;const auto alternate=identity(metainfo(true,"i1e"));write(a.profile/"downloads.config","d9:downloadsle10:pause_datal"+b(hashBytes(alternate.originalKey))+"ee");const auto before=read(a.profile/"downloads.config");write(a.input,metainfo(true));const auto result=storage::importTorrent(*a.lease,a.input,a.destination,true);check(!result&&result.error().code==Code::Conflict&&read(a.profile/"downloads.config")==before&&!fs::exists(a.profile/"active"),"unselected full alias held only by legacy pause ownership also refuses import");
  }
  for(const auto* selector:{"1:x","le"}){Area a;write(a.input,metainfo(true,std::string(selector)));const auto result=storage::importTorrent(*a.lease,a.input,a.destination,true);check(!result&&result.error().code==Code::InvalidFormat,"malformed known selector is never silently defaulted");a.noMutation();}
}
storage::TorrentIntakeStage aliasStopStage=storage::TorrentIntakeStage::JournalSynced;
void aliasInterrupted(storage::TorrentIntakeStage stage){if(stage==aliasStopStage){throw std::runtime_error("owned alias fault after publication stage");}}
void stoppedHybridCompanionPreservesOriginalAuthority(){
  for(const bool sourceV2:{false,true}){
    Area a;const auto from=a.importMetadata(metainfo(true,sourceV2?"i1e":"i0e"));
    auto original=doc(read(a.state(from)));auto attrs=doc(encoded(*original.find("attributes")));
    put(attrs,"future.alias","d3:raw"+b(std::string("\0\xffQ",3))+"e");put(original,"attributes",attrs.serialize());write(a.state(from),original.serialize());
    const auto cache=gzip("d5:stateld4:hash"+b(hashBytes(from))+"10:attributes"+attrs.serialize()+"eee");
    write(a.profile/"active/cache.dat",cache);
    fs::create_directory(a.profile/"active"/from.hex());write(a.profile/"active"/from.hex()/"foreign-plugin-leaf","preserve");
    const auto before=read(a.state(from)),inventory=doc(read(a.profile/"downloads.config"));
    const auto first=encoded(inventory.find("downloads")->children[0]);
    const auto to=take(storage::importHybridAlias(*a.lease,from,sourceV2?storage::OriginalSelectedHash::V1:storage::OriginalSelectedHash::V2,{}));
    check(to!=from&&a.library().downloads.size()==2,"explicit companion adds one unique other selected key");
    check(read(a.state(from))==before&&read(a.profile/"active/cache.dat")==cache,"source state and complete authoritative cache remain byte exact");
    check(read(a.profile/"active"/from.hex()/"foreign-plugin-leaf")=="preserve","no foreign control recursion");
    const auto after=doc(read(a.profile/"downloads.config"));check(encoded(after.find("downloads")->children[0])==first,"original row/counters/intent bytes stay exact");
    const auto selected=doc(read(a.state(to)));const auto oldIdentity=identity(before),newIdentity=identity(selected.serialize());
    check(take(storage::compareOriginalTorrentIdentities(oldIdentity,newIdentity))==storage::OriginalIdentityRelation::AlternateSelected,
        "both aliases retain complete identical v1/v2 evidence");
    check(take(storage::originalTorrentInfoSubject(original))==take(storage::originalTorrentInfoSubject(selected))
        &&selected.find("future.root")&&selected.find("attributes")->find("future.alias"),"raw info and unknown source properties survive counterpart creation");
    check(a.library().downloads[1].startup==core::StartupState::Stopped&&a.library().downloads[1].downloaded==0
        &&a.library().downloads[1].uploaded==0,"new selected identity is stopped with its own fresh accounting");
    check(selected.find("resume")->find("data")->find("valid")->integer==0,"companion never trusts verification for a different selected identity");
    const auto bytes=read(a.profile/"downloads.config");const auto again=storage::importHybridAlias(*a.lease,from,sourceV2?storage::OriginalSelectedHash::V1:storage::OriginalSelectedHash::V2,{});
    check(!again&&read(a.profile/"downloads.config")==bytes&&!storage::hasPendingTorrentIntake(*a.lease),"duplicate other key is rejected before publication");
  }
}
void hybridAliasRejectsRetainedLinkedNonlinearAndManualUnsafeSources(){
  for(int mode=0;mode<5;++mode){
    Area a;const auto from=a.importMetadata(metainfo(true));
    auto state=doc(read(a.state(from)));auto attrs=doc(encoded(*state.find("attributes")));
    if(mode==1){put(attrs,"storetypes","l1:Ce");}
    if(mode==2){put(attrs,"filelinks2","l"+b("0\nignored-from\n"+(a.root/"outside").string())+'e');}
    if(mode==3){auto inventory=doc(read(a.profile/"downloads.config"));auto row=doc(encoded(inventory.find("downloads")->children[0]));
      put(row,"forceStart","i1e");put(inventory,"downloads","l"+row.serialize()+'e');write(a.profile/"downloads.config",inventory.serialize());}
    if(mode==4){fs::create_directory(a.profile/"active"/from.hex());write(a.profile/"active"/from.hex()/"fmfile0.dat.2","opaque original control");}
    put(state,"attributes",attrs.serialize());write(a.state(from),state.serialize());
    const auto before=read(a.state(from)),inventory=read(a.profile/"downloads.config");
    std::vector<storage::OriginalTorrentIdentity> retained;if(mode==0){retained.push_back(identity(state.serialize()));}
    const auto denied=storage::importHybridAlias(*a.lease,from,storage::OriginalSelectedHash::V2,retained);
    check(!denied&&read(a.state(from))==before&&read(a.profile/"downloads.config")==inventory
        &&!storage::hasPendingTorrentIntake(*a.lease)&&!fs::exists(a.destination),"retained/link/type/manual/control refusal precedes all effects");
  }
}
void hybridAliasRefusesOriginalUnloadableEmptyHybridV2() {
  for(const bool emptyFirst:{false,true}){
    Area a;const std::string data(16384,'x');const auto root=lt::hasher256(data.data(),static_cast<int>(data.size())).final().to_string();
    const auto piece=lt::hasher(data.data(),static_cast<int>(data.size())).final().to_string();
    const std::string empty=emptyFirst?"a-empty":"z-empty",real=emptyFirst?"z-data":"a-data";
    const auto emptyLeaf=b(empty)+"d0:d6:lengthi0eee";
    const auto realLeaf=b(real)+"d0:d6:lengthi16384e11:pieces root"+b(root)+"ee";
    const auto emptyFile="d6:lengthi0e4:pathl"+b(empty)+"ee";
    const auto realFile="d6:lengthi16384e4:pathl"+b(real)+"ee";
    const auto info="d9:file treed"+(emptyFirst?emptyLeaf+realLeaf:realLeaf+emptyLeaf)
        +"e5:filesl"+(emptyFirst?emptyFile+realFile:realFile+emptyFile)
        +"e12:meta versioni2e4:name6:bundle12:piece lengthi16384e6:pieces"+b(piece)+"e";
    const auto from=a.importMetadata("d4:info"+info+"e");
    const auto before=read(a.state(from)),inventory=read(a.profile/"downloads.config");
    const auto denied=storage::importHybridAlias(*a.lease,from,storage::OriginalSelectedHash::V2,{});
    check(!denied&&denied.error().code==Code::Unsupported,"actual original empty hybrid V2 reload boundary is explicit");
    check(read(a.state(from))==before&&read(a.profile/"downloads.config")==inventory
        &&!storage::hasPendingTorrentIntake(*a.lease)&&!fs::exists(a.destination),"empty hybrid refusal has no metadata, inventory or payload effects");
  }
}
void hybridAliasRecoveryRetainsSourceProofAndOldJournalCompatibility(){
  for(const auto stage:{storage::TorrentIntakeStage::JournalSynced,storage::TorrentIntakeStage::StatePublished,
      storage::TorrentIntakeStage::ArchivePublished,storage::TorrentIntakeStage::InventoryPublished}){
    Area a;const auto from=a.importMetadata(metainfo(true));const auto source=read(a.state(from));aliasStopStage=stage;
    const auto interrupted=storage::importHybridAlias(*a.lease,from,storage::OriginalSelectedHash::V2,{},aliasInterrupted);
    check(!interrupted&&storage::hasPendingTorrentIntake(*a.lease),"actual alias stage fault keeps exact intake journal");
    check(read(a.state(from))==source,"interrupted publication never mutates old state");
    if(stage==storage::TorrentIntakeStage::JournalSynced){
      write(a.state(from),source+"changed");const auto refused=storage::recoverTorrentIntake(*a.lease);
      check(!refused&&a.library().downloads.size()==1,"source CAS change blocks recovery before new library row");
      write(a.state(from),source);
    }
    const auto recovered=take(storage::recoverTorrentIntake(*a.lease));
    check(recovered&&a.library().downloads.size()==2&&!storage::hasPendingTorrentIntake(*a.lease),"matching source proof recovers unique stopped companion");
    check(read(a.state(from))==source&&!fs::exists(a.destination),"recovery preserves source bytes and never accesses payload");
  }
}
void explicit32AndRetainedIdentity(){
  Area a;const auto input=metainfo(false);const auto expected=identity(input);check(expected.full.v2.has_value(),"actual v2 evidence");const auto key=a.importMetadata(input);auto first=std::make_optional(take(Owner::prepare(*a.lease,key,{})));
  const auto full=core::TorrentHash::parse(std::string_view(reinterpret_cast<const char*>(expected.full.v2->data()),32));check(full.has_value(),"actual explicit32 key");a.explicitFull32(key,*full);
  const auto before=read(a.state(*full));const std::array<storage::OriginalTorrentIdentity,1> retained{{first->identity()}};
  auto conflict=Owner::prepare(*a.lease,*full,{}, {},retained);check(!conflict&&conflict.error().code==Code::Conflict&&read(a.state(*full))==before,"same full subject cannot be co-admitted through explicit32 alias before effects");
  auto offline=storage::prepareFileOperation(*a.lease,{*full,storage::FileOperationKind::RemoveKeepData,{}});check(offline.has_value(),"existing explicit32 file-operation reader policy retained");
  // Default standalone API asserts no other admissions. This is a read/prepare
  // compatibility fixture, not permission to run two handles simultaneously.
  first.reset();auto compatible=Owner::prepare(*a.lease,*full,{});check(compatible.has_value()&&compatible->identity().originalKey==key,"managed explicit32 reader policy retains original20 projected identity");
}
void cachedSelectionAndRecovery(){
  Area a;const auto key=a.importMetadata(metainfo(true,"i1e"));auto state=doc(read(a.state(key)));put(state,"azureus_private_properties","d14:hybrid_hash_v2i0e6:futureli7e3:rawee");write(a.state(key),state.serialize());
  write(a.profile/"active/cache.dat",gzip("d5:stateld4:hash"+b(hashBytes(key))+"4:azppd14:hybrid_hash_v2i1e6:futureli7e3:raweeeee"));
  const auto cacheBefore=read(a.profile/"active/cache.dat"),stateBefore=read(a.state(key));const auto target=a.root/"move-parent";fs::create_directory(target);
  auto plan=take(storage::prepareFileOperation(*a.lease,{key,storage::FileOperationKind::Relocate,target}));check(read(a.state(key))==stateBefore&&read(a.profile/"active/cache.dat")==cacheBefore,"cache-authoritative selected identity is a read-only prepare");
  auto confirmation=take(storage::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));const auto failed=storage::applyFileOperation(*a.lease,std::move(plan),confirmation,interrupted);check(!failed&&storage::hasPendingFileOperation(*a.lease),"generated recovery record retains authoritative cached selector");
  auto recovered=take(storage::recoverFileOperation(*a.lease));check(recovered&&recovered->recovered&&!storage::hasPendingFileOperation(*a.lease),"file-operation replay hydrates azpp before selected/full hash gate");
  const auto after=doc(read(a.state(key)));check(after.find("azureus_private_properties")->find("hybrid_hash_v2")->integer==1&&after.find("azureus_private_properties")->find("future")&&after.find("future.root"),"recovery publishes selected identity and unknown properties unchanged");check(a.library().downloads[0].saveDirectory==target.string(),"recovery changes only approved save parent");
}
void rawSubjectBinding(){
  const auto a=metainfo(true),b=metainfo(true,{},'y');auto source=doc(b);const lt::torrent_info first(lt::span<const char>(a.data(),a.size()),lt::from_span);
  auto mismatch=storage::originalTorrentIdentityFromLibtorrent(source,first);check(!mismatch&&mismatch.error().code==Code::Conflict,"actual LT hashes from a different raw info subject are refused");
  source=doc(a);const auto original=std::string(take(storage::originalTorrentInfoSubject(source)));
  put(source,"info",original);
  check(storage::originalTorrentIdentityFromLibtorrent(source,first).has_value()
      &&source.serialize()==a,"exact-byte replacement retains the actual original/full-hash chain");
  auto changedInfo=doc(original);put(changedInfo,"private","i1e");
  put(source,"info",changedInfo.serialize());
  check(!storage::originalTorrentInfoSubject(source)
      &&!storage::originalTorrentIdentityFromLibtorrent(source,first),
      "actual info edit requires fresh parsing and full-hash validation");
  const auto changedBytes=source.serialize();auto fresh=doc(changedBytes);
  const auto stale=storage::originalTorrentIdentityFromLibtorrent(fresh,first);
  check(!stale&&stale.error().code==Code::Conflict,"fresh parsing cannot authorize full hashes from the prior info subject");
  const lt::torrent_info changed(lt::span<const char>(changedBytes.data(),changedBytes.size()),lt::from_span);
  const auto rebound=take(storage::originalTorrentIdentityFromLibtorrent(fresh,changed));
  check(rebound.full!=identity(a).full&&source.originalBytes()==a,
      "only actual recomputed full hashes authorize the freshly parsed changed subject");
}
std::string orderingMetadata(bool javaOrder) {
  const std::string astral="\xf0\x90\x80\x80-a",bmp="\xee\x80\x80-b",empty="\xef\xbf\xbf-empty";
  const auto leaf=[&](char value){const auto root=lt::hasher256(&value,1).final().to_string();return "d0:d6:lengthi1e11:pieces root"+b(root)+"ee";};
  const auto a=b(astral)+leaf('a'),z=b(bmp)+leaf('z');
  const auto e=b(empty)+"d0:d6:lengthi0eee";
  return "d4:infod9:file treed"+(javaOrder?a+z+e:z+e+a)+"e12:meta versioni2e4:name6:bundle12:piece lengthi16384eee";
}
void originalJavaOrderAndPaddedProgress() {
  Area a;const auto bytes=orderingMetadata(true);const auto source=doc(bytes);
  check(take(storage::originalTorrentInfoOrder(source))==storage::OriginalInfoOrder::JavaUtf16FileTree,"source-compatible UTF16 file tree recognized");
  const auto key=a.importMetadata(bytes);const auto selected=identity(bytes);
  check(key==selected.originalKey,"UTF16 import keeps actual full-hash selected prefix");
  const auto subject=take(storage::originalTorrentInfoSubject(source));
  const lt::torrent_info engine(lt::span<const char>(bytes.data(),bytes.size()),lt::from_span);
  check(std::string_view(engine.info_section().data(),engine.info_section().size())==subject
      &&engine.info_hashes().v2==lt::hasher256(subject.data(),static_cast<int>(subject.size())).final(),"provider retains exact raw info and SHA256, never sorted replacement");
  auto owner=take(Owner::prepare(*a.lease,key,{}));const auto& files=owner.parameters().ti->files();
  check(files.num_files()==5&&files.pad_file_at(lt::file_index_t{1})&&files.pad_file_at(lt::file_index_t{3})
      &&files.file_size(lt::file_index_t{4})==0,"two pads and real empty index retained");
  for(int i:{0,2}){write(a.destination/files.file_path(lt::file_index_t{i}),i==0?"a":"z");}
  done(owner.checkpoint(*a.lease,std::array<std::uint8_t,2>{1,1},true,2,0));
  const auto saved=doc(read(a.state(key)));const auto* progress=saved.find("attributes")->find("filedownloaded")->find("downloaded");
  check(progress&&progress->children.size()==5,"original per-file progress slot count unchanged");
  check(progress->children[0].integer==1&&progress->children[1].integer==16383&&progress->children[2].integer==1
      &&progress->children[3].integer==16383&&progress->children[4].integer==0,"original logical intersections include virtual pads and real empty slot");
  auto inventory=doc(read(a.profile/"downloads.config"));
  check(inventory.find("downloads")->children[0].find("completedbytes")->integer==32768,"original completedbytes is logical checked span, not two payload bytes");
  check(take(storage::originalTorrentInfoSubject(saved))==subject,"checkpoint does not rewrite differently sorted raw info");
}
void canonicalExternalTreeCannotReindexOriginalGeometry() {
  Area a;const auto bytes=orderingMetadata(false);const auto key=a.importMetadata(bytes);
  const auto saved=read(a.state(key)),inventory=read(a.profile/"downloads.config");
  auto owner=Owner::prepare(*a.lease,key,{});
  check(!owner&&owner.error().code==Code::Unsupported,"external UTF8 order versus Java UTF16 index mismatch remains fail closed");
  check(read(a.state(key))==saved&&read(a.profile/"downloads.config")==inventory&&!fs::exists(a.destination),"no guessed reindex or payload mutation on geometry mismatch");
}

void syntheticTailUsesOnlyEnginePadSlot() {
  Area a;const auto bytes=metainfo(false);const auto source=doc(bytes);const auto key=a.importMetadata(bytes);
  const auto raw=take(storage::originalTorrentInfoSubject(source));
  auto owner=take(Owner::prepare(*a.lease,key,{}));const auto params=owner.parameters();
  check(params.ti->num_files()==2&&params.file_priorities.size()==2
      &&params.file_priorities[1]==lt::download_priority_t{0},"actual engine-only pad has explicit zero priority");
  check(owner.originalBytes()==1&&owner.originalProgressRequiresPieces(),"original logical size remains independent of engine tail");
  check(take(owner.verifiedOriginalBytes(std::array<std::uint8_t,1>{0}))==0
      &&take(owner.verifiedOriginalBytes(std::array<std::uint8_t,1>{1}))==1,"checked tail projection never counts synthetic bytes");
  check(owner.mappedFiles()!=nullptr,"extra virtual pad forces authenticated descriptor IO");
  const std::array<char,1> payload{'x'};done(owner.mappedFiles()->write(0,payload));std::string observed(16384,'?');
  done(owner.mappedFiles()->read(0,std::span<char>(observed)));
  check(observed[0]=='x'&&std::all_of(observed.begin()+1,observed.end(),[](char c){return c==0;}),"actual descriptor data plus engine pad reads zero without a file");
  done(owner.mappedFiles()->write(1,std::span<const char>(observed).subspan(1)));
  check(read(a.destination/"identity.bin")=="x"&&!fs::exists(a.destination/".pad"),"pad writes never touch payload or create padding paths");
  done(owner.checkpoint(*a.lease,std::array<std::uint8_t,1>{1},true,1,0));
  auto saved=doc(read(a.state(key)));const auto* attrs=saved.find("attributes");
  check(attrs->find("storetypes")->children.size()==1&&attrs->find("filedownloaded")->find("downloaded")->children.size()==1
      &&attrs->find("filedownloaded")->find("downloaded")->children[0].integer==1,"original controls/types/progress retain exactly one real slot");
  check(saved.find("resume")->find("data")->find("resume data")->bytes==std::string(1,'\1')
      &&saved.find("resume")->find("data")->find("valid")->integer==1,"actual complete original piece count stays authoritative");
  auto inventory=doc(read(a.profile/"downloads.config"));
  check(inventory.find("downloads")->children[0].find("completedbytes")->integer==1,"synthetic LT padding is never original completedbytes");
  check(take(storage::originalTorrentInfoSubject(saved))==raw&&!fs::exists(a.destination/".pad"),"raw hash subject and physical namespace unchanged");
}
void mappedPayloadExternalMutationStillFailsClosed() {
  Area a;const auto key=a.importMetadata(metainfo(false));
  auto owner=take(Owner::prepare(*a.lease,key,{}));check(owner.mappedFiles()!=nullptr,"actual synthetic-tail owner required");
  write(a.destination/"identity.bin","x");std::array<char,1> observed{};
  const auto result=owner.mappedFiles()->read(0,observed);
  check(!result&&result.error().code==Code::Conflict,"external payload mutation is still rejected by authenticated original IO");
}
void projectionRejectsNonTailDifferences() {
  const auto bytes=metainfo(false);const lt::torrent_info info(lt::span<const char>(bytes.data(),bytes.size()),lt::from_span);
  auto layout=compat::originalLayout(doc(bytes));check(layout.has_value(),"actual original single-v2 layout");
  const auto actual=take(engine::detail::projectOriginalLayout(*layout,info));
  check(actual.trailingPad==std::optional<std::size_t>{1}&&actual.originalFiles==1&&actual.engineFiles==2
      &&actual.originalBytes==1&&actual.engineBytes==16384,"one exact source-derived tail range admitted");
  auto changed=*layout;changed.files[0].path="different.bin";check(!engine::detail::projectOriginalLayout(changed,info),"changed source path remains refused");
  changed=*layout;changed.files[0].offset=1;check(!engine::detail::projectOriginalLayout(changed,info),"changed original offset remains refused");
  changed=*layout;changed.files[0].padding=true;check(!engine::detail::projectOriginalLayout(changed,info),"real/pad relabel remains refused");
  changed=*layout;changed.bytes=2;check(!engine::detail::projectOriginalLayout(changed,info),"changed original total remains refused");
  check(!engine::detail::verifiedOriginalBytes(1,16384,std::array<std::uint8_t,2>{1,0})
      &&!engine::detail::verifiedOriginalBytes(1,16384,std::array<std::uint8_t,1>{2}),"verified bitmap arity/value cannot be fabricated");
  const auto ordered=orderingMetadata(true);const lt::torrent_info multi(lt::span<const char>(ordered.data(),ordered.size()),lt::from_span);
  auto withEmpty=compat::originalLayout(doc(ordered));check(withEmpty&&withEmpty->files.back().bytes==0,"real trailing empty fixture");
  withEmpty->files.pop_back();check(!engine::detail::projectOriginalLayout(*withEmpty,multi),"a real empty entry cannot be dropped as an engine-only pad");
}

}
int main(){try{check(std::setlocale(LC_CTYPE, "")!=nullptr,"Initialize actual filesystem character locale");std::cerr<<"CASE syntheticTailUsesOnlyEnginePadSlot\n";syntheticTailUsesOnlyEnginePadSlot();++families;std::cerr<<"CASE mappedPayloadExternalMutationStillFailsClosed\n";mappedPayloadExternalMutationStillFailsClosed();++families;std::cerr<<"CASE projectionRejectsNonTailDifferences\n";projectionRejectsNonTailDifferences();++families;std::cerr<<"CASE originalJavaOrderAndPaddedProgress\n";originalJavaOrderAndPaddedProgress();++families;std::cerr<<"CASE canonicalExternalTreeCannotReindexOriginalGeometry\n";canonicalExternalTreeCannotReindexOriginalGeometry();++families;std::cerr<<"CASE selectedImportAndOfflineOperations\n";selectedImportAndOfflineOperations();++families;std::cerr<<"CASE duplicateAliasesAndMalformedSelectors\n";duplicateAliasesAndMalformedSelectors();++families;std::cerr<<"CASE stoppedHybridCompanionPreservesOriginalAuthority\n";stoppedHybridCompanionPreservesOriginalAuthority();++families;std::cerr<<"CASE hybridAliasRejectsRetainedLinkedNonlinearAndManualUnsafeSources\n";hybridAliasRejectsRetainedLinkedNonlinearAndManualUnsafeSources();++families;std::cerr<<"CASE hybridAliasRefusesOriginalUnloadableEmptyHybridV2\n";hybridAliasRefusesOriginalUnloadableEmptyHybridV2();++families;std::cerr<<"CASE hybridAliasRecoveryRetainsSourceProofAndOldJournalCompatibility\n";hybridAliasRecoveryRetainsSourceProofAndOldJournalCompatibility();++families;std::cerr<<"CASE explicit32AndRetainedIdentity\n";explicit32AndRetainedIdentity();++families;std::cerr<<"CASE cachedSelectionAndRecovery\n";cachedSelectionAndRecovery();++families;std::cerr<<"CASE rawSubjectBinding\n";rawSubjectBinding();++families;std::cout<<"PASS "<<families<<" original-identity caller families, "<<assertions<<" assertions; generated data and actual LT/OpenSSL digests, no engine activation or Java equality credit\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
