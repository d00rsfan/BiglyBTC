// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "transport_fixture.hpp"
#include "transport_network.hpp"
#include <libtorrent/session.hpp>
#include <QCoreApplication>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
namespace lt=libtorrent;namespace e=biglybtc::engine;namespace c=biglybtc::core;namespace s=biglybtc::storage;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
std::string b32(const c::TorrentHash& hash){constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,buffer=0;for(auto byte:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;out+=alphabet[(buffer>>bits)&31];}}if(bits){out+=alphabet[(buffer<<(5-bits))&31];}return out;}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 const bool down=argc>1&&std::string_view(argv[1])=="download";constexpr int cap=16384;
 shared_transport_fixture::Root root;const auto profile=root.path/"profile";std::filesystem::create_directories(profile);
 std::array<lt::session,2> seeds{lt::session(shared_transport_fixture::settings()),lt::session(shared_transport_fixture::settings())};
 std::array<lt::session,2> leeches{lt::session(shared_transport_fixture::settings()),lt::session(shared_transport_fixture::settings())};
 std::array<lt::add_torrent_params,2> params;
 for(int n=0;n<2;++n){params[n]=shared_transport_fixture::torrent(root.path/("seed"+std::to_string(n)),char('a'+n));}
 const auto hash=[&](int n){return c::TorrentHash::parse(std::string_view(params[n].ti->info_hashes().v1.data(),20)).value();};
 auto write=[&](int rate,bool first=true){lt::entry value(lt::entry::dictionary_type{});auto& tag=value["3"]["7"];tag["n"]="Generated tag";tag["c"][down?"rl.down":"rl.up"]=rate;tag["o"]=lt::entry::list_type{};for(int n=first?0:1;n<2;++n){tag["o"].list().emplace_back(b32(hash(n)));}std::ofstream f(profile/"tag.config",std::ios::binary|std::ios::trunc);lt::bencode(std::ostream_iterator<char>(f),value);f.close();require(bool(f),"Generated saved tag source write");};
 write(cap);auto lease=s::ProfileLease::acquire(profile);require(bool(lease),"Actual generated profile lease");auto mode=s::TagManagerStartupMode::capture(*lease);require(bool(mode),"Actual startup capture");auto rates=e::ProfileTagRates::open(*lease,*mode);require(bool(rates),"Actual common profile owner");
 auto store=s::ManualTagStore::open(*lease);require(bool(store),"Actual tag source store");auto snapshot=(*store)->snapshot().value();const std::array ids{7};
 std::array<lt::torrent_handle,2> source,target;std::array<std::shared_ptr<std::atomic_bool>,2> drained;
 for(int n=0;n<2;++n){auto& session=down?leeches[n]:seeds[n];require(bool((*rates)->prepare(n==1?c::TorrentHash::parse(std::string(20,'z')).value():hash(n),session)),"Actual source preparation before paused admission");auto p=params[n];p.flags|=lt::torrent_flags::seed_mode|lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;source[n]=seeds[n].add_torrent(p);p.flags&=~lt::torrent_flags::seed_mode;p.save_path=(root.path/("leech"+std::to_string(n))).string();target[n]=leeches[n].add_torrent(p);drained[n]=shared_transport_fixture::installDrain(target[n]);auto attached=(*rates)->attach(hash(n),down?target[n]:source[n],session);if(n==0){require(bool(attached),"First actual source attachment");}else{require(!attached,"Missing group must refuse partial attachment");require(bool((*rates)->synchronize(session)),"Actual partial source attachment recovery");}}
 require(!(*rates)->attach(hash(0),down?target[0]:source[0]),"Ambiguous two-owner attachment accepted");
 require(!(*rates)->attach(hash(0),down?target[0]:source[0],down?leeches[1]:seeds[1]),"Foreign SDK handle accepted");
 auto proof=(*rates)->prepareSource(*lease,**store,snapshot,ids,down?leeches[0]:seeds[0]);require(bool(proof)&&proof->matches(*lease,**store,snapshot,7),"Complete source preparation");
 const auto total=[&]{return target[0].status().total_payload_download+target[1].status().total_payload_download;};
 for(int n=0;n<2;++n){source[n].resume();target[n].resume();const auto deadline=std::chrono::steady_clock::now()+3s;while(!seeds[n].listen_port()&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(10ms);}require(seeds[n].listen_port()!=0,"Generated listener");target[n].connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(seeds[n].listen_port())});}
 auto deadline=std::chrono::steady_clock::now()+14s;while((target[0].status().total_payload_download<16384||target[1].status().total_payload_download<16384)&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(25ms);}
 for(const auto& t:target){require(t.status().total_payload_download>=16384,"Both source-owned SDK sessions must progress");}
 require(total()<cap*30+98304,"Partial saved attachment recovered without installing its missing SDK group");
 const auto before=total();const auto began=std::chrono::steady_clock::now();std::this_thread::sleep_for(10s);const auto bytes=total()-before;const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();require(bytes>=16384&&bytes<cap*elapsed*1.2+32768,"Profile tag budget multiplied or stalled across SDK sessions");
 auto change=[&](int rate,bool first=true){write(rate,first);snapshot=(*store)->reload().value();require(bool((*rates)->synchronize(down?leeches[0]:seeds[0])),"Actual source effects across all SDK owners");};
 change(-1);require(!proof->matches(*lease,**store,snapshot,7),"Old source proof survived changed common budget");
 for(int n=0;n<2;++n){shared_transport_fixture::drainPriorPayload(seeds[n],source[n],drained[n]);}
 const auto stopped=total();for(int tick=0;tick<3;++tick){for(int n=0;n<2;++n){shared_transport_fixture::keepAlive(seeds[n],shared_transport_fixture::capturePeer(seeds[n],source[n]));}std::this_thread::sleep_for(1s);require(total()==stopped,"Saved disabled direction leaked new payload through a sibling SDK");}
 change(-1,false);const auto detached=target[0].status().total_payload_download,sibling=target[1].status().total_payload_download;
 deadline=std::chrono::steady_clock::now()+8s;while(target[0].status().total_payload_download<=detached+16384&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(25ms);}require(target[0].status().total_payload_download>detached+16384,"Source membership removal retained old class");require(target[1].status().total_payload_download==sibling,"Source membership removal lifted sibling prohibition");
 change(0);change(cap);auto fresh=(*rates)->prepareSource(*lease,**store,snapshot,ids,down?leeches[0]:seeds[0]);require(bool(fresh)&&fresh->matches(*lease,**store,snapshot,7),"Removed/re-added UID did not reuse its common budget safely");
 require(bool((*rates)->release(down?leeches[0]:seeds[0])),"Exact SDK owner release");require(!fresh->matches(*lease,**store,snapshot,7),"Released SDK cohort retained old proof");
 require(bool((*rates)->close()),"Common profile shutdown");require(!fresh->matches(*lease,**store,snapshot,7),"Closed profile retained source proof");
 std::cout<<"PASS actual profile saved tag budgets;direction="<<(down?"download":"upload")<<";two SDK sessions,aggregate="<<bytes<<"/"<<elapsed<<"s,complete source proof,disabled protocol,source detach,sibling isolation,UID retirement/re-add,exact handle refusal and cohort close\n";
 }catch(const std::exception& ex){std::cerr<<"PROFILE_SHARED_PAYLOAD "<<ex.what()<<'\n';return 1;}}
