// SPDX-License-Identifier: GPL-2.0-or-later
#include "category_store.hpp"
#include "profile_transaction_pending.hpp"
#include "profile_file_snapshot.hpp"
#include "torrent_state.hpp"
#include "file_operation_io.hpp"
#include <map>
#include <set>
#include <array>
#include <limits>
#include <new>
#include <thread>

namespace biglybtc::storage {
namespace {
using detail::StateHandle;using detail::StateDescriptor;using detail::StateStamp;
using detail::sameStateSnapshot;using detail::sameStateDirectory;
using detail::stateStamp;using detail::stateStampAt;using Clock=std::chrono::steady_clock;
std::unexpected<Error> fail(ErrorCode code,const char* text){return std::unexpected(Error{code,text});}
using Input=detail::ProfileFileSnapshot;
std::expected<Input,Error> capture(StateHandle directory,const std::string& name,bool read=false,
    std::size_t maximum=core::Limits{}.inputBytes){
  auto snapshot=detail::captureProfileFile(directory,name,read?std::optional{maximum}:std::nullopt);
#ifdef _WIN32
  // Category, torrent-state and configuration publishers rotate these names.
  if(snapshot){snapshot->releaseWitness();}
#endif
  return snapshot;
}
bool freshStamp(StateHandle directory,const std::string& name,const std::optional<StateStamp>& expected){
  const auto named=stateStampAt(directory,name);
  return named&&bool(*named)==bool(expected)&&(!expected||sameStateSnapshot(**named,*expected));
}
template<std::size_t N>bool equal(const std::array<Input,N>& a,const std::array<Input,N>& b){
  for(std::size_t i=0;i<N;++i){if(!a[i].matches(b[i])){return false;}}return true;
}
template<std::size_t N>std::expected<std::array<Input,N>,Error> inputs(StateHandle dir,const std::array<std::string,N>& names,bool firstRead=false,std::size_t maximum=core::Limits{}.inputBytes){std::array<Input,N> result;for(std::size_t i=0;i<N;++i){auto value=capture(dir,names[i],firstRead&&i==0,maximum);if(!value){return std::unexpected(value.error());}result[i]=std::move(*value);}return result;}
const std::array<std::string,4> CategoryNames{"categories.config","categories.config.new","categories.config.bak","categories.config.saving"};
using detail::CategoryJournalNames;
using detail::LegacyCategoryJournalNames;
using detail::categoryJournalPending;
const std::array<std::string,4> CategoryTagNames{"tag.config","tag.config.bak","tag.config.saving","tag.config.bak.saving"};
const std::array<std::string,3> InventoryNames{"downloads.config","downloads.config.bak","downloads.config.saving"};
std::array<std::string,8> targetNames(const core::TorrentHash& hash){const auto file=hash.hex()+".dat";return {file,file+".bak",file+".saving",file+".bak.saving","cache.dat","cache.dat.bak","cache.dat.saving","cache.dat.bak.saving"};}
std::expected<void,Error> selectedInventory(const ConfigFile& file,const core::TorrentHash& hash){
  if(file.recoverySource()!=RecoverySource::Primary){return fail(ErrorCode::Conflict,"Resolve inventory recovery before category assignment");}
  auto library=core::readLibrary(file.document());if(!library||!library->issues.empty()){return fail(ErrorCode::InvalidFormat,"Unambiguous current saved inventory required");}
  std::size_t count=0;for(const auto& row:library->downloads){if(row.hash&&*row.hash==hash){if(!row.persistent){return fail(ErrorCode::Unsupported,"Persistent saved target required for category assignment");}++count;}}
  if(count!=1){return fail(ErrorCode::Conflict,"Category target is absent or ambiguous in current inventory");}return {};
}
bool activeNamed(StateHandle root,StateHandle directory,const StateStamp& expected){
  const auto held=stateStamp(directory);
  const auto named=stateStampAt(root,"active");
  return held&&named&&*named&&sameStateDirectory(*held,expected)&&sameStateDirectory(**named,expected);
}
std::expected<StateStamp,Error> stageCategoryDefinition(StateHandle directory,std::string_view bytes,unsigned mode){
  const auto file=detail::stageStateFile(directory,CategoryNames[1],bytes,mode);
  if(!file){return std::unexpected(file.error());}
  const auto synced=detail::syncStateFile(file->get());
  if(!synced){return std::unexpected(synced.error());}
  const auto held=stateStamp(file->get());
  if(!held||!held->regular||!held->owned||held->links!=1||held->size<0
      ||static_cast<std::uint64_t>(held->size)!=bytes.size()||!freshStamp(directory,CategoryNames[1],*held)){
    return fail(ErrorCode::Conflict,"Held category .new identity changed");
  }
  return held;
}
std::expected<void,Error> publishCategoryDefinition(StateHandle directory,const StateStamp& staged,bool replace){
  const auto published=detail::publishStateFile(directory,CategoryNames[1],CategoryNames[0],
      detail::InvalidStateHandle,replace,&staged);
  if(!published){return published;}
  return detail::syncStateDirectory(directory);
}
std::expected<void,Error> budget(std::stop_token stop,Clock::time_point end){if(stop.stop_requested()){return fail(ErrorCode::Cancelled,"Category operation cancelled before publication");}if(Clock::now()>=end){return fail(ErrorCode::Cancelled,"Category operation deadline expired before publication");}return {};}
Error projectionError(core::SavedCategoryError value){return {value==core::SavedCategoryError::Memory?ErrorCode::Io:value==core::SavedCategoryError::Limit?ErrorCode::Unsupported:ErrorCode::InvalidFormat,"Invalid, ambiguous or oversized category projection/edit"};}
struct Definitions {std::array<Input,4> inputs;std::optional<ConfigFile> file;core::SavedCategories value;bool writable=false;};
std::expected<Definitions,Error> definitions(const ProfileLease& lease){
  auto before=inputs(lease.directoryHandle(),CategoryNames,true);if(!before){return std::unexpected(before.error());}
  const auto raw=(*before)[0].bytes?std::string_view(*(*before)[0].bytes):std::string_view("de");auto projected=core::readSavedCategories(raw);if(!projected){return std::unexpected(projectionError(projected.error()));}
  Definitions result{std::move(*before),{},std::move(*projected),false};
  if(result.inputs[0].stamp){auto file=ConfigFile::load(lease,CategoryNames[0],core::Limits{});if(!file){return std::unexpected(file.error());}if(file->recoverySource()!=RecoverySource::Primary||file->document().originalBytes()!=*result.inputs[0].bytes){return fail(ErrorCode::Conflict,"Category reader cannot substitute a backup or pending file");}result.file.emplace(std::move(*file));}
  auto after=inputs(lease.directoryHandle(),CategoryNames,true);if(!after){return std::unexpected(after.error());}if(!equal(result.inputs,*after)){return fail(ErrorCode::Conflict,"Category definition inputs changed");}
  result.writable=!result.inputs[1].stamp&&!result.inputs[3].stamp&&(result.inputs[0].stamp||!result.inputs[2].stamp);return result;
}
}
struct CategorySnapshot::Data {
  std::uint64_t generation=0;
  bool missing=false,writable=false;
  core::SavedCategories value;
  StateStamp directory{};
  std::array<std::optional<StateStamp>,CategoryNames.size()> definitions;
};
struct CategoryTargetSnapshot::Data {std::uint64_t generation=0;bool writable=false;core::TorrentHash hash;core::SavedCategoryAssignment value;Data(std::uint64_t gen,bool write,core::TorrentHash key,core::SavedCategoryAssignment projection):generation(gen),writable(write),hash(std::move(key)),value(std::move(projection)){};};
std::uint64_t CategorySnapshot::generation()const noexcept{return data_?data_->generation:0;}bool CategorySnapshot::missing()const noexcept{return data_&&data_->missing;}bool CategorySnapshot::writable()const noexcept{return data_&&data_->writable;}const core::SavedCategories* CategorySnapshot::categories()const noexcept{return data_?&data_->value:nullptr;}
bool CategorySnapshot::sameDefinitionSource(const CategorySnapshot& other)const noexcept{
  if (!data_||!other.data_||!sameStateDirectory(data_->directory,other.data_->directory)) { return false; }
  for (std::size_t index=0;index<data_->definitions.size();++index) {
    const auto& left=data_->definitions[index];
    const auto& right=other.data_->definitions[index];
    if (bool(left)!=bool(right)||(left&&!sameStateSnapshot(*left,*right))) { return false; }
  }
  return data_->value.source->originalBytes()==other.data_->value.source->originalBytes();
}
bool CategorySnapshot::matchesDefinitionSource(const ProfileLease& lease)const noexcept{
  try {
    if(!data_||!lease.valid()){return false;}
    const auto root=stateStamp(lease.directoryHandle());
    if(!root||!sameStateDirectory(data_->directory,*root)){return false;}
    for(std::size_t index=0;index<data_->definitions.size();++index){
      if(!freshStamp(lease.directoryHandle(),CategoryNames[index],data_->definitions[index])){return false;}
    }
    return true;
  }catch(...){return false;}
}
std::uint64_t CategoryTargetSnapshot::generation()const noexcept{return data_?data_->generation:0;}bool CategoryTargetSnapshot::writable()const noexcept{return data_&&data_->writable;}const core::TorrentHash* CategoryTargetSnapshot::hash()const noexcept{return data_?&data_->hash:nullptr;}const core::SavedCategoryAssignment* CategoryTargetSnapshot::assignment()const noexcept{return data_?&data_->value:nullptr;}
struct CategoryStore::Impl {
  std::shared_ptr<const ProfileLease> lease;const std::thread::id worker=std::this_thread::get_id();std::filesystem::path profile;StateStamp profileStamp{};bool poisoned=false,deleting=false,observing=false;std::uint64_t generation=0,targetGeneration=0;
  std::optional<Definitions> basis;std::shared_ptr<const CategorySnapshot::Data> latest;
  // Original assigns IDs to user categories in declaration order at startup,
  // then keeps them and allocates fresh IDs for each successful live create.
  using TagIds=std::map<std::string,std::int32_t,std::less<>>;
  std::optional<TagIds> tagIds{std::in_place};
  std::int64_t nextTagId=3;
  struct Target {std::shared_ptr<const CategoryTargetSnapshot::Data> snapshot;std::array<Input,3> inventory;std::array<Input,8> files;StateDescriptor active;StateStamp activeStamp{};TorrentStateStore store;TorrentStateFile state;Target(std::shared_ptr<const CategoryTargetSnapshot::Data> snap,std::array<Input,3> inv,std::array<Input,8> in,StateDescriptor dir,StateStamp info,TorrentStateStore owner,TorrentStateFile file):snapshot(std::move(snap)),inventory(std::move(inv)),files(std::move(in)),active(std::move(dir)),activeStamp(info),store(std::move(owner)),state(std::move(file)){};};
  std::optional<Target> target;
  std::expected<void,Error> current()const{if(std::this_thread::get_id()!=worker){return fail(ErrorCode::Unsupported,"Category store requires its construction worker");}if(observing){return fail(ErrorCode::Conflict,"Category observer cannot reenter the store");}if(!lease){return fail(ErrorCode::UnsafePath,"Category store is closed");}if(poisoned){return fail(ErrorCode::Conflict,"Category publication outcome uncertain; explicitly close/reopen, never replay");}return {};}
  std::expected<void,Error> roots()const {
    const auto retained=lease->retain();if(!retained){return std::unexpected(retained.error());}
    const auto held=stateStamp(lease->directoryHandle());
    const auto named=detail::openStatePathDirectory(profile);
    if(!held||!named||!held->owned||!sameStateDirectory(*held,profileStamp)){
      return fail(ErrorCode::Conflict,"Named category profile directory changed");
    }
    const auto observed=stateStamp(named->get());
    if(!observed||!sameStateDirectory(*held,*observed)){
      return fail(ErrorCode::Conflict,"Named category profile directory changed");
    }
    return {};
  }
  void poison()noexcept{poisoned=true;target.reset();basis.reset();latest.reset();}
  std::expected<Definitions,Error> fresh(const CategorySnapshot& expected){auto valid=roots();if(!valid){return std::unexpected(valid.error());}if(!basis||!latest||expected.data_!=latest||expected.generation()!=generation){return fail(ErrorCode::Conflict,"Category snapshot is stale or belongs to another store");}auto loaded=definitions(*lease);if(!loaded){return std::unexpected(loaded.error());}if(!equal(basis->inputs,loaded->inputs)){return fail(ErrorCode::Conflict,"Category definitions/candidates changed since presentation");}return loaded;}
  void publish(Definitions value,bool knownEdit=false){
    auto next=std::make_shared<CategorySnapshot::Data>();
    next->generation=generation+1;
    next->missing=!value.inputs[0].stamp;
    next->writable=value.writable&&!categoryJournalPending(lease->directoryHandle());
    next->value=value.value;
    next->directory=profileStamp;
    for (std::size_t index=0;index<value.inputs.size();++index) {
      next->definitions[index]=value.inputs[index].stamp;
    }

    if (tagIds && (!basis || knownEdit)) {
      TagIds updated;
      auto allocator=nextTagId;
      for (const auto& row : value.value.categories) {
        if (row.kind!=core::SavedCategoryKind::User) { continue; }
        const auto existing=tagIds->find(row.name);
        if (existing!=tagIds->end()) {
          updated.emplace(row.name,existing->second);
        } else if (allocator<=std::numeric_limits<std::int32_t>::max()) {
          updated.emplace(row.name,static_cast<std::int32_t>(allocator++));
        } else {
          tagIds.reset();
          break;
        }
      }
      if (tagIds) { *tagIds=std::move(updated);nextTagId=allocator; }
    } else if (tagIds && !equal(basis->inputs,value.inputs)) {
      tagIds.reset();
    }

    basis.emplace(std::move(value));
    latest=std::move(next);
    ++generation;
    target.reset();
  }
  std::expected<Target,Error> captureTarget(const core::TorrentHash& hash){
    auto valid=roots();if(!valid){return std::unexpected(valid.error());}
    auto inventory=inputs(lease->directoryHandle(),InventoryNames,true,compatibleConfigLimits().inputBytes);if(!inventory){return std::unexpected(inventory.error());}
    auto file=ConfigFile::load(*lease,InventoryNames[0]);if(!file){return std::unexpected(file.error());}valid=selectedInventory(*file,hash);if(!valid){return std::unexpected(valid.error());}
    if(!(*inventory)[0].bytes||file->document().originalBytes()!=*(*inventory)[0].bytes){return fail(ErrorCode::Conflict,"Inventory reader source changed");}
    auto active=detail::openStateDirectory(lease->directoryHandle(),"active");
    if(!active){return fail(ErrorCode::UnsafePath,"Owned named active-state directory required");}
    const auto directory=stateStamp(active->get());
    if(!directory||!directory->owned||!activeNamed(lease->directoryHandle(),active->get(),*directory)){
      return fail(ErrorCode::UnsafePath,"Owned named active-state directory required");
    }
    const auto names=targetNames(hash);auto before=inputs(active->get(),names);if(!before){return std::unexpected(before.error());}
    auto owner=TorrentStateStore::open(*lease);if(!owner){return std::unexpected(owner.error());}auto state=owner->loadAttributes(*lease,hash);if(!state){return std::unexpected(state.error());}
    // loadAttributes may overlay authoritative cached attributes. Serialize that
    // effective Document for projection, retaining state.original_ privately in
    // the real TorrentStateFile for its existing primary/cache CAS at save.
    auto projected=core::readSavedCategoryAssignment(basis->value.source->originalBytes(),state->document().serialize());if(!projected){return std::unexpected(projectionError(projected.error()));}
    auto after=inputs(active->get(),names);auto inventoryAfter=inputs(lease->directoryHandle(),InventoryNames,true,compatibleConfigLimits().inputBytes);
    if(!after){return std::unexpected(after.error());}if(!inventoryAfter){return std::unexpected(inventoryAfter.error());}
    if(!equal(*before,*after)||!equal(*inventory,*inventoryAfter)
        ||!activeNamed(lease->directoryHandle(),active->get(),*directory)){
      return fail(ErrorCode::Conflict,"Category target changed while reading");
    }
    const bool writable=basis->writable&&(!categoryJournalPending(lease->directoryHandle())||deleting)&&!state->recoveredBackup()&&(*before)[0].stamp&&!(*before)[2].stamp&&!(*before)[3].stamp&&!(*before)[6].stamp&&!(*before)[7].stamp&&!(*inventory)[2].stamp;
    auto data=std::make_shared<CategoryTargetSnapshot::Data>(targetGeneration+1,writable,hash,std::move(*projected));
    return Target(std::move(data),std::move(*inventory),std::move(*before),std::move(*active),*directory,std::move(*owner),std::move(*state));
  }
  std::expected<void,Error> targetCurrent(){
    if(!target){return fail(ErrorCode::Conflict,"No current category target");}auto valid=roots();if(!valid){return valid;}
    if(!activeNamed(lease->directoryHandle(),target->active.get(),target->activeStamp)){
      return fail(ErrorCode::Conflict,"Category active directory changed");
    }
    auto inv=inputs(lease->directoryHandle(),InventoryNames,true,compatibleConfigLimits().inputBytes);if(!inv){return std::unexpected(inv.error());}auto current=inputs(target->active.get(),targetNames(target->snapshot->hash));if(!current){return std::unexpected(current.error());}
    if(!equal(target->inventory,*inv)||!equal(target->files,*current)){return fail(ErrorCode::Conflict,"Category target/inventory/cache snapshot changed");}return {};
  }
};
CategoryStore::CategoryStore(std::unique_ptr<Impl> value):impl_(std::move(value)){}CategoryStore::~CategoryStore()=default;
bool CategoryStore::recoveryRequired()const noexcept{return std::this_thread::get_id()!=impl_->worker||!impl_->lease||impl_->poisoned||categoryJournalPending(impl_->lease->directoryHandle());}
std::expected<std::unique_ptr<CategoryStore>,Error> CategoryStore::open(const ProfileLease& lease,std::stop_token stop,Deadline end){
  try{auto allowed=budget(stop,end);if(!allowed){return std::unexpected(allowed.error());}auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}auto impl=std::make_unique<Impl>();impl->lease=std::move(*retained);auto path=detail::stateDirectoryPath(impl->lease->directoryHandle());if(!path){return std::unexpected(path.error());}impl->profile=std::move(*path);auto profileStamp=stateStamp(impl->lease->directoryHandle());if(!profileStamp){return std::unexpected(profileStamp.error());}impl->profileStamp=*profileStamp;auto result=std::unique_ptr<CategoryStore>(new CategoryStore(std::move(impl)));auto loaded=result->reload(stop,end);if(!loaded){return std::unexpected(loaded.error());}return result;}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory opening category store");}
}
std::expected<CategorySnapshot,Error> CategoryStore::snapshot()const{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}if(!impl_->latest){return fail(ErrorCode::Conflict,"No current category snapshot");}return CategorySnapshot(impl_->latest);}
std::expected<CategorySnapshot,Error> CategoryStore::reload(std::stop_token stop,Deadline end){
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(impl_->generation==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}valid=impl_->roots();if(!valid){return std::unexpected(valid.error());}auto loaded=definitions(*impl_->lease);if(!loaded){return std::unexpected(loaded.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}impl_->publish(std::move(*loaded));return CategorySnapshot(impl_->latest);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory reading category store");}
}
std::expected<CategoryCommit,Error> CategoryStore::create(const CategorySnapshot& expected,std::string name,std::stop_token stop,Deadline end){
  bool attempted=false;
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!valid){return std::unexpected(valid.error());}if(!impl_->deleting&&categoryJournalPending(impl_->lease->directoryHandle())){return fail(ErrorCode::Conflict,"Pending category deletion requires explicit recovery/review");}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(impl_->generation==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto loaded=impl_->fresh(expected);if(!loaded){return std::unexpected(loaded.error());}
    auto edit=core::editSavedCategories(loaded->value.source->originalBytes(),core::SavedCategoryCreate{std::move(name)});if(!edit){return std::unexpected(projectionError(edit.error()));}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(!edit->changed){return CategoryCommit{CategorySnapshot(impl_->latest),false,{}};}if(!loaded->writable){return fail(ErrorCode::Conflict,"Existing category pending/recovery inputs require explicit resolution");}
    const auto& bytes=edit->value.source->originalBytes();if(loaded->file){const auto* list=edit->value.source->find("categories");if(!list||!loaded->file->document().replaceValue("categories",bytes.substr(list->begin,list->end-list->begin))||loaded->file->document().serialize()!=bytes){return fail(ErrorCode::InvalidFormat,"Category patch changed unrelated raw data");}}
    auto writing=impl_->lease->sourceAccess(true);if(!writing){return std::unexpected(writing.error());}valid=impl_->roots();if(!valid){return std::unexpected(valid.error());}auto before=inputs(impl_->lease->directoryHandle(),CategoryNames,true);if(!before){return std::unexpected(before.error());}if(!equal(loaded->inputs,*before)){return fail(ErrorCode::Conflict,"Category inputs changed before publication");}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}
    valid=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!valid){return std::unexpected(valid.error());}
    attempted=true;
    const auto directory=impl_->lease->directoryHandle();
    const auto mode=loaded->inputs[0].stamp?detail::stateWriteMode(*loaded->inputs[0].stamp):0600U;
    const auto staged=stageCategoryDefinition(directory,bytes,mode);
    if(!staged){impl_->poison();return std::unexpected(staged.error());}
    for(const auto index:{0U,2U,3U}){
      auto fresh=capture(directory,CategoryNames[index],index==0);
      if(!fresh||!loaded->inputs[index].matches(*fresh)){
        impl_->poison();return fail(ErrorCode::Conflict,"Category preimages changed before rename");
      }
    }
    valid=impl_->roots();if(!valid){impl_->poison();return std::unexpected(valid.error());}
    valid=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!valid){impl_->poison();return std::unexpected(valid.error());}
    const auto published=publishCategoryDefinition(directory,*staged,loaded->inputs[0].stamp.has_value());
    if(!published){impl_->poison();return std::unexpected(published.error());}
    auto after=definitions(*impl_->lease);if(!after||!after->file||after->value.source->originalBytes()!=bytes||after->inputs[1].stamp){impl_->poison();return fail(ErrorCode::Conflict,"Category primary not verified after publication");}
    for(const auto index:{2U,3U}){if(!loaded->inputs[index].matches(after->inputs[index])){impl_->poison();return fail(ErrorCode::Conflict,"Unrelated category recovery input changed");}}
    if(!after->inputs[0].stamp||!detail::sameStateIdentity(*staged,*after->inputs[0].stamp)){
      impl_->poison();return fail(ErrorCode::Conflict,"Published category primary differs from staged file");
    }
    valid=impl_->roots();if(!valid){impl_->poison();return std::unexpected(valid.error());}impl_->publish(std::move(*after),true);return CategoryCommit{CategorySnapshot(impl_->latest),true,std::move(edit->createdName)};
  }catch(const std::bad_alloc&){if(attempted){impl_->poison();}return fail(ErrorCode::Io,"Insufficient memory; category save may require explicit recovery");}
}
#include "category_delete_journal.inc"
std::expected<CategoryTargetSnapshot,Error> CategoryStore::readTarget(const CategorySnapshot& expected,const core::TorrentHash& hash,std::stop_token stop,Deadline end){
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(impl_->targetGeneration==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category target generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto defs=impl_->fresh(expected);if(!defs){return std::unexpected(defs.error());}auto target=impl_->captureTarget(hash);if(!target){return std::unexpected(target.error());}auto after=impl_->fresh(expected);if(!after){return std::unexpected(after.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}impl_->target.emplace(std::move(*target));++impl_->targetGeneration;return CategoryTargetSnapshot(impl_->target->snapshot);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory reading category assignment");}
}
std::expected<CategoryAssignmentCommit,Error> CategoryStore::assign(const CategorySnapshot& defs,const CategoryTargetSnapshot& expected,const std::optional<std::string>& category,std::stop_token stop,Deadline end){
  bool attempted=false;
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!valid){return std::unexpected(valid.error());}if(!impl_->deleting&&categoryJournalPending(impl_->lease->directoryHandle())){return fail(ErrorCode::Conflict,"Pending category deletion requires explicit recovery/review");}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(!impl_->target||expected.data_!=impl_->target->snapshot||expected.generation()!=impl_->targetGeneration){return fail(ErrorCode::Conflict,"Category target is stale or belongs to another store");}if(impl_->targetGeneration==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category target generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto currentDefs=impl_->fresh(defs);if(!currentDefs){return std::unexpected(currentDefs.error());}valid=impl_->targetCurrent();if(!valid){return std::unexpected(valid.error());}
    auto edited=core::assignSavedCategory(currentDefs->value.source->originalBytes(),expected.assignment()->source->originalBytes(),category);if(!edited){return std::unexpected(projectionError(edited.error()));}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(!edited->changed){return CategoryAssignmentCommit{CategoryTargetSnapshot(impl_->target->snapshot),false};}if(!expected.writable()||!currentDefs->writable||!edited->encodedAttributes){return fail(ErrorCode::Conflict,"Resolve category/state recovery candidates before assignment");}
    auto writing=impl_->lease->sourceAccess(true);if(!writing){return std::unexpected(writing.error());}auto lastDefs=impl_->fresh(defs);if(!lastDefs){return std::unexpected(lastDefs.error());}valid=impl_->targetCurrent();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}
    valid=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!valid){return std::unexpected(valid.error());}const auto key=*expected.hash();attempted=true;auto saved=impl_->target->store.saveAttributes(*impl_->lease,impl_->target->state,*edited->encodedAttributes);if(!saved){impl_->poison();return std::unexpected(saved.error());}
    auto verifiedDefs=impl_->fresh(defs);if(!verifiedDefs){impl_->poison();return std::unexpected(verifiedDefs.error());}auto actual=impl_->captureTarget(key);if(!actual){impl_->poison();return std::unexpected(actual.error());}
    if(!equal(impl_->target->inventory,actual->inventory)||actual->snapshot->value.source->originalBytes()!=edited->value.source->originalBytes()||!actual->snapshot->writable){impl_->poison();return fail(ErrorCode::Conflict,"Category assignment not verified after state/cache publication");}
    // Actual committed save remains success after a late stop/deadline. No
    // callbacks or SDK waits occur while the filesystem source gate is held.
    impl_->target.emplace(std::move(*actual));++impl_->targetGeneration;return CategoryAssignmentCommit{CategoryTargetSnapshot(impl_->target->snapshot),true};
  }catch(const std::bad_alloc&){if(attempted){impl_->poison();}return fail(ErrorCode::Io,"Insufficient memory; category assignment may require explicit recovery");}
}
std::expected<void,Error> CategoryStore::close(){if(std::this_thread::get_id()!=impl_->worker){return fail(ErrorCode::Unsupported,"Category close requires construction worker");}if(impl_->deleting){return fail(ErrorCode::Conflict,"Cannot close during category deletion");}impl_->target.reset();impl_->basis.reset();impl_->latest.reset();impl_->lease.reset();return {};}
} // namespace biglybtc::storage
