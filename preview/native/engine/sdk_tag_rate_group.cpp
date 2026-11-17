// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_tag_rate_group.hpp"
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/peer_info.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <limits>
#include <thread>
namespace biglybtc::engine {
namespace {
namespace lt=libtorrent;using Error=SdkTagRateError;
bool represented(core::SavedTagRateLimits limits)noexcept{
 const auto direction=[](std::int32_t value){return value==0||(value>=10&&value<std::numeric_limits<std::int32_t>::max());};
 return direction(limits.upload)&&direction(limits.download);
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
 bool enabled=true;
 explicit GroupPlugin(std::weak_ptr<Domain> value):domain(std::move(value)){}
 void add(const std::shared_ptr<Domain>& owner,const std::shared_ptr<lt::peer_connection>& peer){
  const auto session=owner->native.lock();if(!session){return;}
  if(!peer->has_class(owner->group)&&peer->num_classes()>=15){
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
  enabled=value;const auto session=owner?owner->native.lock():nullptr;if(!session){return;}
  std::erase_if(peers,[](const auto& peer){return peer.expired();});
  for(const auto& saved:peers){if(const auto peer=saved.lock()){
    if(value){add(owner,peer);}else if(peer->has_class(owner->group)){peer->remove_class(session->peer_classes(),owner->group);}
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
 std::shared_ptr<std::atomic_uint64_t> rateEpoch;
 explicit Retirement(std::weak_ptr<lt::aux::session_impl> value,std::shared_ptr<std::atomic_uint64_t> epoch):native(std::move(value)),rateEpoch(std::move(epoch)){}
 void abort()override{rateEpoch->store(0,std::memory_order_release);}
 ~Retirement()override{rateEpoch->store(0,std::memory_order_release);}

 lt::feature_flags_t implemented_features()override{return tick_feature;}
 void step(){
  if(!ready.load(std::memory_order_acquire)||released){return;}
  const auto session=native.lock();if(!session||!created){released=true;bindings.clear();return;}
  if(!cleared){
    if(auto* value=session->peer_classes().at(group)){value->set_upload_limit(0);value->set_download_limit(0);}
    for(const auto& [handle,plugin]:bindings){(void)handle;plugin->enabled=false;
      for(const auto& saved:plugin->peers){if(const auto peer=saved.lock();peer&&peer->has_class(group)){peer->remove_class(session->peer_classes(),group);}}
    }
    cleared=true;
  }
  for(const auto& [handle,plugin]:bindings){(void)handle;
    for(const auto& saved:plugin->peers){if(const auto peer=saved.lock()){
      // The queue query is assertion-build-only. In the real Release SDK the
      // peer bandwidth flags identify pending quota. Disconnected peers are
      // retained conservatively until the queue releases its strong reference.
      if(peer->is_disconnecting()){return;}
      lt::peer_info info;peer->get_peer_info(info);
      if((info.read_state & lt::peer_info::bw_limit)||(info.write_state & lt::peer_info::bw_limit)){return;}
    }}
  }
  // Mark before decref:an allocator failure while growing the pool free list
  // must never retry a completed decrement on the next network tick.
  released=true;if(session->peer_classes().at(group)){session->peer_classes().decref(group);}bindings.clear();
 }
 void on_tick()override{try{step();}catch(...){if(released){bindings.clear();}}}
};
// A one-shot add_extension factory is a real SDK network-thread job. The
// future is joined only by its creating control worker,outside filesystem gates.
// A second job fences existing-peer hooks from the preceding plugin addition.
template<class Function>std::expected<void,Error> invoke(const std::shared_ptr<Domain>& domain,
    lt::torrent_handle handle,Function operation){
 if(!handle.is_valid()||domain->native.expired()){return std::unexpected(Error::Lifetime);}
 auto completion=std::make_shared<std::promise<std::expected<void,Error>>>();auto future=completion->get_future();
 try{handle.add_extension([domain,completion,operation=std::move(operation)](lt::torrent_handle const& target,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{
   try{if(!owns(domain,target)){completion->set_value(std::unexpected(Error::Foreign));}
     else{operation();completion->set_value({});}}
   catch(...){completion->set_value(std::unexpected(Error::Engine));}return {};
 });}catch(...){return std::unexpected(Error::Engine);}
 if(future.wait_for(std::chrono::seconds(5))!=std::future_status::ready){return std::unexpected(Error::Deadline);}
 return future.get();
}
}
struct SdkTagRateGroup::Impl {
 std::shared_ptr<Domain> domain;
 std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> bindings; // creating worker only
 explicit Impl(std::shared_ptr<Domain> value):domain(std::move(value)){}
 std::expected<void,Error> current()const{
  if(domain->worker!=std::this_thread::get_id()){return std::unexpected(Error::Thread);}
  if(!domain->created||domain->closed.load()||domain->native.expired()){return std::unexpected(Error::Lifetime);}
  if(domain->failed.load()){return std::unexpected(Error::Capacity);}return {};
 }
};
SdkTagRateGroup::SdkTagRateGroup(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SdkTagRateGroup::~SdkTagRateGroup(){(void)close();}
bool SdkTagRateGroup::current()const noexcept{return impl_&&bool(impl_->current());}
core::TagRateReadiness SdkTagRateGroup::readiness()const noexcept{
 if(!current()){return {};}
 const auto& domain=*impl_->domain;const auto epoch=domain.rateEpoch->load(std::memory_order_acquire);
 return core::TagRateReadiness(domain.rateEpoch,epoch,domain.tag,domain.limits);
}

std::expected<std::unique_ptr<SdkTagRateGroup>,Error> SdkTagRateGroup::create(lt::session_handle session,std::uint64_t tag,core::SavedTagRateLimits limits){
 if(!represented(limits)){return std::unexpected(Error::Limits);}
 if(!session.native_handle()){return std::unexpected(Error::Lifetime);}
 try{auto domain=std::make_shared<Domain>(session,lt::peer_class_t{0});domain->tag=tag;
   auto result=std::unique_ptr<SdkTagRateGroup>(new SdkTagRateGroup(std::make_unique<Impl>(std::move(domain))));
   auto retirement=std::make_shared<Retirement>(result->impl_->domain->native,result->impl_->domain->rateEpoch);
   result->impl_->domain->retirement=retirement;session.add_extension(retirement);
   const auto label="native-tag-rate-"+std::to_string(tag);result->impl_->domain->group=session.create_peer_class(label.c_str());result->impl_->domain->created=true;
   retirement->group=result->impl_->domain->group;retirement->created=true;
   auto configured=result->setLimits(limits);if(!configured){return std::unexpected(configured.error());}return result;
 }catch(...){return std::unexpected(Error::Engine);}
}
std::expected<void,Error> SdkTagRateGroup::setLimits(core::SavedTagRateLimits limits){
 auto alive=impl_->current();if(!alive){return alive;}
 if(!represented(limits)){return std::unexpected(Error::Limits);}
 auto& domain=*impl_->domain;
 if(domain.configured&&domain.limits.upload==limits.upload&&domain.limits.download==limits.download){return {};}
 if(domain.epochSequence==std::numeric_limits<std::uint64_t>::max()){domain.fail();return std::unexpected(Error::Lifetime);}
 domain.rateEpoch->store(0,std::memory_order_release); // Old exact-limit proofs cannot survive mutation.
 try{auto info=domain.session.get_peer_class(domain.group);info.upload_limit=limits.upload;info.download_limit=limits.download;
   domain.session.set_peer_class(domain.group,info);
   // Public synchronous getter fences the preceding SDK-thread setter. This
   // happens outside every profile gate,and once per actual limit change.
   const auto actual=domain.session.get_peer_class(domain.group);
   if(actual.upload_limit!=limits.upload||actual.download_limit!=limits.download){domain.fail();return std::unexpected(Error::Engine);}
   domain.limits=limits;domain.configured=true;++domain.epochSequence;
   domain.rateEpoch->store(domain.epochSequence,std::memory_order_release);return {};
 }catch(...){domain.fail();return std::unexpected(Error::Engine);}
}
std::expected<void,Error> SdkTagRateGroup::attach(lt::torrent_handle handle){
 auto alive=impl_->current();if(!alive){return alive;}
 try{auto found=impl_->bindings.find(handle);
   if(found!=impl_->bindings.end()){return invoke(impl_->domain,handle,[plugin=found->second]{plugin->apply(true);});}
   if(impl_->bindings.size()==50000){return std::unexpected(Error::Capacity);}
   auto plugin=std::make_shared<GroupPlugin>(impl_->domain);
   // Allocate registry storage before installing a persistent SDK plugin.
   auto [row,inserted]=impl_->bindings.emplace(handle,plugin);(void)inserted;
   const auto domain=impl_->domain;
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
 const auto found=impl_->bindings.find(handle);if(found==impl_->bindings.end()){return {};}
 return invoke(impl_->domain,handle,[plugin=found->second]{plugin->apply(false);});
}
std::expected<void,Error> SdkTagRateGroup::close()noexcept{
 if(!impl_||impl_->domain->closed.load()){return {};}
 if(impl_->domain->worker!=std::this_thread::get_id()){return std::unexpected(Error::Thread);}
 try{auto domain=impl_->domain;domain->closed.store(true);domain->rateEpoch->store(0,std::memory_order_release);
   auto retirement=domain->retirement;if(!retirement){return {};}
   retirement->bindings=std::move(impl_->bindings);lt::torrent_handle anchor;
   for(const auto& [handle,plugin]:retirement->bindings){(void)plugin;if(handle.is_valid()){anchor=handle;}}
   retirement->ready.store(true,std::memory_order_release);
   if(anchor.is_valid()&&!domain->native.expired()){return invoke(domain,anchor,[retirement]{retirement->step();});}
   // With no active anchor,the already-installed session plugin owns cleanup.
   return {};
 }catch(...){impl_->domain->closed.store(true);impl_->domain->rateEpoch->store(0,std::memory_order_release);return std::unexpected(Error::Engine);}
}
}
