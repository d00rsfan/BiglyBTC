// SPDX-License-Identifier: GPL-2.0-or-later
// Actual SDK backend only;no persisted profile or source-grant assertions.
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_info.hpp>



#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "sdk_tag_rate_group.hpp"
#include <future>
#include <limits>
#include <string_view>
namespace lt=libtorrent;using namespace std::chrono_literals;
struct Root {
 std::filesystem::path path;
 Root(){std::string pattern="/tmp/biglybtc-tag-rate-probe-XXXXXX";auto result=::mkdtemp(pattern.data());if(!result)throw std::runtime_error("Generated probe directory");path=result;}
 ~Root(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
lt::settings_pack settings(){lt::settings_pack pack;pack.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");pack.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");pack.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
 for(int key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_outgoing_utp,lt::settings_pack::enable_incoming_utp})pack.set_bool(key,false);
 pack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);pack.set_int(lt::settings_pack::unchoke_slots_limit,8);return pack;}
lt::add_torrent_params torrent(const std::filesystem::path& root,char byte){constexpr int size=1024*1024,piece=16384;const std::string content(size,byte),name=std::string(1,byte)+".bin";std::filesystem::create_directories(root);std::ofstream file(root/name,std::ios::binary);file.write(content.data(),content.size());file.close();
 lt::entry value(lt::entry::dictionary_type{});auto& info=value["info"];info["name"]=name;info["length"]=size;info["piece length"]=piece;std::string hashes;for(int at=0;at<size;at+=piece){auto hash=lt::hasher(content.data()+at,piece).final();hashes.append(hash.data(),20);}info["pieces"]=hashes;std::vector<char> buffer;lt::bencode(std::back_inserter(buffer),value);auto result=lt::load_torrent_buffer(buffer);result.save_path=root.string();return result;}
int main(int argc,char** argv){try{const bool guards=argc>1&&std::string_view(argv[1])=="guards";const bool download=argc>1&&!guards;using Group=biglybtc::engine::SdkTagRateGroup;using Error=biglybtc::engine::SdkTagRateError;Root root;lt::session seed(settings()),leech(settings()),foreign(settings());
 const auto limits=download?biglybtc::core::SavedTagRateLimits{0,16384}:biglybtc::core::SavedTagRateLimits{16384,0};auto created=Group::create(download?leech:seed,7,limits);if(!created)throw std::runtime_error("Actual SDK group creation");auto group=std::move(*created);
 for(const auto value:{-1,1,2,9,std::numeric_limits<std::int32_t>::max()}){
  for(bool up:{true,false}){const auto invalid=up?biglybtc::core::SavedTagRateLimits{value,0}:biglybtc::core::SavedTagRateLimits{0,value};
   auto refused=Group::create(seed,8,invalid);auto changed=group->setLimits(invalid);
   if(refused||changed||refused.error()!=Error::Limits||changed.error()!=Error::Limits){throw std::runtime_error("Unrepresentable SDK rate silently approximated");}
  }
 }
 if(guards){if(!group->close()||group->current()){throw std::runtime_error("Guard group close");}std::cout<<"PASS20 actual SDK unrepresentable-rate creation/update guards and close\n";return 0;}
 auto wrongThread=std::async(std::launch::async,[&]{auto result=group->setLimits({0,0});return !result&&result.error()==Error::Thread;});if(!wrongThread.get()||!group->current())throw std::runtime_error("Foreign control worker changed group");
 std::vector<lt::torrent_handle> sources,targets;
 for(char byte:{'a','b'}){auto params=torrent(root.path/"seed",byte);params.flags|=lt::torrent_flags::seed_mode;params.flags|=lt::torrent_flags::paused;params.flags&=~lt::torrent_flags::auto_managed;sources.push_back(seed.add_torrent(params));if(!download){auto attached=group->attach(sources.back());if(!attached)throw std::runtime_error("Initial paused upload group attachment");}params.flags&=~lt::torrent_flags::seed_mode;params.save_path=(root.path/"leech").string();targets.push_back(leech.add_torrent(params));if(download&&!group->attach(targets.back()))throw std::runtime_error("Initial paused download group attachment");sources.back().resume();targets.back().resume();}
 auto alienParams=torrent(root.path/"foreign",'c');alienParams.flags|=lt::torrent_flags::paused;auto alien=foreign.add_torrent(alienParams);auto refused=group->attach(alien);if(refused||refused.error()!=Error::Foreign||!group->current())throw std::runtime_error("Foreign actual session received a group");
 const auto ready=std::chrono::steady_clock::now()+3s;while(!seed.listen_port()&&std::chrono::steady_clock::now()<ready)std::this_thread::sleep_for(10ms);if(!seed.listen_port())throw std::runtime_error("Generated listener");
 for(auto& target:targets){target.connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(seed.listen_port())});}
 const auto total=[&]{std::int64_t bytes=0;for(const auto& target:targets){bytes+=target.status().total_payload_download;}return bytes;};
 const auto began=std::chrono::steady_clock::now()+(download?12s:5s);while(total()<4096&&std::chrono::steady_clock::now()<began)std::this_thread::sleep_for(25ms);if(total()<4096){
  std::cerr<<"SDK_GROUP_INITIAL current="<<group->current()<<" direction="<<(download?"download":"upload")<<'\n';
  for(std::size_t i=0;i<targets.size();++i){auto source=sources[i].status();auto target=targets[i].status();std::cerr<<"SDK_GROUP_INITIAL row="<<i<<" seed_state="<<int(source.state)<<" seed_peers="<<source.num_peers<<" target_state="<<int(target.state)<<" target_peers="<<target.num_peers<<" bytes="<<target.total_payload_download<<" error="<<target.errc.message()<<'\n';}
  for(const auto& handle:targets){std::vector<lt::peer_info> peers;handle.get_peer_info(peers);for(const auto& peer:peers){std::cerr<<"SDK_GROUP_PEER target flags="<<static_cast<std::uint32_t>(peer.flags)<<" down="<<peer.down_speed<<" payload="<<peer.payload_down_speed<<" queue="<<peer.download_queue_length<<" queued_bytes="<<peer.queue_bytes<<" recv="<<peer.used_receive_buffer<<'/'<<peer.receive_buffer_size<<" request_timeout="<<peer.request_timeout<<'\n';}}
  for(const auto& handle:sources){std::vector<lt::peer_info> peers;handle.get_peer_info(peers);for(const auto& peer:peers){std::cerr<<"SDK_GROUP_PEER seed flags="<<static_cast<std::uint32_t>(peer.flags)<<" upload_queue="<<peer.upload_queue_length<<" send="<<peer.used_send_buffer<<'/'<<peer.send_buffer_size<<" disk="<<peer.pending_disk_read_bytes<<'\n';}}
  throw std::runtime_error("Initial grouped payload did not start");
 }
 // Hold the global session briefly while changing this tag on established
 // connections. The measurement itself uses only the shared tag class.
 lt::settings_pack held;held.set_int(lt::settings_pack::upload_rate_limit,8192);seed.apply_settings(held);
 const auto& selected=download?targets:sources;
 for(const auto& source:selected){if(!group->detach(source)||!group->attach(source))throw std::runtime_error("Live detach/reattach did not join SDK work");}
 if(!group->close()){throw std::runtime_error("Old live group close");}
 auto fresh=Group::create(download?leech:seed,9,limits);if(!fresh){throw std::runtime_error("Fresh group creation during transfer");}group=std::move(*fresh);
 for(const auto& source:selected){if(!group->attach(source)){throw std::runtime_error("Fresh group did not attach to established peers");}}
 if(download){std::this_thread::sleep_for(2s);} // Drain credits from the previous binding before measuring the fresh group.
 held.set_int(lt::settings_pack::upload_rate_limit,0);seed.apply_settings(held);const auto before=total();const auto start=std::chrono::steady_clock::now();std::this_thread::sleep_for(download?12s:8s);
 const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();const auto limited=total()-before;
 for(const auto& target:targets){if(target.status().total_payload_download==0)throw std::runtime_error("One shared torrent received no payload");}
 if(limited<8192||limited>16384*elapsed*1.25+32768)throw std::runtime_error("Actual shared budget is missing or independent per torrent");
 if(!group->setLimits({0,0})){throw std::runtime_error("Live group limit removal");}const auto deadline=std::chrono::steady_clock::now()+8s;bool complete=false;
 while(std::chrono::steady_clock::now()<deadline){complete=true;for(const auto& target:targets){complete&=target.status().is_seeding;}if(complete)break;std::this_thread::sleep_for(25ms);}
 if(!complete)throw std::runtime_error("Live unlimited group did not complete transfers");
 if(!group->close()||group->current()||group->attach(sources.front())||group->setLimits({1,1}))throw std::runtime_error("Closed group revived");
 std::cout<<"PASS actual SDK shared tag group;two torrents,fresh live attach/detach,direction="<<(download?"download":"upload")<<",limited_payload="<<limited<<",seconds="<<elapsed<<",unlimited complete,foreign/thread/negative/close guards\n";
 }catch(const std::exception& error){std::cerr<<"SDK_TAG_RATE_GROUP "<<error.what()<<'\n';return 1;}}
