// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_network_rates.hpp"
#include "transport_fixture.hpp"
#include "transport_network.hpp"
#include <libtorrent/session.hpp>
#include <array>
#include <iostream>
namespace lt=libtorrent;namespace e=biglybtc::engine;
using namespace std::chrono_literals;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct ProtocolPeer final:lt::peer_plugin {
 std::weak_ptr<lt::peer_connection> peer;std::shared_ptr<std::atomic_int> received;int accounted=0;
 ProtocolPeer(decltype(peer) p,decltype(received) r):peer(std::move(p)),received(std::move(r)){}
 bool on_unknown_message(int length,int message,lt::span<char const> body)override{
  if(message!=99)return false;
  require(length==65537&&body.size()<=65536,"Actual protocol frame shape");
  const int seen=int(body.size())+1;
  if(auto actual=peer.lock())actual->received_bytes(0,seen-accounted);
  received->fetch_add(seen-accounted);accounted=seen;
  if(seen==length)accounted=0;
  return true;
 }
};
struct ProtocolTorrent final:lt::torrent_plugin {
 std::shared_ptr<std::atomic_int> received;
 explicit ProtocolTorrent(decltype(received) r):received(std::move(r)){}
 std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& p)override{return std::make_shared<ProtocolPeer>(p.native_handle(),received);}
};
int main(int argc,char** argv){try{
 const bool down=argc>1&&std::string_view(argv[1])=="download";
 const bool include=true;
 shared_transport_fixture::Root root;
 auto scope=e::SdkSharedQuotaScope::create();require(bool(scope),"Actual quota scope");
 auto global=(*scope)->acquire(e::SdkNetworkRates::GlobalKey,{0,0});auto lan=(*scope)->acquire(e::SdkNetworkRates::LanKey,{0,0});require(bool(global)&&bool(lan),"Actual network quota cells");
 auto settings=shared_transport_fixture::settings();settings.set_bool(lt::settings_pack::close_redundant_connections,false);settings.set_bool(lt::settings_pack::rate_limit_ip_overhead,false);
 lt::session limited(settings),other(settings);
 auto network=e::SdkNetworkRates::create(limited,*global,*lan);require(bool(network),"Fresh common network SDK");
 lt::ip_filter filter;filter.add_rule(lt::make_address("0.0.0.0"),lt::make_address("255.255.255.255"),1);limited.set_peer_class_filter(filter);
 auto a=shared_transport_fixture::torrent(root.path/"a",'x');a.flags|=lt::torrent_flags::seed_mode;a.flags&=~lt::torrent_flags::auto_managed;auto b=a;b.save_path=(root.path/"b").string();std::filesystem::create_directories(b.save_path);std::filesystem::copy_file(root.path/"a/x.bin",root.path/"b/x.bin");
 auto first=limited.add_torrent(a),second=other.add_torrent(b);auto count=std::make_shared<std::atomic_int>(0);
 (down?first:second).add_extension([count](const auto&,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{return std::make_shared<ProtocolTorrent>(count);});
 first.resume();second.resume();
 const auto listen=std::chrono::steady_clock::now()+5s;while(!limited.listen_port()&&std::chrono::steady_clock::now()<listen)std::this_thread::sleep_for(10ms);
 require(limited.listen_port()!=0,"Actual test listener");second.connect_peer({lt::make_address("127.0.0.1"),static_cast<unsigned short>(limited.listen_port())});
 bool connected=false;const auto deadline=std::chrono::steady_clock::now()+10s;
 while(!connected&&std::chrono::steady_clock::now()<deadline){std::vector<lt::peer_info> peers;first.get_peer_info(peers);connected=!peers.empty()&&!(peers[0].flags&lt::peer_info::handshake);if(!connected)std::this_thread::sleep_for(25ms);}
 require(connected,"Actual established protocol connection");
 require(bool((*global)->setLimits({down?0:1024,down?1024:0})),"Exact positive network byte budget");
 require(bool((*network)->configureProtocol(down?true:include,down?include:true)),"Actual optional protocol policy acknowledgement");
 auto sender=down?lt::session_handle(other):lt::session_handle(limited);auto target=down?second:first;
 const auto sendFrames=[&]{shared_transport_fixture::network(sender,[target]{auto actual=target.native_handle();require(actual&&actual->begin()!=actual->end(),"Actual protocol sender");auto* peer=*actual->begin();std::string packet(65541,'P');packet[0]=0;packet[1]=1;packet[2]=0;packet[3]=1;packet[4]=99;for(int n=0;n<16;++n)lt::peer_connection_handle(peer->self()).send_buffer(packet.data(),int(packet.size()));});};sendFrames();
 const auto end=std::chrono::steady_clock::now()+(include?3s:8s);while(std::chrono::steady_clock::now()<end&&count->load()<16*65537)std::this_thread::sleep_for(20ms);
 const int received=count->load();std::cout<<"PROTOCOL_STREAM direction="<<(down?"download":"upload")<<" included="<<include<<" bytes="<<received<<'\n';
 require(include?(received<32768):(received==16*65537),"Protocol quota inclusion/exemption did not match actual wire transfer");
 require(bool((*network)->configureProtocol(down?true:false,down?false:true)),"Live protocol exemption acknowledgement");
 auto until=std::chrono::steady_clock::now()+8s;while(count->load()<16*65537&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(20ms);
 require(count->load()==16*65537,"Live exemption did not release existing queued protocol bytes");
 require(bool((*network)->configureProtocol(true,true)),"Live protocol inclusion acknowledgement");sendFrames();
 std::this_thread::sleep_for(3s);const auto secondWindow=count->load()-16*65537;
 require(secondWindow>=0&&secondWindow<32768,"Restored protocol inclusion reused unlimited old quota");
 require(bool((*network)->configureProtocol(down?true:false,down?false:true)),"Second protocol exemption acknowledgement");
 until=std::chrono::steady_clock::now()+8s;while(count->load()<32*65537&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(20ms);
 require(count->load()==32*65537,"Final protocol drain incomplete");
 std::cout<<"PASS_LIVE_PROTOCOL_POLICY restored_window="<<secondWindow<<";same two sessions and same established connection\n";
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
