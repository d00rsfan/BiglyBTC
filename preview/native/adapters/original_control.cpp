#include "original_control.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
using namespace detail;
struct Failure {Error error;};
void require(bool condition,std::string message,ErrorCode code=ErrorCode::Unsupported){
  if(!condition){throw Failure{{code,std::move(message)}};}
}
template<class T>T take(std::expected<T,Error> result){
  if(!result){throw Failure{result.error()};}return std::move(*result);
}
void take(std::expected<void,Error> result){if(!result){throw Failure{result.error()};}}
constexpr std::size_t MaximumControl = 16 * 1024 * 1024;
using Kind = storage::OriginalStorageKind;
StateStamp ownedDirectory(StateHandle fd) {
  const auto stamp = stateStamp(fd);
  require(stamp && stamp->directory && stamp->owned && stamp->links,
      "Unsafe original storage control directory",ErrorCode::UnsafePath);
  return *stamp;
}
void namedDirectory(StateHandle parent,const std::string& name,StateHandle fd) {
  const auto held = ownedDirectory(fd);
  const auto named = stateStampAt(parent, name);
  require(named && *named && (*named)->directory && sameStateIdentity(held, **named),
      "Original storage control directory was replaced",ErrorCode::Conflict);
}
void absent(StateHandle dir,const std::string& name) {
  const auto named = stateStampAt(dir, name);
  require(named && !*named,"Resolve original storage control .saving before admission",ErrorCode::Conflict);
}
void noSaving(StateHandle dir,const std::string& leaf) { absent(dir,leaf+".saving");absent(dir,leaf+".bak.saving"); }
struct Candidate {
  StateDescriptor file;
  std::optional<std::string> bytes;
  StateStamp stamp{};
};
void checkCandidate(StateHandle dir,const std::string& name,const Candidate& expected) {
  const auto named = stateStampAt(dir, name);
  if(!expected.bytes) { require(named && !*named,"Original control candidate appeared",ErrorCode::Conflict);return; }
  const auto held = stateStamp(expected.file.get());
  require(named && *named && held && sameStateSnapshot(*held, expected.stamp) && sameStateSnapshot(**named, expected.stamp),
      "Original control candidate identity/content changed",ErrorCode::Conflict);
}
Candidate readCandidate(StateHandle dir,const std::string& name) {
  Candidate result;
  const auto named = take(stateStampAt(dir, name));
  if (!named) { return result; }
  require(named->regular && named->owned && named->links == 1 && named->size >= 0
      && static_cast<std::uint64_t>(named->size) <= MaximumControl,"Original control must be bounded owned single-link data",ErrorCode::UnsafePath);
  auto file = openStateFile(dir, name);
  require(file.has_value(), "Original control changed before open", ErrorCode::Conflict);
  result.file = std::move(*file);
  result.stamp = take(stateStamp(result.file.get()));
  require(sameStateSnapshot(*named, result.stamp), "Original control changed before open", ErrorCode::Conflict);
  result.bytes.emplace(static_cast<std::size_t>(named->size),'\0');std::size_t offset=0;
  while(offset < result.bytes->size()) {
    const auto count = readStateAt(result.file.get(), result.bytes->data()+offset, result.bytes->size()-offset, offset);
    require(count > 0,"Original control read incomplete",ErrorCode::Io);offset+=static_cast<std::size_t>(count);
  }
  checkCandidate(dir,name,result);return result;
}
bool validControl(const std::string& bytes,Kind kind) {
  const core::Limits limits{.inputBytes=MaximumControl,.stringBytes=MaximumControl,.nodes=100000,.depth=64};
  const auto doc=core::Document::parse(bytes,limits);if(!doc || doc->root().type != core::Node::Type::Dictionary){return false;}
  const auto integer=[&](const char* key,std::int64_t minimum){const auto* n=doc->find(key);return n && n->type == core::Node::Type::Integer && n->integer >= minimum;};
  if(kind == Kind::Compact) {const auto* version=doc->find("version");return version && version->type == core::Node::Type::Integer && version->integer == 0 && integer("length",0);}
  const auto* pieces=doc->find("pieces"),*previous=doc->find("st");
  return integer("len",0) && integer("next",1) && pieces && pieces->type == core::Node::Type::Bytes && pieces->bytes.size()%4 == 0
      && (!previous || (previous->type == core::Node::Type::Integer && (previous->integer == 3 || previous->integer == 4)));
}
struct Candidates { Candidate primary,backup;bool primaryValid=false,backupValid=false; };
Candidates readCandidates(StateHandle dir,const std::string& leaf,Kind kind) {
  Candidates result;result.primary=readCandidate(dir,leaf);result.backup=readCandidate(dir,leaf+".bak");
  result.primaryValid=result.primary.bytes && validControl(*result.primary.bytes,kind);result.backupValid=result.backup.bytes && validControl(*result.backup.bytes,kind);return result;
}
std::optional<std::string> selected(const Candidates& candidates) {
  if(candidates.primaryValid){return candidates.primary.bytes;}if(candidates.backupValid){return candidates.backup.bytes;}
  require(!candidates.primary.bytes && !candidates.backup.bytes,"Original control and backup are malformed",ErrorCode::InvalidFormat);return {};
}
std::optional<StateStamp> stamp(const Candidate& value) { return value.bytes ? std::optional(value.stamp) : std::nullopt; }
void checkCandidates(StateHandle dir,const std::string& leaf,const Candidates& candidates) {
  checkCandidate(dir,leaf,candidates.primary);checkCandidate(dir,leaf+".bak",candidates.backup);
}
template<class F>auto guarded(F&& action)->decltype(action()) {
  try{return action();}catch(const Failure& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Original control allocation failed"});}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Original control operation failed"});}
}
} // namespace
void OriginalControl::checkDirectories() const {
  const auto held = ownedDirectory(profile_.get());
  const auto current = take(openStatePathDirectory(profilePath_));
  const auto named = take(stateStamp(current.get()));
  require(sameStateIdentity(held,named),"Held original profile pathname changed",ErrorCode::Conflict);
  namedDirectory(profile_.get(),"active",active_.get());namedDirectory(active_.get(),hash_,directory_.get());
}
void OriginalControl::checkObserved(const std::optional<StateStamp>& primary,const std::optional<StateStamp>& backup) const {
  if(!observed_){return;}
  const auto same=[](const auto& old,const auto& now){return bool(old)==bool(now) && (!old || sameStateSnapshot(*old,*now));};
  require(same(primary_,primary) && same(backup_,backup),"Original control changed since the accepted observation",ErrorCode::Conflict);
}
void OriginalControl::remember(const std::optional<StateStamp>& primary,const std::optional<StateStamp>& backup) const {
  primary_=primary;backup_=backup;observed_=true;
}
std::expected<std::shared_ptr<OriginalControl>, Error> OriginalControl::open(const storage::ProfileLease& lease,
    std::string hash,std::size_t index,Kind kind,bool create) {
  return guarded([&]()->std::expected<std::shared_ptr<OriginalControl>,Error>{
    require(lease.valid() && (hash.size() == 40 || hash.size() == 64) && index < 1000000 && kind >= Kind::Compact && kind <= Kind::ReorderedCompact
        && std::ranges::all_of(hash,[](char c){return(c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');}),"Unsafe original storage identity",ErrorCode::UnsafePath);
    auto profile = take(duplicateStateDirectory(lease.directoryHandle()));
    ownedDirectory(profile.get());
    auto path = take(stateDirectoryPath(profile.get()));
    auto active = take(openStateDirectory(profile.get(), "active"));
    namedDirectory(profile.get(),"active",active.get());
    auto candidate = openStateDirectory(active.get(), hash);
    if (!candidate && candidate.error().code == ErrorCode::Missing) {
      if (!create) { return std::shared_ptr<OriginalControl>{}; }
      candidate = createStateDirectory(active.get(), hash);
    }
    auto directory = take(std::move(candidate));
    namedDirectory(active.get(),hash,directory.get());
    auto result=std::shared_ptr<OriginalControl>(new OriginalControl(std::move(profile),std::move(active),std::move(directory),std::move(path),std::move(hash),storage::originalControlLeaf(index,kind),kind));
    (void)take(result->load());return result;
  });
}
// Host snapshot validation and the original disk worker share this owner.
// Serialize their real I/O; these operations never invoke user callbacks,
// acquire a ProfileSource gate, or recursively enter another control method.
std::expected<std::optional<std::string>, Error> OriginalControl::load() const {
  return guarded([&]()->std::expected<std::optional<std::string>,Error>{
    const std::lock_guard lock(mutex_);
    checkDirectories();noSaving(directory_.get(),leaf_);auto candidates=readCandidates(directory_.get(),leaf_,kind_);checkObserved(stamp(candidates.primary),stamp(candidates.backup));checkDirectories();checkCandidates(directory_.get(),leaf_,candidates);auto result=selected(candidates);remember(stamp(candidates.primary),stamp(candidates.backup));return result;
  });
}
std::expected<std::optional<std::string>, Error> OriginalControl::loadValidated(storage::OriginalFileGeometry geometry) const {
  return guarded([&]()->std::expected<std::optional<std::string>,Error>{
    const std::lock_guard lock(mutex_);
    checkDirectories();noSaving(directory_.get(),leaf_);auto candidates=readCandidates(directory_.get(),leaf_,kind_);checkObserved(stamp(candidates.primary),stamp(candidates.backup));
    candidates.primaryValid=candidates.primary.bytes&&storage::OriginalFileIo::validateControl(geometry,kind_,*candidates.primary.bytes).has_value();
    candidates.backupValid=candidates.backup.bytes&&storage::OriginalFileIo::validateControl(geometry,kind_,*candidates.backup.bytes).has_value();
    checkDirectories();checkCandidates(directory_.get(),leaf_,candidates);auto result=selected(candidates);remember(stamp(candidates.primary),stamp(candidates.backup));return result;
  });
}
std::expected<void,Error> OriginalControl::authenticate(const ProfileLease& lease,
    std::string_view hash,std::size_t index,Kind kind) const {
  return guarded([&]()->std::expected<void,Error>{
    std::lock_guard lock(mutex_);checkDirectories();
    require(kind==Kind::Compact||kind==Kind::Reordered||kind==Kind::ReorderedCompact,"Invalid conversion control family",ErrorCode::InvalidFormat);
    const auto held = stateStamp(profile_.get());
    const auto caller = stateStamp(lease.directoryHandle());
    require(held && caller && sameStateIdentity(*held, *caller)
        &&hash_==hash&&leaf_==originalControlLeaf(index,kind),"Conversion control belongs to a different profile/file",ErrorCode::Conflict);
    return {};
  });
}
std::expected<bool,Error> OriginalControl::hasBackupOnly() const {
  return guarded([&]()->std::expected<bool,Error>{
    const std::lock_guard lock(mutex_);checkDirectories();noSaving(directory_.get(),leaf_);
    auto candidates=readCandidates(directory_.get(),leaf_,kind_);checkObserved(stamp(candidates.primary),stamp(candidates.backup));
    checkDirectories();checkCandidates(directory_.get(),leaf_,candidates);
    const bool result=!candidates.primary.bytes&&candidates.backup.bytes.has_value();remember(stamp(candidates.primary),stamp(candidates.backup));return result;
  });
}
storage::OriginalControlPublisher OriginalControl::publisher() {
  return [self=shared_from_this()](const auto& expected,auto replacement){return self->publish(expected,replacement);};
}
std::expected<void, Error> OriginalControl::retire() {
  return guarded([&]()->std::expected<void,Error>{
    const std::lock_guard lock(mutex_);
    checkDirectories();noSaving(directory_.get(),leaf_);auto primary=readCandidate(directory_.get(),leaf_),backup=readCandidate(directory_.get(),leaf_+".bak");checkObserved(stamp(primary),stamp(backup));
    checkDirectories();checkCandidate(directory_.get(),leaf_,primary);checkCandidate(directory_.get(),leaf_+".bak",backup);
    for (const auto& [candidate, name] : {std::pair{&primary, leaf_}, std::pair{&backup, leaf_+".bak"}}) {
      if (!candidate->bytes) { continue; }
      checkDirectories();
      checkCandidate(directory_.get(), name, *candidate);
      candidate->file = StateDescriptor{};
      take(retireStateFile(directory_.get(), name, candidate->stamp));
      take(syncStateDirectory(directory_.get()));
    }
    remember({},{});return {};
  });
}
std::expected<void, Error> OriginalControl::publish(const std::optional<std::string>& expected,std::string_view replacement) {
  return guarded([&]()->std::expected<void,Error>{
    const std::lock_guard lock(mutex_);
    require(replacement.size() <= MaximumControl && validControl(std::string(replacement),kind_),"Invalid original storage control replacement",ErrorCode::InvalidFormat);
    checkDirectories();noSaving(directory_.get(),leaf_);auto candidates=readCandidates(directory_.get(),leaf_,kind_);checkObserved(stamp(candidates.primary),stamp(candidates.backup));require(selected(candidates) == expected,"Original storage control changed",ErrorCode::Conflict);
    const auto saving = leaf_ + ".saving";
    const auto backup = leaf_ + ".bak";
    auto temporary = take(stageStateFile(directory_.get(), saving, replacement, 0600));
    take(protectPrivateStateFile(temporary.get()));
    take(syncStateFile(temporary.get()));
    const auto saved = take(stateStamp(temporary.get()));
    require(saved.regular && saved.owned && saved.links == 1 && saved.size == static_cast<std::int64_t>(replacement.size()),
        "Unsafe original control temporary", ErrorCode::Conflict);
    const auto checkTemporary = [&] {
      const auto held = stateStamp(temporary.get());
      const auto named = stateStampAt(directory_.get(), saving);
      require(held && named && *named && sameStateSnapshot(saved, *held) && sameStateSnapshot(saved, **named),
          "Original control temporary changed", ErrorCode::Conflict);
    };
    checkDirectories();checkCandidates(directory_.get(),leaf_,candidates);checkTemporary();
    // Win32 publication reopens the source with delete access. Its held stamp
    // must still match the authenticated preimage after the read handles close.
    candidates.primary.file = StateDescriptor{};
    candidates.backup.file = StateDescriptor{};
    if(candidates.primaryValid){
      take(publishStateFile(directory_.get(), leaf_, backup, InvalidStateHandle, true, &candidates.primary.stamp));
      take(syncStateDirectory(directory_.get()));
      checkDirectories();checkTemporary();
      const auto moved = stateStampAt(directory_.get(), backup);
      require(moved && *moved && sameStateIdentity(**moved, candidates.primary.stamp),
          "Original control backup identity changed", ErrorCode::Conflict);
      absent(directory_.get(),leaf_);
    }
    // A malformed primary never replaces the valid fallback backup. We replace
    // only the authenticated damaged primary, keeping that valid backup intact.
    const bool damagedPrimary = candidates.primary.bytes && !candidates.primaryValid;
    take(publishStateFile(directory_.get(), saving, leaf_, temporary.get(), damagedPrimary, &saved));
    take(syncStateDirectory(directory_.get()));
    temporary = StateDescriptor{};
    checkDirectories();auto published=readCandidates(directory_.get(),leaf_,kind_);checkCandidates(directory_.get(),leaf_,published);require(published.primary.bytes && *published.primary.bytes == replacement,"Original control changed after publication",ErrorCode::Conflict);remember(stamp(published.primary),stamp(published.backup));return {};
  });
}
} // namespace biglybtc::storage
