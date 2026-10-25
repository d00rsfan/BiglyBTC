#include "original_control.hpp"
#include "local_support.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <exception>
#include <new>
#include <utility>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace biglybtc::engine::detail {
namespace {
constexpr std::size_t MaximumControl = 16 * 1024 * 1024;
using Kind = storage::OriginalStorageKind;
bool identity(const struct stat& a, const struct stat& b) { return a.st_dev == b.st_dev && a.st_ino == b.st_ino; }
bool unchanged(const struct stat& a, const struct stat& b) {
  return identity(a,b) && a.st_mode == b.st_mode && a.st_uid == b.st_uid && a.st_nlink == b.st_nlink
      && a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
      && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
void ownedDirectory(int fd) {
  struct stat s{}; require(fd >= 0 && ::fstat(fd,&s) == 0 && S_ISDIR(s.st_mode) && s.st_uid == ::geteuid() && s.st_nlink,
      "Unsafe original storage control directory",ErrorCode::UnsafePath);
}
void namedDirectory(int parent,const std::string& name,int fd) {
  struct stat held{},named{};ownedDirectory(fd);
  require(::fstat(fd,&held) == 0 && ::fstatat(parent,name.c_str(),&named,AT_SYMLINK_NOFOLLOW) == 0
      && S_ISDIR(named.st_mode) && identity(held,named),"Original storage control directory was replaced",ErrorCode::Conflict);
}
Descriptor absoluteDirectory(const std::filesystem::path& path) {
  require(path.is_absolute() && path.lexically_normal() == path,"Unsafe original profile path",ErrorCode::UnsafePath);
  Descriptor current(::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));require(current.get() >= 0,"Cannot open filesystem root",ErrorCode::Io);
  for(const auto& part:path.relative_path()) {
    require(!part.empty() && part != "." && part != "..","Unsafe original profile component",ErrorCode::UnsafePath);
    Descriptor next(::openat(current.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat held{},named{};
    require(next.get() >= 0 && ::fstat(next.get(),&held) == 0 && ::fstatat(current.get(),part.c_str(),&named,AT_SYMLINK_NOFOLLOW) == 0
        && S_ISDIR(named.st_mode) && identity(held,named),"Original profile directory chain changed",ErrorCode::Conflict);current=std::move(next);
  }return current;
}
std::string profilePath(int fd) {
  std::array<char,4097> bytes{};const auto path="/proc/self/fd/"+std::to_string(fd);const auto count=::readlink(path.c_str(),bytes.data(),bytes.size());
  require(count > 0 && static_cast<std::size_t>(count) < bytes.size(),"Cannot resolve original profile identity",ErrorCode::UnsafePath);
  return {bytes.data(),static_cast<std::size_t>(count)};
}
void absent(int dir,const std::string& name) {
  struct stat named{};const int result=::fstatat(dir,name.c_str(),&named,AT_SYMLINK_NOFOLLOW);
  require(result < 0 && errno == ENOENT,"Resolve original storage control .saving before admission",ErrorCode::Conflict);
}
void noSaving(int dir,const std::string& leaf) { absent(dir,leaf+".saving");absent(dir,leaf+".bak.saving"); }
struct Candidate {
  Descriptor fd{-1};std::optional<std::string> bytes;struct stat stamp{};
};
void checkCandidate(int dir,const std::string& name,const Candidate& expected) {
  struct stat named{};const int found=::fstatat(dir,name.c_str(),&named,AT_SYMLINK_NOFOLLOW);
  if(!expected.bytes) { require(found < 0 && errno == ENOENT,"Original control candidate appeared",ErrorCode::Conflict);return; }
  struct stat held{};
  require(found == 0 && ::fstat(expected.fd.get(),&held) == 0 && unchanged(held,expected.stamp) && unchanged(named,expected.stamp),
      "Original control candidate identity/content changed",ErrorCode::Conflict);
}
Candidate readCandidate(int dir,const std::string& name) {
  Candidate result;struct stat named{};
  if(::fstatat(dir,name.c_str(),&named,AT_SYMLINK_NOFOLLOW) != 0) { require(errno == ENOENT,"Cannot inspect original control candidate",ErrorCode::Io);return result; }
  require(S_ISREG(named.st_mode) && named.st_uid == ::geteuid() && named.st_nlink == 1 && named.st_size >= 0
      && static_cast<std::uint64_t>(named.st_size) <= MaximumControl,"Original control must be bounded owned single-link data",ErrorCode::UnsafePath);
  result.fd=Descriptor(::openat(dir,name.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
  require(result.fd.get() >= 0 && ::fstat(result.fd.get(),&result.stamp) == 0 && unchanged(named,result.stamp),"Original control changed before open",ErrorCode::Conflict);
  result.bytes.emplace(static_cast<std::size_t>(named.st_size),'\0');std::size_t offset=0;
  while(offset < result.bytes->size()) {
    const auto count=::read(result.fd.get(),result.bytes->data()+offset,result.bytes->size()-offset);if(count < 0 && errno == EINTR){continue;}
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
Candidates readCandidates(int dir,const std::string& leaf,Kind kind) {
  Candidates result;result.primary=readCandidate(dir,leaf);result.backup=readCandidate(dir,leaf+".bak");
  result.primaryValid=result.primary.bytes && validControl(*result.primary.bytes,kind);result.backupValid=result.backup.bytes && validControl(*result.backup.bytes,kind);return result;
}
std::optional<std::string> selected(const Candidates& candidates) {
  if(candidates.primaryValid){return candidates.primary.bytes;}if(candidates.backupValid){return candidates.backup.bytes;}
  require(!candidates.primary.bytes && !candidates.backup.bytes,"Original control and backup are malformed",ErrorCode::InvalidFormat);return {};
}
std::optional<struct stat> stamp(const Candidate& value) { return value.bytes ? std::optional<struct stat>(value.stamp) : std::nullopt; }
void checkCandidates(int dir,const std::string& leaf,const Candidates& candidates) {
  checkCandidate(dir,leaf,candidates.primary);checkCandidate(dir,leaf+".bak",candidates.backup);
}
template<class F>auto guarded(F&& action)->decltype(action()) {
  try{return action();}catch(const Failure& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Original control allocation failed"});}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Original control operation failed"});}
}
} // namespace
void OriginalControl::checkDirectories() const {
  ownedDirectory(profile_.get());auto current=absoluteDirectory(profilePath_);struct stat held{},named{};
  require(::fstat(profile_.get(),&held) == 0 && ::fstat(current.get(),&named) == 0 && identity(held,named),"Held original profile pathname changed",ErrorCode::Conflict);
  namedDirectory(profile_.get(),"active",active_.get());namedDirectory(active_.get(),hash_,directory_.get());
}
void OriginalControl::checkObserved(const std::optional<struct stat>& primary,const std::optional<struct stat>& backup) const {
  if(!observed_){return;}
  const auto same=[](const auto& old,const auto& now){return bool(old)==bool(now) && (!old || unchanged(*old,*now));};
  require(same(primary_,primary) && same(backup_,backup),"Original control changed since the accepted observation",ErrorCode::Conflict);
}
void OriginalControl::remember(const std::optional<struct stat>& primary,const std::optional<struct stat>& backup) const {
  primary_=primary;backup_=backup;observed_=true;
}
std::expected<std::shared_ptr<OriginalControl>, Error> OriginalControl::open(const storage::ProfileLease& lease,
    std::string hash,std::size_t index,Kind kind,bool create) {
  return guarded([&]()->std::expected<std::shared_ptr<OriginalControl>,Error>{
    require(lease.valid() && (hash.size() == 40 || hash.size() == 64) && index < 1000000 && kind >= Kind::Compact && kind <= Kind::ReorderedCompact
        && std::ranges::all_of(hash,[](char c){return(c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');}),"Unsafe original storage identity",ErrorCode::UnsafePath);
    Descriptor profile(::fcntl(lease.directoryHandle(),F_DUPFD_CLOEXEC,0));ownedDirectory(profile.get());const auto path=profilePath(profile.get());
    auto active=Descriptor(::openat(profile.get(),"active",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));namedDirectory(profile.get(),"active",active.get());
    int dir=::openat(active.get(),hash.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(dir < 0 && errno == ENOENT && create){require(::mkdirat(active.get(),hash.c_str(),0700) == 0,"Cannot exclusively create original control directory",ErrorCode::Conflict);sync(active.get());dir=::openat(active.get(),hash.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);}
    if(dir < 0 && errno == ENOENT && !create){return std::shared_ptr<OriginalControl>{};}
    Descriptor directory(dir);namedDirectory(active.get(),hash,directory.get());
    auto result=std::shared_ptr<OriginalControl>(new OriginalControl(std::move(profile),std::move(active),std::move(directory),path,std::move(hash),storage::originalControlLeaf(index,kind),kind));
    (void)take(result->load());return result;
  });
}
std::expected<std::optional<std::string>, Error> OriginalControl::load() const {
  return guarded([&]()->std::expected<std::optional<std::string>,Error>{
    const std::unique_lock lock(mutex_,std::try_to_lock);require(lock.owns_lock(),"Concurrent/reentrant original control operation",ErrorCode::Busy);
    checkDirectories();noSaving(directory_.get(),leaf_);auto candidates=readCandidates(directory_.get(),leaf_,kind_);checkObserved(stamp(candidates.primary),stamp(candidates.backup));checkDirectories();checkCandidates(directory_.get(),leaf_,candidates);auto result=selected(candidates);remember(stamp(candidates.primary),stamp(candidates.backup));return result;
  });
}
storage::OriginalControlPublisher OriginalControl::publisher() {
  return [self=shared_from_this()](const auto& expected,auto replacement){return self->publish(expected,replacement);};
}
std::expected<void, Error> OriginalControl::retire() {
  return guarded([&]()->std::expected<void,Error>{
    const std::unique_lock lock(mutex_,std::try_to_lock);require(lock.owns_lock(),"Concurrent/reentrant original control operation",ErrorCode::Busy);
    checkDirectories();noSaving(directory_.get(),leaf_);auto primary=readCandidate(directory_.get(),leaf_),backup=readCandidate(directory_.get(),leaf_+".bak");checkObserved(stamp(primary),stamp(backup));
    checkDirectories();checkCandidate(directory_.get(),leaf_,primary);checkCandidate(directory_.get(),leaf_+".bak",backup);
    for(const auto& entry:{std::pair{&primary,leaf_},std::pair{&backup,leaf_+".bak"}}){if(!entry.first->bytes){continue;}checkDirectories();checkCandidate(directory_.get(),entry.second,*entry.first);require(::unlinkat(directory_.get(),entry.second.c_str(),0) == 0,"Cannot retire exact original control",ErrorCode::Io);sync(directory_.get());}
    remember({},{});return {};
  });
}
std::expected<void, Error> OriginalControl::publish(const std::optional<std::string>& expected,std::string_view replacement) {
  return guarded([&]()->std::expected<void,Error>{
    const std::unique_lock lock(mutex_,std::try_to_lock);require(lock.owns_lock(),"Concurrent/reentrant original control operation",ErrorCode::Busy);
    require(replacement.size() <= MaximumControl && validControl(std::string(replacement),kind_),"Invalid original storage control replacement",ErrorCode::InvalidFormat);
    checkDirectories();noSaving(directory_.get(),leaf_);auto candidates=readCandidates(directory_.get(),leaf_,kind_);checkObserved(stamp(candidates.primary),stamp(candidates.backup));require(selected(candidates) == expected,"Original storage control changed",ErrorCode::Conflict);
    auto temporary=take(storage::detail::writeExclusiveOwned(directory_.get(),leaf_+".saving",replacement,0600));require(::fchmod(temporary.get(),0600) == 0,"Cannot protect original control temporary",ErrorCode::Io);sync(temporary.get());struct stat saved{};
    require(::fstat(temporary.get(),&saved) == 0 && S_ISREG(saved.st_mode) && saved.st_uid == ::geteuid() && saved.st_nlink == 1
        && saved.st_size == static_cast<off_t>(replacement.size()),"Unsafe original control temporary",ErrorCode::Conflict);
    const auto checkTemporary=[&]{struct stat held{},named{};require(::fstat(temporary.get(),&held) == 0 && ::fstatat(directory_.get(),(leaf_+".saving").c_str(),&named,AT_SYMLINK_NOFOLLOW) == 0
        && unchanged(saved,held) && unchanged(saved,named),"Original control temporary changed",ErrorCode::Conflict);};
    checkDirectories();checkCandidates(directory_.get(),leaf_,candidates);checkTemporary();
    if(candidates.primaryValid){
      require(::renameat(directory_.get(),leaf_.c_str(),directory_.get(),(leaf_+".bak").c_str()) == 0,"Cannot retain original control backup",ErrorCode::Io);sync(directory_.get());
      checkDirectories();checkTemporary();struct stat moved{};require(::fstatat(directory_.get(),(leaf_+".bak").c_str(),&moved,AT_SYMLINK_NOFOLLOW) == 0 && identity(moved,candidates.primary.stamp),"Original control backup identity changed",ErrorCode::Conflict);absent(directory_.get(),leaf_);
    }
    // A malformed primary never replaces the valid fallback backup. We replace
    // only the authenticated damaged primary, keeping that valid backup intact.
    constexpr unsigned NoReplace=1;
    const int result=candidates.primary.bytes && !candidates.primaryValid
        ? ::renameat(directory_.get(),(leaf_+".saving").c_str(),directory_.get(),leaf_.c_str())
        : static_cast<int>(::syscall(SYS_renameat2,directory_.get(),(leaf_+".saving").c_str(),directory_.get(),leaf_.c_str(),NoReplace));
    require(result == 0,"Cannot publish original control; recovery candidates retained",ErrorCode::Conflict);
    require(::fsync(directory_.get()) == 0,"Original control primary was published but directory sync failed",ErrorCode::Io);
    checkDirectories();auto published=readCandidates(directory_.get(),leaf_,kind_);checkCandidates(directory_.get(),leaf_,published);require(published.primary.bytes && *published.primary.bytes == replacement,"Original control changed after publication",ErrorCode::Conflict);remember(stamp(published.primary),stamp(published.backup));return {};
  });
}
} // namespace biglybtc::engine::detail
