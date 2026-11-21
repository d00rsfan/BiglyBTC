// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_shared_tag_budget.hpp"
#include "sdk_tag_rate_group.hpp"
#include "worker_fixture.hpp"
#include "transport_fixture.hpp"
#include "transport_network.hpp"
#include <array>
#include <iostream>
namespace lt=libtorrent;namespace e=biglybtc::engine;namespace c=biglybtc::core;
using namespace std::chrono_literals;
namespace {void require(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}}
int main(int argc,char** argv){try{
 const bool down=argc>1&&std::string_view(argv[1])=="download";constexpr int cap=16384;
 const c::SavedTagRateLimits limits{down?0:cap,down?cap:0},disabled{down?0:-1,down?-1:0};
 shared_transport_fixture::Root root;auto scope=e::SdkSharedQuotaScope::create();require(bool(scope),"Quota origin");auto budget=(*scope)->acquire(7,limits);require(bool(budget),"Origin common tag budget");
 lt::session local(shared_transport_fixture::settings());worker_fixture::Remote remote;
 std::array<lt::session,2> other{lt::session(shared_transport_fixture::settings()),lt::session(shared_transport_fixture::settings())};
 auto one=e::SdkTagRateGroup::create(local,7,limits,*budget);require(bool(one),"Origin-worker SDK group");
 const auto remoteSession=remote.call([&](worker_fixture::State& state){state.sdk=std::make_unique<lt::session>(shared_transport_fixture::settings());auto created=e::SdkTagRateGroup::create(*state.sdk,7,limits,*budget);require(bool(created),"Different-worker SDK group");state.group=std::move(*created);return lt::session_handle(*state.sdk);});
 const std::array<lt::session_handle,2> limited{local,remoteSession};std::array<lt::torrent_handle,2> source,target;
 std::array<std::shared_ptr<std::atomic_bool>,2> drains;
 for(int n=0;n<2;++n){auto p=shared_transport_fixture::torrent(root.path/("seed"+std::to_string(n)),char('a'+n));p.flags|=lt::torrent_flags::seed_mode|lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;
  auto q=p;q.flags&=~lt::torrent_flags::seed_mode;q.save_path=(root.path/("leech"+std::to_string(n))).string();
  auto limitedParams=down?q:p;auto handle=n?remote.call([limitedParams](worker_fixture::State& state){return state.sdk->add_torrent(limitedParams);}):local.add_torrent(limitedParams);
  source[n]=down?other[n].add_torrent(p):handle;target[n]=down?handle:other[n].add_torrent(q);drains[n]=shared_transport_fixture::installDrain(target[n]);
  auto attached=n?remote.call([handle](worker_fixture::State& state){return state.group->attach(handle);}):(*one)->attach(handle);require(bool(attached),"SDK attachment on each actual control worker");
 }
 const auto total=[&]{return target[0].status().total_payload_download+target[1].status().total_payload_download;};
 for(int n=0;n<2;++n){source[n].resume();target[n].resume();const auto sender=down?lt::session_handle(other[n]):limited[n];auto end=std::chrono::steady_clock::now()+3s;while(!sender.listen_port()&&std::chrono::steady_clock::now()<end){std::this_thread::sleep_for(10ms);}require(sender.listen_port()!=0,"Generated sender listener");target[n].connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(sender.listen_port())});}
 auto deadline=std::chrono::steady_clock::now()+14s;while((target[0].status().total_payload_download<16384||target[1].status().total_payload_download<16384)&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(25ms);}
 for(auto t:target){require(t.status().total_payload_download>=16384,"Both SDK-control workers must progress");}
 const auto before=total();const auto began=std::chrono::steady_clock::now();std::this_thread::sleep_for(10s);const auto bytes=total()-before;const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();require(bytes>=16384&&bytes<cap*seconds*1.2+32768,"Different SDK-control workers multiplied or stalled the common quota");
 const auto a=(*one)->readiness(),b=remote.call([](worker_fixture::State& state){return state.group->readiness();});require(a.currentFor(7,limits)&&b.currentFor(7,limits),"Cross-worker initial witnesses");
 require(bool((*one)->setLimits(disabled)),"Origin-only common prohibition");require(!a.currentFor(7,limits)&&!b.currentFor(7,limits),"Cross-worker passive revocation");require(!remote.call([](worker_fixture::State& state){return state.group->readiness();}).currentFor(7,limits),"Consumer minted readiness before acknowledging origin policy");
 for(int n=0;n<2;++n){shared_transport_fixture::drainPriorPayload(down?lt::session_handle(other[n]):limited[n],source[n],drains[n]);}
 const auto stopped=total();for(int tick=0;tick<3;++tick){for(int n=0;n<2;++n){const auto sender=down?lt::session_handle(other[n]):limited[n];shared_transport_fixture::keepAlive(sender,shared_transport_fixture::capturePeer(sender,source[n]));}std::this_thread::sleep_for(1s);require(total()==stopped,"Consumer ignored origin prohibition before local acknowledgement");}
 require(bool(remote.call([&](worker_fixture::State& state){return state.group->setLimits(disabled);})),"Consumer acknowledged published limits");auto acknowledged=remote.call([](worker_fixture::State& state){return state.group->readiness();});require(acknowledged.currentFor(7,disabled),"Exact acknowledged consumer witness");
 const auto handle=down?target[1]:source[1];require(bool(remote.call([handle](worker_fixture::State& state){return state.group->detach(handle);})),"Remote SDK live detach");const auto detached=target[1].status().total_payload_download,sibling=target[0].status().total_payload_download;
 deadline=std::chrono::steady_clock::now()+8s;while(target[1].status().total_payload_download<=detached+16384&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(25ms);}require(target[1].status().total_payload_download>detached+16384,"Remote detach retained the queued tag class");require(target[0].status().total_payload_download==sibling,"Remote detach lifted the origin sibling's prohibition");
 require(bool((*one)->setLimits(limits)),"Origin restores common limits");require(bool(remote.call([&](worker_fixture::State& state){return state.group->setLimits(limits);})),"Consumer refreshes restored policy");require(bool(remote.call([handle](worker_fixture::State& state){return state.group->attach(handle);})),"Remote live reattachment");
 auto final=remote.call([](worker_fixture::State& state){return state.group->readiness();});require(bool((*scope)->close())&&!final.currentFor(7,limits),"Origin close revokes consumer without a worker callback");require(bool(remote.call([](worker_fixture::State& state){return state.group->close();}))&&bool((*one)->close()),"Each SDK class retires on its own control worker");
 std::cout<<"PASS_WORKER_PAYLOAD direction="<<(down?"download":"upload")<<" bytes="<<bytes<<" seconds="<<seconds<<";one image,two SDK-control workers,origin-only changes,pre-ack prohibition,protocol,detach/sibling/reattach,passive close and joins\n";
 }catch(const std::exception& error){std::cerr<<"WORKER_PAYLOAD "<<error.what()<<'\n';return 1;}}
