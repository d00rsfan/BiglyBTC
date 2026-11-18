// SPDX-License-Identifier: GPL-2.0-or-later
#include <libtorrent/aux_/bandwidth_manager.hpp>
#include <libtorrent/aux_/bandwidth_limit.hpp>
#include <libtorrent/aux_/bandwidth_socket.hpp>
#include <libtorrent/time.hpp>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace lt=libtorrent;
struct Socket final:lt::aux::bandwidth_socket {
 int granted=0,calls=0;bool disconnected=false;
 void assign_bandwidth(int,int amount)override{granted+=amount;++calls;}
 bool is_disconnecting()const override{return disconnected;}
};
void check(bool good,const char* why){if(!good)throw std::runtime_error(why);}
struct Channels {
 std::vector<lt::aux::bandwidth_channel> values;
 std::vector<lt::aux::bandwidth_channel*> pointers;
 explicit Channels(int count,int rate=4096):values(count){for(auto& v:values){v.throttle(rate);pointers.push_back(&v);}}
};
void tail_limit(int count){
 for(int position: {0,count/2,count-1}){
  Channels channels(count);channels.values[position].throttle(1);auto peer=std::make_shared<Socket>();lt::aux::bandwidth_manager manager(0);
  check(manager.request_bandwidth(peer,4096,1,channels.pointers.data(),count)==0,"request not queued");manager.update_quotas(lt::seconds(1));
  check(peer->granted==0&&manager.queued_bytes()==4095,"strict channel not applied");
  for(auto& v:channels.values)check(v.quota_left()==(v.throttle()==1?0:4095),"quota debit skipped a channel");
  peer->disconnected=true;manager.update_quotas(lt::milliseconds(0));
  check(peer->granted==0&&peer->calls==1&&manager.queued_bytes()==0&&manager.queue_size()==0,"disconnect did not retire request");
  for(auto& v:channels.values){check(v.quota_left()==(v.throttle()==1?1:4096),"disconnect did not return all quota");}
  manager.close();
 }
}
void shared(int count){
 Channels channels(count);channels.values.back().throttle(1024);auto a=std::make_shared<Socket>();auto b=std::make_shared<Socket>();lt::aux::bandwidth_manager manager(0);
 check(manager.request_bandwidth(a,4096,1,channels.pointers.data(),count)==0,"shared A request");
 check(manager.request_bandwidth(b,4096,3,channels.pointers.data(),count)==0,"shared B request");
 manager.update_quotas(lt::seconds(1));check(manager.queued_bytes()==8192-1024,"shared budget was multiplied");
 for(auto& v:channels.values)check(v.quota_left()==(v.throttle()==1024?0:3072),"shared debit mismatch");
 a->disconnected=true;manager.update_quotas(lt::milliseconds(0));check(manager.queue_size()==1&&a->calls==1&&a->granted==0,"shared disconnect");
 check(manager.queued_bytes()==4096-1024,"returned quota not reassigned with survivor priority");
 b->disconnected=true;manager.update_quotas(lt::milliseconds(0));
 check(manager.queued_bytes()==0&&manager.queue_size()==0,"shared queued bytes left behind");
 for(auto& v:channels.values){check(v.quota_left()==v.throttle(),"shared total quota not restored");}
 manager.close();
}
void live_limits(){
 Channels channels(12,0);auto peer=std::make_shared<Socket>();lt::aux::bandwidth_manager manager(0);
 check(manager.request_bandwidth(peer,4096,1,nullptr,0)==4096,"unlimited channels delayed");
 channels.values.back().throttle(1);check(manager.request_bandwidth(peer,4096,1,&channels.pointers.back(),1)==0,"last limiter not queued");
 manager.update_quotas(lt::seconds(1));check(manager.queued_bytes()==4095,"live limit omitted");
 channels.values.back().throttle(0);manager.update_quotas(lt::seconds(1));check(peer->granted==4096&&manager.queued_bytes()==0,"live unlimited did not release request");manager.close();
}
int main(){try{for(int count:{1,5,6,8,10,11,12,16,30,64,256}){tail_limit(count);shared(count);}live_limits();std::cout<<"PASS all-channel quota:33 limiter positions,11 weighted shared/disconnect cases,live unlimited transition\n";}catch(std::exception const& e){std::cerr<<"FAIL quota: "<<e.what()<<'\n';return 1;}}
