// SPDX-License-Identifier: GPL-2.0-or-later
#include "../platform/posix_profile_files.hpp"
#include "manual_tag_store.hpp"
#include "prepared_tag_rate_source.hpp"
#include "tag_rate_capacity.hpp"
#include "biglybtc/tag_rate_limits.hpp"
#include "file_operations.hpp"
#include "durable_removal_receipt.hpp"
#include "original_torrent_identity.hpp"
#include "biglybtc/original_tag_order.hpp"
#include "file_io.hpp"
#include <array>
#include <biglybtc/download_state_names.hpp>
#include <dirent.h>
#include <algorithm>
#include <cerrno>
#include <new>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
// Prospective per-subject geometry,checked before any positive-rate CAS.
// Exact group fan-in bound matches ProfileTagRates;no target is saved into an
// unsupported rate cohort by the native assignment writer.
bool supportedRateSubject(const core::SavedDownloadTags& tags,const core::TorrentHash& hash){
 const auto* manual=tags.source?tags.source->find("3"):nullptr;std::size_t count=0;
 for(const auto& tag:tags.tags){if(std::ranges::find(tag.uniqueHashes,hash)==tag.uniqueHashes.end()){continue;}
  if(!manual){return false;}const auto& record=manual->children[tag.sourceIndex*2+1];auto rate=core::readSavedTagRateLimits(record);
  if(!rate){return false;}if(rate->unlimited()){continue;}
  const auto supported=[](std::int32_t value){return value>=-1&&value<2147483647;};
  if(!supported(rate->upload)||!supported(rate->download)||++count>MaximumTagRateGroups){return false;}
 }return true;
}
constexpr std::array<const char*,3> Names{"tag.config","tag.config.bak","tag.config.saving"};
constexpr std::array<const char*,3> SettingsNames{"biglybt.config","biglybt.config.bak","biglybt.config.saving"};
struct Stamp {
  std::uint64_t device, inode, size, mode, links, uid;
  std::int64_t modifiedSeconds, modifiedNanos, changedSeconds, changedNanos;
  bool operator==(const Stamp&) const = default;
};
Stamp stamp(const struct stat& value) {
  return {static_cast<std::uint64_t>(value.st_dev),static_cast<std::uint64_t>(value.st_ino),
    static_cast<std::uint64_t>(value.st_size),static_cast<std::uint64_t>(value.st_mode),
    static_cast<std::uint64_t>(value.st_nlink),static_cast<std::uint64_t>(value.st_uid),
    platform::modificationTime(value).tv_sec,platform::modificationTime(value).tv_nsec,platform::changeTime(value).tv_sec,platform::changeTime(value).tv_nsec};
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
    case core::SavedTagEditError::UnsupportedGroupSettings: return {ErrorCode::Unsupported,"Configured or ambiguous tag group effects are unavailable; saved membership was preserved"};
    case core::SavedTagEditError::Memory: return {ErrorCode::Io,"Insufficient memory for manual-tag edit"};
    case core::SavedTagEditError::Limit:
    case core::SavedTagEditError::IdExhausted: return {ErrorCode::Unsupported,"Manual-tag edit exceeds compatible bounded limits"};
    case core::SavedTagEditError::MissingTag:
    case core::SavedTagEditError::InvalidCounter: return conflict();
    default: return {ErrorCode::InvalidFormat,"Ambiguous or malformed manual-tag edit/document"};
  }
}
std::expected<Candidates,Error> candidates(const ProfileLease& lease,std::stop_token stop,const std::array<const char*,3>& names=Names) {
  Candidates result;
  for (std::size_t i=0;i<names.size();++i) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    struct stat before{};
    if (::fstatat(lease.directoryHandle(),names[i],&before,AT_SYMLINK_NOFOLLOW)!=0) {
      if (errno==ENOENT) { continue; }
      return std::unexpected(Error{ErrorCode::Io,"Cannot inspect manual-tag candidate"});
    }
    if (!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_uid!=::geteuid()) {
      return std::unexpected(Error{ErrorCode::UnsafePath,"Manual-tag candidate must be an owned regular file with one link"});
    }
    auto bytes=detail::readFile(lease.directoryHandle(),names[i],core::Limits{}.inputBytes);
    if (!bytes) { return std::unexpected(bytes.error()); }
    struct stat after{};
    if (!bytes->has_value()||::fstatat(lease.directoryHandle(),names[i],&after,AT_SYMLINK_NOFOLLOW)!=0
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
Error removalTextError(core::OriginalTagOrderError error){
  switch(error){
    case core::OriginalTagOrderError::InvalidUtf8:return {ErrorCode::InvalidFormat,"Removal tag text is malformed UTF-8"};
    case core::OriginalTagOrderError::ReferenceDifference:return {ErrorCode::Unsupported,"Removal tag Character properties differ between qualified Java17 and Java26"};
    case core::OriginalTagOrderError::Memory:return {ErrorCode::Io,"Cannot allocate bounded removal tag sort key"};
    case core::OriginalTagOrderError::Limit:return {ErrorCode::Unsupported,"Removal tag text exceeds original-order bounds"};
  }
  return {ErrorCode::Unsupported,"Removal tag original ordering is unavailable"};
}
// Ordinary original managers resolve selected20 IDs. Native full32 remains a
// distinct runtime key; never infer membership by blindly truncating it.
std::expected<void,Error> unambiguousFull32Inventory(const ProfileLease& lease,const core::TorrentHash& runtime,const core::TorrentHash& selected,bool removed){
  if(runtime.bytes().size()!=32){return {};}
  auto bytes=detail::readFile(lease.directoryHandle(),"downloads.config",64U*1024U*1024U);if(!bytes){return std::unexpected(bytes.error());}if(!*bytes){return std::unexpected(conflict());}
  auto document=core::Document::parse(**bytes,compatibleConfigLimits());if(!document){return std::unexpected(conflict());}auto library=core::readLibrary(*document,{10000,10000});if(!library||!library->issues.empty()){return std::unexpected(conflict());}
  std::size_t target=0;for(const auto& row:library->downloads){
    if(!row.hash){return std::unexpected(conflict());}if(*row.hash==runtime){++target;continue;}
    // Further full32 owners need their own source-bound selector evidence.
    // Refuse that unproved case rather than treating their prefixes as IDs.
    if(row.hash->bytes().size()!=20||*row.hash==selected){return std::unexpected(Error{ErrorCode::Unsupported,"Full32 taggable identity is ambiguous with another library owner"});}
  }
  if(target!=(removed?0U:1U)){return std::unexpected(conflict());}return {};
}
struct RemovalTag {std::string name;core::OriginalTagSortKey nameKey;std::optional<core::OriginalTagSortKey> group;};
int compareRemoval(const RemovalTag& a,const RemovalTag& b){
  if(bool(a.group)!=bool(b.group)){return a.group?-1:1;}
  if(a.group){const auto n=core::compareOriginalTagSortKeys(*a.group,*b.group);if(n){return n;}}
  return core::compareOriginalTagSortKeys(a.nameKey,b.nameKey);
}
std::expected<std::vector<std::string>,Error> removalNames(const core::SavedDownloadTags& source,const core::TorrentHash& hash){
  if(hash.bytes().size()!=20){return std::unexpected(Error{ErrorCode::Unsupported,"Removal tags need the original20-byte taggable identity"});}
  if(!source.source||!source.issues.empty()){return std::unexpected(Error{ErrorCode::InvalidFormat,"Incomplete manual namespace cannot supply removal tags"});}
  const auto* manual=source.source->find("3");if(!manual){return std::vector<std::string>{};}
  for(std::size_t n=0;n<manual->children.size();n+=2){const auto key=manual->children[n].bytes;if(!key.empty()&&static_cast<unsigned char>(key.front())>=0x80){return std::unexpected(Error{ErrorCode::Unsupported,"Unicode manual tag IDs need original Character classification"});}}
  std::vector<RemovalTag> selected;
  for(const auto& tag:source.tags){
    if(!tag.id||tag.idKey!=std::to_string(*tag.id)){return std::unexpected(Error{ErrorCode::Unsupported,"Manual tag ID alias cannot supply original canonical attributes"});}
    const auto& stored=manual->children.at(tag.sourceIndex*2+1);
    if(const auto* props=stored.find("p")){
      const auto* objects=stored.find("o");const auto count=objects?objects->children.size():0;
      if(props->type!=core::Node::Type::List||props->children.size()!=count){return std::unexpected(Error{ErrorCode::InvalidFormat,"Incomplete manual membership properties"});}
      for(const auto& property:props->children){if(property.type!=core::Node::Type::Dictionary){return std::unexpected(Error{ErrorCode::InvalidFormat,"Malformed manual membership property"});}const auto* added=property.find("a");if(added&&added->type!=core::Node::Type::Integer){return std::unexpected(Error{ErrorCode::InvalidFormat,"Malformed manual membership added time"});}}
    }
    if(std::find(tag.uniqueHashes.begin(),tag.uniqueHashes.end(),hash)==tag.uniqueHashes.end()){continue;}
    if(!tag.id||tag.duplicateId||tag.membersMalformed||!tag.rawName||tag.nameMayRequireLocalization){
      return std::unexpected(Error{ErrorCode::Unsupported,"Removal tag name/localization/namespace is unavailable"});
    }
    auto nameKey=core::originalTagSortKey(*tag.rawName);if(!nameKey){return std::unexpected(removalTextError(nameKey.error()));}
    const auto& record=manual->children.at(tag.sourceIndex*2+1);RemovalTag value{*tag.rawName,std::move(*nameKey),{}};
    if(const auto* config=record.find("c")){if(config->type!=core::Node::Type::Dictionary){return std::unexpected(Error{ErrorCode::InvalidFormat,"Malformed removal tag configuration"});}if(const auto* group=config->find("gr")){
      if(group->type!=core::Node::Type::Bytes){return std::unexpected(Error{ErrorCode::Unsupported,"Removal tag group needs supported original text"});}
      auto groupKey=core::originalTagSortKey(group->bytes);if(!groupKey){return std::unexpected(removalTextError(groupKey.error()));}value.group.emplace(std::move(*groupKey));
    }}
    if(selected.size()==256){return std::unexpected(Error{ErrorCode::Unsupported,"Removal tag list exceeds history bound"});}selected.push_back(std::move(value));
  }
  std::stable_sort(selected.begin(),selected.end(),[](const auto& a,const auto& b){return compareRemoval(a,b)<0;});
  for(std::size_t n=1;n<selected.size();++n){if(compareRemoval(selected[n-1],selected[n])==0&&selected[n-1].name!=selected[n].name){return std::unexpected(Error{ErrorCode::Unsupported,"Ambiguous original stable tag order needs a live manager order"});}}
  std::vector<std::string> names;names.reserve(selected.size());for(auto& tag:selected){names.push_back(std::move(tag.name));}return names;
}
} // namespace
struct ManualTagSnapshot::Data {
  std::uint64_t generation=0;
  bool writable=false;
  std::optional<RecoverySource> recovery;
  Candidates preimage;
  core::SavedDownloadTags tags;
};
struct ManualTagMembershipPlan::Data {
  ManualTagSnapshot base;
  std::shared_ptr<const core::SavedDownloadTags> view;
  std::vector<ManualTagMembershipEdit> operations;
};
const core::SavedDownloadTags* ManualTagMembershipPlan::tags()const noexcept{return data_?data_->view.get():nullptr;}
std::optional<std::string_view> ManualTagMembershipPlan::rawBytes()const noexcept{
  return data_&&data_->view&&data_->view->source?std::optional<std::string_view>{data_->view->source->originalBytes()}:std::nullopt;
}
std::size_t ManualTagMembershipPlan::edits()const noexcept{return data_?data_->operations.size():0;}
struct CapturedRemovalTags::Data {
  std::shared_ptr<const ProfileLease> lease;std::thread::id worker=std::this_thread::get_id();
  core::TorrentHash coreRuntimeHash;std::optional<OriginalTorrentIdentity> originalIdentity;Candidates preimage;std::vector<std::string> names;
};
CapturedRemovalTags::CapturedRemovalTags(std::unique_ptr<Data> data):data_(std::move(data)){}
CapturedRemovalTags::CapturedRemovalTags(CapturedRemovalTags&&)noexcept=default;
CapturedRemovalTags& CapturedRemovalTags::operator=(CapturedRemovalTags&&)noexcept=default;
CapturedRemovalTags::~CapturedRemovalTags()=default;
std::expected<void,Error> CapturedRemovalTags::current(const ProfileLease& lease,const core::TorrentHash& hash)const{
  if(!data_||data_->worker!=std::this_thread::get_id()||!data_->lease->sharesOwnership(lease)||data_->coreRuntimeHash!=hash){return std::unexpected(conflict());}
  auto identity=lease.retain();if(!identity){return std::unexpected(identity.error());}auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
  const auto now=candidates(lease,{});if(!now){return std::unexpected(now.error());}if(*now!=data_->preimage){return std::unexpected(conflict());}
  if(!data_->originalIdentity){return std::unexpected(conflict());}if(!data_->names.empty()){return unambiguousFull32Inventory(lease,hash,data_->originalIdentity->originalKey,true);}return {};
}
std::expected<void,Error> CapturedRemovalTags::matchesIdentity(const OriginalTorrentIdentity& actual)const{
  if(!data_||!data_->originalIdentity){return std::unexpected(conflict());}auto relation=compareOriginalTorrentIdentities(*data_->originalIdentity,actual);if(!relation){return std::unexpected(relation.error());}if(*relation!=OriginalIdentityRelation::SameSelected){return std::unexpected(conflict());}return {};
}
const std::vector<std::string>& CapturedRemovalTags::names()const{return data_->names;}
std::uint64_t ManualTagSnapshot::generation() const noexcept { return data_?data_->generation:0; }
bool ManualTagSnapshot::missing() const noexcept { return data_&&!data_->recovery; }
bool ManualTagSnapshot::writable() const noexcept { return data_&&data_->writable; }
bool ManualTagSnapshot::sameSource(const ManualTagSnapshot& other) const noexcept {
  return data_ && other.data_ && (data_==other.data_||data_->preimage==other.data_->preimage);
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
    if(!latest_||latest_->preimage!=next->preimage){counter_.reset();swarmCounter_.reset();}
    latest_=std::move(next);++generation_;return ManualTagSnapshot(latest_);
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory reading manual tags"});}
}
namespace {
// A custom original user bundle invalidates the stock-label assumption. Open
// a separate directory description; dup/readdir would move the lease offset.
bool stockTagBundlesOnly(const ProfileLease& lease)noexcept{
  const int fd=::openat(lease.directoryHandle(),".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(fd<0){return false;}DIR* entries=::fdopendir(fd);
  if(!entries){::close(fd);return false;}bool stock=true;unsigned count=0;errno=0;
  while(const auto* entry=::readdir(entries)){
    if(++count>4096){stock=false;break;}
    const std::string_view name(entry->d_name);
    if(name.starts_with("MessagesBundle")&&name.ends_with(".properties")){stock=false;break;}
  }
  if(errno){stock=false;}::closedir(entries);return stock;
}
}
std::expected<TagManagerStartupMode,Error> TagManagerStartupMode::capture(const ProfileLease& lease,std::stop_token stop){
  try{
    auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
    const auto before=candidates(lease,stop,SettingsNames);if(!before){return std::unexpected(before.error());}
    if((*before)[2].bytes||(!(*before)[0].bytes&&(*before)[1].bytes)){return std::unexpected(Error{ErrorCode::Conflict,"Tag manager startup mode needs primary settings or all absent"});}
    std::optional<std::string> locale;bool enabled=true;if((*before)[0].bytes){auto doc=core::Document::parse(*(*before)[0].bytes);if(!doc||doc->root().type!=core::Node::Type::Dictionary){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid tag manager startup settings"});}if(const auto* value=doc->find("tagmanager.enable")){if(value->type!=core::Node::Type::Integer){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid tag manager enable flag"});}enabled=static_cast<std::uint32_t>(value->integer)!=0;}
      if(const auto* value=doc->find("locale");value&&value->type==core::Node::Type::Bytes&&value->bytes.size()<=64
          &&core::tags::stockDownloadStateNames(value->bytes)&&stockTagBundlesOnly(lease)){locale=std::string(value->bytes);}
    }
    const auto after=candidates(lease,stop,SettingsNames);if(!after){return std::unexpected(after.error());}if(*before!=*after){return std::unexpected(conflict());}return TagManagerStartupMode(std::move(*retained),enabled,std::move(locale));
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot capture tag manager startup mode"});}
}
std::expected<bool,Error> ManualTagStore::historyRemovalModeAtOpen(const ProfileLease& lease,std::stop_token stop){
  auto mode=TagManagerStartupMode::capture(lease,stop);if(!mode){return std::unexpected(mode.error());}return mode->enabled();
}
std::expected<CapturedRemovalTags,Error> ManualTagStore::captureHistoryRemovalTags(const ProfileLease& lease,const PreparedFileOperation& prepared,bool enabledAtOpen,std::stop_token stop){
  try{
    auto state=current();if(!state){return std::unexpected(state.error());}if(!sharesOwnership(lease)||!latest_){return std::unexpected(conflict());}
    const auto& hash=prepared.preview().hash;auto binding=prepared.historyRemovalIdentity(lease);if(!binding){return std::unexpected(binding.error());}
    auto access=lease_->sourceAccess();if(!access){return std::unexpected(access.error());}auto identity=lease_->retain();if(!identity){return std::unexpected(identity.error());}
    auto loaded=load(*lease_,stop);if(!loaded){return std::unexpected(loaded.error());}if(loaded->candidates!=latest_->preimage||!latest_->writable){return std::unexpected(conflict());}
    auto names=enabledAtOpen?removalNames(loaded->tags,binding->originalKey):std::expected<std::vector<std::string>,Error>{std::vector<std::string>{}};if(!names){return std::unexpected(names.error());}
    const auto checked=candidates(*lease_,stop);if(!checked){return std::unexpected(checked.error());}if(*checked!=loaded->candidates){return std::unexpected(conflict());}
    if(!names->empty()){auto mapping=unambiguousFull32Inventory(lease,hash,binding->originalKey,false);if(!mapping){return std::unexpected(mapping.error());}}
    auto data=std::make_unique<CapturedRemovalTags::Data>();data->lease=std::move(*identity);data->coreRuntimeHash=hash;data->originalIdentity=std::move(*binding);data->preimage=std::move(loaded->candidates);data->names=std::move(*names);return CapturedRemovalTags(std::move(data));
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot capture pre-removal manual tags"});}
}
std::expected<ManualTagCommit,Error> ManualTagStore::observeSwarmTags(
    const ProfileLease& lease,core::SavedTagObserveSwarm observation,std::stop_token stop) {
  const auto active = current();
  if (!active) { return std::unexpected(active.error()); }
  if (!sharesOwnership(lease) || !latest_ || observation.counter) { return std::unexpected(conflict()); }
  observation.counter=swarmCounter_;
  const core::SavedTagEdit edit=std::move(observation);
  return applySequence(ManualTagSnapshot(latest_),{&edit,1},stop);
}
std::expected<void,Error> ManualTagStore::advanceCapturedRemovalTags(
    CapturedRemovalTags& captured, const ManualTagSnapshot& before, const ManualTagSnapshot& committed) {
  try {
    const auto state = current();
    if (!state) { return std::unexpected(state.error()); }
    auto* saved = captured.data_.get();
    if (!saved || saved->worker != worker_ || !saved->lease->sharesOwnership(*lease_)
        || saved->coreRuntimeHash.bytes().size() != 20 || !before.data_ || !latest_
        || committed.data_ != latest_ || generation_ == 0 || before.generation() != generation_ - 1
        || saved->preimage != before.data_->preimage || !latest_->writable) {
      return std::unexpected(conflict());
    }
    const auto access = lease_->sourceAccess();
    if (!access) { return std::unexpected(access.error()); }
    auto actual = candidates(*lease_, {});
    if (!actual) { return std::unexpected(actual.error()); }
    if (*actual != latest_->preimage) { return std::unexpected(conflict()); }
    // History retains the names captured before stop. Only its byte fence
    // advances through the worker's own exact committed membership change.
    saved->preimage = std::move(*actual);
    return {};
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io,"Cannot retain the removal tag source"});
  }
}
std::expected<core::TorrentHash,Error> ManualTagStore::removedMembershipIdentity(
    const ProfileLease& lease, const DurableRemovalReceipt& receipt, const core::TorrentHash& runtime) const {
  const auto state = current();
  if (!state) { return std::unexpected(state.error()); }
  if (!sharesOwnership(lease)) { return std::unexpected(conflict()); }
  return receipt.removedTagIdentity(lease,runtime);
}
std::expected<ManualTagCommit,Error> ManualTagStore::retireRemovedSubject(
    const ProfileLease& lease, const core::TorrentHash& runtime, const core::TorrentHash& original,
    std::stop_token stop) {
  try {
    auto state = current();
    if (!state) { return std::unexpected(state.error()); }
    if (!sharesOwnership(lease) || !latest_ || original.bytes().size() != 20
        || (runtime != original && runtime.bytes().size() != 32)) {
      return std::unexpected(conflict());
    }
    const auto access = lease.sourceAccess(true);
    if (!access) { return std::unexpected(access.error()); }
    const auto inventory = ConfigFile::load(lease, "downloads.config");
    if (!inventory || inventory->recoverySource() != RecoverySource::Primary) {
      return std::unexpected(conflict());
    }
    const auto subjects = core::readLibrary(inventory->document());
    if (!subjects || !subjects->issues.empty()
        || std::ranges::any_of(subjects->downloads, [&](const auto& row) {
          return !row.hash || row.hash->bytes().size() != 20 || row.hash == original;
        })
        || std::ranges::any_of(subjects->paused, [&](const auto& row) {
          return row.hash.bytes().size() != 20 || row.hash == original;
        })) {
      return std::unexpected(conflict());
    }
    const auto absent = [&](const std::string& path) {
      struct stat status{};
      return ::fstatat(lease.directoryHandle(),path.c_str(),&status,AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT;
    };
    const auto removedState = [&](const core::TorrentHash& hash) {
      const auto name = "active/" + hash.hex() + ".dat";
      return absent(name) && absent(name+".bak") && absent(name+".saving")
          && absent(name+".bak.saving") && absent(name+".import");
    };
    if (!removedState(runtime) || (runtime != original && !removedState(original))
        || !absent("downloads.config.saving") || !absent("downloads.config.bak.saving")
        || hasPendingFileOperation(lease)) {
      return std::unexpected(conflict());
    }
    const core::SavedTagEdit edit = core::SavedTagRetireSubject{original};
    return applySequence(ManualTagSnapshot(latest_), {&edit,1}, stop);
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io,"Cannot retire removed tag subject"});
  }
}
std::expected<ManualTagCommit,Error> ManualTagStore::setInternalTrackerSelection(
    const ProfileLease& lease,const ManualTagSnapshot& expected,
    const core::SavedTagSetInternalTrackerSelection& requested,std::stop_token stop){
  if(!matchesSnapshotLineage(expected,lease)){return std::unexpected(conflict());}
  const core::SavedTagEdit edit=requested;
  return applySequence(expected,{&edit,1},stop);
}
std::expected<ManualTagCommit,Error> ManualTagStore::apply(const ManualTagSnapshot& expected,
    const core::SavedTagEdit& requested,std::stop_token stop){
  const auto* create=std::get_if<core::SavedTagCreate>(&requested);
  if (std::holds_alternative<core::SavedTagRetireSubject>(requested)
      || std::holds_alternative<core::SavedTagObserveSwarm>(requested)
      || std::holds_alternative<core::SavedTagSetInternalTrackerSelection>(requested)
      || (create&&create->discovery)) {
    return std::unexpected(Error{ErrorCode::Unsupported,"Tag observation requires its actual lifecycle owner"});
  }
  return applySequence(expected,{&requested,1},stop);
}
std::expected<ManualTagCommit,Error> ManualTagStore::applyLifecycleMembership(const ManualTagSnapshot& expected,
    const ManualTagMembershipEdit& requested,std::stop_token stop){
  const auto edit=std::visit([](const auto& value)->core::SavedTagEdit{return value;},requested);
  return applySequence(expected,{&edit,1},stop,std::nullopt,true);
}
std::expected<ManualTagCommit,Error> ManualTagStore::applyLifecycleBatch(const ManualTagSnapshot& expected,
    std::span<const ManualTagMembershipEdit> requests,std::optional<std::string_view> prepared,std::stop_token stop,const PreparedTagRateSource* rates){
  if(requests.size()>32){return std::unexpected(Error{ErrorCode::Unsupported,"Assignment batch exceeds32 edits"});}
  try{std::vector<core::SavedTagEdit> edits;edits.reserve(requests.size());
    for(const auto& request:requests){std::visit([&](const auto& edit){edits.emplace_back(edit);},request);}
    return applySequence(expected,edits,stop,prepared,true,rates);
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare assignment membership batch"});}
}
std::span<const ManualTagMembershipEdit> ManualTagStore::membershipPlanOperations(const ManualTagMembershipPlan& plan)const noexcept{
  return plan.data_?std::span<const ManualTagMembershipEdit>{plan.data_->operations}:std::span<const ManualTagMembershipEdit>{};
}
std::expected<ManualTagCommit,Error> ManualTagStore::applyMembershipBatch(const ManualTagSnapshot& expected,
    std::span<const ManualTagMembershipEdit> requests,std::stop_token stop){
  if(requests.size()>32){return std::unexpected(Error{ErrorCode::Unsupported,"Membership batch exceeds32 edits"});}
  try{
    std::vector<core::SavedTagEdit> edits;edits.reserve(requests.size());
    for(const auto& request:requests){std::visit([&](const auto& edit){edits.emplace_back(edit);},request);}
    return applySequence(expected,edits,stop);
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare bounded membership batch"});}
}
std::expected<ManualTagCommit,Error> ManualTagStore::applySequence(const ManualTagSnapshot& expected,
    std::span<const core::SavedTagEdit> requests,std::stop_token stop,std::optional<std::string_view> prepared,bool lifecycleExecutor,const PreparedTagRateSource* rates,bool rateOnlyExecutor) {
  bool attempted=false;
  const auto poison=[&]{poisoned_=true;assignmentIdentity_.reset();counter_.reset();swarmCounter_.reset();latest_.reset();};
  try {
    auto state=current();if(!state){return std::unexpected(state.error());}
    auto allowed=requireNoPendingSubscriptionAcquisition(*lease_);if(!allowed){return std::unexpected(allowed.error());}
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    if(!expected.data_||expected.data_!=latest_||expected.generation()!=generation_){return std::unexpected(conflict());}
    if(generation_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(conflict());}
    auto access=lease_->sourceAccess();if(!access){return std::unexpected(access.error());}
    auto identity=lease_->retain();if(!identity){return std::unexpected(identity.error());}
    auto loaded=load(*lease_,stop);if(!loaded){return std::unexpected(loaded.error());}
    if(loaded->candidates!=latest_->preimage){return std::unexpected(conflict());}
    std::optional<core::SavedTagEditResult> edited;
    const auto* before=&loaded->tags;
    for(const auto& requested:requests){
      auto action=requested;
      if(auto* create=std::get_if<core::SavedTagCreate>(&action)){
        if(create->counter){return std::unexpected(Error{ErrorCode::Unsupported,"Manual-tag allocation counter is store-owned"});}
        create->counter=counter_;
      }
      auto candidate=core::editSavedDownloadTags(before->source->originalBytes(),action);
      if(!candidate){return std::unexpected(editError(candidate.error()));}
      if(stop.stop_requested()){return std::unexpected(cancelled());}
      if(!candidate->changed){continue;}
      const auto memberId=std::get_if<core::SavedTagAddMember>(&action)?std::optional{std::get<core::SavedTagAddMember>(action).id}
          :std::get_if<core::SavedTagRemoveMember>(&action)?std::optional{std::get<core::SavedTagRemoveMember>(action).id}:std::get_if<core::SavedTagMemberBatch>(&action)?std::optional{std::get<core::SavedTagMemberBatch>(action).id}:std::nullopt;
      if(const auto* rate=std::get_if<core::SavedTagSetRateLimit>(&action);rate&&rate->bytesPerSecond!=0){
        const auto selected=std::ranges::find_if(candidate->value.tags,[&](const auto& tag){return tag.id==rate->id;});
        if(selected==candidate->value.tags.end()||selected->membersMalformed||std::ranges::any_of(selected->members,[](const auto& member){return !member.hash||member.duplicate||member.hash->bytes().size()!=20||!member.rawId||member.rawId->size()!=32;})
            ||!std::ranges::all_of(selected->uniqueHashes,[&](const auto& hash){return supportedRateSubject(candidate->value,hash);})){
          return std::unexpected(Error{ErrorCode::Unsupported,"Saved rate settings exceed the supported member/rate cohort"});
        }
      }
      if(memberId){const auto* type=before->source->find("3");const auto* record=type?type->find(std::to_string(*memberId)):nullptr;
        const bool ordinary=record&&core::ordinarySavedTagMembershipEffects(*before,*record);
        const bool lifecycle=lifecycleExecutor&&record&&core::lifecycleSavedTagMembershipEffects(*before,*record);
        const bool rateReady=(lifecycleExecutor||rateOnlyExecutor)&&record&&rates&&rates->matches(*lease_,*this,expected,*memberId)
            &&(rateOnlyExecutor?core::rateOnlySavedTagMembershipEffects(*before,*record):core::rateReadyLifecycleSavedTagMembershipEffects(*before,*record));
        if(rateReady){
          const bool supported=std::visit([&](const auto& request){
            using Request=std::decay_t<decltype(request)>;
            if constexpr(std::is_same_v<Request,core::SavedTagAddMember>||std::is_same_v<Request,core::SavedTagRemoveMember>){return supportedRateSubject(candidate->value,request.hash);}
            else if constexpr(std::is_same_v<Request,core::SavedTagMemberBatch>){return std::ranges::all_of(request.hashes,[&](const auto& hash){return supportedRateSubject(candidate->value,hash);});}
            else{return false;}
          },action);
          if(!supported){return std::unexpected(Error{ErrorCode::Unsupported,"Assignment would exceed the supported saved tag rate cohort"});}
        }
        if(record&&!ordinary&&!lifecycle&&!rateReady){
          return std::unexpected(Error{ErrorCode::Unsupported,"Configured tag or group membership effects are unavailable; original source was preserved"});
        }
      }
      edited=std::move(*candidate);before=&edited->value;
    }
    if(prepared&&*prepared!=before->source->originalBytes()){
      return std::unexpected(Error{ErrorCode::Conflict,"Membership plan replay differs from its captured value"});
    }
    if(!edited||edited->value.source->originalBytes()==loaded->tags.source->originalBytes()){
      return ManualTagCommit{ManualTagSnapshot(latest_),false,{},{}};
    }
    if(loaded->file&&(loaded->file->recoverySource()!=RecoverySource::Primary||loaded->candidates[2].bytes)){
      return std::unexpected(Error{ErrorCode::Conflict,"Manual-tag recovery candidates require explicit resolution before editing"});
    }
    const bool swarm=requests.size()==1&&std::holds_alternative<core::SavedTagObserveSwarm>(requests.front());
    const bool internal=requests.size()==1&&
        std::holds_alternative<core::SavedTagSetInternalTrackerSelection>(requests.front());
    const std::string_view typeKey=swarm?"7":internal?"5":"3";
    const auto* type=edited->value.source->find(typeKey);
    if(!type){return std::unexpected(Error{ErrorCode::InvalidFormat,"Tag edit lost its selected type"});}
    const auto& bytes=edited->value.source->originalBytes();
    if(loaded->file){
      // Keep the loader's originalBytes(), damaged-primary and recovery
      // preimage. Assigning a newly parsed Document here would defeat saveCAS.
      auto replaced=loaded->file->document().replaceValue(typeKey,bytes.substr(type->begin,type->end-type->begin));
      if(!replaced||loaded->file->document().serialize()!=bytes){return std::unexpected(Error{ErrorCode::InvalidFormat,"Manual-tag patch changed unrelated raw data"});}
    }
    auto finalIdentity=lease_->retain();if(!finalIdentity){return std::unexpected(finalIdentity.error());}
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    allowed=requireNoPendingSubscriptionAcquisition(*lease_);if(!allowed){return std::unexpected(allowed.error());}
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
    if(edited->allocationCounter){(swarm?swarmCounter_:counter_)=edited->allocationCounter;}
    return ManualTagCommit{ManualTagSnapshot(latest_),true,edited->createdId,edited->createdUid};
  }catch(const std::bad_alloc&){if(attempted){poison();}return std::unexpected(Error{ErrorCode::Io,"Insufficient memory; manual-tag save may require explicit recovery"});}
}
std::expected<ManualTagMembershipPlan,Error> ManualTagStore::planMembership(const ManualTagSnapshot& base)const{
  try{
    auto active=current();if(!active){return std::unexpected(active.error());}
    if(!base.data_||base.data_!=latest_||base.generation()!=generation_||!base.writable()){
      return std::unexpected(conflict());
    }
    auto data=std::make_shared<ManualTagMembershipPlan::Data>();data->base=base;
    data->view=std::shared_ptr<const core::SavedDownloadTags>(base.data_,&base.data_->tags);
    return ManualTagMembershipPlan(std::move(data));
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot capture membership plan data"});}
}
bool ManualTagStore::matchesMembershipPlan(const ManualTagMembershipPlan& plan,const ProfileLease& lease,
    const ManualTagSnapshot& base)const noexcept{
  return plan.data_&&plan.data_->base.data_==base.data_&&matchesSnapshotLineage(base,lease);
}
std::expected<ManualTagMembershipPlan,Error> ManualTagStore::extendMembershipPlan(const ManualTagMembershipPlan& plan,
    const ManualTagMembershipEdit& operation,std::stop_token stop)const{
  try{
    auto active=current();if(!active){return std::unexpected(active.error());}
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    if(!plan.data_||!matchesMembershipPlan(plan,*lease_,plan.data_->base)){return std::unexpected(conflict());}
    if(plan.data_->operations.size()==32){return std::unexpected(Error{ErrorCode::Unsupported,"Membership plan exceeds32 changes"});}
    const auto& before=*plan.data_->view;
    const auto edit=std::visit([](const auto& value){return core::SavedTagEdit{value};},operation);
    auto changed=core::editSavedDownloadTags(before.source->originalBytes(),edit);
    if(!changed){return std::unexpected(editError(changed.error()));}if(!changed->changed){return plan;}
    const auto id=std::visit([](const auto& value){return value.id;},operation);
    const auto* type=before.source->find("3");const auto* record=type?type->find(std::to_string(id)):nullptr;
    if(!record||!core::rateReadyLifecycleSavedTagMembershipEffects(before,*record)){
      return std::unexpected(Error{ErrorCode::Unsupported,"Membership plan requires supported effect data"});
    }
    if(stop.stop_requested()){return std::unexpected(cancelled());}
    auto next=std::make_shared<ManualTagMembershipPlan::Data>(*plan.data_);
    next->view=std::make_shared<const core::SavedDownloadTags>(std::move(changed->value));next->operations.push_back(operation);
    return ManualTagMembershipPlan(std::move(next));
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot extend membership plan data"});}
}
std::expected<ManualTagCommit,Error> ManualTagStore::publishMembershipPlan(const ManualTagMembershipPlan& plan,std::stop_token stop){
  try{
    auto active=current();if(!active){return std::unexpected(active.error());}
    if(!plan.data_||!matchesMembershipPlan(plan,*lease_,plan.data_->base)||!plan.rawBytes()){return std::unexpected(conflict());}
    std::vector<core::SavedTagEdit> edits;edits.reserve(plan.data_->operations.size());
    for(const auto& operation:plan.data_->operations){std::visit([&](const auto& value){edits.emplace_back(value);},operation);}
    return applySequence(plan.data_->base,edits,stop,*plan.rawBytes());
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare membership plan publication"});}
}

std::expected<void,Error> ManualTagStore::close() {
  if(std::this_thread::get_id()!=worker_){return std::unexpected(Error{ErrorCode::Unsupported,"Manual-tag close requires its construction worker"});}
  assignmentIdentity_.reset();latest_.reset();counter_.reset();swarmCounter_.reset();lease_.reset();return {};
}
} // namespace biglybtc::storage
