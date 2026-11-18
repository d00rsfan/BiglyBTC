// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_tag_rate_group.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_status.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>
namespace lt=libtorrent;
using namespace std::chrono_literals;
using Group=biglybtc::engine::SdkTagRateGroup;
using Limits=biglybtc::core::SavedTagRateLimits;
void need(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
struct Root{
 std::filesystem::path path;
 Root(){std::string pattern="/tmp/biglybtc-tag-directions-XXXXXX";auto made=::mkdtemp(pattern.data());need(made,"Generated fixture directory");path=made;}
 ~Root(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
lt::settings_pack settings(){
 lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
 for(auto key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_outgoing_utp,lt::settings_pack::enable_incoming_utp}){p.set_bool(key,false);}
 p.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);p.set_int(lt::settings_pack::unchoke_slots_limit,8);return p;
}
lt::add_torrent_params torrent(const std::filesystem::path& path,bool first,int size=2*1024*1024){
 constexpr int piece=16384;std::string data(size,'\0');for(int i=0;i<size;++i){data[i]=char(1+(i*31+i/piece)%251);}
 std::filesystem::create_directories(path);auto half=data;if(first){half.replace(size/2,size/2,std::string(size/2,'\0'));}else{half.replace(0,size/2,std::string(size/2,'\0'));}
 std::ofstream out(path/"duplex.bin",std::ios::binary);out.write(half.data(),half.size());out.close();
 lt::entry value(lt::entry::dictionary_type{});auto& info=value["info"];info["name"]="duplex.bin";info["length"]=size;info["piece length"]=piece;std::string hashes;
 for(int offset=0;offset<size;offset+=piece){auto hash=lt::hasher(data.data()+offset,piece).final();hashes.append(hash.data(),20);}
 info["pieces"]=hashes;std::vector<char> encoded;lt::bencode(std::back_inserter(encoded),value);auto params=lt::load_torrent_buffer(encoded);params.save_path=path.string();params.flags&=~lt::torrent_flags::auto_managed;params.flags|=lt::torrent_flags::paused;return params;
}
template<class F>bool await(F condition,std::chrono::seconds deadline){auto end=std::chrono::steady_clock::now()+deadline;while(!condition()&&std::chrono::steady_clock::now()<end){std::this_thread::sleep_for(20ms);}return condition();}
int main(int argc,char** argv){try{
 const std::string mode=argc>1?argv[1]:"upload";
 const int fanIn=argc>2?std::stoi(argv[2]):1;need(fanIn>=1&&fanIn<=256,"Directional cohort bound");
 if(mode=="abi-refuse"){
  need(!Group::supports({0,0})&&!Group::supports({0,10})&&!Group::supports({-1,1}),"Wrong private ABI accepted some group values");
  auto refused=Group::create(lt::session_handle{},7,{0,10});need(!refused&&refused.error()==biglybtc::engine::SdkTagRateError::Limits,"Wrong ABI reached SDK handle/class access");
  std::cout<<"PASS incompatible class ABI refused for unlimited,ordinary,disabled and tiny groups before SDK access\n";return 0;
 }

 if(!Group::supports({-1,1})){
  need(!Group::supports({-1,0})&&!Group::supports({0,1})&&Group::supports({0,10}),"Old provider representation guard");
  std::cout<<"PASS older SDK refuses disabled and tiny rates explicitly\n";return 0;
 }
 Root root;lt::session a(settings()),b(settings());
 if(mode=="tiny"){
  auto made=Group::create(a,7,{});need(bool(made),"Actual group without torrents");auto group=std::move(*made);
  for(int value=1;value<=9;++value){need(bool(group->setLimits({value,value})),"Exact tiny class update");need(group->readiness().currentFor(7,{value,value}),"Exact tiny SDK getter fence");}
  auto old=group->readiness();need(bool(group->close())&&!old.currentFor(7,{9,9}),"Anchorless close revokes readiness");
  std::cout<<"PASS9 exact tiny SDK group settings and anchorless retirement\n";return 0;
 }
 std::vector<std::unique_ptr<Group>> additional;
 for(int i=1;i<fanIn;++i){auto made=Group::create(a,100+i,{});need(bool(made),"Additional directional owner");additional.push_back(std::move(*made));}
 const bool download=mode=="download";const bool live=mode=="live";const Limits limits=live?Limits{32768,0}:(download?Limits{0,-1}:Limits{-1,0});auto made=Group::create(a,7,limits);need(bool(made),"Actual disabled group creation");auto group=std::move(*made);
 if(mode=="reuse"){need(bool(group->close()),"Anchorless disabled owner cleanup");auto fresh=Group::create(a,9,{});need(bool(fresh),"Reused unlimited class slot");group=std::move(*fresh);}
 std::unique_ptr<Group> second;if(mode=="owners"){auto madeSecond=Group::create(a,8,limits);need(bool(madeSecond),"Independent disabled owner");second=std::move(*madeSecond);}
 const int bytes=live?16*1024*1024:2*1024*1024;auto hA=a.add_torrent(torrent(root.path/"a",true,bytes)),hB=b.add_torrent(torrent(root.path/"b",false,bytes));for(auto& extra:additional){need(bool(extra->attach(hA)),"Attach earlier neutral class");}need(bool(group->attach(hA)),"Attach actual group before payload");if(second){need(bool(second->attach(hA)),"Attach independent group");}
 need(!group->attach(hB),"Foreign SDK session cannot borrow group");hA.resume();hB.resume();
 need(await([&]{return hA.status().num_pieces==bytes/32768&&hB.status().num_pieces==bytes/32768&&a.listen_port();},5s),"Complementary checked halves");hB.connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(a.listen_port())});
 if(mode=="reuse"){need(await([&]{return hA.status().is_seeding&&hB.status().is_seeding;},10s),"Retired class left a stale directional gate");std::cout<<"PASS actual SDK class reuse has no retired gate\n";return 0;}
 auto allowed=[&]{return download?hB.status().total_payload_download:hA.status().total_payload_download;};auto blocked=[&]{return download?hA.status().total_payload_download:hB.status().total_payload_download;};
 need(await([&]{return allowed()>=65536;},8s),"Allowed direction progress");
 if(live){
  need(await([&]{return blocked()>=32768;},8s),"Initial live upload progress");auto previous=group->readiness();need(bool(group->setLimits({-1,0})),"Disable upload on live peer");need(!previous.currentFor(7,limits),"Old readiness survived live policy mutation");
  std::this_thread::sleep_for(1500ms);const auto stopped=blocked();need(stopped<bytes/2,"Live disable completed all payload");std::this_thread::sleep_for(1500ms);need(blocked()==stopped,"Disabled established peer continued payload");
 }else{std::this_thread::sleep_for(1500ms);need(blocked()==0,"Disabled direction leaked payload");}
 if(second){need(bool(group->setLimits({0,0})),"Clear first owner");std::this_thread::sleep_for(1500ms);need(blocked()==0,"Second owner's independent prohibition lost");need(bool(second->close()),"Retire second owner on live connection");}
 else if(mode=="detach"){need(bool(group->detach(hA)),"Detach current class and wake peer");}
 else{need(bool(group->setLimits({0,0})),"Remove live prohibition and wake both directions");}
 need(await([&]{return hA.status().is_seeding&&hB.status().is_seeding;},10s),"Both directions resume without reconnect");
 need(bool(group->close()),"Close current group");std::cout<<"PASS actual SDK tag group mode="<<mode<<" tag_groups="<<fanIn<<" direction_gate=1 permitted_payload="<<allowed()<<" resumed_both=1\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
