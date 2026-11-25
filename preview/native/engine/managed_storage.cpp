#include "runtime_console.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/original_file_links.hpp"
#include "managed_storage.hpp"
#include "managed_claim_index.hpp"
#include "managed_checkpoint_inventory.hpp"
#include "mixed_i2p_source.hpp"
#include "owned_i2p_policy.hpp"
#include "allocation_intent.hpp"
#include "verified_existing_scope.hpp"
#include "selected_existing_scope.hpp"
#include "verified_seeding.hpp"
#include "queue_positions.hpp"
#include "managed_payload_counters.hpp"
#include "linear_to_compact.hpp"
#include "original_layout_projection.hpp"
#include "original_torrent_identity_lt.hpp"
#include "original_disk_io.hpp"
#include "original_control.hpp"
#include "original_storage_defaults.hpp"
#include "original_storage_conversion.hpp"
#include "original_filename_policy.hpp"
#include "local_support.hpp"
#include "network_policy.hpp"
#include "torrent_state.hpp"
#include "encoded_document.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/download_state_tags.hpp"
#include "biglybtc/compact_file_requirement.hpp"
#include "biglybtc/saved_dnd.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/hasher.hpp>
#include "biglybtc/torrent_intake_trackers.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <initializer_list>
#include <bit>
#include <charconv>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace biglybtc::engine::detail {
namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
using storage::OriginalPayloadHandle;
using storage::detail::OriginalPayloadDescriptor;
using storage::detail::StateDescriptor;
using storage::detail::StateHandle;
using storage::detail::StateStamp;
using storage::detail::FilesystemPathLess;
using storage::detail::InvalidStateHandle;
using storage::detail::stateStamp;
using storage::detail::stateStampAt;
using storage::detail::sameStateIdentity;
using storage::detail::sameStateTimestamp;
using storage::detail::openDirectoryAt;
using storage::detail::openFilesystemRoot;
using storage::detail::filesystemRoot;
using storage::detail::syncStateFile;
using storage::detail::syncStateDirectory;

bool held(StateHandle handle) { return handle != InvalidStateHandle; }

using platform::pathFromUtf8;
using platform::pathToUtf8;

fs::path relativeComponents(const fs::path& path, const fs::path& root) {
  require(!root.empty(), "Unsafe payload filesystem root", ErrorCode::UnsafePath);
  if (path == root) { return {}; }
  if (root == path.root_path()) { return path.relative_path(); }
  auto components = path.lexically_relative(root);
  require(!components.empty(), "Payload path is outside its filesystem root", ErrorCode::UnsafePath);
  return components;
}

OriginalPayloadDescriptor duplicatePayload(OriginalPayloadHandle payload) {
  auto copy = take(storage::detail::duplicateStateDirectory(payload));
  return OriginalPayloadDescriptor(copy.release());
}

bool pathWithin(const fs::path& child,const fs::path& parent) {
  auto a=child.begin();
  for(auto b=parent.begin();b!=parent.end();++a,++b){
    if(a==child.end()||!storage::detail::sameFilesystemComponent(*a,*b)){return false;}
  }
  return true;
}

bool reservedComponent(const fs::path& part) {
  static const fs::path Git(".git"), BiglyBtc(".BiglyBTC");
  return storage::detail::sameFilesystemComponent(part,Git)
      || storage::detail::sameFilesystemComponent(fs::path(part.native().substr(0,BiglyBtc.native().size())),BiglyBtc);
}

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
  bool linearBackupCandidate=false,linearBackupCleanup=false;
  bool allocationCreated=false;
  StateDescriptor parent{};
  OriginalPayloadDescriptor data{};
  StateStamp identity{};
};
std::string intValue(std::int64_t value);
struct AllocationFile {
  std::size_t index=0;fs::path path;std::int64_t bytes=0;
  bool created=false,removed=false,authenticated=false;std::uint64_t device=0,inode=0,parentDevice=0,parentInode=0;
  std::string originalStamp,activeStamp;
  std::int64_t prefix=0,scanned=0,written=0,verified=0;
};
struct AllocationWork {
  storage::AllocationIntent intent;
  int stage=1;std::string info;
  std::vector<AllocationFile> files;
  std::size_t cursor=0;std::int64_t completed=0,total=0;
  bool attempted=false,started=false,integrityLost=false;
  std::map<std::string,std::pair<std::uint64_t,std::uint64_t>,std::less<>> directories;
};
constexpr std::string_view AllocationWorkKey="BiglyBTC.allocationWork";
std::int64_t allocationNumber(std::uint64_t value){require(value<=static_cast<std::uint64_t>(INT64_MAX),"Allocation identity exceeds bounded journal",ErrorCode::Unsupported);return static_cast<std::int64_t>(value);}
std::string allocationStamp(const StateStamp& value){
  return "l"+intValue(allocationNumber(value.device))+intValue(allocationNumber(value.inode))+intValue(value.size)
      +intValue(value.mode)+intValue(value.owner)+intValue(value.links)+intValue(value.modified.tv_sec)+intValue(value.modified.tv_nsec)
      +intValue(value.changed.tv_sec)+intValue(value.changed.tv_nsec)+'e';
}
void allocationKeys(const Node& value,std::initializer_list<std::string_view> allowed){
  require(value.type==Node::Type::Dictionary&&value.children.size()==allowed.size()*2,"Invalid bounded allocation journal shape",ErrorCode::InvalidFormat);
  std::set<std::string_view> seen;for(std::size_t n=0;n<value.children.size();n+=2){const auto& key=value.children[n];require(key.type==Node::Type::Bytes&&std::ranges::find(allowed,key.bytes)!=allowed.end()&&seen.insert(key.bytes).second,"Unknown/duplicate allocation journal key",ErrorCode::InvalidFormat);}
}
std::string allocationInfo(const core::Document& source){const auto info=take(storage::originalTorrentInfoSubject(source));require(info.size()<=static_cast<std::size_t>(INT_MAX),"Allocation info bound",ErrorCode::Unsupported);return lt::hasher256(info.data(),static_cast<int>(info.size())).final().to_string();}
using AllocationDirectories=std::map<std::string,std::pair<std::uint64_t,std::uint64_t>,std::less<>>;
AllocationDirectories allocationChain(const fs::path& parent,bool missing){
  require(parent.is_absolute()&&parent.lexically_normal()==parent&&parent.native().size()<=4096,"Unsafe allocation ancestor path",ErrorCode::UnsafePath);
  auto path=filesystemRoot(parent);
  auto directory=take(openFilesystemRoot(path));
  const auto root=take(stateStamp(directory.get()));
  AllocationDirectories result;
  result.emplace(pathToUtf8(path),std::pair{root.device,root.inode});
  for(const auto& part:relativeComponents(parent,path)){
    require(!part.empty()&&part!="."&&part!=".."&&part.native().size()<=255,"Unsafe allocation ancestor component",ErrorCode::UnsafePath);path/=part;
    auto next=openDirectoryAt(directory.get(),part);
    if(!next&&next.error().code==ErrorCode::Missing&&missing){break;}
    require(next.has_value(),"Allocation ancestor changed or is unsafe",ErrorCode::Conflict);
    const auto value=take(stateStamp(next->get()));
    require(value.directory,"Allocation ancestor is unsafe",ErrorCode::Conflict);
    result.emplace(pathToUtf8(path),std::pair{value.device,value.inode});
    directory=std::move(*next);
  }
  return result;
}
void checkAllocationChain(const AllocationDirectories& expected,const AllocationDirectories& current){
  for(const auto& [path,id]:current){const auto old=expected.find(path);require(old!=expected.end()&&old->second==id,"Allocation ancestor identity changed",ErrorCode::Conflict);}
}
std::unique_ptr<AllocationWork> readAllocationWork(const storage::AllocationIntent& intent,const Node& attributes,const core::Document& source,std::span<const Slot> slots,const fs::path& save){
  auto work=std::make_unique<AllocationWork>();work->intent=intent;work->info=allocationInfo(source);const auto* journal=attributes.find(AllocationWorkKey);const Node* records=nullptr;
  if(journal){allocationKeys(*journal,{"version","mode","stage","selected","info","files","directories"});
    require(integer(journal->find("version"),-1)==1&&integer(journal->find("mode"),-1)==static_cast<int>(intent.mode)&&text(journal->find("selected"))==intent.hash.hex()&&text(journal->find("info"))==work->info,"Allocation journal identity changed",ErrorCode::Conflict);
    const auto stage=integer(journal->find("stage"),-1);require(stage>=1&&stage<=3,"Unknown allocation journal stage",ErrorCode::InvalidFormat);work->stage=static_cast<int>(stage);records=journal->find("files");
    require(records&&records->type==Node::Type::List&&records->children.size()<=4096,"Invalid allocation file journal",ErrorCode::InvalidFormat);
    const auto* dirs=journal->find("directories");require(dirs&&dirs->type==Node::Type::List&&!dirs->children.empty()&&dirs->children.size()<=8192,"Invalid allocation ancestor journal",ErrorCode::InvalidFormat);
    for(const auto& row:dirs->children){require(row.type==Node::Type::List&&row.children.size()==3&&row.children[0].type==Node::Type::Bytes,"Invalid allocation directory record",ErrorCode::InvalidFormat);
      const auto path=pathFromUtf8(text(&row.children[0]));const auto dev=integer(&row.children[1],-1),ino=integer(&row.children[2],-1);
      require(path.is_absolute()&&path.lexically_normal()==path&&path.native().size()<=4096&&dev>=0&&ino>0&&work->directories.emplace(pathToUtf8(path),std::pair{static_cast<std::uint64_t>(dev),static_cast<std::uint64_t>(ino)}).second,"Invalid/duplicate allocation ancestor identity",ErrorCode::InvalidFormat);
    }
  }
  std::set<std::string> desiredDirectories;std::size_t number=0;for(const auto& slot:slots){if(slot.padding){continue;}AllocationFile file;file.index=slot.index;file.path=(save/slot.actual).lexically_normal();file.bytes=slot.bytes;
    require(file.bytes>=0&&file.bytes<=INT64_MAX-work->total,"Allocation total overflows",ErrorCode::InvalidFormat);work->total+=file.bytes;
    for(auto path=file.path.parent_path();!path.empty();path=path.parent_path()){desiredDirectories.insert(pathToUtf8(path));require(desiredDirectories.size()<=8192,"Allocation ancestor count bound",ErrorCode::Unsupported);if(path==filesystemRoot(file.path)){break;}}
    const auto chain=allocationChain(file.path.parent_path(),!records);if(records){checkAllocationChain(work->directories,chain);}else{work->directories.insert(chain.begin(),chain.end());}
    if(records){require(number<records->children.size(),"Missing original allocation index",ErrorCode::InvalidFormat);const auto& record=records->children[number++];
      allocationKeys(record,{"index","path","bytes","created","removed","device","inode","parentDevice","parentInode","stamp"});
      require(integer(record.find("index"),-1)==static_cast<std::int64_t>(file.index)&&text(record.find("path"))==pathToUtf8(file.path)&&integer(record.find("bytes"),-1)==file.bytes,"Allocation journal mapping changed",ErrorCode::Conflict);
      const auto created=integer(record.find("created"),-1),removed=integer(record.find("removed"),-1);require((created==0||created==1)&&(removed==0||removed==1)&&(!removed||created==1),"Invalid allocation ownership flags",ErrorCode::InvalidFormat);file.created=created==1;file.removed=removed==1;
      const auto takeId=[&](std::string_view key){const auto value=integer(record.find(key),-1);require(value>=0,"Invalid allocation inode identity",ErrorCode::InvalidFormat);return static_cast<std::uint64_t>(value);};
      file.device=takeId("device");file.inode=takeId("inode");file.parentDevice=takeId("parentDevice");file.parentInode=takeId("parentInode");file.originalStamp=text(record.find("stamp"));require(file.originalStamp.size()<=1024,"Allocation stamp bound",ErrorCode::InvalidFormat);
      const auto parent=stateStamp(slot.parent.get());require(parent&&file.parentDevice==parent->device&&file.parentInode==parent->inode,"Allocation journal parent changed",ErrorCode::Conflict);
      if(file.removed){require(work->stage==3&&!slot.identity.inode,"Cancelled allocation target reappeared",ErrorCode::Conflict);}
      else{require(slot.identity.inode&&file.device==slot.identity.device&&file.inode==slot.identity.inode&&slot.identity.links==1
          &&(file.created||file.originalStamp==allocationStamp(slot.identity)),"Allocation journal target changed",ErrorCode::Conflict);}
    }else{
      require(!slot.identity.inode||slot.identity.size==file.bytes,"Existing short data cannot be zeroed without review",ErrorCode::Unsupported);file.created=!slot.identity.inode;
      if(slot.identity.inode){const auto parent=stateStamp(slot.parent.get());require(parent&&slot.identity.links==1,"Allocation full-file parent",ErrorCode::Conflict);file.device=slot.identity.device;file.inode=slot.identity.inode;file.parentDevice=parent->device;file.parentInode=parent->inode;file.originalStamp=allocationStamp(slot.identity);}
    }
    work->files.push_back(std::move(file));
  }
  require(!records||(number==records->children.size()&&desiredDirectories.size()==work->directories.size()&&std::ranges::all_of(work->directories,[&](const auto& value){return desiredDirectories.contains(value.first);})),"Extra/missing allocation file or ancestor indices",ErrorCode::InvalidFormat);
  std::size_t encodedBound=512;
  for(const auto& file:work->files){const auto amount=pathToUtf8(file.path).size()+std::max<std::size_t>(file.originalStamp.size(),256)+512;require(amount<=4*1024*1024-encodedBound,"Allocation journal bound before file creation",ErrorCode::Unsupported);encodedBound+=amount;}
  for(const auto& path:desiredDirectories){const auto amount=path.size()+128;require(amount<=4*1024*1024-encodedBound,"Allocation ancestor byte bound before file creation",ErrorCode::Unsupported);encodedBound+=amount;}
  for(const auto& [path,id]:work->directories){(void)path;(void)allocationNumber(id.first);(void)allocationNumber(id.second);}return work;
}
std::string encodeAllocationWork(const AllocationWork& work){
  std::string records="l";for(const auto& file:work.files){
    records+="d5:index"+intValue(static_cast<std::int64_t>(file.index))+"4:path"+stringValue(pathToUtf8(file.path))+"5:bytes"+intValue(file.bytes)+"7:created"+(file.created?"i1e":"i0e")+"7:removed"+(file.removed?"i1e":"i0e")
        +"6:device"+intValue(allocationNumber(file.device))+"5:inode"+intValue(allocationNumber(file.inode))+"12:parentDevice"+intValue(allocationNumber(file.parentDevice))+"11:parentInode"+intValue(allocationNumber(file.parentInode))+"5:stamp"+stringValue(file.originalStamp)+'e';
    require(records.size()<=4*1024*1024,"Allocation journal byte bound",ErrorCode::Unsupported);
  }
  std::string dirs="l";for(const auto& [path,id]:work.directories){dirs+='l'+stringValue(path)+intValue(allocationNumber(id.first))+intValue(allocationNumber(id.second))+'e';}dirs+='e';
  auto result="d7:versioni1e4:mode"+intValue(static_cast<int>(work.intent.mode))+"5:stage"+intValue(work.stage)+"8:selected"+stringValue(work.intent.hash.hex())+"4:info"+stringValue(work.info)+"5:files"+records+"e11:directories"+dirs+'e';require(result.size()<=4*1024*1024,"Allocation full journal byte bound",ErrorCode::Unsupported);return result;
}
bool safeManagedRelative(const fs::path& path) {
  if (path.empty() || path.has_root_path() || path.lexically_normal() != path || path.native().size()>4096
      || path.native().find('\n')!=path.native().npos || path.native().find('\0')!=path.native().npos) { return false; }
  for (const auto& part : path) { if (part.empty() || part=="." || part==".." || part.native().size()>255) { return false; } }
  return true;
}
bool safeManagedPhysical(const fs::path& path) {
  return safeManagedRelative(path)||(path.is_absolute()&&path.lexically_normal()==path
      &&!filesystemRoot(path).empty()&&safeManagedRelative(path.relative_path()));
}
bool sameFile(const StateStamp& a, const StateStamp& b) {
  return a.regular&&b.regular&&sameStateIdentity(a,b);
}
std::string intValue(std::int64_t value) { return "i"+std::to_string(value)+"e"; }
const Node& entryFor(const Document& inventory, const core::SavedDownload& item, bool legacy) {
  return legacy ? inventory.root().children.at(item.sourceIndex*2+1) : inventory.find("downloads")->children.at(item.sourceIndex);
}
core::LibrarySnapshot checkedInventory(const Document& inventory) {
  auto parsed = core::readLibrary(inventory);
  require(parsed.has_value(),"Invalid managed saved inventory",ErrorCode::InvalidFormat);
  for (const auto& issue : parsed->issues) {
    if (issue.pauseEntry || !parsed->legacyMap) { throw Failure{{ErrorCode::InvalidFormat,"Malformed managed inventory"}}; }
    const auto& key = inventory.root().children.at(issue.sourceIndex*2);
    const auto& value = inventory.root().children.at(issue.sourceIndex*2+1);
    require(key.bytes=="pause_data" || (!value.find("torrent")&&!value.find("torrent_hash")&&!value.find("save_dir")&&!value.find("path")),
        "Malformed legacy managed inventory",ErrorCode::InvalidFormat);
  }
  return std::move(*parsed);
}
std::map<std::string,core::SavedDownload> inventoryIndex(const Document& inventory){
  auto parsed=checkedInventory(inventory);std::map<std::string,core::SavedDownload> result;
  for(auto& item:parsed.downloads){if(item.hash){
    const auto key=item.hash->hex();
    require(result.emplace(key,std::move(item)).second,"Duplicate managed saved identity",ErrorCode::Conflict);
  }}return result;
}
core::SavedDownload selected(const Document& inventory,const core::TorrentHash& hash){
  auto parsed=checkedInventory(inventory);const core::SavedDownload* found=nullptr;
  std::set<std::string> hashes;
  for(const auto& item:parsed.downloads){if(item.hash){
    require(hashes.insert(item.hash->hex()).second,"Duplicate managed saved identity",ErrorCode::Conflict);
    if(*item.hash==hash){found=&item;}
  }}
  require(found!=nullptr,"Managed target is stale or unavailable",ErrorCode::Conflict);
  return *found;
}
Document authoritative(const storage::ProfileLease& lease, const storage::TorrentStateFile& state) {
  auto result = state.document();
  auto active=take(storage::detail::openStateDirectory(lease.directoryHandle(),"active"));
  const auto raw=take(storage::detail::readStateFile(active.get(),"cache.dat",storage::compatibleConfigLimits().inputBytes));
  if (!raw) { return result; }
  auto cache = lease.decodeMetadataSnapshot("active/cache.dat",*raw,storage::compatibleConfigLimits());
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
    Journal entry{index,pathFromUtf8(text(item.find("source"))),pathFromUtf8(text(item.find("target"))),
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
    put(record,"source",stringValue(pathToUtf8(item.source))); put(record,"target",stringValue(pathToUtf8(item.target)));
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
StateDescriptor parentDirectory(const fs::path& path, bool create) {
  require(path.is_absolute()&&path.lexically_normal()==path&&path.native().find('\n')==path.native().npos,
      "Unsafe managed payload directory",ErrorCode::UnsafePath);
  const auto root=filesystemRoot(path);
  auto current=take(openFilesystemRoot(root));
  for (const auto& part:relativeComponents(path,root)) {
    require(part!="."&&part!=".."&&!part.empty()&&part.native().size()<=255,
        "Unsafe managed directory component",ErrorCode::UnsafePath);
    auto next=openDirectoryAt(current.get(),part);
    if (!next&&next.error().code==ErrorCode::Missing) {
      const auto parent=stateStamp(current.get());
      require(parent&&parent->owned,"Missing payload directory requires an owned parent",ErrorCode::UnsafePath);
      if (!create) { return {}; }
      next=storage::detail::createDirectoryAt(current.get(),part);
      if (!next&&next.error().code==ErrorCode::Conflict) { next=openDirectoryAt(current.get(),part); }
      require(next.has_value(),"Cannot create managed payload directory",ErrorCode::Io);
      done(syncStateDirectory(current.get()));
      const auto made=stateStamp(next->get());
      require(made&&made->owned,"New managed directory ownership changed",ErrorCode::UnsafePath);
    }
    require(next.has_value(),"Managed payload directory is inaccessible or symbolic",ErrorCode::UnsafePath);
    current=std::move(*next);
  }
  return current;
}
bool sameExistingSnapshot(const StateStamp& a,const StateStamp& b) {
  return sameFile(a,b)&&a.mode==b.mode&&a.owner==b.owner&&a.owned==b.owned&&a.links==b.links&&a.size==b.size
      &&sameStateTimestamp(a.modified,b.modified)&&sameStateTimestamp(a.changed,b.changed);
}
bool inspect(Slot& slot, const fs::path& save, const Journal* journal, bool open,bool readOnly=false) {
  slot.parent=parentDirectory((save/slot.actual).parent_path(),false);
  if (!held(slot.parent.get())) { return false; }
  const auto entry=stateStampAt(slot.parent.get(),slot.actual.filename());
  require(entry.has_value(),"Cannot inspect managed payload",ErrorCode::UnsafePath);
  if (!*entry) {
    const auto parent=stateStamp(slot.parent.get());
    require(parent&&parent->owned,"Missing managed file requires an owned parent",ErrorCode::UnsafePath);
    return false;
  }
  const auto& named=**entry;
  // This normal-owner candidate is still untrusted until its retained control
  // proves backup-only under CAS. A full L extent cannot be a bounded packed C
  // extent. Existing exact C replay intents keep their original codec instead.
  if(slot.linearBackupCandidate&&slot.kind==storage::OriginalStorageKind::Compact
      &&named.size==slot.bytes&&slot.geometry.fileBytes>take(storage::originalMaximumPhysicalSize(slot.geometry,storage::OriginalStorageKind::Compact))){
    slot.kind=storage::OriginalStorageKind::Linear;slot.linearBackupCleanup=!slot.expanding;slot.expanding=true;
  }
  require(named.regular&&named.owned&&named.size>=0
      &&static_cast<std::uint64_t>(named.size)<=take(storage::originalMaximumPhysicalSize(slot.geometry,slot.expanding?storage::OriginalStorageKind::Linear:slot.kind))
      &&named.links>=1&&named.links<=2,"Unsafe managed payload type, ownership, size or links",ErrorCode::UnsafePath);
  if (slot.identity.inode) { require(readOnly?sameExistingSnapshot(slot.identity,named):sameFile(slot.identity,named),"Managed payload identity changed after preflight",ErrorCode::Conflict); }
  if (journal) {
    require((slot.actual==journal->source||slot.actual==journal->target)&&named.device==journal->device&&named.inode==journal->inode,"Completion journal differs from payload",ErrorCode::Conflict);
  }
  if (named.links==2) {
    require(journal!=nullptr,"Unrecognized managed payload hard link",ErrorCode::UnsafePath);
    const auto alias=slot.actual==journal->source?journal->target:journal->source;
    const auto other=stateStampAt(slot.parent.get(),alias.filename());
    require(other&&*other&&sameFile(named,**other),"Unrecognized completion alias",ErrorCode::UnsafePath);
  }
  if (open) {
    if (!held(slot.data.get())) {
      using Access = storage::detail::OriginalPayloadAccess;
      slot.data=take(storage::detail::openOriginalPayloadAt(slot.parent.get(),slot.actual.filename(),
          readOnly?Access::ReadOnly:Access::ReadWrite,static_cast<unsigned>(named.links),!readOnly));
    }
    const auto captured=stateStamp(slot.data.get());
    require(captured&&(readOnly?sameExistingSnapshot(named,*captured):sameFile(named,*captured)),
        "Managed payload changed while opening",ErrorCode::Conflict);
    slot.identity=*captured;
  } else { slot.identity=named; }
  return true;
}
std::expected<void,Error> validateExistingPayload(const fs::path& path,StateHandle parent,
    OriginalPayloadHandle descriptor,const StateStamp& expected) {
  try {
    const auto namedParent=parentDirectory(path.parent_path(),false);
    const auto namedDirectory=stateStamp(namedParent.get());
    const auto heldDirectory=stateStamp(parent);
    require(namedDirectory&&heldDirectory&&sameStateIdentity(*namedDirectory,*heldDirectory),
        "Existing-only payload parent changed",ErrorCode::Conflict);
    const auto opened=stateStamp(descriptor);
    const auto named=stateStampAt(parent,path.filename());
    require(opened&&named&&*named&&sameExistingSnapshot(expected,*opened)&&sameExistingSnapshot(expected,**named),
        "Existing-only payload identity/content metadata changed",ErrorCode::Conflict);
    return {};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Cannot validate existing-only named payload"});}
}
std::vector<std::pair<std::uint64_t,std::uint64_t>> missingPayloadSnapshot(const fs::path& path){
  require(path.is_absolute()&&path.lexically_normal()==path,"Unsafe missing recheck path",ErrorCode::UnsafePath);
  const auto root=filesystemRoot(path);
  auto parent=take(openFilesystemRoot(root));
  auto stamp=take(stateStamp(parent.get()));
  std::vector<std::pair<std::uint64_t,std::uint64_t>> out{{stamp.device,stamp.inode}};
  for(const auto& part:relativeComponents(path,root)){
    const auto named=stateStampAt(parent.get(),part);
    require(named.has_value(),"Cannot observe missing recheck path",ErrorCode::UnsafePath);
    if(!*named){return out;}
    require((*named)->directory,"Previously missing recheck payload appeared or path is unsafe",ErrorCode::Conflict);
    auto next=take(openDirectoryAt(parent.get(),part));
    stamp=take(stateStamp(next.get()));
    require(stamp.directory&&sameStateIdentity(stamp,**named),"Missing recheck ancestor changed",ErrorCode::Conflict);
    out.emplace_back(stamp.device,stamp.inode);parent=std::move(next);
  }
  throw Failure{{ErrorCode::Conflict,"Missing recheck payload unexpectedly exists"}};
}
std::function<std::expected<void,Error>()> missingPayloadProof(const fs::path& path){
  auto expected=missingPayloadSnapshot(path);return [path,expected=std::move(expected)]()->std::expected<void,Error>{try{require(missingPayloadSnapshot(path)==expected,"Missing recheck path/ancestor changed",ErrorCode::Conflict);return {};}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate missing recheck path"});}};
}
std::function<std::expected<void,Error>()> existingPayloadProof(const Slot& slot,const fs::path& save,OriginalPayloadHandle descriptor) {
  auto parent=std::make_shared<StateDescriptor>(take(storage::detail::duplicateStateDirectory(slot.parent.get())));
  const auto path=(save/slot.actual).lexically_normal();const auto expected=slot.identity;
  return [parent,path,expected,descriptor]{return validateExistingPayload(path,parent->get(),descriptor,expected);};
}
using DirectoryChain=std::vector<std::pair<std::uint64_t,std::uint64_t>>;
DirectoryChain existingDirectoryChain(const fs::path& path){
  require(path.is_absolute()&&path.lexically_normal()==path,"Unsafe existing directory chain",ErrorCode::UnsafePath);
  const auto root=filesystemRoot(path);
  auto current=take(openFilesystemRoot(root));
  const auto top=take(stateStamp(current.get()));
  DirectoryChain chain{{top.device,top.inode}};
  for(const auto& part:relativeComponents(path,root)){
    auto next=take(openDirectoryAt(current.get(),part));
    const auto opened=take(stateStamp(next.get()));
    const auto named=stateStampAt(current.get(),part);
    require(named&&*named&&opened.directory&&(*named)->directory&&sameStateIdentity(opened,**named),
        "Selected-existing ancestor changed",ErrorCode::Conflict);
    require(chain.size()<256,"Selected-existing ancestor depth bound",ErrorCode::Unsupported);
    chain.emplace_back(opened.device,opened.inode);current=std::move(next);
  }
  return chain;
}
std::function<std::expected<void,Error>()> selectedPayloadProof(const Slot& slot,const fs::path& save,OriginalPayloadHandle descriptor){
  const auto path=(save/slot.actual).lexically_normal(),directory=path.parent_path();auto chain=existingDirectoryChain(directory);auto leaf=existingPayloadProof(slot,save,descriptor);
  return [directory,chain=std::move(chain),leaf=std::move(leaf)]() -> std::expected<void,Error>{
    try{require(existingDirectoryChain(directory)==chain,"Approved selected-existing ancestor identity changed",ErrorCode::Conflict);return leaf();}
    catch(const Failure& failure){return std::unexpected(failure.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate selected-existing ancestry"});}
  };
}
void existingOnlyAttributes(const Node& attributes,bool multifile,std::size_t files,bool approvedLinks=false) {
  require(journals(attributes,multifile,files).empty(),"Existing-only verification cannot complete pending payload names");
  if(const auto* links=attributes.find("filelinks2")){require(links->type==Node::Type::List&&(approvedLinks||links->children.empty()),"Existing-only linked verification requires an authenticated mapped scope");}
  for(const auto* key:{"incompfilesuffix","dnd_pfx","dnd_sf","dnd_al","BiglyBTC.compactConversion"}){if(const auto* value=attributes.find(key)){
    require((value->type==Node::Type::List&&value->children.empty())||(value->type==Node::Type::Bytes&&value->bytes.empty()),"Existing-only verification cannot redirect or complete payload paths");}}
}
std::vector<fs::path> existingMappedPaths(const core::SavedDownload& saved,const compat::OriginalLayout& layout,const Node& attributes,const fs::path& save) {
  std::vector<std::string> bases;for(const auto& file:layout.files){bases.push_back(pathToUtf8(save/(layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(file.path):pathFromUtf8(*saved.saveFile))));}
  const auto links=compat::resolveOriginalFileLinks(attributes.find("filelinks2"),bases);require(links.has_value(),"Invalid existing mapped file links",ErrorCode::InvalidFormat);
  std::vector<fs::path> paths(layout.files.size());for(std::size_t n=0;n<layout.files.size();++n){if(layout.files[n].padding){require(!links->byIndex[n],"Virtual padding cannot have an existing mapped target");continue;}paths[n]=links->byIndex[n]?pathFromUtf8(links->byIndex[n]->target):pathFromUtf8(bases[n]);}
  return paths;
}
std::vector<Slot> plan(const core::SavedDownload& saved, const Node& attributes, const compat::OriginalLayout& layout,
    const std::vector<compat::FilePolicy>& policy, const fs::path& save, const std::vector<Journal>& pending,
    const fs::path& profile,bool readOnly,std::span<const std::uint8_t> linearBackupCandidates,bool retainPayload=true) {
  require(!saved.legacySavePath&&saved.saveFile&&safeManagedRelative(pathFromUtf8(*saved.saveFile))&&!pathFromUtf8(*saved.saveFile).has_parent_path(),"Unsupported original save-root mapping",ErrorCode::UnsafePath);
  for (const auto* key : {"dnd_pfx","dnd_sf","dnd_al"}) {
    if (const auto* field=attributes.find(key)) { require(field->type==Node::Type::Bytes&&field->bytes.size()<=4096,"Invalid original DND policy",ErrorCode::InvalidFormat); }
  }
  const auto* stores=attributes.find("storetypes");
  require(stores&&stores->type==Node::Type::List&&stores->children.size()==layout.files.size(),"Storage-type count differs from metadata");
  std::vector<Slot> result; result.reserve(layout.files.size());
  for (std::size_t index=0;index<layout.files.size();++index) {
    const auto type=text(&stores->children[index]);
    require(type=="L"||type=="C"||type=="R"||type=="X","Unknown original storage kind");
    const auto& file=layout.files[index]; const auto base=layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(file.path):pathFromUtf8(*saved.saveFile);
    require(safeManagedRelative(base),"Unsafe managed payload path",ErrorCode::UnsafePath);
    result.push_back({index,base,base,base,file.bytes,file.padding,policy[index].skipped});
    auto& slot=result.back();slot.linearBackupCandidate=index<linearBackupCandidates.size()&&linearBackupCandidates[index];
    slot.kind=type=="L"?storage::OriginalStorageKind::Linear:type=="C"?storage::OriginalStorageKind::Compact:type=="R"?storage::OriginalStorageKind::Reordered:storage::OriginalStorageKind::ReorderedCompact;
    slot.geometry={static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout.pieceLength)};
    (void)take(storage::originalMaximumPhysicalSize(slot.geometry,slot.kind));
  }
  std::set<std::size_t> linked;
  std::vector<std::string> originalLinkBases; originalLinkBases.reserve(result.size());
  for (const auto& slot:result) { originalLinkBases.push_back(pathToUtf8(save/slot.base)); }
  const auto links=compat::resolveOriginalFileLinks(attributes.find("filelinks2"),originalLinkBases);
  if (!links) {
    const auto code=links.error().code==compat::OriginalFileLinksErrorCode::Memory?ErrorCode::Io:
        links.error().code==compat::OriginalFileLinksErrorCode::AmbiguousBase?ErrorCode::UnsafePath:ErrorCode::InvalidFormat;
    require(false,std::string(links.error().message),code);
  }
  for (std::size_t index=0;index<result.size();++index) {
    if (!links->byIndex[index]) { continue; }
    auto& slot=result[index]; const auto target=pathFromUtf8(links->byIndex[index]->target);
    require(target.is_absolute()&&target.lexically_normal()==target&&target.native().size()<=4096&&target.native().find('\0')==target.native().npos,"Unsafe external file link",ErrorCode::UnsafePath);
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
  std::set<fs::path,FilesystemPathLess> physical; std::set<std::pair<std::uint64_t,std::uint64_t>> inodes;
  for (auto& slot:result) {
    if (slot.padding) { require(!byIndex.contains(slot.index),"Padding cannot own completion journal"); continue; }
    const auto found=byIndex.find(slot.index); const auto* journal=found==byIndex.end()?nullptr:found->second;
    require(!journal||slot.actual==journal->source||slot.actual==journal->target,"Journal path differs from mapping",ErrorCode::Conflict);
    if (!inspect(slot,save,journal,retainPayload,readOnly)) {
      if (journal) {
        slot.actual=slot.actual==journal->source?journal->target:journal->source;
        require(inspect(slot,save,journal,retainPayload,readOnly),"Both completion journal paths are absent",ErrorCode::Conflict);
      } else if (!linked.contains(slot.index)&&!suffix.empty()) {
        slot.actual+=pathFromUtf8(suffix); require(safeManagedRelative(slot.actual),"Unsafe incomplete filename",ErrorCode::UnsafePath); inspect(slot,save,nullptr,retainPayload,readOnly);
      }
    }
    slot.final=slot.actual; const auto absolute=(save/slot.actual).lexically_normal();requireUtf8Filesystem(absolute);
    require(!pathWithin(absolute,profile),"Managed payload overlaps profile",ErrorCode::UnsafePath);
    for (const auto& part:absolute) { require(!reservedComponent(part),"Managed payload overlaps reserved path",ErrorCode::UnsafePath); }
    if (const auto found=expanding.find(slot.index);found!=expanding.end()) {require(slot.identity.inode&&found->second==std::pair{static_cast<std::uint64_t>(slot.identity.device),static_cast<std::uint64_t>(slot.identity.inode)},"Compact conversion payload changed",ErrorCode::Conflict);}
    require(physical.insert(absolute).second,"Two files alias the same managed target",ErrorCode::UnsafePath);
    if (slot.identity.inode) { require(inodes.emplace(slot.identity.device,slot.identity.inode).second,"Two files alias the same managed inode",ErrorCode::UnsafePath); }
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
PayloadClaimPlan payloadClaimPlan(const std::vector<Slot>& mapping,const fs::path& save,
    const Node& attributes,const std::vector<Journal>& pending) {
  PayloadClaimPlan result;std::map<fs::path,std::size_t,FilesystemPathLess> paths;
  const auto suffix=attributes.find("incompfilesuffix")?text(attributes.find("incompfilesuffix")):std::string{};
  const auto prefix=attributes.find("dnd_pfx")?text(attributes.find("dnd_pfx")):std::string{};
  auto add=[&](const fs::path& relative,std::size_t index) {
    const auto absolute=(save/relative).lexically_normal();require(absolute.is_absolute()&&safeManagedPhysical(absolute),"Unsafe managed claim path",ErrorCode::UnsafePath);
    const auto [at,inserted]=paths.emplace(absolute,index);if(!inserted){require(at->second==index,"Managed files share a current/completion path",ErrorCode::Conflict);return;}
    require(result.values.size()<MaximumManagedPayloadClaims,"Managed payload claim bound exceeded",ErrorCode::Unsupported);
    const auto& id=mapping[index].identity;result.values.push_back({absolute,static_cast<std::uint64_t>(id.device),static_cast<std::uint64_t>(id.inode)});result.indices.push_back(index);
  };
  for(const auto& slot:mapping) {
    if(slot.padding){continue;}add(slot.actual,slot.index);add(slot.final,slot.index);
    const auto name=pathToUtf8(slot.actual.filename());
    if(!suffix.empty()&&name.size()>suffix.size()&&name.ends_with(suffix)) {
      auto next=name.substr(0,name.size()-suffix.size());if(!slot.skipped&&!prefix.empty()&&next.starts_with(prefix)){next.erase(0,prefix.size());}
      auto target=slot.actual;target.replace_filename(pathFromUtf8(next));require(!next.empty()&&safeManagedPhysical(target)&&!reservedComponent(target.filename()),"Unsafe forecast completion name",ErrorCode::UnsafePath);add(target,slot.index);
    }
  }
  for(const auto& entry:pending){add(entry.source,entry.index);add(entry.target,entry.index);}
  for(const auto& [path,index]:paths){(void)index;for(auto parent=path.parent_path();!parent.empty()&&parent!=parent.parent_path();parent=parent.parent_path()){require(!paths.contains(parent),"Managed file claims another file's directory",ErrorCode::Conflict);}}
  return result;
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
ManagedMainlinePolicy originalMainlinePolicy(const NetworkPolicy& policy, const Node& attributes, bool privateTorrent) {
  ManagedMainlinePolicy result{policy.dht && !privateTorrent, true};
  if (const auto* sources=attributes.find("peersources")) {
    require(sources->type==Node::Type::List && sources->children.size()<=16,"Invalid original peer sources",ErrorCode::InvalidFormat);
    result.discover=result.discover && std::ranges::any_of(sources->children,[](const Node& source){return text(&source)=="DHT";});
  }
  if (const auto* denied=attributes.find("peersourcesdenied")) {
    require(denied->type==Node::Type::List && denied->children.size()<=16,"Invalid original denied peer sources",ErrorCode::InvalidFormat);
    if (std::ranges::any_of(denied->children,[](const Node& source){return text(&source)=="DHT";})) { result.discover=false; }
  }
  if (const auto* options=attributes.find("pluginoptions")) {
    require(options->type==Node::Type::Dictionary,"Invalid original plugin options",ErrorCode::InvalidFormat);
    if (const auto* plugin=options->find("mldht")) {
      require(plugin->type==Node::Type::Dictionary,"Invalid original mlDHT options",ErrorCode::InvalidFormat);
      if (const auto* enabled=plugin->find("enableannounce")) {
        require(enabled->type==Node::Type::Integer,"Invalid original mlDHT announce option",ErrorCode::InvalidFormat);
        result.announce=static_cast<std::uint32_t>(enabled->integer)!=0;
      }
    }
  }
  // Disabled active announcements map to original QUEUED scrape-only mode.
  // Until that scrape service is ported, neither lookup nor peer injection is admitted.
  result.discover=result.discover && result.announce;
  result.announce=result.announce && result.discover;
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
    qualifiedNames=qualifiedNames||name=="utf-8"||name=="utf8"||name=="iso-8859-1"||name=="windows-1251"||name=="cp1251"||name=="cp5347"||name=="ansi-1251"
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
      const auto path=original.multifile?pathFromUtf8(original.name)/pathFromUtf8(file.path):pathFromUtf8(file.path);
      require(safeManagedRelative(path)&&std::ranges::none_of(pathToUtf8(path),[](unsigned char c){return c<0x20||c==0x7f;}),
          "Original decoded filename needs an unqualified filesystem conversion",ErrorCode::Unsupported);
    }
  }
  require(end==original.bytes,"Original filename projection byte span differs",ErrorCode::Unsupported);
  for(std::size_t i=0;i<original.files.size();++i){
    const auto& file=original.files[i];if(file.padding){continue;}
    metadata.rename_file(lt::file_index_t{static_cast<int>(i)},
        pathToUtf8(original.multifile?pathFromUtf8(original.name)/pathFromUtf8(file.path):pathFromUtf8(file.path)));
  }
  const auto after=take(storage::originalTorrentIdentityFromLibtorrent(source,metadata));
  require(after.full==identity.full&&after.originalKey==identity.originalKey&&after.selected==identity.selected,
      "Runtime filename projection changed original raw identity",ErrorCode::Conflict);
}
// Piece layers are outside the info hash. Authenticate every required layer
// against its info-bound file root before trusting a selected-V2 saved bitmap.
// This hashes metadata only; no payload bytes are read.
bool selectedV2PieceLayersReady(const lt::torrent_info& info){
  const auto pair=[](const lt::sha256_hash& first,const lt::sha256_hash& second){
    std::array<char,64> bytes{};std::copy_n(first.data(),32,bytes.data());std::copy_n(second.data(),32,bytes.data()+32);
    return lt::hasher256(lt::span<const char>(bytes.data(),bytes.size())).final();
  };
  const auto& files=info.files();
  for(const auto index:files.file_range()){
    if(files.pad_file_at(index)||files.file_size(index)<=info.piece_length()){continue;}
    const auto count=files.file_num_pieces(index);const auto raw=info.piece_layer(index);
    if(count<=0||raw.size()!=static_cast<std::ptrdiff_t>(count)*32){return false;}
    lt::sha256_hash pad;for(int blocks=files.blocks_per_piece();blocks>1;blocks/=2){pad=pair(pad,pad);}
    std::vector<lt::sha256_hash> layer;layer.reserve(static_cast<std::size_t>(count));
    for(int piece=0;piece<count;++piece){layer.emplace_back(raw.data()+static_cast<std::ptrdiff_t>(piece)*32);}
    while(layer.size()>1){const auto size=layer.size();for(std::size_t i=0;i<size;i+=2){layer[i/2]=pair(layer[i],i+1<size?layer[i+1]:pad);}layer.resize((size+1)/2);pad=pair(pad,pad);}
    if(layer.front()!=files.root(index)){return false;}
  }return true;
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
      const auto path=layout.multifile?pathFromUtf8(layout.name)/pathFromUtf8(file.path):pathFromUtf8(file.path);
      require(safeManagedRelative(path)&&std::ranges::none_of(pathToUtf8(path),[](unsigned char c){return c<0x20||c==0x7f;}),"Converted filename requires separate filesystem policy",ErrorCode::UnsafePath);
      projected->rename_file(lt::file_index_t{static_cast<int>(n)},pathToUtf8(path));
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
bool writeLinks(storage::TorrentStateFile& state, const std::vector<Slot>& slots, const fs::path& save, bool final) {
  auto attributes=dictionary(state.document().find("attributes"));const auto before=attributes.serialize(); std::string list="l";
  for (const auto& slot:slots) { const auto& path=final?slot.final:slot.actual;
    if (!slot.padding&&path!=slot.base) { list+=stringValue(std::to_string(slot.index)+'\n'+pathToUtf8((save/slot.base))+'\n'+pathToUtf8((save/path))); }
  }
  put(attributes,"filelinks2",list+'e');auto after=attributes.serialize();
  if(after==before){return false;}
  done(state.replaceSection("attributes",std::move(after)));return true;
}
void writeProgress(storage::TorrentStateFile& state, const std::vector<std::int64_t>& counts,
    std::span<const std::uint8_t> pieces, bool complete, bool classified) {
  auto resume=dictionary(state.document().find("resume")); auto data=dictionary(resume.find("data"));
  std::string bitmap(pieces.size(),'\2');
  for(std::size_t i=0;i<pieces.size();++i){if(pieces[i]){bitmap[i]=1;}else if(complete||classified){bitmap[i]=0;}}
  put(data,"resume data",stringValue(bitmap)); put(data,"valid",complete||classified?"i1e":"i0e"); put(data,"blocks","de");
  put(resume,"data",data.serialize()); done(state.replaceSection("resume",resume.serialize()));
  auto attributes=dictionary(state.document().find("attributes")); put(attributes,"resumecomplete",complete?"i2e":"i1e");
  auto downloaded=dictionary(attributes.find("filedownloaded")); std::string list="l";
  for (const auto value:counts) { list+=intValue(value); } put(downloaded,"downloaded",list+'e'); put(attributes,"filedownloaded",downloaded.serialize());
  done(state.replaceSection("attributes",attributes.serialize()));
}
void writeProgress(storage::TorrentStateFile& state,const std::vector<std::int64_t>& counts,
    std::size_t pieces,bool complete){
  const std::vector<std::uint8_t> flags(pieces,complete?1:0);
  writeProgress(state,counts,flags,complete,false);
}
} // namespace

struct ManagedStorage::Impl {
  core::TorrentHash hash;
  storage::OriginalTorrentIdentity selectedIdentity;
  std::shared_ptr<const ManagedPayloadCounterBridge> swarmBridge;
  core::SavedDownload captured;
  ManagedQueueMetadata queue;
  bool normalOriginalLibrary=false;
  std::int64_t mergeDelta=0;std::map<int,bool> mergeLayerProofs;
  ManagedMainlinePolicy mainline;
  std::vector<compat::FilePolicy> filePolicies,savedFilePolicies;
  std::string queueMappingScope;
  storage::TorrentStateStore store;
  storage::TorrentStateFile state;
  std::string categorySnapshot;
  std::shared_ptr<const storage::ProfileLease> categoryLease; // Actual same-OFD identity through storage lifetime.
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
  std::string recheckInventoryRow;
  bool historyRecorded=false;std::optional<storage::ErrorCode> historyProblem;std::optional<std::int64_t> historyBytes;
  bool progressFromPieces=false;
  std::string resumeDecision;
  std::optional<std::string> importedOriginalResume,importedOriginalBlocks,pendingOriginalResume;
  std::vector<std::function<std::expected<void,Error>()>> originalResumeProofs;
  std::unique_ptr<AllocationWork> allocation;
  std::vector<std::uint8_t> requiredPieces;
  const storage::ProfileLease* selectedLease=nullptr; // Caller retains sole lease through joined retirement.
  std::optional<storage::SelectedExistingScope> selectedScope;
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
#include "merge_storage.inc"
ManagedMainlinePolicy ManagedStorage::mainlinePolicy() const noexcept { return impl_?impl_->mainline:ManagedMainlinePolicy{}; }
ManagedStoragePurpose ManagedStorage::purpose() const noexcept {return impl_?impl_->purpose:ManagedStoragePurpose::Download;}
std::span<const std::uint8_t> ManagedStorage::existingVerificationPieces()const noexcept{return impl_?std::span<const std::uint8_t>{impl_->requiredPieces}:std::span<const std::uint8_t>{};}
std::expected<void,Error> ManagedStorage::validateExistingSnapshot() const {
  if(!impl_||(impl_->purpose!=ManagedStoragePurpose::VerifyExistingReadOnly&&impl_->purpose!=ManagedStoragePurpose::VerifySelectedReadOnly&&impl_->purpose!=ManagedStoragePurpose::RecheckReadOnly)){return std::unexpected(Error{ErrorCode::Unsupported,"No existing-only snapshot authority"});}
  try{if(impl_->purpose==ManagedStoragePurpose::RecheckReadOnly){for(const auto& proof:impl_->approvedProofs){done(proof());}return {};}if(impl_->purpose==ManagedStoragePurpose::VerifySelectedReadOnly){
      require(impl_->selectedLease&&impl_->selectedScope,"Selected-existing scope authority is absent",ErrorCode::Conflict);
      std::vector<fs::path> paths(impl_->slots.size());std::vector<storage::OriginalStorageKind> kinds;
      for(const auto& slot:impl_->slots){kinds.push_back(slot.kind);if(!slot.padding){paths[slot.index]=(impl_->save/slot.actual).lexically_normal();}}
      const auto checked=impl_->selectedScope->validate(*impl_->selectedLease,impl_->state.document(),impl_->layout,impl_->selectedIdentity,impl_->filePolicies,kinds,paths);if(!checked){return checked;}
    }
    if(!impl_->approvedProofs.empty()){for(const auto& proof:impl_->approvedProofs){const auto checked=proof();if(!checked){return checked;}}return {};}
    for(const auto& slot:impl_->slots){if(slot.padding||(impl_->purpose==ManagedStoragePurpose::VerifySelectedReadOnly&&!held(slot.data.get()))){continue;}const auto proof=validateExistingPayload((impl_->save/slot.actual).lexically_normal(),slot.parent.get(),slot.data.get(),slot.identity);if(!proof){return proof;}}return {};}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Existing-only snapshot validation failed"});}
}
std::expected<void,Error> ManagedStorage::validateSharedSeedSource(
    const storage::ProfileLease& lease,const compat::OriginalLayout& wanted,
    const fs::path& destination) const {
  if(!impl_||!impl_->categoryLease||!lease.valid()||!lease.sharesOwnership(*impl_->categoryLease)
      ||impl_->purpose!=ManagedStoragePurpose::VerifyExistingReadOnly){
    return std::unexpected(Error{ErrorCode::Conflict,"Sharing requires this profile's full verified-existing admission"});
  }
  const auto& actual=impl_->layout;
  if(!destination.is_absolute()||destination.lexically_normal()!=pathFromUtf8(impl_->captured.saveDirectory).lexically_normal()
      ||!impl_->captured.saveFile||*impl_->captured.saveFile!=wanted.name
      ||actual.name!=wanted.name||actual.multifile!=wanted.multifile||actual.bytes!=wanted.bytes
      ||actual.pieceLength!=wanted.pieceLength||actual.files.size()!=wanted.files.size()){
    return std::unexpected(Error{ErrorCode::Conflict,"Shared seed mapping or geometry differs from its admission"});
  }
  for(std::size_t i=0;i<actual.files.size();++i){
    const auto& a=actual.files[i];const auto& b=wanted.files[i];
    if(a.path!=b.path||a.offset!=b.offset||a.bytes!=b.bytes||a.padding!=b.padding){
      return std::unexpected(Error{ErrorCode::Conflict,"Shared seed file geometry differs"});
    }
  }
  return validateExistingSnapshot();
}
const ManagedQueueMetadata& ManagedStorage::queueMetadata() const { return impl_->queue; }
const core::SavedDownload& ManagedStorage::capturedDownload() const { return impl_->captured; }
std::span<const compat::FilePolicy> ManagedStorage::capturedFilePolicy() const noexcept { return impl_->filePolicies; }
const compat::OriginalLayout& ManagedStorage::capturedFileLayout() const noexcept { return impl_->layout; }
std::span<const compat::FilePolicy> ManagedStorage::capturedSavedFilePolicy() const noexcept { return impl_->savedFilePolicies; }
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
std::vector<std::int64_t> ManagedStorage::inspectFileProgress(std::span<const std::uint8_t> pieces) const {
  if(!impl_||impl_->layout.pieceLength<=0||pieces.size()!=static_cast<std::size_t>(impl_->params.ti->num_pieces())){return {};}
  // Original file offsets include Java's v2 padding. Never zip SDK file indices.
  std::vector<std::int64_t> prefix(pieces.size()+1);
  for(std::size_t n=0;n<pieces.size();++n){prefix[n+1]=prefix[n]+(pieces[n]?1:0);}
  const auto length=impl_->layout.pieceLength;std::vector<std::int64_t> result;result.reserve(impl_->layout.files.size());
  for(const auto& file:impl_->layout.files){
    if(!file.bytes){result.push_back(0);continue;}
    const auto first=file.offset/length,last=(file.offset+file.bytes-1)/length;
    if(first<0||last>=static_cast<std::int64_t>(pieces.size())){return {};}
    std::int64_t count=0;
    if(first==last){count=pieces[std::size_t(first)]?file.bytes:0;}
    else{count=(pieces[std::size_t(first)]?length-file.offset%length:0)+(pieces[std::size_t(last)]?(file.offset+file.bytes-1)%length+1:0)+(prefix[std::size_t(last)]-prefix[std::size_t(first+1)])*length;}
    result.push_back(count);
  }return result;
}
std::optional<bool> ManagedStorage::fileDeleted(std::size_t index) const noexcept {
  if (!impl_ || index >= impl_->slots.size() || impl_->slots[index].index != index) { return std::nullopt; }
  const auto kind = impl_->slots[index].kind;
  return kind == storage::OriginalStorageKind::Compact || kind == storage::OriginalStorageKind::ReorderedCompact;
}
std::shared_ptr<OriginalDiskFiles> ManagedStorage::mappedFiles() const { return impl_?impl_->mapped:nullptr; }
std::span<const ManagedPayloadClaim> ManagedStorage::claims() const noexcept { return impl_?std::span<const ManagedPayloadClaim>(impl_->payloadClaims):std::span<const ManagedPayloadClaim>{}; }
const storage::OriginalTorrentIdentity& ManagedStorage::identity() const {return impl_->selectedIdentity;}

void ManagedClaimIndex::append(std::span<const ManagedPayloadClaim> files){
  require(files.size()<=MaximumManagedPayloadClaims-count_,"Retained payload claim bound exceeded",ErrorCode::Unsupported);
  for(const auto& claim:files){
    require(claim.absolutePath.is_absolute()&&safeManagedPhysical(claim.absolutePath)
        &&claim.absolutePath!=fs::path("/")&&(claim.inode||!claim.device),"Malformed retained payload claim",ErrorCode::InvalidFormat);
    paths_.insert(claim.absolutePath);
    if(claim.inode){inodes_.emplace(claim.device,claim.inode);}
  }
  count_+=files.size();
}
void ManagedClaimIndex::refresh(std::span<const OwnerClaims> owners){
  const auto kept=std::ranges::count_if(owners,[&](const auto& owner){return owners_.contains(owner.admission);});
  if(static_cast<std::size_t>(kept)!=owners_.size()){*this={};}
  try{
    for(const auto& owner:owners){
      if(owners_.insert(owner.admission).second){append(owner.files);}
    }
  }catch(...){*this={};throw;}
}
void ManagedClaimIndex::check(std::span<const ManagedPayloadClaim> candidate)const{
  require(candidate.size()<=MaximumManagedPayloadClaims-count_,"Combined payload claim bound exceeded",ErrorCode::Unsupported);
  for(const auto& claim:candidate){
    bool overlap=paths_.contains(claim.absolutePath);
    for(auto parent=claim.absolutePath.parent_path();!overlap&&!parent.empty()&&parent!=parent.parent_path();parent=parent.parent_path()){
      overlap=paths_.contains(parent);
    }
    const auto descendant=paths_.lower_bound(claim.absolutePath);
    overlap=overlap||(descendant!=paths_.end()&&pathWithin(*descendant,claim.absolutePath));
    require(!overlap&&(!claim.inode||!inodes_.contains({claim.device,claim.inode})),
        "Payload path/inode is retained by another managed admission",ErrorCode::Conflict);
  }
}
core::SavedDownload ManagedAdmissionInventory::select(const Document& inventory,const core::TorrentHash& hash) {
  if(!source_||&source_->root()!=&inventory.root()){
    auto index=inventoryIndex(inventory);
    source_=inventory;
    index_=std::move(index);
  }
  const auto found=index_.find(hash.hex());
  require(found!=index_.end(),"Managed target is stale or unavailable",ErrorCode::Conflict);
  return found->second;
}

std::expected<ManagedStorage,Error> ManagedStorage::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,const NetworkPolicy& policy,std::span<const ManagedPayloadClaim> claimed,std::span<const storage::OriginalTorrentIdentity> retainedIdentities,std::shared_ptr<const ManagedPayloadCounterBridge> swarmBridge,ManagedStoragePurpose purpose,std::stop_token stop,ManagedAdmissionInventory* inventory,const ManagedClaimIndex* claimIndex) {
  return prepareImpl(lease,hash,policy,claimed,retainedIdentities,std::move(swarmBridge),purpose,nullptr,nullptr,stop,inventory,claimIndex);
}
std::expected<ManagedStorage,Error> ManagedStorage::prepareOwnedI2p(const storage::ProfileLease& lease,const core::TorrentHash& hash,const OwnedI2pPolicy& owned,std::span<const ManagedPayloadClaim> claimed,std::span<const storage::OriginalTorrentIdentity> identities){
  if(!owned.matches(lease,hash)){return std::unexpected(Error{ErrorCode::Conflict,"Owned I2P storage needs SAME lease/hash"});}
  return prepareImpl(lease,hash,NetworkPolicy{},claimed,identities,{},ManagedStoragePurpose::Download,&owned);
}
std::expected<ManagedStorage,Error> ManagedStorage::prepareMixedPublic(const storage::ProfileLease& lease,const core::TorrentHash& hash,const NetworkPolicy& policy,const MixedPublicPolicy& mixed,std::span<const ManagedPayloadClaim> claimed,std::span<const storage::OriginalTorrentIdentity> identities,std::shared_ptr<const ManagedPayloadCounterBridge> bridge,ManagedStoragePurpose purpose){
  if(!mixed.matches(lease,hash)){return std::unexpected(Error{ErrorCode::Conflict,"Mixed Public storage needs SAME lease/hash"});}
  return prepareImpl(lease,hash,policy,claimed,identities,std::move(bridge),purpose,nullptr,&mixed);
}
std::string ManagedStorage::preparedStateSnapshot()const{return impl_->state.document().serialize();}
bool ManagedStorage::historyCompletionRecorded()const noexcept{return impl_&&impl_->historyRecorded;}
std::optional<std::int64_t> ManagedStorage::historyCheckpointBytes()const noexcept{return impl_?impl_->historyBytes:std::nullopt;}
std::optional<storage::ErrorCode> ManagedStorage::historyCompletionProblem()const noexcept{return impl_?impl_->historyProblem:std::optional{ErrorCode::Conflict};}
std::string_view ManagedStorage::originalResumeDecision()const noexcept{return impl_?std::string_view(impl_->resumeDecision):std::string_view{};}
std::expected<void,Error> ManagedStorage::rebaseCategory(const storage::ProfileLease& lease){
  return rebaseMetadata(lease,false);
}
std::expected<void,Error> ManagedStorage::rebaseSwarmTags(const storage::ProfileLease& lease){
  return rebaseMetadata(lease,true);
}
std::expected<void,Error> ManagedStorage::rebaseMetadata(const storage::ProfileLease& lease,bool swarmTags){
  try{
    require(impl_!=nullptr,"Managed storage was moved or released",ErrorCode::Conflict);auto& owner=*impl_;
    require(!owner.attempted&&owner.categoryLease&&lease.sharesOwnership(*owner.categoryLease),
        "Metadata rebase requires the actual uncheckpointed owner and same OFD",ErrorCode::Conflict);
    const auto root=stateStamp(lease.directoryHandle());require(root&&root->device==owner.profileDevice&&root->inode==owner.profileInode
        &&owner.state.hash()==owner.hash,"Metadata rebase profile/full identity differs",ErrorCode::Conflict);
    auto changed=swarmTags ? owner.store.rebaseSwarmTags(lease,owner.state)
                           : owner.store.rebaseCategory(lease,owner.state);
    if(!changed){return std::unexpected(changed.error());}owner.categorySnapshot=std::move(*changed);return {};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory rebasing owner metadata"});}
}

std::string_view ManagedStorage::categoryStateSnapshot()const noexcept{return impl_?std::string_view(impl_->categorySnapshot):std::string_view{};}
std::expected<ManagedStorage,Error> ManagedStorage::prepareImpl(const storage::ProfileLease& lease,const core::TorrentHash& hash,const NetworkPolicy& policy,std::span<const ManagedPayloadClaim> claimed,std::span<const storage::OriginalTorrentIdentity> retainedIdentities,std::shared_ptr<const ManagedPayloadCounterBridge> swarmBridge,ManagedStoragePurpose purpose,const OwnedI2pPolicy* owned,const MixedPublicPolicy* mixed,std::stop_token stop,ManagedAdmissionInventory* indexedInventory,const ManagedClaimIndex* claimIndex) {
  try {
    auto sourceAccess=take(lease.sourceAccess(true));
    auto categoryLease=take(lease.retain());
    require(purpose==ManagedStoragePurpose::Download||purpose==ManagedStoragePurpose::VerifyExistingReadOnly||purpose==ManagedStoragePurpose::VerifySelectedReadOnly||purpose==ManagedStoragePurpose::RecheckReadOnly||purpose==ManagedStoragePurpose::AllocateOnly,"Unknown managed preparation purpose",ErrorCode::InvalidFormat);
    const bool allocationOnly=purpose==ManagedStoragePurpose::AllocateOnly;
    const bool rechecking=purpose==ManagedStoragePurpose::RecheckReadOnly;
    const bool selectedOnly=purpose==ManagedStoragePurpose::VerifySelectedReadOnly;
    const bool existingOnly=purpose==ManagedStoragePurpose::VerifyExistingReadOnly||selectedOnly||rechecking;
    require(!storage::hasPendingOriginalCompaction(lease),"Recover pending explicit compaction with all profile handles retired before admission",ErrorCode::Conflict);
    require(claimed.size()<=MaximumManagedPayloadClaims,"Retained payload claim bound exceeded",ErrorCode::Unsupported);
    require(retainedIdentities.size()<=MaximumManagedPayloadClaims,"Retained identity bound exceeded",ErrorCode::Unsupported);
    require(lease.valid(),"Managed storage requires a valid held lease",ErrorCode::UnsafePath);
    const auto profile=take(stateStamp(lease.directoryHandle()));
    auto inventory=take(storage::ConfigFile::load(lease,"downloads.config"));
    require(inventory.recoverySource()!=storage::RecoverySource::Saving,"Resolve saved-inventory recovery before managed preparation",ErrorCode::Conflict);
    const auto rawSaved=indexedInventory?indexedInventory->select(inventory.document(),hash)
        :selected(inventory.document(),hash);
    auto saved=rawSaved;
    const auto& row=entryFor(inventory.document(),saved,!inventory.document().find("downloads"));
    const auto allocationIntent=(row.find(storage::AllocationModeKey)||row.find(storage::AllocationStateKey))
        ?take(storage::readAllocationIntent(lease,hash)):std::optional<storage::AllocationIntent>{};
    require(!allocationOnly||(allocationIntent&&allocationIntent->state!=storage::AllocationState::Complete),"No current allocation authority",ErrorCode::Conflict);
    require(allocationOnly||!allocationIntent||allocationIntent->state==storage::AllocationState::Complete,"Pending allocation requires its typed owner",ErrorCode::Conflict);
    bool dataAlreadyAllocated=false;
    if(purpose==ManagedStoragePurpose::Download){
      if(const auto* allocated=row.find("allocated")){
        require(allocated->type==Node::Type::Integer,"Invalid original allocation flag",ErrorCode::InvalidFormat);
        // GlobalManagerImpl restores this with Long.intValue()==1.
        dataAlreadyAllocated=static_cast<std::uint32_t>(allocated->integer)==1u;
      }
    }
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
    if(const auto* lost=row.find("BiglyBTC.badPayloadCountersLost")){
      require(lost->type==core::Node::Type::Integer&&(lost->integer==0||lost->integer==1),
          "Invalid saved bad-payload uncertainty",ErrorCode::InvalidFormat);
      queue.badPayloadCountersLost=lost->integer==1;queue.badCounterLossSource=encoded(*lost);
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
    if(purpose==ManagedStoragePurpose::Download&&policy.mode==NetworkMode::Normal
        &&policy.context==NetworkPolicyContext::ExistingLibraryPublic&&identity.selected==storage::OriginalSelectedHash::V2){
      require(selectedV2PieceLayersReady(*info),"Saved V2 Merkle piece layers are missing or invalid; automatic recheck deferred",ErrorCode::Unsupported);
    }
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
    const auto originalQueueComplete=core::projectSavedDndProgress(layout.bytes,saved.completedBytes,
        core::readSavedDndFacts(&attributes.root())).controllerNoDiskCompleteExcludingDnd;
    queue.savedSelectedComplete=originalQueueComplete.value_or(saved.completedBytes&&*saved.completedBytes>=layout.bytes);

    const auto storageDefaults=take(storage::resolveOriginalStorageTypes(lease,hash,layout,attributes.root()));
    require(!storageDefaults.unsupportedConversion,"Original policy requires unsupported cross-family storage conversion");
    done(storage::applyOriginalStorageDefaults(attributes,storageDefaults,true));
    auto policies=take(storage::readOriginalFilePolicies(row,storageDefaults.requested));
    auto savedPolicy=std::move(policies.projected);
    // Existing explicit read-only scopes retain their admitted saved policy.
    auto parsedPolicy=purpose==ManagedStoragePurpose::Download?std::move(policies.restored):savedPolicy;
    auto pending=journals(attributes.root(),layout.multifile,layout.files.size()); const auto save=pathFromUtf8(saved.saveDirectory);
    require(save.is_absolute()&&save.lexically_normal()==save,"Managed save directory must be absolute and normalized",ErrorCode::UnsafePath);
    const auto profilePath=take(storage::detail::stateDirectoryPath(lease.directoryHandle())).lexically_normal();
    std::optional<storage::VerifiedExistingScope> approvedScope;
    std::optional<storage::SelectedExistingScope> selectedScope;
    if(selectedOnly){
      const auto intent=take(storage::readVerifiedSeedIntent(lease,hash));require(intent&&intent->goal==core::ExistingVerificationGoal::SelectedFiles,"Selected read-only storage requires its current typed saved intent",ErrorCode::Conflict);
      const auto* scope=row.find(storage::VerifiedSelectedScopeKey);require(scope,"Selected-existing scope disappeared",ErrorCode::Conflict);selectedScope=take(storage::SelectedExistingScope::decode(*scope));
    }
    if(existingOnly&&!selectedOnly&&!rechecking){
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
          require((parsedPolicy)[n].skipped&&(parsedPolicy)[n].priority==0,"Existing-only padding must remain virtual and unselected");
        }else{
          require(!(parsedPolicy)[n].skipped&&storageDefaults.requested[n]==storage::OriginalStorageKind::Linear,
              "Existing-only verification requires all real files selected in linear storage");
        }
      }
      require(pending.empty()&&storageDefaults.markers.empty(),
          "Existing-only verification cannot recover/convert original storage controls");
      existingOnlyAttributes(attributes.root(),layout.multifile,layout.files.size(),approvedScope&&approvedScope->mapped());
    }
    if(rechecking){
      require(identity.selected==storage::OriginalSelectedHash::V1,"Standalone recheck currently requires a full selected-v1 identity",ErrorCode::Unsupported);
      require(pending.empty(),"Recheck cannot recover pending completion controls",ErrorCode::Conflict);
      // Ordinary fmfile*.dat/.bak are authenticated physical C controls, not
      // pending conversion. The resolver already refuses .saving cuts; retain
      // requested==actual so read-only checking never changes storage family.
      require(storageDefaults.requested==storageDefaults.actual,"Recheck cannot convert original physical storage kinds",ErrorCode::Conflict);
    }
    std::vector<std::uint8_t> linearBackupCandidates(layout.files.size());
    if(purpose==ManagedStoragePurpose::Download){std::vector<std::uint8_t> markers(layout.files.size());
      for(const auto& marker:storageDefaults.markers){const auto backup=storage::originalControlLeaf(marker.index,storage::OriginalStorageKind::Compact)+".bak";markers[marker.index]|=marker.leaf==backup?2:1;}
      for(std::size_t index=0;index<markers.size();++index){linearBackupCandidates[index]=markers[index]==2&&storageDefaults.requested[index]==storage::OriginalStorageKind::Linear&&storageDefaults.actual[index]==storage::OriginalStorageKind::Compact;}
    }
    auto slots=plan(saved,attributes.root(),layout,parsedPolicy,save,pending,profilePath,existingOnly,linearBackupCandidates);
    if(purpose==ManagedStoragePurpose::Download&&dataAlreadyAllocated){
      // Original noncompact files always mustExistOrAllocate. Do not recreate
      // previously allocated data on an absent volume before owning state.
      for(const auto& slot:slots){
        if(slot.padding||slot.kind==storage::OriginalStorageKind::Compact||slot.kind==storage::OriginalStorageKind::ReorderedCompact){continue;}
        require(slot.identity.inode&&held(slot.data.get()),
            "Previously allocated data is missing; reconnect its storage before starting",ErrorCode::Missing);
      }
    }
    if(purpose==ManagedStoragePurpose::Download&&dataAlreadyAllocated){
      const auto compact=[](auto kind){return kind==storage::OriginalStorageKind::Compact||kind==storage::OriginalStorageKind::ReorderedCompact;};
      const bool missingCompact=std::ranges::any_of(slots,[&](const auto& slot){return !slot.padding&&compact(slot.kind)&&(!slot.identity.inode||!held(slot.data.get()));});
      if(missingCompact){
        // The same actual requested-kind constructor/restoration policy is
        // used by the live owner,SDK priorities and required-file preflight.
        require(storageDefaults.requested.size()==layout.files.size(),"Incomplete original requested storage census",ErrorCode::InvalidFormat);
        std::vector<compat::OriginalFilePieceSpan> spans;spans.reserve(layout.files.size());
        for(std::size_t i=0;i<layout.files.size();++i){const auto& file=layout.files[i];
          const auto span=compat::originalFilePieceSpan(file.offset,file.bytes,layout.pieceLength,parsedPolicy[i].skipped);
          require(span.has_value(),"Original compact availability geometry is unsupported",ErrorCode::Unsupported);spans.push_back(*span);
        }
        const auto required=compat::originalCompactFileRequirements(spans);
        require(required.has_value(),"Original compact availability census is unsupported",ErrorCode::Unsupported);
        for(const auto& slot:slots){
          if(slot.padding||!compact(slot.kind)||(slot.identity.inode&&held(slot.data.get()))){continue;}
          require(compact(storageDefaults.requested[slot.index])&&!(*required)[slot.index],
              "Previously allocated required compact data is missing; reconnect its storage before starting",ErrorCode::Missing);
        }
      }
    }
    if(rechecking){for(const auto& slot:slots){require(slot.padding||(!slot.expanding&&(slot.kind==storage::OriginalStorageKind::Linear||slot.kind==storage::OriginalStorageKind::Compact)),"Read-only recheck supports ordinary linear/compact storage",ErrorCode::Unsupported);}}

    std::unique_ptr<AllocationWork> allocation;
    if(allocationOnly){
      require(pending.empty()&&storageDefaults.markers.empty()&&slots.size()<=4096,"Allocation does not convert/recover nonlinear or completion controls",ErrorCode::Unsupported);
      require(integer(attributes.find("fas"),-1)==2,"Allocation requires original fas2",ErrorCode::Conflict);
      require((*flags&(0x200|0x4000))==0,"Original flags disable ordinary stop-after-allocation",ErrorCode::Unsupported);
      for(const auto& slot:slots){if(slot.padding){continue;}require(!slot.skipped&&slot.kind==storage::OriginalStorageKind::Linear&&!slot.expanding&&slot.actual==slot.base&&slot.final==slot.base,"Allocation requires every real file selected in linear storage",ErrorCode::Unsupported);}
      allocation=readAllocationWork(*allocationIntent,attributes.root(),source,slots,save);
      if(!attributes.find(AllocationWorkKey)){
        std::vector<fs::path> paths(slots.size());for(const auto& slot:slots){if(!slot.padding){paths[slot.index]=(save/slot.actual).lexically_normal();}}
        done(storage::validateAllocationTargets(lease,*allocationIntent,source,paths));
      }
    }
    if(existingOnly&&!selectedOnly&&!rechecking){for(const auto& slot:slots){if(slot.padding){continue;}require(slot.kind==storage::OriginalStorageKind::Linear&&!slot.expanding&&(slot.actual==slot.base||(approvedScope&&approvedScope->mapped()))
          &&held(slot.data.get())&&slot.identity.size==slot.bytes&&slot.identity.links==1,
          "Existing-only verification requires every complete original linear leaf",ErrorCode::Missing);}}
    std::vector<std::uint8_t> requiredPieces;
    if(existingOnly){const auto goal=core::existingPieceGoal(layout,parsedPolicy,selectedOnly?core::ExistingVerificationGoal::SelectedFiles:core::ExistingVerificationGoal::FullTorrent,static_cast<std::size_t>(info->num_pieces()));require(goal.has_value(),"Invalid authenticated existing piece goal",ErrorCode::InvalidFormat);requiredPieces=*goal;}
    if(selectedOnly){
      require(pending.empty(),"Selected-existing verification cannot recover completion names");existingOnlyAttributes(attributes.root(),layout.multifile,layout.files.size(),true);
      std::vector<fs::path> paths(slots.size());std::vector<storage::OriginalStorageKind> kinds;
      for(const auto& slot:slots){kinds.push_back(slot.kind);require(!slot.expanding,"Selected read-only verification cannot recover conversion");if(!slot.padding){paths[slot.index]=(save/slot.actual).lexically_normal();}}
      done(selectedScope->validate(lease,source,layout,identity,parsedPolicy,kinds,paths));
    }
#ifdef _WIN32
    // Use the retained payload handles for ordinary linear files as well.
    // libtorrent's independent mmap opens cannot own those Win32 handles.
    const bool mapped=!allocationOnly;
#else
    const bool mapped=!allocationOnly&&(existingOnly||static_cast<std::size_t>(info->num_files())!=slots.size()||std::ranges::any_of(slots,[](const Slot& slot) {return !slot.padding&&(slot.kind!=storage::OriginalStorageKind::Linear||slot.actual.is_absolute()||slot.expanding);}));
#endif
    // Validate every existing physical map before creating files or owning state.
    for (const auto& slot:slots) {
      if(slot.padding||slot.kind==storage::OriginalStorageKind::Linear||!held(slot.data.get())){continue;}
      auto control=take(OriginalControl::open(lease,hash.hex(),slot.index,slot.kind,false));
      auto io=take(storage::OriginalFileIo::open(slot.data.get(),slot.geometry,slot.kind,control?take((selectedOnly||rechecking)?control->loadValidated(slot.geometry):control->load()):std::nullopt,{},!existingOnly,slot.expanding&&slot.kind==storage::OriginalStorageKind::Compact,static_cast<unsigned>(slot.identity.links)));
      require(!existingOnly||!io.recoveryPending(),"Read-only existing data requires unresolved original recovery");
    }
    std::size_t budget=8192;
#ifndef _WIN32
    struct rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE,&limit)==0&&limit.rlim_cur!=RLIM_INFINITY) { budget=std::min(budget,static_cast<std::size_t>(limit.rlim_cur/(mapped?8:4))); }
#endif
    const auto files=static_cast<std::size_t>(std::ranges::count_if(slots,[](const auto& slot) { return !slot.padding; }));
    require(files<=budget,"Managed payload exceeds descriptor budget");
    params.save_path=pathToUtf8(save); params.file_priorities=priorities(parsedPolicy,layout);
    if(rechecking){std::fill(params.file_priorities.begin(),params.file_priorities.end(),lt::default_priority);}
    params.file_priorities.resize(static_cast<std::size_t>(info->num_files()),lt::download_priority_t{0});
    for (auto [key,target]:{std::pair{"maxdl",&params.download_limit},{"maxul",&params.upload_limit}}) {
      const auto value=integer(row.find(key)); require(value>=0&&value<=std::numeric_limits<int>::max(),"Unsupported disabled/overflowed per-torrent rate limit");
      *target=value?static_cast<int>(value):-1;
    }
    params.max_uploads=4;
    if(const auto* parameters=attributes.find("parameters")){
      require(parameters->type==Node::Type::Dictionary,"Invalid saved torrent connection parameters",ErrorCode::InvalidFormat);
      for(const auto& [key,target]:{std::pair{"max.peers",&params.max_connections},{"max.uploads",&params.max_uploads}}){
        if(const auto* setting=parameters->find(key)){require(setting->type==Node::Type::Integer&&setting->integer>=0&&setting->integer<=65535,"Invalid saved torrent connection limit",ErrorCode::InvalidFormat);*target=std::string_view(key)=="max.uploads"?std::max(2,int(setting->integer)):setting->integer?int(setting->integer):-1;}
      }
    }
    params.flags&=~lt::torrent_flags::auto_managed; params.flags|=lt::torrent_flags::paused;
    const auto selectedNetworks=networks(source,attributes.root());
    if(mixed){
      const auto* selected=attributes.root().find("networks");
      require(selected&&selected->type==Node::Type::List&&selected->children.size()==2
          &&selected->children[0].type==Node::Type::Bytes&&selected->children[1].type==Node::Type::Bytes
          &&selected->children[0].bytes!=selected->children[1].bytes
          &&selectedNetworks.publicNetwork&&selectedNetworks.i2p&&!selectedNetworks.tor,
          "Mixed storage needs exactly saved Public and I2P networks",ErrorCode::Unsupported);
    }
    auto mainline=originalMainlinePolicy(policy,attributes.root(),info->priv());
    mainline.discover=mainline.discover && selectedNetworks.publicNetwork && selectedNetworks.dhtBackup && (!selectedNetworks.tor||policy.context==NetworkPolicyContext::ExistingLibraryPublic) && (!selectedNetworks.i2p||mixed||policy.context==NetworkPolicyContext::ExistingLibraryPublic);
    mainline.announce=mainline.announce && mainline.discover;
    auto filtered=owned?owned->apply(selectedNetworks,params):mixed?mixed->apply(policy,selectedNetworks,params):applyExistingTorrentNetworkPolicy(policy,selectedNetworks,params);
    if(!filtered){throw Failure{Error{ErrorCode::Unsupported,"Managed torrent selection is unsupported by the owner policy: "+filtered.error().field}};}
    auto claimPlan=payloadClaimPlan(slots,save,attributes.root(),pending);
    ManagedClaimIndex localClaims;
    if(claimIndex){require(claimed.empty(),"Ambiguous retained payload source",ErrorCode::Conflict);}
    else{localClaims.append(claimed);claimIndex=&localClaims;}
    claimIndex->check(claimPlan.values);
    // All unsupported geometry/mapping/policy decisions precede state ownership
    // and payload mutation. First borrow the same cached sections read-only;
    // untranslatable Resume must not be invalidated by beginUpdate.
    require(!stop.stop_requested(),"Read-only check cancelled before saved preparation",ErrorCode::Cancelled);
    const auto resumeDirectory=storage::detail::openStateDirectory(lease.directoryHandle(),"active");
    const auto resumePrimary=resumeDirectory?stateStampAt(resumeDirectory->get(),hash.hex()+".dat"):
        std::expected<std::optional<StateStamp>,Error>{std::nullopt};
    const bool resumePrimaryKnown=resumePrimary&&*resumePrimary&&(*resumePrimary)->regular;
    auto state=take(store.loadAttributes(lease,hash)); auto actual=dictionary(state.document().find("attributes"));
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
    if(existingOnly&&!rechecking){existingOnlyAttributes(actual.root(),layout.multifile,layout.files.size(),selectedOnly||(approvedScope&&approvedScope->mapped()));}
    {
      auto check=plan(saved,actual.root(),layout,parsedPolicy,save,journals(actual.root(),layout.multifile,layout.files.size()),profilePath,existingOnly,linearBackupCandidates,false);
      require(check.size()==slots.size(),"Managed mapping changed during preparation",ErrorCode::Conflict);
      const auto refreshedClaims=payloadClaimPlan(check,save,actual.root(),journals(actual.root(),layout.multifile,layout.files.size()));
      claimIndex->check(refreshedClaims.values);
      require(refreshedClaims.values.size()==claimPlan.values.size(),"Managed completion scope changed during preparation",ErrorCode::Conflict);
      for(std::size_t i=0;i<claimPlan.values.size();++i){require(refreshedClaims.values[i].absolutePath==claimPlan.values[i].absolutePath,"Managed claim path changed during preparation",ErrorCode::Conflict);}
      for (std::size_t index=0;index<slots.size();++index) {
        require(check[index].actual==slots[index].actual,"Authoritative managed mapping changed",ErrorCode::Conflict);
        require((check[index].identity.inode==0)==(slots[index].identity.inode==0)
          &&(!slots[index].identity.inode||(existingOnly?sameExistingSnapshot(check[index].identity,slots[index].identity):sameFile(check[index].identity,slots[index].identity))),"Managed payload changed during preparation",ErrorCode::Conflict);
      }
    }
    // Cache values are copied before their backing state is relinquished.
    auto rechecked=owned?owned->apply(networks(state.document(),actual.root()),params):mixed?mixed->apply(policy,networks(state.document(),actual.root()),params):applyExistingTorrentNetworkPolicy(policy,networks(state.document(),actual.root()),params);
    require(rechecked.has_value(),"Authoritative torrent network policy changed",ErrorCode::Conflict);
    // Original RDResumeHandler restores DONE pieces from a valid saved map.
    // Read the actual primary map before native invalidation; presentation
    // completion and cached resumecomplete flags never grant this fast path.
    // metadata() proved the original/SDK piece geometry, file identities and
    // virtual padding before either selected-V1 or pure-V2 bits can be used.
    // Selected-V2 hybrids retain their separate qualified-provider boundary.
    std::optional<std::string> importedResume,importedBlocks,pendingResume,pendingPieces,pendingBlocks;
    // metadata() already proves every original/SDK file offset, padding slot
    // and piece count. Selected-v2 (including hybrid) additionally passed the
    // authenticated Merkle-layer check above and requires the exact selected-
    // swarm bridge. The same physical DONE-prefix proofs below apply to both.
    const bool resumeHashSupported=identity.selected==storage::OriginalSelectedHash::V1
        ||identity.selected==storage::OriginalSelectedHash::V2;
    std::string resumeDecision="Saved resume is unavailable in this admission mode";
    const auto resumeLayoutSupported=[&](const Slot& slot){
      return slot.padding||(!slot.expanding&&(slot.kind==storage::OriginalStorageKind::Linear
          ||(slot.kind==storage::OriginalStorageKind::Compact&&slot.skipped
              &&storageDefaults.requested[slot.index]==storage::OriginalStorageKind::Compact)));
    };
    std::vector<std::function<std::expected<void,Error>()>> compactResumeProofs(slots.size());
    if(purpose==ManagedStoragePurpose::Download&&policy.mode==NetworkMode::Normal&&policy.context==NetworkPolicyContext::ExistingLibraryPublic){
      if(!resumeHashSupported){resumeDecision="Saved selected-V2 hybrid resume translation is not available";}
      else if(!std::ranges::all_of(slots,resumeLayoutSupported)){resumeDecision="Saved resume requires a different storage layout";}
      else if(original.recoveredBackup()||state.recoveredBackup()){resumeDecision="Recovered saved state requires verification";}
    }
    if(purpose==ManagedStoragePurpose::Download&&!owned&&!mixed
        &&policy.mode==NetworkMode::Normal&&policy.context==NetworkPolicyContext::ExistingLibraryPublic
        &&resumeHashSupported
        &&std::ranges::all_of(slots,resumeLayoutSupported)
        &&!original.recoveredBackup()&&!state.recoveredBackup()){
      const auto uniqueDictionary=[](const Node* node){
        if(!node||node->type!=Node::Type::Dictionary||node->children.size()%2){return false;}
        std::set<std::string_view> keys;
        for(std::size_t i=0;i<node->children.size();i+=2){if(node->children[i].type!=Node::Type::Bytes||!keys.insert(node->children[i].bytes).second){return false;}}
        return true;
      };
      const auto* outer=source.find("resume");const auto* data=outer?outer->find("data"):nullptr;
      const auto* valid=data?data->find("valid"):nullptr;const auto* bitmap=data?data->find("resume data"):nullptr;const auto* blocks=data?data->find("blocks"):nullptr;
      if(!uniqueDictionary(outer)||!uniqueDictionary(data)){resumeDecision="Saved resume dictionaries are missing or invalid";}
      else if(!valid||valid->type!=Node::Type::Integer||(valid->integer!=0&&valid->integer!=1)){resumeDecision="Saved resume validity flag is missing or unsupported";}
      else if(!bitmap||bitmap->type!=Node::Type::Bytes){resumeDecision="Saved piece map is missing or invalid";}
      else if(bitmap->bytes.size()!=static_cast<std::size_t>(info->num_pieces())){resumeDecision="Saved piece count differs from engine geometry";}
      else if(!std::ranges::all_of(bitmap->bytes,[](char v){return v==0||v==1;})){resumeDecision="Saved piece map contains STARTED or RECHECK parts";}
      else if(blocks&&!uniqueDictionary(blocks)){resumeDecision="Saved partial-block map is invalid";}
      else{resumeDecision="Saved unfinished pieces require verification";}
      bool acceptable=uniqueDictionary(outer)&&uniqueDictionary(data)&&valid&&valid->type==Node::Type::Integer&&(valid->integer==0||valid->integer==1)
          &&bitmap&&bitmap->type==Node::Type::Bytes&&bitmap->bytes.size()==static_cast<std::size_t>(info->num_pieces())
          &&std::ranges::all_of(bitmap->bytes,[](char value){return value==0||value==1;})&&(!blocks||uniqueDictionary(blocks));
      // Java restores DONE and leaves NOT_DONE alone even when valid=0.
      // STARTED/RECHECK entries are excluded above. Prove only the physical
      // extents actually claimed by DONE; missing/skipped NOT_DONE files do
      // not invalidate checked pieces belonging to other files.
      if(acceptable&&!resumePrimaryKnown){resumeDecision="Original primary state timestamp is unavailable";}acceptable=acceptable&&resumePrimaryKnown;
      for(const auto& slot:slots){
        if(!acceptable||slot.padding||!slot.bytes){continue;}
        const auto start=slot.geometry.torrentOffset,end=start+slot.geometry.fileBytes;
        const auto pieceBytes=static_cast<std::uint64_t>(info->piece_length());
        std::uint64_t required=0;
        for(auto piece=start/pieceBytes;piece<=(end-1)/pieceBytes;++piece){
          if(bitmap->bytes[static_cast<std::size_t>(piece)]==1){required=std::min(end,(piece+1)*pieceBytes)-start;}
        }
        if(!required){continue;}
        std::optional<storage::OriginalFileIo> compact;
        if(slot.kind==storage::OriginalStorageKind::Compact){
          resumeDecision="Saved compact boundary extent requires verification";
          const auto control=OriginalControl::open(lease,hash.hex(),slot.index,slot.kind,false);
          if(!control){acceptable=false;break;}
          const auto raw=*control?(*control)->load():std::expected<std::optional<std::string>,Error>{std::nullopt};
          if(!raw||(*raw&&!storage::OriginalFileIo::validateControl(slot.geometry,slot.kind,**raw))){acceptable=false;break;}
          auto io=storage::OriginalFileIo::open(slot.data.get(),slot.geometry,slot.kind,*raw);
          if(!io||io->requiresRecheck()||io->recoveryPending()){acceptable=false;break;}
          compact=std::move(*io);
          for(auto piece=start/pieceBytes;piece<=(end-1)/pieceBytes;++piece){
            if(bitmap->bytes[static_cast<std::size_t>(piece)]!=1){continue;}
            const auto first=std::max(start,piece*pieceBytes)-start;
            const auto length=std::min(end,(piece+1)*pieceBytes)-start-first;
            const auto present=compact->rangePresent(first,length);
            if(!present||!*present){acceptable=false;break;}
          }
          if(!acceptable){break;}
          if(*control){compactResumeProofs[slot.index]=[control=*control,raw=*raw]() -> std::expected<void,Error>{
            const auto current=control->load();if(!current){return std::unexpected(current.error());}
            if(*current!=raw){return std::unexpected(Error{ErrorCode::Conflict,"Original compact resume control changed"});}return {};
          };}
        }
        const auto current=stateStampAt(slot.parent.get(),slot.actual.filename());
        resumeDecision="Payload identity, size or modification time requires verification";
        acceptable=slot.identity.inode&&current&&*current&&sameFile(**current,slot.identity)&&(*current)->size>=0
            &&(compact.has_value()||static_cast<std::uint64_t>((*current)->size)>=required);
        if (acceptable) {
          const auto& modified = (*current)->modified;
          const auto& saved = (*resumePrimary)->modified;
          acceptable = std::tie(modified.tv_sec, modified.tv_nsec) <= std::tie(saved.tv_sec, saved.tv_nsec);
        }
      }
      if(acceptable&&bitmap){
        resumeDecision="Saved checked pieces restored";
        importedResume=std::string(bitmap->bytes);if(blocks){importedBlocks=encoded(*blocks);}params.have_pieces.resize(info->num_pieces(),false);
        for(int i=0;i<info->num_pieces();++i){if((*importedResume)[static_cast<std::size_t>(i)]==1){params.have_pieces.set_bit(lt::piece_index_t{i});}}
      }
    }
    {
      const auto* outer=source.find("resume");const auto* data=outer?outer->find("data"):nullptr;
      const auto* bitmap=data?data->find("resume data"):nullptr;const auto* valid=data?data->find("valid"):nullptr;
      std::array<unsigned long long,5> counts{};
      if(bitmap&&bitmap->type==Node::Type::Bytes){for(const unsigned char piece:bitmap->bytes){++counts[piece<4?piece:4];}}
      std::array<unsigned,5> layouts{};for(const auto& slot:slots){if(slot.padding){continue;}++layouts[static_cast<unsigned>(slot.kind)-1];layouts[4]+=slot.expanding;}
      runtimeConsole("resume.source")<<" key="<<hash.hex()<<" saved_order="<<queue.position<<" valid="<<(valid&&valid->type==Node::Type::Integer?valid->integer:-1)<<" missing="<<counts[0]<<" done="<<counts[1]<<" recheck="<<counts[2]<<" started="<<counts[3]<<" other="<<counts[4]<<" linear="<<layouts[0]<<" compact="<<layouts[1]<<" reordered="<<layouts[2]<<" reordered_compact="<<layouts[3]<<" expanding="<<layouts[4]<<" selected_hash="<<(identity.selected==storage::OriginalSelectedHash::V1?"v1":"v2")<<" recovered="<<(original.recoveredBackup()||state.recoveredBackup())<<" decision="<<consoleText(resumeDecision)<<'\n'<<std::flush;
    }
    if(purpose==ManagedStoragePurpose::Download&&policy.mode==NetworkMode::Normal
        &&policy.context==NetworkPolicyContext::ExistingLibraryPublic&&!importedResume){
      const auto* outer=source.find("resume");const auto* data=outer?outer->find("data"):nullptr;
      const auto* bitmap=data?data->find("resume data"):nullptr;
      const bool requested=bitmap&&bitmap->type==Node::Type::Bytes
          &&std::ranges::any_of(bitmap->bytes,[](char value){return value==2||value==3;});
      require(requested,"Saved resume cannot be restored safely: "+resumeDecision+"; automatic full recheck deferred (use explicit Recheck)",ErrorCode::Unsupported);
    }
    // A mixed original map is NOT an SDK resume certificate. Keep its DONE
    // bits as saved metadata while the queued admission waits for a real check.
    // Only a joined, unclassified, zero-write retirement with fresh payload
    // proofs may restore this admission's exact input map/blocks/valid flag.
    if(purpose==ManagedStoragePurpose::Download&&!owned&&!mixed
        &&policy.mode==NetworkMode::Normal&&policy.context==NetworkPolicyContext::ExistingLibraryPublic
        &&!importedResume&&!original.recoveredBackup()&&!state.recoveredBackup()){
      const auto unique=[](const Node* node){if(!node||node->type!=Node::Type::Dictionary||node->children.size()%2){return false;}std::set<std::string_view> keys;for(std::size_t i=0;i<node->children.size();i+=2){if(node->children[i].type!=Node::Type::Bytes||!keys.insert(node->children[i].bytes).second){return false;}}return true;};
      const auto* outer=source.find("resume");const auto* data=outer?outer->find("data"):nullptr;
      const auto* bits=data?data->find("resume data"):nullptr;const auto* valid=data?data->find("valid"):nullptr;const auto* blocks=data?data->find("blocks"):nullptr;
      if(unique(outer)&&unique(data)&&valid&&valid->type==Node::Type::Integer&&(valid->integer==0||valid->integer==1)
          &&bits&&bits->type==Node::Type::Bytes&&bits->bytes.size()==static_cast<std::size_t>(info->num_pieces())
          &&std::ranges::all_of(bits->bytes,[](unsigned char v){return v<=3;})&&(!blocks||unique(blocks))){
        pendingResume=encoded(*outer);pendingPieces=std::string(bits->bytes);if(blocks){pendingBlocks=encoded(*blocks);}
      }
    }
    // Only now may this owner prepare durable write/recheck state. Every
    // unsupported clean-resume refusal above leaves the original map intact.
    // Publish the conservative map once. A failed/cancelled preparation must
    // never leave an intermediate all-RECHECK primary for a clean seed.
    state=take(store.stageUpdate(lease,hash));
    // Preserve original checked pieces. Newly written/unfinished pieces remain
    // RECHECK_REQUIRED after a crash; explicit recheck and unsupported mappings
    // keep their existing full validation path. LT counters still start at zero.
    auto resume=dictionary(state.document().find("resume"));auto resumeData=dictionary(resume.find("data"));
    auto durablePieces=importedResume.value_or(pendingPieces.value_or(std::string(static_cast<std::size_t>(info->num_pieces()),'\2')));
    const bool completeImported=importedResume&&std::ranges::all_of(durablePieces,[](char value){return value==1;});
    if(importedResume||pendingResume){
      std::vector<bool> wanted(durablePieces.size(),false);const auto pieceBytes=static_cast<std::uint64_t>(info->piece_length());
      for(const auto& slot:slots){if(slot.padding||slot.skipped||!slot.bytes){continue;}
        const auto first=slot.geometry.torrentOffset/pieceBytes,last=(slot.geometry.torrentOffset+slot.geometry.fileBytes-1)/pieceBytes;
        for(auto piece=first;piece<=last;++piece){wanted[static_cast<std::size_t>(piece)]=true;}
      }
      // Mark possible native writes conservatively before allowing I/O, but
      // never turn fully skipped, never-requested pieces into recheck work.
      for(std::size_t i=0;i<durablePieces.size();++i){if(durablePieces[i]==0&&wanted[i]){durablePieces[i]=2;}}
      if(importedBlocks){put(resumeData,"blocks",*importedBlocks);}else if(pendingBlocks){put(resumeData,"blocks",*pendingBlocks);}
    }else{put(resumeData,"blocks","de");}
    put(resumeData,"valid",completeImported?"i1e":"i0e");put(resumeData,"resume data",stringValue(durablePieces));
    put(resume,"data",resumeData.serialize());done(state.replaceSection("resume",resume.serialize()));
    done(storage::applyOriginalStorageDefaults(actual,storageDefaults));
    put(actual,"resumecomplete",completeImported?"i2e":"i1e");done(state.replaceSection("attributes",actual.serialize()));
    require(!stop.stop_requested(),"Read-only check cancelled before resume invalidation",ErrorCode::Cancelled);
    if(!rechecking){writeLinks(state,slots,save,false);} done(store.save(lease,state,nullptr,"managed-prepare"));
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
    std::vector<std::function<std::expected<void,Error>()>> resumeProofs,retirementProofs;
    const auto resumePayloadProof=[&](const Slot& slot,OriginalPayloadHandle descriptor){
      auto leaf=existingPayloadProof(slot,save,descriptor);auto control=compactResumeProofs[slot.index];
      return [leaf=std::move(leaf),control=std::move(control)]() -> std::expected<void,Error>{
        auto checked=leaf();if(!checked){return checked;}return control?control():std::expected<void,Error>{};
      };
    };
#ifdef _WIN32
    bool sparsePayload=false, sparseReserve=false;
    if (purpose==ManagedStoragePurpose::Download) {
      const auto config=storage::ConfigFile::load(lease,"biglybt.config");
      if (!config&&config.error().code!=ErrorCode::Missing) { throw Failure{config.error()}; }
      const bool enabled=config&&integer(config->document().find("Enable Sparse Files"))!=0;
      const bool skipReserve=config&&integer(config->document().find("Sparse Files No Prealloc"))!=0;
      const auto strategy=integer(actual.find("fas"));
      sparsePayload=enabled||strategy==3||strategy==4;
      sparseReserve=sparsePayload&&strategy!=4&&!(enabled&&skipReserve);
    }
#endif
    for (auto& slot:slots) {
      if (slot.padding) { continue; }
      const auto journal=std::find_if(pending.begin(),pending.end(),[&](const auto& value) { return value.index==slot.index; });
      const auto* intent=journal==pending.end()?nullptr:&*journal;
      if (!inspect(slot,save,intent,true,existingOnly)) {
        if(selectedOnly||rechecking){if(rechecking){approvedProofs.push_back(missingPayloadProof((save/slot.actual).lexically_normal()));}params.renamed_files[lt::file_index_t{static_cast<int>(slot.index)}]=pathToUtf8(slot.actual);continue;}
        require(!existingOnly,"Existing-only payload disappeared; no creation is authorized",ErrorCode::Conflict);
        slot.parent=parentDirectory((save/slot.actual).parent_path(),true);
        slot.allocationCreated=allocationOnly;
        slot.data=take(storage::detail::openOriginalPayloadAt(slot.parent.get(), slot.actual.filename(),
            storage::detail::OriginalPayloadAccess::CreateExclusive,1,true));
#ifdef _WIN32
        if (sparsePayload) {
          const auto sparse=storage::detail::enableSparseOriginalPayload(slot.data.get());
          if (!sparse&&sparse.error().code!=ErrorCode::Unsupported) { throw Failure{sparse.error()}; }
          if (sparse&&sparseReserve&&!slot.skipped&&slot.kind==storage::OriginalStorageKind::Linear) {
            require(storage::detail::resizeOriginalPayload(slot.data.get(),static_cast<std::uint64_t>(slot.bytes)),
                "Cannot reserve the logical length of the sparse payload",ErrorCode::Io);
          }
        }
#endif
        slot.identity=take(stateStamp(slot.data.get()));
        require(slot.identity.regular&&slot.identity.owned&&slot.identity.links==1,
            "Cannot exclusively create managed payload",ErrorCode::Conflict);
        done(syncStateFile(slot.data.get()));
        done(syncStateDirectory(slot.parent.get()));
      }
      // An absence receipt only authenticates data that remains absent. Bind
      // this now-present descriptor,then retire its old receipt durably before
      // any payload write or SDK admission can make the file valuable again.
      if(purpose==ManagedStoragePurpose::Download){
        auto latest=dictionary(state.document().find("attributes"));
        if(const auto* absent=latest.find("BiglyBTC.selectionAbsent")){
          require(absent->type==Node::Type::Dictionary,"Invalid selection absence receipts",ErrorCode::InvalidFormat);
          auto receipts=dictionary(absent);const auto key=std::to_string(slot.index);bool changed=false;
          if(receipts.find(key)){require(receipts.eraseValue(key).has_value(),"Cannot retire present-file absence receipt",ErrorCode::InvalidFormat);changed=true;}
          if(receipts.root().children.empty()){require(latest.eraseValue("BiglyBTC.selectionAbsent").has_value(),"Cannot retire empty absence receipts",ErrorCode::InvalidFormat);changed=true;}
          else if(changed){put(latest,"BiglyBTC.selectionAbsent",receipts.serialize());}
          if(changed){done(state.replaceSection("attributes",latest.serialize()));done(store.save(lease,state,nullptr,"managed-retire-selection-absence"));}
        }
      }
      // LT mmap storage uses partfiles for missing/empty skipped files. Sparse
      // extension keeps skipped boundary-piece bytes in the original linear file.
      if (!mapped&&slot.skipped&&slot.bytes>slot.identity.size) {
        require(storage::detail::resizeOriginalPayload(slot.data.get(),static_cast<std::uint64_t>(slot.bytes)),
            "Cannot prepare skipped linear file",ErrorCode::Io);
        done(syncStateFile(slot.data.get()));
        slot.identity=take(stateStamp(slot.data.get()));
      }
      if(mapped) {
        auto mappedDescriptor=duplicatePayload(slot.data.get());
        if(importedResume){resumeProofs.push_back(resumePayloadProof(slot,mappedDescriptor.get()));}
        std::shared_ptr<OriginalControl> control;
        if(slot.kind!=storage::OriginalStorageKind::Linear||slot.expanding) {control=take(OriginalControl::open(lease,hash.hex(),slot.index,slot.expanding?storage::OriginalStorageKind::Compact:slot.kind,!existingOnly));}
        if(slot.linearBackupCandidate&&slot.kind==storage::OriginalStorageKind::Linear&&slot.expanding){require(control&&take(control->hasBackupOnly()),"Linear compact-backup candidate changed before owned IO",ErrorCode::Conflict);}
        const auto rawControl=control?take((selectedOnly||rechecking)?control->loadValidated(slot.geometry):control->load()):std::nullopt;
        auto io=take(storage::OriginalFileIo::open(mappedDescriptor.get(),slot.geometry,slot.kind,rawControl,!existingOnly&&control?control->publisher():storage::OriginalControlPublisher{},!existingOnly,slot.expanding&&slot.kind==storage::OriginalStorageKind::Compact,static_cast<unsigned>(slot.identity.links)));
        if(existingOnly){require(!io.recoveryPending(),"Read-only existing data cannot recover original controls");}
        else if(io.recoveryPending()){done(io.recover());}
        if(!existingOnly&&((slot.kind==storage::OriginalStorageKind::Compact&&(!slot.skipped||storageDefaults.requested[slot.index]==storage::OriginalStorageKind::Linear))||slot.expanding)) {
          if(slot.linearBackupCleanup){done(storage::retireOwnedLinearCompactBackup(lease,store,state,slot.index,mappedDescriptor.get(),io,control));}
          else{done(storage::expandOwnedOriginalCompact(lease,store,state,slot.index,mappedDescriptor.get(),io,control));}
          slot.kind=storage::OriginalStorageKind::Linear;slot.expanding=false;
        } else if(!existingOnly&&slot.kind==storage::OriginalStorageKind::ReorderedCompact&&!slot.skipped) {
          done(storage::unskipOwnedOriginalReordered(lease,store,state,slot.index,mappedDescriptor.get(),io,control));
          slot.kind=storage::OriginalStorageKind::Reordered;
        }
        std::function<std::expected<void,Error>()> validate;
        if(selectedOnly||rechecking){
          auto leaf=selectedPayloadProof(slot,save,mappedDescriptor.get());const auto geometry=slot.geometry;
          validate=[leaf=std::move(leaf),control,geometry,rawControl]() -> std::expected<void,Error>{const auto checked=leaf();if(!checked){return checked;}if(control){const auto current=control->loadValidated(geometry);if(!current){return std::unexpected(current.error());}if(*current!=rawControl){return std::unexpected(Error{ErrorCode::Conflict,"Selected original control changed"});}}return {};};approvedProofs.push_back(validate);
        }else if(existingOnly){
          if(approvedScope){validate=take(approvedScope->bind(slot.index,(save/slot.actual).lexically_normal(),mappedDescriptor.get()));approvedProofs.push_back(validate);}
          else{validate=existingPayloadProof(slot,save,mappedDescriptor.get());}
        }
        else if(control&&slot.kind!=storage::OriginalStorageKind::Linear){validate=[control]() -> std::expected<void,Error> {auto checked=control->load();if(!checked){return std::unexpected(checked.error());}return {};};}
        mappedFiles[slot.index].emplace(OriginalDiskFile{std::move(mappedDescriptor),std::move(io),std::move(validate)});
      }
      if(importedResume||pendingResume){retirementProofs.push_back(resumePayloadProof(slot,slot.data.get()));}
      params.renamed_files[lt::file_index_t{static_cast<int>(slot.index)}]=pathToUtf8(slot.actual);
    }
    for(std::size_t i=0;i<claimPlan.values.size();++i){const auto& id=slots[claimPlan.indices[i]].identity;claimPlan.values[i].device=static_cast<std::uint64_t>(id.device);claimPlan.values[i].inode=static_cast<std::uint64_t>(id.inode);}
    auto checkedMainline=originalMainlinePolicy(policy,actual.root(),info->priv());
    const auto actualNetworks=networks(state.document(),actual.root());
    checkedMainline.discover=checkedMainline.discover && actualNetworks.publicNetwork && actualNetworks.dhtBackup && (!actualNetworks.tor||policy.context==NetworkPolicyContext::ExistingLibraryPublic) && (!actualNetworks.i2p||mixed||policy.context==NetworkPolicyContext::ExistingLibraryPublic);
    checkedMainline.announce=checkedMainline.announce && checkedMainline.discover;
    require(mainline==checkedMainline,"Original Mainline source selection changed during preparation",ErrorCode::Conflict);
    auto result=std::make_unique<Impl>(hash,std::move(identity),saved,std::move(store),std::move(state),std::move(layout),std::move(params),save,
      std::move(slots),std::move(pending),std::move(claimPlan.values),profile.device,profile.inode);
    result->approvedProofs=std::move(approvedProofs);
    result->categoryLease=std::move(categoryLease);
    result->mainline=mainline;
    result->normalOriginalLibrary=purpose==ManagedStoragePurpose::Download&&policy.mode==NetworkMode::Normal&&policy.context==NetworkPolicyContext::ExistingLibraryPublic;
    result->purpose=purpose;result->requiredPieces=std::move(requiredPieces);result->selectedLease=selectedOnly?&lease:nullptr;result->selectedScope=std::move(selectedScope); result->queue=queue; result->swarmBridge=std::move(swarmBridge); result->filePolicies=std::move(parsedPolicy);result->savedFilePolicies=std::move(savedPolicy);
    // Capture after authoritative cache hydration, defaults and conversions;
    // later classification cannot authorize a newly relinked physical map.
    result->queueMappingScope=take(storage::originalQueueMappingScope(result->state.document()));
    if(mapped){result->mapped=std::make_shared<OriginalDiskFiles>(info,std::move(mappedFiles),rechecking?OriginalDiskAccess::RecheckReadOnly:selectedOnly?OriginalDiskAccess::SelectedReadOnly:existingOnly?OriginalDiskAccess::ReadOnly:OriginalDiskAccess::ReadWrite,
        importedResume?std::function<bool()>{[proofs=std::move(resumeProofs)]{return std::ranges::all_of(proofs,[](const auto& proof){return bool(proof());});}}:std::function<bool()>{});}
    result->resumeDecision=std::move(resumeDecision);
    result->pendingOriginalResume=std::move(pendingResume);
    result->importedOriginalBlocks=std::move(importedBlocks);result->importedOriginalResume=std::move(importedResume);result->originalResumeProofs=std::move(retirementProofs);
    result->allocation=std::move(allocation);
    result->recheckInventoryRow=rechecking?encoded(entryFor(inventory.document(),saved,!inventory.document().find("downloads"))):std::string{};
    ManagedStorage prepared(std::move(result));if(selectedOnly||rechecking){done(prepared.validateExistingSnapshot());}
    if(allocationOnly){done(prepared.beginAllocation(lease));}return prepared;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io,"Managed storage preparation failed; prepared state may require recheck"}); }
}

#include "managed_allocation.inc"

#include "managed_recheck_storage.inc"

std::expected<core::SavedDownload,Error> ManagedCheckpointInventory::select(const storage::ProfileLease& lease,const core::TorrentHash& hash){
  try{
    if(!access_){access_.emplace(take(lease.sourceAccess(true)));}
    if(!inventory_){
      inventory_.emplace(take(storage::ConfigFile::load(lease,"downloads.config")));
      require(inventory_->recoverySource()!=storage::RecoverySource::Saving,"Resolve saved-inventory recovery before checkpoint",ErrorCode::Conflict);
      index_=inventoryIndex(inventory_->document());
    }
    const auto found=index_.find(hash.hex());
    require(found!=index_.end(),"Managed target is stale or unavailable",ErrorCode::Conflict);
    return found->second;
  }catch(const Failure& e){return std::unexpected(e.error);}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare shutdown inventory"});}
}
std::expected<void,Error> ManagedCheckpointInventory::stage(const core::SavedDownload& saved,std::string row,storage::CheckpointState state){
  try{
    require(inventory_&&saved.hash,"Missing shutdown inventory source",ErrorCode::Conflict);
    const auto found=index_.find(saved.hash->hex());
    require(found!=index_.end()&&found->second.sourceIndex==saved.sourceIndex,"Shutdown target changed",ErrorCode::Conflict);
    require(!rows_.contains(saved.sourceIndex),"Duplicate shutdown checkpoint",ErrorCode::Conflict);
    const auto limit=storage::compatibleConfigLimits().inputBytes;
    require(row.size()<=limit-rowBytes_,"Checkpoint rows exceed the inventory size limit",ErrorCode::InvalidFormat);
    const auto size=row.size();
    rows_.emplace(saved.sourceIndex,std::move(row));
    try {states_.push_back(state);rowBytes_+=size;}catch(...){rows_.erase(saved.sourceIndex);throw;}
    return {};
  }catch(const Failure& e){return std::unexpected(e.error);}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Cannot stage shutdown inventory"});}
}
std::expected<void,Error> ManagedCheckpointInventory::commit(const storage::ProfileLease& lease){
  std::expected<void,Error> result;
  try{
    if(!rows_.empty()){
      if(!access_){access_.emplace(take(lease.sourceAccess(true)));}
      // The owner releases the profile mutex while waiting for SDK callbacks.
      // Reject a changed generation before writing any participant, and let
      // ConfigFile compare the source again at its final atomic publication.
      const auto current=take(storage::ConfigFile::load(lease,"downloads.config"));
      require(current.recoverySource()==inventory_->recoverySource()
          &&current.document().originalBytes()==inventory_->document().originalBytes(),
          "Shutdown inventory changed before state publication",ErrorCode::Conflict);
      // Validate the complete inventory before its state fence. Bound each
      // state-file transaction independently of the SDK retirement window.
      const auto saveStates=[&]() -> std::expected<void,Error> {
        auto rest=std::span<const storage::CheckpointState>(states_);
        while(!rest.empty()){
          const auto count=std::min<std::size_t>(32,rest.size());
          auto saved=storage::CheckpointBatch::save(lease,rest.first(count));
          if(!saved){return saved;}
          rest=rest.subspan(count);
        }
        return {};
      };
      auto& document=inventory_->document();
      const auto* entries=document.find("downloads");
      if(entries&&!document.root().changed){
        const auto& original=document.originalBytes();
        std::string bytes;
        bytes.reserve(original.size());
        std::size_t cursor=0;
        for(const auto& [index,replacement]:rows_){
          const auto& row=entries->children.at(index);
          bytes.append(original,cursor,row.begin-cursor);
          bytes+=replacement;
          cursor=row.end;
        }
        bytes.append(original,cursor);
        result=inventory_->saveReplacement(lease,std::move(bytes),saveStates);
        clear();
        return result;
      }
      if(entries){
        std::string list="l";
        for(std::size_t n=0;n<entries->children.size();++n){
          const auto changed=rows_.find(n);list+=changed==rows_.end()?encoded(entries->children[n]):changed->second;
        }
        put(document,"downloads",std::move(list)+'e');
      }else{
        for(const auto& [hash,saved]:index_){(void)hash;
          if(const auto row=rows_.find(saved.sourceIndex);row!=rows_.end()){
            require(saved.legacyKey.has_value(),"Missing legacy shutdown key",ErrorCode::InvalidFormat);
            put(document,*saved.legacyKey,row->second);
          }
        }
      }
      done(saveStates());
      result=inventory_->save(lease);
    }
  }catch(const Failure& e){result=std::unexpected(e.error);}
  catch(const std::exception&){result=std::unexpected(Error{ErrorCode::Io,"Shutdown inventory publication failed"});}
  clear();return result;
}
bool ManagedStorage::supportsBatchedCheckpoint()const{
  if(!impl_||impl_->purpose!=ManagedStoragePurpose::Download||!impl_->pending.empty()){return false;}
  // These rename/journal operations require the inventory DURABLE before old
  // payload links can be removed. Preserve their original individual protocol.
  const auto* attrs=impl_->state.document().find("attributes");
  const auto* suffix=attrs?attrs->find("incompfilesuffix"):nullptr;
  if(suffix){
    if(suffix->type!=Node::Type::Bytes){return false;}
    if(!suffix->bytes.empty()){
      for(const auto& slot:impl_->slots){
        if(!slot.padding&&pathToUtf8(slot.actual.filename()).ends_with(suffix->bytes)){return false;}
      }
    }
  }
  return true;
}

std::expected<void,Error> ManagedStorage::checkpoint(const storage::ProfileLease& lease,std::span<const std::uint8_t> verified,
    bool complete,std::int64_t received,std::int64_t sent,
    std::int64_t secondsDownloading,std::int64_t secondsSeeding,std::optional<std::int32_t> secondsSinceUpload,
    std::optional<ManagedBadPayloadBytes> badPayloadBytes,bool badPayloadComplete,std::optional<ManagedHistoryCompletionEvent> historyEvent,bool mapClassified,bool retainUnclassifiedOriginal,
    ManagedCheckpointInventory* batch, std::optional<std::int64_t> lastActiveTagMillis) {
  try {
    require(impl_!=nullptr,"Managed storage was moved or released",ErrorCode::Conflict); auto& owner=*impl_;
    require(owner.purpose!=ManagedStoragePurpose::AllocateOnly,"Allocation cannot publish verified piece progress",ErrorCode::Conflict);
    require(!owner.attempted,"Managed admission checkpoint already attempted",ErrorCode::Conflict);
    require(!batch||supportsBatchedCheckpoint(),"Completion requires individual checkpoint publication",ErrorCode::Conflict);
    if(owner.purpose==ManagedStoragePurpose::VerifySelectedReadOnly){done(validateExistingSnapshot());}
    if(owner.mapped){if(const auto error=owner.mapped->failure()){throw Failure{*error};}done(owner.mapped->flush());}
    const auto profile=stateStamp(lease.directoryHandle());
    require(lease.valid()&&profile&&profile->device==owner.profileDevice&&profile->inode==owner.profileInode,
      "Managed checkpoint requires its original held lease",ErrorCode::UnsafePath);
    require(verified.size()==static_cast<std::size_t>(owner.params.ti->num_pieces())&&std::ranges::all_of(verified,[](auto value) { return value<=1; })
      &&(!complete||std::ranges::all_of(verified,[](auto value) { return value==1; })),"Invalid actual verified-piece checkpoint",ErrorCode::InvalidFormat);
    std::vector<std::uint8_t> retainedPieces;bool retainedOriginalClassification=false;
    if(retainUnclassifiedOriginal&&!mapClassified&&!complete&&owner.importedOriginalResume
        &&owner.originalResumeProofs.size()==static_cast<std::size_t>(std::ranges::count_if(owner.slots,[](const auto& slot){return !slot.padding;}))
        &&std::ranges::all_of(owner.originalResumeProofs,[](const auto& proof){return bool(proof());})){
      // A clean close may retire a still-paused admission before the SDK has
      // returned a piece map. Retain only the checked original bits whose
      // exact payload identities, sizes and timestamps are still unchanged.
      // Any SDK resume rejection/hash failure disables this path at the owner.
      retainedPieces.assign(verified.begin(),verified.end());for(std::size_t i=0;i<retainedPieces.size();++i){if((*owner.importedOriginalResume)[i]==1){retainedPieces[i]=1;}}
      retainedOriginalClassification=true;verified=retainedPieces;complete=std::ranges::all_of(verified,[](auto piece){return piece==1;});
    }
    // An SDK admission that has not finished checking supplies no replacement
    // classification. Keep the already-conservative resume and saved progress;
    // an empty temporary piece query must not move a queued seed into Leech.
    const bool preservePendingProgress=owner.normalOriginalLibrary&&retainUnclassifiedOriginal
        &&!mapClassified&&!complete&&!owner.importedOriginalResume;
    const bool retainPendingMap=preservePendingProgress&&owner.pendingOriginalResume&&received==0
        &&owner.originalResumeProofs.size()==static_cast<std::size_t>(std::ranges::count_if(owner.slots,[](const auto& slot){return !slot.padding;}))
        &&std::ranges::all_of(owner.originalResumeProofs,[](const auto& proof){return bool(proof());});
    const auto previousReceived=owner.captured.downloaded.value_or(0),previousSent=owner.captured.uploaded.value_or(0);
    require(received>=0&&sent>=0&&received<=std::numeric_limits<std::int64_t>::max()-previousReceived&&sent<=std::numeric_limits<std::int64_t>::max()-previousSent,
      "Managed admission cumulative counters overflow",ErrorCode::InvalidFormat);
    std::optional<storage::ConfigFile> individual;
    if(!batch){individual.emplace(take(storage::ConfigFile::load(lease,"downloads.config")));
      require(individual->recoverySource()!=storage::RecoverySource::Saving,"Resolve saved-inventory recovery before checkpoint",ErrorCode::Conflict);}
    const auto current=batch?take(batch->select(lease,owner.hash)):selected(individual->document(),owner.hash);
    const auto& source=batch?batch->document():individual->document();
    require(current.torrentFile==owner.captured.torrentFile&&current.saveDirectory==owner.captured.saveDirectory
      &&current.saveFile==owner.captured.saveFile&&current.legacySavePath==owner.captured.legacySavePath,"Managed saved mapping changed during admission",ErrorCode::Conflict);
    require(current.downloaded.value_or(0)==previousReceived&&current.uploaded.value_or(0)==previousSent,"Saved cumulative counters changed during managed admission",ErrorCode::Conflict);
    require(secondsDownloading>=0&&secondsSeeding>=0
        &&secondsDownloading<=std::numeric_limits<std::int64_t>::max()-owner.queue.secondsDownloading
        &&secondsSeeding<=std::numeric_limits<std::int64_t>::max()-owner.queue.secondsSeeding,
        "Managed queue elapsed times overflow",ErrorCode::InvalidFormat);
    auto entry=dictionary(&entryFor(source,current,!source.find("downloads")));
    std::optional<std::string> lossSource;
    if(const auto* lost=entry.find("BiglyBTC.badPayloadCountersLost")){lossSource=encoded(*lost);}
    require(lossSource==owner.queue.badCounterLossSource,"Saved bad-payload uncertainty changed during admission",ErrorCode::Conflict);
    for(const auto& [key,expected]:{std::pair{"discarded",owner.queue.discarded},{"hashfailbytes",owner.queue.hashFailed}}){
      const auto* value=entry.find(key);
      require(value?(value->type==core::Node::Type::Integer&&value->integer==expected):expected==0,
          "Saved bad-payload counters changed during admission",ErrorCode::Conflict);
    }
    if(badPayloadBytes){
      require(badPayloadBytes->discarded>=0&&badPayloadBytes->hashFailed>=0
          &&badPayloadBytes->discarded<=std::numeric_limits<std::int64_t>::max()-owner.queue.discarded
          &&badPayloadBytes->hashFailed<=std::numeric_limits<std::int64_t>::max()-owner.queue.hashFailed,
          "Managed bad-payload cumulative counters overflow",ErrorCode::InvalidFormat);
    }

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
    if(owner.mapped&&!preservePendingProgress){std::vector<std::uint8_t> completeFiles(static_cast<std::size_t>(owner.params.ti->num_files()));for(const auto& slot:owner.slots){completeFiles[slot.index]=!slot.padding&&counts[slot.index]==slot.bytes;}done(owner.mapped->finishComplete(completeFiles));}
    bool allocated=true;
    std::optional<timespec> latestPayload;
    if(!owner.mapped){latestPayload=timespec{};}
    for (auto& slot:owner.slots) {
      if (slot.padding) { continue; }
      if(owner.purpose==ManagedStoragePurpose::VerifySelectedReadOnly&&!held(slot.data.get())){require(counts[slot.index]==0,"Unallocated selected-existing file cannot own verified bytes",ErrorCode::Conflict);allocated=false;continue;}
      const auto actualParent=parentDirectory((owner.save/slot.actual).parent_path(),false);
      const auto parent=stateStamp(actualParent.get());
      require(parent.has_value(),"Managed parent path disappeared",ErrorCode::Conflict);
      const auto heldParent=stateStamp(slot.parent.get());
      require(heldParent&&sameStateIdentity(*heldParent,*parent),"Managed parent identity changed",ErrorCode::Conflict);
      const auto openedEntry=stateStamp(slot.data.get());
      const auto namedEntry=stateStampAt(slot.parent.get(),slot.actual.filename());
      const bool openedKnown=openedEntry.has_value();
      const bool namedKnown=openedKnown&&namedEntry&&*namedEntry;
      const auto opened=openedEntry.value_or(StateStamp{});
      const auto named=namedKnown?**namedEntry:StateStamp{};
      const bool unchanged=namedKnown
        &&sameFile(slot.identity,opened)&&sameFile(opened,named)&&opened.owned&&opened.size>=0&&static_cast<std::uint64_t>(opened.size)<=take(storage::originalMaximumPhysicalSize(slot.geometry,slot.kind))
        &&(owner.mapped||!complete||opened.size==slot.bytes)
        &&(retainedPieces.empty()||(sameExistingSnapshot(slot.identity,opened)&&sameExistingSnapshot(slot.identity,named)))
        &&(owner.purpose==ManagedStoragePurpose::Download||(sameExistingSnapshot(slot.identity,opened)&&sameExistingSnapshot(slot.identity,named)));
      if(!unchanged){
        // Diagnose the failed predicate without another filesystem read or any
        // relaxation of the original named/held-file and snapshot checks.
        const auto snapshotCause=[&](){
          const auto& observed=sameExistingSnapshot(slot.identity,opened)?named:opened;
          if(slot.identity.mode!=observed.mode){return "permissions";}
          if(slot.identity.owner!=observed.owner||slot.identity.owned!=observed.owned){return "owner";}
          if(slot.identity.links!=observed.links){return "links";}
          if(slot.identity.size!=observed.size){return "length";}
          if(!sameStateTimestamp(slot.identity.modified,observed.modified)){return "mtime";}
          if(!sameStateTimestamp(slot.identity.changed,observed.changed)){return "ctime";}
          return "identity";
        };
        std::string cause;
        if(!openedKnown){cause="descriptor-status";}
        else if(!namedKnown){cause="named-status";}
        else if(!sameFile(slot.identity,opened)){cause="descriptor-identity";}
        else if(!sameFile(opened,named)){cause="named-identity";}
        else if(!opened.owned){cause="owner";}
        else if(opened.size<0){cause="negative-size";}
        else if(static_cast<std::uint64_t>(opened.size)>take(storage::originalMaximumPhysicalSize(slot.geometry,slot.kind))){cause="physical-size";}
        else if(!owner.mapped&&complete&&opened.size!=slot.bytes){cause="completed-size";}
        else if(!retainedPieces.empty()&&(!sameExistingSnapshot(slot.identity,opened)||!sameExistingSnapshot(slot.identity,named))){cause=std::string("retained-resume-")+snapshotCause();}
        else{cause=std::string("read-only-")+snapshotCause();}
        const auto key=owner.hash.hex();
        runtimeConsole("storage.checkpoint.conflict")<<" key="<<key<<" file="<<slot.index<<" cause="<<cause<<'\n'<<std::flush;
        throw Failure{{ErrorCode::Conflict,"Managed payload identity/size changed (torrent="+key+", file="+std::to_string(slot.index)+", cause="+cause+")"}};
      }
      require(owner.mapped||verifiedEnds[slot.index]<=opened.size,"Verified checkpoint exceeds actual file extent",ErrorCode::Conflict);
      allocated=allocated&&opened.size==slot.bytes;
      if(owner.purpose==ManagedStoragePurpose::Download){done(syncStateFile(slot.data.get()));}
      const auto& modified = opened.modified;
      if(latestPayload&&std::tie(latestPayload->tv_sec,latestPayload->tv_nsec)<std::tie(modified.tv_sec,modified.tv_nsec)){
        latestPayload=modified;
      }
    }
    put(entry,"downloaded",intValue(previousReceived+received)); put(entry,"uploaded",intValue(previousSent+sent)); if(!preservePendingProgress){put(entry,"completedbytes",intValue(total));}
    if(badPayloadBytes){
      if(badPayloadBytes->discarded){put(entry,"discarded",intValue(owner.queue.discarded+badPayloadBytes->discarded));}
      if(badPayloadBytes->hashFailed){put(entry,"hashfailbytes",intValue(owner.queue.hashFailed+badPayloadBytes->hashFailed));}
    }
    if((!badPayloadBytes||!badPayloadComplete)&&received>0){
      // Preserve previously observed totals; unavailable tails cannot later be
      // mistaken for a known0 admission when reopened with a newer provider.
      put(entry,"BiglyBTC.badPayloadCountersLost","i1e");
    }

    if (secondsDownloading) { put(entry,"secondsDownloading",intValue(owner.queue.secondsDownloading+secondsDownloading)); }
    if (secondsSeeding) { put(entry,"secondsOnlySeeding",intValue(owner.queue.secondsSeeding+secondsSeeding)); }
    put(entry,"allocated",allocated?"i1e":"i0e");
    // The fresh root retains all current user command fields, policy and pauses.
    if(!batch){
      auto& inventory=*individual;
      if (current.legacyKey) { put(inventory.document(),*current.legacyKey,entry.serialize()); }
      else {
        const auto* entries=inventory.document().find("downloads"); std::string rows="l";
        for (std::size_t index=0;index<entries->children.size();++index) { rows+=index==current.sourceIndex?entry.serialize():encoded(entries->children[index]); }
        put(inventory.document(),"downloads",rows+'e');
      }
    }
    owner.attempted=true;
    if(secondsSinceUpload){
      auto attributes=dictionary(owner.state.document().find("attributes"));
      put(attributes,"timesinceul",intValue(*secondsSinceUpload));
      done(owner.state.replaceSection("attributes",attributes.serialize()));
    }
    if (lastActiveTagMillis && *lastActiveTagMillis > 0
        && owner.purpose == ManagedStoragePurpose::Download) {
      namespace tag = core::tags::download_state;
      if (const auto saved = tag::lastActiveStamp(owner.state.document());
          saved && *saved != *lastActiveTagMillis) {
        auto attributes = dictionary(owner.state.document().find("attributes"));
        put(attributes, tag::LastActiveAttribute, intValue(*lastActiveTagMillis));
        done(owner.state.replaceSection("attributes", attributes.serialize()));
      }
    }
    // A real accepted checked-state event supplies its retained time and state.
    // Prepare the metadata in a local dictionary; malformed auxiliary fields
    // do not turn a real payload checkpoint into an invented rollback/failure.
    owner.historyRecorded=false;owner.historyProblem.reset();owner.historyBytes.reset();
    if(historyEvent&&owner.purpose==ManagedStoragePurpose::Download){
      try{
        require(historyEvent->wallMillis>0,"Invalid actual completion wall time",ErrorCode::InvalidFormat);
        require(owner.layout.bytes>0&&std::ranges::none_of(owner.filePolicies,[](const auto& value){return value.skipped;}),"Selected/DND completion producer is not qualified",ErrorCode::Unsupported);
        const auto unique=[](const Node& map,std::string_view key){std::size_t count=0;require(map.type==Node::Type::Dictionary,"Malformed original completion dictionary",ErrorCode::InvalidFormat);for(std::size_t n=0;n+1<map.children.size();n+=2){if(map.children[n].bytes==key){++count;}}require(count<=1,"Duplicate original completion producer field",ErrorCode::InvalidFormat);};
        unique(owner.state.document().root(),"attributes");auto attributes=dictionary(owner.state.document().find("attributes"));unique(attributes.root(),"complt");unique(attributes.root(),"parameters");const auto* oldLast=attributes.find("complt");
        require(!oldLast||oldLast->type==Node::Type::Integer,"Unknown original completion attribute",ErrorCode::InvalidFormat);
        // Apply the genuine accepted false callback before any later true one.
        // The boolean is private per-admission owner evidence, never a UI flag.
        if(historyEvent->checkedIncompleteObserved){put(attributes,"complt",intValue(-1));}
        if(historyEvent->complete){
          const auto* negative=attributes.find("complt");require(negative&&negative->type==Node::Type::Integer&&negative->integer==-1,"Original negative completion producer is required",ErrorCode::Unsupported);
          auto parameters=dictionary(attributes.find("parameters"));unique(parameters.root(),"stats.download.completed.time");const auto* first=parameters.find("stats.download.completed.time");
          require(!first||first->type==Node::Type::Integer,"Unknown original completed parameter",ErrorCode::InvalidFormat);
          if(!first||first->integer<=0){put(parameters,"stats.download.completed.time",intValue(historyEvent->wallMillis));}
          put(attributes,"parameters",parameters.serialize());put(attributes,"complt",intValue(historyEvent->wallMillis));
        }else{require(historyEvent->checkedIncompleteObserved,"Incomplete history needs a genuine checked event",ErrorCode::Conflict);}
        done(owner.state.replaceSection("attributes",attributes.serialize()));owner.historyRecorded=true;
      }catch(const Failure& f){owner.historyProblem=f.error.code;}
      catch(...){owner.historyProblem=ErrorCode::Io;}
    }
    if(!preservePendingProgress){writeProgress(owner.state,counts,verified,complete,mapClassified||retainedOriginalClassification);}
    else if(owner.pendingOriginalResume){
      if(retainPendingMap){done(owner.state.replaceSection("resume",*owner.pendingOriginalResume));}
      else{auto resume=dictionary(owner.state.document().find("resume"));auto data=dictionary(resume.find("data"));put(data,"valid","i0e");put(data,"resume data",stringValue(std::string(verified.size(),'\2')));put(data,"blocks","de");put(resume,"data",data.serialize());done(owner.state.replaceSection("resume",resume.serialize()));}
    }
    if(retainedOriginalClassification&&!complete&&owner.importedOriginalBlocks){auto resume=dictionary(owner.state.document().find("resume"));auto data=dictionary(resume.find("data"));put(data,"blocks",*owner.importedOriginalBlocks);put(resume,"data",data.serialize());done(owner.state.replaceSection("resume",resume.serialize()));}
    if(owner.mergeDelta){auto attributes=dictionary(owner.state.document().find("attributes"));const auto current=mergedBytes();require(current.has_value(),"Original merged-data counter is invalid",ErrorCode::Conflict);put(attributes,"mergedata",intValue(*current));done(owner.state.replaceSection("attributes",attributes.serialize()));owner.mergeDelta=0;}
    if(!batch){done(owner.store.save(lease,owner.state,nullptr,"managed-checkpoint"));}
    const bool completedFiles=!preservePendingProgress&&(!owner.pending.empty()||std::ranges::any_of(owner.slots,[&](const auto& slot){return !slot.padding&&counts[slot.index]==slot.bytes;}));
    if (completedFiles) {
      auto attributes=dictionary(owner.state.document().find("attributes")); const auto suffix=attributes.find("incompfilesuffix")?text(attributes.find("incompfilesuffix")):std::string{};
      const auto prefix=attributes.find("dnd_pfx")?text(attributes.find("dnd_pfx")):std::string{};
      std::set<std::size_t> known; for (const auto& item:owner.pending) { known.insert(item.index); }
      for (const auto& slot:owner.slots) {
        if (slot.padding||counts[slot.index]!=slot.bytes||known.contains(slot.index)) { continue; } const auto name=pathToUtf8(slot.actual.filename());
        if (!suffix.empty()&&name.size()>suffix.size()&&name.ends_with(suffix)) {
          auto newName=name.substr(0,name.size()-suffix.size());
          if(!slot.skipped&&!prefix.empty()&&newName.starts_with(prefix)){newName.erase(0,prefix.size());}
          auto final=slot.actual; final.replace_filename(pathFromUtf8(newName));
          require(!newName.empty()&&safeManagedPhysical(final)&&!reservedComponent(final.filename()),"Unsafe completed filename",ErrorCode::UnsafePath);
          require(slot.identity.device<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())&&slot.identity.inode<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),"Payload identity exceeds completion journal range");
          owner.pending.push_back({slot.index,slot.actual,final,slot.identity.device,slot.identity.inode});
        }
      }
      if (!owner.pending.empty()) { saveJournal(owner.state,owner.store,lease,owner.layout.multifile,owner.pending); }
      for (auto& item:owner.pending) {
        auto& slot=owner.slots[item.index];
        if (slot.actual==item.target) { item.linked=true; }
        else {
          const auto linked=storage::detail::linkPayloadName(slot.parent.get(),slot.data.get(),slot.actual.filename(),item.target.filename());
          if(linked){item.linked=true;done(syncStateDirectory(slot.parent.get()));}
          else if(linked.error().code==ErrorCode::Conflict){
            const auto other=stateStampAt(slot.parent.get(),item.target.filename());
            item.linked=other&&*other&&sameFile(slot.identity,**other);
          }else{throw Failure{{ErrorCode::Io,"Managed completion linking failed; source names and journal retained"}};}
        }
        if (item.linked) { slot.final=item.target; }
      }
      // Preparation already persisted unchanged links. Avoid another full
      // state backup/flush/fsync transaction unless a completion changed them.
      if(writeLinks(owner.state,owner.slots,owner.save,true)&&!batch){done(owner.store.save(lease,owner.state));}
    }
    if(batch){done(batch->stage(current,entry.serialize(),{&owner.store,&owner.state,latestPayload}));}else{done(individual->save(lease));}
    if (completedFiles) {
      for (const auto& item:owner.pending) {
        if (!item.linked) { continue; }
        const auto& slot=owner.slots[item.index];
        const auto old=take(stateStampAt(slot.parent.get(),item.source.filename()));
        if(old){
          require(old->regular&&old->device==item.device&&old->inode==item.inode,"Managed completion source alias changed; retained",ErrorCode::Conflict);
          done(storage::detail::removePayloadName(slot.parent.get(),item.source.filename(),*old));
          done(syncStateDirectory(slot.parent.get()));
        }
      }
      if (!owner.pending.empty()) { saveJournal(owner.state,owner.store,lease,owner.layout.multifile,{}); }
    }
    owner.historyBytes=total;return {};
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io,"Managed checkpoint failed; original state or inventory may already have changed"}); }
}
} // namespace biglybtc::engine::detail
