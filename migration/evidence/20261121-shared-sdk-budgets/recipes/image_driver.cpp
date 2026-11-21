// SPDX-License-Identifier: GPL-2.0-or-later
// C-only test driver. The session/socket/class/allocators stay in its SDK image.
#include <libtorrent/session.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/biglybtc_shared_quota.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/aux_/bandwidth_manager.hpp>
#include <libtorrent/aux_/bandwidth_socket.hpp>
#include <boost/asio/post.hpp>
#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
namespace lt=libtorrent;
namespace {
struct Socket final:lt::aux::bandwidth_socket {
 std::atomic_int bytes{0};bool pending=false,closed=false;
 void assign_bandwidth(int,int amount)override{bytes.fetch_add(amount);pending=false;}
 bool is_disconnecting()const override{return closed;}
};
struct Owner {
 std::shared_ptr<Socket> socket=std::make_shared<Socket>();
 lt::peer_class_t group{0};
 std::unique_ptr<lt::session> session;
 Owner(){lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int k:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp})p.set_bool(k,false);session=std::make_unique<lt::session>(p);group=session->create_peer_class("generated-cross-image");}
 template<class F>void on(F operation){auto native=session->native_handle();auto p=std::make_shared<std::promise<void>>();auto done=p->get_future();boost::asio::post(native->get_context(),[native,p,operation=std::move(operation)]()mutable{try{operation(*native);p->set_value();}catch(...){p->set_exception(std::current_exception());}});if(done.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Driver deadline");done.get();}
};
}
extern "C" void* shared_image_create(const biglybtc_shared_quota_v1* operations)noexcept{
 try{auto owner=std::make_unique<Owner>();int result=-1;owner->on([&](auto& native){result=biglybtc_tag_shared_bind_v1(static_cast<lt::aux::session_interface*>(&native),static_cast<std::uint32_t>(owner->group),operations);});if(result)return nullptr;return owner.release();}catch(...){return nullptr;}
}
extern "C" void* shared_image_plain_create()noexcept{try{return new Owner;}catch(...){return nullptr;}}
// A rejected foreign allocator must not inspect an SDK C++ session object or
// invoke its callbacks. This sentinel makes an accidental dereference fail.
extern "C" int shared_image_foreign_probe(const biglybtc_shared_quota_v1* operations)noexcept{return biglybtc_tag_shared_bind_v1(reinterpret_cast<void*>(std::uintptr_t{1}),3,operations);}
extern "C" int shared_image_bind_again(void* raw,const biglybtc_shared_quota_v1* operations)noexcept{
 if(!raw||!operations){return -99;}try{auto& owner=*static_cast<Owner*>(raw);int result=-99;owner.on([&](auto& native){result=biglybtc_tag_shared_bind_v1(static_cast<lt::aux::session_interface*>(&native),static_cast<std::uint32_t>(owner.group),operations);});return result;}catch(...){return -99;}
}
extern "C" int shared_image_pump(void* raw)noexcept{
 if(!raw){return -1;}try{auto& owner=*static_cast<Owner*>(raw);owner.on([&](auto& native){if(owner.socket->pending)return;auto* pc=native.peer_classes().at(owner.group);if(!pc)throw std::runtime_error("Driver class");lt::aux::bandwidth_channel* channels[]={&pc->channel[0]};owner.socket->pending=true;const int granted=native.get_bandwidth_manager(0)->request_bandwidth(owner.socket,1,1,channels,1);if(granted)owner.socket->assign_bandwidth(0,granted);});return owner.socket->bytes.load();}catch(...){return -1;}
}
extern "C" int shared_image_bytes(void* raw)noexcept{return raw?static_cast<Owner*>(raw)->socket->bytes.load():-1;}
extern "C" void shared_image_destroy(void* raw)noexcept{delete static_cast<Owner*>(raw);}
extern "C" void* shared_image_sdk()noexcept{return reinterpret_cast<void*>(&lt::version);}
