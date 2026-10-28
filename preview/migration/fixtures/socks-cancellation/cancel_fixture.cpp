#include <libtorrent/socks5_stream.hpp>
#include <boost/asio.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace asio=boost::asio;
namespace lt=libtorrent;
using tcp=asio::ip::tcp;
using Error=boost::system::error_code;

// wrap_allocator_t forwards the final handler's associated executor to the
// ACTUAL resolver completion. No fake resolver, sleep, DNS packet or retry.
struct DeliveryGate {
  std::function<void()> first;
  unsigned deliveries=0,target=1;
  void before() { ++deliveries; if(first&&deliveries==target){auto once=std::move(first);once();} }
};
struct GateExecutor {
  asio::io_context::executor_type inner;
  std::shared_ptr<DeliveryGate> gate;
  asio::execution_context& context() const noexcept {return inner.context();}
  void on_work_started() const noexcept {inner.on_work_started();}
  void on_work_finished() const noexcept {inner.on_work_finished();}
  template<class F,class A> void dispatch(F&& f,A const& a) const {
    inner.dispatch([g=gate,fn=std::forward<F>(f)]() mutable {g->before();std::move(fn)();},a);
  }
  template<class F,class A> void post(F&& f,A const& a) const {
    inner.post([g=gate,fn=std::forward<F>(f)]() mutable {g->before();std::move(fn)();},a);
  }
  template<class F,class A> void defer(F&& f,A const& a) const {
    inner.defer([g=gate,fn=std::forward<F>(f)]() mutable {g->before();std::move(fn)();},a);
  }
  friend bool operator==(GateExecutor const& a,GateExecutor const& b) noexcept {return a.inner==b.inner&&a.gate==b.gate;}
  friend bool operator!=(GateExecutor const& a,GateExecutor const& b) noexcept {return !(a==b);}
};
static_assert(asio::is_executor<GateExecutor>::value,"Associated executor must really be honoured");

std::string hex(std::vector<unsigned char> const& bytes) {
  std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto x:bytes)out<<std::setw(2)<<unsigned(x);return out.str();
}
struct Route {
  unsigned command=0,type=0,port=0;
  std::string host,wire;
};
struct Proxy {
  struct Connection:std::enable_shared_from_this<Connection> {
    Proxy& owner;
    tcp::socket socket;
    std::array<unsigned char,4> head{};
    std::vector<unsigned char> body,frame;
    std::array<unsigned char,2> method{{5,0}};
    std::array<unsigned char,10> ack{{5,0,0,1,127,0,0,1,0,0}};
    bool expected_close=false;
    Connection(Proxy& p,tcp::socket s):owner(p),socket(std::move(s)){
      if(owner.reject_first_method&&owner.accepts==1)method[0]=4;
      if(owner.command==3){auto port=owner.datagram.local_endpoint().port();ack[8]=static_cast<unsigned char>(port>>8);ack[9]=static_cast<unsigned char>(port);}
    }
    void stop(){Error ignored;socket.close(ignored);}
    void error(Error e){
      bool expected=expected_close&&(e==asio::error::eof||e==asio::error::operation_aborted||e==asio::error::connection_reset);
      if(e&&!expected&&!owner.stopping&&owner.error.empty())owner.error=e.message();
    }
    void start() {
      auto self=shared_from_this();
      asio::async_read(socket,asio::buffer(head.data(),3),[self](Error e,std::size_t){
        if(e){self->error(e);return;}
        if(self->head[0]!=5||self->head[1]!=1||self->head[2]!=0){self->owner.error="unexpected authentication bytes";self->stop();return;}
        ++self->owner.auth_replies;
        asio::async_write(self->socket,asio::buffer(self->method),[self](Error e,std::size_t){
          if(e){self->error(e);return;}
          if(self->method[0]!=5)return; // Keep bad-auth transport open; client must clean it up.
          asio::async_read(self->socket,asio::buffer(self->head),[self](Error e,std::size_t){
            if(e){self->error(e);return;}
            if(self->head[0]!=5||self->head[1]!=self->owner.command||self->head[2]!=0){self->owner.error="unexpected SOCKS request header";self->stop();return;}
            self->frame.assign(self->head.begin(),self->head.end());
            if(self->head[3]==3){
              self->body.resize(1);
              asio::async_read(self->socket,asio::buffer(self->body),[self](Error e,std::size_t){
                if(e){self->error(e);return;}
                auto const length=self->body[0];self->frame.push_back(length);self->read_destination(std::size_t(length)+2);
              });
            }else if(self->head[3]==1){self->read_destination(6);}
            else if(self->head[3]==4){self->read_destination(18);}
            else {self->owner.error="invalid address type";self->stop();}
          });
        });
      });
    }
    void read_destination(std::size_t count) {
      auto self=shared_from_this();body.resize(count);
      asio::async_read(socket,asio::buffer(body),[self](Error e,std::size_t){
        if(e){self->error(e);return;}
        auto const n=self->body.size();self->frame.insert(self->frame.end(),self->body.begin(),self->body.end());
        Route r;r.command=self->head[1];r.type=self->head[3];r.port=(unsigned(self->body[n-2])<<8)|self->body[n-1];r.wire=hex(self->frame);
        if(r.type==3)r.host.assign(self->body.begin(),self->body.end()-2);
        else if(r.type==1){asio::ip::address_v4::bytes_type b{};std::copy_n(self->body.begin(),4,b.begin());r.host=asio::ip::address_v4(b).to_string();}
        else {asio::ip::address_v6::bytes_type b{};std::copy_n(self->body.begin(),16,b.begin());r.host=asio::ip::address_v6(b).to_string();}
        self->owner.routes.push_back(std::move(r));
        // Protocol-only owned proxy: ACK is real, target forwarding never occurs.
        asio::async_write(self->socket,asio::buffer(self->ack),[self](Error e,std::size_t){self->error(e);});
      });
    }
  };
  tcp::acceptor listener;
  asio::ip::udp::socket datagram;
  unsigned char command=1;
  bool reject_first_method=false;
  std::vector<std::shared_ptr<Connection>> peers;
  std::vector<Route> routes;
  unsigned accepts=0,auth_replies=0;
  bool stopping=false;
  std::string error;
  explicit Proxy(asio::io_context& io,unsigned char cmd=1,bool reject=false)
      :listener(io,tcp::endpoint(asio::ip::address_v4::loopback(),0)),datagram(io),command(cmd),reject_first_method(reject){
    if(command==3){datagram.open(asio::ip::udp::v4());datagram.bind({asio::ip::address_v4::loopback(),0});}accept();
  }
  unsigned short port()const{return listener.local_endpoint().port();}
  void accept(){listener.async_accept([this](Error e,tcp::socket s){
    if(e){if(!stopping)error=e.message();return;}
    ++accepts;auto peer=std::make_shared<Connection>(*this,std::move(s));peers.push_back(peer);peer->start();accept();
  });}
  void stop(){stopping=true;Error ignored;listener.close(ignored);datagram.close(ignored);for(auto& p:peers)p->stop();}
  ~Proxy(){stop();}
};
struct Observation {
  std::string name,error,category,proxy_error;
  unsigned completions=0,boundaries=0,accepts=0;
  int code=0;
  bool timed_out=false,closed_at_boundary=false,open_at_completion=false;
  std::vector<Route> routes;
};
Observation phase(asio::io_context& io,lt::socks5_stream& stream,std::string name,std::string host,unsigned short port,bool cancel,unsigned char command=1) {
  io.restart();Proxy proxy(io,command);asio::steady_timer watchdog(io,std::chrono::seconds(5));
  Observation result;result.name=std::move(name);auto gate=std::make_shared<DeliveryGate>();
  stream.set_version(5);stream.set_command(command);stream.set_proxy("127.0.0.1",proxy.port());if(!host.empty())stream.set_dst_name(host);
  if(cancel)gate->first=[&]{
    ++result.boundaries;
    if(result.completions!=0){result.error="user completion preceded resolver boundary";}
    Error e;stream.close(e);result.closed_at_boundary=!e&&!stream.is_open();
  };
  watchdog.async_wait([&](Error e){if(!e){result.timed_out=true;Error ignored;stream.close(ignored);proxy.stop();}});
  stream.async_connect(tcp::endpoint(asio::ip::address_v4::any(),port),asio::bind_executor(GateExecutor{io.get_executor(),gate},[&](Error e){
    ++result.completions;result.code=e.value();result.category=e.category().name();result.error=e.message();result.open_at_completion=stream.is_open();
    asio::post(io,[&]{watchdog.cancel();proxy.stop();});
  }));
  io.run();result.accepts=proxy.accepts;result.routes=proxy.routes;result.proxy_error=proxy.error;
  Error ignored;stream.close(ignored);io.restart();io.poll();return result;
}
bool named(Observation const& o,std::string const& name,unsigned port){
  return !o.timed_out&&o.completions==1&&o.code==0&&o.proxy_error.empty()&&o.accepts==1&&o.routes.size()==1
      &&o.routes[0].type==3&&o.routes[0].host==name&&o.routes[0].port==port;
}
void print(Observation const& o){
  std::cout<<"CASE\t"<<o.name<<"\tcallbacks="<<o.completions<<"\tec="<<o.category<<":"<<o.code
      <<"\tclose_boundary="<<o.boundaries<<"\tclosed="<<o.closed_at_boundary<<"\topen_at_callback="<<o.open_at_completion
      <<"\taccepts="<<o.accepts<<"\troutes="<<o.routes.size()<<"\tdeadline="<<o.timed_out<<"\tproxy_error="<<o.proxy_error<<"\n";
  for(auto const& r:o.routes)std::cout<<"CONNECT\t"<<o.name<<"\tcmd="<<r.command<<"\tatyp="<<r.type<<"\thost="<<r.host<<"\tport="<<r.port<<"\twire="<<r.wire<<"\n";
}
#include "reconnect_cases.inc"

int main(int argc,char** argv){
  try {
    bool const legacy=argc==2&&std::string(argv[1])=="--expect-legacy";
    if(!(argc==2&&(legacy||std::string(argv[1])=="--expect-fixed")))throw std::runtime_error("Use --expect-legacy or --expect-fixed");
    asio::io_context io;lt::socks5_stream stream(io);
    auto ordinary=phase(io,stream,"ordinary","ordinary.invalid",32123,false);print(ordinary);
    auto cancelled=phase(io,stream,"cancel-at-resolver-delivery","cancelled.invalid",32124,true);print(cancelled);
    // No overlapping shared m_buffer I/O: the old completion chain has drained.
    // Reuse independently proves the closed stream can establish a NEW name.
    auto reused=phase(io,stream,"reuse-after-drain","after-close.invalid",32125,false);print(reused);
    bool const old_bug=!cancelled.timed_out&&cancelled.boundaries==1&&cancelled.closed_at_boundary&&cancelled.completions==1
        &&cancelled.code==0&&cancelled.open_at_completion&&cancelled.proxy_error.empty()&&cancelled.accepts==1&&cancelled.routes.size()==1
        &&cancelled.routes[0].type==1&&cancelled.routes[0].host=="0.0.0.0"&&cancelled.routes[0].port==0
        &&cancelled.routes[0].wire=="05010001000000000000";
    Error const aborted=asio::error::operation_aborted;
    bool const fixed=!cancelled.timed_out&&cancelled.boundaries==1&&cancelled.closed_at_boundary&&cancelled.completions==1
        &&cancelled.category==aborted.category().name()&&cancelled.code==aborted.value()&&!cancelled.open_at_completion
        &&cancelled.accepts==0&&cancelled.routes.empty()&&cancelled.proxy_error.empty();
    bool const controls=named(ordinary,"ordinary.invalid",32123)&&named(reused,"after-close.invalid",32125);
    if(!controls){std::cout<<"RESULT\tFAIL_ORDINARY_OR_REUSE_CONTROL\n";return 2;}
    if(legacy&&old_bug){std::cout<<"RESULT\tOBSERVED_LEGACY_CANCEL_REOPEN_ZERO_CONNECT\n";return 0;}
    if(!legacy&&fixed){
      auto resolver=reconnectPhase(io,stream,"close-and-reconnect-inside-resolver-delivery",1,false);print(resolver);
      auto auth=reconnectPhase(io,stream,"close-and-reconnect-inside-auth-reply-delivery",4,false);print(auth);
      auto reentry=reconnectPhase(io,stream,"reconnect-inside-invalid-auth-callback",0,true);print(reentry);
      auto udp=phase(io,stream,"explicit-cmd3-default-zero","",0,false,3);print(udp);
      bool const cmd3=!udp.timed_out&&udp.completions==1&&udp.code==0&&udp.proxy_error.empty()
          &&udp.accepts==1&&udp.routes.size()==1&&udp.routes[0].command==3&&udp.routes[0].type==1
          &&udp.routes[0].host=="0.0.0.0"&&udp.routes[0].port==0&&udp.routes[0].wire=="05030001000000000000";
      bool const all=reconnectValid(resolver,1,false)&&reconnectValid(auth,4,false)&&reconnectValid(reentry,0,true)&&cmd3;
      std::cout<<"RESULT\t"<<(all?"PASS_FIXED_CANCEL_RECONNECT_AND_CMD3":"FAIL_FIXED_RECONNECT_OR_CMD3")<<"\n";return all?0:1;
    }
    std::cout<<"RESULT\tFAIL_EXPECTED_CAUSAL_SEQUENCE\tlegacy_observed="<<old_bug<<"\tfixed_observed="<<fixed<<"\n";return 1;
  }catch(std::exception const& e){std::cerr<<"FAIL\t"<<e.what()<<"\n";return 2;}
}
