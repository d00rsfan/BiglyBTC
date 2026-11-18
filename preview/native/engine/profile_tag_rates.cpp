// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "manual_tag_store.hpp"
#include "tag_rate_capacity.hpp"
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
bool equal(core::SavedTagRateLimits a,core::SavedTagRateLimits b){return a.upload==b.upload&&a.download==b.download;}
}
struct ProfileTagRates::Impl {
 std::shared_ptr<const storage::ProfileLease> lease;bool enabled=false;
 const std::thread::id worker=std::this_thread::get_id();
 std::optional<libtorrent::session_handle> sessionOwner;
 std::optional<Stamps> source;bool dirty=true;
 storage::ManualTagSnapshot sourceSnapshot;
 std::shared_ptr<const std::byte> sourceIdentity=std::make_shared<const std::byte>(std::byte{});

 std::map<std::uint64_t,core::SavedTagRateLimits> limits;
 std::map<std::string,std::vector<std::uint64_t>> membership;
 std::map<std::string,Error> rejected;
 std::map<std::uint64_t,std::unique_ptr<SdkTagRateGroup>> groups;
 struct Binding{core::TorrentHash hash;libtorrent::torrent_handle handle;std::vector<std::uint64_t> tags;};
 std::map<std::string,Binding> bindings;
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
 std::expected<void,Error> ensure(std::span<const std::uint64_t> ids,libtorrent::session_handle session){
  for(auto id:ids){const auto rate=limits.at(id);auto found=groups.find(id);
   if(found==groups.end()){
    auto created=SdkTagRateGroup::create(session,id,rate);if(!created){return std::unexpected(unavailable("Cannot create the actual saved tag rate group"));}
    groups.emplace(id,std::move(*created));
   }else if(!found->second->current()){return std::unexpected(unavailable("Saved tag rate SDK owner is no longer current"));}
  }return {};
 }
 std::map<std::uint64_t,core::SavedTagRateLimits> applied;
 std::expected<void,Error> sync(libtorrent::session_handle session){
  auto live=current();if(!live){return live;}
  const auto actual=session.native_handle();
  if(!actual){return std::unexpected(unavailable("Saved tag rate SDK session has expired"));}
  if(sessionOwner&&sessionOwner->native_handle()!=actual){return std::unexpected(unavailable("Saved tag rates cannot cross SDK owners"));}
  sessionOwner=session;
  auto loaded=read();if(!loaded){return loaded;}
  if(!dirty){
   for(const auto& [id,group]:groups){static_cast<void>(id);if(!group->current()){return std::unexpected(unavailable("Saved tag rate SDK owner is no longer current"));}}
   return {}; // No unchanged-source traversal of the already admitted library.
  }

  // Validate/create every current target's new classes before removing any old.
  for(auto it=bindings.begin();it!=bindings.end();){
   if(!it->second.handle.is_valid()){it=bindings.erase(it);continue;}
   auto ids=wanted(it->second.hash);
   if(!ids){return std::unexpected(ids.error());}auto ready=ensure(*ids,session);if(!ready){return ready;}++it;
  }
  for(auto& [id,group]:groups){
   const auto desired=limits.find(id);const core::SavedTagRateLimits rate=desired==limits.end()?core::SavedTagRateLimits{}:desired->second;
   if(!applied.contains(id)||!equal(applied.at(id),rate)){if(!group->setLimits(rate)){return std::unexpected(unavailable("Cannot update the actual saved tag rate group"));}applied[id]=rate;}
  }
  for(auto& [key,binding]:bindings){static_cast<void>(key);auto ids=wanted(binding.hash);
   for(auto id:*ids){if(std::find(binding.tags.begin(),binding.tags.end(),id)==binding.tags.end()&&!groups.at(id)->attach(binding.handle)){return std::unexpected(unavailable("Cannot attach a saved tag rate group"));}}
   for(auto id:binding.tags){if(std::find(ids->begin(),ids->end(),id)==ids->end()&&!groups.at(id)->detach(binding.handle)){return std::unexpected(unavailable("Cannot detach a saved tag rate group"));}}
   binding.tags=std::move(*ids);
  }
  dirty=false;
  // Retire removed groups only after detach;SDK owns pending quota lifetime.
  for(auto it=groups.begin();it!=groups.end();){if(!limits.contains(it->first)){applied.erase(it->first);it=groups.erase(it);}else{++it;}}
  return {};
 }
};
ProfileTagRates::ProfileTagRates(std::unique_ptr<Impl> p):impl_(std::move(p)){}
ProfileTagRates::~ProfileTagRates()=default;
std::expected<std::unique_ptr<ProfileTagRates>,storage::Error> ProfileTagRates::open(const storage::ProfileLease& lease,const storage::TagManagerStartupMode& mode){
 if(!mode.matches(lease)){return std::unexpected(unavailable("Tag startup mode belongs to another profile owner"));}
 auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
 auto state=std::make_unique<Impl>();state->lease=std::move(*retained);state->enabled=mode.enabled();
 auto result=std::unique_ptr<ProfileTagRates>(new ProfileTagRates(std::move(state)));auto loaded=result->impl_->read();if(!loaded){return std::unexpected(loaded.error());}return result;
}
std::expected<void,storage::Error> ProfileTagRates::prepare(const core::TorrentHash& hash,libtorrent::session_handle session){
 auto synced=impl_->sync(session);if(!synced){return synced;}
 if(const auto old=impl_->bindings.find(hash.hex());old!=impl_->bindings.end()&&!old->second.handle.is_valid()){impl_->bindings.erase(old);}
 auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}return impl_->ensure(*ids,session);
}
std::expected<void,storage::Error> ProfileTagRates::attach(const core::TorrentHash& hash,libtorrent::torrent_handle handle){
 auto live=impl_->current();if(!live){return live;}auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}
 if(!handle.is_valid()){return std::unexpected(unavailable("Actual paused torrent is required for saved tag rates"));}
 if(impl_->bindings.contains(hash.hex())){return std::unexpected(unavailable("Duplicate saved tag rate admission"));}
 // Publish binding storage first;partial attachment remains held for retry/close.
 auto [at,inserted]=impl_->bindings.emplace(hash.hex(),Impl::Binding{hash,handle,{}});static_cast<void>(inserted);
 for(auto id:*ids){auto found=impl_->groups.find(id);if(found==impl_->groups.end()||!found->second->attach(handle)){return std::unexpected(unavailable("Cannot attach saved rates before torrent resume"));}at->second.tags.push_back(id);}
 return {};
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
 auto ready=impl_->ensure(ids,session);if(!ready){return std::unexpected(ready.error());}
 for(std::size_t i=0;i<tags.size();++i){const auto uid=ids[i];const auto rate=impl_->limits.at(uid);auto witness=impl_->groups.at(uid)->readiness();
  if(!witness.currentFor(uid,rate)){return std::unexpected(unavailable("Actual SDK rate group is not ready"));}
  entries.push_back({tags[i],uid,rate,std::move(witness)});
 }
 return storage::PreparedTagRateSource(impl_->lease,store,source,impl_->sourceIdentity,std::move(entries));
}
std::expected<void,storage::Error> ProfileTagRates::close(){auto live=impl_->current();if(!live){return live;}impl_->sourceIdentity.reset();impl_->bindings.clear();impl_->groups.clear();impl_->lease.reset();return {};}
}
