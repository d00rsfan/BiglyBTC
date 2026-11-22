// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_network_rates.hpp"
#include "sdk_tag_rate_group.hpp"
#include "sdk_prepared_tag_attachment.hpp"
#include "transport_fixture.hpp"
#include "transport_network.hpp"
#include <libtorrent/ip_filter.hpp>
#include <array>
#include <iostream>
namespace lt=libtorrent;namespace e=biglybtc::engine;namespace c=biglybtc::core;
using namespace std::chrono_literals;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
 const bool down=argc>1&&std::string_view(argv[1])=="download";
 const bool local=argc>2&&std::string_view(argv[2])=="lan";
 const int cap=local?32768:16384;
 shared_transport_fixture::Root root;{auto scope=e::SdkSharedQuotaScope::create();require(bool(scope),"Actual SDK quota scope");
 auto global=(*scope)->acquire(e::SdkNetworkRates::GlobalKey,{down?0:16384,down?16384:0});
 auto lan=(*scope)->acquire(e::SdkNetworkRates::LanKey,{down?0:32768,down?32768:0});require(bool(global)&&bool(lan),"Distinct common global/LAN cells");
 const auto settings=[](){auto p=shared_transport_fixture::settings();p.set_bool(lt::settings_pack::rate_limit_ip_overhead,false);p.set_int(lt::settings_pack::in_enc_policy,lt::settings_pack::pe_forced);p.set_int(lt::settings_pack::out_enc_policy,lt::settings_pack::pe_forced);p.set_int(lt::settings_pack::allowed_enc_level,lt::settings_pack::pe_rc4);p.set_bool(lt::settings_pack::prefer_rc4,true);return p;};
 std::array<lt::session,2> limited{lt::session(settings()),lt::session(settings())};
 std::array<lt::session,2> other{lt::session(settings()),lt::session(settings())};
 std::array<std::unique_ptr<e::SdkNetworkRates>,2> owners;
 std::array<std::unique_ptr<e::SdkTagRateGroup>,2> tags;
 std::shared_ptr<e::SdkSharedTagBudget> tag;
 if(local){auto acquired=(*scope)->acquire(7,{down?0:1024,down?1024:0});require(bool(acquired),"Positive tag cell");tag=std::move(*acquired);}
 std::array<lt::torrent_handle,2> seed,leech;
 std::array<std::shared_ptr<std::atomic_bool>,2> dataMarkers,requestMarkers;
 for(int n=0;n<2;++n){
  auto owner=e::SdkNetworkRates::create(limited[n],*global,*lan,false,false);require(bool(owner),"Fresh actual SDK global/LAN binding");owners[n]=std::move(*owner);
  lt::ip_filter filter;const auto id=local?lt::session_handle::local_peer_class_id:lt::session_handle::global_peer_class_id;const unsigned mask=1U<<static_cast<unsigned>(id);
  filter.add_rule(lt::make_address("0.0.0.0"),lt::make_address("255.255.255.255"),mask);filter.add_rule(lt::make_address("::"),lt::make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),mask);limited[n].set_peer_class_filter(filter);
  if(local){auto group=e::SdkTagRateGroup::create(limited[n],7,{down?0:1024,down?1024:0},tag);require(bool(group)&&bool((*group)->configureOriginalLanBypass(true)),"Actual Original LAN tag policy");tags[n]=std::move(*group);}
  auto p=shared_transport_fixture::torrent(root.path/("seed"+std::to_string(n)),char('c'+n));p.flags|=lt::torrent_flags::seed_mode|lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;
  auto q=p;q.flags&=~lt::torrent_flags::seed_mode;q.save_path=(root.path/("leech"+std::to_string(n))).string();auto params=down?q:p;
  std::unique_ptr<e::SdkPreparedTagAttachment> prepared;
  if(local){auto token=tags[n]->prepareAttachment(params.ti->info_hashes());require(bool(token),"LAN constructor tag preparation");prepared=std::move(*token);require(bool(prepared->install(params)),"LAN constructor attachment");}
  const auto handle=limited[n].add_torrent(params);if(local)require(prepared->boundTo(handle),"Actual pre-resume LAN class");
  seed[n]=down?other[n].add_torrent(p):handle;leech[n]=down?handle:other[n].add_torrent(q);
  dataMarkers[n]=shared_transport_fixture::installDrain(leech[n]);requestMarkers[n]=shared_transport_fixture::installDrain(seed[n]);
 }
 const auto bytes=[&]{return leech[0].status().total_payload_download+leech[1].status().total_payload_download;};
 for(int n=0;n<2;++n){seed[n].resume();leech[n].resume();auto sender=down?lt::session_handle(other[n]):lt::session_handle(limited[n]);const auto end=std::chrono::steady_clock::now()+5s;while(!sender.listen_port()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(10ms);require(sender.listen_port()!=0,"Actual listener");leech[n].connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(sender.listen_port())});}
 const auto end=std::chrono::steady_clock::now()+35s;
 while(std::chrono::steady_clock::now()<end&&(leech[0].status().total_payload_download<16384||leech[1].status().total_payload_download<16384))std::this_thread::sleep_for(25ms);
 for(auto h:leech)require(h.status().total_payload_download>=16384,"Both actual SDK streams must progress");
 for(auto h:leech){std::vector<lt::peer_info> peers;h.get_peer_info(peers);require(!peers.empty()&&std::ranges::all_of(peers,[](const auto& peer){return bool(peer.flags&lt::peer_info::rc4_encrypted);}),"Actual forced MSE/RC4 peer required");}
 const auto before=bytes();const auto started=std::chrono::steady_clock::now();std::this_thread::sleep_for(10s);const auto transferred=bytes()-before;const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
 std::cout<<"NETWORK_PAYLOAD mode="<<(local?"lan":"wan")<<" direction="<<(down?"download":"upload")<<" bytes="<<transferred<<" seconds="<<seconds<<" cap="<<cap<<'\n';
 for(int n=0;n<2;++n)shared_transport_fixture::network(limited[n],[&,n]{for(auto h:limited[n].get_torrents()){auto t=h.native_handle();if(t)for(auto* peer:*t)std::cerr<<"SPAN n="<<n<<" bytes="<<peer->biglybtc_bandwidth_span(1)<<'\n';}});
 for(int n=0;n<2;++n){for(auto h:{seed[n],leech[n]}){std::vector<lt::peer_info> peers;h.get_peer_info(peers);for(const auto& peer:peers)std::cerr<<"PEER n="<<n<<" seed="<<(h==seed[n])<<" down="<<peer.total_download<<" up="<<peer.total_upload<<" rq="<<peer.receive_quota<<" sq="<<peer.send_quota<<" rs="<<int(peer.read_state)<<" ws="<<int(peer.write_state)<<" recv="<<peer.used_receive_buffer<<'/'<<peer.receive_buffer_size<<" send="<<peer.used_send_buffer<<" dq="<<peer.download_queue_length<<" flags="<<static_cast<unsigned>(peer.flags)<<'\n';}}
 require(transferred>=32768&&transferred<cap*seconds*1.2+32768,"Actual global/LAN common budget multiplied or stalled;positive LAN tag was not bypassed");
 if(local){const c::SavedTagRateLimits disabled{down?0:-1,down?-1:0};require(bool(tags[0]->setLimits(disabled))&&bool(tags[1]->setLimits(disabled)),"LAN prohibition update");
  if(down)for(int n=0;n<2;++n)shared_transport_fixture::drainPriorPayload(limited[n],leech[n],requestMarkers[n]);
  for(int n=0;n<2;++n)shared_transport_fixture::drainPriorPayload(down?lt::session_handle(other[n]):lt::session_handle(limited[n]),seed[n],dataMarkers[n]);
  const auto held=bytes();std::this_thread::sleep_for(2s);require(bytes()==held,"LAN bypass removed a directional prohibition");
  const c::SavedTagRateLimits positive{down?0:1024,down?1024:0};require(bool(tags[0]->setLimits(positive))&&bool(tags[1]->setLimits(positive)),"LAN positive recovery");
  const auto deadline=std::chrono::steady_clock::now()+15s;while(bytes()==held&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(25ms);require(bytes()>held,"LAN directional prohibition did not recover");
 }
 require(bool((*global)->setLimits({0,0}))&&bool((*lan)->setLimits({0,0})),"Final network quota removal");
 for(auto& owner:owners)require(bool(owner->refresh()),"Final quota removal acknowledgement");
 const auto completed=std::chrono::steady_clock::now()+20s;while(std::chrono::steady_clock::now()<completed&&(!leech[0].status().is_seeding||!leech[1].status().is_seeding))std::this_thread::sleep_for(25ms);
 require(leech[0].status().is_seeding&&leech[1].status().is_seeding,"Forced MSE payloads must complete verification");
 } // Join all four actual SDK sessions before reading generated payload files.
 for(int n=0;n<2;++n){const char value=char('c'+n);std::ifstream file(root.path/("leech"+std::to_string(n))/(std::string(1,value)+".bin"),std::ios::binary);require(bool(file),"Joined MSE payload readback");const std::string bytes{std::istreambuf_iterator<char>(file),{}};require(bytes==std::string(4*1024*1024,value),"Joined MSE payload exact bytes");}
 std::cout<<"PASS_FORCED_MSE_NETWORK_PAYLOAD verified_bytes=8388608;four SDK joins before readback\n";
 }catch(std::exception const& error){std::cerr<<"NETWORK_PAYLOAD "<<error.what()<<'\n';return 1;}}
