#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/biglybtc_mainline_port.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>
#include <boost/asio.hpp>
#include <array>
#include <atomic>
#include <deque>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace lt=libtorrent;
namespace asio=boost::asio;
using Tcp=asio::ip::tcp;
using Udp=asio::ip::udp;
using namespace std::chrono_literals;
void need(bool value,char const* text) { if(!value) throw std::runtime_error(text); }
constexpr std::uint64_t Admission=4242;
struct Observer final:lt::BiglybtcMainlinePortObserverV1 {
  std::atomic<std::uint16_t> port{0};
  mutable std::atomic<unsigned> zeroReads{0};
  std::array<lt::BiglybtcMainlinePortEventV1,16> ring{};
  std::atomic<unsigned> published{0};
  std::atomic<bool> lost{false};
  std::uint16_t advertised_port()const noexcept override {
    auto const value=port.load(std::memory_order_acquire);
    if(value==0) zeroReads.fetch_add(1,std::memory_order_relaxed);
    return value;
  }
  void observe(lt::BiglybtcMainlinePortEventV1 const& value)noexcept override {
    auto const at=published.load(std::memory_order_relaxed);
    if(at==ring.size()) {lost.store(true,std::memory_order_release);port.store(0,std::memory_order_release);return;}
    ring[at]=value;published.store(at+1,std::memory_order_release);
  }
};
std::vector<char> encoded(lt::entry const& value) {std::vector<char> out;lt::bencode(std::back_inserter(out),value);return out;}
std::vector<char> portMessage(std::uint16_t port) {
  return {0,0,0,3,9,static_cast<char>(port>>8),static_cast<char>(port&255)};
}
struct Wire {
  asio::io_context& io;
  Tcp::socket socket;
  std::array<char,68> handshake{};
  std::array<char,4> length{};
  std::vector<char> body;
  std::deque<std::shared_ptr<std::vector<char>>> outgoing;
  std::vector<std::uint16_t> advertisements;
  lt::sha1_hash hash;
  std::string failure;
  bool bitfield=false,closed=false,expectClose=false;
  std::uint16_t sourcePort=0;
  explicit Wire(asio::io_context& context,lt::sha1_hash value):io(context),socket(context),hash(value){}
  void bad(char const* why) {if(failure.empty())failure=why;boost::system::error_code ec;socket.close(ec);}
  void writeNext() {
    if(outgoing.empty() || !failure.empty())return;
    auto const owned=outgoing.front();
    asio::async_write(socket,asio::buffer(*owned),[this,owned](auto ec,std::size_t){
      if(ec){bad("Owned peer write failed");return;}
      outgoing.pop_front();writeNext();
    });
  }
  void send(std::vector<char> value) {
    bool const idle=outgoing.empty();
    outgoing.push_back(std::make_shared<std::vector<char>>(std::move(value)));
    if(idle)writeNext();
  }
  void connect(std::uint16_t port) {
    socket.async_connect({asio::ip::make_address("127.0.0.1"),port},[this](auto ec){
      if(ec){bad("Owned peer connect failed");return;}
      sourcePort=socket.local_endpoint().port();
      std::vector<char> message(68,0);message[0]=19;std::memcpy(message.data()+1,"BitTorrent protocol",19);
      message[27]=1;std::memcpy(message.data()+28,hash.data(),20);std::memcpy(message.data()+48,"-BCPORT-0123456789012",20);
      auto owned=std::make_shared<std::vector<char>>(std::move(message));
      asio::async_write(socket,asio::buffer(*owned),[this,owned](auto error,std::size_t){
        if(error){bad("Owned handshake write failed");return;}
        asio::async_read(socket,asio::buffer(handshake),[this](auto e,std::size_t){
          if(e || handshake[0]!=19 || std::memcmp(handshake.data()+1,"BitTorrent protocol",19)
              || std::memcmp(handshake.data()+28,hash.data(),20)){bad("Actual handshake/hash mismatch");return;}
          readLength();
        });
      });
    });
  }
  void readLength() {
    asio::async_read(socket,asio::buffer(length),[this](auto ec,std::size_t){
      if(ec){closed=true;if(!expectClose)bad("Unexpected owned peer EOF");return;}
      std::uint32_t count=0;for(auto c:length)count=(count<<8)|static_cast<unsigned char>(c);
      if(count>256*1024){bad("Excessive actual peer frame");return;}
      if(count==0){readLength();return;}body.resize(count);
      asio::async_read(socket,asio::buffer(body),[this](auto error,std::size_t){
        if(error){bad("Incomplete actual peer frame");return;}
        auto const id=static_cast<unsigned char>(body[0]);
        if(id==5)bitfield=true;
        if(id==9){
          if(body.size()!=3){bad("Invalid outgoing PORT length");return;}
          advertisements.push_back(static_cast<std::uint16_t>((static_cast<unsigned char>(body[1])<<8)|static_cast<unsigned char>(body[2])));
        }
        readLength();
      });
    });
  }
};
void sameImage() {
  Dl_info expected{},actual{};
  auto version=reinterpret_cast<void*>(::dlsym(RTLD_DEFAULT,"_ZN10libtorrent7versionEv"));
  need(version && ::dladdr(version,&expected),"Missing actual version image");
  for(auto const* name:{"biglybtc_mainline_port_abi_v1","biglybtc_attach_mainline_port_observer_v1","biglybtc_refresh_mainline_port_v1"}) {
    auto* symbol=::dlsym(RTLD_DEFAULT,name);
    need(symbol && ::dladdr(symbol,&actual) && actual.dli_fbase==expected.dli_fbase,"PORT export resolves to another image");
  }
  need(std::filesystem::canonical(expected.dli_fname)==std::filesystem::canonical(MAINLINE_EXPECTED_PROVIDER),"Wrong installed provider loaded");
  need(lt::biglybtc_mainline_port_abi_v1()==1,"Wrong PORT ABI marker");
}
void run(std::filesystem::path const& root,bool privateTorrent,bool attach) {
  std::filesystem::create_directories(root);
  std::string data(16384,'q');std::ofstream file(root/"sample.bin",std::ios::binary);file.write(data.data(),data.size());file.close();need(bool(file),"Owned payload write failed");
  auto const piece=lt::hasher(lt::span<char const>(data.data(),data.size())).final();
  lt::entry metadata;auto& info=metadata["info"];info["name"]="sample.bin";info["length"]=std::int64_t(data.size());info["piece length"]=16384;
  info["pieces"]=std::string(piece.data(),20);if(privateTorrent)info["private"]=1;
  auto raw=encoded(metadata);lt::error_code ec;auto ti=std::make_shared<lt::torrent_info>(lt::span<char const>(raw.data(),raw.size()),ec,lt::from_span);need(!ec,"Owned metainfo failed");
  auto const hash=ti->info_hashes().v1;
  asio::io_context io;auto work=asio::make_work_guard(io);
  Udp::socket live(io,{asio::ip::make_address("127.0.0.1"),0}),replacement(io,{asio::ip::make_address("127.0.0.1"),0});
  Udp::socket remote(io,{asio::ip::make_address("127.0.0.1"),0}),remote2(io,{asio::ip::make_address("127.0.0.1"),0});
  auto observer=std::make_shared<Observer>();std::shared_ptr<lt::BiglybtcMainlinePortObserverV1> sink=observer;
  lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");settings.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
  for(auto key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_incoming_utp,lt::settings_pack::enable_outgoing_utp})settings.set_bool(key,false);
  settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::status|lt::alert_category::error));
  lt::session_params sp(std::move(settings));sp.extensions.clear();auto session=std::make_unique<lt::session>(std::move(sp));
  std::atomic<unsigned> attached{99},refreshed{99};
  lt::add_torrent_params params;params.ti=ti;params.save_path=root.string();
  params.flags &= ~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);params.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
  if(attach)params.extensions.push_back([&](lt::torrent_handle const& h,lt::client_data_t){
    attached.store(lt::biglybtc_attach_mainline_port_observer_v1(&h,&sink,Admission),std::memory_order_release);return std::shared_ptr<lt::torrent_plugin>{};
  });
  auto handle=session->add_torrent(params,ec);need(!ec && handle.is_valid(),"Actual admission failed");
  auto const Accepted=static_cast<unsigned>(lt::BiglybtcMainlinePortStatusV1::Accepted);
  if(attach)need(attached.load(std::memory_order_acquire)==Accepted,"Actual executor attach failed");
  need(lt::biglybtc_refresh_mainline_port_v1(&handle,Admission)==static_cast<unsigned>(lt::BiglybtcMainlinePortStatusV1::WrongThread),"Wrong-thread refresh accepted");
  auto refresh=[&]{refreshed.store(99,std::memory_order_release);handle.add_extension([&](lt::torrent_handle const& h,lt::client_data_t){refreshed.store(lt::biglybtc_refresh_mainline_port_v1(&h,Admission),std::memory_order_release);return std::shared_ptr<lt::torrent_plugin>{};});};
  Wire wire(io,hash);bool connected=false,checked=false,done=false;int phase=0;unsigned zeroBefore=0;
  auto const deadline=std::chrono::steady_clock::now()+25s;
  while(!done && wire.failure.empty() && std::chrono::steady_clock::now()<deadline) {
    io.restart();io.run_for(5ms); // Event pump only; no request retry or timed absence assertion.
    std::vector<lt::alert*> alerts;session->pop_alerts(&alerts);
    for(auto* a:alerts){if(lt::alert_cast<lt::torrent_checked_alert>(a))checked=true;if(lt::alert_cast<lt::torrent_error_alert>(a)||lt::alert_cast<lt::file_error_alert>(a)||lt::alert_cast<lt::listen_failed_alert>(a))throw std::runtime_error("Actual LT source/storage failure");}
    if(!connected && checked && session->listen_port()!=0){connected=true;wire.connect(session->listen_port());}
    if(!attach || privateTorrent) {
      if(wire.bitfield && phase==0){observer->port.store(live.local_endpoint().port(),std::memory_order_release);wire.expectClose=true;auto message=portMessage(remote.local_endpoint().port());message.insert(message.end(),{0,0,0,2,9,0});wire.send(std::move(message));phase=1;}
      if(wire.closed && phase==1){need(wire.advertisements.empty(),"Disabled/private route advertised PORT");need(observer->published.load()==0,"Disabled/private PORT produced hint");done=true;}
      continue;
    }
    auto const count=observer->published.load(std::memory_order_acquire);need(!observer->lost.load(),"Bounded observer lost events");
    if(wire.bitfield && phase==0){need(wire.advertisements.empty(),"Withdrawn control advertised PORT");observer->port.store(live.local_endpoint().port(),std::memory_order_release);refresh();phase=1;}
    if(phase==1 && wire.advertisements.size()==1 && refreshed.load(std::memory_order_acquire)==Accepted){
      need(wire.advertisements.front()==live.local_endpoint().port(),"PORT advertised transfer/default port");auto message=portMessage(remote.local_endpoint().port());auto zero=portMessage(0),next=portMessage(remote2.local_endpoint().port());message.insert(message.end(),zero.begin(),zero.end());message.insert(message.end(),next.begin(),next.end());wire.send(std::move(message));phase=2;
    }
    if(phase==2 && count==2){observer->port.store(0,std::memory_order_release);refresh();phase=3;}
    if(phase==3 && refreshed.load(std::memory_order_acquire)==Accepted){zeroBefore=observer->zeroReads.load();wire.send(portMessage(remote.local_endpoint().port()));phase=4;}
    if(phase==4 && observer->zeroReads.load()>zeroBefore){need(count==2,"Withdrawn PORT emitted event");observer->port.store(replacement.local_endpoint().port(),std::memory_order_release);refresh();phase=5;}
    if(phase==5 && wire.advertisements.size()==2){need(wire.advertisements.back()==replacement.local_endpoint().port(),"Existing peer did not receive live-port refresh");wire.send(portMessage(remote.local_endpoint().port()));phase=6;}
    if(phase==6 && count==3){
      for(unsigned i=0;i<count;++i){auto const& event=observer->ring[i];need(event.sequence==i+1 && event.admission==Admission && event.kind==1 && event.reserved==0,"Event identity/order mismatch");need(event.family==4 && event.protocol==1 && event.peer_port==wire.sourcePort,"Not actual peer socket/protocol");need(event.address[0]==127 && event.address[1]==0 && event.address[2]==0 && event.address[3]==1,"Not actual numeric loopback source");need(std::equal(event.wire_info_hash.begin(),event.wire_info_hash.end(),reinterpret_cast<unsigned char const*>(hash.data())),"Actual connected wire hash mismatch");need(event.dht_port==(i==1?remote2.local_endpoint().port():remote.local_endpoint().port()),"PORT field mismatch");}
      done=true;
    }
  }
  need(wire.failure.empty(),wire.failure.c_str());need(done,"Owned PORT fixture deadline");need(!session->is_dht_running(),"Bridge activated hidden torrent DHT");
  boost::system::error_code ignored;wire.socket.close(ignored);work.reset();session.reset();
  std::ifstream payload(root/"sample.bin",std::ios::binary);std::string after((std::istreambuf_iterator<char>(payload)),{});need(after==data,"Provider proof changed owned payload");
  std::cout<<"PASS\t"<<(privateTorrent?"private":attach?"live-refresh-receive-withdraw":"unbound-default")<<"\tcallbacks="<<observer->published.load()<<"\tadvertisements="<<wire.advertisements.size()<<"\n";
}
int main(int argc,char** argv) {
  try {
    need(argc==2,"Usage: owned-mainline-port-proof OWNED_PARENT");sameImage();
    auto parent=std::filesystem::canonical(argv[1]);need(std::filesystem::is_directory(parent),"Owned parent must exist");
    std::string pattern=(parent/"mainline-port-XXXXXX").string();std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back(0);auto* made=::mkdtemp(temporary.data());need(made,"Cannot create owned fixture root");std::filesystem::path root(made);
    run(root/"public",false,true);run(root/"private",true,true);run(root/"unbound",false,false);
    std::cout<<"PASS\tall-owned-mainline-port-cases\troot="<<root.string()<<"\n";return 0;
  }catch(std::exception const& e){std::cerr<<"FAIL\t"<<e.what()<<"\n";return 1;}
}
