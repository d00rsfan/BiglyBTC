// Included only by this standalone fixture. All operations run on one io_context.
struct ReconnectObservation {
  std::string name,proxy_error;
  Error old_error,new_error;
  unsigned old_calls=0,new_calls=0,delivery=0,accepts=0,auth_at_boundary=0,routes_at_boundary=0;
  bool timed_out=false,closed_before=false,reset_before=false,no_callback_before_boundary=false;
  std::vector<Route> routes;
};
ReconnectObservation reconnectPhase(asio::io_context& io,lt::socks5_stream& stream,
    std::string name,unsigned delivery,bool error_callback) {
  io.restart();Proxy proxy(io,1,error_callback);asio::steady_timer watchdog(io,std::chrono::seconds(5));
  ReconnectObservation out;out.name=std::move(name);bool finishing=false,started=false;
  auto gate=std::make_shared<DeliveryGate>();gate->target=delivery;
  std::string const fresh=error_callback?"fresh-after-error.invalid":delivery==1?"fresh-after-resolver.invalid":"fresh-after-auth.invalid";
  unsigned short const port=error_callback?32133:delivery==1?32131:32132;
  auto finish=[&]{if(out.old_calls&&out.new_calls&&!finishing){finishing=true;asio::post(io,[&]{watchdog.cancel();proxy.stop();});}};
  auto launch=[&]{
    if(started){out.proxy_error="duplicate new attempt";return;}started=true;
    stream.set_dst_name(fresh);
    stream.async_connect(tcp::endpoint(asio::ip::address_v4::any(),port),asio::bind_executor(io.get_executor(),[&](Error e){
      ++out.new_calls;out.new_error=e;finish();
    }));
  };
  stream.set_version(5);stream.set_command(1);stream.set_proxy("127.0.0.1",proxy.port());stream.set_dst_name("old-attempt.invalid");
  if(!error_callback)gate->first=[&]{
    out.delivery=gate->deliveries;out.no_callback_before_boundary=out.old_calls==0&&out.new_calls==0;
    out.auth_at_boundary=proxy.auth_replies;out.routes_at_boundary=static_cast<unsigned>(proxy.routes.size());
    // Old auth-reply delivery retains its own read buffer while the NEW attempt
    // is admitted. Only EOF/reset from that exact old connection is expected.
    if(delivery==4&&!proxy.peers.empty())proxy.peers.front()->expected_close=true;
    Error e;stream.close(e);out.closed_before=!e&&!stream.is_open();
    Error endpoint_error;auto endpoint=stream.remote_endpoint(endpoint_error);
    out.reset_before=endpoint.address().is_unspecified()&&endpoint.port()==0;
    launch(); // New resolver is outstanding BEFORE the old continuation enters.
  };
  watchdog.async_wait([&](Error e){if(!e){out.timed_out=true;Error ignored;stream.close(ignored);proxy.stop();}});
  stream.async_connect(tcp::endpoint(asio::ip::address_v4::any(),32130),asio::bind_executor(GateExecutor{io.get_executor(),gate},[&](Error e){
    ++out.old_calls;out.old_error=e;
    if(error_callback){
      // This is the actual unsupported-version error callback. Cleanup must
      // already be complete; no old cleanup may cancel our new resolver later.
      out.closed_before=!stream.is_open();Error ignored;auto endpoint=stream.remote_endpoint(ignored);
      out.reset_before=endpoint.address().is_unspecified()&&endpoint.port()==0;launch();
    }
    finish();
  }));
  io.run();out.accepts=proxy.accepts;out.routes=proxy.routes;
  if(!proxy.error.empty())out.proxy_error=proxy.error;
  Error ignored;stream.close(ignored);io.restart();io.poll();return out;
}
bool reconnectValid(ReconnectObservation const& o,unsigned delivery,bool error_callback){
  Error expected=error_callback?lt::socks_error::make_error_code(lt::socks_error::unsupported_version):Error(asio::error::operation_aborted);
  std::string const fresh=error_callback?"fresh-after-error.invalid":delivery==1?"fresh-after-resolver.invalid":"fresh-after-auth.invalid";
  unsigned const port=error_callback?32133:delivery==1?32131:32132;
  return !o.timed_out&&o.old_calls==1&&o.new_calls==1&&o.old_error==expected&&!o.new_error
      &&o.closed_before&&o.reset_before&&o.proxy_error.empty()
      &&(error_callback||(o.delivery==delivery&&o.no_callback_before_boundary&&o.routes_at_boundary==0
        &&o.auth_at_boundary==(delivery==1?0u:1u)))
      &&o.accepts==(error_callback||delivery==4?2u:1u)&&o.routes.size()==1
      &&o.routes[0].command==1&&o.routes[0].type==3&&o.routes[0].host==fresh&&o.routes[0].port==port;
}
void print(ReconnectObservation const& o){
  std::cout<<"CASE\t"<<o.name<<"\told_callbacks="<<o.old_calls<<"\tnew_callbacks="<<o.new_calls
      <<"\told_error="<<o.old_error.category().name()<<":"<<o.old_error.value()<<"\tnew_error="<<o.new_error.category().name()<<":"<<o.new_error.value()
      <<"\tdelivery="<<o.delivery<<"\tclosed_before="<<o.closed_before<<"\treset_before="<<o.reset_before
      <<"\taccepts="<<o.accepts<<"\troutes="<<o.routes.size()<<"\tdeadline="<<o.timed_out<<"\tproxy_error="<<o.proxy_error<<"\n";
  for(auto const& r:o.routes)std::cout<<"CONNECT\t"<<o.name<<"\tcmd="<<r.command<<"\tatyp="<<r.type<<"\thost="<<r.host<<"\tport="<<r.port<<"\twire="<<r.wire<<"\n";
}
