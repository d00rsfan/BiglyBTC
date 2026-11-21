// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/hasher.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace shared_transport_fixture {
namespace lt=libtorrent;
struct Root {
 std::filesystem::path path;
 Root(){std::string pattern="/tmp/biglybtc-tag-rate-probe-XXXXXX";auto result=::mkdtemp(pattern.data());if(!result)throw std::runtime_error("Generated probe directory");path=result;}
 ~Root(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
lt::settings_pack settings(){lt::settings_pack pack;pack.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");pack.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");pack.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
 for(int key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_outgoing_utp,lt::settings_pack::enable_incoming_utp})pack.set_bool(key,false);
 pack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);pack.set_int(lt::settings_pack::unchoke_slots_limit,8);return pack;}
lt::add_torrent_params torrent(const std::filesystem::path& root,char byte){constexpr int size=4*1024*1024,piece=16384;const std::string content(size,byte),name=std::string(1,byte)+".bin";std::filesystem::create_directories(root);std::ofstream file(root/name,std::ios::binary);file.write(content.data(),content.size());file.close();
 lt::entry value(lt::entry::dictionary_type{});auto& info=value["info"];info["name"]=name;info["length"]=size;info["piece length"]=piece;std::string hashes;for(int at=0;at<size;at+=piece){auto hash=lt::hasher(content.data()+at,piece).final();hashes.append(hash.data(),20);}info["pieces"]=hashes;std::vector<char> buffer;lt::bencode(std::back_inserter(buffer),value);auto result=lt::load_torrent_buffer(buffer);result.save_path=root.string();return result;}
}
