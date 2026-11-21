// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_shared_tag_budget.hpp"
#include "sdk_tag_rate_group.hpp"
#include "transport_fixture.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <libtorrent/biglybtc_mixed_i2p_transport.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <array>
#include <iostream>

namespace lt=libtorrent;namespace e=biglybtc::engine;namespace c=biglybtc::core;
namespace {
unsigned assertions=0;
void check(bool condition,const char* reason){++assertions;if(!condition){throw std::runtime_error(reason);}}
lt::settings_pack isolated(){
 auto p=shared_transport_fixture::settings();
 p.set_str(lt::settings_pack::listen_interfaces,"");
 for(int key:{lt::settings_pack::enable_incoming_tcp,lt::settings_pack::enable_outgoing_tcp,lt::settings_pack::allow_i2p_mixed}){p.set_bool(key,false);}
 p.set_str(lt::settings_pack::i2p_hostname,"");p.set_int(lt::settings_pack::i2p_port,0);return p;
}
template<class Policy>void identity(Policy& p,std::uint16_t port){
 p.generation=71;p.capability.fill(0xd3);p.public_destination_hash.fill(0x63);
 p.bridge_port=port;p.peer_port=6881;p.local_peer_port=6881;
}
template<class Route>void hashes(Route& r,lt::info_hash_t const& h){
 r.hashes=(h.has_v1()?1U:0U)|(h.has_v2()?2U:0U);
 if(h.has_v1()){std::copy_n(reinterpret_cast<unsigned char const*>(h.v1.data()),20,r.torrent_v1.begin());}
 if(h.has_v2()){std::copy_n(reinterpret_cast<unsigned char const*>(h.v2.data()),32,r.torrent_v2.begin());}
}
}
int main(){try{
 shared_transport_fixture::Root root;
 lt::io_context fixture;lt::tcp::acceptor listener(fixture,lt::tcp::endpoint(lt::address_v4::loopback(),0));
 std::array<lt::session,3> sessions{lt::session(lt::session_params(isolated())),lt::session(lt::session_params(isolated())),lt::session(lt::session_params(isolated()))};
 auto work0=boost::asio::make_work_guard(sessions[0].native_handle()->get_context());
 auto work1=boost::asio::make_work_guard(sessions[1].native_handle()->get_context());
 auto work2=boost::asio::make_work_guard(sessions[2].native_handle()->get_context());
 const c::SavedTagRateLimits initial{16384,8192},disabled{-1,-1},tiny{1,9};
 auto scope=e::SdkSharedQuotaScope::create();check(bool(scope),"Actual SDK shared origin");
 auto budget=(*scope)->acquire(7,initial);check(bool(budget),"One common tag UID");
 std::array<lt::add_torrent_params,3> params;
 for(int n=0;n<3;++n){params[n]=shared_transport_fixture::torrent(root.path/std::to_string(n),char('r'+n));params[n].flags|=lt::torrent_flags::paused;params[n].flags&=~lt::torrent_flags::auto_managed;}
 lt::BiglybtcPrivateI2pOwnerPolicyV1 privatePolicy;identity(privatePolicy,listener.local_endpoint().port());privatePolicy.admission=101;hashes(privatePolicy,params[1].ti->info_hashes());
 check(lt::biglybtc_configure_private_i2p_owner_v1(&sessions[1],&privatePolicy)==0,"Actual dedicated private SDK owner");
 lt::BiglybtcMixedI2pTransportPolicyV1 mixedPolicy;identity(mixedPolicy,listener.local_endpoint().port());mixedPolicy.peer_id_mask.fill(1);
 check(lt::biglybtc_configure_mixed_i2p_transport_v1(&sessions[2],&mixedPolicy)==0,"Actual mixed SDK transport");
 lt::BiglybtcMixedI2pRouteV1 route;route.generation=mixedPolicy.generation;route.admission=201;hashes(route,params[2].ti->info_hashes());
 check(lt::biglybtc_admit_mixed_i2p_route_v1(&sessions[2],&route)==0,"Exact mixed route registration");
 std::array<std::unique_ptr<e::SdkTagRateGroup>,3> groups;
 std::array<lt::torrent_handle,3> handles;
 std::array<c::TagRateReadiness,3> before;
 for(int n=0;n<3;++n){
  auto group=e::SdkTagRateGroup::create(sessions[n],7,initial,*budget);check(bool(group),"Actual role session binds common SDK allocator");groups[n]=std::move(*group);
  lt::error_code ec;handles[n]=sessions[n].add_torrent(params[n],ec);check(!ec&&handles[n].is_valid(),"Exact admitted role handle");
  check(bool(groups[n]->attach(handles[n])),"Class attaches before payload resume");before[n]=groups[n]->readiness();check(before[n].currentFor(7,initial),"Per-role actual class witness");
 }
 check(!groups[0]->attach(handles[1]),"Foreign role handle cannot enter Public class");
 check(bool((*budget)->setLimits(disabled)),"One origin publishes both prohibited directions");
 for(int n=0;n<3;++n){check(!before[n].currentFor(7,initial),"Common publication revokes every prior SDK witness");check(bool(groups[n]->setLimits(disabled))&&groups[n]->readiness().currentFor(7,disabled),"Each role acknowledges exact prohibited directions");}
 // SDK class readiness proves rate ownership only. It never grants Private
 // profile/source/storage authority or removes Native's pre-storage fence.
 check(lt::biglybtc_revoke_mixed_i2p_route_v1(&sessions[2],&route)==0,"Mixed route withdraw");
 check(handles[2].is_valid()&&groups[2]->current(),"Mixed withdraw preserves retained handle and quota class");
 check(lt::biglybtc_withdraw_private_i2p_owner_v1(&sessions[1],privatePolicy.generation)==0,"Dedicated owner withdraw");
 check(groups[0]->readiness().currentFor(7,disabled),"Private withdraw leaves Public prohibition intact");
 check(bool(groups[1]->close()),"Retire private SDK class independently");
 check(groups[0]->current()&&groups[2]->current(),"Private class retirement preserves sibling classes");
 check(bool((*budget)->setLimits(tiny)),"Retained origin accepts tiny exact rates");
 for(int n:{0,2}){check(bool(groups[n]->setLimits(tiny))&&groups[n]->readiness().currentFor(7,tiny),"Remaining roles acknowledge exact tiny rates");}
 auto publicProof=groups[0]->readiness(),mixedProof=groups[2]->readiness();
 check(bool((*scope)->close()),"Close common SDK origin");
 check(!publicProof.currentFor(7,tiny)&&!mixedProof.currentFor(7,tiny),"Origin close revokes surviving role witnesses");
 for(int n:{0,2}){check(bool(groups[n]->close()),"Retire surviving SDK class");}
 check(lt::biglybtc_withdraw_mixed_i2p_transport_v1(&sessions[2],mixedPolicy.generation)==0,"Close mixed transport");
 work0.reset();work1.reset();work2.reset();
 std::cout<<"PASS_SHARED_ROLE_QUOTA "<<assertions<<" assertions;actual Public/private/mixed SDK sessions,one UID,paused exact handles,prohibition,tiny rates,withdrawal isolation,passive close;no I2P payload or Native profile authority claim\n";
 }catch(std::exception const& error){std::cerr<<"ROLE_QUOTA "<<error.what()<<'\n';return 1;}}
