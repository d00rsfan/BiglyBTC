#include "checkpoint_batch.hpp"
#include "biglybtc/console_history.hpp"

#include <atomic>
#include <thread>
#include <algorithm>
#include <array>
#include <new>
#include <map>
#include <set>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <cstdlib>
#include "state_cache_materialization.hpp"
#include <tuple>
#include <utility>

namespace biglybtc::storage {
namespace {
using detail::StateDescriptor;
using detail::StateHandle;
using detail::StateStamp;
using detail::InvalidStateHandle;
using detail::openStateDirectory;
using detail::openStateFile;
using detail::publishStateFile;
using detail::readStateAt;
using detail::readStateFile;
using detail::sameStateIdentity;
using detail::sameStateSnapshot;
using detail::sameStateTimestamp;
using detail::stageStateFile;
using detail::stateStamp;
using detail::stateStampAt;
using detail::stateWriteMode;
using detail::syncStateDirectory;
using detail::syncStateFile;
std::unexpected<Error> error(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}
std::expected<void, Error> regularOrMissing(StateHandle directory, const std::string& name) {
  const auto info = stateStampAt(directory, name);
  if (!info) { return std::unexpected(info.error()); }
  if (*info && !(*info)->regular) {
    return error(ErrorCode::UnsafePath, "Saved-state entry must be a regular file");
  }
  return {};
}

std::expected<void, Error> preserveTemporary(StateHandle directory, const std::string& name) {
  const auto info = stateStampAt(directory, name);
  if (!info) { return std::unexpected(info.error()); }
  if (!*info) { return {}; }
  if (!(*info)->regular) {
    return error(ErrorCode::UnsafePath, "Saved-state entry must be a regular file");
  }
  const auto retained = name + ".bak";
  auto safe = regularOrMissing(directory, retained);
  if (!safe) { return safe; }
  auto published = publishStateFile(directory, name, retained);
  if (!published) { return published; }
  return syncStateDirectory(directory);
}

unsigned writeMode(StateHandle directory, const std::string& name) {
  const auto info = stateStampAt(directory, name);
  return info && *info ? stateWriteMode(**info) : 0600;
}

std::expected<void, Error> recheck(EncodedDocument& envelope) {
  const auto limits = compatibleConfigLimits();
  const auto* previous = envelope.document().find("resume");
  auto bytes = previous ? core::encodeValue(*previous, limits) : std::expected<std::string, core::ParseError>{"de"};
  if (!bytes) { return error(ErrorCode::InvalidFormat, "Cannot encode original resume section"); }
  auto resume = core::Document::parse(std::move(*bytes), limits);
  if (!resume || resume->root().type != core::Node::Type::Dictionary) {
    return error(ErrorCode::InvalidFormat, "Resume section is not a dictionary");
  }
  previous = resume->find("data");
  bytes = previous ? core::encodeValue(*previous, limits) : std::expected<std::string, core::ParseError>{"de"};
  if (!bytes) { return error(ErrorCode::InvalidFormat, "Cannot encode original resume data"); }
  auto data = core::Document::parse(std::move(*bytes), limits);
  if (!data || data->root().type != core::Node::Type::Dictionary) {
    return error(ErrorCode::InvalidFormat, "Resume data is not a dictionary");
  }
  if (const auto* pieces = data->find("resume data")) {
    if (pieces->type != core::Node::Type::Bytes) { return error(ErrorCode::InvalidFormat, "Invalid saved piece flags"); }
    const std::string flags(pieces->bytes.size(), '\2');
    if (!data->replaceValue("resume data", std::to_string(flags.size()) + ":" + flags)) {
      return error(ErrorCode::InvalidFormat, "Saved piece flags exceed edit limits");
    }
  }
  if (!data->replaceValue("valid", "i0e") || !data->replaceValue("blocks", "de")
      || !resume->replaceValue("data", data->serialize()) || !envelope.replaceValue("resume", resume->serialize())) {
    return error(ErrorCode::InvalidFormat, "Unable to mark saved state for recheck");
  }
  previous = envelope.document().find("attributes");
  bytes = previous ? core::encodeValue(*previous, limits) : std::expected<std::string, core::ParseError>{"de"};
  if (!bytes) { return error(ErrorCode::InvalidFormat, "Cannot encode saved attributes"); }
  auto attributes = core::Document::parse(std::move(*bytes), limits);
  if (!attributes || !attributes->replaceValue("resumecomplete", "i1e")
      || !envelope.replaceValue("attributes", attributes->serialize())) {
    return error(ErrorCode::InvalidFormat, "Unable to clear cached completion state");
  }
  return {};
}

std::expected<void, Error> replace(const ProfileLease& lease, StateHandle directory, const std::string& filename,
    const std::optional<std::string>& original, std::string_view encoded, std::string_view backup,
    void (*observer)(StateSaveStage), ProfileSourceAccess* access=nullptr, const timespec* retainMtime=nullptr) {
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  auto current = readStateFile(directory, filename, compatibleConfigLimits().inputBytes);
  if (!current) { return std::unexpected(current.error()); }
  if (*current != original) { return error(ErrorCode::Conflict, "Saved state changed since loading"); }
  const auto observe = [&](StateSaveStage stage) {
    if (access) { access->outside([&] { observer(stage); }); }
    else { observer(stage); }
  };
  const auto observed = [&](StateSaveStage stage) -> std::expected<void, Error> {
    if (!observer) { return {}; }
    observe(stage);
    const auto latest = readStateFile(directory, filename, compatibleConfigLimits().inputBytes);
    if (!latest) { return std::unexpected(latest.error()); }
    if (*latest != original) { return error(ErrorCode::Conflict, "Saved state changed at the publication boundary"); }
    return {};
  };
  auto safe = regularOrMissing(directory, filename + ".bak");
  if (!safe) { return safe; }
  safe = preserveTemporary(directory, filename + ".saving");
  if (!safe) { return safe; }
  safe = preserveTemporary(directory, filename + ".bak.saving");
  if (!safe) { return safe; }
  const auto mode = writeMode(directory, filename);
  const auto saving = filename + ".saving";
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  auto staged = stageStateFile(directory, saving, encoded, mode);
  if (!staged) { return std::unexpected(staged.error()); }
  if (retainMtime) {
    const auto held = stateStamp(staged->get());
    if (!held || !held->regular || !held->owned || held->links != 1) {
      return error(ErrorCode::Io, "Cannot retain original resume timestamp during cache hydration");
    }
    auto retained = detail::setStateMtime(staged->get(), *retainMtime);
    if (!retained) { return retained; }
  }
  auto synced = syncStateFile(staged->get());
  if (!synced) { return synced; }
  auto checked = observed(StateSaveStage::TemporarySynced);
  if (!checked) { return checked; }
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  const auto backupSaving = filename + ".bak.saving";
  auto stagedBackup = stageStateFile(directory, backupSaving, backup, mode);
  if (!stagedBackup) { return std::unexpected(stagedBackup.error()); }
  synced = syncStateFile(stagedBackup->get());
  if (!synced) { return synced; }
  auto published = publishStateFile(directory, backupSaving, filename + ".bak", stagedBackup->get());
  if (!published) { return published; }
  *stagedBackup = StateDescriptor{};
  safe = syncStateDirectory(directory);
  if (!safe) { return safe; }
  checked = observed(StateSaveStage::BackupSynced);
  if (!checked) { return checked; }
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  // Java can read the old or new primary throughout backup preparation.
  published = publishStateFile(directory, saving, filename, staged->get());
  if (!published) { return published; }
  *staged = StateDescriptor{};
  if (observer) { observe(StateSaveStage::PrimaryRenamed); }
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  return syncStateDirectory(directory);
}

struct CheckpointFile {
  StateDescriptor fd;
  bool existing = false;
  StateStamp original{};
};

std::expected<CheckpointFile,Error> prepareCheckpointFile(StateHandle directory,const std::string& name,
    std::string_view bytes,unsigned mode,std::optional<timespec> minimumTime) {
#ifndef _WIN32
  if (minimumTime) {
    auto fd = openStateFile(directory, name);
    const auto stamp = fd ? stateStamp(fd->get()) : std::expected<StateStamp, Error>{std::unexpected(fd.error())};
    if (stamp && stamp->regular && stamp->links == 1 && stamp->size >= 0
        && static_cast<std::uint64_t>(stamp->size) == bytes.size()
        && std::tie(stamp->modified.tv_sec, stamp->modified.tv_nsec)
            >= std::tie(minimumTime->tv_sec, minimumTime->tv_nsec)) {
      std::array<char,16384> block;
      std::size_t offset = 0;
      while (offset < bytes.size()) {
        const auto n = readStateAt(fd->get(), block.data(), std::min(block.size(), bytes.size() - offset), offset);
        if (n <= 0 || bytes.substr(offset, n) != std::string_view(block.data(), n)) { break; }
        offset += static_cast<std::size_t>(n);
      }
      if (offset == bytes.size()) { return CheckpointFile{std::move(*fd), true, *stamp}; }
    }
  }
#else
  // FlushFileBuffers needs write access. Use a fresh staged image instead of
  // reusing a read-only primary as one of the synchronized checkpoint files.
  (void)minimumTime;
#endif
  auto fd = stageStateFile(directory, name + ".saving", bytes, mode);
  if (!fd) { return std::unexpected(fd.error()); }
  return CheckpointFile{std::move(*fd), false, {}};
}
} // namespace

// Publish conservative backups before replacing any primary. All files are
// fsynced individually. ext4 can commit their data in the same transaction.
std::expected<void,Error> CheckpointBatch::save(const ProfileLease& lease,std::span<const CheckpointState> states) {
  if(states.empty()){return {};}
  if(states.size()>32){return error(ErrorCode::Conflict,"Checkpoint batch exceeds owner limit");}
  // Keep the additional encoded postimages bounded even when metadata is large.
  std::size_t bytes=0;
  std::set<std::string> unique;
  for(std::size_t i=0;i<states.size();++i){
    const auto& item=states[i];
    if(!item.store||!item.state||!unique.insert(item.state->hash_.hex()).second){
      return error(ErrorCode::Conflict,"Invalid or repeated checkpoint state");
    }
  }
  for(std::size_t i=0;i<states.size();++i){
    const auto n=states[i].state->document().storageBytes();
    constexpr std::size_t budget=16*1024*1024;
    if(i&&n>budget-std::min(bytes,budget)){
      auto first=save(lease,states.first(i));
      if(!first){return first;}
      return save(lease,states.subspan(i));
    }
    bytes+=n;
  }
  auto access=lease.sourceAccess(true);
  if(!access){return std::unexpected(access.error());}
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);
  if(!allowed){return allowed;}
  struct Pending {
    TorrentStateFile* state;
    std::string name,encoded;
    CheckpointFile primary,backup;
  };
  std::vector<Pending> pending;
  const auto& first=*states.front().store;
  const auto directory = first.active_.get();
  for(auto [store,state,latestPayload]:states){
    auto valid=store->validate(lease);
    if(!valid){return valid;}
    if(!state->prepared_||state->device_!=first.activeDevice_||state->inode_!=first.activeInode_
        ||store->activeDevice_!=first.activeDevice_||store->activeInode_!=first.activeInode_){
      return error(ErrorCode::Conflict,"Checkpoint state directory changed");
    }
    const auto name=state->hash_.hex()+".dat";
    auto current=readStateFile(directory,name,compatibleConfigLimits().inputBytes);
    if(!current){return std::unexpected(current.error());}
    if(*current!=state->original_){return error(ErrorCode::Conflict,"Checkpoint primary changed");}
    const auto safe=regularOrMissing(directory,name+".bak");
    if(!safe){return safe;}
    for(const auto suffix:{".saving",".bak.saving"}){
      auto retained=preserveTemporary(directory,name+suffix);
      if(!retained){return retained;}
    }
    const auto mode = writeMode(directory, name);
    auto encoded=state->document_.serialize();
    auto backup=state->document_;
    auto checked=recheck(backup);
    if(!checked){return checked;}
    auto safeBytes=backup.serialize();
    if(!encoded||!safeBytes){return error(ErrorCode::InvalidFormat,"Cannot encode checkpoint state");}
    auto primary=prepareCheckpointFile(directory,name,*encoded,mode,latestPayload);
    if(!primary){return std::unexpected(primary.error());}
    auto secondary=prepareCheckpointFile(directory,name+".bak",*safeBytes,mode,timespec{});
    if(!secondary){return std::unexpected(secondary.error());}
    pending.push_back({state,name,std::move(*encoded),std::move(*primary),std::move(*secondary)});
  }
  // Independent inodes can share an ext4 journal commit. Wait for every
  // file fence before publishing even the first conservative backup.
  std::atomic<std::size_t> next{0};
  std::atomic<bool> failed{false};
  const auto flush=[&]{
    for(;;){
      const auto i=next.fetch_add(1,std::memory_order_relaxed);
      if(i>=2*pending.size()){return;}
      const auto& file=pending[i/2];
      if(!syncStateFile(i%2?file.backup.fd.get():file.primary.fd.get())){failed=true;}
    }
  };
  {
    std::array<std::jthread,7> workers;
    const auto jobs=std::min(workers.size(),2*pending.size()-1);
    try{
      for(std::size_t i=0;i<jobs;++i){workers[i]=std::jthread(flush);}
    }catch(const std::system_error&){/* The caller completes all remaining fences. */}
    flush();
  }
  if(failed){return error(ErrorCode::Io,"Cannot synchronize checkpoint data");}
  for(bool backup:{true,false}){
    allowed=requireNoPendingSubscriptionAcquisition(lease);
    if(!allowed){return allowed;}
    for(auto& item:pending){
      const auto dest=backup?item.name+".bak":item.name;
      auto& file=backup?item.backup:item.primary;
      const auto source=file.existing?dest:dest+".saving";
      if (file.existing) {
        const auto held = stateStamp(file.fd.get());
        const auto named = stateStampAt(directory, source);
        if (!held || !named || !*named || !sameStateSnapshot(*held, **named)
            || !sameStateSnapshot(*held, file.original)) {
          return error(ErrorCode::Conflict, "Existing checkpoint file changed during synchronization");
        }
      }
      if(!backup){
        auto current=readStateFile(directory,dest,compatibleConfigLimits().inputBytes);
        if(!current){return std::unexpected(current.error());}
        if(*current!=item.state->original_){return error(ErrorCode::Conflict,"Checkpoint primary changed before publication");}
      }
      if (!file.existing) {
        auto published = publishStateFile(directory, source, dest, file.fd.get());
        if (!published) { return published; }
      }
      file.fd = StateDescriptor{};
    }
    auto durable=syncStateDirectory(directory);
    if(!durable){return durable;}
  }
  for(auto& item:pending){item.state->original_=std::move(item.encoded);item.state->backup_=false;}
  return {};
}

std::expected<void, Error> TorrentStateFile::replaceSection(std::string_view key, std::string encoded) {
  if (!prepared_) { return error(ErrorCode::Conflict, "State ownership must be prepared before editing"); }
  if (key != "resume" && key != "attributes" && key != "azureus_properties" && key != "azureus_private_properties") {
    return error(ErrorCode::Unsupported, "Only state sections can be edited; torrent info remains immutable");
  }
  if (encoded.empty() || encoded.front() != 'd' || !document_.replaceValue(key, std::move(encoded))) {
    return error(ErrorCode::InvalidFormat, "Invalid or oversized state section");
  }
  return {};
}

std::expected<TorrentStateStore, Error> TorrentStateStore::open(const ProfileLease& lease) {
  if (!lease.valid()) { return error(ErrorCode::UnsafePath, "A held profile lease is required"); }
  const auto root = stateStamp(lease.directoryHandle());
  if (!root) { return std::unexpected(root.error()); }
  auto active = openStateDirectory(lease.directoryHandle(), "active");
  if (!active) { return std::unexpected(active.error()); }
  const auto info = stateStamp(active->get());
  if (!info) { return std::unexpected(info.error()); }
  return TorrentStateStore(std::move(*active), root->device, root->inode, info->device, info->inode);
}

TorrentStateStore::TorrentStateStore(TorrentStateStore&& other) noexcept = default;
TorrentStateStore& TorrentStateStore::operator=(TorrentStateStore&& other) noexcept = default;
TorrentStateStore::~TorrentStateStore() = default;

std::expected<void, Error> TorrentStateStore::validate(const ProfileLease& lease) const {
  if (lease.valid() && active_.get() != InvalidStateHandle) {
    const auto root = stateStamp(lease.directoryHandle());
    const auto active = stateStampAt(lease.directoryHandle(), "active");
    if (root && active && *active && (*active)->directory && root->device == device_ && root->inode == inode_
        && (*active)->device == activeDevice_ && (*active)->inode == activeInode_) {
      return {};
    }
  }
  return error(ErrorCode::UnsafePath, "Saved state belongs to a different or replaced profile directory");
}

std::expected<TorrentStateFile, Error> TorrentStateStore::load(const ProfileLease& lease, const core::TorrentHash& hash) const {
  const auto valid = validate(lease);
  if (!valid) { return std::unexpected(valid.error()); }
  if (hash.bytes().size() != 20 && hash.bytes().size() != 32) { return error(ErrorCode::InvalidFormat, "Invalid torrent identity"); }
  const auto filename = hash.hex() + ".dat";
  auto primary = readStateFile(active_.get(), filename, compatibleConfigLimits().inputBytes);
  if (!primary) { return std::unexpected(primary.error()); }
  for (const bool backup : {false, true}) {
    auto input = backup ? readStateFile(active_.get(), filename + ".bak", compatibleConfigLimits().inputBytes) : primary;
    if (!input) { return std::unexpected(input.error()); }
    if (!*input) { continue; }
    auto parsed = EncodedDocument::parse(**input, compatibleConfigLimits());
    if (parsed && parsed->document().root().type == core::Node::Type::Dictionary) {
      const auto* info = parsed->document().find("info");
      if (info && info->type == core::Node::Type::Dictionary) {
        return TorrentStateFile(hash, std::move(*parsed), std::move(*primary), backup, activeDevice_, activeInode_);
      }
    }
  }
  return error(ErrorCode::InvalidFormat, "No complete compatible torrent state or backup");
}

namespace {
struct ResumeSummary {
  std::int64_t valid=-1;std::array<std::size_t,5> pieces{};
  bool operator==(const ResumeSummary&)const=default;
};
ResumeSummary resumeSummary(const core::Document& doc){
  ResumeSummary out;const auto* r=doc.find("resume"),*d=r?r->find("data"):nullptr;
  const auto* v=d?d->find("valid"):nullptr,*p=d?d->find("resume data"):nullptr;
  if(v&&v->type==core::Node::Type::Integer){out.valid=v->integer;}
  if(p&&p->type==core::Node::Type::Bytes){for(unsigned char value:p->bytes){++out.pieces[std::min<unsigned>(value,4)];}}
  return out;
}
void traceResume(const core::TorrentHash& hash,std::string_view reason,const ResumeSummary& from,const ResumeSummary& to,bool committed)noexcept{
  try{std::ostringstream line;line<<"BiglyBTC resume.write key="<<hash.hex()<<" reason="<<reason<<" committed="<<committed;
    const auto counts=[&](std::string_view prefix,const ResumeSummary& value){line<<' '<<prefix<<"_valid="<<value.valid<<' '<<prefix<<"_missing="<<value.pieces[0]<<' '<<prefix<<"_done="<<value.pieces[1]<<' '<<prefix<<"_recheck="<<value.pieces[2]<<' '<<prefix<<"_started="<<value.pieces[3]<<' '<<prefix<<"_other="<<value.pieces[4];};
    counts("before",from);counts("after",to);line<<'\n';
    const auto text=line.str();
    const auto* mirror=std::getenv("BIGLYBTC_CONSOLE_MIRROR_STDERR");
    const std::lock_guard lock(core::consoleOutputMutex());
    std::cout<<text<<std::flush;
    if(mirror&&std::string_view(mirror)=="1"){std::clog<<text<<std::flush;}
  }catch(...){} // A local diagnostic cannot change publication semantics.
}
}

std::expected<void, Error> TorrentStateStore::saveInternal(const ProfileLease& lease, TorrentStateFile& state,
    void (*observer)(StateSaveStage), ProfileSourceAccess* access, std::string_view reason) const {
  if (state.device_ != activeDevice_ || state.inode_ != activeInode_) {
    return error(ErrorCode::UnsafePath, "Torrent state belongs to a different profile directory");
  }
  auto encoded = state.document_.serialize();
  if (!encoded) { return error(ErrorCode::InvalidFormat, "Unable to encode state within resource limits"); }
  auto backup = state.document_;
  const auto checked = recheck(backup);
  if (!checked) { return checked; }
  auto backupBytes = backup.serialize();
  if (!backupBytes) { return error(ErrorCode::InvalidFormat, "Unable to encode recheck-safe state backup"); }
  const auto current=resumeSummary(state.document_.document());
  std::optional<ResumeSummary> previous;
  // Only writes that can introduce checking need an old-document diagnostic.
  // Avoid another full parse for every healthy seed during a large startup.
  if(state.original_&&(current.valid==0||current.pieces[2]||current.pieces[3])){
    if(auto parsed=EncodedDocument::parse(*state.original_,compatibleConfigLimits())){previous=resumeSummary(parsed->document());}
  }
  auto written = replace(lease, active_.get(), state.hash_.hex() + ".dat", state.original_, *encoded, *backupBytes, observer, access);
  if(previous&&*previous!=current){traceResume(state.hash_,reason,*previous,current,bool(written));}
  if (!written) { return written; }
  state.original_ = std::move(*encoded);
  state.backup_ = false;
  return {};
}

std::expected<void, Error> TorrentStateStore::save(const ProfileLease& lease, TorrentStateFile& state,
    void (*observer)(StateSaveStage), std::string_view reason) const {

  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
  if(observer&&!access->observationsAllowed()){return error(ErrorCode::Unsupported,"Observed state writes require an unnested source boundary");}
  auto valid = validate(lease);
  if (!valid) { return valid; }
  if (!state.prepared_) { return error(ErrorCode::Conflict, "State ownership must be prepared before saving"); }
  return saveInternal(lease, state, observer, &*access, reason);
}

std::expected<void, Error> TorrentStateStore::removeCachedEntry(const ProfileLease& lease, TorrentStateFile& state) const {
  auto raw = readStateFile(active_.get(), "cache.dat", compatibleConfigLimits().inputBytes);
  if (!raw) { return std::unexpected(raw.error()); }
  if (!*raw) { return {}; }
  auto cache = lease.decodeMetadataSnapshot("active/cache.dat",**raw,compatibleConfigLimits());
  if (!cache || !cache->compressed()) {
    auto safe = regularOrMissing(active_.get(), "cache.dat.bak");
    if (!safe) { return safe; }
    auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
    auto published = publishStateFile(active_.get(), "cache.dat", "cache.dat.bak");
    if (!published) { return published; }
    return syncStateDirectory(active_.get());
  }
  const auto* entries = cache->document().find("state");
  if (!entries) { return {}; }
  if (entries->type != core::Node::Type::List) { return error(ErrorCode::InvalidFormat, "Invalid original state-cache list"); }
  const auto bytes = state.hash_.bytes();
  const std::string_view key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  const auto matches = [&](const core::Node& entry) {
    const auto* hash = entry.find("hash");
    return hash && hash->type == core::Node::Type::Bytes && hash->bytes == key;
  };
  const core::Node* selected = nullptr;
  for (const auto& entry : entries->children) {
    if (matches(entry)) { selected = &entry; } // The last cache entry wins in Java.
  }
  if (!selected) { return {}; }
  std::string retained = "l";
  for (const auto& entry : entries->children) {
    if (matches(entry)) { continue; }
    auto value = core::encodeValue(entry, compatibleConfigLimits());
    if (!value || value->size() >= compatibleConfigLimits().inputBytes - retained.size()) {
      return error(ErrorCode::InvalidFormat, "State cache exceeds edit limits");
    }
    retained += *value;
  }
  for (auto [from, to] : {std::pair{"attributes", "attributes"}, {"azp", "azureus_properties"}, {"azpp", "azureus_private_properties"}}) {
    if (const auto* value = selected->find(from)) {
      if (value->type != core::Node::Type::Dictionary) { return error(ErrorCode::InvalidFormat, "Invalid cached state properties"); }
      auto encoded = core::encodeValue(*value, compatibleConfigLimits());
      if (!encoded || !state.document_.replaceValue(to, std::move(*encoded))) {
        return error(ErrorCode::InvalidFormat, "Cached state properties exceed edit limits");
      }
    }
  }
  // Persist the authoritative cache view before dropping its entry. A crash at
  // this point still lets Java use its unchanged cache and matching state.
  auto saved = saveInternal(lease, state, nullptr);
  if (!saved) { return saved; }
  retained += 'e';
  if (!cache->replaceValue("state", std::move(retained))) { return error(ErrorCode::InvalidFormat, "Cannot update original state cache"); }
  auto encoded = cache->serialize();
  if (!encoded) { return error(ErrorCode::InvalidFormat, "Cannot encode original state cache"); }
  return replace(lease, active_.get(), "cache.dat", *raw, *encoded, **raw, nullptr);
}

// Attribute-only edits share the existing resilient state/cache publication
// primitive, without invoking recheck on the primary. All existing payload
// preparation/save methods remain unchanged.
std::expected<TorrentStateFile, Error> TorrentStateStore::loadAttributes(
    const ProfileLease& lease, const core::TorrentHash& hash) const {
  auto state = load(lease, hash);
  if (!state) { return std::unexpected(state.error()); }
  auto raw = readStateFile(active_.get(), "cache.dat", compatibleConfigLimits().inputBytes);
  if (!raw) { return std::unexpected(raw.error()); }
  state->attributeCache_ = *raw;
  if (*raw) {
    auto cache = lease.decodeMetadataSnapshot("active/cache.dat",**raw,compatibleConfigLimits());
    if (!cache || !cache->compressed()) { return error(ErrorCode::InvalidFormat, "Resolve the invalid state cache before editing attributes"); }
    if (const auto* entries = cache->document().find("state")) {
      if (entries->type != core::Node::Type::List) { return error(ErrorCode::InvalidFormat, "Invalid original state-cache list"); }
      const auto bytes = hash.bytes();
      const std::string_view key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      const core::Node* selected = nullptr;
      for (const auto& entry : entries->children) {
        const auto* identity = entry.find("hash");
        if (identity && identity->type == core::Node::Type::Bytes && identity->bytes == key) {
          if (selected) { return error(ErrorCode::Conflict, "The original state cache has duplicate entries for this torrent"); }
          selected = &entry;
        }
      }
      if (selected) {
        for (auto [from, to] : {std::pair{"attributes", "attributes"}, {"azp", "azureus_properties"}, {"azpp", "azureus_private_properties"}}) {
          if (const auto* value = selected->find(from)) {
            if (value->type != core::Node::Type::Dictionary) { return error(ErrorCode::InvalidFormat, "Invalid cached state properties"); }
            const auto& source = cache->document().originalBytes();
            if (!state->document_.replaceValue(to, source.substr(value->begin, value->end - value->begin))) {
              return error(ErrorCode::InvalidFormat, "Cached attributes exceed edit limits");
            }
          }
        }
      }
    }
  }
  state->attributesOnly_ = true;
  return std::move(*state);
}

std::expected<void, Error> TorrentStateStore::saveAttributes(const ProfileLease& lease,
    TorrentStateFile& state, std::string encodedAttributes) const {
  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
  auto valid = validate(lease);
  if (!valid) { return valid; }
  if (!state.attributesOnly_ || state.prepared_ || state.device_ != activeDevice_ || state.inode_ != activeInode_) {
    return error(ErrorCode::Conflict, "Fresh attributes-only snapshot required");
  }
  auto attributes = core::Document::parse(encodedAttributes, compatibleConfigLimits());
  if (!attributes || attributes->root().type != core::Node::Type::Dictionary) {
    return error(ErrorCode::InvalidFormat, "Attributes must be a bounded dictionary");
  }
  auto changed = state.document_;
  if (!changed.replaceValue("attributes", std::move(encodedAttributes))) {
    return error(ErrorCode::InvalidFormat, "Attributes exceed edit limits");
  }
  auto current = readStateFile(active_.get(), state.hash_.hex() + ".dat", compatibleConfigLimits().inputBytes);
  auto cacheBytes = readStateFile(active_.get(), "cache.dat", compatibleConfigLimits().inputBytes);
  if (!current) { return std::unexpected(current.error()); }
  if (!cacheBytes) { return std::unexpected(cacheBytes.error()); }
  if (*current != state.original_ || *cacheBytes != state.attributeCache_) {
    return error(ErrorCode::Conflict, "Original state or cache changed since reading attributes");
  }
  std::optional<std::string> replacementCache;
  if (*cacheBytes) {
    auto cache = lease.decodeMetadataSnapshot("active/cache.dat",**cacheBytes,compatibleConfigLimits());
    if (!cache || !cache->compressed()) { return error(ErrorCode::InvalidFormat, "Invalid original state cache"); }
    if (const auto* entries = cache->document().find("state")) {
      if (entries->type != core::Node::Type::List) { return error(ErrorCode::InvalidFormat, "Invalid original state-cache list"); }
      const auto bytes = state.hash_.bytes();
      const std::string_view key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      bool selected = false;
      std::string retained = "l";
      const auto& source = cache->document().originalBytes();
      for (const auto& entry : entries->children) {
        const auto* identity = entry.find("hash");
        if (identity && identity->type == core::Node::Type::Bytes && identity->bytes == key) { selected = true; }
        else { retained += source.substr(entry.begin, entry.end - entry.begin); }
      }
      if (selected) {
        retained += 'e';
        if (!cache->replaceValue("state", std::move(retained))) { return error(ErrorCode::InvalidFormat, "Cached attributes exceed edit limits"); }
        auto encoded = cache->serialize();
        if (!encoded) { return error(ErrorCode::InvalidFormat, "Cannot encode original state cache"); }
        replacementCache = std::move(*encoded);
      }
    }
  }
  if (replacementCache) {
    // First persist the authoritative old cache view. A crash before dropping
    // that entry still leaves a matching primary. Then publish the local edit.
    auto saved = saveInternal(lease, state, nullptr);
    if (!saved) { return saved; }
    saved = replace(lease, active_.get(), "cache.dat", state.attributeCache_, *replacementCache, *state.attributeCache_, nullptr);
    if (!saved) { return saved; }
    state.attributeCache_ = std::move(replacementCache);
  }
  state.document_ = std::move(changed);
  state.attributesOnly_ = false; // Never silently repeat a partly published edit.
  return saveInternal(lease, state, nullptr);
}

#include "category_state_rebase.inc"

std::expected<StateCacheMaterialization,Error> materializeStateCache(
    const ProfileLease& lease,std::stop_token stop,std::function<void(std::size_t,std::size_t)> progress,
    const std::function<bool(const core::TorrentHash&)>& requested) {
  auto directory = openStateDirectory(lease.directoryHandle(), "active");
  if (!directory) { return std::unexpected(directory.error()); }
  const auto active = directory->get();
  const auto heldDirectory = stateStamp(active);
  if (!heldDirectory) { return std::unexpected(heldDirectory.error()); }
  const auto validDirectory = [&]() -> std::expected<void, Error> {
    if (lease.valid()) {
      const auto named = stateStampAt(lease.directoryHandle(), "active");
      if (named && *named && (*named)->directory && sameStateIdentity(**named, *heldDirectory)) { return {}; }
    }
    return error(ErrorCode::Conflict, "Original active-state directory changed");
  };
  StateCacheMaterialization result;
  std::optional<std::string> raw;
  std::optional<EncodedDocument> cache;
  StateStamp stamp{};
  const auto unchanged = [&] {
    const auto current = stateStampAt(active, "cache.dat");
    return current && *current && sameStateSnapshot(**current, stamp);
  };
  {
    auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
    const auto valid=validDirectory();if(!valid){return std::unexpected(valid.error());}
    const auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
    const auto observed = stateStampAt(active, "cache.dat");
    if (!observed) { return std::unexpected(observed.error()); }
    if (!*observed) { return result; }
    stamp = **observed;
    auto input=readStateFile(active,"cache.dat",compatibleConfigLimits().inputBytes);if(!input){return std::unexpected(input.error());}
    if(!*input||!unchanged()){return error(ErrorCode::Conflict,"State cache changed before materialization");}
    raw=std::move(*input);auto parsed=EncodedDocument::parse(*raw,compatibleConfigLimits());
    if(!parsed||!parsed->compressed()){return error(ErrorCode::InvalidFormat,"Cannot materialize an invalid original state cache");}
    cache=std::move(*parsed);
  }
  const auto* entries=cache->document().find("state");if(!entries){return result;}
  if(entries->type!=core::Node::Type::List){return error(ErrorCode::InvalidFormat,"Invalid original state-cache list");}
  std::map<std::string_view,const core::Node*> selected;
  for(const auto& entry:entries->children){
    const auto* hash=entry.find("hash");
    if(!hash||hash->type!=core::Node::Type::Bytes){continue;}
    const auto identity=core::TorrentHash::parse(hash->bytes);
    if(identity&&(!requested||requested(*identity))){selected.insert_or_assign(hash->bytes,&entry);}
  }
  if(selected.empty()){
    if(stop.stop_requested()){return error(ErrorCode::Cancelled,"State-cache materialization cancelled");}
    result.retained=entries->children.size();
    return result;
  }
  std::set<std::string_view> hydrated;
  for(const auto& [key,entry]:selected){
    if(stop.stop_requested()){return error(ErrorCode::Cancelled,"State-cache materialization cancelled");}
    {
      auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
      const auto valid=validDirectory();if(!valid){return std::unexpected(valid.error());}
      const auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
      if(!unchanged()){return error(ErrorCode::Conflict,"Original state cache changed during materialization");}
      const auto hash=*core::TorrentHash::parse(key);const auto filename=hash.hex()+".dat";
      auto primaryBytes=readStateFile(active,filename,compatibleConfigLimits().inputBytes);
      auto state=primaryBytes&&*primaryBytes?EncodedDocument::parse(**primaryBytes,compatibleConfigLimits()):std::expected<EncodedDocument,CodecError>{std::unexpected(CodecError::InvalidBencode)};
      if(state&&state->document().root().type==core::Node::Type::Dictionary&&state->document().find("info")&&state->document().find("info")->type==core::Node::Type::Dictionary){
        bool complete=true,changed=false;
        for(auto [from,to]:{std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}){
          if(const auto* value=entry->find(from)){
            if(value->type!=core::Node::Type::Dictionary){complete=false;break;}
            auto encoded=core::encodeValue(*value,compatibleConfigLimits());
            if(!encoded){complete=false;break;}
            const auto* old=state->document().find(to);const auto oldBytes=old?core::encodeValue(*old,compatibleConfigLimits()):std::expected<std::string,core::ParseError>{std::string{}};
            if(!oldBytes||*oldBytes!=*encoded){changed=true;if(!state->replaceValue(to,std::move(*encoded))){complete=false;break;}}
          }
        }
        if(complete){
          if(changed){
            const auto primary = stateStampAt(active, filename);
            if (!primary || !*primary || !(*primary)->regular) {
              return error(ErrorCode::Conflict, "Original primary changed during cache materialization");
            }
            auto encoded=state->serialize();auto backup=*state;auto checked=recheck(backup);
            if(!encoded||!checked){return error(ErrorCode::InvalidFormat,"Cannot encode materialized original state");}
            auto backupBytes=backup.serialize();if(!backupBytes){return error(ErrorCode::InvalidFormat,"Cannot encode safe materialized state backup");}
            auto saved=replace(lease,active,filename,*primaryBytes,*encoded,*backupBytes,nullptr,&*access,
                &(*primary)->modified);if(!saved){return std::unexpected(saved.error());}++result.stateWrites;
          }
          hydrated.insert(key);++result.hydrated;
        }
      }
    }
    ++result.examined;if(progress){progress(result.examined,selected.size());}
  }
  if(stop.stop_requested()){return error(ErrorCode::Cancelled,"State-cache materialization cancelled");}
  std::string retained="l";
  for(const auto& entry:entries->children){
    const auto* hash=entry.find("hash");if(hash&&hash->type==core::Node::Type::Bytes&&hydrated.contains(hash->bytes)){continue;}
    auto value=core::encodeValue(entry,compatibleConfigLimits());if(!value||value->size()>=compatibleConfigLimits().inputBytes-retained.size()){return error(ErrorCode::InvalidFormat,"Retained state cache exceeds edit limits");}retained+=*value;++result.retained;
  }
  if(hydrated.empty()){return result;}retained+='e';
  if(!cache->replaceValue("state",std::move(retained))){return error(ErrorCode::InvalidFormat,"Cannot encode retained original state cache");}
  auto encoded=cache->serialize();if(!encoded){return error(ErrorCode::InvalidFormat,"Cannot serialize retained original state cache");}
  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  if(!unchanged()){return error(ErrorCode::Conflict,"Original state cache changed before materialization commit");}
  auto written=replace(lease,active,"cache.dat",raw,*encoded,*raw,nullptr,&*access);if(!written){return std::unexpected(written.error());}
  ++result.cacheWrites;return result;
}

std::expected<TorrentStateFile, Error> TorrentStateStore::stageUpdate(const ProfileLease& lease,const core::TorrentHash& hash) const {
  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
  auto state=load(lease,hash);if(!state){return std::unexpected(state.error());}
  auto prepared=removeCachedEntry(lease,*state);if(!prepared){return std::unexpected(prepared.error());}
  state->prepared_=true;return std::move(*state);
}

std::expected<TorrentStateFile, Error> TorrentStateStore::beginUpdate(const ProfileLease& lease, const core::TorrentHash& hash) const {
  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
  auto state = load(lease, hash);
  if (!state) { return std::unexpected(state.error()); }
  auto prepared = removeCachedEntry(lease, *state);
  if (!prepared) { return std::unexpected(prepared.error()); }
  prepared = recheck(state->document_);
  if (!prepared) { return std::unexpected(prepared.error()); }
  prepared = saveInternal(lease, *state, nullptr, nullptr, "invalidate-before-payload");
  if (!prepared) { return std::unexpected(prepared.error()); }
  state->prepared_ = true;
  return std::move(*state);
}

} // namespace biglybtc::storage
