// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_tag_rate_group.hpp"
#include "sdk_prepared_tag_attachment.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <mutex>
#include "sdk_shared_tag_budget.hpp"
#include <boost/asio/post.hpp>
#if __has_include(<libtorrent/biglybtc_tag_gate.hpp>)
#include <libtorrent/biglybtc_tag_gate.hpp>
#define BIGLYBTC_SDK_TAG_DIRECTIONS 1
#else
#define BIGLYBTC_SDK_TAG_DIRECTIONS 0
#endif
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/peer_info.hpp>
#include <algorithm>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <limits>
#include <thread>
#include <stdexcept>
namespace biglybtc::engine {
namespace {
namespace lt=libtorrent;using Error=SdkTagRateError;
#if BIGLYBTC_SDK_TAG_DIRECTIONS
struct DirectionApi {
 decltype(&biglybtc_tag_gate_set_v1) set=nullptr;
 decltype(&biglybtc_tag_gate_peer_v1) peer=nullptr;
 decltype(&biglybtc_tag_gate_features_v1) features=nullptr;
 decltype(&biglybtc_tag_gate_notify_peer_v1) notify=nullptr;
 int (*lanBypass)(void*,std::uint32_t,int)noexcept=nullptr;
#if defined(TORRENT_BIGLYBTC_TAG_CLASS_ABI)
 decltype(&biglybtc_tag_class_abi_v1) classes=nullptr;
#endif
};
const DirectionApi& directionApi()noexcept{
 // Optional exports must not become mandatory loader relocations:an older
 // image must reach the normal dependency refusal before any SDK use.
 static const DirectionApi api=[]()noexcept{
  DirectionApi value;
  value.set=reinterpret_cast<decltype(value.set)>(::dlsym(RTLD_DEFAULT,"biglybtc_tag_gate_set_v1"));
  value.peer=reinterpret_cast<decltype(value.peer)>(::dlsym(RTLD_DEFAULT,"biglybtc_tag_gate_peer_v1"));
  value.features=reinterpret_cast<decltype(value.features)>(::dlsym(RTLD_DEFAULT,"biglybtc_tag_gate_features_v1"));
  value.notify=reinterpret_cast<decltype(value.notify)>(::dlsym(RTLD_DEFAULT,"biglybtc_tag_gate_notify_peer_v1"));
  value.lanBypass=reinterpret_cast<decltype(value.lanBypass)>(::dlsym(RTLD_DEFAULT,"biglybtc_tag_lan_bypass_set_v1"));
  if(!value.set||!value.peer||!value.features||!value.notify){return DirectionApi{};}
  Dl_info sdk{};const auto original=::dlsym(RTLD_DEFAULT,"_ZN10libtorrent7versionEv");if(!original||!::dladdr(original,&sdk)){return DirectionApi{};}
  for(void* address:{reinterpret_cast<void*>(value.set),reinterpret_cast<void*>(value.peer),reinterpret_cast<void*>(value.features),reinterpret_cast<void*>(value.notify)}){
    Dl_info owner{};if(!::dladdr(address,&owner)||owner.dli_fbase!=sdk.dli_fbase){return DirectionApi{};}
  }
  if(value.lanBypass){Dl_info owner{};if(!::dladdr(reinterpret_cast<void*>(value.lanBypass),&owner)||owner.dli_fbase!=sdk.dli_fbase){return DirectionApi{};}}
#if defined(TORRENT_BIGLYBTC_TAG_CLASS_ABI)
  value.classes=reinterpret_cast<decltype(value.classes)>(::dlsym(RTLD_DEFAULT,"biglybtc_tag_class_abi_v1"));
  Dl_info classImage{};
  if(!value.classes||!::dladdr(reinterpret_cast<void*>(value.classes),&classImage)
      ||classImage.dli_fbase!=sdk.dli_fbase||value.classes()!=TORRENT_BIGLYBTC_TAG_CLASS_ABI){return DirectionApi{};}
#endif
  if(value.features()!=UINT64_C(0x4242544700010007)){return DirectionApi{};}return value;
 }();return api;
}
#endif
bool directions()noexcept{
#if BIGLYBTC_SDK_TAG_DIRECTIONS
 return directionApi().features!=nullptr;
#else
 return false;
#endif
}
bool represented(core::SavedTagRateLimits limits)noexcept{
#if defined(TORRENT_BIGLYBTC_TAG_CLASS_ABI)
 // Any access to a changed private base requires its actual same-image ABI,
 // including unlimited and ordinary positive limits. No old-array fallback.
 if(!directions()){return false;}
#else
 // Legacy array headers must also refuse a dynamically sized SDK image.
 if(::dlsym(RTLD_DEFAULT,"biglybtc_tag_class_abi_v1")){return false;}
#endif
 const bool extended=directions();const auto direction=[extended](std::int32_t value){return value==0||(value>=10&&value<std::numeric_limits<std::int32_t>::max())||(extended&&value>=-1&&value<=9);};
 return direction(limits.upload)&&direction(limits.download);
}
int gate(const std::shared_ptr<lt::aux::session_impl>& session,lt::peer_class_t group,core::SavedTagRateLimits limits){
#if BIGLYBTC_SDK_TAG_DIRECTIONS
 if(directions()){return directionApi().set(static_cast<lt::aux::session_interface*>(session.get()),static_cast<std::uint32_t>(group),(limits.upload==-1?1:0)|(limits.download==-1?2:0));}
#else
 (void)session;(void)group;(void)limits;
#endif
 return 0;
}
int notify(const std::shared_ptr<lt::peer_connection>& peer){
#if BIGLYBTC_SDK_TAG_DIRECTIONS
 if(directions()&&!peer->is_disconnecting()&&!peer->associated_torrent().expired()){return directionApi().notify(peer.get());}
#else
 (void)peer;
#endif
 return 0;
}
struct Retirement;
struct Domain {
 lt::session_handle session;
 std::weak_ptr<lt::aux::session_impl> native;
 lt::peer_class_t group;
 std::thread::id worker=std::this_thread::get_id();
 std::atomic_bool closed=false,failed=false;
 std::shared_ptr<std::atomic_uint64_t> rateEpoch=std::make_shared<std::atomic_uint64_t>(0);
 std::uint64_t epochSequence=0,tag=0;core::SavedTagRateLimits limits{};
 std::shared_ptr<SdkSharedTagBudget> budget;
 bool configured=false;
 void fail()noexcept{failed.store(true);rateEpoch->store(0,std::memory_order_release);}

 std::shared_ptr<Retirement> retirement;
 bool created=false; // creating worker;never delete the default SDK class on allocation failure.
 Domain(lt::session_handle value,lt::peer_class_t id):session(value),native(value.native_handle()),group(id){}
};
bool owns(const std::shared_ptr<Domain>& domain,const lt::torrent_handle& handle){
 const auto session=domain->native.lock();const auto torrent=handle.native_handle();
 return session&&torrent&& &torrent->session()==static_cast<lt::aux::session_interface*>(session.get());
}
struct GroupPlugin final:lt::torrent_plugin {
 std::weak_ptr<Domain> domain;
 std::vector<std::weak_ptr<lt::peer_connection>> peers;
 bool enabled=true;std::atomic_bool attached=true;
 explicit GroupPlugin(std::weak_ptr<Domain> value):domain(std::move(value)){}
 void add(const std::shared_ptr<Domain>& owner,const std::shared_ptr<lt::peer_connection>& peer){
  const auto session=owner->native.lock();if(!session){return;}
#if defined(TORRENT_BIGLYBTC_TAG_CLASS_ABI)
  constexpr int capacity=4096; // Bound one low-level owner callback;normal sources have a smaller cohort bound.
#else
  constexpr int capacity=14;
#endif
  if(!peer->has_class(owner->group)&&peer->num_classes()>=capacity){
    owner->fail();lt::peer_connection_handle(peer).disconnect(lt::errors::too_many_connections,lt::operation_t::bittorrent);return;
  }
  peer->add_class(session->peer_classes(),owner->group);
  if(!peer->has_class(owner->group)){owner->fail();}
 }
 std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& handle)override{
  const auto owner=domain.lock();const auto peer=handle.native_handle();if(!owner||owner->closed.load()||!peer){return {};}
  std::erase_if(peers,[](const auto& value){return value.expired();});
  if(std::ranges::none_of(peers,[&](const auto& value){return value.lock()==peer;})){
    if(peers.size()==16384){owner->fail();lt::peer_connection_handle(peer).disconnect(lt::errors::too_many_connections,lt::operation_t::bittorrent);return {};}
    peers.push_back(peer);
  }
  if(enabled){add(owner,peer);}return {};
 }
 void apply(bool value){
  const auto owner=domain.lock();if(value&&(!owner||owner->closed.load()||owner->failed.load())){return;}
  enabled=value;attached.store(value,std::memory_order_release);const auto session=owner?owner->native.lock():nullptr;if(!session){return;}
  std::erase_if(peers,[](const auto& peer){return peer.expired();});
  for(const auto& saved:peers){if(const auto peer=saved.lock()){
    if(value){add(owner,peer);}else if(peer->has_class(owner->group)){peer->remove_class(session->peer_classes(),owner->group);}
    if(notify(peer)<0){owner->fail();}
  }}
 }
};
// Pending SDK bandwidth requests borrow channel pointers. Keep the initial
// pool reference until those requests have left both managers;otherwise a new
// class could reuse a still-referenced slot. The existing SDK tick performs
// retirement without a new timer or a blocking wait per download.
struct Retirement final:lt::plugin {
 std::weak_ptr<lt::aux::session_impl> native;
 lt::peer_class_t group{0};bool created=false,cleared=false,released=false;
 std::atomic_bool ready=false;
 std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> bindings;
 // SDK-thread-only pre-add registry;legacy controls remain on their worker.
 std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> preparedBindings;
 std::atomic_size_t preparedCount=0;
 template<class F>void each(F&& f){
  for(const auto& [handle,plugin]:bindings){(void)handle;f(plugin);}
  for(const auto& [handle,plugin]:preparedBindings){(void)handle;f(plugin);}
 }
 void clear(){bindings.clear();preparedBindings.clear();preparedCount.store(0,std::memory_order_release);}
 std::shared_ptr<std::atomic_uint64_t> rateEpoch;
 explicit Retirement(std::weak_ptr<lt::aux::session_impl> value,std::shared_ptr<std::atomic_uint64_t> epoch):native(std::move(value)),rateEpoch(std::move(epoch)){}
 void abort()override{rateEpoch->store(0,std::memory_order_release);}
 ~Retirement()override{rateEpoch->store(0,std::memory_order_release);}

 lt::feature_flags_t implemented_features()override{return tick_feature;}
 void step(){
  if(!ready.load(std::memory_order_acquire)||released){return;}
  const auto session=native.lock();if(!session||!created){released=true;clear();return;}
  if(!cleared){
    if(gate(session,group,{})!=0){return;}
#if BIGLYBTC_SDK_TAG_DIRECTIONS
    if(const auto fn=directionApi().lanBypass;fn&&fn(static_cast<lt::aux::session_interface*>(session.get()),static_cast<std::uint32_t>(group),0)!=0){return;}
#endif
    if(auto* value=session->peer_classes().at(group)){value->set_upload_limit(0);value->set_download_limit(0);}
    each([&](const auto& plugin){plugin->enabled=false;plugin->attached.store(false,std::memory_order_release);
      for(const auto& saved:plugin->peers){if(const auto peer=saved.lock();peer&&peer->has_class(group)){peer->remove_class(session->peer_classes(),group);(void)notify(peer);}}
    });cleared=true;
  }
  bool pending=false;
  each([&](const auto& plugin){for(const auto& saved:plugin->peers){if(const auto peer=saved.lock()){
    if(peer->is_disconnecting()){pending=true;continue;}
    lt::peer_info info;peer->get_peer_info(info);
    if((info.read_state&lt::peer_info::bw_limit)||(info.write_state&lt::peer_info::bw_limit)){pending=true;}
  }}});
  if(pending){return;}
  // Both registries retain their peer/channel subjects until requests drain.
  released=true;if(session->peer_classes().at(group)){session->peer_classes().decref(group);}clear();
 }
 void on_tick()override{try{step();}catch(...){if(released){clear();}}}

};
// Controls run on the actual SDK context,including a class with no torrents.
// The creating worker joins outside profile gates;no permanent job plugins.
template<class Function>std::expected<void,Error> invoke(const std::shared_ptr<Domain>& domain,Function operation){
 auto native=domain->native.lock();if(!native){return std::unexpected(Error::Lifetime);}
 auto completion=std::make_shared<std::promise<std::expected<void,Error>>>();auto future=completion->get_future();
 try{boost::asio::post(native->get_context(),[native,domain,completion,operation=std::move(operation)]{
   try{operation();completion->set_value({});}catch(...){completion->set_value(std::unexpected(Error::Engine));}
 });}catch(...){return std::unexpected(Error::Engine);}
 if(future.wait_for(std::chrono::seconds(5))!=std::future_status::ready){return std::unexpected(Error::Deadline);}
 return future.get();
}
template<class Function>std::expected<void,Error> invoke(const std::shared_ptr<Domain>& domain,lt::torrent_handle handle,Function operation){
 if(!owns(domain,handle)){return std::unexpected(Error::Foreign);}
 return invoke(domain,[domain,handle,operation=std::move(operation)]{if(!owns(domain,handle)){throw std::runtime_error("Tag rate target lifetime changed");}operation();});
}

}
struct SdkTagRateGroup::Impl {
 std::shared_ptr<Domain> domain;
 std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> bindings; // creating worker only
 explicit Impl(std::shared_ptr<Domain> value):domain(std::move(value)){}
 std::expected<void,Error> current()const{
  if(domain->worker!=std::this_thread::get_id()){return std::unexpected(Error::Thread);}
  if(!domain->created||domain->closed.load()||domain->native.expired()){return std::unexpected(Error::Lifetime);}
  if(domain->budget&&!domain->budget->alive()){return std::unexpected(Error::Lifetime);}
  if(domain->failed.load()){return std::unexpected(Error::Capacity);}return {};
 }
};
SdkTagRateGroup::SdkTagRateGroup(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SdkTagRateGroup::~SdkTagRateGroup(){(void)close();}
bool SdkTagRateGroup::supports(core::SavedTagRateLimits limits)noexcept{return represented(limits);}
bool SdkTagRateGroup::current()const noexcept{return impl_&&bool(impl_->current());}
core::TagRateReadiness SdkTagRateGroup::readiness()const noexcept{
 if(!current()){return {};}
 const auto& domain=*impl_->domain;const auto epoch=domain.rateEpoch->load(std::memory_order_acquire);
 if(domain.budget){const auto witness=domain.budget->witness(domain.limits);if(!witness.epoch)return {};
  return core::TagRateReadiness(domain.rateEpoch,epoch,domain.tag,domain.limits,witness.cookie,witness.read,witness.epoch);
 }
 return core::TagRateReadiness(domain.rateEpoch,epoch,domain.tag,domain.limits);
}

std::expected<std::unique_ptr<SdkTagRateGroup>,Error> SdkTagRateGroup::create(lt::session_handle session,std::uint64_t tag,core::SavedTagRateLimits limits){
 return create(session,tag,limits,{});
}
std::expected<std::unique_ptr<SdkTagRateGroup>,Error> SdkTagRateGroup::create(lt::session_handle session,std::uint64_t tag,core::SavedTagRateLimits limits,std::shared_ptr<SdkSharedTagBudget> budget){
 if(!represented(limits)){return std::unexpected(Error::Limits);}
 if(!session.native_handle()){return std::unexpected(Error::Lifetime);}
 if(budget){const auto actual=budget->observedLimits();if(!actual||budget->tag()!=tag||actual->upload!=limits.upload||actual->download!=limits.download)return std::unexpected(Error::Limits);}
 try{auto domain=std::make_shared<Domain>(session,lt::peer_class_t{0});domain->tag=tag;
   domain->budget=std::move(budget);
   auto result=std::unique_ptr<SdkTagRateGroup>(new SdkTagRateGroup(std::make_unique<Impl>(std::move(domain))));
   auto retirement=std::make_shared<Retirement>(result->impl_->domain->native,result->impl_->domain->rateEpoch);
   result->impl_->domain->retirement=retirement;session.add_extension(retirement);
   const auto label="native-tag-rate-"+std::to_string(tag);result->impl_->domain->group=session.create_peer_class(label.c_str());result->impl_->domain->created=true;
   retirement->group=result->impl_->domain->group;retirement->created=true;
   if(result->impl_->domain->budget){const auto owner=result->impl_->domain;auto bound=invoke(owner,[owner]{const auto native=owner->native.lock();if(!native||owner->budget->bind(static_cast<lt::aux::session_interface*>(native.get()),static_cast<std::uint32_t>(owner->group))!=0)throw std::runtime_error("Shared tag rate class binding rejected");});if(!bound)return std::unexpected(bound.error());}
   auto configured=result->setLimits(limits);if(!configured){return std::unexpected(configured.error());}return result;
 }catch(...){return std::unexpected(Error::Engine);}
}
struct SdkPreparedTagAttachment::Impl {
 enum class Phase {Fresh,Armed,Bound,Failed};
 std::weak_ptr<Domain> domain;std::shared_ptr<Retirement> retirement;
 core::TagRateReadiness readiness;std::uint64_t tag=0;core::SavedTagRateLimits limits;
 lt::info_hash_t expected;mutable std::mutex mutex;Phase phase=Phase::Fresh;std::atomic_bool failed=false;
 lt::torrent_handle target;std::weak_ptr<GroupPlugin> plugin;
 bool current()const noexcept{
  const auto owner=domain.lock();return !failed.load()&&owner&&!owner->closed.load()&&!owner->failed.load()
      &&readiness.currentFor(tag,limits);
 }
};
SdkPreparedTagAttachment::SdkPreparedTagAttachment(std::shared_ptr<Impl> value):impl_(std::move(value)){}
SdkPreparedTagAttachment::~SdkPreparedTagAttachment()=default;
bool SdkPreparedTagAttachment::current()const noexcept{return impl_&&impl_->current();}
bool SdkPreparedTagAttachment::boundTo(lt::torrent_handle target)const noexcept{
 try{if(!current()){return false;}std::lock_guard lock(impl_->mutex);
  const auto plugin=impl_->plugin.lock();
  return impl_->phase==Impl::Phase::Bound&&impl_->target==target&&target.is_valid()&&plugin&&plugin->attached.load(std::memory_order_acquire);
 }catch(...){return false;}
}
std::expected<void,Error> SdkPreparedTagAttachment::install(lt::add_torrent_params& params){return installImpl(params,false);}
std::expected<void,Error> SdkPreparedTagAttachment::installForChecking(lt::add_torrent_params& params){return installImpl(params,true);}
std::expected<void,Error> SdkPreparedTagAttachment::installImpl(lt::add_torrent_params& params,bool checking){
 if(!current()){return std::unexpected(Error::Lifetime);}
 const bool paused=bool(params.flags&lt::torrent_flags::paused);
 if((params.flags&lt::torrent_flags::auto_managed)
     ||(checking?(!params.ti||paused||!(params.flags&lt::torrent_flags::stop_when_ready)):!paused)){
  return std::unexpected(Error::Limits);
 }
 const auto hashes=params.ti?params.ti->info_hashes():params.info_hashes;
 if(hashes!=impl_->expected){return std::unexpected(Error::Foreign);}
 std::lock_guard lock(impl_->mutex);if(impl_->phase!=Impl::Phase::Fresh){return std::unexpected(Error::Lifetime);}
 try{const auto state=impl_;
  params.extensions.emplace_back([state](lt::torrent_handle const& target,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{
   std::lock_guard held(state->mutex);
   if(state->phase!=Impl::Phase::Armed||!state->current()){state->phase=Impl::Phase::Failed;state->failed.store(true);return {};}
   const auto owner=state->domain.lock();
   if(!owner||!owns(owner,target)||target.info_hashes()!=state->expected){state->phase=Impl::Phase::Failed;state->failed.store(true);return {};}
   try{
    auto& bindings=state->retirement->preparedBindings;auto found=bindings.find(target);
    if(found==bindings.end()){
     if(bindings.size()==50000){state->phase=Impl::Phase::Failed;state->failed.store(true);return {};}
     found=bindings.emplace(target,std::make_shared<GroupPlugin>(owner)).first;state->retirement->preparedCount.fetch_add(1,std::memory_order_release);
    }
    found->second->apply(true);state->target=target;state->plugin=found->second;state->phase=Impl::Phase::Bound;return found->second;
   }catch(...){state->phase=Impl::Phase::Failed;state->failed.store(true);owner->fail();return {};}
  });
  params.flags|=lt::torrent_flags::duplicate_is_error;impl_->phase=Impl::Phase::Armed;return {};
 }catch(...){impl_->phase=Impl::Phase::Failed;impl_->failed.store(true);return std::unexpected(Error::Engine);}
}
std::expected<std::unique_ptr<SdkPreparedTagAttachment>,Error> SdkTagRateGroup::prepareAttachment(const lt::info_hash_t& expected){
 auto live=impl_->current();if(!live){return std::unexpected(live.error());}
 if(!expected.has_v1()&&!expected.has_v2()){return std::unexpected(Error::Limits);}
 try{auto state=std::make_shared<SdkPreparedTagAttachment::Impl>();state->domain=impl_->domain;state->retirement=impl_->domain->retirement;
  state->expected=expected;state->tag=impl_->domain->tag;state->limits=impl_->domain->limits;state->readiness=readiness();
  if(!state->current()){return std::unexpected(Error::Lifetime);}
  return std::unique_ptr<SdkPreparedTagAttachment>(new SdkPreparedTagAttachment(std::move(state)));
 }catch(...){return std::unexpected(Error::Engine);}
}
std::expected<void,Error> SdkTagRateGroup::configureOriginalLanBypass(bool enabled){
 auto live=impl_->current();if(!live){return live;}
#if BIGLYBTC_SDK_TAG_DIRECTIONS
 const auto fn=directionApi().lanBypass;if(!fn){return std::unexpected(Error::Limits);}
 auto domain=impl_->domain;
 if(!impl_->bindings.empty()||domain->retirement->preparedCount.load()!=0){return std::unexpected(Error::Limits);}
 if(domain->epochSequence==std::numeric_limits<std::uint64_t>::max()){domain->fail();return std::unexpected(Error::Lifetime);}
 domain->rateEpoch->store(0,std::memory_order_release);
 auto result=invoke(domain,[domain,fn,enabled]{const auto session=domain->native.lock();
   if(!session||fn(static_cast<lt::aux::session_interface*>(session.get()),static_cast<std::uint32_t>(domain->group),enabled?1:0)!=0){throw std::runtime_error("Original LAN tag policy refused");}
 });
 if(!result){domain->fail();return result;}
 ++domain->epochSequence;domain->rateEpoch->store(domain->epochSequence,std::memory_order_release);return {};
#else
 (void)enabled;return std::unexpected(Error::Limits);
#endif
}
std::expected<void,Error> SdkTagRateGroup::setLimits(core::SavedTagRateLimits limits){
 auto alive=impl_->current();if(!alive){return alive;}
 if(!represented(limits)){return std::unexpected(Error::Limits);}
 auto& domain=*impl_->domain;
 const auto shared=domain.budget?domain.budget->observedLimits():std::optional<core::SavedTagRateLimits>{};
 if(domain.budget&&!shared){domain.fail();return std::unexpected(Error::Lifetime);}
 const bool sharedMatches=!domain.budget||(shared->upload==limits.upload&&shared->download==limits.download);
 // A consumer on another control worker acknowledges an already published
 // budget. It cannot mutate the quota owner's policy or revoke an old proof
 // merely by proposing a different rate.
 if(domain.budget&&!sharedMatches&&!domain.budget->controls()){return std::unexpected(Error::Limits);}
 if(domain.configured&&domain.limits.upload==limits.upload&&domain.limits.download==limits.download
     &&(!domain.budget||(shared->upload==limits.upload&&shared->download==limits.download))){return {};}
 if(domain.epochSequence==std::numeric_limits<std::uint64_t>::max()){domain.fail();return std::unexpected(Error::Lifetime);}
 domain.rateEpoch->store(0,std::memory_order_release); // Old exact-limit proofs cannot survive mutation.
 if(domain.budget&&!sharedMatches&&!domain.budget->setLimits(limits)){domain.fail();return std::unexpected(Error::Engine);}
 try{std::vector<std::shared_ptr<GroupPlugin>> plugins;plugins.reserve(impl_->bindings.size());
   for(const auto& [handle,plugin]:impl_->bindings){(void)handle;plugins.push_back(plugin);}
   const auto owner=impl_->domain;
   auto changed=invoke(owner,[owner,limits,plugins=std::move(plugins)]{
     const auto native=owner->native.lock();if(!native){throw std::runtime_error("Tag rate SDK lifetime changed");}
     auto* value=native->peer_classes().at(owner->group);if(!value||(!owner->budget&&gate(native,owner->group,limits)!=0)){throw std::runtime_error("Tag rate class gate rejected");}
     lt::peer_class_info info;value->get_info(&info);info.upload_limit=std::max(0,limits.upload);info.download_limit=std::max(0,limits.download);value->set_info(&info);
     lt::peer_class_info actual;value->get_info(&actual);
     if(actual.upload_limit!=info.upload_limit||actual.download_limit!=info.download_limit){throw std::runtime_error("Tag rate SDK rounded limits");}
     for(const auto& plugin:plugins){plugin->apply(plugin->enabled);}
     for(const auto& [handle,plugin]:owner->retirement->preparedBindings){(void)handle;plugin->apply(plugin->enabled);}
   });
   if(!changed||domain.failed.load()){domain.fail();return changed?std::unexpected(Error::Engine):changed;}
   domain.limits=limits;domain.configured=true;++domain.epochSequence;
   domain.rateEpoch->store(domain.epochSequence,std::memory_order_release);return {};
 }catch(...){domain.fail();return std::unexpected(Error::Engine);}

}
std::expected<void,Error> SdkTagRateGroup::attach(lt::torrent_handle handle){
 auto alive=impl_->current();if(!alive){return alive;}
 try{
   auto prepared=std::make_shared<std::atomic_bool>(false);const auto domain=impl_->domain;
   auto existing=domain->retirement->preparedCount.load(std::memory_order_acquire)?invoke(domain,handle,[domain,handle,prepared]{
    const auto at=domain->retirement->preparedBindings.find(handle);
    if(at!=domain->retirement->preparedBindings.end()){at->second->apply(true);prepared->store(true);}
   }):std::expected<void,Error>{};
   if(!existing){return existing;}if(prepared->load()){return {};}
   auto found=impl_->bindings.find(handle);
   if(found!=impl_->bindings.end()){return invoke(impl_->domain,handle,[plugin=found->second]{plugin->apply(true);});}
   if(impl_->bindings.size()==50000){return std::unexpected(Error::Capacity);}
   auto plugin=std::make_shared<GroupPlugin>(impl_->domain);
   // Allocate registry storage before installing a persistent SDK plugin.
   auto [row,inserted]=impl_->bindings.emplace(handle,plugin);(void)inserted;
   handle.add_extension([domain,plugin](lt::torrent_handle const& target,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{
     if(domain->closed.load()||domain->failed.load()||!owns(domain,target)){return {};}return plugin;
   });
   auto applied=invoke(domain,handle,[plugin]{plugin->apply(true);});
   if(!applied||domain->failed.load()){
     (void)invoke(domain,handle,[plugin]{plugin->apply(false);});impl_->bindings.erase(row);
     if(!applied&&(applied.error()==Error::Deadline||applied.error()==Error::Engine)){domain->fail();}
     return applied?std::unexpected(Error::Capacity):applied;
   }
   return {};
 }catch(...){impl_->domain->fail();return std::unexpected(Error::Engine);}
}
std::expected<void,Error> SdkTagRateGroup::detach(lt::torrent_handle handle){
 auto alive=impl_->current();if(!alive){return alive;}
 const auto found=impl_->bindings.find(handle);
 if(found==impl_->bindings.end()&&!owns(impl_->domain,handle)){return {};}
 const auto legacy=found==impl_->bindings.end()?std::shared_ptr<GroupPlugin>{}:found->second;
 const auto domain=impl_->domain;
 return invoke(domain,handle,[domain,handle,legacy]{
  if(legacy){legacy->apply(false);}
  const auto prepared=domain->retirement->preparedBindings.find(handle);
  if(prepared!=domain->retirement->preparedBindings.end()){prepared->second->apply(false);}
 });
}
std::expected<void,Error> SdkTagRateGroup::close()noexcept{
 if(!impl_||impl_->domain->closed.load()){return {};}
 if(impl_->domain->worker!=std::this_thread::get_id()){return std::unexpected(Error::Thread);}
 try{auto domain=impl_->domain;domain->closed.store(true);domain->rateEpoch->store(0,std::memory_order_release);
   auto retirement=domain->retirement;if(!retirement){return {};}
   retirement->bindings=std::move(impl_->bindings);
   retirement->ready.store(true,std::memory_order_release);
   if(!domain->native.expired()){return invoke(domain,[retirement]{retirement->step();});}
   return {};
 }catch(...){impl_->domain->closed.store(true);impl_->domain->rateEpoch->store(0,std::memory_order_release);return std::unexpected(Error::Engine);}
}
}
