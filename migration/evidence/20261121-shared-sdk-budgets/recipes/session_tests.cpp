// SPDX-License-Identifier: GPL-2.0-or-later
// Generated-profile transport test of the actual isolated SDK, not a saved
// profile permission test. All sockets bind loopback; all payloads are generated.
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/biglybtc_shared_quota.hpp>
#include <libtorrent/biglybtc_tag_gate.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/peer_class.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/hasher.hpp>
#include <boost/asio/post.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <unistd.h>
#include "transport_network.hpp"
namespace lt=libtorrent;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function> void on(lt::session_handle session,Function operation){
 auto owner=session.native_handle();require(bool(owner),"Session lifetime");
 auto promise=std::make_shared<std::promise<void>>();auto done=promise->get_future();
 boost::asio::post(owner->get_context(),[owner,promise,operation=std::move(operation)]()mutable{
  try{operation(*owner);promise->set_value();}catch(...){promise->set_exception(std::current_exception());}
 });
 require(done.wait_for(5s)==std::future_status::ready,"SDK network-thread deadline");done.get();
}
struct Root {
 std::filesystem::path path;
 Root(){std::string pattern="/tmp/biglybtc-cross-owner-XXXXXX";const auto* p=::mkdtemp(pattern.data());require(p,"Generated root");path=p;}
 ~Root(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
struct Scope {
 void* value=biglybtc_shared_quota_scope_create_v1();
 Scope(){require(value,"Shared scope create");}
 ~Scope(){biglybtc_shared_quota_scope_close_v1(value);biglybtc_shared_quota_scope_release_v1(value);}
};
struct Budget {
 biglybtc_shared_quota_v1 operations;
 Budget(Scope& scope,std::uint64_t tag,int up,int down){require(biglybtc_shared_quota_acquire_v1(scope.value,tag,up,down,&operations)==0,"Shared budget acquire");}
 ~Budget(){operations.release_budget(operations.budget);}
 Budget(const Budget&)=delete;
 void set(int up,int down){require(biglybtc_shared_quota_set_v1(operations.budget,up,down)==0,"Shared budget mutation");}
};
struct Plugin final:lt::torrent_plugin {
 lt::aux::session_impl* session;lt::peer_class_t group;
 bool enabled=true;std::vector<std::weak_ptr<lt::peer_connection>> peers;
 Plugin(lt::aux::session_impl* s,lt::peer_class_t g):session(s),group(g){}
 std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& handle)override{
  const auto peer=handle.native_handle();if(!peer)return {};
  peers.push_back(peer);if(enabled)peer->add_class(session->peer_classes(),group);return {};
 }
 void apply(bool value){enabled=value;for(auto& saved:peers)if(auto peer=saved.lock()){
  if(peer->is_disconnecting())continue;
  if(value)peer->add_class(session->peer_classes(),group);else peer->remove_class(session->peer_classes(),group);
  require(biglybtc_tag_gate_notify_peer_v1(peer.get())==0,"Actual queued-channel refresh");
 }}
};
lt::settings_pack settings(){
 lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
 for(int k:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_outgoing_utp,lt::settings_pack::enable_incoming_utp})p.set_bool(k,false);
 p.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);p.set_bool(lt::settings_pack::rate_limit_ip_overhead,false);p.set_int(lt::settings_pack::unchoke_slots_limit,8);return p;
}
lt::add_torrent_params generated(const std::filesystem::path& root,char value){
 constexpr int size=4*1024*1024,piece=16384;std::filesystem::create_directories(root);
 const std::string name=std::string(1,value)+".bin",content(size,value);
 std::ofstream file(root/name,std::ios::binary);file.write(content.data(),content.size());file.close();
 lt::entry e(lt::entry::dictionary_type{});auto& i=e["info"];i["name"]=name;i["length"]=size;i["piece length"]=piece;
 std::string hashes;for(int at=0;at<size;at+=piece){auto hash=lt::hasher(content.data()+at,piece).final();hashes.append(hash.data(),20);}i["pieces"]=hashes;
 std::vector<char> data;lt::bencode(std::back_inserter(data),e);auto p=lt::load_torrent_buffer(data);p.save_path=root.string();
 p.flags|=lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;return p;
}
std::shared_ptr<Plugin> attach(lt::session_handle session,lt::torrent_handle torrent,Budget& budget){
 const auto group=session.create_peer_class("generated-shared-tag");std::shared_ptr<Plugin> plugin;
 const auto operations=budget.operations;
 on(session,[&](lt::aux::session_impl& native){require(biglybtc_tag_shared_bind_v1(static_cast<lt::aux::session_interface*>(&native),static_cast<std::uint32_t>(group),&operations)==0,"Tag class shared binding");plugin=std::make_shared<Plugin>(&native,group);});
 torrent.add_extension([plugin](const lt::torrent_handle&,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{return plugin;});
 on(session,[](auto&){});return plugin;
}
void global(lt::session_handle session,Budget& budget){
 lt::ip_filter filter;filter.add_rule(lt::make_address("0.0.0.0"),lt::make_address("255.255.255.255"),1);session.set_peer_class_filter(filter);
 const auto operations=budget.operations;on(session,[operations](auto& native){require(biglybtc_tag_shared_bind_v1(static_cast<lt::aux::session_interface*>(&native),0,&operations)==0,"Global class shared binding");});
}
}
int main(int argc,char** argv){try{
 const bool down=argc>1&&std::string_view(argv[1])=="download";
 const bool with_global=argc>2&&std::string_view(argv[2])=="global";
 const bool long_disabled=argc>3&&std::string_view(argv[3])=="long";
 require(biglybtc_tag_class_abi_v1()==TORRENT_BIGLYBTC_TAG_CLASS_ABI,"Exact new class ABI");
 require(biglybtc_shared_quota_features_v1()==TORRENT_BIGLYBTC_SHARED_QUOTA_ABI,"Exact shared callback ABI");
 Root root;Scope scope;constexpr int cap=16384;
 Budget tag(scope,7,down?0:cap,down?cap:0),same_tag(scope,7,down?0:cap,down?cap:0);
 require(tag.operations.budget==same_tag.operations.budget,"One identity for shared Source tag");
 Budget all(scope,1,with_global&&!down?8192:0,with_global&&down?8192:0);
 // Scope and public budget references outlive every real SDK session.
 std::array<lt::session,2> seeds{lt::session(settings()),lt::session(settings())};
 std::array<lt::session,2> leeches{lt::session(settings()),lt::session(settings())};
 std::array<lt::torrent_handle,2> source,target;
 std::array<std::shared_ptr<Plugin>,2> plugins;
 std::array<std::shared_ptr<std::atomic_bool>,2> drained;
 for(int n=0;n<2;++n){auto p=generated(root.path/("seed"+std::to_string(n)),char('a'+n));p.flags|=lt::torrent_flags::seed_mode;source[n]=seeds[n].add_torrent(p);
  p.flags&=~lt::torrent_flags::seed_mode;p.save_path=(root.path/("leech"+std::to_string(n))).string();target[n]=leeches[n].add_torrent(p);drained[n]=shared_transport_fixture::installDrain(target[n]);
  auto& session=down?leeches[n]:seeds[n];plugins[n]=attach(session,down?target[n]:source[n],n?same_tag:tag);if(with_global)global(session,all);
 }
 auto total=[&]{return target[0].status().total_payload_download+target[1].status().total_payload_download;};
 for(int n=0;n<2;++n){source[n].resume();target[n].resume();const auto deadline=std::chrono::steady_clock::now()+3s;while(!seeds[n].listen_port()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);
  require(seeds[n].listen_port()!=0,"Generated listener");target[n].connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(seeds[n].listen_port())});}
 const auto start=std::chrono::steady_clock::now(),deadline=start+14s;
 while((target[0].status().total_payload_download<16384||target[1].status().total_payload_download<16384)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(25ms);
 for(int n=0;n<2;++n)require(target[n].status().total_payload_download>=16384,"Both actual owners must progress");
 const auto before=total();const auto measure=std::chrono::steady_clock::now();std::this_thread::sleep_for(10s);
 const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-measure).count();const auto bytes=total()-before;const int effective=with_global?8192:cap;
 std::cout<<"MEASURE direction="<<(down?"download":"upload")<<" global="<<with_global<<" bytes="<<bytes<<" seconds="<<elapsed<<" owner0="<<target[0].status().total_payload_download<<" owner1="<<target[1].status().total_payload_download<<std::endl;
 require(bytes>=16384,"Shared transport stalled");require(bytes<=effective*elapsed*1.2+32768,"Budget was multiplied by real session count");
 const auto old_epoch=tag.operations.epoch(tag.operations.budget);tag.set(down?0:-1,down?-1:0);
 require(tag.operations.epoch(tag.operations.budget)!=old_epoch,"Cross-owner epoch must change");
 std::this_thread::sleep_for(3s);for(int n=0;n<2;++n)shared_transport_fixture::drainPriorPayload(seeds[n],source[n],drained[n]);const auto disabled=total();
 if(long_disabled){
  std::array<std::weak_ptr<lt::peer_connection>,2> original;
  for(int n=0;n<2;++n)original[n]=shared_transport_fixture::capturePeer(seeds[n],source[n]);
  const auto protocol_before=target[0].status().total_download+target[1].status().total_download-disabled;
  for(int tick=0;tick<12;++tick){for(int n=0;n<2;++n)shared_transport_fixture::keepAlive(seeds[n],original[n]);std::this_thread::sleep_for(1s);require(total()==disabled,"Long disabled interval leaked payload");}
  const auto protocol_after=target[0].status().total_download+target[1].status().total_download-total();require(protocol_after>protocol_before+16,"Disabled tag blocked real protocol exchange");
 }else std::this_thread::sleep_for(2s);
 require(total()==disabled,"Disabled shared direction leaked payload");
 // Remove the disabled tag from ONE owner. Its pending request must stop
 // borrowing that channel; the other owner's prohibition must remain live.
 on(down?leeches[0]:seeds[0],[&](auto&){plugins[0]->apply(false);});
 const auto other=target[1].status().total_payload_download,detached=target[0].status().total_payload_download;
 const auto detach_deadline=std::chrono::steady_clock::now()+10s;
 while(target[0].status().total_payload_download<=detached+16384&&std::chrono::steady_clock::now()<detach_deadline)std::this_thread::sleep_for(25ms);
 require(target[0].status().total_payload_download>detached+16384,"Live detached owner stayed bound to disabled budget");require(target[1].status().total_payload_download==other,"Detach lifted sibling prohibition");
 tag.set(down?0:cap,down?cap:0);on(down?leeches[0]:seeds[0],[&](auto&){plugins[0]->apply(true);});
 const auto sibling_deadline=std::chrono::steady_clock::now()+10s;
 while(target[1].status().total_payload_download<=other+16384&&std::chrono::steady_clock::now()<sibling_deadline)std::this_thread::sleep_for(25ms);
 require(target[1].status().total_payload_download>other+16384,"Sibling failed to resume");
 biglybtc_shared_quota_scope_close_v1(scope.value);
 require(!tag.operations.epoch(tag.operations.budget)&&!all.operations.epoch(all.operations.budget),"Scope close must revoke every passive witness");
 std::this_thread::sleep_for(3s);const auto closed=total();std::this_thread::sleep_for(2s);require(total()==closed,"Closed shared scope became unlimited");
 std::cout<<"PASS actual SDK two-session aggregate,shared tag identity,global composition,disabled direction,live detach/reattach,sibling isolation and closed scope\n";
 }catch(const std::exception& e){std::cerr<<"SHARED_SESSION "<<e.what()<<'\n';return 1;}}
