// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "profile_prepared_tag_attachment.hpp"
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
 const bool down=argc>1&&std::string_view(argv[1])=="download";
 const bool empty=argc>2&&std::string_view(argv[2])=="empty";constexpr int cap=16384;
 shared_transport_fixture::Root root;const auto profile=root.path/"profile";std::filesystem::create_directories(profile);
 std::array<lt::session,2> seeds{lt::session(shared_transport_fixture::settings()),lt::session(shared_transport_fixture::settings())};
 std::array<lt::session,2> leeches{lt::session(shared_transport_fixture::settings()),lt::session(shared_transport_fixture::settings())};
 std::array<lt::add_torrent_params,2> params;
 for(int n=0;n<2;++n){params[n]=shared_transport_fixture::torrent(root.path/("seed"+std::to_string(n)),char('a'+n));}
 const auto hash=[&](int n){return c::TorrentHash::parse(std::string_view(params[n].ti->info_hashes().v1.data(),20)).value();};
 auto write=[&](int rate,bool first=true){lt::entry value(lt::entry::dictionary_type{});auto& tag=value["3"]["7"];tag["n"]="Generated tag";tag["c"][down?"rl.down":"rl.up"]=rate;tag["o"]=lt::entry::list_type{};for(int n=0;n<(first?2:1);++n){tag["o"].list().emplace_back(b32(hash(n)));}std::ofstream f(profile/"tag.config",std::ios::binary|std::ios::trunc);lt::bencode(std::ostream_iterator<char>(f),value);f.close();require(bool(f),"Generated saved tag source write");};
 write(empty?0:cap);auto lease=s::ProfileLease::acquire(profile);require(bool(lease),"Actual generated profile lease");auto mode=s::TagManagerStartupMode::capture(*lease);require(bool(mode),"Actual startup capture");auto rates=e::ProfileTagRates::open(*lease,*mode);require(bool(rates),"Actual common profile owner");
 auto store=s::ManualTagStore::open(*lease);require(bool(store),"Actual tag source store");auto snapshot=(*store)->snapshot().value();const std::array ids{7};
 std::array<lt::torrent_handle,2> source,target;std::array<std::shared_ptr<std::atomic_bool>,2> drained,requestDrained;
 // Admit/prepare the complete SDK cohort before issuing any pending bundle.
 for(int n=0;n<2;++n){require(bool((*rates)->prepare(hash(n),down?leeches[n]:seeds[n])),"Actual complete SDK cohort before pending preparation");}
 std::optional<s::PreparedTagRateSource> proof;
 if(!empty){auto ready=(*rates)->prepareSource(*lease,**store,snapshot,ids,down?leeches[0]:seeds[0]);require(bool(ready),"Actual initial complete source proof");proof=*ready;}
 for(int n=0;n<2;++n){auto& session=down?leeches[n]:seeds[n];auto p=params[n];p.flags|=lt::torrent_flags::seed_mode|lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;
  auto q=p;q.flags&=~lt::torrent_flags::seed_mode;q.save_path=(root.path/("leech"+std::to_string(n))).string();auto limited=down?q:p;
  auto prepared=(*rates)->prepareAttachment(hash(n),limited.ti->info_hashes(),session);require(bool(prepared),"Actual profile pending reservation");
  require(bool((*prepared)->install(limited)),"Actual paused bundle installation");auto handle=session.add_torrent(limited);
  require((*prepared)->boundTo(handle),"Complete actual source/cohort bundle attaches inside SDK construction");
  source[n]=down?seeds[n].add_torrent(p):handle;target[n]=down?handle:leeches[n].add_torrent(q);
  drained[n]=shared_transport_fixture::installDrain(target[n]);requestDrained[n]=shared_transport_fixture::installDrain(source[n]);
 }
 auto change=[&](int rate,bool first=true){write(rate,first);snapshot=(*store)->reload().value();require(bool((*rates)->synchronize(down?leeches[0]:seeds[0])),"Actual source effects harvest/cancel every pending target");};
 // No explicit attach and no controller call after the second constructor.
 // In the empty case,the first source change must attach the first real class
 // to both previously class-less pending handles before their first resume.
 if(empty){change(-1);}
 const auto total=[&]{return target[0].status().total_payload_download+target[1].status().total_payload_download;};
 for(int n=0;n<2;++n){source[n].resume();target[n].resume();const auto deadline=std::chrono::steady_clock::now()+3s;while(!seeds[n].listen_port()&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(10ms);}require(seeds[n].listen_port()!=0,"Generated listener");target[n].connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(seeds[n].listen_port())});}
 if(empty){std::this_thread::sleep_for(1500ms);require(total()==0,"First active source tag missed a constructor target with no prior classes");change(cap);auto ready=(*rates)->prepareSource(*lease,**store,snapshot,ids,down?leeches[0]:seeds[0]);require(bool(ready),"First active source proof");proof=*ready;}
 auto deadline=std::chrono::steady_clock::now()+14s;while((target[0].status().total_payload_download<16384||target[1].status().total_payload_download<16384)&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(25ms);}
 for(const auto& t:target){require(t.status().total_payload_download>=16384,"Both source-owned SDK sessions must progress");}
 require(total()<cap*30+98304,"Partial saved attachment recovered without installing its missing SDK group");
 const auto before=total();const auto began=std::chrono::steady_clock::now();std::this_thread::sleep_for(10s);const auto bytes=total()-before;const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();require(bytes>=16384&&bytes<cap*elapsed*1.2+32768,"Profile tag budget multiplied or stalled across SDK sessions");
 // Combine the first post-constructor source update with last-target removal:
 // failing to harvest that pending handle would leave its class Disabled.
 const auto detachedBefore=target[1].status().total_payload_download;
 change(-1,false);require(proof&&!proof->matches(*lease,**store,snapshot,7),"Old source proof survived changed common budget");
 if(down){shared_transport_fixture::drainPriorPayload(leeches[0],target[0],requestDrained[0]);}
 shared_transport_fixture::drainPriorPayload(seeds[0],source[0],drained[0]);
 const auto stopped=target[0].status().total_payload_download;
 for(int tick=0;tick<3;++tick){shared_transport_fixture::keepAlive(seeds[0],shared_transport_fixture::capturePeer(seeds[0],source[0]));std::this_thread::sleep_for(1s);require(target[0].status().total_payload_download==stopped,"Removed sibling lifted the retained target prohibition");}
 deadline=std::chrono::steady_clock::now()+8s;while(target[1].status().total_payload_download<=detachedBefore+16384&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(25ms);}
 require(target[1].status().total_payload_download>detachedBefore+16384,"Source removal missed the last constructor target before census harvest");
 change(0);change(cap);auto fresh=(*rates)->prepareSource(*lease,**store,snapshot,ids,down?leeches[0]:seeds[0]);require(bool(fresh)&&fresh->matches(*lease,**store,snapshot,7),"Removed/re-added UID did not reuse its common budget safely");
 require(bool((*rates)->release(down?leeches[0]:seeds[0])),"Exact SDK owner release");require(!fresh->matches(*lease,**store,snapshot,7),"Released SDK cohort retained old proof");
 require(bool((*rates)->close()),"Common profile shutdown");require(!fresh->matches(*lease,**store,snapshot,7),"Closed profile retained source proof");
 std::cout<<"PASS_PROFILE_PREADD_PAYLOAD constructor source/census;direction="<<(down?"download":"upload")<<";initially-empty="<<empty<<";two SDK sessions,aggregate="<<bytes<<"/"<<elapsed<<"s,complete source proof,disabled protocol,source detach,sibling isolation,UID retirement/re-add,exact handle refusal and cohort close\n";
 }catch(const std::exception& ex){std::cerr<<"PROFILE_SHARED_PAYLOAD "<<ex.what()<<'\n';return 1;}}
