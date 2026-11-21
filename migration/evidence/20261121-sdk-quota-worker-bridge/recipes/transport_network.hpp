// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <libtorrent/session_handle.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/extensions.hpp>
#include <boost/asio/post.hpp>
#include <future>
#include <stdexcept>
#include <atomic>
#include <thread>
namespace shared_transport_fixture {
template<class Function> void network(libtorrent::session_handle session,Function operation){
 auto owner=session.native_handle();if(!owner)throw std::runtime_error("Generated network owner lifetime");
 auto promise=std::make_shared<std::promise<void>>();auto done=promise->get_future();
 boost::asio::post(owner->get_context(),[owner,promise,operation=std::move(operation)]()mutable{try{operation();promise->set_value();}catch(...){promise->set_exception(std::current_exception());}});
 if(done.wait_for(std::chrono::seconds(5))!=std::future_status::ready){throw std::runtime_error("Generated network owner deadline");}done.get();
}
inline std::weak_ptr<libtorrent::peer_connection> capturePeer(libtorrent::session_handle session,libtorrent::torrent_handle torrent){
 std::weak_ptr<libtorrent::peer_connection> result;network(session,[&]{const auto actual=torrent.native_handle();if(!actual||actual->begin()==actual->end())throw std::runtime_error("Generated established peer");result=(*actual->begin())->self();});return result;
}
inline void keepAlive(libtorrent::session_handle session,const std::weak_ptr<libtorrent::peer_connection>& saved){
 network(session,[saved]{const auto peer=saved.lock();if(!peer||peer->is_disconnecting())throw std::runtime_error("Disabled tag lost the established peer");static const char keepalive[]={0,0,0,0};libtorrent::peer_connection_handle(peer).send_buffer(keepalive,4);});
}
struct DrainPeer final:libtorrent::peer_plugin {
 std::weak_ptr<libtorrent::peer_connection> peer;
 std::shared_ptr<std::atomic_bool> acknowledged;int accounted=0;
 DrainPeer(std::weak_ptr<libtorrent::peer_connection> p,std::shared_ptr<std::atomic_bool> a):peer(std::move(p)),acknowledged(std::move(a)){}
 bool on_unknown_message(int length,int message,libtorrent::span<char const> body)override{
  if(message!=99)return false;
  if(length!=9||body.size()>8)throw std::runtime_error("Generated drain marker shape");
  const int seen=int(body.size())+1;if(auto p=peer.lock())p->received_bytes(0,seen-accounted);accounted=seen;
  if(body.size()==8){static const char expected[]="SDKDRAIN";if(!std::equal(body.begin(),body.end(),expected))throw std::runtime_error("Generated drain marker bytes");acknowledged->store(true,std::memory_order_release);accounted=0;}
  return true;
 }
};
struct DrainTorrent final:libtorrent::torrent_plugin {
 std::shared_ptr<std::atomic_bool> acknowledged;
 explicit DrainTorrent(std::shared_ptr<std::atomic_bool> a):acknowledged(std::move(a)){}
 std::shared_ptr<libtorrent::peer_plugin> new_connection(libtorrent::peer_connection_handle const& handle)override{return std::make_shared<DrainPeer>(handle.native_handle(),acknowledged);}
};
inline std::shared_ptr<std::atomic_bool> installDrain(libtorrent::torrent_handle target){
 auto acknowledged=std::make_shared<std::atomic_bool>(false);target.add_extension([acknowledged](const auto&,libtorrent::client_data_t)->std::shared_ptr<libtorrent::torrent_plugin>{return std::make_shared<DrainTorrent>(acknowledged);});return acknowledged;
}
inline void drainPriorPayload(libtorrent::session_handle sender,libtorrent::torrent_handle source,const std::shared_ptr<std::atomic_bool>& acknowledged){
 bool sent=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
 while(!sent&&std::chrono::steady_clock::now()<deadline){network(sender,[&]{const auto actual=source.native_handle();if(!actual)return;for(auto* peer:*actual){libtorrent::peer_info info;peer->get_peer_info(info);if(peer->is_disconnecting()||!peer->upload_queue().empty()||info.pending_disk_read_bytes)continue;static const char marker[]={0,0,0,9,99,'S','D','K','D','R','A','I','N'};libtorrent::peer_connection_handle(peer->self()).send_buffer(marker,13);sent=true;break;}});if(!sent)std::this_thread::sleep_for(std::chrono::milliseconds(25));}
 if(!sent)throw std::runtime_error("Generated sender payload queue did not settle");
 while(!acknowledged->load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(25));
 if(!acknowledged->load())throw std::runtime_error("Generated ordered drain marker not received");
}
}
