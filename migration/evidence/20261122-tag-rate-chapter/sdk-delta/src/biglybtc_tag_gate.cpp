// SPDX-License-Identifier: BSD-3-Clause
#include "biglybtc_tag_gate.hpp"
#include "libtorrent/biglybtc_tag_gate.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/session_handle.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/request_blocks.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include <unordered_map>
#include <unordered_set>
#include "libtorrent/aux_/bandwidth_manager.hpp"
namespace {
using Classes=std::unordered_map<std::uint32_t,int>;
// Each default SDK session owns its network thread. Keys also distinguish
// multiple sessions on one external context;abort explicitly clears that key.
thread_local std::unordered_map<void const*,Classes> gates;
thread_local std::unordered_map<void const*,std::unordered_set<std::uint32_t>> lan_bypasses;
struct SharedState {void* budget=nullptr;std::uint64_t epoch=0;bool seen=false;};
thread_local std::unordered_map<void const*,std::unordered_map<std::uint32_t,SharedState>> shared_classes;
thread_local std::unordered_map<void const*,std::unordered_set<std::uint32_t>> changed_classes;
struct PendingReset {void const* session;int directions=0,protocol=0;};
thread_local std::unordered_map<void const*,int> protocol_policies;
thread_local std::unordered_map<libtorrent::peer_connection const*,PendingReset> pending_resets;
int mask(libtorrent::peer_connection const& peer) noexcept {
 const auto torrent=peer.associated_torrent().lock();if(!torrent){return 0;}auto& owner=torrent->session();auto session=gates.find(&owner);
 int result=0;for(int i=0;i<peer.num_classes();++i){const auto group=peer.class_at(i);
  if(session!=gates.end()){auto at=session->second.find(static_cast<std::uint32_t>(group));if(at!=session->second.end()){result|=at->second;}}
  if(const auto* actual=owner.peer_classes().at(group)){if(const auto* operations=actual->channel[0].shared_operations()){
   if(!operations->epoch(operations->budget)){result|=3;continue;}
   if(operations->limit(operations->budget,0)<0)result|=1;
   if(operations->limit(operations->budget,1)<0)result|=2;
  }}
 }return result;
}
}
namespace libtorrent {namespace biglybtc_tag_gate {
bool original_lan_peer(peer_connection const& peer) noexcept{
 if(!peer.has_class(session_handle::local_peer_class_id))return false;
 const auto torrent=peer.associated_torrent().lock();if(!torrent)return false;
 const auto* group=torrent->session().peer_classes().at(session_handle::local_peer_class_id);
 // Binding the real LAN class is the explicit common-network composition.
 // A bare SDK/standalone tag backend keeps its existing all-peer policy.
 return group&&group->channel[0].shared_operations()&&group->channel[1].shared_operations();
}
bool local_bypass(aux::session_interface const& session,std::uint32_t group) noexcept{
 const auto found=lan_bypasses.find(&session);return found!=lan_bypasses.end()&&found->second.contains(group);
}
bool upload_disabled(peer_connection const& peer) noexcept{return (mask(peer)&1)!=0;}
bool download_disabled(peer_connection const& peer) noexcept{return (mask(peer)&2)!=0;}
void clear_session(void* session) noexcept{
 gates.erase(session);shared_classes.erase(session);changed_classes.erase(session);lan_bypasses.erase(session);
 protocol_policies.erase(session);
 // Keep outstanding I/O classification until its callback,including abort.
 for(auto i=pending_resets.begin();i!=pending_resets.end();){
  if(i->second.session!=session){++i;continue;}i->second.directions=0;
  if(!i->second.protocol)i=pending_resets.erase(i);else ++i;
 }
}
bool protocol_enabled(aux::session_interface& session,peer_connection const& peer,int direction) noexcept{
 const auto policy=protocol_policies.find(&session);if(policy==protocol_policies.end()||!(policy->second&(1<<direction)))return false;
 const auto alive=[&](auto const& set){for(int n=0;n<set.num_classes();++n){
  const auto* group=session.peer_classes().at(set.class_at(n));if(!group)continue;
  for(const auto& channel:group->channel)if(const auto* operations=channel.shared_operations();operations&&!operations->epoch(operations->budget))return false;
 }return true;};
 if(!alive(peer))return false;const auto torrent=peer.associated_torrent().lock();return !torrent||alive(*torrent);
}
bool protocol_io(peer_connection const& peer,int direction) noexcept{
 const auto i=pending_resets.find(&peer);return i!=pending_resets.end()&&(i->second.protocol&(1<<direction));
}
bool mark_protocol_io(aux::session_interface const& session,peer_connection const& peer,int direction,bool enabled) noexcept{
 try{
  if(enabled){auto& value=pending_resets[&peer];value.session=&session;value.protocol|=1<<direction;}
  else if(auto i=pending_resets.find(&peer);i!=pending_resets.end()){
   i->second.protocol&=~(1<<direction);if(!i->second.protocol&&!i->second.directions)pending_resets.erase(i);
  }return true;
 }catch(...){return false;}
}
void forget_peer(peer_connection const& peer) noexcept{pending_resets.erase(&peer);}
void defer_grant_reset(peer_connection const& peer,int direction){
 const auto torrent=peer.associated_torrent().lock();if(!torrent)return;
 auto& value=pending_resets[&peer];value.session=&torrent->session();value.directions|=1<<direction;
}
bool consume_grant_reset(peer_connection const& peer,int direction) noexcept{
 auto i=pending_resets.find(&peer);if(i==pending_resets.end())return false;
 const bool result=(i->second.directions&(1<<direction))!=0;i->second.directions&=~(1<<direction);
 if(!i->second.directions&&!i->second.protocol)pending_resets.erase(i);return result;
}
bool register_shared_class(aux::session_interface* session,std::uint32_t group) noexcept {
 try{shared_classes[session][group]={};return true;}catch(...){return false;}
}
bool shared_changed(aux::session_interface& session) noexcept {
 auto found=shared_classes.find(&session);if(found==shared_classes.end()){changed_classes.erase(&session);return false;}bool changed=false;
 try{auto& affected=changed_classes[&session];affected.clear();
 for(auto i=found->second.begin();i!=found->second.end();){const auto* actual=session.peer_classes().at(libtorrent::peer_class_t{i->first});
  const auto* operations=actual?actual->channel[0].shared_operations():nullptr;
  if(!operations){i=found->second.erase(i);continue;}
  const auto epoch=operations->epoch(operations->budget);auto& previous=i->second;
  if(!previous.seen||previous.budget!=operations->budget||previous.epoch!=epoch){affected.insert(i->first);previous={operations->budget,epoch,true};changed=true;}++i;
 }
 if(found->second.empty()){shared_classes.erase(found);}return changed;
 }catch(...){changed_classes.erase(&session);return true;}
}
bool shared_peer_changed(peer_connection const& peer) noexcept {
 const auto torrent=peer.associated_torrent().lock();if(!torrent)return false;
 // On allocation failure,refresh every shared peer rather than lose a gate
 // transition. This does no source/profile work on the network thread.
 const auto found=changed_classes.find(&torrent->session());
 for(int i=0;i<peer.num_classes();++i){const auto group=peer.class_at(i);
  if(found!=changed_classes.end()){if(found->second.contains(static_cast<std::uint32_t>(group)))return true;}
  else if(const auto* actual=torrent->session().peer_classes().at(group);actual&&actual->channel[0].shared_operations())return true;
 }return false;
}
}}
extern "C" TORRENT_EXPORT int biglybtc_tag_gate_set_v1(void* opaque,std::uint32_t group,int flags) noexcept {
 if(!opaque||group<3||flags<0||flags>3){return -1;}auto* session=static_cast<libtorrent::aux::session_interface*>(opaque);
 if(!session->get_context().get_executor().running_in_this_thread()){return -2;}
 try{
  if(flags){gates[session][group]=flags;}
  else{auto at=gates.find(session);if(at!=gates.end()){at->second.erase(group);if(at->second.empty()){gates.erase(at);}}}
  return 0;
 }catch(...){return -3;}
}
extern "C" TORRENT_EXPORT int biglybtc_tag_gate_peer_v1(void* opaque) noexcept {
 if(!opaque){return -1;}auto* peer=static_cast<libtorrent::peer_connection*>(opaque);const auto torrent=peer->associated_torrent().lock();if(!torrent){return -1;}if(!torrent->session().get_context().get_executor().running_in_this_thread()){return -2;}return mask(*peer);
}
extern "C" TORRENT_EXPORT std::uint64_t biglybtc_tag_gate_features_v1() noexcept{return UINT64_C(0x4242544700010007);}

extern "C" TORRENT_EXPORT int biglybtc_tag_gate_notify_peer_v1(void* opaque) noexcept {
 if(!opaque){return -1;}auto* peer=static_cast<libtorrent::peer_connection*>(opaque);const auto torrent=peer->associated_torrent().lock();
 if(!torrent||peer->is_disconnecting()){return -1;}if(!torrent->session().get_context().get_executor().running_in_this_thread()){return -2;}
 try{
  for(int direction=0;direction<2;++direction){std::vector<libtorrent::aux::bandwidth_channel*> channels(std::size_t(peer->num_classes()+torrent->num_classes()));
   int count=torrent->session().copy_pertinent_channels(*peer,direction,channels.data(),int(channels.size()));
   if(!libtorrent::biglybtc_tag_gate::original_lan_peer(*peer))count+=torrent->session().copy_pertinent_channels(*torrent,direction,channels.data()+count,int(channels.size())-count);
   if(auto* manager=torrent->session().get_bandwidth_manager(direction))manager->refresh_peer_channels(static_cast<libtorrent::aux::bandwidth_socket const*>(peer),channels.data(),count);
  }
  peer->invalidate_tag_bandwidth();
  const int effective=mask(*peer);
  if((effective&1)&&!peer->is_choked()){peer->choke_this_peer();}
  else if(!(effective&1)&&peer->is_choked()&&peer->ignore_unchoke_slots()&&peer->is_peer_interested()&&!torrent->is_paused()&&!torrent->graceful_pause()){peer->send_unchoke();}
  if(effective&2){peer->cancel_all_requests();}
  else{peer->update_interest();libtorrent::request_a_block(*torrent,*peer);peer->send_block_requests();}
  torrent->trigger_unchoke();return effective;
 }catch(...){return -3;}
}

extern "C" TORRENT_EXPORT std::uint64_t biglybtc_tag_class_abi_v1() noexcept {return TORRENT_BIGLYBTC_TAG_CLASS_ABI;}

extern "C" TORRENT_EXPORT int biglybtc_tag_lan_bypass_set_v1(void* raw,std::uint32_t group,int enabled) noexcept{
 if(!raw||group<3||(enabled!=0&&enabled!=1))return -1;
 auto* session=static_cast<libtorrent::aux::session_interface*>(raw);
 if(!session->get_context().get_executor().running_in_this_thread())return -2;
 if(!session->peer_classes().at(libtorrent::peer_class_t{group}))return -1;
 try{
  if(enabled)lan_bypasses[session].insert(group);
  else if(auto i=lan_bypasses.find(session);i!=lan_bypasses.end()){
   i->second.erase(group);if(i->second.empty())lan_bypasses.erase(i);
  }
  return 0;
 }catch(...){return -3;}
}

extern "C" TORRENT_EXPORT int biglybtc_protocol_bypass_set_v1(void* raw,int flags) noexcept{
 if(!raw||flags<0||flags>3)return -1;auto* session=static_cast<libtorrent::aux::session_interface*>(raw);
 if(!session->get_context().get_executor().running_in_this_thread())return -2;
 try{if(flags)protocol_policies[session]=flags;else protocol_policies.erase(session);return 0;}catch(...){return -3;}
}
