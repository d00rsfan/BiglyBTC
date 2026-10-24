#include "managed_storage.hpp"
#include "local_support.hpp"
#include "network_policy.hpp"
#include "torrent_state.hpp"
#include "encoded_document.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/torrent_info.hpp>
#include <algorithm>
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
  Descriptor parent{-1}, data{-1};
  struct stat identity{};
};
bool safeManagedRelative(const fs::path& path) {
  if (path.empty() || path.is_absolute() || path.lexically_normal() != path || path.native().size()>4096
      || path.native().find('\n')!=std::string::npos || path.native().find('\0')!=std::string::npos) { return false; }
  for (const auto& part : path) { if (part.empty() || part=="." || part==".." || part.native().size()>255) { return false; } }
  return true;
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
  require(version==(multifile?2:1),"Unsupported completion journal version");
  std::vector<Journal> result;
  const auto append=[&](const Node& item,std::size_t index) {
    const auto device=integer(item.find("device"),-1),inode=integer(item.find("inode"),-1);
    require(index<files&&device>=0&&inode>=0,"Invalid completion journal identity",ErrorCode::InvalidFormat);
    Journal entry{index,fs::path(text(item.find("source"))),fs::path(text(item.find("target"))),
      static_cast<std::uint64_t>(device),static_cast<std::uint64_t>(inode)};
    require(safeManagedRelative(entry.source)&&safeManagedRelative(entry.target)&&entry.source!=entry.target
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
    if (multifile) {
      put(value,"version","i2e"); std::string list="l"; for (const auto& item:pending) { list+=record(item,true); } put(value,"files",list+'e');
    } else {
      require(pending.size()==1&&pending[0].index==0,"Invalid single-file completion journal",ErrorCode::InvalidFormat);
      value=take(Document::parse(record(pending[0],false),storage::compatibleConfigLimits()).transform_error([](const auto&) { return Error{ErrorCode::InvalidFormat,"Invalid completion journal encoding"}; }));
      put(value,"version","i1e");
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
bool inspect(Slot& slot, const fs::path& save, const Journal* journal, bool open) {
  slot.parent=parentDirectory((save/slot.actual).parent_path(),false);
  if (slot.parent.get()<0) { return false; }
  struct stat named{};
  if (::fstatat(slot.parent.get(),slot.actual.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0) {
    require(errno==ENOENT,"Cannot inspect managed payload",ErrorCode::UnsafePath);
    struct stat parent{}; require(::fstat(slot.parent.get(),&parent)==0&&parent.st_uid==::geteuid(),"Missing managed file requires an owned parent",ErrorCode::UnsafePath);
    return false;
  }
  require(S_ISREG(named.st_mode)&&named.st_uid==::geteuid()&&named.st_size>=0&&named.st_size<=slot.bytes
      &&named.st_nlink>=1&&named.st_nlink<=2,"Unsafe managed payload type, ownership, size or links",ErrorCode::UnsafePath);
  if (slot.identity.st_ino) { require(sameFile(slot.identity,named),"Managed payload identity changed after preflight",ErrorCode::Conflict); }
  if (journal) {
    require((slot.actual==journal->source||slot.actual==journal->target)&&named.st_dev==journal->device&&named.st_ino==journal->inode,"Completion journal differs from payload",ErrorCode::Conflict);
  }
  if (named.st_nlink==2) {
    require(journal!=nullptr,"Unrecognized managed payload hard link",ErrorCode::UnsafePath);
    const auto alias=slot.actual==journal->source?journal->target:journal->source; struct stat other{};
    require(::fstatat(slot.parent.get(),alias.filename().c_str(),&other,AT_SYMLINK_NOFOLLOW)==0&&sameFile(named,other),"Unrecognized completion alias",ErrorCode::UnsafePath);
  }
  if (open) {
    slot.data=Descriptor(::openat(slot.parent.get(),slot.actual.filename().c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    require(slot.data.get()>=0&&::fstat(slot.data.get(),&slot.identity)==0&&sameFile(named,slot.identity),"Managed payload changed while opening",ErrorCode::Conflict);
  } else { slot.identity=named; }
  return true;
}
std::vector<Slot> plan(const core::SavedDownload& saved, const Node& attributes, const compat::OriginalLayout& layout,
    const std::vector<compat::FilePolicy>& policy, const fs::path& save, const std::vector<Journal>& pending) {
  require(!saved.legacySavePath&&saved.saveFile&&safeManagedRelative(fs::path(*saved.saveFile))&&!fs::path(*saved.saveFile).has_parent_path(),"Unsupported original save-root mapping",ErrorCode::UnsafePath);
  require(!attributes.find("filelinks"),"Legacy file links require their qualified mapping adapter");
  if (const auto* prefix=attributes.find("dnd_pfx")) { require(text(prefix).empty(),"DND prefix relocation requires its mapping adapter"); }
  const auto* stores=attributes.find("storetypes");
  require(stores&&stores->type==Node::Type::List&&stores->children.size()==layout.files.size(),"Storage-type count differs from metadata");
  std::vector<Slot> result; result.reserve(layout.files.size());
  for (std::size_t index=0;index<layout.files.size();++index) {
    require(text(&stores->children[index])=="L","Compact or reordered storage requires its disk adapter");
    const auto& file=layout.files[index]; const auto base=layout.multifile?fs::path(*saved.saveFile)/file.path:fs::path(*saved.saveFile);
    require(safeManagedRelative(base),"Unsafe managed payload path",ErrorCode::UnsafePath);
    result.push_back({index,base,base,base,file.bytes,file.padding,policy[index].skipped});
  }
  std::set<std::size_t> linked;
  if (const auto* links=attributes.find("filelinks2")) {
    require(links->type==Node::Type::List&&links->children.size()<=layout.files.size()*4,"File-link list exceeds its bound");
    for (const auto& item:links->children) {
      const auto link=text(&item); const auto first=link.find('\n'),second=first==std::string::npos?first:link.find('\n',first+1);
      require(first!=std::string::npos&&second!=std::string::npos&&link.find('\n',second+1)==std::string::npos,"Malformed file link");
      std::size_t index=0; const auto parsed=std::from_chars(link.data(),link.data()+first,index);
      require(parsed.ec==std::errc{}&&parsed.ptr==link.data()+first&&index<result.size(),"Invalid file-link index");
      auto& slot=result[index]; const auto from=link.substr(first+1,second-first-1),to=link.substr(second+1);
      require(from.empty()||fs::path(from)==save/slot.base,"File-link source differs from saved path");
      if (to.empty()||to==from) { slot.actual=slot.base; linked.erase(index); continue; }
      const fs::path target(to);
      require(target.is_absolute()&&target.lexically_normal()==target&&target.parent_path()==(save/slot.base).parent_path(),"External file link is unsupported",ErrorCode::UnsafePath);
      slot.actual=target.lexically_relative(save); require(safeManagedRelative(slot.actual),"Unsafe linked payload path",ErrorCode::UnsafePath); linked.insert(index);
    }
  }
  const auto suffix=attributes.find("incompfilesuffix")?text(attributes.find("incompfilesuffix")):std::string{};
  require(suffix.find('/')==std::string::npos&&suffix.find('\n')==std::string::npos,"Unsafe incomplete suffix",ErrorCode::UnsafePath);
  std::map<std::size_t,const Journal*> byIndex; for (const auto& item:pending) { byIndex.emplace(item.index,&item); }
  std::set<fs::path> physical; std::set<std::pair<std::uint64_t,std::uint64_t>> inodes;
  for (auto& slot:result) {
    if (slot.padding) { require(!byIndex.contains(slot.index),"Padding cannot own completion journal"); continue; }
    const auto found=byIndex.find(slot.index); const auto* journal=found==byIndex.end()?nullptr:found->second;
    require(!journal||slot.actual==journal->source||slot.actual==journal->target,"Journal path differs from mapping",ErrorCode::Conflict);
    if (!inspect(slot,save,journal,true)) {
      if (journal) {
        slot.actual=slot.actual==journal->source?journal->target:journal->source;
        require(inspect(slot,save,journal,true),"Both completion journal paths are absent",ErrorCode::Conflict);
      } else if (!linked.contains(slot.index)&&!suffix.empty()) {
        slot.actual+=suffix; require(safeManagedRelative(slot.actual),"Unsafe incomplete filename",ErrorCode::UnsafePath); inspect(slot,save,nullptr,true);
      }
    }
    slot.final=slot.actual; requireUtf8Filesystem(save/slot.actual);
    require(physical.insert(slot.actual).second,"Two files alias the same managed target",ErrorCode::UnsafePath);
    if (slot.identity.st_ino) { require(inodes.emplace(slot.identity.st_dev,slot.identity.st_ino).second,"Two files alias the same managed inode",ErrorCode::UnsafePath); }
  }
  for (const auto& path:physical) {
    auto parent=path.parent_path(); while (!parent.empty()) { require(!physical.contains(parent),"Managed file aliases another file's directory",ErrorCode::UnsafePath); parent=parent.parent_path(); }
  }
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
std::shared_ptr<lt::torrent_info> metadata(const Document& state, const core::TorrentHash& hash, const compat::OriginalLayout& layout) {
  const auto value=state.serialize(); lt::error_code error;
  auto info=std::make_shared<lt::torrent_info>(lt::span<const char>(value.data(),value.size()),error,lt::from_span);
  require(!error&&info->num_files()>0&&info->num_files()<=100000&&info->num_pieces()>0&&info->num_pieces()<=1000000,"Invalid or unbounded managed metadata",ErrorCode::InvalidFormat);
  const auto key=hash.bytes(); const auto hashes=info->info_hashes();
  const bool original20=key.size()==20&&((hashes.has_v1()&&std::memcmp(key.data(),hashes.v1.data(),20)==0)
      ||(!hashes.has_v1()&&hashes.has_v2()&&std::memcmp(key.data(),hashes.v2.data(),20)==0));
  require(original20||(key.size()==32&&hashes.has_v2()&&std::memcmp(key.data(),hashes.v2.data(),32)==0),
      "Managed metadata hash mismatch",ErrorCode::InvalidFormat);
  require(layout.files.size()==static_cast<std::size_t>(info->num_files())&&layout.bytes==info->total_size()&&layout.pieceLength==info->piece_length(),"Original/engine managed geometry differs");
  for (std::size_t index=0;index<layout.files.size();++index) {
    const auto file=lt::file_index_t{static_cast<int>(index)}; const auto& original=layout.files[index];
    require(original.bytes==info->files().file_size(file)&&original.offset==info->files().file_offset(file)&&original.padding==info->files().pad_file_at(file),"Original/engine file indices differ");
    require(original.padding||fs::path(info->files().file_path(file))==(layout.multifile?fs::path(layout.name)/original.path:fs::path(original.path)),"Original/engine filename codecs differ");
  }
  return info; // Keep original trackers/web seeds for the explicit policy filter.
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
  core::SavedDownload captured;
  ManagedQueueMetadata queue;
  storage::TorrentStateStore store;
  storage::TorrentStateFile state;
  compat::OriginalLayout layout;
  lt::add_torrent_params params;
  fs::path save;
  std::vector<Slot> slots;
  std::vector<Journal> pending;
  std::uint64_t profileDevice,profileInode;
  bool attempted=false;
  Impl(core::TorrentHash key,core::SavedDownload selected,storage::TorrentStateStore owner,storage::TorrentStateFile source,
      compat::OriginalLayout geometry,lt::add_torrent_params parameters,fs::path root,std::vector<Slot> mapping,std::vector<Journal> journal,
      std::uint64_t device,std::uint64_t inode)
    : hash(std::move(key)),captured(std::move(selected)),store(std::move(owner)),state(std::move(source)),layout(std::move(geometry)),
      params(std::move(parameters)),save(std::move(root)),slots(std::move(mapping)),pending(std::move(journal)),profileDevice(device),profileInode(inode) {}
};
ManagedStorage::ManagedStorage(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
ManagedStorage::ManagedStorage(ManagedStorage&&) noexcept=default;
ManagedStorage& ManagedStorage::operator=(ManagedStorage&&) noexcept=default;
ManagedStorage::~ManagedStorage()=default;
lt::add_torrent_params ManagedStorage::parameters() const { return impl_?impl_->params:lt::add_torrent_params{}; }
const ManagedQueueMetadata& ManagedStorage::queueMetadata() const { return impl_->queue; }

std::expected<ManagedStorage,Error> ManagedStorage::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,const NetworkPolicy& policy) {
  try {
    struct stat profile{}; require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0,"Managed storage requires a valid held lease",ErrorCode::UnsafePath);
    auto inventory=take(storage::ConfigFile::load(lease,"downloads.config"));
    require(inventory.recoverySource()!=storage::RecoverySource::Saving,"Resolve saved-inventory recovery before managed preparation",ErrorCode::Conflict);
    const auto saved=selected(inventory.document(),hash);
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
    if (const auto* attributes=source.find("attributes")) {
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
    if (const auto* encoding=source.find("encoding")) {
      auto name=text(encoding); std::erase_if(name,[](unsigned char c) { return c=='-'||c=='_'||c==' '; });
      std::ranges::transform(name,name.begin(),[](unsigned char c) { return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c); });
      require(name=="utf8"||name=="utf8keys","Legacy filename encoding requires its codec adapter");
    }
    const auto geometry=compat::originalLayout(source);
    require(geometry.has_value(),"Invalid original managed geometry",ErrorCode::InvalidFormat); auto layout=*geometry;
    auto info=metadata(source,hash,layout); auto attributes=dictionary(source.find("attributes"));
    const auto parsedPolicy=compat::originalFilePolicy(row,layout.files.size()); require(parsedPolicy.has_value(),"Invalid original managed file priorities",ErrorCode::InvalidFormat);
    auto pending=journals(attributes.root(),layout.multifile,layout.files.size()); const fs::path save(saved.saveDirectory);
    require(save.is_absolute()&&save.lexically_normal()==save,"Managed save directory must be absolute and normalized",ErrorCode::UnsafePath);
    auto slots=plan(saved,attributes.root(),layout,*parsedPolicy,save,pending);
    struct rlimit limit{}; std::size_t budget=8192;
    if (::getrlimit(RLIMIT_NOFILE,&limit)==0&&limit.rlim_cur!=RLIM_INFINITY) { budget=std::min(budget,static_cast<std::size_t>(limit.rlim_cur/4)); }
    const auto files=static_cast<std::size_t>(std::ranges::count_if(slots,[](const auto& slot) { return !slot.padding; }));
    require(files<=budget,"Managed payload exceeds descriptor budget");
    lt::add_torrent_params params; params.ti=info; params.save_path=save.string(); params.file_priorities=priorities(*parsedPolicy,layout);
    for (auto [key,target]:{std::pair{"maxdl",&params.download_limit},{"maxul",&params.upload_limit}}) {
      const auto value=integer(row.find(key)); require(value>=0&&value<=std::numeric_limits<int>::max(),"Unsupported disabled/overflowed per-torrent rate limit");
      *target=value?static_cast<int>(value):-1;
    }
    params.flags&=~lt::torrent_flags::auto_managed; params.flags|=lt::torrent_flags::paused;
    auto filtered=applyTorrentNetworkPolicy(policy,networks(source,attributes.root()),params);
    require(filtered.has_value(),"Managed torrent network selection is unsupported by the owner policy");
    // All unsupported geometry/mapping/policy decisions precede state ownership
    // and payload mutation. beginUpdate hydrates the same last cached sections.
    auto state=take(store.beginUpdate(lease,hash)); auto actual=dictionary(state.document().find("attributes"));
    {
      auto check=plan(saved,actual.root(),layout,*parsedPolicy,save,journals(actual.root(),layout.multifile,layout.files.size()));
      require(check.size()==slots.size(),"Managed mapping changed during preparation",ErrorCode::Conflict);
      for (std::size_t index=0;index<slots.size();++index) {
        require(check[index].actual==slots[index].actual,"Authoritative managed mapping changed",ErrorCode::Conflict);
        require((check[index].identity.st_ino==0)==(slots[index].identity.st_ino==0)
          &&(!slots[index].identity.st_ino||sameFile(check[index].identity,slots[index].identity)),"Managed payload changed during preparation",ErrorCode::Conflict);
      }
    }
    // Cache values are copied before their backing state is relinquished.
    auto rechecked=applyTorrentNetworkPolicy(policy,networks(state.document(),actual.root()),params);
    require(rechecked.has_value(),"Authoritative torrent network policy changed",ErrorCode::Conflict);
    writeLinks(state,slots,save,false); done(store.save(lease,state));
    for (auto& slot:slots) {
      if (slot.padding) { continue; }
      const auto journal=std::find_if(pending.begin(),pending.end(),[&](const auto& value) { return value.index==slot.index; });
      const auto* intent=journal==pending.end()?nullptr:&*journal;
      if (!inspect(slot,save,intent,true)) {
        slot.parent=parentDirectory((save/slot.actual).parent_path(),true);
        slot.data=Descriptor(::openat(slot.parent.get(),slot.actual.filename().c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));
        require(slot.data.get()>=0&&::fstat(slot.data.get(),&slot.identity)==0&&S_ISREG(slot.identity.st_mode)
            &&slot.identity.st_uid==::geteuid()&&slot.identity.st_nlink==1,"Cannot exclusively create managed payload",ErrorCode::Conflict);
        sync(slot.data.get()); sync(slot.parent.get());
      }
      // LT mmap storage uses partfiles for missing/empty skipped files. Sparse
      // extension keeps skipped boundary-piece bytes in the original linear file.
      if (slot.skipped&&slot.bytes>slot.identity.st_size) {
        require(::ftruncate(slot.data.get(),slot.bytes)==0,"Cannot prepare skipped linear file",ErrorCode::Io); sync(slot.data.get());
      }
      params.renamed_files[lt::file_index_t{static_cast<int>(slot.index)}]=slot.actual.string();
    }
    auto result=std::make_unique<Impl>(hash,saved,std::move(store),std::move(state),std::move(layout),std::move(params),save,
      std::move(slots),std::move(pending),profile.st_dev,profile.st_ino);
    result->queue=queue;
    return ManagedStorage(std::move(result));
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io,"Managed storage preparation failed; prepared state may require recheck"}); }
}

std::expected<void,Error> ManagedStorage::checkpoint(const storage::ProfileLease& lease,std::span<const std::uint8_t> verified,
    bool complete,std::int64_t received,std::int64_t sent,
    std::int64_t secondsDownloading,std::int64_t secondsSeeding) {
  try {
    require(impl_!=nullptr,"Managed storage was moved or released",ErrorCode::Conflict); auto& owner=*impl_;
    require(!owner.attempted,"Managed admission checkpoint already attempted",ErrorCode::Conflict);
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
    bool allocated=true;
    for (auto& slot:owner.slots) {
      if (slot.padding) { continue; } struct stat named{},opened{},parent{};
      auto actualParent=parentDirectory((owner.save/slot.actual).parent_path(),false);
      require(actualParent.get()>=0&&::fstat(actualParent.get(),&parent)==0,"Managed parent path disappeared",ErrorCode::Conflict);
      struct stat heldParent{}; require(::fstat(slot.parent.get(),&heldParent)==0&&heldParent.st_dev==parent.st_dev&&heldParent.st_ino==parent.st_ino,"Managed parent identity changed",ErrorCode::Conflict);
      require(::fstat(slot.data.get(),&opened)==0&&::fstatat(slot.parent.get(),slot.actual.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
        &&sameFile(slot.identity,opened)&&sameFile(opened,named)&&opened.st_uid==::geteuid()&&opened.st_size>=0&&opened.st_size<=slot.bytes
        &&(!complete||opened.st_size==slot.bytes),"Managed payload identity/size changed",ErrorCode::Conflict);
      require(verifiedEnds[slot.index]<=opened.st_size,"Verified checkpoint exceeds actual file extent",ErrorCode::Conflict);
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
    writeProgress(owner.state,counts,verified.size(),complete); done(owner.store.save(lease,owner.state));
    if (complete) {
      auto attributes=dictionary(owner.state.document().find("attributes")); const auto suffix=attributes.find("incompfilesuffix")?text(attributes.find("incompfilesuffix")):std::string{};
      std::set<std::size_t> known; for (const auto& item:owner.pending) { known.insert(item.index); }
      for (const auto& slot:owner.slots) {
        if (slot.padding||known.contains(slot.index)) { continue; } const auto name=slot.actual.filename().string();
        if (!suffix.empty()&&name.size()>suffix.size()&&name.ends_with(suffix)) {
          auto final=slot.actual; final.replace_filename(name.substr(0,name.size()-suffix.size()));
          require(safeManagedRelative(final),"Unsafe completed filename",ErrorCode::UnsafePath);
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
    if (complete) {
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
