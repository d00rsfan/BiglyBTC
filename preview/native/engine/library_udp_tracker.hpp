#pragma once
#include "biglybtc/library.hpp"
#include "biglybtc/queue_observations.hpp"
#include "network_policy.hpp"
#include "profile.hpp"
#include <libtorrent/address.hpp>
#include <libtorrent/socket.hpp>
#include <openssl/rand.h>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <sys/socket.h>
#include <unistd.h>

namespace biglybtc::engine::detail {
namespace udp_tracker_wire {
inline std::uint32_t word(std::string_view p,std::size_t at){
  std::uint32_t value=0;for(unsigned i=0;i<4;++i){value=(value<<8)|static_cast<unsigned char>(p[at+i]);}return value;
}
inline void put(std::span<char> out,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i){out[at+i]=static_cast<char>(value>>(24-8*i));}}
inline std::array<char,16> connect(std::uint32_t transaction){
  std::array<char,16> p{};put(p,0,0x417);put(p,4,0x27101980);put(p,8,0);put(p,12,transaction);return p;
}
inline std::array<char,36> scrape(std::uint64_t connection,std::uint32_t transaction,std::span<const std::byte,20> hash){
  std::array<char,36> p{};put(p,0,static_cast<std::uint32_t>(connection>>32));put(p,4,static_cast<std::uint32_t>(connection));put(p,8,2);put(p,12,transaction);
  for(unsigned i=0;i<20;++i){p[16+i]=static_cast<char>(hash[i]);}return p;
}
enum class ReplyKind {Connect,Scrape,Error,Unsupported};
struct Reply {ReplyKind kind;std::uint64_t connection=0;std::int32_t seeds=0,downloaded=0,peers=0;};
// Canonical single-hash BEP15/VERSION2 subset. Wrong transaction/action,
// partial/multiple entries and trailing bytes are never accepted as counts.
inline std::optional<Reply> reply(std::string_view packet,std::uint32_t transaction,bool connecting){
  if(packet.size()<8||word(packet,4)!=transaction){return {};}
  if(packet.size()>1024){return Reply{ReplyKind::Unsupported};} // A truncated correlated reply cannot become timeout/error authority.
  const auto action=word(packet,0);if(action==3){return Reply{ReplyKind::Error};}
  if(connecting&&action==0&&packet.size()==16){return Reply{ReplyKind::Connect,(static_cast<std::uint64_t>(word(packet,8))<<32)|word(packet,12)};}
  if(!connecting&&action==2&&packet.size()==20){return Reply{ReplyKind::Scrape,0,std::bit_cast<std::int32_t>(word(packet,8)),std::bit_cast<std::int32_t>(word(packet,12)),std::bit_cast<std::int32_t>(word(packet,16))};}
  if((connecting&&action==0)||(!connecting&&action==2)){return Reply{ReplyKind::Unsupported};}
  return {};
}
} // namespace udp_tracker_wire

struct LibraryUdpRoute {
  libtorrent::udp::endpoint remote,local;
  bool operator==(const LibraryUdpRoute&)const=default;
};
inline std::expected<LibraryUdpRoute,storage::Error> libraryUdpRoute(const NetworkPolicy& p,std::string_view url,bool regularAuthority=false){
  const auto unsupported=[](){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Standalone tracker scrape requires one numeric direct UDP route"});};
  if(p.mode!=NetworkMode::Normal||(!regularAuthority&&!p.dht)||!p.udpTrackers||p.proxy.kind!=NetworkProxyKind::None||!url.starts_with("udp://")||url.size()>2048){return unsupported();}
  const auto end=url.find('/',6);if(end==url.npos||url.substr(end)!="/announce"){return unsupported();}
  const auto authority=url.substr(6,end-6);std::string_view host,port;
  if(authority.starts_with("[")){const auto close=authority.find(']');if(close==authority.npos||close+1>=authority.size()||authority[close+1]!=':'){return unsupported();}host=authority.substr(1,close-1);port=authority.substr(close+2);}
  else{const auto colon=authority.find(':');if(colon==authority.npos||authority.find(':',colon+1)!=authority.npos){return unsupported();}host=authority.substr(0,colon);port=authority.substr(colon+1);}
  unsigned number=0;const auto parsed=std::from_chars(port.data(),port.data()+port.size(),number);if(port.empty()||parsed.ec!=std::errc{}||parsed.ptr!=port.data()+port.size()||!number||number>65535){return unsupported();}
  libtorrent::error_code ec;const auto remote=libtorrent::make_address(std::string(host),ec);
  if(ec||remote.is_unspecified()||remote.is_multicast()||(!p.ipv6&&remote.is_v6())
      ||(remote.is_v6()&&(remote.to_v6().is_v4_mapped()||remote.to_v6().scope_id()!=0))){return unsupported();}
  if(remote.is_v4()){const auto x=remote.to_v4().to_uint();if((x>>24)==0||(x>>24)>=224){return unsupported();}}
  std::optional<libtorrent::address> local;
  for(const auto& text:p.bindAddresses){libtorrent::error_code error;const auto address=libtorrent::make_address(text,error);
    if(error){return unsupported();}if(address.is_v4()!=remote.is_v4()){continue;}
    if(local&&*local!=address){return unsupported();}local=address;
  }
  if(!local){return unsupported();}
  // Explicit loopback is owned-test/local scope; wildcards never authorise it.
  // Initial production subset refuses other private/local contacts.
  bool privateRoute=remote.is_loopback();
  if(remote.is_v4()){const auto x=remote.to_v4().to_uint();privateRoute=privateRoute||(x&0xff000000u)==0x0a000000u||(x&0xfff00000u)==0xac100000u||(x&0xffff0000u)==0xc0a80000u||(x&0xffc00000u)==0x64400000u||(x&0xffff0000u)==0xa9fe0000u;}
  else{const auto x=remote.to_v6();privateRoute=privateRoute||x.is_link_local()||x.is_site_local()||(x.to_bytes()[0]&0xfe)==0xfc;}
  if(privateRoute&&(!remote.is_loopback()||!local->is_loopback())){return unsupported();}
  return LibraryUdpRoute{{remote,static_cast<unsigned short>(number)},{*local,0}};
}

class LibraryUdpTracker final {
public:
  struct Request {core::TorrentHash hash;std::string responseKey,scope,configuration;std::uint64_t nonce=0;bool regularAuthority=false;};
  struct Result {Request request;core::QueueTrackerTerminal terminal;std::int64_t observedNanos=0;};
  static constexpr std::int64_t StageTimeoutMillis=30000; // Original DEFAULT_UDP_TIMEOUT, one retry-count1.
  LibraryUdpTracker()=default;LibraryUdpTracker(const LibraryUdpTracker&)=delete;LibraryUdpTracker& operator=(const LibraryUdpTracker&)=delete;
  ~LibraryUdpTracker()=default;
  const Request* active()const noexcept{return round_?&round_->request:nullptr;}
  void cancel()noexcept{round_.reset();}
  std::expected<void,storage::Error> start(Request request,const LibraryUdpRoute& route,std::int64_t now,std::int64_t wall){
    if(std::this_thread::get_id()!=worker_||round_||!request.nonce||request.hash.bytes().size()!=20||request.responseKey.empty()||request.scope.empty()||now<0||now>INT64_MAX-StageTimeoutMillis){return fail(storage::ErrorCode::Conflict,"Standalone tracker operation is unavailable");}
    std::array<std::uint32_t,2> transactions{};bool random=false;
    for(unsigned tries=0;tries<8;++tries){if(RAND_bytes(reinterpret_cast<unsigned char*>(transactions.data()),sizeof(transactions))!=1){break;}if(transactions[0]!=transactions[1]){random=true;break;}}
    if(!random){return fail(storage::ErrorCode::Io,"Tracker transaction entropy failed");}
    static_assert(std::is_nothrow_move_constructible_v<Round>); // No allocation/throw between actual send and retained ownership.
    Round prepared{std::move(request),route,transactions,now,wall,now+StageTimeoutMillis};
    const int family=route.remote.address().is_v4()?AF_INET:AF_INET6;
    prepared.socket.fd=::socket(family,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(prepared.socket.fd<0||::bind(prepared.socket.fd,reinterpret_cast<const sockaddr*>(route.local.data()),route.local.size())!=0
        ||::connect(prepared.socket.fd,reinterpret_cast<const sockaddr*>(route.remote.data()),route.remote.size())!=0){return fail(storage::ErrorCode::Io,"Cannot bind the explicit tracker route");}
    const auto packet=udp_tracker_wire::connect(transactions[0]);
    if(::send(prepared.socket.fd,packet.data(),packet.size(),0)!=static_cast<ssize_t>(packet.size())){return fail(storage::ErrorCode::Io,"Tracker connect send failed");}
    round_.emplace(std::move(prepared));return {};
  }
  template<class Authorise>
  std::expected<std::optional<Result>,storage::Error> poll(std::int64_t now,std::int64_t wall,Authorise&& authorise){
    if(std::this_thread::get_id()!=worker_){return fail(storage::ErrorCode::Conflict,"Tracker worker changed");}
    if(!round_){return std::optional<Result>{};}if(now<round_->started||now>INT64_MAX-StageTimeoutMillis){cancel();return fail(storage::ErrorCode::Conflict,"Tracker monotonic clock changed");}
    const auto terminal=[&](bool success,std::int32_t seeds=0,std::int32_t peers=0,std::int32_t downloaded=0)->std::optional<Result>{
      // Capture the actual consumed terminal before fresh profile authority IO;
      // a later provider event during that IO must keep its true chronology.
      const auto observed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      if(!authorise(round_->request)){cancel();return {};}
      return finish(now,wall,observed,success,seeds,peers,downloaded);
    };
    if(now>=round_->deadline){return terminal(false);}
    for(unsigned count=0;count<16&&round_;++count){std::array<char,1025> packet{};const auto got=::recv(round_->socket.fd,packet.data(),packet.size(),0);
      if(got<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR){break;}return terminal(false);}
      const auto decoded=udp_tracker_wire::reply({packet.data(),static_cast<std::size_t>(got)},round_->transactions[round_->connecting?0:1],round_->connecting);
      if(!decoded){continue;}if(decoded->kind==udp_tracker_wire::ReplyKind::Unsupported){round_->unknown=true;return terminal(false);}
      if(decoded->kind==udp_tracker_wire::ReplyKind::Error){return terminal(false);}
      if(decoded->kind==udp_tracker_wire::ReplyKind::Connect){
        if(!authorise(round_->request)){cancel();return std::optional<Result>{};}
        const auto request=udp_tracker_wire::scrape(decoded->connection,round_->transactions[1],std::span<const std::byte,20>{round_->request.hash.bytes().data(),20});
        if(::send(round_->socket.fd,request.data(),request.size(),0)!=static_cast<ssize_t>(request.size())){return terminal(false);}
        round_->connecting=false;round_->deadline=now+StageTimeoutMillis;
      }else{return terminal(true,decoded->seeds,decoded->peers,decoded->downloaded);}
    }
    if(round_&&now>=round_->deadline){return terminal(false);}return std::optional<Result>{};
  }
private:
  struct Socket {int fd=-1;Socket()=default;Socket(Socket&& other)noexcept:fd(std::exchange(other.fd,-1)){}Socket(const Socket&)=delete;~Socket(){if(fd>=0){::close(fd);}}};
  struct Round {Request request;LibraryUdpRoute route;std::array<std::uint32_t,2> transactions;std::int64_t started,wall,deadline;Socket socket{};bool connecting=true,unknown=false;};
  const std::thread::id worker_=std::this_thread::get_id();std::optional<Round> round_;
  static std::unexpected<storage::Error> fail(storage::ErrorCode code,const char* text){return std::unexpected(storage::Error{code,text});}
  std::optional<Result> finish(std::int64_t now,std::int64_t wall,std::int64_t observed,bool success,std::int32_t seeds=0,std::int32_t peers=0,std::int32_t downloaded=0){
    core::QueueTrackerTerminal terminal;terminal.kind=success?core::QueueTrackerTerminalKind::Scrape:core::QueueTrackerTerminalKind::ScrapeFailed;
    terminal.complete=seeds;terminal.incomplete=peers;terminal.downloaded=downloaded;terminal.present=success?7u:0u;terminal.comparable=success&&!round_->unknown;terminal.definitiveFailure=!success&&!round_->unknown;
    terminal.observedMillis=now;terminal.observedWallMillis=wall;terminal.requestStartedWallMillis=round_->wall;
    Result result{std::move(round_->request),terminal,observed};cancel();return result;
  }
};
} // namespace biglybtc::engine::detail
