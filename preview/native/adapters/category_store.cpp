// SPDX-License-Identifier: GPL-2.0-or-later
#include "category_store.hpp"
#include "file_io.hpp"
#include "torrent_state.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <new>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
using detail::Descriptor;using Clock=std::chrono::steady_clock;
std::unexpected<Error> fail(ErrorCode code,const char* text){return std::unexpected(Error{code,text});}
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
bool sameDirectory(const struct stat& a,const struct stat& b){return S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid;}
struct Input {Descriptor fd{-1};std::optional<struct stat> stamp;std::optional<std::string> bytes;};
std::expected<Input,Error> capture(int dir,const std::string& name,bool read=false,std::size_t maximum=core::Limits{}.inputBytes){
  Input result;struct stat named{};if(::fstatat(dir,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0){if(errno==ENOENT){return result;}return fail(ErrorCode::Io,"Cannot inspect category input");}
  if(!S_ISREG(named.st_mode)||named.st_nlink!=1||named.st_uid!=::geteuid()){return fail(ErrorCode::UnsafePath,"Category input must be owned regular single-link data");}
  result.fd=Descriptor(::openat(dir,name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));struct stat held{};
  if(result.fd.get()<0||::fstat(result.fd.get(),&held)!=0||!same(named,held)){return fail(ErrorCode::Conflict,"Category input changed while opening");}
  result.stamp=held;
  if(read){auto value=detail::readFile(dir,name,maximum);if(!value){return std::unexpected(value.error());}if(!*value){return fail(ErrorCode::Conflict,"Category input disappeared");}result.bytes=std::move(*value);}
  struct stat after{},again{};if(::fstat(result.fd.get(),&after)!=0||::fstatat(dir,name.c_str(),&again,AT_SYMLINK_NOFOLLOW)!=0||!same(after,held)||!same(again,held)){return fail(ErrorCode::Conflict,"Category input changed during capture");}return result;
}
bool held(const Input& input){if(!input.stamp){return input.fd.get()<0;}struct stat current{};return input.fd.get()>=0&&::fstat(input.fd.get(),&current)==0&&same(current,*input.stamp);}
bool equal(const Input& a,const Input& b){return bool(a.stamp)==bool(b.stamp)&&(!a.stamp||same(*a.stamp,*b.stamp))&&a.bytes==b.bytes;}
template<std::size_t N>bool equal(const std::array<Input,N>& a,const std::array<Input,N>& b){for(std::size_t i=0;i<N;++i){if(!held(a[i])||!equal(a[i],b[i])){return false;}}return true;}
template<std::size_t N>std::expected<std::array<Input,N>,Error> inputs(int dir,const std::array<std::string,N>& names,bool firstRead=false,std::size_t maximum=core::Limits{}.inputBytes){std::array<Input,N> result;for(std::size_t i=0;i<N;++i){auto value=capture(dir,names[i],firstRead&&i==0,maximum);if(!value){return std::unexpected(value.error());}result[i]=std::move(*value);}return result;}
const std::array<std::string,4> CategoryNames{"categories.config","categories.config.new","categories.config.bak","categories.config.saving"};
const std::array<std::string,3> InventoryNames{"downloads.config","downloads.config.bak","downloads.config.saving"};
std::array<std::string,8> targetNames(const core::TorrentHash& hash){const auto file=hash.hex()+".dat";return {file,file+".bak",file+".saving",file+".bak.saving","cache.dat","cache.dat.bak","cache.dat.saving","cache.dat.bak.saving"};}
std::expected<void,Error> selectedInventory(const ConfigFile& file,const core::TorrentHash& hash){
  if(file.recoverySource()!=RecoverySource::Primary){return fail(ErrorCode::Conflict,"Resolve inventory recovery before category assignment");}
  auto library=core::readLibrary(file.document());if(!library||!library->issues.empty()){return fail(ErrorCode::InvalidFormat,"Unambiguous current saved inventory required");}
  std::size_t count=0;for(const auto& row:library->downloads){if(row.hash&&*row.hash==hash){if(!row.persistent){return fail(ErrorCode::Unsupported,"Persistent saved target required for category assignment");}++count;}}
  if(count!=1){return fail(ErrorCode::Conflict,"Category target is absent or ambiguous in current inventory");}return {};
}
std::expected<std::string,Error> profilePath(int fd){std::array<char,4097> text{};const auto n=::readlink(("/proc/self/fd/"+std::to_string(fd)).c_str(),text.data(),text.size());if(n<=0||static_cast<std::size_t>(n)>=text.size()||text[0]!='/'){return fail(ErrorCode::UnsafePath,"Named category profile required");}return std::string(text.data(),static_cast<std::size_t>(n));}
bool profileNamed(const std::string& path,int heldFd){Descriptor dir(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));if(dir.get()<0||path.empty()||path[0]!='/'){return false;}for(std::size_t at=1;at<path.size();){const auto end=path.find('/',at);const auto part=path.substr(at,end==std::string::npos?path.size()-at:end-at);if(part.empty()||part=="."||part==".."){return false;}Descriptor next(::openat(dir.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(next.get()<0){return false;}dir=std::move(next);if(end==std::string::npos){break;}at=end+1;}struct stat a{},b{};return ::fstat(heldFd,&a)==0&&::fstat(dir.get(),&b)==0&&sameDirectory(a,b)&&a.st_uid==::geteuid();}
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
struct CategorySnapshot::Data {std::uint64_t generation=0;bool missing=false,writable=false;core::SavedCategories value;};
struct CategoryTargetSnapshot::Data {std::uint64_t generation=0;bool writable=false;core::TorrentHash hash;core::SavedCategoryAssignment value;Data(std::uint64_t gen,bool write,core::TorrentHash key,core::SavedCategoryAssignment projection):generation(gen),writable(write),hash(std::move(key)),value(std::move(projection)){};};
std::uint64_t CategorySnapshot::generation()const noexcept{return data_?data_->generation:0;}bool CategorySnapshot::missing()const noexcept{return data_&&data_->missing;}bool CategorySnapshot::writable()const noexcept{return data_&&data_->writable;}const core::SavedCategories* CategorySnapshot::categories()const noexcept{return data_?&data_->value:nullptr;}
std::uint64_t CategoryTargetSnapshot::generation()const noexcept{return data_?data_->generation:0;}bool CategoryTargetSnapshot::writable()const noexcept{return data_&&data_->writable;}const core::TorrentHash* CategoryTargetSnapshot::hash()const noexcept{return data_?&data_->hash:nullptr;}const core::SavedCategoryAssignment* CategoryTargetSnapshot::assignment()const noexcept{return data_?&data_->value:nullptr;}
struct CategoryStore::Impl {
  std::shared_ptr<const ProfileLease> lease;const std::thread::id worker=std::this_thread::get_id();std::string profile;struct stat profileStamp{};bool poisoned=false;std::uint64_t generation=0,targetGeneration=0;
  std::optional<Definitions> basis;std::shared_ptr<const CategorySnapshot::Data> latest;
  struct Target {std::shared_ptr<const CategoryTargetSnapshot::Data> snapshot;std::array<Input,3> inventory;std::array<Input,8> files;Descriptor active{-1};struct stat activeStamp{};TorrentStateStore store;TorrentStateFile state;Target(std::shared_ptr<const CategoryTargetSnapshot::Data> snap,std::array<Input,3> inv,std::array<Input,8> in,Descriptor dir,struct stat info,TorrentStateStore owner,TorrentStateFile file):snapshot(std::move(snap)),inventory(std::move(inv)),files(std::move(in)),active(std::move(dir)),activeStamp(info),store(std::move(owner)),state(std::move(file)){};};
  std::optional<Target> target;
  std::expected<void,Error> current()const{if(std::this_thread::get_id()!=worker){return fail(ErrorCode::Unsupported,"Category store requires its construction worker");}if(!lease){return fail(ErrorCode::UnsafePath,"Category store is closed");}if(poisoned){return fail(ErrorCode::Conflict,"Category publication outcome uncertain; explicitly close/reopen, never replay");}return {};}
  std::expected<void,Error> roots()const{auto retained=lease->retain();if(!retained){return std::unexpected(retained.error());}struct stat value{};if(::fstat(lease->directoryHandle(),&value)!=0||!sameDirectory(value,profileStamp)||!profileNamed(profile,lease->directoryHandle())){return fail(ErrorCode::Conflict,"Named category profile directory changed");}return {};}
  void poison()noexcept{poisoned=true;target.reset();basis.reset();latest.reset();}
  std::expected<Definitions,Error> fresh(const CategorySnapshot& expected){auto valid=roots();if(!valid){return std::unexpected(valid.error());}if(!basis||!latest||expected.data_!=latest||expected.generation()!=generation){return fail(ErrorCode::Conflict,"Category snapshot is stale or belongs to another store");}auto loaded=definitions(*lease);if(!loaded){return std::unexpected(loaded.error());}if(!equal(basis->inputs,loaded->inputs)){return fail(ErrorCode::Conflict,"Category definitions/candidates changed since presentation");}return loaded;}
  void publish(Definitions value){auto next=std::make_shared<CategorySnapshot::Data>();next->generation=generation+1;next->missing=!value.inputs[0].stamp;next->writable=value.writable;next->value=value.value;basis.emplace(std::move(value));latest=std::move(next);++generation;target.reset();}
  std::expected<Target,Error> captureTarget(const core::TorrentHash& hash){
    auto valid=roots();if(!valid){return std::unexpected(valid.error());}
    auto inventory=inputs(lease->directoryHandle(),InventoryNames,true,compatibleConfigLimits().inputBytes);if(!inventory){return std::unexpected(inventory.error());}
    auto file=ConfigFile::load(*lease,InventoryNames[0]);if(!file){return std::unexpected(file.error());}valid=selectedInventory(*file,hash);if(!valid){return std::unexpected(valid.error());}
    if(!(*inventory)[0].bytes||file->document().originalBytes()!=*(*inventory)[0].bytes){return fail(ErrorCode::Conflict,"Inventory reader source changed");}
    Descriptor active(::openat(lease->directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat directory{},named{};
    if(active.get()<0||::fstat(active.get(),&directory)!=0||directory.st_uid!=::geteuid()||::fstatat(lease->directoryHandle(),"active",&named,AT_SYMLINK_NOFOLLOW)!=0||!sameDirectory(directory,named)){return fail(ErrorCode::UnsafePath,"Owned named active-state directory required");}
    const auto names=targetNames(hash);auto before=inputs(active.get(),names);if(!before){return std::unexpected(before.error());}
    auto owner=TorrentStateStore::open(*lease);if(!owner){return std::unexpected(owner.error());}auto state=owner->loadAttributes(*lease,hash);if(!state){return std::unexpected(state.error());}
    // loadAttributes may overlay authoritative cached attributes. Serialize that
    // effective Document for projection, retaining state.original_ privately in
    // the real TorrentStateFile for its existing primary/cache CAS at save.
    auto projected=core::readSavedCategoryAssignment(basis->value.source->originalBytes(),state->document().serialize());if(!projected){return std::unexpected(projectionError(projected.error()));}
    auto after=inputs(active.get(),names);auto inventoryAfter=inputs(lease->directoryHandle(),InventoryNames,true,compatibleConfigLimits().inputBytes);
    if(!after){return std::unexpected(after.error());}if(!inventoryAfter){return std::unexpected(inventoryAfter.error());}
    if(!equal(*before,*after)||!equal(*inventory,*inventoryAfter)||::fstat(active.get(),&named)!=0||!sameDirectory(directory,named)||::fstatat(lease->directoryHandle(),"active",&named,AT_SYMLINK_NOFOLLOW)!=0||!sameDirectory(directory,named)){return fail(ErrorCode::Conflict,"Category target changed while reading");}
    const bool writable=basis->writable&&!state->recoveredBackup()&&(*before)[0].stamp&&!(*before)[2].stamp&&!(*before)[3].stamp&&!(*before)[6].stamp&&!(*before)[7].stamp&&!(*inventory)[2].stamp;
    auto data=std::make_shared<CategoryTargetSnapshot::Data>(targetGeneration+1,writable,hash,std::move(*projected));
    return Target(std::move(data),std::move(*inventory),std::move(*before),std::move(active),directory,std::move(*owner),std::move(*state));
  }
  std::expected<void,Error> targetCurrent(){
    if(!target){return fail(ErrorCode::Conflict,"No current category target");}auto valid=roots();if(!valid){return valid;}struct stat value{};
    if(::fstat(target->active.get(),&value)!=0||!sameDirectory(value,target->activeStamp)||::fstatat(lease->directoryHandle(),"active",&value,AT_SYMLINK_NOFOLLOW)!=0||!sameDirectory(value,target->activeStamp)){return fail(ErrorCode::Conflict,"Category active directory changed");}
    auto inv=inputs(lease->directoryHandle(),InventoryNames,true,compatibleConfigLimits().inputBytes);if(!inv){return std::unexpected(inv.error());}auto current=inputs(target->active.get(),targetNames(target->snapshot->hash));if(!current){return std::unexpected(current.error());}
    if(!equal(target->inventory,*inv)||!equal(target->files,*current)){return fail(ErrorCode::Conflict,"Category target/inventory/cache snapshot changed");}return {};
  }
};
CategoryStore::CategoryStore(std::unique_ptr<Impl> value):impl_(std::move(value)){}CategoryStore::~CategoryStore()=default;
bool CategoryStore::recoveryRequired()const noexcept{return std::this_thread::get_id()!=impl_->worker||!impl_->lease||impl_->poisoned;}
std::expected<std::unique_ptr<CategoryStore>,Error> CategoryStore::open(const ProfileLease& lease,std::stop_token stop,Deadline end){
  try{auto allowed=budget(stop,end);if(!allowed){return std::unexpected(allowed.error());}auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}auto impl=std::make_unique<Impl>();impl->lease=std::move(*retained);auto path=profilePath(impl->lease->directoryHandle());if(!path){return std::unexpected(path.error());}impl->profile=std::move(*path);if(::fstat(impl->lease->directoryHandle(),&impl->profileStamp)!=0){return fail(ErrorCode::Io,"Cannot identify category profile");}auto result=std::unique_ptr<CategoryStore>(new CategoryStore(std::move(impl)));auto loaded=result->reload(stop,end);if(!loaded){return std::unexpected(loaded.error());}return result;}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory opening category store");}
}
std::expected<CategorySnapshot,Error> CategoryStore::snapshot()const{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}if(!impl_->latest){return fail(ErrorCode::Conflict,"No current category snapshot");}return CategorySnapshot(impl_->latest);}
std::expected<CategorySnapshot,Error> CategoryStore::reload(std::stop_token stop,Deadline end){
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(impl_->generation==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}valid=impl_->roots();if(!valid){return std::unexpected(valid.error());}auto loaded=definitions(*impl_->lease);if(!loaded){return std::unexpected(loaded.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}impl_->publish(std::move(*loaded));return CategorySnapshot(impl_->latest);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory reading category store");}
}
std::expected<CategoryCommit,Error> CategoryStore::create(const CategorySnapshot& expected,std::string name,std::stop_token stop,Deadline end){
  bool attempted=false;
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(impl_->generation==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto loaded=impl_->fresh(expected);if(!loaded){return std::unexpected(loaded.error());}
    auto edit=core::editSavedCategories(loaded->value.source->originalBytes(),core::SavedCategoryCreate{std::move(name)});if(!edit){return std::unexpected(projectionError(edit.error()));}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(!edit->changed){return CategoryCommit{CategorySnapshot(impl_->latest),false,{}};}if(!loaded->writable){return fail(ErrorCode::Conflict,"Existing category pending/recovery inputs require explicit resolution");}
    const auto& bytes=edit->value.source->originalBytes();if(loaded->file){const auto* list=edit->value.source->find("categories");if(!list||!loaded->file->document().replaceValue("categories",bytes.substr(list->begin,list->end-list->begin))||loaded->file->document().serialize()!=bytes){return fail(ErrorCode::InvalidFormat,"Category patch changed unrelated raw data");}}
    auto writing=impl_->lease->sourceAccess(true);if(!writing){return std::unexpected(writing.error());}valid=impl_->roots();if(!valid){return std::unexpected(valid.error());}auto before=inputs(impl_->lease->directoryHandle(),CategoryNames,true);if(!before){return std::unexpected(before.error());}if(!equal(loaded->inputs,*before)){return fail(ErrorCode::Conflict,"Category inputs changed before publication");}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}
    attempted=true;const auto mode=loaded->inputs[0].stamp?static_cast<mode_t>(loaded->inputs[0].stamp->st_mode&0777):mode_t(0600);auto made=detail::writeExclusiveOwned(impl_->lease->directoryHandle(),CategoryNames[1],bytes,mode);if(!made){impl_->poison();return std::unexpected(made.error());}
    struct stat actual{},named{};if(::fstat(made->get(),&actual)!=0||!S_ISREG(actual.st_mode)||actual.st_nlink!=1||actual.st_uid!=::geteuid()||actual.st_size<0||static_cast<std::uint64_t>(actual.st_size)!=bytes.size()||::fstatat(impl_->lease->directoryHandle(),CategoryNames[1].c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0||!same(actual,named)){impl_->poison();return fail(ErrorCode::Conflict,"Held category .new identity changed");}
    for(const auto index:{0U,2U,3U}){auto fresh=capture(impl_->lease->directoryHandle(),CategoryNames[index],index==0);if(!fresh||!held(loaded->inputs[index])||!equal(loaded->inputs[index],*fresh)){impl_->poison();return fail(ErrorCode::Conflict,"Category preimages changed before rename");}}
    valid=impl_->roots();if(!valid){impl_->poison();return std::unexpected(valid.error());}
    const int renamed=loaded->inputs[0].stamp ? ::renameat(impl_->lease->directoryHandle(),CategoryNames[1].c_str(),impl_->lease->directoryHandle(),CategoryNames[0].c_str()) : ::renameat2(impl_->lease->directoryHandle(),CategoryNames[1].c_str(),impl_->lease->directoryHandle(),CategoryNames[0].c_str(),RENAME_NOREPLACE);
    if(renamed!=0||::fsync(impl_->lease->directoryHandle())!=0){impl_->poison();return fail(ErrorCode::Io,"Category publication uncertain; preserve candidates and explicitly reopen");}
    auto after=definitions(*impl_->lease);if(!after||!after->file||after->value.source->originalBytes()!=bytes||after->inputs[1].stamp){impl_->poison();return fail(ErrorCode::Conflict,"Category primary not verified after publication");}
    for(const auto index:{2U,3U}){if(!held(loaded->inputs[index])||!equal(loaded->inputs[index],after->inputs[index])){impl_->poison();return fail(ErrorCode::Conflict,"Unrelated category recovery input changed");}}
    if(::fstat(made->get(),&actual)!=0||!after->inputs[0].stamp||!same(actual,*after->inputs[0].stamp)){impl_->poison();return fail(ErrorCode::Conflict,"Published category primary differs from held created file");}
    valid=impl_->roots();if(!valid){impl_->poison();return std::unexpected(valid.error());}impl_->publish(std::move(*after));return CategoryCommit{CategorySnapshot(impl_->latest),true,std::move(edit->createdName)};
  }catch(const std::bad_alloc&){if(attempted){impl_->poison();}return fail(ErrorCode::Io,"Insufficient memory; category save may require explicit recovery");}
}
std::expected<CategoryCommit,Error> CategoryStore::remove(const CategorySnapshot& expected,std::string_view){auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}if(!impl_->latest||expected.data_!=impl_->latest){return fail(ErrorCode::Conflict,"Category deletion snapshot is stale");}return fail(ErrorCode::Unsupported,"Category deletion requires a reviewed inventory-complete multi-file journal; no changes made");}
std::expected<CategoryTargetSnapshot,Error> CategoryStore::readTarget(const CategorySnapshot& expected,const core::TorrentHash& hash,std::stop_token stop,Deadline end){
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(impl_->targetGeneration==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category target generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto defs=impl_->fresh(expected);if(!defs){return std::unexpected(defs.error());}auto target=impl_->captureTarget(hash);if(!target){return std::unexpected(target.error());}auto after=impl_->fresh(expected);if(!after){return std::unexpected(after.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}impl_->target.emplace(std::move(*target));++impl_->targetGeneration;return CategoryTargetSnapshot(impl_->target->snapshot);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory reading category assignment");}
}
std::expected<CategoryAssignmentCommit,Error> CategoryStore::assign(const CategorySnapshot& defs,const CategoryTargetSnapshot& expected,const std::optional<std::string>& category,std::stop_token stop,Deadline end){
  bool attempted=false;
  try{auto valid=impl_->current();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(!impl_->target||expected.data_!=impl_->target->snapshot||expected.generation()!=impl_->targetGeneration){return fail(ErrorCode::Conflict,"Category target is stale or belongs to another store");}if(impl_->targetGeneration==std::numeric_limits<std::uint64_t>::max()){return fail(ErrorCode::Conflict,"Category target generations exhausted");}auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto currentDefs=impl_->fresh(defs);if(!currentDefs){return std::unexpected(currentDefs.error());}valid=impl_->targetCurrent();if(!valid){return std::unexpected(valid.error());}
    auto edited=core::assignSavedCategory(currentDefs->value.source->originalBytes(),expected.assignment()->source->originalBytes(),category);if(!edited){return std::unexpected(projectionError(edited.error()));}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}if(!edited->changed){return CategoryAssignmentCommit{CategoryTargetSnapshot(impl_->target->snapshot),false};}if(!expected.writable()||!currentDefs->writable||!edited->encodedAttributes){return fail(ErrorCode::Conflict,"Resolve category/state recovery candidates before assignment");}
    auto writing=impl_->lease->sourceAccess(true);if(!writing){return std::unexpected(writing.error());}auto lastDefs=impl_->fresh(defs);if(!lastDefs){return std::unexpected(lastDefs.error());}valid=impl_->targetCurrent();if(!valid){return std::unexpected(valid.error());}valid=budget(stop,end);if(!valid){return std::unexpected(valid.error());}
    const auto key=*expected.hash();attempted=true;auto saved=impl_->target->store.saveAttributes(*impl_->lease,impl_->target->state,*edited->encodedAttributes);if(!saved){impl_->poison();return std::unexpected(saved.error());}
    auto verifiedDefs=impl_->fresh(defs);if(!verifiedDefs){impl_->poison();return std::unexpected(verifiedDefs.error());}auto actual=impl_->captureTarget(key);if(!actual){impl_->poison();return std::unexpected(actual.error());}
    if(!equal(impl_->target->inventory,actual->inventory)||actual->snapshot->value.source->originalBytes()!=edited->value.source->originalBytes()||!actual->snapshot->writable){impl_->poison();return fail(ErrorCode::Conflict,"Category assignment not verified after state/cache publication");}
    // Actual committed save remains success after a late stop/deadline. No
    // callbacks or SDK waits occur while the filesystem source gate is held.
    impl_->target.emplace(std::move(*actual));++impl_->targetGeneration;return CategoryAssignmentCommit{CategoryTargetSnapshot(impl_->target->snapshot),true};
  }catch(const std::bad_alloc&){if(attempted){impl_->poison();}return fail(ErrorCode::Io,"Insufficient memory; category assignment may require explicit recovery");}
}
std::expected<void,Error> CategoryStore::close(){if(std::this_thread::get_id()!=impl_->worker){return fail(ErrorCode::Unsupported,"Category close requires construction worker");}impl_->target.reset();impl_->basis.reset();impl_->latest.reset();impl_->lease.reset();return {};}
} // namespace biglybtc::storage
