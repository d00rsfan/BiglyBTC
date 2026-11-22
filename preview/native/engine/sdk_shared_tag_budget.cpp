// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_shared_tag_budget.hpp"
#include <libtorrent/version.hpp>
#if __has_include(<libtorrent/biglybtc_shared_quota.hpp>) && __has_include(<libtorrent/biglybtc_tag_gate.hpp>)
#include <libtorrent/biglybtc_shared_quota.hpp>
#include <libtorrent/biglybtc_tag_gate.hpp>
#define BIGLYBTC_SHARED_QUOTA_HEADERS 1
#else
#define BIGLYBTC_SHARED_QUOTA_HEADERS 0
#endif
#include <dlfcn.h>
#include <atomic>
#include <limits>
#include <thread>
namespace biglybtc::engine {
namespace {
using Error=SdkSharedQuotaError;
bool valid(core::SavedTagRateLimits value){const auto rate=[](auto n){return n>=-1&&n<std::numeric_limits<std::int32_t>::max();};return rate(value.upload)&&rate(value.download);}
#if BIGLYBTC_SHARED_QUOTA_HEADERS
struct Api {
 decltype(&biglybtc_shared_quota_scope_create_v1) create=nullptr;
 decltype(&biglybtc_shared_quota_scope_close_v1) close=nullptr;
 decltype(&biglybtc_shared_quota_scope_release_v1) release=nullptr;
 decltype(&biglybtc_shared_quota_acquire_v1) acquire=nullptr;
 decltype(&biglybtc_shared_quota_set_v1) set=nullptr;
 decltype(&biglybtc_tag_shared_bind_v1) bind=nullptr;
 decltype(&biglybtc_shared_quota_features_v1) features=nullptr;
 decltype(&biglybtc_tag_class_abi_v1) classes=nullptr;
 decltype(&biglybtc_tag_gate_notify_peer_v1) notify=nullptr;
 void* lanPolicy=nullptr;
 int (*protocolPolicy)(void*,int)noexcept=nullptr;
};
const Api& api()noexcept{
 static const Api result=[]()noexcept{
  Api a;Dl_info sdk{};
  if(!::dladdr(reinterpret_cast<void*>(&libtorrent::version),&sdk)){return Api{};}
  const auto symbol=[&](const char* name){auto* p=::dlsym(RTLD_DEFAULT,name);Dl_info image{};return p&&::dladdr(p,&image)&&image.dli_fbase==sdk.dli_fbase?p:nullptr;};
  a.create=reinterpret_cast<decltype(a.create)>(symbol("biglybtc_shared_quota_scope_create_v1"));
  a.close=reinterpret_cast<decltype(a.close)>(symbol("biglybtc_shared_quota_scope_close_v1"));
  a.release=reinterpret_cast<decltype(a.release)>(symbol("biglybtc_shared_quota_scope_release_v1"));
  a.acquire=reinterpret_cast<decltype(a.acquire)>(symbol("biglybtc_shared_quota_acquire_v1"));
  a.set=reinterpret_cast<decltype(a.set)>(symbol("biglybtc_shared_quota_set_v1"));
  a.bind=reinterpret_cast<decltype(a.bind)>(symbol("biglybtc_tag_shared_bind_v1"));
  a.features=reinterpret_cast<decltype(a.features)>(symbol("biglybtc_shared_quota_features_v1"));
  a.classes=reinterpret_cast<decltype(a.classes)>(symbol("biglybtc_tag_class_abi_v1"));
  a.notify=reinterpret_cast<decltype(a.notify)>(symbol("biglybtc_tag_gate_notify_peer_v1"));
  a.lanPolicy=symbol("biglybtc_tag_lan_bypass_set_v1");
  a.protocolPolicy=reinterpret_cast<decltype(a.protocolPolicy)>(symbol("biglybtc_protocol_bypass_set_v1"));
  if(!a.create||!a.close||!a.release||!a.acquire||!a.set||!a.bind||!a.features||!a.classes||!a.notify
      ||a.features()!=TORRENT_BIGLYBTC_SHARED_QUOTA_ABI||a.classes()!=TORRENT_BIGLYBTC_TAG_CLASS_ABI)return Api{};
  return a;
 }();return result;
}
#endif
#if BIGLYBTC_SHARED_QUOTA_HEADERS
Error error(int code){return code==-4?Error::Capacity:code==-2?Error::Lifetime:code==-1||code==-3?Error::Limits:Error::Engine;}
#endif
}
struct SdkSharedQuotaScope::Impl {
 const std::thread::id worker=std::this_thread::get_id();
 void* scope=nullptr;std::atomic_bool closed{false};
 ~Impl(){
#if BIGLYBTC_SHARED_QUOTA_HEADERS
  if(scope)api().release(scope);
#endif
 }
};
struct SdkSharedTagBudget::Impl {
 std::shared_ptr<SdkSharedQuotaScope::Impl> root;std::uint64_t tag=0;
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 biglybtc_shared_quota_v1 operations;
 ~Impl(){if(operations.budget)operations.release_budget(operations.budget);}
 static std::uint64_t epoch(const void* raw)noexcept{const auto& self=*static_cast<const Impl*>(raw);return self.operations.epoch(self.operations.budget);}
#endif
};
bool SdkSharedQuotaScope::supported()noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return api().create!=nullptr;
#else
 return false;
#endif
}
SdkSharedQuotaScope::SdkSharedQuotaScope(std::shared_ptr<Impl> p):impl_(std::move(p)){}
std::expected<std::unique_ptr<SdkSharedQuotaScope>,Error> SdkSharedQuotaScope::create(){
 if(!supported())return std::unexpected(Error::Unsupported);
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 try{auto p=std::make_shared<Impl>();p->scope=api().create();if(!p->scope)return std::unexpected(Error::Engine);return std::unique_ptr<SdkSharedQuotaScope>(new SdkSharedQuotaScope(std::move(p)));}catch(...){return std::unexpected(Error::Engine);}
#else
 return std::unexpected(Error::Unsupported);
#endif
}
SdkSharedQuotaScope::~SdkSharedQuotaScope(){
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 if(impl_&&!impl_->closed.exchange(true))api().close(impl_->scope);
#endif
}
std::expected<void,Error> SdkSharedQuotaScope::close()noexcept{
 if(impl_->worker!=std::this_thread::get_id())return std::unexpected(Error::Thread);
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 if(!impl_->closed.exchange(true))api().close(impl_->scope);
#endif
 return {};
}
std::expected<std::shared_ptr<SdkSharedTagBudget>,Error> SdkSharedQuotaScope::acquire(std::uint64_t tag,core::SavedTagRateLimits limits){
 if(impl_->worker!=std::this_thread::get_id())return std::unexpected(Error::Thread);
 if(impl_->closed.load())return std::unexpected(Error::Lifetime);
 if(!tag||!valid(limits))return std::unexpected(Error::Limits);
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 try{auto p=std::make_shared<SdkSharedTagBudget::Impl>();p->root=impl_;p->tag=tag;const int r=api().acquire(impl_->scope,tag,limits.upload,limits.download,&p->operations);if(r)return std::unexpected(error(r));return std::shared_ptr<SdkSharedTagBudget>(new SdkSharedTagBudget(std::move(p)));}catch(...){return std::unexpected(Error::Engine);}
#else
 return std::unexpected(Error::Unsupported);
#endif
}
SdkSharedTagBudget::SdkSharedTagBudget(std::shared_ptr<Impl> p):impl_(std::move(p)){}
SdkSharedTagBudget::~SdkSharedTagBudget()=default;
std::uint64_t SdkSharedTagBudget::tag()const noexcept{return impl_->tag;}
bool SdkSharedTagBudget::controls()const noexcept{return impl_->root->worker==std::this_thread::get_id();}
bool SdkSharedTagBudget::alive()const noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return Impl::epoch(impl_.get())!=0;
#else
 return false;
#endif
}
bool SdkSharedTagBudget::current()const noexcept{return controls()&&alive();}
std::optional<core::SavedTagRateLimits> SdkSharedTagBudget::observedLimits()const noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 if(!alive()){return {};}const auto& o=impl_->operations;o.enter(o.scope);
 const auto epoch=o.epoch(o.budget);const core::SavedTagRateLimits value{o.limit(o.budget,0),o.limit(o.budget,1)};o.leave(o.scope);if(epoch){return value;}
#endif
 return {};
}
std::optional<core::SavedTagRateLimits> SdkSharedTagBudget::limits()const noexcept{
 return controls()?observedLimits():std::optional<core::SavedTagRateLimits>{};
}
std::expected<void,Error> SdkSharedTagBudget::setLimits(core::SavedTagRateLimits limits){
 if(impl_->root->worker!=std::this_thread::get_id())return std::unexpected(Error::Thread);
 if(!valid(limits))return std::unexpected(Error::Limits);
 if(!current())return std::unexpected(Error::Lifetime);
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 const int r=api().set(impl_->operations.budget,limits.upload,limits.download);if(r)return std::unexpected(error(r));return {};
#else
 return std::unexpected(Error::Unsupported);
#endif
}
SdkSharedTagBudget::Witness SdkSharedTagBudget::witness(core::SavedTagRateLimits expected)const noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 if(!alive()){return {};}const auto& o=impl_->operations;o.enter(o.scope);
 const auto epoch=o.epoch(o.budget);const bool matches=o.limit(o.budget,0)==expected.upload&&o.limit(o.budget,1)==expected.download;o.leave(o.scope);
 if(epoch&&matches){return {impl_,Impl::epoch,epoch};}
#else
 (void)expected;
#endif
 return {};
}
bool SdkSharedTagBudget::supportsNetworkBindings()noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return api().lanPolicy!=nullptr;
#else
 return false;
#endif
}
bool SdkSharedTagBudget::supportsProtocolPolicy()noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return api().protocolPolicy!=nullptr;
#else
 return false;
#endif
}
int SdkSharedTagBudget::setProtocolPolicy(void* session,int flags)const noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return api().protocolPolicy?api().protocolPolicy(session,flags):-1;
#else
 (void)session;(void)flags;return -1;
#endif
}
bool SdkSharedTagBudget::matchesBinding(const void* scope,const void* budget)const noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return impl_->operations.scope==scope&&impl_->operations.budget==budget;
#else
 (void)scope;(void)budget;return false;
#endif
}
int SdkSharedTagBudget::refreshPeer(void* peer)const noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return api().notify?api().notify(peer):-1;
#else
 (void)peer;return -1;
#endif
}
int SdkSharedTagBudget::bind(void* session,std::uint32_t group)const noexcept{
#if BIGLYBTC_SHARED_QUOTA_HEADERS
 return api().bind(session,group,&impl_->operations);
#else
 (void)session;(void)group;return -1;
#endif
}
}
