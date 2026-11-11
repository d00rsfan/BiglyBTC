#include "torrent_state.hpp"
#include "file_io.hpp"

#include <cerrno>
#include <algorithm>
#include <array>
#include <new>
#include <map>
#include <set>
#include <ctime>
#include "state_cache_materialization.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::storage {
namespace {
using detail::Descriptor;
using detail::readFile;
using detail::writeExclusive;
std::unexpected<Error> error(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}
std::expected<void, Error> sync(int directory) {
  if (::fsync(directory) != 0) { return error(ErrorCode::Io, "Unable to synchronize saved-state directory"); }
  return {};
}
std::expected<void, Error> regularOrMissing(int directory, const std::string& name) {
  struct stat value{};
  if (::fstatat(directory, name.c_str(), &value, AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT) { return {}; }
    return error(ErrorCode::Io, "Unable to inspect saved-state entry");
  }
  if (!S_ISREG(value.st_mode)) { return error(ErrorCode::UnsafePath, "Saved-state entry must be a regular file"); }
  return {};
}
std::expected<void, Error> preserveTemporary(int directory, const std::string& name) {
  auto safe = regularOrMissing(directory, name);
  if (!safe) { return safe; }
  struct stat value{};
  if (::fstatat(directory, name.c_str(), &value, AT_SYMLINK_NOFOLLOW) != 0) {
    return errno == ENOENT ? std::expected<void, Error>{} : error(ErrorCode::Io, "Unable to inspect recovery file");
  }
  const auto retained = name + ".bak";
  safe = regularOrMissing(directory, retained);
  if (!safe) { return safe; }
  if (::renameat(directory, name.c_str(), directory, retained.c_str()) != 0) {
    return error(ErrorCode::Io, "Unable to retain previous temporary state");
  }
  return sync(directory);
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

std::expected<void, Error> replace(const ProfileLease& lease, int directory, const std::string& filename,
    const std::optional<std::string>& original, std::string_view encoded, std::string_view backup,
    void (*observer)(StateSaveStage), ProfileSourceAccess* access=nullptr, const timespec* retainMtime=nullptr) {
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  auto current = readFile(directory, filename, compatibleConfigLimits().inputBytes);
  if (!current) { return std::unexpected(current.error()); }
  if (*current != original) { return error(ErrorCode::Conflict, "Saved state changed since loading"); }
  auto safe = regularOrMissing(directory, filename + ".bak");
  if (!safe) { return safe; }
  safe = preserveTemporary(directory, filename + ".saving");
  if (!safe) { return safe; }
  safe = preserveTemporary(directory, filename + ".bak.saving");
  if (!safe) { return safe; }
  mode_t mode = 0600;
  struct stat info{};
  if (::fstatat(directory, filename.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) { mode = info.st_mode & 0777; }
  const auto saving = filename + ".saving";
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  auto written = writeExclusive(directory, saving, encoded, mode);
  if (!written) { return written; }
  if(retainMtime){
    Descriptor temporary(::openat(directory,saving.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));struct stat held{};
    const timespec times[2]{{0,UTIME_OMIT},*retainMtime};
    if(temporary.get()<0||::fstat(temporary.get(),&held)||!S_ISREG(held.st_mode)||held.st_uid!=::geteuid()
        ||held.st_nlink!=1||::futimens(temporary.get(),times)||::fsync(temporary.get())){
      return error(ErrorCode::Io,"Cannot retain original resume timestamp during cache hydration");
    }
  }
  if (observer) { if(access){access->outside([&]{observer(StateSaveStage::TemporarySynced);});}else{observer(StateSaveStage::TemporarySynced);} }
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  const auto backupSaving = filename + ".bak.saving";
  written = writeExclusive(directory, backupSaving, backup, mode);
  if (!written) { return written; }
  if (::renameat(directory, backupSaving.c_str(), directory, (filename + ".bak").c_str()) != 0) {
    return error(ErrorCode::Io, "Unable to replace saved-state backup");
  }
  safe = sync(directory);
  if (!safe) { return safe; }
  if (observer) { if(access){access->outside([&]{observer(StateSaveStage::BackupSynced);});}else{observer(StateSaveStage::BackupSynced);} }
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  // The primary stays present through backup preparation; Java can always read
  // its old or new state, even though it ignores an unfinished .saving file.
  if (::renameat(directory, saving.c_str(), directory, filename.c_str()) != 0) {
    return error(ErrorCode::Io, "Unable to replace saved state; primary retained");
  }
  if (observer) { if(access){access->outside([&]{observer(StateSaveStage::PrimaryRenamed);});}else{observer(StateSaveStage::PrimaryRenamed);} }
  allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
  return sync(directory);
}
} // namespace

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
  struct stat root{};
  if (::fstat(lease.directoryHandle(), &root) != 0) { return error(ErrorCode::Io, "Cannot identify profile directory"); }
  Descriptor active(::openat(lease.directoryHandle(), "active", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (active.get() < 0) { return error(ErrorCode::UnsafePath, "Cannot open original active-state directory safely"); }
  struct stat info{};
  if (::fstat(active.get(), &info) != 0) { return error(ErrorCode::Io, "Cannot identify active-state directory"); }
  return TorrentStateStore(active.release(), root.st_dev, root.st_ino, info.st_dev, info.st_ino);
}

TorrentStateStore::TorrentStateStore(TorrentStateStore&& other) noexcept
    : active_(std::exchange(other.active_, -1)), device_(other.device_), inode_(other.inode_),
      activeDevice_(other.activeDevice_), activeInode_(other.activeInode_) {}
TorrentStateStore& TorrentStateStore::operator=(TorrentStateStore&& other) noexcept {
  if (this != &other) {
    if (active_ >= 0) { ::close(active_); }
    active_ = std::exchange(other.active_, -1);
    device_ = other.device_; inode_ = other.inode_;
    activeDevice_ = other.activeDevice_; activeInode_ = other.activeInode_;
  }
  return *this;
}
TorrentStateStore::~TorrentStateStore() { if (active_ >= 0) { ::close(active_); } }

std::expected<void, Error> TorrentStateStore::validate(const ProfileLease& lease) const {
  struct stat root{}, active{};
  if (!lease.valid() || active_ < 0 || ::fstat(lease.directoryHandle(), &root) != 0
      || root.st_dev != device_ || root.st_ino != inode_
      || ::fstatat(lease.directoryHandle(), "active", &active, AT_SYMLINK_NOFOLLOW) != 0
      || active.st_dev != activeDevice_ || active.st_ino != activeInode_) {
    return error(ErrorCode::UnsafePath, "Saved state belongs to a different or replaced profile directory");
  }
  return {};
}

std::expected<TorrentStateFile, Error> TorrentStateStore::load(const ProfileLease& lease, const core::TorrentHash& hash) const {
  const auto valid = validate(lease);
  if (!valid) { return std::unexpected(valid.error()); }
  if (hash.bytes().size() != 20 && hash.bytes().size() != 32) { return error(ErrorCode::InvalidFormat, "Invalid torrent identity"); }
  const auto filename = hash.hex() + ".dat";
  auto primary = readFile(active_, filename, compatibleConfigLimits().inputBytes);
  if (!primary) { return std::unexpected(primary.error()); }
  for (const bool backup : {false, true}) {
    auto input = backup ? readFile(active_, filename + ".bak", compatibleConfigLimits().inputBytes) : primary;
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

std::expected<void, Error> TorrentStateStore::saveInternal(const ProfileLease& lease, TorrentStateFile& state,
    void (*observer)(StateSaveStage), ProfileSourceAccess* access) const {
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
  auto written = replace(lease, active_, state.hash_.hex() + ".dat", state.original_, *encoded, *backupBytes, observer, access);
  if (!written) { return written; }
  state.original_ = std::move(*encoded);
  state.backup_ = false;
  return {};
}

std::expected<void, Error> TorrentStateStore::save(const ProfileLease& lease, TorrentStateFile& state,
    void (*observer)(StateSaveStage)) const {

  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
  if(observer&&!access->observationsAllowed()){return error(ErrorCode::Unsupported,"Observed state writes require an unnested source boundary");}
  auto valid = validate(lease);
  if (!valid) { return valid; }
  if (!state.prepared_) { return error(ErrorCode::Conflict, "State ownership must be prepared before saving"); }
  return saveInternal(lease, state, observer, &*access);
}

std::expected<void, Error> TorrentStateStore::removeCachedEntry(const ProfileLease& lease, TorrentStateFile& state) const {
  auto raw = readFile(active_, "cache.dat", compatibleConfigLimits().inputBytes);
  if (!raw) { return std::unexpected(raw.error()); }
  if (!*raw) { return {}; }
  auto cache = EncodedDocument::parse(**raw, compatibleConfigLimits());
  if (!cache || !cache->compressed()) {
    auto safe = regularOrMissing(active_, "cache.dat.bak");
    if (!safe) { return safe; }
    auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return allowed;}
    if (::renameat(active_, "cache.dat", active_, "cache.dat.bak") != 0) {
      return error(ErrorCode::Io, "Cannot retain corrupt original state cache");
    }
    return sync(active_);
  }
  const auto* entries = cache->document().find("state");
  if (!entries) { return {}; }
  if (entries->type != core::Node::Type::List) { return error(ErrorCode::InvalidFormat, "Invalid original state-cache list"); }
  const auto bytes = state.hash_.bytes();
  const std::string_view key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  const core::Node* selected = nullptr;
  std::string retained = "l";
  for (const auto& entry : entries->children) {
    const auto* hash = entry.find("hash");
    if (hash && hash->type == core::Node::Type::Bytes && hash->bytes == key) {
      selected = &entry; // Original Java map insertion makes the last entry win.
    } else {
      auto value = core::encodeValue(entry, compatibleConfigLimits());
      if (!value || value->size() >= compatibleConfigLimits().inputBytes - retained.size()) {
        return error(ErrorCode::InvalidFormat, "State cache exceeds edit limits");
      }
      retained += *value;
    }
  }
  if (!selected) { return {}; }
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
  return replace(lease, active_, "cache.dat", *raw, *encoded, **raw, nullptr);
}

// Attribute-only edits share the existing resilient state/cache publication
// primitive, without invoking recheck on the primary. All existing payload
// preparation/save methods remain unchanged.
std::expected<TorrentStateFile, Error> TorrentStateStore::loadAttributes(
    const ProfileLease& lease, const core::TorrentHash& hash) const {
  auto state = load(lease, hash);
  if (!state) { return std::unexpected(state.error()); }
  auto raw = readFile(active_, "cache.dat", compatibleConfigLimits().inputBytes);
  if (!raw) { return std::unexpected(raw.error()); }
  state->attributeCache_ = *raw;
  if (*raw) {
    auto cache = EncodedDocument::parse(**raw, compatibleConfigLimits());
    if (!cache || !cache->compressed()) { return error(ErrorCode::InvalidFormat, "Resolve the invalid state cache before editing attributes"); }
    if (const auto* entries = cache->document().find("state")) {
      if (entries->type != core::Node::Type::List) { return error(ErrorCode::InvalidFormat, "Invalid original state-cache list"); }
      const auto bytes = hash.bytes();
      const std::string_view key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      const core::Node* selected = nullptr;
      for (const auto& entry : entries->children) {
        const auto* identity = entry.find("hash");
        if (identity && identity->type == core::Node::Type::Bytes && identity->bytes == key) { selected = &entry; }
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
  auto current = readFile(active_, state.hash_.hex() + ".dat", compatibleConfigLimits().inputBytes);
  auto cacheBytes = readFile(active_, "cache.dat", compatibleConfigLimits().inputBytes);
  if (!current) { return std::unexpected(current.error()); }
  if (!cacheBytes) { return std::unexpected(cacheBytes.error()); }
  if (*current != state.original_ || *cacheBytes != state.attributeCache_) {
    return error(ErrorCode::Conflict, "Original state or cache changed since reading attributes");
  }
  std::optional<std::string> replacementCache;
  if (*cacheBytes) {
    auto cache = EncodedDocument::parse(**cacheBytes, compatibleConfigLimits());
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
    saved = replace(lease, active_, "cache.dat", state.attributeCache_, *replacementCache, *state.attributeCache_, nullptr);
    if (!saved) { return saved; }
    state.attributeCache_ = std::move(replacementCache);
  }
  state.document_ = std::move(changed);
  state.attributesOnly_ = false; // Never silently repeat a partly published edit.
  return saveInternal(lease, state, nullptr);
}

#include "category_state_rebase.inc"

std::expected<StateCacheMaterialization,Error> materializeStateCache(
    const ProfileLease& lease,std::stop_token stop,std::function<void(std::size_t,std::size_t)> progress) {
  Descriptor directory(::openat(lease.directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  if(directory.get()<0){return error(ErrorCode::UnsafePath,"Cannot open original active-state directory");}
  const int active_=directory.get();struct stat heldDirectory{};
  if(::fstat(active_,&heldDirectory)){return error(ErrorCode::Io,"Cannot identify active-state directory");}
  const auto validDirectory=[&]() -> std::expected<void,Error>{struct stat named{};
    if(!lease.valid()||::fstatat(lease.directoryHandle(),"active",&named,AT_SYMLINK_NOFOLLOW)
        ||!S_ISDIR(named.st_mode)||named.st_dev!=heldDirectory.st_dev||named.st_ino!=heldDirectory.st_ino){return error(ErrorCode::Conflict,"Original active-state directory changed");}return {};
  };
  StateCacheMaterialization result;std::optional<std::string> raw;std::optional<EncodedDocument> cache;struct stat stamp{};
  const auto unchanged=[&](){
    struct stat current{};
    return ::fstatat(active_,"cache.dat",&current,AT_SYMLINK_NOFOLLOW)==0
        &&current.st_dev==stamp.st_dev&&current.st_ino==stamp.st_ino&&current.st_mode==stamp.st_mode
        &&current.st_uid==stamp.st_uid&&current.st_gid==stamp.st_gid&&current.st_nlink==stamp.st_nlink&&current.st_size==stamp.st_size
        &&current.st_mtim.tv_sec==stamp.st_mtim.tv_sec&&current.st_mtim.tv_nsec==stamp.st_mtim.tv_nsec
        &&current.st_ctim.tv_sec==stamp.st_ctim.tv_sec&&current.st_ctim.tv_nsec==stamp.st_ctim.tv_nsec;
  };
  {
    auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
    const auto valid=validDirectory();if(!valid){return std::unexpected(valid.error());}
    const auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
    if(::fstatat(active_,"cache.dat",&stamp,AT_SYMLINK_NOFOLLOW)){if(errno==ENOENT){return result;}return error(ErrorCode::Io,"Cannot identify original state cache");}
    auto input=readFile(active_,"cache.dat",compatibleConfigLimits().inputBytes);if(!input){return std::unexpected(input.error());}
    if(!*input||!unchanged()){return error(ErrorCode::Conflict,"State cache changed before materialization");}
    raw=std::move(*input);auto parsed=EncodedDocument::parse(*raw,compatibleConfigLimits());
    if(!parsed||!parsed->compressed()){return error(ErrorCode::InvalidFormat,"Cannot materialize an invalid original state cache");}
    cache=std::move(*parsed);
  }
  const auto* entries=cache->document().find("state");if(!entries){return result;}
  if(entries->type!=core::Node::Type::List){return error(ErrorCode::InvalidFormat,"Invalid original state-cache list");}
  std::map<std::string_view,const core::Node*> selected;
  for(const auto& entry:entries->children){const auto* hash=entry.find("hash");if(hash&&hash->type==core::Node::Type::Bytes&&core::TorrentHash::parse(hash->bytes)){selected.insert_or_assign(hash->bytes,&entry);}}
  std::set<std::string_view> hydrated;
  for(const auto& [key,entry]:selected){
    if(stop.stop_requested()){return error(ErrorCode::Cancelled,"State-cache materialization cancelled");}
    {
      auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
      const auto valid=validDirectory();if(!valid){return std::unexpected(valid.error());}
      const auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
      if(!unchanged()){return error(ErrorCode::Conflict,"Original state cache changed during materialization");}
      const auto hash=*core::TorrentHash::parse(key);const auto filename=hash.hex()+".dat";
      auto primaryBytes=readFile(active_,filename,compatibleConfigLimits().inputBytes);
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
            struct stat primary{};if(::fstatat(active_,(hash.hex()+".dat").c_str(),&primary,AT_SYMLINK_NOFOLLOW)||!S_ISREG(primary.st_mode)){return error(ErrorCode::Conflict,"Original primary changed during cache materialization");}
            auto encoded=state->serialize();auto backup=*state;auto checked=recheck(backup);
            if(!encoded||!checked){return error(ErrorCode::InvalidFormat,"Cannot encode materialized original state");}
            auto backupBytes=backup.serialize();if(!backupBytes){return error(ErrorCode::InvalidFormat,"Cannot encode safe materialized state backup");}
            auto saved=replace(lease,active_,filename,*primaryBytes,*encoded,*backupBytes,nullptr,&*access,&primary.st_mtim);if(!saved){return std::unexpected(saved.error());}++result.stateWrites;
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
  auto written=replace(lease,active_,"cache.dat",raw,*encoded,*raw,nullptr,&*access);if(!written){return std::unexpected(written.error());}
  ++result.cacheWrites;return result;
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
  prepared = saveInternal(lease, *state, nullptr);
  if (!prepared) { return std::unexpected(prepared.error()); }
  state->prepared_ = true;
  return std::move(*state);
}

} // namespace biglybtc::storage
