// SPDX-License-Identifier: GPL-2.0-or-later
#include "manual_tag_store.hpp"
#include "file_io.hpp"
#include <array>
#include <cerrno>
#include <new>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
constexpr std::array<const char*,3> Names{"tag.config","tag.config.bak","tag.config.saving"};
struct Stamp {
  std::uint64_t device, inode, size, mode, links, uid;
  std::int64_t modifiedSeconds, modifiedNanos, changedSeconds, changedNanos;
  bool operator==(const Stamp&) const = default;
};
Stamp stamp(const struct stat& value) {
  return {static_cast<std::uint64_t>(value.st_dev),static_cast<std::uint64_t>(value.st_ino),
    static_cast<std::uint64_t>(value.st_size),static_cast<std::uint64_t>(value.st_mode),
    static_cast<std::uint64_t>(value.st_nlink),static_cast<std::uint64_t>(value.st_uid),
    value.st_mtim.tv_sec,value.st_mtim.tv_nsec,value.st_ctim.tv_sec,value.st_ctim.tv_nsec};
}
struct Candidate {
  std::optional<Stamp> identity;
  std::optional<std::string> bytes;
  bool operator==(const Candidate&) const = default;
};
using Candidates = std::array<Candidate,3>;
Error conflict() { return {ErrorCode::Conflict,"Saved manual-tag snapshot or recovery candidates changed"}; }
Error cancelled() { return {ErrorCode::Cancelled,"Manual-tag operation cancelled before publication"}; }
Error uncertain() { return {ErrorCode::Conflict,"Manual-tag publication outcome uncertain; close and reopen before retry"}; }
Error editError(core::SavedTagEditError error) {
  switch (error) {
    case core::SavedTagEditError::Memory: return {ErrorCode::Io,"Insufficient memory for manual-tag edit"};
    case core::SavedTagEditError::Limit:
    case core::SavedTagEditError::IdExhausted: return {ErrorCode::Unsupported,"Manual-tag edit exceeds compatible bounded limits"};
    case core::SavedTagEditError::MissingTag:
    case core::SavedTagEditError::InvalidCounter: return conflict();
    default: return {ErrorCode::InvalidFormat,"Ambiguous or malformed manual-tag edit/document"};
  }
}
std::expected<Candidates,Error> candidates(const ProfileLease& lease,std::stop_token stop) {
  Candidates result;
  for (std::size_t i=0;i<Names.size();++i) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    struct stat before{};
    if (::fstatat(lease.directoryHandle(),Names[i],&before,AT_SYMLINK_NOFOLLOW)!=0) {
      if (errno==ENOENT) { continue; }
      return std::unexpected(Error{ErrorCode::Io,"Cannot inspect manual-tag candidate"});
    }
    if (!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_uid!=::geteuid()) {
      return std::unexpected(Error{ErrorCode::UnsafePath,"Manual-tag candidate must be an owned regular file with one link"});
    }
    auto bytes=detail::readFile(lease.directoryHandle(),Names[i],core::Limits{}.inputBytes);
    if (!bytes) { return std::unexpected(bytes.error()); }
    struct stat after{};
    if (!bytes->has_value()||::fstatat(lease.directoryHandle(),Names[i],&after,AT_SYMLINK_NOFOLLOW)!=0
        ||stamp(before)!=stamp(after)) { return std::unexpected(conflict()); }
    result[i]={stamp(after),std::move(*bytes)};
  }
  if (stop.stop_requested()) { return std::unexpected(cancelled()); }
  return result;
}
struct Loaded {
  Candidates candidates;
  std::optional<ConfigFile> file;
  core::SavedDownloadTags tags;
};
std::expected<Loaded,Error> load(const ProfileLease& lease,std::stop_token stop) {
  auto before=candidates(lease,stop);if(!before){return std::unexpected(before.error());}
  auto file=ConfigFile::load(lease,Names[0],core::Limits{});
  if(stop.stop_requested()){return std::unexpected(cancelled());}
  auto after=candidates(lease,stop);if(!after){return std::unexpected(after.error());}
  if(*before!=*after){return std::unexpected(conflict());}
  const bool missing=!(*before)[0].bytes&&!(*before)[1].bytes&&!(*before)[2].bytes;
  if(!file&&(!missing||file.error().code!=ErrorCode::Missing)){
    return std::unexpected(file.error().code==ErrorCode::Missing?Error{ErrorCode::InvalidFormat,"Existing manual-tag candidates are not complete compatible data"}:file.error());
  }
  const std::string_view raw=file?std::string_view(file->document().originalBytes()):std::string_view("de");
  auto projection=core::readSavedDownloadTags(raw);
  if(!projection){
    const auto code=projection.error()==core::SavedTagError::Memory?ErrorCode::Io:projection.error()==core::SavedTagError::Limit?ErrorCode::Unsupported:ErrorCode::InvalidFormat;
    return std::unexpected(Error{code,"Invalid or oversized saved manual-tag projection"});
  }
  if(stop.stop_requested()){return std::unexpected(cancelled());}
  Loaded result{std::move(*after),{},std::move(*projection)};
  if(file){
    const auto index=file->recoverySource()==RecoverySource::Primary?0U:file->recoverySource()==RecoverySource::Backup?1U:2U;
    if(!result.candidates[index].bytes||*result.candidates[index].bytes!=file->document().originalBytes()){return std::unexpected(conflict());}
    result.file.emplace(std::move(*file));
  }
  return result;
}
} // namespace
struct ManualTagSnapshot::Data {
  std::uint64_t generation=0;
  bool writable=false;
  std::optional<RecoverySource> recovery;
  Candidates preimage;
  core::SavedDownloadTags tags;
};
std::uint64_t ManualTagSnapshot::generation() const noexcept { return data_?data_->generation:0; }
bool ManualTagSnapshot::missing() const noexcept { return data_&&!data_->recovery; }
bool ManualTagSnapshot::writable() const noexcept { return data_&&data_->writable; }
bool ManualTagSnapshot::sameSource(const ManualTagSnapshot& other) const noexcept {
  return data_ && other.data_ && data_->preimage == other.data_->preimage;
}
std::optional<RecoverySource> ManualTagSnapshot::recoverySource() const noexcept { return data_?data_->recovery:std::nullopt; }
const core::SavedDownloadTags* ManualTagSnapshot::tags() const noexcept { return data_?&data_->tags:nullptr; }
std::optional<std::string_view> ManualTagSnapshot::rawBytes() const noexcept {
  if(!data_||!data_->recovery){return {};}
  return data_->tags.source->originalBytes();
}
ManualTagStore::~ManualTagStore() = default;
bool ManualTagStore::recoveryRequired() const noexcept {
  return std::this_thread::get_id()!=worker_||!lease_||poisoned_;
}
std::expected<void,Error> ManualTagStore::current() const {
  if(std::this_thread::get_id()!=worker_){return std::unexpected(Error{ErrorCode::Unsupported,"Manual-tag store requires its construction worker"});}
  if(!lease_){return std::unexpected(Error{ErrorCode::UnsafePath,"Manual-tag store is closed"});}
  if(poisoned_){return std::unexpected(uncertain());}
  return {};
}
std::expected<std::unique_ptr<ManualTagStore>,Error> ManualTagStore::open(const ProfileLease& lease,std::stop_token stop) {
  try {
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
    auto result=std::unique_ptr<ManualTagStore>(new ManualTagStore(std::move(*retained)));
    auto snapshot=result->reload(stop);if(!snapshot){return std::unexpected(snapshot.error());}
    return result;
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory for manual-tag store"});}
}
std::expected<ManualTagSnapshot,Error> ManualTagStore::snapshot() const {
  auto state=current();if(!state){return std::unexpected(state.error());}
  if(!latest_){return std::unexpected(conflict());}
  return ManualTagSnapshot(latest_);
}
std::expected<ManualTagSnapshot,Error> ManualTagStore::reload(std::stop_token stop) {
  try {
    auto state=current();if(!state){return std::unexpected(state.error());}
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    if(generation_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(conflict());}
    auto access=lease_->sourceAccess();if(!access){return std::unexpected(access.error());}
    auto identity=lease_->retain();if(!identity){return std::unexpected(identity.error());}
    auto loaded=load(*lease_,stop);if(!loaded){return std::unexpected(loaded.error());}
    auto next=std::make_shared<ManualTagSnapshot::Data>();next->generation=generation_+1;
    if(loaded->file){next->recovery=loaded->file->recoverySource();}
    next->writable=!loaded->file||(loaded->file->recoverySource()==RecoverySource::Primary&&!loaded->candidates[2].bytes);
    next->preimage=std::move(loaded->candidates);next->tags=std::move(loaded->tags);
    if(!latest_||latest_->preimage!=next->preimage){counter_.reset();}
    latest_=std::move(next);++generation_;return ManualTagSnapshot(latest_);
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory reading manual tags"});}
}
std::expected<ManualTagCommit,Error> ManualTagStore::apply(const ManualTagSnapshot& expected,
    const core::SavedTagEdit& requested,std::stop_token stop) {
  bool attempted=false;
  const auto poison=[&]{poisoned_=true;counter_.reset();latest_.reset();};
  try {
    auto state=current();if(!state){return std::unexpected(state.error());}
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    if(!expected.data_||expected.data_!=latest_||expected.generation()!=generation_){return std::unexpected(conflict());}
    if(generation_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(conflict());}
    auto access=lease_->sourceAccess();if(!access){return std::unexpected(access.error());}
    auto identity=lease_->retain();if(!identity){return std::unexpected(identity.error());}
    auto loaded=load(*lease_,stop);if(!loaded){return std::unexpected(loaded.error());}
    if(loaded->candidates!=latest_->preimage){return std::unexpected(conflict());}
    auto action=requested;
    if(auto* create=std::get_if<core::SavedTagCreate>(&action)){
      if(create->counter){return std::unexpected(Error{ErrorCode::Unsupported,"Manual-tag allocation counter is store-owned"});}
      create->counter=counter_;
    }
    auto edited=core::editSavedDownloadTags(loaded->tags.source->originalBytes(),action);
    if(!edited){return std::unexpected(editError(edited.error()));}
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    if(!edited->changed){return ManualTagCommit{ManualTagSnapshot(latest_),false,{},{}};}
    if(loaded->file&&(loaded->file->recoverySource()!=RecoverySource::Primary||loaded->candidates[2].bytes)){
      return std::unexpected(Error{ErrorCode::Conflict,"Manual-tag recovery candidates require explicit resolution before editing"});
    }
    const auto* manual=edited->value.source->find("3");
    if(!manual){return std::unexpected(Error{ErrorCode::InvalidFormat,"Manual-tag edit lost its selected type"});}
    const auto& bytes=edited->value.source->originalBytes();
    if(loaded->file){
      // Keep the loader's originalBytes(), damaged-primary and recovery
      // preimage. Assigning a newly parsed Document here would defeat saveCAS.
      auto replaced=loaded->file->document().replaceValue("3",bytes.substr(manual->begin,manual->end-manual->begin));
      if(!replaced||loaded->file->document().serialize()!=bytes){return std::unexpected(Error{ErrorCode::InvalidFormat,"Manual-tag patch changed unrelated raw data"});}
    }
    auto finalIdentity=lease_->retain();if(!finalIdentity){return std::unexpected(finalIdentity.error());}
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    attempted=true;
    if(loaded->file){
      auto saved=loaded->file->save(*lease_);if(!saved){poison();return std::unexpected(saved.error());}
    }else{
      auto saved=ConfigFile::createMissing(*lease_,Names[0],bytes,core::Limits{});if(!saved){poison();return std::unexpected(saved.error());}
    }
    // Publication is synchronous/noncancellable once attempted. Verify actual
    // reloaded Primary before claiming success or advancing allocator state.
    auto actual=load(*lease_,{});
    if(!actual||!actual->file||actual->file->recoverySource()!=RecoverySource::Primary
        ||actual->tags.source->originalBytes()!=bytes||actual->candidates[2].bytes){poison();return std::unexpected(uncertain());}
    auto next=std::make_shared<ManualTagSnapshot::Data>();next->generation=generation_+1;next->recovery=RecoverySource::Primary;next->writable=true;
    next->preimage=std::move(actual->candidates);next->tags=std::move(actual->tags);
    latest_=std::move(next);++generation_;
    if(edited->allocationCounter){counter_=edited->allocationCounter;}
    return ManualTagCommit{ManualTagSnapshot(latest_),true,edited->createdId,edited->createdUid};
  }catch(const std::bad_alloc&){if(attempted){poison();}return std::unexpected(Error{ErrorCode::Io,"Insufficient memory; manual-tag save may require explicit recovery"});}
}
std::expected<void,Error> ManualTagStore::close() {
  if(std::this_thread::get_id()!=worker_){return std::unexpected(Error{ErrorCode::Unsupported,"Manual-tag close requires its construction worker"});}
  latest_.reset();counter_.reset();lease_.reset();return {};
}
} // namespace biglybtc::storage
