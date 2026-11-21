// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_shared_tag_budget.hpp"
#include "sdk_tag_rate_group.hpp"
#include "worker_fixture.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
namespace e=biglybtc::engine;namespace c=biglybtc::core;namespace lt=libtorrent;
using namespace std::chrono_literals;
namespace {
int assertions=0;
void require(bool value,const char* message){++assertions;if(!value){throw std::runtime_error(message);}}
lt::settings_pack isolated(){lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int k:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp}){p.set_bool(k,false);}return p;}
using worker_fixture::State;using worker_fixture::Remote;
}
int main(int argc,char** argv){try{
 const bool baseline=argc>1&&std::string_view(argv[1])=="baseline";
 if(!e::SdkSharedQuotaScope::supported()){
  auto refused=e::SdkSharedQuotaScope::create();require(!refused&&refused.error()==e::SdkSharedQuotaError::Unsupported,"Legacy SDK optional refusal");
  std::cout<<"PASS_WORKER_UNSUPPORTED ordinary SDK exposes no shared quota API;normal fallback preserved\n";return 0;
 }
 constexpr std::uint64_t uid=77;const c::SavedTagRateLimits initial{16384,0},changed{32768,-1};
 auto scope=e::SdkSharedQuotaScope::create();require(bool(scope),"Common quota origin creation");auto budget=(*scope)->acquire(uid,initial);require(bool(budget),"Common quota acquisition");
 lt::session local(isolated());auto first=e::SdkTagRateGroup::create(local,uid,initial,*budget);require(bool(first),"Origin-worker SDK class");auto original=(*first)->readiness();require(original.currentFor(uid,initial),"Origin class exact witness");
 Remote remote;
 auto created=remote.call([&](State& state){state.sdk=std::make_unique<lt::session>(isolated());auto group=e::SdkTagRateGroup::create(*state.sdk,uid,initial,*budget);if(group){state.group=std::move(*group);}return bool(group);});
 if(baseline){require(!created,"Old wrapper must expose the control-worker barrier");std::cout<<"PASS_BASELINE worker-affine budget refuses a different SDK-control worker\n";return 0;}
 require(created,"SDK consumer could not bind the origin budget on its own control worker");
 auto second=remote.call([](State& state){return state.group->readiness();});require(second.currentFor(uid,initial),"Consumer's passive readiness must cross back without a worker callback");
 auto pointer=remote.call([](State& state){return state.group.get();});require(!pointer->current(),"SDK class lost its own control-worker affinity");auto wrong=pointer->setLimits(initial);require(!wrong&&wrong.error()==e::SdkTagRateError::Thread,"Foreign class control must refuse");
 auto denied=remote.call([&](State& state){
  const bool hidden=!(*budget)->current()&&!(*budget)->limits();auto direct=(*budget)->setLimits(changed);auto proposed=state.group->setLimits(changed);auto wrongTag=e::SdkTagRateGroup::create(*state.sdk,uid+1,initial,*budget);
  return hidden&&!direct&&direct.error()==e::SdkSharedQuotaError::Thread&&!proposed&&proposed.error()==e::SdkTagRateError::Limits&&!wrongTag&&state.group->current();
 });require(denied,"Consumer mutated origin policy,accepted foreign UID,or lost the existing class");
 require(original.currentFor(uid,initial)&&second.currentFor(uid,initial),"Rejected consumer proposal revoked unchanged proofs");
 require(bool((*budget)->setLimits(changed)),"Origin control must publish changed common limits");require(!original.currentFor(uid,initial)&&!second.currentFor(uid,initial),"Origin mutation failed to revoke cross-worker proofs");
 require(!remote.call([](State& state){return state.group->readiness();}).currentFor(uid,initial),"Consumer minted an old-limit proof after origin mutation");
 require(bool(remote.call([&](State& state){return state.group->setLimits(changed);})),"Consumer must acknowledge the exact already-published origin limits");require(bool((*first)->setLimits(changed)),"Origin SDK class acknowledgement");
 auto updated=remote.call([](State& state){return state.group->readiness();});require(updated.currentFor(uid,changed)&&(*first)->readiness().currentFor(uid,changed),"Complete cross-worker exact limits");
 require(bool(remote.call([&](State& state){return state.group->setLimits(changed);})),"Consumer exact no-op");require(updated.currentFor(uid,changed),"Consumer no-op revoked exact readiness");
 auto foreignRoot=remote.call([&](State&){return (*scope)->close();});require(!foreignRoot&&foreignRoot.error()==e::SdkSharedQuotaError::Thread,"Foreign scope close must refuse");require(updated.currentFor(uid,changed),"Refused foreign close revoked the origin scope");
 require(bool((*scope)->close()),"Origin scope close");require(!updated.currentFor(uid,changed)&&!(*first)->current(),"Origin close left passive classes ready");require(!remote.call([](State& state){return state.group->current();}),"Remote consumer survived closed quota origin");
 require(bool(remote.call([](State& state){return state.group->close();}))&&bool((*first)->close()),"SDK class retirement on each actual control worker");
 std::cout<<"PASS_WORKER_BRIDGE "<<assertions<<";origin-only quota mutation,read-only remote SDK consumer,exact limits,own worker,foreign UID,no-op,passive epochs,close and joined workers\n";
 }catch(const std::exception& error){std::cerr<<"WORKER_BRIDGE "<<error.what()<<'\n';return 1;}}
