#include "biglybtc/original_file_links.hpp"
#include "managed_storage.hpp"
#include "verified_existing_scope.hpp"
#include "queue_positions.hpp"
#include "managed_payload_counters.hpp"
#include "linear_to_compact.hpp"
#include "original_layout_projection.hpp"
#include "original_torrent_identity_lt.hpp"
#include "original_disk_io.hpp"
#include "original_control.hpp"
#include "original_storage_defaults.hpp"
#include "original_filename_policy.hpp"
#include "local_support.hpp"
#include "network_policy.hpp"
#include "torrent_state.hpp"
#include "encoded_document.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/hasher.hpp>
#include "biglybtc/torrent_intake_trackers.hpp"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <map>
#include <set>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::engine::detail {
namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
constexpr std::string_view JournalKey = "BiglyBTC.completion";
struct Journal {
  std::size_t index;
  fs::path source, target;
  std::uint64_t device, inode;
  bool linked = false;
};
struct Slot {
  std::size_t index;
  fs::path base, actual, final;
  std::int64_t bytes;
  bool padding, skipped;
  storage::OriginalStorageKind kind=storage::OriginalStorageKind::Linear;
  storage::OriginalFileGeometry geometry{};
  bool expanding=false;
  Descriptor parent{-1}, data{-1};
  struct stat identity{};
};
bool safeManagedRelative(const fs::path& path) {
  if (path.empty() || path.is_absolute() || path.lexically_normal() != path || path.native().size()>4096
      || path.native().find('\n')!=std::string::npos || path.native().find('\0')!=std::string::npos) { return false; }
  for (const auto& part : path) { if (part.empty() || part=="." || part==".." || part.native().size()>255) { return false; } }
  return true;
}
bool safeManagedPhysical(const fs::path& path) {
  return safeManagedRelative(path)||(path.is_absolute()&&path.lexically_normal()==path&&safeManagedRelative(path.relative_path()));
}
bool sameFile(const struct stat& a, const struct stat& b) {
  return S_ISREG(a.st_mode)&&S_ISREG(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;
}
std::string intValue(std::int64_t value) { return "i"+std::to_string(value)+"e"; }
const Node& entryFor(const Document& inventory, const core::SavedDownload& item, bool legacy) {
  return legacy ? inventory.root().children.at(item.sourceIndex*2+1) : inventory.find("downloads")->children.at(item.sourceIndex);
}
core::SavedDownload selected(const Document& inventory, const core::TorrentHash& hash) {
  auto parsed = core::readLibrary(inventory);
  require(parsed.has_value(),"Invalid managed saved inventory",ErrorCode::InvalidFormat);
  for (const auto& issue : parsed->issues) {
    if (issue.pauseEntry || !parsed->legacyMap) { throw Failure{{ErrorCode::InvalidFormat,"Malformed managed inventory"}}; }
    const auto& key = inventory.root().children.at(issue.sourceIndex*2);
    const auto& value = inventory.root().children.at(issue.sourceIndex*2+1);
    require(key.bytes=="pause_data" || (!value.find("torrent")&&!value.find("torrent_hash")&&!value.find("save_dir")&&!value.find("path")),
        "Malformed legacy managed inventory",ErrorCode::InvalidFormat);
  }
  const core::SavedDownload* found = nullptr;
  std::set<std::string> hashes;
  for (const auto& item : parsed->downloads) {
    if (item.hash) {
      require(hashes.insert(item.hash->hex()).second,"Duplicate managed saved identity",ErrorCode::Conflict);
      if (*item.hash==hash) { found=&item; }
    }
  }
  require(found!=nullptr,"Managed target is stale or unavailable",ErrorCode::Conflict);
  return *found;
}
Document authoritative(const storage::ProfileLease& lease, const storage::TorrentStateFile& state) {
  auto result = state.document();
  Descriptor active(::openat(lease.directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  require(active.get()>=0,"Original active state directory unavailable",ErrorCode::UnsafePath);
  const auto raw = take(storage::detail::readFile(active.get(),"cache.dat",storage::compatibleConfigLimits().inputBytes));
  if (!raw) { return result; }
  auto cache = storage::EncodedDocument::parse(*raw,storage::compatibleConfigLimits());
  require(cache&&cache->compressed(),"Invalid authoritative original state cache",ErrorCode::InvalidFormat);
  const auto* entries=cache->document().find("state");
  if (!entries) { return result; }
  require(entries->type==Node::Type::List,"Invalid authoritative state-cache list",ErrorCode::InvalidFormat);
  const auto wanted=state.hash().bytes();
  const std::string_view key(reinterpret_cast<const char*>(wanted.data()),wanted.size());
  const Node* chosen=nullptr;
  for (const auto& item : entries->children) {
    const auto* value=item.find("hash"); if (value&&value->type==Node::Type::Bytes&&value->bytes==key) { chosen=&item; }
  }
  if (chosen) {
    for (auto [from,to] : {std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}) {
      if (const auto* value=chosen->find(from)) {
        require(value->type==Node::Type::Dictionary,"Invalid authoritative cached section",ErrorCode::InvalidFormat);
        put(result,to,encoded(*value));
      }
    }
  }
  return result;
}
std::vector<Journal> journals(const Node& attributes, bool multifile, std::size_t files) {
  const auto* value=attributes.find(JournalKey); if (!value) { return {}; }
  require(value->type==Node::Type::Dictionary,"Invalid completion journal",ErrorCode::InvalidFormat);
  if (value->children.empty()) { return {}; }
  const auto version=integer(value->find("version"),-1);
  require(version==(multifile?2:1)||version==(multifile?4:3),"Unsupported completion journal version");
  std::vector<Journal> result;
  const auto append=[&](const Node& item,std::size_t index) {
    const auto device=integer(item.find("device"),-1),inode=integer(item.find("inode"),-1);
    require(index<files&&device>=0&&inode>=0,"Invalid completion journal identity",ErrorCode::InvalidFormat);
    Journal entry{index,fs::path(text(item.find("source"))),fs::path(text(item.find("target"))),
      static_cast<std::uint64_t>(device),static_cast<std::uint64_t>(inode)};
    require((version>=3?safeManagedPhysical(entry.source)&&safeManagedPhysical(entry.target):safeManagedRelative(entry.source)&&safeManagedRelative(entry.target))&&entry.source!=entry.target
        &&entry.source.parent_path()==entry.target.parent_path(),"Unsafe completion journal",ErrorCode::UnsafePath);
    result.push_back(std::move(entry));
  };
  if (!multifile) { append(*value,0); }
  else {
    const auto* list=value->find("files"); require(list&&list->type==Node::Type::List&&list->children.size()<=files,"Invalid completion journal files",ErrorCode::InvalidFormat);
    std::set<std::size_t> indices;
    for (const auto& item : list->children) {
      const auto index=integer(item.find("index"),-1);
      require(index>=0&&static_cast<std::uint64_t>(index)<files&&indices.insert(static_cast<std::size_t>(index)).second,"Duplicate/invalid completion journal file",ErrorCode::InvalidFormat);
      append(item,static_cast<std::size_t>(index));
    }
  }
  return result;
}
void saveJournal(storage::TorrentStateFile& state, const storage::TorrentStateStore& store,
    const storage::ProfileLease& lease, bool multifile, const std::vector<Journal>& pending) {
  auto attributes=dictionary(state.document().find("attributes")); auto value=dictionary(nullptr);
  const auto record=[](const Journal& item,bool indexed) {
    auto record=dictionary(nullptr);
    if (indexed) { put(record,"index",intValue(static_cast<std::int64_t>(item.index))); }
    put(record,"source",stringValue(item.source.string())); put(record,"target",stringValue(item.target.string()));
    put(record,"device",intValue(static_cast<std::int64_t>(item.device))); put(record,"inode",intValue(static_cast<std::int64_t>(item.inode)));
    return record.serialize();
  };
  if (!pending.empty()) {
    const bool absolute=std::ranges::any_of(pending,[](const auto& item){return item.source.is_absolute()||item.target.is_absolute();});
    if (multifile) {
      put(value,"version",absolute?"i4e":"i2e"); std::string list="l"; for (const auto& item:pending) { list+=record(item,true); } put(value,"files",list+'e');
    } else {
      require(pending.size()==1&&pending[0].index==0,"Invalid single-file completion journal",ErrorCode::InvalidFormat);
      value=take(Document::parse(record(pending[0],false),storage::compatibleConfigLimits()).transform_error([](const auto&) { return Error{ErrorCode::InvalidFormat,"Invalid completion journal encoding"}; }));
      put(value,"version",absolute?"i3e":"i1e");
    }
  }
  put(attributes,JournalKey,value.serialize()); done(state.replaceSection("attributes",attributes.serialize())); done(store.save(lease,state));
}
Descriptor parentDirectory(const fs::path& path, bool create) {
  require(path.is_absolute()&&path.lexically_normal()==path&&path.native().find('\n')==std::string::npos,"Unsafe managed payload directory",ErrorCode::UnsafePath);
  auto current=Descriptor(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC)); require(current.get()>=0,"Cannot open filesystem root",ErrorCode::Io);
  for (const auto& part:path.relative_path()) {
    require(part!="."&&part!=".."&&!part.empty()&&part.native().size()<=255,"Unsafe managed directory component",ErrorCode::UnsafePath);
    int next=::openat(current.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (next<0&&errno==ENOENT) {
      struct stat parent{}; require(::fstat(current.get(),&parent)==0&&parent.st_uid==::geteuid(),"Missing payload directory requires an owned parent",ErrorCode::UnsafePath);
      if (!create) { return Descriptor(-1); }
      require(::mkdirat(current.get(),part.c_str(),0700)==0||errno==EEXIST,"Cannot create managed payload directory",ErrorCode::Io);
      sync(current.get()); next=::openat(current.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
      struct stat made{}; require(next>=0&&::fstat(next,&made)==0&&made.st_uid==::geteuid(),"New managed directory ownership changed",ErrorCode::UnsafePath);
    }
    require(next>=0,"Managed payload directory is inaccessible or symbolic",ErrorCode::UnsafePath); current=Descriptor(next);
  }
  return current;
}
bool sameExistingSnapshot(const struct stat& a,const struct stat& b) {
  return sameFile(a,b)&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size
      &&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
      &&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
bool inspect(Slot& slot, const fs::path& save, const Journal* journal, bool open,bool readOnly=false) {
  slot.parent=parentDirectory((save/slot.actual).parent_path(),false);
  if (slot.parent.get()<0) { return false; }
  struct stat named{};
  if (::fstatat(slot.parent.get(),slot.actual.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0) {
    require(errno==ENOENT,"Cannot inspect managed payload",ErrorCode::UnsafePath);
    struct stat parent{}; require(::fstat(slot.parent.get(),&parent)==0&&parent.st_uid==::geteuid(),"Missing managed file requires an owned parent",ErrorCode::UnsafePath);
    return false;
  }
  require(S_ISREG(named.st_mode)&&named.st_uid==::geteuid()&&named.st_size>=0&&static_cast<std::uint64_t>(named.st_size)<=take(storage::originalMaximumPhysicalSize(slot.geometry,slot.expanding?storage::OriginalStorageKind::Linear:slot.kind))
      &&named.st_nlink>=1&&named.st_nlink<=2,"Unsafe managed payload type, ownership, size or links",ErrorCode::UnsafePath);
  if (slot.identity.st_ino) { require(readOnly?sameExistingSnapshot(slot.identity,named):sameFile(slot.identity,named),"Managed payload identity changed after preflight",ErrorCode::Conflict); }
  if (journal) {
    require((slot.actual==journal->source||slot.actual==journal->target)&&named.st_dev==journal->device&&named.st_ino==journal->inode,"Completion journal differs from payload",ErrorCode::Conflict);
  }
  if (named.st_nlink==2) {
    require(journal!=nullptr,"Unrecognized managed payload hard link",ErrorCode::UnsafePath);
    const auto alias=slot.actual==journal->source?journal->target:journal->source; struct stat other{};
    require(::fstatat(slot.parent.get(),alias.filename().c_str(),&other,AT_SYMLINK_NOFOLLOW)==0&&sameFile(named,other),"Unrecognized completion alias",ErrorCode::UnsafePath);
  }
  if (open) {
    slot.data=Descriptor(::openat(slot.parent.get(),slot.actual.filename().c_str(),(readOnly?O_RDONLY:O_RDWR)|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    require(slot.data.get()>=0&&::fstat(slot.data.get(),&slot.identity)==0&&(readOnly?sameExistingSnapshot(named,slot.identity):sameFile(named,slot.identity)),"Managed payload changed while opening",ErrorCode::Conflict);
  } else { slot.identity=named; }
  return true;
}
std::expected<void,Error> validateExistingPayload(const fs::path& path,int parent,int descriptor,const struct stat& expected) {
  try{auto namedParent=parentDirectory(path.parent_path(),false);struct stat namedDirectory{},heldDirectory{},held{},named{};
    require(namedParent.get()>=0&&::fstat(namedParent.get(),&namedDirectory)==0&&::fstat(parent,&heldDirectory)==0
        &&namedDirectory.st_dev==heldDirectory.st_dev&&namedDirectory.st_ino==heldDirectory.st_ino,
        "Existing-only payload parent changed",ErrorCode::Conflict);
    require(::fstat(descriptor,&held)==0&&::fstatat(parent,path.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
        &&sameExistingSnapshot(expected,held)&&sameExistingSnapshot(expected,named),
        "Existing-only payload identity/content metadata changed",ErrorCode::Conflict);return {};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Cannot validate existing-only named payload"});}
}
std::function<std::expected<void,Error>()> existingPayloadProof(const Slot& slot,const fs::path& save,int descriptor) {
  auto parent=std::make_shared<Descriptor>(::fcntl(slot.parent.get(),F_DUPFD_CLOEXEC,3));
  require(parent->get()>=0,"Cannot retain existing-only parent",ErrorCode::Io);
  const auto path=(save/slot.actual).lexically_normal();const auto expected=slot.identity;
  return [parent,path,expected,descriptor]{return validateExistingPayload(path,parent->get(),descriptor,expected);};
}
void existingOnlyAttributes(const Node& attributes,bool multifile,std::size_t files,bool approvedLinks=false) {
  require(journals(attributes,multifile,files).empty(),"Existing-only verification cannot complete pending payload names");
  if(const auto* links=attributes.find("filelinks2")){require(links->type==Node::Type::List&&(approvedLinks||links->children.empty()),"Existing-only linked verification requires an authenticated mapped scope");}
  for(const auto* key:{"incompfilesuffix","dnd_pfx","dnd_sf","dnd_al","BiglyBTC.compactConversion"}){if(const auto* value=attributes.find(key)){
    require((value->type==Node::Type::List&&value->children.empty())||(value->type==Node::Type::Bytes&&value->bytes.empty()),"Existing-only verification cannot redirect or complete payload paths");}}
}
std::vector<fs::path> existingMappedPaths(const core::SavedDownload& saved,const compat::OriginalLayout& layout,const Node& attributes,const fs::path& save) {
  std::vector<std::string> bases;for(const auto& file:layout.files){bases.push_back((save/(layout.multifile?fs::path(*saved.saveFile)/file.path:fs::path(*saved.saveFile))).string());}
  const auto links=compat::resolveOriginalFileLinks(attributes.find("filelinks2"),bases);require(links.has_value(),"Invalid existing mapped file links",ErrorCode::InvalidFormat);
  std::vector<fs::path> paths(layout.files.size());for(std::size_t n=0;n<layout.files.size();++n){if(layout.files[n].padding){require(!links->byIndex[n],"Virtual padding cannot have an existing mapped target");continue;}paths[n]=links->byIndex[n]?fs::path(links->byIndex[n]->target):fs::path(bases[n]);}
  return paths;
}
std::vector<Slot> plan(const core::SavedDownload& saved, const Node& attributes, const compat::OriginalLayout& layout,
    const std::vector<compat::FilePolicy>& policy, const fs::path& save, const std::vector<Journal>& pending, const fs::path& profile,bool readOnly=false) {
  require(!saved.legacySavePath&&saved.saveFile&&safeManagedRelative(fs::path(*saved.saveFile))&&!fs::path(*saved.saveFile).has_parent_path(),"Unsupported original save-root mapping",ErrorCode::UnsafePath);
  for (const auto* key : {"dnd_pfx","dnd_sf","dnd_al"}) {
    if (const auto* field=attributes.find(key)) { require(field->type==Node::Type::Bytes&&field->bytes.size()<=4096,"Invalid original DND policy",ErrorCode::InvalidFormat); }
  }
  const auto* stores=attributes.find("storetypes");
  require(stores&&stores->type==Node::Type::List&&stores->children.size()==layout.files.size(),"Storage-type count differs from metadata");
  std::vector<Slot> result; result.reserve(layout.files.size());
  for (std::size_t index=0;index<layout.files.size();++index) {
    const auto type=text(&stores->children[index]);
    require(type=="L"||type=="C"||type=="R"||type=="X","Unknown original storage kind");
    const auto& file=layout.files[index]; const auto base=layout.multifile?fs::path(*saved.saveFile)/file.path:fs::path(*saved.saveFile);
    require(safeManagedRelative(base),"Unsafe managed payload path",ErrorCode::UnsafePath);
    result.push_back({index,base,base,base,file.bytes,file.padding,policy[index].skipped});
    auto& slot=result.back();
    slot.kind=type=="L"?storage::OriginalStorageKind::Linear:type=="C"?storage::OriginalStorageKind::Compact:type=="R"?storage::OriginalStorageKind::Reordered:storage::OriginalStorageKind::ReorderedCompact;
    slot.geometry={static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout.pieceLength)};
    (void)take(storage::originalMaximumPhysicalSize(slot.geometry,slot.kind));
  }
  std::set<std::size_t> linked;
  std::vector<std::string> originalLinkBases; originalLinkBases.reserve(result.size());
  for (const auto& slot:result) { originalLinkBases.push_back((save/slot.base).native()); }
  const auto links=compat::resolveOriginalFileLinks(attributes.find("filelinks2"),originalLinkBases);
  if (!links) {
    const auto code=links.error().code==compat::OriginalFileLinksErrorCode::Memory?ErrorCode::Io:
        links.error().code==compat::OriginalFileLinksErrorCode::AmbiguousBase?ErrorCode::UnsafePath:ErrorCode::InvalidFormat;
    require(false,std::string(links.error().message),code);
  }
  for (std::size_t index=0;index<result.size();++index) {
    if (!links->byIndex[index]) { continue; }
    auto& slot=result[index]; const fs::path target(links->byIndex[index]->target);
    require(target.is_absolute()&&target.lexically_normal()==target&&target.native().size()<=4096&&target.native().find('\0')==std::string::npos,"Unsafe external file link",ErrorCode::UnsafePath);
    const auto within=target.lexically_relative(save);
    slot.actual=safeManagedRelative(within)?within:target; linked.insert(index);
  }
  const auto suffix=attributes.find("incompfilesuffix")?text(attributes.find("incompfilesuffix")):std::string{};
  require(suffix.find('/')==std::string::npos&&suffix.find('\n')==std::string::npos,"Unsafe incomplete suffix",ErrorCode::UnsafePath);
  std::map<std::size_t,const Journal*> byIndex; for (const auto& item:pending) { byIndex.emplace(item.index,&item); }
  std::map<std::size_t,std::pair<std::uint64_t,std::uint64_t>> expanding;
  if (const auto* value=attributes.find("BiglyBTC.compactConversion")) {
    require(value->type==Node::Type::List&&value->children.size()<=result.size(),"Invalid compact conversion journal",ErrorCode::InvalidFormat);
    for (const auto& item:value->children) {
      const auto index=integer(item.find("index"),-1),dev=integer(item.find("device"),-1),ino=integer(item.find("inode"),-1);
      require(index>=0&&static_cast<std::size_t>(index)<result.size()&&dev>=0&&ino>0&&expanding.emplace(static_cast<std::size_t>(index),std::pair{static_cast<std::uint64_t>(dev),static_cast<std::uint64_t>(ino)}).second,"Invalid compact conversion identity",ErrorCode::InvalidFormat);
      auto& slot=result[static_cast<std::size_t>(index)];require(slot.kind==storage::OriginalStorageKind::Compact||slot.kind==storage::OriginalStorageKind::Linear,"Conversion journal storage differs",ErrorCode::Conflict);slot.expanding=true;
    }
  }
  std::set<fs::path> physical; std::set<std::pair<std::uint64_t,std::uint64_t>> inodes;
  for (auto& slot:result) {
    if (slot.padding) { require(!byIndex.contains(slot.index),"Padding cannot own completion journal"); continue; }
    const auto found=byIndex.find(slot.index); const auto* journal=found==byIndex.end()?nullptr:found->second;
    require(!journal||slot.actual==journal->source||slot.actual==journal->target,"Journal path differs from mapping",ErrorCode::Conflict);
    if (!inspect(slot,save,journal,true,readOnly)) {
      if (journal) {
        slot.actual=slot.actual==journal->source?journal->target:journal->source;
        require(inspect(slot,save,journal,true,readOnly),"Both completion journal paths are absent",ErrorCode::Conflict);
      } else if (!linked.contains(slot.index)&&!suffix.empty()) {
        slot.actual+=suffix; require(safeManagedRelative(slot.actual),"Unsafe incomplete filename",ErrorCode::UnsafePath); inspect(slot,save,nullptr,true,readOnly);
      }
    }
    slot.final=slot.actual; const auto absolute=(save/slot.actual).lexically_normal();requireUtf8Filesystem(absolute);
    const auto outside=absolute.lexically_relative(profile);
    require(!outside.empty()&&*outside.begin()=="..","Managed payload overlaps profile",ErrorCode::UnsafePath);
    for (const auto& part:absolute) { require(part!=".git"&&!part.native().starts_with(".BiglyBTC"),"Managed payload overlaps reserved path",ErrorCode::UnsafePath); }
    if (const auto found=expanding.find(slot.index);found!=expanding.end()) {require(slot.identity.st_ino&&found->second==std::pair{static_cast<std::uint64_t>(slot.identity.st_dev),static_cast<std::uint64_t>(slot.identity.st_ino)},"Compact conversion payload changed",ErrorCode::Conflict);}
    require(physical.insert(absolute).second,"Two files alias the same managed target",ErrorCode::UnsafePath);
    if (slot.identity.st_ino) { require(inodes.emplace(slot.identity.st_dev,slot.identity.st_ino).second,"Two files alias the same managed inode",ErrorCode::UnsafePath); }
  }
  for (const auto& path:physical) {
    auto parent=path.parent_path(); while (!parent.empty()&&parent!=parent.root_path()) { require(!physical.contains(parent),"Managed file aliases another file's directory",ErrorCode::UnsafePath); parent=parent.parent_path(); }
  }
  return result;
}
struct PayloadClaimPlan {
  std::vector<ManagedPayloadClaim> values;
  std::vector<std::size_t> indices;
};
bool pathWithin(const fs::path& child,const fs::path& parent) {
  auto a=child.begin();for(auto b=parent.begin();b!=parent.end();++a,++b){if(a==child.end()||*a!=*b){return false;}}return true;
}
PayloadClaimPlan payloadClaimPlan(const std::vector<Slot>& mapping,const fs::path& save,
    const Node& attributes,const std::vector<Journal>& pending) {
  PayloadClaimPlan result;std::map<fs::path,std::size_t> paths;
  const auto suffix=attributes.find("incompfilesuffix")?text(attributes.find("incompfilesuffix")):std::string{};
  const auto prefix=attributes.find("dnd_pfx")?text(attributes.find("dnd_pfx")):std::string{};
  auto add=[&](const fs::path& relative,std::size_t index) {
    const auto absolute=(save/relative).lexically_normal();require(absolute.is_absolute()&&safeManagedPhysical(absolute),"Unsafe managed claim path",ErrorCode::UnsafePath);
    const auto [at,inserted]=paths.emplace(absolute,index);if(!inserted){require(at->second==index,"Managed files share a current/completion path",ErrorCode::Conflict);return;}
    require(result.values.size()<MaximumManagedPayloadClaims,"Managed payload claim bound exceeded",ErrorCode::Unsupported);
    const auto& id=mapping[index].identity;result.values.push_back({absolute,static_cast<std::uint64_t>(id.st_dev),static_cast<std::uint64_t>(id.st_ino)});result.indices.push_back(index);
  };
  for(const auto& slot:mapping) {
    if(slot.padding){continue;}add(slot.actual,slot.index);add(slot.final,slot.index);
    const auto name=slot.actual.filename().string();
    if(!suffix.empty()&&name.size()>suffix.size()&&name.ends_with(suffix)) {
      auto next=name.substr(0,name.size()-suffix.size());if(!slot.skipped&&!prefix.empty()&&next.starts_with(prefix)){next.erase(0,prefix.size());}
      auto target=slot.actual;target.replace_filename(next);require(!next.empty()&&safeManagedPhysical(target)&&target.filename()!=".git"&&!next.starts_with(".BiglyBTC"),"Unsafe forecast completion name",ErrorCode::UnsafePath);add(target,slot.index);
    }
  }
  for(const auto& entry:pending){add(entry.source,entry.index);add(entry.target,entry.index);}
  for(const auto& [path,index]:paths){(void)index;for(auto parent=path.parent_path();!parent.empty()&&parent!=parent.parent_path();parent=parent.parent_path()){require(!paths.contains(parent),"Managed file claims another file's directory",ErrorCode::Conflict);}}
  return result;
}
void checkManagedClaims(std::span<const ManagedPayloadClaim> candidate,
    std::span<const ManagedPayloadClaim> existing) {
  require(existing.size()<=MaximumManagedPayloadClaims,"Retained payload claim bound exceeded",ErrorCode::Unsupported);
  require(candidate.size()<=MaximumManagedPayloadClaims-existing.size(),"Combined payload claim bound exceeded",ErrorCode::Unsupported);
  std::set<fs::path> paths;std::set<std::pair<std::uint64_t,std::uint64_t>> inodes;
  for(const auto& claim:existing){require(claim.absolutePath.is_absolute()&&safeManagedPhysical(claim.absolutePath)&&claim.absolutePath!=fs::path("/")&&(claim.inode||!claim.device),"Malformed retained payload claim",ErrorCode::InvalidFormat);paths.insert(claim.absolutePath);if(claim.inode){inodes.emplace(claim.device,claim.inode);}}
  for(const auto& claim:candidate) {
    bool overlap=paths.contains(claim.absolutePath);
    for(auto parent=claim.absolutePath.parent_path();!overlap&&!parent.empty()&&parent!=parent.parent_path();parent=parent.parent_path()){overlap=paths.contains(parent);}
    const auto descendant=paths.lower_bound(claim.absolutePath);overlap=overlap||(descendant!=paths.end()&&pathWithin(*descendant,claim.absolutePath));
    require(!overlap&&(!claim.inode||!inodes.contains({claim.device,claim.inode})),"Payload path/inode is retained by another managed admission",ErrorCode::Conflict);
  }
}
TorrentNetworkSelection networks(const Document& state, const Node& attributes) {
  TorrentNetworkSelection result;
  if (const auto* list=attributes.find("networks")) {
    require(list->type==Node::Type::List&&list->children.size()<=16,"Invalid original network selection",ErrorCode::InvalidFormat);
    result.publicNetwork=false;
    for (const auto& item:list->children) {
      const auto name=text(&item);
      if (name=="Public") { result.publicNetwork=true; } else if (name=="Tor") { result.tor=true; }
      else if (name=="I2P") { result.i2p=true; } else { require(false,"Unknown original torrent network"); }
    }
  }
  if (const auto* az=state.find("azureus_properties")) {
    require(az->type==Node::Type::Dictionary,"Invalid original Azureus properties",ErrorCode::InvalidFormat);
    if (const auto* backup=az->find("dht_backup_enable")) { result.dhtBackup=integer(backup)==1; }
  }
  return result;
}
// Called only on this fresh, privately owned LT parse AFTER its complete raw
// info/full hashes and selected original key have been authenticated. This
// projects a qualified v1 filename codec, never a file/piece geometry change.
void projectManagedV1Names(const Document& source,const storage::OriginalTorrentIdentity& identity,
    const compat::OriginalLayout& original,lt::torrent_info& metadata) {
  const auto* info=source.find("info");
  if(!identity.full.v1||identity.full.v2||!info||info->find("file tree")){return;}
  bool qualifiedNames=info->find("name.utf-8")!=nullptr;
  if(const auto* encoding=source.find("encoding")){
    auto name=text(encoding);
    std::ranges::transform(name,name.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
    qualifiedNames=qualifiedNames||name=="iso-8859-1"||name=="windows-1251"||name=="cp1251"||name=="cp5347"||name=="ansi-1251"
        ||name=="koi8-r"||name=="koi8_r"||name=="koi8"||name=="cskoi8r";
  }
  if(const auto* files=info->find("files")){
    for(const auto& file:files->children){qualifiedNames=qualifiedNames||file.find("path.utf-8")!=nullptr;}
  }
  if(!qualifiedNames){return;} // All unrelated metadata keeps the strict path gate.
  const auto& files=metadata.files();
  require(original.bytes>0&&original.pieceLength>0&&original.files.size()==static_cast<std::size_t>(metadata.num_files())
      &&original.pieceLength==metadata.piece_length()&&original.bytes==metadata.total_size()
      &&original.bytes/original.pieceLength+(original.bytes%original.pieceLength!=0)==metadata.num_pieces(),
      "Original filename projection cannot change file/piece geometry",ErrorCode::Unsupported);
  // Validate EVERY original slot before performing even a runtime-only rename.
  std::int64_t end=0;
  for(std::size_t i=0;i<original.files.size();++i){
    const auto& file=original.files[i];const auto index=lt::file_index_t{static_cast<int>(i)};
    require(file.offset==end&&file.bytes>=0&&file.bytes<=std::numeric_limits<std::int64_t>::max()-end
        &&file.offset==files.file_offset(index)&&file.bytes==files.file_size(index)
        &&file.padding==files.pad_file_at(index),"Original filename projection indices differ",ErrorCode::Unsupported);
    end+=file.bytes;
    if(!file.padding){
      const auto path=original.multifile?fs::path(original.name)/file.path:fs::path(file.path);
      require(safeManagedRelative(path)&&std::ranges::none_of(path.native(),[](unsigned char c){return c<0x20||c==0x7f;}),
          "Original decoded filename needs an unqualified filesystem conversion",ErrorCode::Unsupported);
    }
  }
  require(end==original.bytes,"Original filename projection byte span differs",ErrorCode::Unsupported);
  for(std::size_t i=0;i<original.files.size();++i){
    const auto& file=original.files[i];if(file.padding){continue;}
    metadata.rename_file(lt::file_index_t{static_cast<int>(i)},
        (original.multifile?fs::path(original.name)/file.path:fs::path(file.path)).string());
  }
  const auto after=take(storage::originalTorrentIdentityFromLibtorrent(source,metadata));
  require(after.full==identity.full&&after.originalKey==identity.originalKey&&after.selected==identity.selected,
      "Runtime filename projection changed original raw identity",ErrorCode::Conflict);
}
struct ValidatedMetadata {std::shared_ptr<lt::torrent_info> info;storage::OriginalTorrentIdentity identity;};
ValidatedMetadata metadata(const Document& state, const core::TorrentHash& hash, const compat::OriginalLayout& layout,
    const std::shared_ptr<const ManagedPayloadCounterBridge>& swarmBridge) {
  const auto value=state.serialize(); lt::error_code error;
  auto info=std::make_shared<lt::torrent_info>(lt::span<const char>(value.data(),value.size()),error,lt::from_span);
  require(!error&&info->num_files()>0&&info->num_files()<=100000&&info->num_pieces()>0&&info->num_pieces()<=1000000,"Invalid or unbounded managed metadata",ErrorCode::InvalidFormat);
  auto identity=take(storage::originalTorrentIdentityFromLibtorrent(state,*info));
  done(storage::validateOriginalTorrentKey(identity,hash,true)); // Existing explicit32 reader policy.
  require(!(identity.selected==storage::OriginalSelectedHash::V2&&identity.full.v1)||swarmBridge,
      "Selected-v2 hybrid requires the exact qualified swarm provider",ErrorCode::Unsupported);
  if(layout.filenameConverted){
    // Conversion is derived from THIS authenticated raw source and saved
    // policy. A private metadata copy changes names only; the unchanged shared
    // projection then proves EVERY slot/count/offset/size/pad/piece before the
    // copy can become the admission metadata. No global geometry exception.
    require(layout.files.size()<=static_cast<std::size_t>(info->num_files()),"Filename conversion file count differs",ErrorCode::Conflict);
    auto projected=std::make_shared<lt::torrent_info>(*info);
    for(std::size_t n=0;n<layout.files.size();++n){const auto& file=layout.files[n];if(file.padding){continue;}
      const auto path=layout.multifile?fs::path(layout.name)/file.path:fs::path(file.path);
      require(safeManagedRelative(path)&&std::ranges::none_of(path.native(),[](unsigned char c){return c<0x20||c==0x7f;}),"Converted filename requires separate filesystem policy",ErrorCode::UnsafePath);
      projected->rename_file(lt::file_index_t{static_cast<int>(n)},path.string());
    }
    (void)take(projectOriginalLayout(layout,*projected));
    const auto after=take(storage::originalTorrentIdentityFromLibtorrent(state,*projected));require(after.full==identity.full&&after.originalKey==identity.originalKey&&after.selected==identity.selected,
        "Filename conversion changed raw torrent identity",ErrorCode::Conflict);
    info=std::move(projected);
  }else{projectManagedV1Names(state,identity,layout,*info);}
  (void)take(projectOriginalLayout(layout,*info)); // Full strict geometry/path validation still applies.
  return {std::move(info),std::move(identity)}; // Keep original trackers/web seeds for the explicit policy filter.
}
std::vector<lt::download_priority_t> priorities(const std::vector<compat::FilePolicy>& policy, const compat::OriginalLayout& layout) {
  std::vector<std::int32_t> levels;
  for (std::size_t index=0;index<policy.size();++index) { if (!policy[index].skipped&&!layout.files[index].padding) { levels.push_back(policy[index].priority); } }
  std::ranges::sort(levels); levels.erase(std::unique(levels.begin(),levels.end()),levels.end()); require(levels.size()<=7,"More than seven non-skipped priority ranks are unsupported");
  std::vector<lt::download_priority_t> result; result.reserve(policy.size());
  for (std::size_t index=0;index<policy.size();++index) {
    const auto& file=policy[index]; const auto rank=file.skipped||layout.files[index].padding?0
      : levels.size()==1?4:static_cast<int>(1+std::lower_bound(levels.begin(),levels.end(),file.priority)-levels.begin());
    result.push_back(lt::download_priority_t{static_cast<std::uint8_t>(rank)});
  }
  return result;
}
void writeLinks(storage::TorrentStateFile& state, const std::vector<Slot>& slots, const fs::path& save, bool final) {
  auto attributes=dictionary(state.document().find("attributes")); std::string list="l";
  for (const auto& slot:slots) { const auto& path=final?slot.final:slot.actual;
    if (!slot.padding&&path!=slot.base) { list+=stringValue(std::to_string(slot.index)+'\n'+(save/slot.base).string()+'\n'+(save/path).string()); }
  }
  put(attributes,"filelinks2",list+'e'); done(state.replaceSection("attributes",attributes.serialize()));
}
void writeProgress(storage::TorrentStateFile& state, const std::vector<std::int64_t>& counts,
    std::size_t pieces, bool complete) {
  auto resume=dictionary(state.document().find("resume")); auto data=dictionary(resume.find("data"));
  put(data,"resume data",stringValue(std::string(pieces,complete?'\1':'\2'))); put(data,"valid",complete?"i1e":"i0e"); put(data,"blocks","de");
  put(resume,"data",data.serialize()); done(state.replaceSection("resume",resume.serialize()));
  auto attributes=dictionary(state.document().find("attributes")); put(attributes,"resumecomplete",complete?"i2e":"i1e");
  auto downloaded=dictionary(attributes.find("filedownloaded")); std::string list="l";
  for (const auto value:counts) { list+=intValue(value); } put(downloaded,"downloaded",list+'e'); put(attributes,"filedownloaded",downloaded.serialize());
  done(state.replaceSection("attributes",attributes.serialize()));
}
} // namespace

struct ManagedStorage::Impl {
  core::TorrentHash hash;
  storage::OriginalTorrentIdentity selectedIdentity;
  std::shared_ptr<const ManagedPayloadCounterBridge> swarmBridge;
  core::SavedDownload captured;
  ManagedQueueMetadata queue;
  std::vector<compat::FilePolicy> filePolicies;
  std::string queueMappingScope;
  storage::TorrentStateStore store;
  storage::TorrentStateFile state;
  compat::OriginalLayout layout;
  lt::add_torrent_params params;
  std::shared_ptr<OriginalDiskFiles> mapped;
  fs::path save;
  std::vector<Slot> slots;
  std::vector<Journal> pending;
  std::vector<ManagedPayloadClaim> payloadClaims;
  std::uint64_t profileDevice,profileInode;
  ManagedStoragePurpose purpose=ManagedStoragePurpose::Download;
  std::vector<storage::VerifiedExistingScope::Proof> approvedProofs;
  bool attempted=false;
  bool progressFromPieces=false;
  Impl(core::TorrentHash key,storage::OriginalTorrentIdentity identity,core::SavedDownload selected,storage::TorrentStateStore owner,storage::TorrentStateFile source,
      compat::OriginalLayout geometry,lt::add_torrent_params parameters,fs::path root,std::vector<Slot> mapping,std::vector<Journal> journal,std::vector<ManagedPayloadClaim> claims,
      std::uint64_t device,std::uint64_t inode)
    : hash(std::move(key)),selectedIdentity(std::move(identity)),captured(std::move(selected)),store(std::move(owner)),state(std::move(source)),layout(std::move(geometry)),
      params(std::move(parameters)),save(std::move(root)),slots(std::move(mapping)),pending(std::move(journal)),payloadClaims(std::move(claims)),profileDevice(device),profileInode(inode) {
      progressFromPieces=layout.files.size()!=static_cast<std::size_t>(params.ti->num_files())
          ||std::ranges::any_of(layout.files,[](const auto& file){return file.padding;});
    }
};
ManagedStorage::ManagedStorage(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
ManagedStorage::ManagedStorage(ManagedStorage&&) noexcept=default;
ManagedStorage& ManagedStorage::operator=(ManagedStorage&&) noexcept=default;
ManagedStorage::~ManagedStorage()=default;
lt::add_torrent_params ManagedStorage::parameters() const { return impl_?impl_->params:lt::add_torrent_params{}; }
ManagedStoragePurpose ManagedStorage::purpose() const noexcept {return impl_?impl_->purpose:ManagedStoragePurpose::Download;}
std::expected<void,Error> ManagedStorage::validateExistingSnapshot() const {
  if(!impl_||impl_->purpose!=ManagedStoragePurpose::VerifyExistingReadOnly){return std::unexpected(Error{ErrorCode::Unsupported,"No existing-only snapshot authority"});}
  try{if(!impl_->approvedProofs.empty()){for(const auto& proof:impl_->approvedProofs){const auto checked=proof();if(!checked){return checked;}}return {};}
    for(const auto& slot:impl_->slots){if(slot.padding){continue;}const auto proof=validateExistingPayload((impl_->save/slot.actual).lexically_normal(),slot.parent.get(),slot.data.get(),slot.identity);if(!proof){return proof;}}return {};}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Existing-only snapshot validation failed"});}
}
const ManagedQueueMetadata& ManagedStorage::queueMetadata() const { return impl_->queue; }
const core::SavedDownload& ManagedStorage::capturedDownload() const { return impl_->captured; }
std::span<const compat::FilePolicy> ManagedStorage::capturedFilePolicy() const noexcept { return impl_->filePolicies; }
const std::string& ManagedStorage::capturedQueueMappingScope() const noexcept { return impl_->queueMappingScope; }
std::int64_t ManagedStorage::originalBytes() const noexcept { return impl_?impl_->layout.bytes:0; }
bool ManagedStorage::originalProgressRequiresPieces() const noexcept { return impl_&&impl_->progressFromPieces; }
std::expected<core::QueueFilePriorityStats,core::QueueFilePriorityError> ManagedStorage::filePriorityStatistics(
    std::span<const std::uint8_t> pieces) const noexcept {
  if(!impl_) return std::unexpected(core::QueueFilePriorityError::Geometry);
  return core::queueFilePriorityStatistics(impl_->layout,impl_->filePolicies,pieces);
}
std::expected<std::int64_t,Error> ManagedStorage::verifiedOriginalBytes(std::span<const std::uint8_t> pieces) const {
  if(!impl_) return std::unexpected(Error{ErrorCode::Conflict,"Managed storage was moved or released"});
  return biglybtc::engine::detail::verifiedOriginalBytes(impl_->layout.bytes,impl_->layout.pieceLength,pieces);
}
std::shared_ptr<OriginalDiskFiles> ManagedStorage::mappedFiles() const { return impl_?impl_->mapped:nullptr; }
std::span<const ManagedPayloadClaim> ManagedStorage::claims() const noexcept { return impl_?std::span<const ManagedPayloadClaim>(impl_->payloadClaims):std::span<const ManagedPayloadClaim>{}; }
const storage::OriginalTorrentIdentity& ManagedStorage::identity() const {return impl_->selectedIdentity;}

std::expected<ManagedStorage,Error> ManagedStorage::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,const NetworkPolicy& policy,std::span<const ManagedPayloadClaim> claimed,std::span<const storage::OriginalTorrentIdentity> retainedIdentities,std::shared_ptr<const ManagedPayloadCounterBridge> swarmBridge,ManagedStoragePurpose purpose) {
  try {
    require(purpose==ManagedStoragePurpose::Download||purpose==ManagedStoragePurpose::VerifyExistingReadOnly,"Unknown managed preparation purpose",ErrorCode::InvalidFormat);
    const bool existingOnly=purpose==ManagedStoragePurpose::VerifyExistingReadOnly;
    require(!storage::hasPendingOriginalCompaction(lease),"Recover pending explicit compaction with all profile handles retired before admission",ErrorCode::Conflict);
    require(claimed.size()<=MaximumManagedPayloadClaims,"Retained payload claim bound exceeded",ErrorCode::Unsupported);
    require(retainedIdentities.size()<=MaximumManagedPayloadClaims,"Retained identity bound exceeded",ErrorCode::Unsupported);
    struct stat profile{}; require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0,"Managed storage requires a valid held lease",ErrorCode::UnsafePath);
    auto inventory=take(storage::ConfigFile::load(lease,"downloads.config"));
    require(inventory.recoverySource()!=storage::RecoverySource::Saving,"Resolve saved-inventory recovery before managed preparation",ErrorCode::Conflict);
    const auto rawSaved=selected(inventory.document(),hash);
    auto saved=rawSaved;
    const auto& row=entryFor(inventory.document(),saved,!inventory.document().find("downloads"));
    ManagedQueueMetadata queue;
    queue.position=saved.position.value_or(1);
    queue.downloaded=saved.downloaded.value_or(0); queue.uploaded=saved.uploaded.value_or(0);
    for (const auto& [key,target] : {std::pair{"discarded",&queue.discarded},
        {"hashfailbytes",&queue.hashFailed},{"secondsDownloading",&queue.secondsDownloading},
        {"secondsOnlySeeding",&queue.secondsSeeding}}) {
      if (const auto* value=row.find(key)) {
        require(value->type==core::Node::Type::Integer && value->integer>=0,
            "Invalid original queue statistics: "+std::string(key),ErrorCode::InvalidFormat);
        *target=value->integer;
      }
    }
    require(saved.downloaded.value_or(0)>=0&&saved.uploaded.value_or(0)>=0,"Negative saved cumulative transfer counters",ErrorCode::InvalidFormat);
    auto store=take(storage::TorrentStateStore::open(lease)); auto original=take(store.load(lease,hash));
    const auto source=authoritative(lease,original);
    const auto flags=core::originalDownloadFlags(source.find("attributes"));
    require(flags.has_value(),"Invalid original download flags",ErrorCode::InvalidFormat);
    if (const auto* attributes=source.find("attributes")) {
      if(const auto* idle=attributes->find("timesinceul");idle&&idle->type==core::Node::Type::Integer){
        queue.secondsSinceUpload=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(idle->integer));
      }
      if(const auto* aggregate=attributes->find("agsc");aggregate&&aggregate->type==core::Node::Type::Bytes&&aggregate->bytes.size()<=128){queue.aggregateScrapeCache=std::string(aggregate->bytes);}
      if(const auto* legacy=attributes->find("scrapecache");legacy&&legacy->type==core::Node::Type::Integer){queue.legacyScrapeCache=legacy->integer;}
      if (const auto* parameters=attributes->find("parameters")) {
        for (const auto& [key,target] : {std::pair{"sr.min",&queue.minimumShareRatio},{"sr.max",&queue.maximumShareRatio}}) {
          if (const auto* value=parameters->find(key)) {
            require(value->type==core::Node::Type::Integer && value->integer>=0
                && value->integer<=std::numeric_limits<std::int32_t>::max(),
                "Invalid original per-torrent share ratio",ErrorCode::InvalidFormat);
            *target=static_cast<std::int32_t>(value->integer);
          }
        }
      }
    }
    // Original getGoodTrackerScrapeResponses iterates stored sets in order;
    // equal-count ties must not acquire network-arrival ordering instead.
    try {
      std::vector<std::string> urls;bool valid=true,hasSets=false;
      if(const auto* groups=source.find("announce-list")){
        const auto tiers=core::decodeTorrentIntakeTrackerTiers(*groups);
        if(!tiers){valid=false;}else{hasSets=!tiers->empty();for(const auto& tier:*tiers){for(const auto& url:tier){urls.push_back(url);}}}
      }
      if(valid&&!hasSets){
        if(const auto* primary=source.find("announce")){
          if(primary->type!=core::Node::Type::Bytes||primary->bytes.size()>core::MaximumIntakeTrackerUrlBytes){valid=false;}
          else if(!primary->bytes.empty()){urls.emplace_back(primary->bytes);}
        }
      }
      if(valid&&urls.size()<=64){
        queue.trackerOrder.emplace();
        for(const auto& url:urls){const auto digest=lt::hasher256(lt::span<const char>(url.data(),url.size())).final();
          std::array<unsigned char,32> key{};std::copy(digest.begin(),digest.end(),key.begin());queue.trackerOrder->push_back(key);}
      }
    }catch(...){queue.trackerOrder.reset();} // Optional provider state cannot fail storage.
    // The shared original layout owns the encoding/UTF8-key policy. Keeping a
    // separate blanket UTF8 gate here would reject qualified Latin1 profiles.
    const auto geometry=storage::readOriginalLayout(lease,source);
    require(geometry.has_value(),"Invalid original managed geometry",ErrorCode::InvalidFormat); auto layout=*geometry;
    auto validated=metadata(source,hash,layout,swarmBridge);auto info=std::move(validated.info);auto identity=std::move(validated.identity);
    lt::add_torrent_params params; params.ti=info;
    if(*flags&core::OriginalSequentialDownloadFlag){params.flags|=lt::torrent_flags::sequential_download;}
    else{params.flags&=~lt::torrent_flags::sequential_download;}
    if(swarmBridge) {
      require(selectManagedSwarm(params,identity,swarmBridge),
          "Selected swarm requires the same exact counter/provider module",ErrorCode::Unsupported);
    } // Immutable admission flags are fixed before state ownership/payload effects.
    for(const auto& retained:retainedIdentities){const auto relation=take(storage::compareOriginalTorrentIdentities(identity,retained));require(relation==storage::OriginalIdentityRelation::Distinct,"Full torrent identity is retained by another admission",ErrorCode::Conflict);}
    auto attributes=dictionary(source.find("attributes"));
    saved=take(storage::resolveOriginalSaveRoot(rawSaved,layout));
    const auto storageDefaults=take(storage::resolveOriginalStorageTypes(lease,hash,layout,attributes.root()));
    require(!storageDefaults.unsupportedConversion,"Original policy requires unsupported cross-family storage conversion");
    done(storage::applyOriginalStorageDefaults(attributes,storageDefaults,true));
    auto parsedPolicy=compat::originalFilePolicy(row,layout.files.size()); require(parsedPolicy.has_value(),"Invalid original managed file priorities",ErrorCode::InvalidFormat);
    auto pending=journals(attributes.root(),layout.multifile,layout.files.size()); const fs::path save(saved.saveDirectory);
    require(save.is_absolute()&&save.lexically_normal()==save,"Managed save directory must be absolute and normalized",ErrorCode::UnsafePath);
    const auto profilePath=fs::read_symlink(fs::path("/proc/self/fd")/std::to_string(lease.directoryHandle())).lexically_normal();
    require(profilePath.is_absolute(),"Cannot authenticate managed profile path",ErrorCode::UnsafePath);
    std::optional<storage::VerifiedExistingScope> approvedScope;
    if(existingOnly){
      if(const auto* scope=row.find(storage::VerifiedExistingScopeKey)){approvedScope=take(storage::VerifiedExistingScope::decode(*scope));
        const auto paths=approvedScope->mapped()?existingMappedPaths(saved,layout,attributes.root(),save):std::vector<fs::path>{};
        done(approvedScope->validate(lease,source,layout,identity,save,saved.saveFile.value_or(""),paths));}
      require(!rawSaved.legacySavePath&&rawSaved.saveFile&&(*rawSaved.saveFile==layout.name||approvedScope.has_value()),
          "Existing-only verification requires the canonical original save leaf");
      // metadata() already authenticated the exact original/engine projection,
      // including its sole pure-v2 virtual trailing-pad exception. Only REAL
      // original slots need selected linear storage and an owned descriptor.
      for(std::size_t n=0;n<layout.files.size();++n){
        if(layout.files[n].padding){
          require((*parsedPolicy)[n].skipped&&(*parsedPolicy)[n].priority==0,"Existing-only padding must remain virtual and unselected");
        }else{
          require(!(*parsedPolicy)[n].skipped&&storageDefaults.requested[n]==storage::OriginalStorageKind::Linear,
              "Existing-only verification requires all real files selected in linear storage");
        }
      }
      require(pending.empty()&&storageDefaults.markers.empty(),
          "Existing-only verification cannot recover/convert original storage controls");
      existingOnlyAttributes(attributes.root(),layout.multifile,layout.files.size(),approvedScope&&approvedScope->mapped());
    }
    auto slots=plan(saved,attributes.root(),layout,*parsedPolicy,save,pending,profilePath,existingOnly);
    if(existingOnly){for(const auto& slot:slots){if(slot.padding){continue;}require(slot.kind==storage::OriginalStorageKind::Linear&&!slot.expanding&&(slot.actual==slot.base||(approvedScope&&approvedScope->mapped()))
          &&slot.data.get()>=0&&slot.identity.st_size==slot.bytes&&slot.identity.st_nlink==1,
          "Existing-only verification requires every complete original linear leaf",ErrorCode::Missing);}}
    const bool mapped=existingOnly||static_cast<std::size_t>(info->num_files())!=slots.size()||std::ranges::any_of(slots,[](const Slot& slot) {return !slot.padding&&(slot.kind!=storage::OriginalStorageKind::Linear||slot.actual.is_absolute()||slot.expanding);});
    // Validate every existing physical map before creating files or owning state.
    for (const auto& slot:slots) {
      if(slot.padding||slot.kind==storage::OriginalStorageKind::Linear||slot.data.get()<0){continue;}
      auto control=take(OriginalControl::open(lease,hash.hex(),slot.index,slot.kind,false));
      (void)take(storage::OriginalFileIo::open(slot.data.get(),slot.geometry,slot.kind,control?take(control->load()):std::nullopt,{},true,slot.expanding&&slot.kind==storage::OriginalStorageKind::Compact,static_cast<unsigned>(slot.identity.st_nlink)));
    }
    struct rlimit limit{}; std::size_t budget=8192;
    if (::getrlimit(RLIMIT_NOFILE,&limit)==0&&limit.rlim_cur!=RLIM_INFINITY) { budget=std::min(budget,static_cast<std::size_t>(limit.rlim_cur/(mapped?8:4))); }
    const auto files=static_cast<std::size_t>(std::ranges::count_if(slots,[](const auto& slot) { return !slot.padding; }));
    require(files<=budget,"Managed payload exceeds descriptor budget");
    params.save_path=save.string(); params.file_priorities=priorities(*parsedPolicy,layout);
    params.file_priorities.resize(static_cast<std::size_t>(info->num_files()),lt::download_priority_t{0});
    for (auto [key,target]:{std::pair{"maxdl",&params.download_limit},{"maxul",&params.upload_limit}}) {
      const auto value=integer(row.find(key)); require(value>=0&&value<=std::numeric_limits<int>::max(),"Unsupported disabled/overflowed per-torrent rate limit");
      *target=value?static_cast<int>(value):-1;
    }
    params.flags&=~lt::torrent_flags::auto_managed; params.flags|=lt::torrent_flags::paused;
    auto filtered=applyTorrentNetworkPolicy(policy,networks(source,attributes.root()),params);
    require(filtered.has_value(),"Managed torrent network selection is unsupported by the owner policy");
    auto claimPlan=payloadClaimPlan(slots,save,attributes.root(),pending);
    checkManagedClaims(claimPlan.values,claimed);
    // All unsupported geometry/mapping/policy decisions precede state ownership
    // and payload mutation. beginUpdate hydrates the same last cached sections.
    auto state=take(store.beginUpdate(lease,hash)); auto actual=dictionary(state.document().find("attributes"));
    require(core::originalDownloadFlags(state.document().find("attributes"))==flags,
        "Original sequential flags changed during preparation",ErrorCode::Conflict);
    const auto actualIdentity=take(storage::originalTorrentIdentityFromLibtorrent(state.document(),*info));done(storage::validateOriginalTorrentKey(actualIdentity,hash,true));
    require(take(storage::compareOriginalTorrentIdentities(identity,actualIdentity))==storage::OriginalIdentityRelation::SameSelected,"Authoritative original selected identity changed during preparation",ErrorCode::Conflict);
    const auto recheckedDefaults=take(storage::resolveOriginalStorageTypes(lease,hash,layout,actual.root()));
    require(recheckedDefaults==storageDefaults,"Original storage defaults/controls changed during preparation",ErrorCode::Conflict);
    const auto recheckedRoot=take(storage::resolveOriginalSaveRoot(rawSaved,layout));
    require(recheckedRoot.saveDirectory==saved.saveDirectory&&recheckedRoot.saveFile==saved.saveFile,
        "Original save-root interpretation changed during preparation",ErrorCode::Conflict);
    done(storage::applyOriginalStorageDefaults(actual,storageDefaults,true));
    if(existingOnly){existingOnlyAttributes(actual.root(),layout.multifile,layout.files.size(),approvedScope&&approvedScope->mapped());}
    {
      auto check=plan(saved,actual.root(),layout,*parsedPolicy,save,journals(actual.root(),layout.multifile,layout.files.size()),profilePath,existingOnly);
      require(check.size()==slots.size(),"Managed mapping changed during preparation",ErrorCode::Conflict);
      const auto refreshedClaims=payloadClaimPlan(check,save,actual.root(),journals(actual.root(),layout.multifile,layout.files.size()));
      checkManagedClaims(refreshedClaims.values,claimed);
      require(refreshedClaims.values.size()==claimPlan.values.size(),"Managed completion scope changed during preparation",ErrorCode::Conflict);
      for(std::size_t i=0;i<claimPlan.values.size();++i){require(refreshedClaims.values[i].absolutePath==claimPlan.values[i].absolutePath,"Managed claim path changed during preparation",ErrorCode::Conflict);}
      for (std::size_t index=0;index<slots.size();++index) {
        require(check[index].actual==slots[index].actual,"Authoritative managed mapping changed",ErrorCode::Conflict);
        require((check[index].identity.st_ino==0)==(slots[index].identity.st_ino==0)
          &&(!slots[index].identity.st_ino||(existingOnly?sameExistingSnapshot(check[index].identity,slots[index].identity):sameFile(check[index].identity,slots[index].identity))),"Managed payload changed during preparation",ErrorCode::Conflict);
      }
    }
    // Cache values are copied before their backing state is relinquished.
    auto rechecked=applyTorrentNetworkPolicy(policy,networks(state.document(),actual.root()),params);
    require(rechecked.has_value(),"Authoritative torrent network policy changed",ErrorCode::Conflict);
    // Before any payload write, Java must recheck after an interrupted owner.
    auto resume=dictionary(state.document().find("resume"));auto resumeData=dictionary(resume.find("data"));
    put(resumeData,"valid","i0e");put(resumeData,"resume data",stringValue(std::string(static_cast<std::size_t>(info->num_pieces()),'\2')));put(resumeData,"blocks","de");
    put(resume,"data",resumeData.serialize());done(state.replaceSection("resume",resume.serialize()));
    done(storage::applyOriginalStorageDefaults(actual,storageDefaults));
    put(actual,"resumecomplete","i1e");done(state.replaceSection("attributes",actual.serialize()));
    writeLinks(state,slots,save,false); done(store.save(lease,state));
    if(rawSaved.saveDirectory!=saved.saveDirectory||rawSaved.saveFile!=saved.saveFile||rawSaved.legacySavePath) {
      // Capture the original first-load interpretation before creating parents:
      // their later existence must not change a legacy path's meaning.
      const auto& oldRow=entryFor(inventory.document(),rawSaved,!inventory.document().find("downloads"));
      auto migrated=take(Document::parse(encoded(oldRow),storage::compatibleConfigLimits()).transform_error(
          [](const auto&){return Error{ErrorCode::InvalidFormat,"Cannot migrate original save root"};}));
      put(migrated,"save_dir",stringValue(saved.saveDirectory));put(migrated,"save_file",stringValue(*saved.saveFile));
      if(rawSaved.legacyKey){put(inventory.document(),*rawSaved.legacyKey,migrated.serialize());}
      else {const auto* rows=inventory.document().find("downloads");std::string list="l";
        for(std::size_t i=0;i<rows->children.size();++i){list+=i==rawSaved.sourceIndex?migrated.serialize():encoded(rows->children[i]);}
        put(inventory.document(),"downloads",list+'e');}
      done(inventory.save(lease));
    }
    std::vector<storage::VerifiedExistingScope::Proof> approvedProofs;
    std::vector<std::optional<OriginalDiskFile>> mappedFiles(static_cast<std::size_t>(info->num_files()));
    for (auto& slot:slots) {
      if (slot.padding) { continue; }
      const auto journal=std::find_if(pending.begin(),pending.end(),[&](const auto& value) { return value.index==slot.index; });
      const auto* intent=journal==pending.end()?nullptr:&*journal;
      if (!inspect(slot,save,intent,true,existingOnly)) {
        require(!existingOnly,"Existing-only payload disappeared; no creation is authorized",ErrorCode::Conflict);
        slot.parent=parentDirectory((save/slot.actual).parent_path(),true);
        slot.data=Descriptor(::openat(slot.parent.get(),slot.actual.filename().c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));
        require(slot.data.get()>=0&&::fstat(slot.data.get(),&slot.identity)==0&&S_ISREG(slot.identity.st_mode)
            &&slot.identity.st_uid==::geteuid()&&slot.identity.st_nlink==1,"Cannot exclusively create managed payload",ErrorCode::Conflict);
        sync(slot.data.get()); sync(slot.parent.get());
      }
      // LT mmap storage uses partfiles for missing/empty skipped files. Sparse
      // extension keeps skipped boundary-piece bytes in the original linear file.
      if (!mapped&&slot.skipped&&slot.bytes>slot.identity.st_size) {
        require(::ftruncate(slot.data.get(),slot.bytes)==0,"Cannot prepare skipped linear file",ErrorCode::Io); sync(slot.data.get());
      }
      if(mapped) {
        Descriptor mappedDescriptor(::fcntl(slot.data.get(),F_DUPFD_CLOEXEC,3));
        require(mappedDescriptor.get()>=0,"Cannot retain mapped payload descriptor",ErrorCode::Io);
        std::shared_ptr<OriginalControl> control;
        if(slot.kind!=storage::OriginalStorageKind::Linear||slot.expanding) {control=take(OriginalControl::open(lease,hash.hex(),slot.index,slot.expanding?storage::OriginalStorageKind::Compact:slot.kind,true));}
        auto io=take(storage::OriginalFileIo::open(mappedDescriptor.get(),slot.geometry,slot.kind,control?take(control->load()):std::nullopt,control?control->publisher():storage::OriginalControlPublisher{},true,slot.expanding&&slot.kind==storage::OriginalStorageKind::Compact,static_cast<unsigned>(slot.identity.st_nlink)));
        if(io.recoveryPending()){done(io.recover());}
        if((slot.kind==storage::OriginalStorageKind::Compact&&(!slot.skipped||storageDefaults.requested[slot.index]==storage::OriginalStorageKind::Linear))||slot.expanding) {
          require(slot.identity.st_dev<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())&&slot.identity.st_ino<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),"Compact conversion identity exceeds original journal range",ErrorCode::Unsupported);
          auto attrs=dictionary(state.document().find("attributes"));std::string conversions="l";
          // Existing other intents remain bound until their own iteration.
          if(const auto* old=attrs.find("BiglyBTC.compactConversion")) {for(const auto& row:old->children) {if(integer(row.find("index"),-1)!=static_cast<std::int64_t>(slot.index)){conversions+=encoded(row);}}}
          conversions+="d5:index"+intValue(static_cast<std::int64_t>(slot.index))+"6:device"+intValue(static_cast<std::int64_t>(slot.identity.st_dev))+"5:inode"+intValue(static_cast<std::int64_t>(slot.identity.st_ino))+"e";
          put(attrs,"BiglyBTC.compactConversion",conversions+'e');done(state.replaceSection("attributes",attrs.serialize()));done(store.save(lease,state));
          if(slot.kind==storage::OriginalStorageKind::Compact){done(io.convertToLinear());}
          slot.kind=storage::OriginalStorageKind::Linear;
          attrs=dictionary(state.document().find("attributes"));std::string stores="l";
          const auto* old=attrs.find("storetypes");for(std::size_t i=0;i<slots.size();++i){stores+=i==slot.index?"1:L":encoded(old->children[i]);}
          put(attrs,"storetypes",stores+'e');done(state.replaceSection("attributes",attrs.serialize()));done(store.save(lease,state));
          if(control){done(control->retire());}
          attrs=dictionary(state.document().find("attributes"));conversions="l";for(const auto& row:attrs.find("BiglyBTC.compactConversion")->children){if(integer(row.find("index"),-1)!=static_cast<std::int64_t>(slot.index)){conversions+=encoded(row);}}
          put(attrs,"BiglyBTC.compactConversion",conversions+'e');done(state.replaceSection("attributes",attrs.serialize()));done(store.save(lease,state));slot.expanding=false;
          io=take(storage::OriginalFileIo::open(mappedDescriptor.get(),slot.geometry,slot.kind,std::nullopt,{},false,false,static_cast<unsigned>(slot.identity.st_nlink)));
        } else if(slot.kind==storage::OriginalStorageKind::ReorderedCompact&&!slot.skipped) {
          done(io.setReorderedKind(storage::OriginalStorageKind::Reordered));slot.kind=storage::OriginalStorageKind::Reordered;
          auto attrs=dictionary(state.document().find("attributes"));std::string stores="l";const auto* old=attrs.find("storetypes");
          for(std::size_t i=0;i<slots.size();++i){stores+=i==slot.index?"1:R":encoded(old->children[i]);}put(attrs,"storetypes",stores+'e');done(state.replaceSection("attributes",attrs.serialize()));done(store.save(lease,state));
        }
        std::function<std::expected<void,Error>()> validate;
        if(existingOnly){
          if(approvedScope){validate=take(approvedScope->bind(slot.index,(save/slot.actual).lexically_normal(),mappedDescriptor.get()));approvedProofs.push_back(validate);}
          else{validate=existingPayloadProof(slot,save,mappedDescriptor.get());}
        }
        else if(control&&slot.kind!=storage::OriginalStorageKind::Linear){validate=[control]() -> std::expected<void,Error> {auto checked=control->load();if(!checked){return std::unexpected(checked.error());}return {};};}
        mappedFiles[slot.index].emplace(OriginalDiskFile{std::move(mappedDescriptor),std::move(io),std::move(validate)});
      }
      params.renamed_files[lt::file_index_t{static_cast<int>(slot.index)}]=slot.actual.string();
    }
    for(std::size_t i=0;i<claimPlan.values.size();++i){const auto& id=slots[claimPlan.indices[i]].identity;claimPlan.values[i].device=static_cast<std::uint64_t>(id.st_dev);claimPlan.values[i].inode=static_cast<std::uint64_t>(id.st_ino);}
    auto result=std::make_unique<Impl>(hash,std::move(identity),saved,std::move(store),std::move(state),std::move(layout),std::move(params),save,
      std::move(slots),std::move(pending),std::move(claimPlan.values),profile.st_dev,profile.st_ino);
    result->approvedProofs=std::move(approvedProofs);
    result->purpose=purpose; result->queue=queue; result->swarmBridge=std::move(swarmBridge); result->filePolicies=std::move(*parsedPolicy);
    // Capture after authoritative cache hydration, defaults and conversions;
    // later classification cannot authorize a newly relinked physical map.
    result->queueMappingScope=take(storage::originalQueueMappingScope(result->state.document()));
    if(mapped){result->mapped=std::make_shared<OriginalDiskFiles>(info,std::move(mappedFiles),existingOnly?OriginalDiskAccess::ReadOnly:OriginalDiskAccess::ReadWrite);}
    return ManagedStorage(std::move(result));
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io,"Managed storage preparation failed; prepared state may require recheck"}); }
}

std::expected<void,Error> ManagedStorage::checkpoint(const storage::ProfileLease& lease,std::span<const std::uint8_t> verified,
    bool complete,std::int64_t received,std::int64_t sent,
    std::int64_t secondsDownloading,std::int64_t secondsSeeding,std::optional<std::int32_t> secondsSinceUpload) {
  try {
    require(impl_!=nullptr,"Managed storage was moved or released",ErrorCode::Conflict); auto& owner=*impl_;
    require(!owner.attempted,"Managed admission checkpoint already attempted",ErrorCode::Conflict);
    if(owner.mapped){if(const auto error=owner.mapped->failure()){throw Failure{*error};}done(owner.mapped->flush());}
    struct stat profile{}; require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0&&profile.st_dev==owner.profileDevice&&profile.st_ino==owner.profileInode,
      "Managed checkpoint requires its original held lease",ErrorCode::UnsafePath);
    require(verified.size()==static_cast<std::size_t>(owner.params.ti->num_pieces())&&std::ranges::all_of(verified,[](auto value) { return value<=1; })
      &&(!complete||std::ranges::all_of(verified,[](auto value) { return value==1; })),"Invalid actual verified-piece checkpoint",ErrorCode::InvalidFormat);
    const auto previousReceived=owner.captured.downloaded.value_or(0),previousSent=owner.captured.uploaded.value_or(0);
    require(received>=0&&sent>=0&&received<=std::numeric_limits<std::int64_t>::max()-previousReceived&&sent<=std::numeric_limits<std::int64_t>::max()-previousSent,
      "Managed admission cumulative counters overflow",ErrorCode::InvalidFormat);
    auto inventory=take(storage::ConfigFile::load(lease,"downloads.config"));
    require(inventory.recoverySource()!=storage::RecoverySource::Saving,"Resolve saved-inventory recovery before checkpoint",ErrorCode::Conflict);
    const auto current=selected(inventory.document(),owner.hash);
    require(current.torrentFile==owner.captured.torrentFile&&current.saveDirectory==owner.captured.saveDirectory
      &&current.saveFile==owner.captured.saveFile&&current.legacySavePath==owner.captured.legacySavePath,"Managed saved mapping changed during admission",ErrorCode::Conflict);
    require(current.downloaded.value_or(0)==previousReceived&&current.uploaded.value_or(0)==previousSent,"Saved cumulative counters changed during managed admission",ErrorCode::Conflict);
    require(secondsDownloading>=0&&secondsSeeding>=0
        &&secondsDownloading<=std::numeric_limits<std::int64_t>::max()-owner.queue.secondsDownloading
        &&secondsSeeding<=std::numeric_limits<std::int64_t>::max()-owner.queue.secondsSeeding,
        "Managed queue elapsed times overflow",ErrorCode::InvalidFormat);
    auto entry=dictionary(&entryFor(inventory.document(),current,!inventory.document().find("downloads")));
    for (const auto& [key,expected] : {std::pair{"secondsDownloading",owner.queue.secondsDownloading},
        {"secondsOnlySeeding",owner.queue.secondsSeeding}}) {
      if (const auto* value=entry.find(key)) {
        require(value->type==core::Node::Type::Integer&&value->integer==expected,
            "Original queue elapsed times changed during admission",ErrorCode::Conflict);
      } else { require(expected==0,"Original queue elapsed time disappeared",ErrorCode::Conflict); }
    }
    std::vector<std::int64_t> counts(owner.layout.files.size()),verifiedEnds(owner.layout.files.size()); std::int64_t total=0;
    for (std::size_t index=0;index<owner.layout.files.size();++index) {
      const auto& file=owner.layout.files[index];
      if (file.bytes) {
        const auto first=file.offset/owner.layout.pieceLength,last=(file.offset+file.bytes-1)/owner.layout.pieceLength;
        for (auto piece=first;piece<=last;++piece) {
          if (!verified[static_cast<std::size_t>(piece)]) { continue; }
          const auto start=piece*owner.layout.pieceLength;
          const auto end=start+std::min(owner.layout.pieceLength,owner.layout.bytes-start);
          const auto overlapEnd=std::min(file.offset+file.bytes,end);
          counts[index]+=overlapEnd-std::max(file.offset,start);
          verifiedEnds[index]=std::max(verifiedEnds[index],overlapEnd-file.offset);
        }
      }
      require(counts[index]>=0&&counts[index]<=file.bytes&&counts[index]<=std::numeric_limits<std::int64_t>::max()-total,"Verified file accounting overflows",ErrorCode::InvalidFormat); total+=counts[index];
    }
    if(owner.mapped){std::vector<std::uint8_t> completeFiles(static_cast<std::size_t>(owner.params.ti->num_files()));for(const auto& slot:owner.slots){completeFiles[slot.index]=!slot.padding&&counts[slot.index]==slot.bytes;}done(owner.mapped->finishComplete(completeFiles));}
    bool allocated=true;
    for (auto& slot:owner.slots) {
      if (slot.padding) { continue; } struct stat named{},opened{},parent{};
      auto actualParent=parentDirectory((owner.save/slot.actual).parent_path(),false);
      require(actualParent.get()>=0&&::fstat(actualParent.get(),&parent)==0,"Managed parent path disappeared",ErrorCode::Conflict);
      struct stat heldParent{}; require(::fstat(slot.parent.get(),&heldParent)==0&&heldParent.st_dev==parent.st_dev&&heldParent.st_ino==parent.st_ino,"Managed parent identity changed",ErrorCode::Conflict);
      require(::fstat(slot.data.get(),&opened)==0&&::fstatat(slot.parent.get(),slot.actual.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
        &&sameFile(slot.identity,opened)&&sameFile(opened,named)&&opened.st_uid==::geteuid()&&opened.st_size>=0&&static_cast<std::uint64_t>(opened.st_size)<=take(storage::originalMaximumPhysicalSize(slot.geometry,slot.kind))
        &&(owner.mapped||!complete||opened.st_size==slot.bytes)
        &&(owner.purpose!=ManagedStoragePurpose::VerifyExistingReadOnly||(sameExistingSnapshot(slot.identity,opened)&&sameExistingSnapshot(slot.identity,named))),"Managed payload identity/size changed",ErrorCode::Conflict);
      require(owner.mapped||verifiedEnds[slot.index]<=opened.st_size,"Verified checkpoint exceeds actual file extent",ErrorCode::Conflict);
      allocated=allocated&&opened.st_size==slot.bytes; sync(slot.data.get());
    }
    put(entry,"downloaded",intValue(previousReceived+received)); put(entry,"uploaded",intValue(previousSent+sent)); put(entry,"completedbytes",intValue(total));
    if (secondsDownloading) { put(entry,"secondsDownloading",intValue(owner.queue.secondsDownloading+secondsDownloading)); }
    if (secondsSeeding) { put(entry,"secondsOnlySeeding",intValue(owner.queue.secondsSeeding+secondsSeeding)); }
    put(entry,"allocated",allocated?"i1e":"i0e");
    // The fresh root retains all current user command fields, policy and pauses.
    if (current.legacyKey) { put(inventory.document(),*current.legacyKey,entry.serialize()); }
    else {
      const auto* entries=inventory.document().find("downloads"); std::string rows="l";
      for (std::size_t index=0;index<entries->children.size();++index) { rows+=index==current.sourceIndex?entry.serialize():encoded(entries->children[index]); }
      put(inventory.document(),"downloads",rows+'e');
    }
    owner.attempted=true;
    if(secondsSinceUpload){
      auto attributes=dictionary(owner.state.document().find("attributes"));
      put(attributes,"timesinceul",intValue(*secondsSinceUpload));
      put(owner.state.document(),"attributes",attributes.serialize());
    }
    writeProgress(owner.state,counts,verified.size(),complete); done(owner.store.save(lease,owner.state));
    const bool completedFiles=!owner.pending.empty()||std::ranges::any_of(owner.slots,[&](const auto& slot){return !slot.padding&&counts[slot.index]==slot.bytes;});
    if (completedFiles) {
      auto attributes=dictionary(owner.state.document().find("attributes")); const auto suffix=attributes.find("incompfilesuffix")?text(attributes.find("incompfilesuffix")):std::string{};
      const auto prefix=attributes.find("dnd_pfx")?text(attributes.find("dnd_pfx")):std::string{};
      std::set<std::size_t> known; for (const auto& item:owner.pending) { known.insert(item.index); }
      for (const auto& slot:owner.slots) {
        if (slot.padding||counts[slot.index]!=slot.bytes||known.contains(slot.index)) { continue; } const auto name=slot.actual.filename().string();
        if (!suffix.empty()&&name.size()>suffix.size()&&name.ends_with(suffix)) {
          auto newName=name.substr(0,name.size()-suffix.size());
          if(!slot.skipped&&!prefix.empty()&&newName.starts_with(prefix)){newName.erase(0,prefix.size());}
          auto final=slot.actual; final.replace_filename(newName);
          require(!newName.empty()&&safeManagedPhysical(final)&&final.filename()!=".git"&&!newName.starts_with(".BiglyBTC"),"Unsafe completed filename",ErrorCode::UnsafePath);
          require(slot.identity.st_dev<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())&&slot.identity.st_ino<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),"Payload identity exceeds completion journal range");
          owner.pending.push_back({slot.index,slot.actual,final,slot.identity.st_dev,slot.identity.st_ino});
        }
      }
      if (!owner.pending.empty()) { saveJournal(owner.state,owner.store,lease,owner.layout.multifile,owner.pending); }
      for (auto& item:owner.pending) {
        auto& slot=owner.slots[item.index];
        if (slot.actual==item.target) { item.linked=true; }
        else if (::linkat(slot.parent.get(),slot.actual.filename().c_str(),slot.parent.get(),item.target.filename().c_str(),0)==0) { item.linked=true; sync(slot.parent.get()); }
        else if (errno==EEXIST) { struct stat other{}; item.linked=::fstatat(slot.parent.get(),item.target.filename().c_str(),&other,AT_SYMLINK_NOFOLLOW)==0&&sameFile(slot.identity,other); }
        else { throw Failure{{ErrorCode::Io,"Managed completion linking failed; source names and journal retained"}}; }
        if (item.linked) { slot.final=item.target; }
      }
      writeLinks(owner.state,owner.slots,owner.save,true); done(owner.store.save(lease,owner.state));
    }
    done(inventory.save(lease));
    if (completedFiles) {
      for (const auto& item:owner.pending) {
        if (!item.linked) { continue; } const auto& slot=owner.slots[item.index]; struct stat old{};
        if (::fstatat(slot.parent.get(),item.source.filename().c_str(),&old,AT_SYMLINK_NOFOLLOW)==0) {
          require(S_ISREG(old.st_mode)&&old.st_dev==item.device&&old.st_ino==item.inode,"Managed completion source alias changed; retained",ErrorCode::Conflict);
          require(::unlinkat(slot.parent.get(),item.source.filename().c_str(),0)==0,"Managed completion alias removal failed",ErrorCode::Io); sync(slot.parent.get());
        } else { require(errno==ENOENT,"Managed completion alias inspection failed",ErrorCode::Io); }
      }
      if (!owner.pending.empty()) { saveJournal(owner.state,owner.store,lease,owner.layout.multifile,{}); }
    }
    return {};
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io,"Managed checkpoint failed; original state or inventory may already have changed"}); }
}
} // namespace biglybtc::engine::detail
