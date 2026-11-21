// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "private_tag_rate_source.hpp"
#include "profile_prepared_tag_attachment.hpp"
#include "sdk_prepared_tag_attachment.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/extensions.hpp>
#include <cstring>
#include <mutex>
#include "manual_tag_store.hpp"
#include "tag_rate_capacity.hpp"
#include "sdk_shared_tag_budget.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <span>
#include <sys/stat.h>
#include <thread>
namespace biglybtc::engine {
namespace {
using storage::Error;using storage::ErrorCode;
Error unavailable(const char* text){return {ErrorCode::Unsupported,text};}
struct Stamp {
 bool present=false;std::uint64_t dev=0,ino=0,size=0,mode=0,links=0,uid=0;
 std::int64_t msec=0,mnano=0,csec=0,cnano=0;
 bool operator==(const Stamp&)const=default;
};
using Stamps=std::array<Stamp,3>;
std::expected<Stamps,Error> stamps(const storage::ProfileLease& lease){
 Stamps result;constexpr std::array names{"tag.config","tag.config.bak","tag.config.saving"};
 for(std::size_t i=0;i<names.size();++i){struct stat st{};
  if(::fstatat(lease.directoryHandle(),names[i],&st,AT_SYMLINK_NOFOLLOW)!=0){
   if(errno==ENOENT){continue;}return std::unexpected(unavailable("Cannot inspect saved tag rate source"));
  }
  if(!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<0){return std::unexpected(unavailable("Unsafe saved tag rate source"));}
  result[i]={true,static_cast<std::uint64_t>(st.st_dev),static_cast<std::uint64_t>(st.st_ino),static_cast<std::uint64_t>(st.st_size),static_cast<std::uint64_t>(st.st_mode),static_cast<std::uint64_t>(st.st_nlink),static_cast<std::uint64_t>(st.st_uid),st.st_mtim.tv_sec,st.st_mtim.tv_nsec,st.st_ctim.tv_sec,st.st_ctim.tv_nsec};
 }return result;
}
bool supported(core::SavedTagRateLimits r){return SdkTagRateGroup::supports(r);}
}
struct ProfilePreparedTagAttachment::Impl {
 enum class Phase{Fresh,Installed,Bound,Cancelled};
 std::weak_ptr<const std::byte> source;
 std::weak_ptr<libtorrent::aux::session_impl> session;
 libtorrent::info_hash_t expected;
 std::vector<std::uint64_t> tags;
 std::vector<std::unique_ptr<SdkPreparedTagAttachment>> tokens;
 mutable std::mutex mutex;Phase phase=Phase::Fresh;std::atomic_bool cancelled=false;
 libtorrent::torrent_handle handle;
 bool current()const noexcept{
  if(cancelled.load()||source.expired()||session.expired()){return false;}
  return std::ranges::all_of(tokens,[](const auto& token){return token->current();});
 }
 bool owns(libtorrent::torrent_handle target)const{
  const auto sdk=session.lock();const auto actual=target.native_handle();
  return sdk&&actual&& &actual->session()==static_cast<libtorrent::aux::session_interface*>(sdk.get());
 }
 void cancel(){cancelled.store(true);phase=Phase::Cancelled;}
};
ProfilePreparedTagAttachment::ProfilePreparedTagAttachment(std::shared_ptr<Impl> value):impl_(std::move(value)){}
ProfilePreparedTagAttachment::~ProfilePreparedTagAttachment(){
 if(!impl_){return;}
 try{std::lock_guard held(impl_->mutex);
  // An abandoned preparation must release its reservation on the next sync.
  // A completed constructor still belongs to the controller's binding census.
  if(impl_->phase!=Impl::Phase::Bound){impl_->cancel();}
 }catch(...){impl_->cancelled.store(true);}
}
bool ProfilePreparedTagAttachment::current()const noexcept{return impl_&&impl_->current();}
bool ProfilePreparedTagAttachment::boundTo(libtorrent::torrent_handle target)const noexcept{
 try{if(!current()){return false;}std::lock_guard held(impl_->mutex);
  return impl_->phase==Impl::Phase::Bound&&impl_->handle==target&&target.is_valid()
      &&std::ranges::all_of(impl_->tokens,[&](const auto& token){return token->boundTo(target);});
 }catch(...){return false;}
}
std::expected<void,storage::Error> ProfilePreparedTagAttachment::install(libtorrent::add_torrent_params& params){return installImpl(params,false);}
std::expected<void,storage::Error> ProfilePreparedTagAttachment::installForChecking(libtorrent::add_torrent_params& params){return installImpl(params,true);}
std::expected<void,storage::Error> ProfilePreparedTagAttachment::installImpl(libtorrent::add_torrent_params& params,bool checking){
 if(!current()){return std::unexpected(unavailable("Pending tag target source/cohort is no longer current"));}
 std::lock_guard held(impl_->mutex);
 const bool paused=bool(params.flags&libtorrent::torrent_flags::paused);
 if(impl_->phase!=Impl::Phase::Fresh
     ||(checking?(!params.ti||paused||!(params.flags&libtorrent::torrent_flags::stop_when_ready)):!paused)
     ||(params.flags&libtorrent::torrent_flags::auto_managed)
     ||(params.ti?params.ti->info_hashes():params.info_hashes)!=impl_->expected){
  return std::unexpected(unavailable("Pending tag target requires its exact paused add"));
 }
 try{auto staged=params;
  for(auto& token:impl_->tokens){if(!(checking?token->installForChecking(staged):token->install(staged))){impl_->cancel();return std::unexpected(unavailable("Cannot install the complete pending tag target"));}}
  staged.extensions.emplace_back([state=impl_](libtorrent::torrent_handle const& target,libtorrent::client_data_t)->std::shared_ptr<libtorrent::torrent_plugin>{
   std::lock_guard held(state->mutex);
   if(state->phase!=Impl::Phase::Installed||!state->current()||!state->owns(target)
       ||target.info_hashes()!=state->expected
       ||!std::ranges::all_of(state->tokens,[&](const auto& token){return token->boundTo(target);})){state->cancel();return {};}
   state->handle=target;state->phase=Impl::Phase::Bound;return {};
  });
  staged.flags|=libtorrent::torrent_flags::duplicate_is_error;
  params=std::move(staged);impl_->phase=Impl::Phase::Installed;return {};
 }catch(...){impl_->cancel();return std::unexpected(unavailable("Cannot retain pending tag target parameters"));}
}

struct PrivateTagRateSource::Data {
 struct Entry {std::uint64_t uid;core::SavedTagRateLimits limits;std::vector<core::TagRateReadiness> cohort;};
 std::weak_ptr<const storage::ProfileLease> lease;
 storage::ManualTagSnapshot snapshot;core::TorrentHash hash;
 libtorrent::info_hash_t expected;
 std::weak_ptr<libtorrent::aux::session_impl> session;
 std::weak_ptr<const std::byte> source;
 std::weak_ptr<const void> target;
 bool (*readTarget)(const void*)noexcept=nullptr;
 std::vector<Entry> entries;
 explicit Data(core::TorrentHash value):hash(std::move(value)){}
 bool current()const noexcept{
  const auto held=target.lock();
  if(lease.expired()||source.expired()||session.expired()||!held||!readTarget||!readTarget(held.get())){return false;}
  for(const auto& entry:entries){if(entry.cohort.empty()){return false;}
   for(const auto& witness:entry.cohort){if(!witness.currentFor(entry.uid,entry.limits)){return false;}}
  }return true;
 }
};
PrivateTagRateSource::PrivateTagRateSource(std::shared_ptr<const Data> value):data_(std::move(value)){}
bool PrivateTagRateSource::current()const noexcept{return data_&&data_->current();}
bool PrivateTagRateSource::matches(const storage::ProfileLease& lease,const core::TorrentHash& hash,
    const storage::ManualTagSnapshot& snapshot,const libtorrent::info_hash_t& expected,
    const libtorrent::session_handle& session)const noexcept{
 try{if(!current()||hash!=data_->hash||expected!=data_->expected||!snapshot.writable()
      ||!data_->snapshot.sameSource(snapshot)){return false;}
  const auto owner=data_->lease.lock();const auto actual=data_->session.lock();
  return owner&&owner->sharesOwnership(lease)&&actual&&actual==session.native_handle();
 }catch(...){return false;}
}

struct ProfileTagRates::Impl {
 std::shared_ptr<const storage::ProfileLease> lease;bool enabled=false;
 const std::thread::id worker=std::this_thread::get_id();
 struct Owner {
  libtorrent::session_handle session;
  std::map<std::uint64_t,std::unique_ptr<SdkTagRateGroup>> groups;
  bool held=false;
 };
 std::map<std::uint64_t,Owner> owners;
 std::uint64_t nextOwner=0;
 std::unique_ptr<SdkSharedQuotaScope> scope;
 std::map<std::uint64_t,std::shared_ptr<SdkSharedTagBudget>> budgets;
 std::optional<Stamps> source;bool dirty=true;
 storage::ManualTagSnapshot sourceSnapshot;
 std::shared_ptr<const std::byte> sourceIdentity=std::make_shared<const std::byte>(std::byte{});

 std::map<std::uint64_t,core::SavedTagRateLimits> limits;
 std::map<std::string,std::vector<std::uint64_t>> membership;
 std::map<std::string,Error> rejected;
 struct Binding{core::TorrentHash hash;libtorrent::torrent_handle handle;std::vector<std::uint64_t> tags;std::shared_ptr<ProfilePreparedTagAttachment::Impl> pending{};};
 std::map<std::pair<std::uint64_t,std::string>,Binding> bindings;
 std::expected<void,Error> current()const{
  if(worker!=std::this_thread::get_id()||!lease){return std::unexpected(unavailable("Saved tag rate owner is unavailable"));}return {};
 }
 std::expected<void,Error> read(){
  auto live=current();if(!live){return live;}if(!enabled){return {};}
  // This scope owns all filesystem/source work; callers do SDK work afterwards.
  auto gate=lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
  auto before=stamps(*lease);if(!before){return std::unexpected(before.error());}
  if(source&&*source==*before){return {};}
  auto store=storage::ManualTagStore::open(*lease);if(!store){return std::unexpected(store.error());}
  auto snapshot=(*store)->snapshot();if(!snapshot){return std::unexpected(snapshot.error());}
  if(!snapshot->writable()){return std::unexpected(unavailable("Saved tag rates require an intact primary source"));}
  const auto* saved=snapshot->tags();if(!saved||!saved->source){return std::unexpected(unavailable("Saved tag rate source is unavailable"));}
  std::map<std::uint64_t,core::SavedTagRateLimits> nextLimits;
  std::map<std::string,std::vector<std::uint64_t>> nextMembers;std::map<std::string,Error> nextRejected;
  const auto* manual=saved->source->find("3");
  for(const auto& tag:saved->tags){
   const auto& record=manual->children[tag.sourceIndex*2+1];auto rate=core::readSavedTagRateLimits(record);
   if(!rate){return std::unexpected(unavailable("Malformed saved tag rate settings"));}
   if(rate->unlimited()){continue;}
   if(!tag.id||!tag.uid||tag.idKey!=std::to_string(*tag.id)||tag.duplicateId||tag.membersMalformed){return std::unexpected(unavailable("Ambiguous saved rate-tag membership"));}
   const auto uid=static_cast<std::uint64_t>(*tag.uid);
   for(const auto& member:tag.members){
    if(!member.hash||!member.rawId||member.duplicate||member.hash->bytes().size()!=20||member.rawId->size()!=32){
     return std::unexpected(unavailable("Saved rate tags require unambiguous original 20-byte identities"));
    }
    const auto key=member.hash->hex();
    if(!supported(*rate)){nextRejected.emplace(key,unavailable("Saved tag rate is outside the supported SDK range"));continue;}
    auto& ids=nextMembers[key];ids.push_back(uid);
    if(ids.size()>storage::MaximumTagRateGroups){nextRejected.emplace(key,unavailable("Saved tag rate peer-class capacity exceeded"));}
   }
   if(supported(*rate)){nextLimits.emplace(uid,*rate);}
  }
  auto after=stamps(*lease);if(!after){return std::unexpected(after.error());}
  if(*before!=*after){return std::unexpected(unavailable("Saved tag rate source changed during capture"));}
  sourceIdentity=std::make_shared<const std::byte>(std::byte{});sourceSnapshot=*snapshot;
  dirty=true;source=*after;limits=std::move(nextLimits);membership=std::move(nextMembers);rejected=std::move(nextRejected);return {};
 }
 std::expected<std::vector<std::uint64_t>,Error> wanted(const core::TorrentHash& hash)const{
  if(const auto bad=rejected.find(hash.hex());bad!=rejected.end()){return std::unexpected(bad->second);}
  if(hash.bytes().size()!=20&&!limits.empty()){return std::unexpected(unavailable("Saved rate tags require the original canonical torrent identity"));}
  auto found=membership.find(hash.hex());return found==membership.end()?std::vector<std::uint64_t>{}:found->second;
 }
 void revokeSource(){sourceIdentity.reset();sourceIdentity=std::make_shared<const std::byte>(std::byte{});}
 std::optional<std::uint64_t> ownerId(libtorrent::session_handle session)const{
  const auto actual=session.native_handle();if(!actual){return {};}
  for(const auto& [id,owner]:owners){if(owner.session.native_handle()==actual){return id;}}
  return {};
 }
 void retire(std::uint64_t id){
  revokeSource();
  if(owners.at(id).session.native_handle()){owners.at(id).session.pause();}
  std::erase_if(bindings,[&](const auto& row){return row.first.first==id;});
  owners.erase(id);dirty=true;
 }
 std::expected<std::uint64_t,Error> admit(libtorrent::session_handle session){
  if(!session.native_handle()){return std::unexpected(unavailable("Saved tag rate SDK session has expired"));}
  // Expired sessions cannot retain payload or leave an address-reuse alias.
  for(auto it=owners.begin();it!=owners.end();){
   const auto id=it->first;++it;if(!owners.at(id).session.native_handle()){retire(id);}
  }
  if(auto existing=ownerId(session)){return *existing;}
  if(!owners.empty()&&!scope){return std::unexpected(unavailable("SDK cannot share saved tag budgets across sessions"));}
  if(owners.size()>=8||nextOwner==UINT64_MAX){return std::unexpected(unavailable("Saved tag rate session cohort is full"));}
  if(!nextOwner&&SdkSharedQuotaScope::supported()){
   auto created=SdkSharedQuotaScope::create();if(!created){return std::unexpected(unavailable("Cannot create the common saved tag budget owner"));}scope=std::move(*created);
  }
  const auto id=nextOwner+1;revokeSource();owners.emplace(id,Owner{session,{},false});nextOwner=id;dirty=true;return id;
 }
 std::expected<void,Error> ensure(std::span<const std::uint64_t> ids,Owner& owner){
  for(auto id:ids){const auto rate=limits.at(id);auto found=owner.groups.find(id);
   if(found==owner.groups.end()){
    std::shared_ptr<SdkSharedTagBudget> budget;
    if(scope){
     auto at=budgets.find(id);
     if(at==budgets.end()){
      auto acquired=scope->acquire(id,rate);if(!acquired){return std::unexpected(unavailable("Cannot acquire the common saved tag budget"));}
      at=budgets.emplace(id,std::move(*acquired)).first;
     }budget=at->second;
    }
    auto created=SdkTagRateGroup::create(owner.session,id,rate,std::move(budget));
    if(!created){return std::unexpected(unavailable("Cannot create the actual saved tag rate group"));}
    owner.groups.emplace(id,std::move(*created));
   }else if(!found->second->current()){return std::unexpected(unavailable("Saved tag rate SDK owner is no longer current"));}
  }return {};
 }
 void hold(){
  sourceIdentity.reset();
  for(auto& [id,owner]:owners){static_cast<void>(id);
   try{if(owner.session.native_handle()&&!owner.session.is_paused()){owner.session.pause();owner.held=true;}}catch(...){}
  }
 }
 void recover(){
  if(!sourceIdentity){revokeSource();}
  for(auto& [id,owner]:owners){static_cast<void>(id);if(owner.held){owner.session.resume();owner.held=false;}}
 }
 // The constructor and this controller serialize publication/cancellation.
 // No SDK wait occurs while the pending-state mutex is held.
 void settlePending(){
  for(auto it=bindings.begin();it!=bindings.end();){
   const auto pending=it->second.pending;if(!pending){++it;continue;}
   std::lock_guard held(pending->mutex);
   if(pending->phase==ProfilePreparedTagAttachment::Impl::Phase::Bound){
    it->second.handle=pending->handle;it->second.tags=pending->tags;
    it->second.pending.reset();++it;
   }else if(dirty||!pending->current()){
    pending->cancel();it=bindings.erase(it);
   }else{++it;}
  }
 }
 std::expected<void,Error> sync(libtorrent::session_handle session){
  auto live=current();if(!live){return live;}
  auto admitted=admit(session);if(!admitted){return std::unexpected(admitted.error());}
  const auto apply=[&]()->std::expected<void,Error>{
   auto loaded=read();if(!loaded){return loaded;}
   for(const auto& [ownerId,owner]:owners){static_cast<void>(ownerId);
    if(!owner.session.native_handle()){return std::unexpected(unavailable("Saved tag rate cohort contains an expired SDK session"));}
    for(const auto& [id,group]:owner.groups){static_cast<void>(id);if(!group->current()){return std::unexpected(unavailable("Saved tag rate SDK owner is no longer current"));}}
   }
   settlePending();
   if(!dirty){return {};}
   // Capture/validate all desired bindings before changing the shared budget.
   for(auto it=bindings.begin();it!=bindings.end();){
    if(!it->second.handle.is_valid()){if(it->second.pending){++it;continue;}it=bindings.erase(it);continue;}
    auto ids=wanted(it->second.hash);if(!ids){return std::unexpected(ids.error());}++it;
   }
   for(auto& [id,budget]:budgets){
    const auto desired=limits.find(id);const auto rate=desired==limits.end()?core::SavedTagRateLimits{}:desired->second;
    if(!budget->setLimits(rate)){return std::unexpected(unavailable("Cannot update the common saved tag budget"));}
   }
   for(auto& [key,binding]:bindings){
    auto ids=wanted(binding.hash);auto ready=ensure(*ids,owners.at(key.first));if(!ready){return ready;}
   }
   for(auto& [ownerId,owner]:owners){static_cast<void>(ownerId);
    for(auto& [id,group]:owner.groups){
     const auto desired=limits.find(id);const auto rate=desired==limits.end()?core::SavedTagRateLimits{}:desired->second;
     if(!group->setLimits(rate)){return std::unexpected(unavailable("Cannot update the actual saved tag rate group"));}
    }
   }
   for(auto& [key,binding]:bindings){auto& groups=owners.at(key.first).groups;auto ids=wanted(binding.hash);
    for(auto id:*ids){if(std::find(binding.tags.begin(),binding.tags.end(),id)==binding.tags.end()&&!groups.at(id)->attach(binding.handle)){return std::unexpected(unavailable("Cannot attach a saved tag rate group"));}}
    for(auto id:binding.tags){if(std::find(ids->begin(),ids->end(),id)==ids->end()&&!groups.at(id)->detach(binding.handle)){return std::unexpected(unavailable("Cannot detach a saved tag rate group"));}}
    binding.tags=std::move(*ids);
   }
   for(auto& [ownerId,owner]:owners){static_cast<void>(ownerId);std::erase_if(owner.groups,[&](const auto& row){return !limits.contains(row.first);});}
   // Retain at most the SDK scope bound of identity cells. A retired SDK
   // request may still hold one;re-adding its UID must reuse the same budget.
   dirty=false;return {};
  };
  try{auto result=apply();if(!result){hold();return result;}recover();return {};}
  catch(...){hold();return std::unexpected(unavailable("Saved tag rate cohort requires recovery"));}
 }

};
ProfileTagRates::ProfileTagRates(std::unique_ptr<Impl> p):impl_(std::move(p)){}
bool ProfileTagRates::matchesProfile(const storage::ProfileLease& lease,const storage::TagManagerStartupMode& mode)const noexcept{
 return impl_&&impl_->worker==std::this_thread::get_id()&&impl_->lease&&mode.matches(lease)&&impl_->lease->sharesOwnership(lease)&&impl_->enabled==mode.enabled();
}
ProfileTagRates::~ProfileTagRates(){if(impl_->lease){static_cast<void>(close());}}
std::expected<std::unique_ptr<ProfileTagRates>,storage::Error> ProfileTagRates::open(const storage::ProfileLease& lease,const storage::TagManagerStartupMode& mode){
 if(!mode.matches(lease)){return std::unexpected(unavailable("Tag startup mode belongs to another profile owner"));}
 auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
 auto state=std::make_unique<Impl>();state->lease=std::move(*retained);state->enabled=mode.enabled();
 auto result=std::unique_ptr<ProfileTagRates>(new ProfileTagRates(std::move(state)));auto loaded=result->impl_->read();if(!loaded){return std::unexpected(loaded.error());}return result;
}
std::expected<PrivateTagRateSource,storage::Error> ProfileTagRates::preparePrivateSource(
    const core::TorrentHash& hash,libtorrent::session_handle session,const ProfilePreparedTagAttachment& pending){
 auto live=impl_->current();if(!live){return std::unexpected(live.error());}
 if(!impl_->enabled||!SdkSharedQuotaScope::supported()||!pending.impl_){
  return std::unexpected(unavailable("Private tag sources require the actual common SDK quota runtime"));
 }
 const auto state=pending.impl_;const auto owner=impl_->ownerId(session);
 if(!owner||!impl_->scope||state->session.lock()!=session.native_handle()
     ||state->source.lock()!=impl_->sourceIdentity||!state->current()){
  return std::unexpected(unavailable("Private tag source requires its actual current SDK target"));
 }
 const auto reserved=impl_->bindings.find(std::pair{*owner,hash.hex()});
 if(reserved==impl_->bindings.end()||reserved->second.pending!=state){
  return std::unexpected(unavailable("Private tag source belongs to a different target reservation"));
 }
 {
  auto gate=impl_->lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
  if(!gate->observationsAllowed()){return std::unexpected(unavailable("Private tag source preparation requires an unlocked source turn"));}
 }
 auto synced=impl_->sync(session);if(!synced){return std::unexpected(synced.error());}
 auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}
 if(!impl_->sourceSnapshot.writable()||!state->current()
     ||state->source.lock()!=impl_->sourceIdentity||state->tags!=*ids){
  return std::unexpected(unavailable("Private tag source requires its current complete pending target"));
 }
 const auto key=std::pair{*owner,hash.hex()};const auto at=impl_->bindings.find(key);
 if(at==impl_->bindings.end()||at->second.pending!=state){return std::unexpected(unavailable("Private tag source target reservation changed"));}
 for(auto& [id,member]:impl_->owners){(void)id;auto ready=impl_->ensure(*ids,member);
  if(!ready){impl_->dirty=true;impl_->hold();return std::unexpected(ready.error());}
 }
 try{auto data=std::make_shared<PrivateTagRateSource::Data>(hash);
  data->lease=impl_->lease;data->snapshot=impl_->sourceSnapshot;data->expected=state->expected;
  data->session=session.native_handle();data->source=impl_->sourceIdentity;data->target=state;
  data->readTarget=[](const void* raw)noexcept{
   try{const auto& target=*static_cast<const ProfilePreparedTagAttachment::Impl*>(raw);
    if(!target.current()){return false;}std::lock_guard held(target.mutex);
    if(target.phase==ProfilePreparedTagAttachment::Impl::Phase::Fresh||target.phase==ProfilePreparedTagAttachment::Impl::Phase::Installed){return true;}
    return target.phase==ProfilePreparedTagAttachment::Impl::Phase::Bound&&target.handle.is_valid()
        &&std::ranges::all_of(target.tokens,[&](const auto& token){return token->boundTo(target.handle);});
   }catch(...){return false;}
  };
  for(auto uid:*ids){const auto limits=impl_->limits.at(uid);PrivateTagRateSource::Data::Entry entry{uid,limits,{}};
   for(const auto& [id,member]:impl_->owners){(void)id;auto witness=member.groups.at(uid)->readiness();
    if(!witness.currentFor(uid,limits)){impl_->hold();return std::unexpected(unavailable("Private tag source SDK cohort is not ready"));}
    entry.cohort.push_back(std::move(witness));
   }data->entries.push_back(std::move(entry));
  }
  if(!data->current()){return std::unexpected(unavailable("Private tag source lost current target readiness"));}
  return PrivateTagRateSource(std::move(data));
 }catch(...){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot capture private tag source readiness"));}
}

std::expected<std::unique_ptr<ProfilePreparedTagAttachment>,storage::Error> ProfileTagRates::prepareAttachment(
    const core::TorrentHash& hash,const libtorrent::info_hash_t& expected,libtorrent::session_handle session){
 auto live=impl_->current();if(!live){return std::unexpected(live.error());}
 {
  auto gate=impl_->lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
  if(!gate->observationsAllowed()){return std::unexpected(unavailable("Pending tag preparation requires an unlocked source turn"));}
 }
 const auto bytes=hash.bytes();
 const bool same=bytes.size()==20?expected.has_v1()&&std::memcmp(bytes.data(),expected.v1.data(),20)==0:
     bytes.size()==32&&expected.has_v2()&&std::memcmp(bytes.data(),expected.v2.data(),32)==0;
 if(!same){return std::unexpected(unavailable("Pending tag target canonical and full SDK identity differ"));}
 auto synced=impl_->sync(session);if(!synced){return std::unexpected(synced.error());}
 auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}
 const auto owner=*impl_->ownerId(session);const auto key=std::pair{owner,hash.hex()};
 if(impl_->bindings.contains(key)){return std::unexpected(unavailable("Duplicate pending saved tag target"));}
 auto ready=impl_->ensure(*ids,impl_->owners.at(owner));if(!ready){impl_->dirty=true;impl_->hold();return std::unexpected(ready.error());}
 try{auto state=std::make_shared<ProfilePreparedTagAttachment::Impl>();state->source=impl_->sourceIdentity;
  state->session=session.native_handle();state->expected=expected;state->tags=*ids;
  for(auto id:*ids){auto token=impl_->owners.at(owner).groups.at(id)->prepareAttachment(expected);
   if(!token){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot prepare the actual pending tag class"));}
   state->tokens.push_back(std::move(*token));
  }
  if(!state->current()){return std::unexpected(unavailable("Pending tag target lost current SDK/source"));}
  auto result=std::unique_ptr<ProfilePreparedTagAttachment>(new ProfilePreparedTagAttachment(state));
  impl_->bindings.emplace(key,Impl::Binding{hash,{},*ids,state});return result;
 }catch(...){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot retain the pending saved tag target"));}
}

std::expected<void,storage::Error> ProfileTagRates::prepare(const core::TorrentHash& hash,libtorrent::session_handle session){
 auto synced=impl_->sync(session);if(!synced){return synced;}
 const auto owner=*impl_->ownerId(session);const auto key=std::pair{owner,hash.hex()};
 if(const auto old=impl_->bindings.find(key);old!=impl_->bindings.end()&&!old->second.pending&&!old->second.handle.is_valid()){impl_->bindings.erase(old);}
 auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}auto ready=impl_->ensure(*ids,impl_->owners.at(owner));
 if(!ready){impl_->dirty=true;impl_->hold();}return ready;
}
std::expected<void,storage::Error> ProfileTagRates::attach(const core::TorrentHash& hash,libtorrent::torrent_handle handle){
 auto live=impl_->current();if(!live){return live;}
 if(impl_->owners.size()!=1){return std::unexpected(unavailable("Saved tag attachment requires its exact SDK session"));}
 return attach(hash,handle,impl_->owners.begin()->second.session);
}
std::expected<void,storage::Error> ProfileTagRates::attach(const core::TorrentHash& hash,libtorrent::torrent_handle handle,libtorrent::session_handle session){
 auto live=impl_->current();if(!live){return live;}auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}
 const auto owner=impl_->ownerId(session);
 if(!owner||!handle.is_valid()||session.find_torrent(handle.info_hashes().get_best())!=handle){return std::unexpected(unavailable("Actual paused torrent and its saved tag SDK session are required"));}
 const auto key=std::pair{*owner,hash.hex()};
 if(impl_->bindings.contains(key)){return std::unexpected(unavailable("Duplicate saved tag rate admission"));}
 auto& groups=impl_->owners.at(*owner).groups;
 // Publish binding storage first;partial attachment remains held for retry/close.
 auto [at,inserted]=impl_->bindings.emplace(key,Impl::Binding{hash,handle,{}});static_cast<void>(inserted);
 for(auto id:*ids){auto found=groups.find(id);if(found==groups.end()||!found->second->attach(handle)){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot attach saved rates before torrent resume"));}at->second.tags.push_back(id);}
 return {};
}
std::expected<void,storage::Error> ProfileTagRates::release(libtorrent::session_handle session){
 auto live=impl_->current();if(!live){return live;}if(const auto id=impl_->ownerId(session)){impl_->retire(*id);}return {};
}
std::expected<void,storage::Error> ProfileTagRates::synchronize(libtorrent::session_handle session){return impl_->sync(session);}
std::expected<storage::PreparedTagRateSource,storage::Error> ProfileTagRates::prepareSource(
    const storage::ProfileLease& lease,const storage::ManualTagStore& store,const storage::ManualTagSnapshot& source,
    std::span<const std::int32_t> tags,libtorrent::session_handle session){
 auto live=impl_->current();if(!live){return std::unexpected(live.error());}
 if(!impl_->enabled||!impl_->lease->sharesOwnership(lease)||!source.writable()
     ||!store.matchesSnapshotLineage(source,lease)||tags.empty()||tags.size()>32){return std::unexpected(unavailable("Saved rate source preparation requires the actual current store"));}
 auto synced=impl_->sync(session);if(!synced){return std::unexpected(synced.error());}
 if(!impl_->sourceSnapshot.sameSource(source)){return std::unexpected(unavailable("Saved rate source changed before SDK preparation"));}
 std::vector<std::uint64_t> ids;std::vector<storage::PreparedTagRateSource::Entry> entries;ids.reserve(tags.size());entries.reserve(tags.size());
 for(auto tag:tags){
  if(tag<0){return std::unexpected(unavailable("Invalid prepared rate tag"));}
  const auto uid=(std::uint64_t{3}<<32)|static_cast<std::uint32_t>(tag);
  if(std::find(ids.begin(),ids.end(),uid)!=ids.end()||!impl_->limits.contains(uid)){
   return std::unexpected(unavailable("Prepared rate tag is duplicate,unlimited or unsupported"));
  }ids.push_back(uid);
 }
 for(auto& [ownerId,owner]:impl_->owners){static_cast<void>(ownerId);auto ready=impl_->ensure(ids,owner);if(!ready){impl_->hold();return std::unexpected(ready.error());}}
 for(std::size_t i=0;i<tags.size();++i){const auto uid=ids[i];const auto rate=impl_->limits.at(uid);
  std::vector<core::TagRateReadiness> witnesses;witnesses.reserve(impl_->owners.size());
  for(const auto& [ownerId,owner]:impl_->owners){static_cast<void>(ownerId);auto witness=owner.groups.at(uid)->readiness();
   if(!witness.currentFor(uid,rate)){impl_->hold();return std::unexpected(unavailable("Actual SDK rate cohort is not ready"));}witnesses.push_back(std::move(witness));
  }
  entries.push_back({tags[i],uid,rate,std::move(witnesses)});
 }
 return storage::PreparedTagRateSource(impl_->lease,store,source,impl_->sourceIdentity,std::move(entries));
}
std::expected<void,storage::Error> ProfileTagRates::close(){auto live=impl_->current();if(!live){return live;}impl_->hold();if(impl_->scope){static_cast<void>(impl_->scope->close());}impl_->bindings.clear();impl_->owners.clear();impl_->budgets.clear();impl_->scope.reset();impl_->lease.reset();return {};}
}
