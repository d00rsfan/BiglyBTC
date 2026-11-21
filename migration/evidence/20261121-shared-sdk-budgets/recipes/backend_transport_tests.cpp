// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_shared_tag_budget.hpp"
#include "sdk_tag_rate_group.hpp"
#include "transport_fixture.hpp"
#include "transport_network.hpp"
#include <libtorrent/session.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
namespace lt=libtorrent;namespace e=biglybtc::engine;namespace c=biglybtc::core;
using namespace std::chrono_literals;
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){try{
 const bool down=argc>1&&std::string_view(argv[1])=="download";constexpr int cap=16384;
 const bool long_disabled=argc>2&&std::string_view(argv[2])=="long";
 const c::SavedTagRateLimits limits{down?0:cap,down?cap:0},disabled{down?0:-1,down?-1:0};
 shared_transport_fixture::Root root;auto scope=e::SdkSharedQuotaScope::create();require(bool(scope),"Native shared scope creation");
 auto common=(*scope)->acquire(7,limits);require(bool(common),"Native common tag budget");
 std::array<lt::session,2> seeds{lt::session(shared_transport_fixture::settings()),lt::session(shared_transport_fixture::settings())};
 std::array<lt::session,2> leeches{lt::session(shared_transport_fixture::settings()),lt::session(shared_transport_fixture::settings())};
 std::array<lt::torrent_handle,2> source,target;std::array<std::unique_ptr<e::SdkTagRateGroup>,2> groups;
 for(int n=0;n<2;++n){auto p=shared_transport_fixture::torrent(root.path/("seed"+std::to_string(n)),char('a'+n));p.flags|=lt::torrent_flags::seed_mode;p.flags|=lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;source[n]=seeds[n].add_torrent(p);
  p.flags&=~lt::torrent_flags::seed_mode;p.save_path=(root.path/("leech"+std::to_string(n))).string();target[n]=leeches[n].add_torrent(p);
  auto group=e::SdkTagRateGroup::create(down?leeches[n]:seeds[n],7,limits,*common);require(bool(group),"Actual Native SDK group creation");groups[n]=std::move(*group);require(bool(groups[n]->attach(down?target[n]:source[n])),"Actual Native paused attachment");
 }
 const auto total=[&]{return target[0].status().total_payload_download+target[1].status().total_payload_download;};
 for(int n=0;n<2;++n){source[n].resume();target[n].resume();const auto deadline=std::chrono::steady_clock::now()+3s;while(!seeds[n].listen_port()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);require(seeds[n].listen_port()!=0,"Generated listener");target[n].connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(seeds[n].listen_port())});}
 auto deadline=std::chrono::steady_clock::now()+14s;while((target[0].status().total_payload_download<16384||target[1].status().total_payload_download<16384)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(25ms);
 for(const auto& t:target)require(t.status().total_payload_download>=16384,"Both actual Native owners must progress");
 const auto before=total();const auto began=std::chrono::steady_clock::now();std::this_thread::sleep_for(10s);const auto bytes=total()-before;const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
 require(bytes>=16384&&bytes<cap*elapsed*1.2+32768,"Actual Native backend multiplied or stalled the shared budget");
 const auto one=groups[0]->readiness(),two=groups[1]->readiness();require(one.currentFor(7,limits)&&two.currentFor(7,limits),"Actual transferring owner readiness");require(bool(groups[0]->setLimits(disabled)),"Actual owner disabled direction");require(!one.currentFor(7,limits)&&!two.currentFor(7,limits),"Other actual SDK owner kept stale source witness");require(!groups[1]->readiness().currentFor(7,limits),"Other owner minted false old-limit readiness");
 std::this_thread::sleep_for(3s);const auto stopped=total();
 if(long_disabled){std::array<std::weak_ptr<lt::peer_connection>,2> original;for(int n=0;n<2;++n)original[n]=shared_transport_fixture::capturePeer(seeds[n],source[n]);const auto protocol_before=target[0].status().total_download+target[1].status().total_download-stopped;
  for(int tick=0;tick<12;++tick){for(int n=0;n<2;++n)shared_transport_fixture::keepAlive(seeds[n],original[n]);std::this_thread::sleep_for(1s);require(total()==stopped,"Long Native disabled interval leaked payload");}
  const auto protocol_after=target[0].status().total_download+target[1].status().total_download-total();require(protocol_after>protocol_before+16,"Native disabled tag blocked actual protocol exchange");
 }else std::this_thread::sleep_for(2s);
 require(total()==stopped,"Other owner ignored shared disabled direction");
 require(bool(groups[0]->detach(down?target[0]:source[0])),"Live Native detach");const auto sibling=target[1].status().total_payload_download,detached=target[0].status().total_payload_download;
 deadline=std::chrono::steady_clock::now()+8s;while(target[0].status().total_payload_download<=detached+16384&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(25ms);
 require(target[0].status().total_payload_download>detached+16384,"Actual Native detach retained old queued disabled class");require(target[1].status().total_payload_download==sibling,"Actual Native detach lifted sibling prohibition");
 require(bool(groups[1]->setLimits(limits))&&bool(groups[0]->setLimits(limits)),"Actual Native owners restored limits");require(groups[0]->readiness().currentFor(7,limits)&&groups[1]->readiness().currentFor(7,limits),"Restored owner witnesses");
 require(bool(groups[0]->attach(down?target[0]:source[0])),"Live Native reattach");const auto latest=groups[1]->readiness();require(bool((*scope)->close()),"Native common root close");require(!latest.currentFor(7,limits)&&!groups[0]->current()&&!groups[1]->current(),"Native root close left transfer/source readiness live");
 // Explicit close joins actual SDK class retirement. SDK sessions then join
 // their network threads before generated payload directories disappear.
 require(bool(groups[0]->close())&&bool(groups[1]->close()),"Native class retirement");
 std::cout<<"PASS actual Native shared backend;direction="<<(down?"download":"upload")<<" bytes="<<bytes<<" seconds="<<elapsed<<";two SDK sessions,aggregate,disabled,exact passive epochs,live detach/reattach,sibling isolation,root close and retirement\n";
 }catch(const std::exception& ex){std::cerr<<"SHARED_BACKEND "<<ex.what()<<'\n';return 1;}}
