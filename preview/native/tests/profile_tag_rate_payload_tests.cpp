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
 Root(){std::string pattern="/tmp/biglybtc-tag-rate-probe-XXXXXX";auto result=::mkdtemp(pattern.data());if(!result){throw std::runtime_error("Generated probe directory");}path=result;}
 ~Root(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
lt::settings_pack settings(){lt::settings_pack pack;pack.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");pack.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");pack.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
 for(int key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_outgoing_utp,lt::settings_pack::enable_incoming_utp})pack.set_bool(key,false);
 pack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);pack.set_int(lt::settings_pack::unchoke_slots_limit,8);return pack;}
lt::add_torrent_params torrent(const std::filesystem::path& root,char byte){constexpr int size=1024*1024,piece=16384;const std::string content(size,byte),name=std::string(1,byte)+".bin";std::filesystem::create_directories(root);std::ofstream file(root/name,std::ios::binary);file.write(content.data(),content.size());file.close();
 lt::entry value(lt::entry::dictionary_type{});auto& info=value["info"];info["name"]=name;info["length"]=size;info["piece length"]=piece;std::string hashes;for(int at=0;at<size;at+=piece){auto hash=lt::hasher(content.data()+at,piece).final();hashes.append(hash.data(),20);}info["pieces"]=hashes;std::vector<char> buffer;lt::bencode(std::back_inserter(buffer),value);auto result=lt::load_torrent_buffer(buffer);result.save_path=root.string();return result;}

#include "profile_tag_rates.hpp"
#include <map>
std::string txt(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string b32(const lt::sha1_hash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,buffer=0;for(char c:hash){buffer=(buffer<<8)|static_cast<unsigned char>(c);bits+=8;while(bits>=5){bits-=5;out+=chars[(buffer>>bits)&31];}}return out;}
int main(){try{using namespace biglybtc;Root root;std::filesystem::create_directories(root.path/"profile");auto held=storage::ProfileLease::acquire(root.path/"profile");if(!held){throw std::runtime_error("Generated profile ownership");}
 lt::session seed(settings()),leech(settings()),foreign(settings());std::vector<lt::add_torrent_params> parameters;std::string members="l";
 for(char byte:{'a','b'}){auto p=torrent(root.path/"seed",byte);members+=txt(b32(p.ti->info_hashes().v1));parameters.push_back(std::move(p));}members+='e';
 const auto write=[&](std::string rate,std::string roster){std::ofstream file(root.path/"profile/tag.config",std::ios::binary);file<<"d1:3d1:7d1:cd5:rl.upi"<<rate<<"ee1:n13:Generated tag1:o"<<roster<<"eee";};write("16384",members);
 auto mode=storage::TagManagerStartupMode::capture(*held);if(!mode){throw std::runtime_error("Actual startup mode");}auto rates=engine::ProfileTagRates::open(*held,*mode);if(!rates){throw std::runtime_error(rates.error().message);}
 std::vector<lt::torrent_handle> targets;std::vector<core::TorrentHash> hashes;
 for(auto& p:parameters){auto digest=p.ti->info_hashes().v1;auto hash=core::TorrentHash::parse({digest.data(),20});hashes.push_back(*hash);
  if(!(*rates)->prepare(*hash,seed)){throw std::runtime_error("Source prepare before paused admission");}p.flags|=lt::torrent_flags::seed_mode|lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;auto source=seed.add_torrent(p);
  if(!(*rates)->attach(*hash,source)){throw std::runtime_error("Saved rate attach");}source.resume();p.save_path=(root.path/"leech").string();p.flags&=~(lt::torrent_flags::seed_mode|lt::torrent_flags::paused);targets.push_back(leech.add_torrent(p));
 }
 if((*rates)->synchronize(foreign)){throw std::runtime_error("Foreign SDK owner accepted");}
 const auto until=std::chrono::steady_clock::now()+3s;while(!seed.listen_port()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(10ms);
 for(auto& target:targets)target.connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(seed.listen_port())});
 const auto total=[&]{std::int64_t n=0;for(auto h:targets)n+=h.status().total_payload_download;return n;};
 const auto begin=std::chrono::steady_clock::now()+5s;while(total()<4096&&std::chrono::steady_clock::now()<begin)std::this_thread::sleep_for(25ms);if(total()<4096){throw std::runtime_error("Saved group payload did not begin");}
 const auto baseline=total();const auto at=std::chrono::steady_clock::now();std::this_thread::sleep_for(8s);const auto bytes=total()-baseline;for(auto h:targets)if(h.status().total_payload_download<=0){throw std::runtime_error("Both generated torrents must receive");}
 if(bytes>175000||bytes<32000){throw std::runtime_error("Saved shared rate budget");}std::cout<<"SAVED_GROUP payload="<<bytes<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-at).count()<<'\n';
 // Delete current membership through source bytes;group must retire on existing peers.
 write("16384","le");if(!(*rates)->synchronize(seed)){throw std::runtime_error("Saved membership removal");}const auto finish=std::chrono::steady_clock::now()+8s;while(total()<2097152&&std::chrono::steady_clock::now()<finish)std::this_thread::sleep_for(20ms);if(total()!=2097152){throw std::runtime_error("Removed saved membership still limits peers");}
 if(!(*rates)->close()){throw std::runtime_error("Source owner close");}std::cout<<"PASS saved profile shared rate restoration,foreign owner,live membership removal and close\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
