from pathlib import Path
import json,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;O=D/'drafts';O.mkdir(exist_ok=True)
parent=json.loads((D/'pre-implementation.json').read_text())['native_parent']
def source(name):return subprocess.check_output(['git','-C',str(N),'show',parent+':preview/native/engine/'+name],text=True)
s=source('sdk_tag_rate_group.hpp').replace('class SdkSharedTagBudget;','class SdkSharedTagBudget;\nclass SdkPreparedTagAttachment;')
needle=' std::expected<void,SdkTagRateError> attach(libtorrent::torrent_handle);'
assert needle in s;s=s.replace(needle,' // Create on this control worker;install on a future paused add from any\n // Native worker. The separate token proves SDK attachment only.\n std::expected<std::unique_ptr<SdkPreparedTagAttachment>,SdkTagRateError> prepareAttachment(\n     const libtorrent::info_hash_t&);\n'+needle);(O/'sdk_tag_rate_group.hpp').write_text(s)
shutil.copy2(D/'sdk_prepared_tag_attachment.hpp',O/'sdk_prepared_tag_attachment.hpp')
s=source('sdk_tag_rate_group.cpp').replace('#include "sdk_tag_rate_group.hpp"','#include "sdk_tag_rate_group.hpp"\n#include "sdk_prepared_tag_attachment.hpp"\n#include <libtorrent/add_torrent_params.hpp>\n#include <libtorrent/torrent_info.hpp>\n#include <mutex>')
needle=' std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> bindings;\n std::shared_ptr<std::atomic_uint64_t> rateEpoch;';assert s.count(needle)==1;s=s.replace(needle,' std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> bindings;\n // SDK-thread-only pre-add registry;legacy controls remain on their worker.\n std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> preparedBindings;\n template<class F>void each(F&& f){\n  for(const auto& [handle,plugin]:bindings){(void)handle;f(plugin);}\n  for(const auto& [handle,plugin]:preparedBindings){(void)handle;f(plugin);}\n }\n void clear(){bindings.clear();preparedBindings.clear();}\n std::shared_ptr<std::atomic_uint64_t> rateEpoch;')
start=s.index(' void step(){',s.index('struct Retirement final:'));end=s.index('\n};\n// Controls run',start)
s=s[:start]+''' void step(){
  if(!ready.load(std::memory_order_acquire)||released){return;}
  const auto session=native.lock();if(!session||!created){released=true;clear();return;}
  if(!cleared){
    if(gate(session,group,{})!=0){return;}
    if(auto* value=session->peer_classes().at(group)){value->set_upload_limit(0);value->set_download_limit(0);}
    each([&](const auto& plugin){plugin->enabled=false;
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
'''+s[end:]
needle='     for(const auto& plugin:plugins){plugin->apply(plugin->enabled);}'
assert s.count(needle)==1;s=s.replace(needle,needle+'\n     for(const auto& [handle,plugin]:owner->retirement->preparedBindings){(void)handle;plugin->apply(plugin->enabled);}')
start=s.index('std::expected<void,Error> SdkTagRateGroup::detach(');end=s.index('std::expected<void,Error> SdkTagRateGroup::close()',start)
s=s[:start]+'''std::expected<void,Error> SdkTagRateGroup::detach(lt::torrent_handle handle){
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
'''+s[end:]
# Reattaching through the existing API must reactivate the actual pre-add plugin.
needle=' try{auto found=impl_->bindings.find(handle);';assert s.count(needle)==1;s=s.replace(needle,''' try{
   bool prepared=false;const auto domain=impl_->domain;
   auto existing=domain->retirement->preparedCount.load(std::memory_order_acquire)?invoke(domain,handle,[domain,handle,&prepared]{
    const auto at=domain->retirement->preparedBindings.find(handle);
    if(at!=domain->retirement->preparedBindings.end()){at->second->apply(true);prepared=true;}
   }):std::expected<void,Error>{};
   if(!existing){return existing;}if(prepared){return {};}
   auto found=impl_->bindings.find(handle);''')
# Keep ordinary attachment's existing number of SDK round trips.
s=s.replace(' std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> preparedBindings;', ' std::map<lt::torrent_handle,std::shared_ptr<GroupPlugin>> preparedBindings;\n std::atomic_size_t preparedCount=0;')
s=s.replace(' void clear(){bindings.clear();preparedBindings.clear();}', ' void clear(){bindings.clear();preparedBindings.clear();preparedCount.store(0,std::memory_order_release);}')
# Reuse the existing domain after the registry probe;keep one declaration.
s=s.replace('auto [row,inserted]=impl_->bindings.emplace(handle,plugin);(void)inserted;\n   const auto domain=impl_->domain;', 'auto [row,inserted]=impl_->bindings.emplace(handle,plugin);(void)inserted;')
# Passive token queries observe an atomic SDK-thread attachment state.
s=s.replace(' bool enabled=true;', ' bool enabled=true;std::atomic_bool attached=true;')
s=s.replace('  enabled=value;const auto session=', '  enabled=value;attached.store(value,std::memory_order_release);const auto session=')
s=s.replace('each([&](const auto& plugin){plugin->enabled=false;', 'each([&](const auto& plugin){plugin->enabled=false;plugin->attached.store(false,std::memory_order_release);')
# The stack reference above must never escape a timed-out SDK invocation.
s=s.replace('bool prepared=false;const auto domain=impl_->domain;','auto prepared=std::make_shared<std::atomic_bool>(false);const auto domain=impl_->domain;').replace('[domain,handle,&prepared]{','[domain,handle,prepared]{').replace('at->second->apply(true);prepared=true;','at->second->apply(true);prepared->store(true);').replace('if(!existing){return existing;}if(prepared){return {};}','if(!existing){return existing;}if(prepared->load()){return {};}')
needle='std::expected<void,Error> SdkTagRateGroup::setLimits('
start=s.index(needle)
addition='''struct SdkPreparedTagAttachment::Impl {
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
std::expected<void,Error> SdkPreparedTagAttachment::install(lt::add_torrent_params& params){
 if(!current()){return std::unexpected(Error::Lifetime);}
 if(!(params.flags&lt::torrent_flags::paused)||(params.flags&lt::torrent_flags::auto_managed)){
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
'''
s=s[:start]+addition+s[start:];(O/'sdk_tag_rate_group.cpp').write_text(s)
print('Staged pre-add capability and SDK-thread registry;existing worker controls preserved')
