#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/alert_types.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "exports.hpp"

namespace lt=libtorrent;
using namespace std::chrono_literals;
using Clock=std::chrono::steady_clock;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void await(F f,std::chrono::seconds budget,const char* message){
  const auto end=Clock::now()+budget;
  while(!f()){require(Clock::now()<end,message);std::this_thread::sleep_for(10ms);}
}
struct OwnedSession {
  std::unique_ptr<lt::session> value;
  using Guard=boost::asio::executor_work_guard<lt::io_context::executor_type>;
  std::optional<Guard> work;
  explicit OwnedSession(lt::settings_pack settings):value(std::make_unique<lt::session>(lt::session_params{settings})){
    work.emplace(boost::asio::make_work_guard(value->native_handle()->get_context()));
  }
  void close(){if(value){work.reset();auto joined=value->abort();value.reset();}}
  ~OwnedSession(){close();}
};
lt::settings_pack isolated(){
  lt::settings_pack p;
  for(int k:{lt::settings_pack::enable_incoming_tcp,lt::settings_pack::enable_outgoing_tcp,
      lt::settings_pack::enable_incoming_utp,lt::settings_pack::enable_outgoing_utp,
      lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,
      lt::settings_pack::enable_natpmp,lt::settings_pack::allow_i2p_mixed})p.set_bool(k,false);
  p.set_str(lt::settings_pack::listen_interfaces,"");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
  p.set_str(lt::settings_pack::i2p_hostname,"");p.set_int(lt::settings_pack::i2p_port,0);
  p.set_int(lt::settings_pack::proxy_type,lt::settings_pack::none);
  return p;
}
lt::settings_pack publicSettings(){
  auto p=isolated();p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");
  p.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");
  p.set_bool(lt::settings_pack::enable_incoming_tcp,true);p.set_bool(lt::settings_pack::enable_outgoing_tcp,true);
  p.set_bool(lt::settings_pack::seeding_outgoing_connections,true);
  p.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);
  p.set_int(lt::settings_pack::download_rate_limit,65536);
  p.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::error|lt::alert_category::peer|lt::alert_category::connect|lt::alert_category::status|lt::alert_category::tracker|lt::alert_category::ip_block));
  return p;
}
struct Tracker {
  int socket=-1;std::uint16_t port=0;std::atomic<std::uint16_t> seedPort{0};
  std::atomic<unsigned> requests{0},errors{0};std::atomic<bool> stop{false};std::thread worker;
  Tracker(){
    socket=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(socket>=0,"tracker socket");
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    require(::bind(socket,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0,"tracker bind");
    socklen_t n=sizeof(a);require(::getsockname(socket,reinterpret_cast<sockaddr*>(&a),&n)==0,"tracker address");
    port=ntohs(a.sin_port);require(port&&::listen(socket,8)==0,"tracker listen");
    worker=std::thread([this]{run();});
  }
  void run()noexcept{try{
    while(!stop.load()){
      pollfd p{socket,POLLIN,0};if(::poll(&p,1,50)<=0)continue;
      const int client=::accept4(socket,nullptr,nullptr,SOCK_CLOEXEC);if(client<0)continue;
      struct Close{int fd;~Close(){::close(fd);}} close{client};
      timeval timeout{1,0};::setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
      ::setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
      std::string request;char bytes[1024];
      while(request.find("\r\n\r\n")==std::string::npos&&request.size()<4096){
        const auto got=::recv(client,bytes,sizeof(bytes),0);if(got<=0)break;request.append(bytes,static_cast<std::size_t>(got));
      }
      if(!request.starts_with("GET /announce?")||request.size()>4096){++errors;continue;}
      ++requests;std::string peers;const auto seed=seedPort.load();
      const auto portAt=request.find("&port=");require(portAt!=std::string::npos,"actual tracker announce port");
      const auto announced=std::stoul(request.substr(portAt+6));require(announced>0&&announced<=65535,"announced port range");
      if(seed&&announced!=seed){peers={char(127),0,0,1,static_cast<char>(seed>>8),static_cast<char>(seed&255)};}
      const auto body="d8:completei1e10:incompletei1e8:intervali1e5:peers"+std::to_string(peers.size())+":"+peers+"e";
      const auto response="HTTP/1.0 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
      std::size_t sent=0;while(sent<response.size()){
        const auto amount=::send(client,response.data()+sent,response.size()-sent,MSG_NOSIGNAL);
        if(amount<=0){++errors;break;}sent+=static_cast<std::size_t>(amount);
      }
    }
  }catch(...){++errors;}}
  ~Tracker(){stop=true;if(worker.joinable())worker.join();if(socket>=0)::close(socket);}
};
lt::BiglybtcPrivateI2pOwnerPolicyV1 policy(lt::info_hash_t const& h,std::uint64_t generation){
  lt::BiglybtcPrivateI2pOwnerPolicyV1 p;p.generation=generation;p.admission=101;
  p.capability.fill(0xf1);p.public_destination_hash.fill(0x41);p.hashes=1;
  std::copy_n(reinterpret_cast<unsigned char const*>(h.v1.data()),20,p.torrent_v1.begin());
  p.bridge_port=49222;p.peer_port=p.local_peer_port=6881;return p;
}
lt::BiglybtcPrivateI2pTorrentV1 tuple(lt::BiglybtcPrivateI2pOwnerPolicyV1 const& p){
  lt::BiglybtcPrivateI2pTorrentV1 t;t.generation=p.generation;t.admission=p.admission;
  t.hashes=p.hashes;t.torrent_v1=p.torrent_v1;return t;
}
lt::BiglybtcPrivateI2pOwnerStatusV1 status(lt::session& s){
  lt::BiglybtcPrivateI2pOwnerStatusV1 v;
  require(lt::biglybtc_read_private_i2p_owner_v1(&s,&v)==0,"actual owner status");return v;
}
void configure(OwnedSession& s,lt::BiglybtcPrivateI2pOwnerPolicyV1 const& p){
  require(lt::biglybtc_configure_private_i2p_owner_v1(s.value.get(),&p)==0,"private configure");
  require(status(*s.value).phase==1,"private configured phase");
}
lt::torrent_handle add(lt::session& s,std::shared_ptr<lt::torrent_info> ti,std::filesystem::path const& path){
  lt::add_torrent_params p;p.ti=std::move(ti);p.save_path=path.string();
  p.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);
  lt::error_code ec;auto h=s.add_torrent(std::move(p),ec);require(!ec&&h.is_valid(),"Public add");return h;
}
std::shared_ptr<lt::torrent_info> metainfo(std::string const& payload,std::uint16_t tracker){
  lt::entry root;root["announce"]="http://127.0.0.1:"+std::to_string(tracker)+"/announce";
  auto& info=root["info"];info["length"]=static_cast<std::int64_t>(payload.size());info["name"]="payload.bin";info["piece length"]=16384;
  std::string pieces;for(std::size_t at=0;at<payload.size();at+=16384){
    auto h=lt::hasher(payload.data()+at,static_cast<int>(std::min<std::size_t>(16384,payload.size()-at))).final();pieces.append(h.data(),20);
  }
  info["pieces"]=pieces;std::vector<char> bytes;lt::bencode(std::back_inserter(bytes),root);
  lt::error_code ec;auto ti=std::make_shared<lt::torrent_info>(lt::span<char const>(bytes.data(),bytes.size()),ec,lt::from_span);
  require(!ec,"generated metainfo");return ti;
}
std::vector<std::string> passed;
void pass(const char* name){passed.emplace_back(name);std::cout<<"PASS "<<name<<'\n';}
void xml(std::filesystem::path const& path,std::string const& failure){
  std::ofstream out(path);out<<"<testsuite name=\"sdk-public-private-roles\" tests=\""<<passed.size()+(failure.empty()?0:1)
    <<"\" failures=\""<<(failure.empty()?0:1)<<"\" errors=\"0\" skipped=\"0\">\n";
  for(auto const& name:passed)out<<"<testcase name=\""<<name<<"\" status=\"run\"/>\n";
  if(!failure.empty())out<<"<testcase name=\"role-control-failure\" status=\"run\"><failure>"<<failure<<"</failure></testcase>\n";
  out<<"</testsuite>\n";require(bool(out),"XML write");
}
int main(int argc,char** argv){
  if(argc!=2){return 2;}
  const auto root=std::filesystem::path(argv[1]);
  try{
    require(std::filesystem::is_directory(root),"owned fixture directory");
    void* library=::dlopen("libtorrent-rasterbar.so.2.0",RTLD_NOW|RTLD_NOLOAD);require(library,"mapped provider");
    void* base=nullptr;std::string origin;
    for(const auto* name:requiredExports){void* symbol=::dlsym(library,name);Dl_info d{};
      require(symbol&&symbol==::dlsym(RTLD_DEFAULT,name)&&::dladdr(symbol,&d),"same-image export");
      if(!base){base=d.dli_fbase;origin=d.dli_fname;}require(base==d.dli_fbase&&origin==d.dli_fname,"single provider image");
    }
    require(lt::biglybtc_private_i2p_owner_abi_v1()==lt::BiglybtcPrivateI2pOwnerAbiV1&&lt::biglybtc_private_i2p_torrents_abi_v1()==lt::BiglybtcPrivateI2pTorrentsAbiV1,"both private ABI markers");
    std::cout<<"PROVIDER "<<std::filesystem::canonical(origin).string()<<" EXPORTS "<<std::size(requiredExports)<<'\n';
    ::dlclose(library);pass("same-image-34-exports-and-markers");
    Tracker tracker;std::string payload(262144,'\0');for(std::size_t i=0;i<payload.size();++i)payload[i]=static_cast<char>((i*31+7)&255);
    std::filesystem::create_directory(root/"seed");std::filesystem::create_directory(root/"leech");
    {std::ofstream f(root/"seed/payload.bin",std::ios::binary);f.write(payload.data(),static_cast<std::streamsize>(payload.size()));require(bool(f),"seed bytes");}
    auto ti=metainfo(payload,tracker.port);OwnedSession seed(publicSettings()),leech(publicSettings());
    lt::ip_filter classes;classes.add_rule(boost::asio::ip::address_v4::loopback(),boost::asio::ip::address_v4::loopback(),1u<<static_cast<unsigned>(lt::session::global_peer_class_id));
    seed.value->set_peer_class_filter(classes);leech.value->set_peer_class_filter(classes);
    await([&]{return seed.value->listen_port()!=0&&leech.value->listen_port()!=0;},3s,"Public listen");
    tracker.seedPort=seed.value->listen_port();
    require(status(*seed.value).phase==0&&status(*leech.value).phase==0,"Public sessions unbound");
    require(seed.value->get_settings().get_bool(lt::settings_pack::enable_outgoing_tcp)&&seed.value->get_settings().get_str(lt::settings_pack::i2p_hostname).empty(),"Public role settings");
    auto p=policy(ti->info_hashes(),701);{
      OwnedSession deniedPublic(publicSettings());require(lt::biglybtc_configure_private_i2p_owner_v1(deniedPublic.value.get(),&p)==1,"Public cannot become private");
      require(status(*deniedPublic.value).phase==0,"denied configure has no owner side effect");
    }
    OwnedSession privateA(isolated());configure(privateA,p);
    auto t=tuple(p);auto stale=t;stale.generation++;
    require(lt::biglybtc_admit_private_i2p_torrent_v1(privateA.value.get(),&stale)==5,"stale generation");
    require(lt::biglybtc_admit_private_i2p_torrent_v1(seed.value.get(),&t)==5,"Public no private grant");
    auto zero=t;zero.admission=0;require(lt::biglybtc_admit_private_i2p_torrent_v1(privateA.value.get(),&zero)==1,"zero admission");
    lt::add_torrent_params foreign;foreign.ti=ti;foreign.save_path=(root/"leech").string();lt::error_code ec;
    auto denied=privateA.value->add_torrent(foreign,ec);require(ec&&!denied.is_valid(),"private refuses Public tracker despite same hash");
    pass("distinct-roles-public-and-private-admission-denials");
    auto a=add(*seed.value,ti,root/"seed");await([&]{return a.status().is_seeding;},5s,"genuine seed check");
    auto b=add(*leech.value,ti,root/"leech");
    try{await([&]{return b.status().total_payload_download>=16384;},60s,"real Public tracker-peer payload");}
    catch(...){const auto actual=b.status();std::cerr<<"PUBLIC_STATE requests="<<tracker.requests.load()<<" tracker_fixture_errors="<<tracker.errors.load()<<" paused="<<bool(actual.flags&lt::torrent_flags::paused)<<" announcing="<<actual.announcing_to_trackers<<" peers="<<actual.num_peers<<" error="<<actual.errc.value()<<" payload="<<actual.total_payload_download<<'\n';
      for(auto* ses:{seed.value.get(),leech.value.get()}){std::vector<lt::alert*> alerts;ses->pop_alerts(&alerts);for(auto* alert:alerts){std::cerr<<"PUBLIC_ALERT "<<alert->message()<<'\n';}}throw;
    }
    const auto before=b.status().total_payload_download;require(before<static_cast<std::int64_t>(payload.size()),"withdraw during active transfer");
    require(status(*privateA.value).phase==1,"private alive alongside transfer");
    require(lt::biglybtc_withdraw_private_i2p_owner_v1(privateA.value.get(),p.generation)==0,"private withdraw");
    require(status(*privateA.value).phase==2,"private withdrawn phase");privateA.close();
    await([&]{return b.status().is_seeding;},15s,"Public continues after private withdraw");
    require(b.status().total_payload_download>before&&tracker.requests.load()>=2&&tracker.errors.load()==0,"actual tracker and continuing payload");
    pass("loopback-tracker-peer-transfer-survives-private-withdraw");
    OwnedSession escaping(isolated());auto ep=policy(ti->info_hashes(),702);configure(escaping,ep);
    auto mixed=isolated();mixed.set_bool(lt::settings_pack::allow_i2p_mixed,true);escaping.value->apply_settings(mixed);
    require(status(*escaping.value).phase==2,"mixed escape withdraws only private");
    require(b.is_valid()&&b.status().is_seeding&&status(*leech.value).phase==0,"Public survives private settings failure");escaping.close();
    pass("private-mixed-settings-failure-is-session-local");
    OwnedSession privateB(isolated());auto bp=policy(ti->info_hashes(),703);configure(privateB,bp);
    seed.close();leech.close();require(status(*privateB.value).phase==1,"Public close preserves private owner");
    require(lt::biglybtc_admit_private_i2p_torrent_v1(privateB.value.get(),&t)==5,"old owner tuple cannot cross sessions");
    auto current=tuple(bp);require(lt::biglybtc_admit_private_i2p_torrent_v1(privateB.value.get(),&current)==0,"current private tuple survives Public join");
    privateB.close();pass("public-join-preserves-private-registration-and-independent-join");
    std::ifstream file(root/"leech/payload.bin",std::ios::binary);const std::string actual{std::istreambuf_iterator<char>(file),{}};
    require(actual==payload,"exact disk payload after both Public joins");pass("joined-public-disk-bytes-exact");
    xml(root/"tests.xml",{});return 0;
  }catch(std::exception const& e){std::cerr<<"FAIL "<<e.what()<<'\n';xml(root/"tests.xml",e.what());return 1;}
}
