#include <libtorrent/aux_/biglybtc_shared_quota.hpp>
#include <array>
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <future>
#include <vector>
namespace {
int checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
struct Clock {std::atomic<std::int64_t> now{0};static std::int64_t get(void* raw)noexcept{return static_cast<Clock*>(raw)->now.load();}};
struct Scope {Clock clock;void* value=libtorrent::aux::make_shared_quota_scope(Clock::get,&clock);~Scope(){biglybtc_shared_quota_scope_close_v1(value);biglybtc_shared_quota_scope_release_v1(value);}};
struct Budget {biglybtc_shared_quota_v1 value{};Budget(Scope& s,std::uint64_t tag,int up,int down){require(biglybtc_shared_quota_acquire_v1(s.value,tag,up,down,&value)==0,"acquire");}~Budget(){if(value.budget)value.release_budget(value.budget);}Budget(const Budget&)=delete;};
int take(Budget& b,int direction,int maximum=std::numeric_limits<int>::max()){
 auto& o=b.value;o.enter(o.scope);const auto n=std::min(maximum,o.available(o.budget,direction));o.use(o.budget,direction,n);o.leave(o.scope);return n;
}
void tiny(){
 for(int rate=1;rate<=10;++rate){Scope scope;Budget a(scope,1,rate,0),b(scope,1,rate,0);require(a.value.budget==b.value.budget,"one tag must have one common budget");long sum=0;
  for(int step=0;step<100;++step){scope.clock.now+=100000000;sum+=take(a,0);sum+=take(b,0);}
  require(sum==rate*10,"shared tiny grants must match ten real seconds,not two session timers");
 }
}
void updates(){
 Scope scope;Budget a(scope,1,10,20),b(scope,1,10,20);scope.clock.now=1000000000;require(take(a,0)==10&&take(b,0)==0,"joint upload grant");require(take(b,1)==20&&take(a,1)==0,"independent download grant");
 scope.clock.now=10000000000;require(a.value.available(a.value.budget,0)==30,"three-second burst bound");require(take(a,0,12)==12,"quota reservation");a.value.give_back(a.value.budget,0,12);require(b.value.available(b.value.budget,0)==30,"refund shared with another owner");
 auto epoch=a.value.epoch(a.value.budget);require(biglybtc_shared_quota_set_v1(a.value.budget,2,20)==0,"lower rate");require(b.value.available(b.value.budget,0)==6,"lower rate clips old credit");require(a.value.epoch(a.value.budget)>epoch,"changed rate revokes old version");epoch=a.value.epoch(a.value.budget);require(biglybtc_shared_quota_set_v1(a.value.budget,2,20)==0&&a.value.epoch(a.value.budget)==epoch,"exact noop preserves version");
 require(biglybtc_shared_quota_set_v1(a.value.budget,-1,0)==0,"disable upload independently");require(b.value.limit(b.value.budget,0)==-1&&b.value.available(b.value.budget,0)==std::numeric_limits<int>::max()&&b.value.available(b.value.budget,1)==std::numeric_limits<int>::max(),"disabled payload retains protocol byte budget");
 biglybtc_shared_quota_scope_close_v1(scope.value);require(!a.value.epoch(a.value.budget)&&!b.value.available(b.value.budget,1),"closed unlimited must fail closed");require(biglybtc_shared_quota_set_v1(a.value.budget,0,0)==-2,"closed cannot restart");
}
void fractionAndClock(){
 Scope s;Budget a(s,1,1,1);for(int n=1;n<=9;++n){s.clock.now=100000000LL*n;require(biglybtc_shared_quota_set_v1(a.value.budget,1,n+1)==0,"opposite setting");require(!a.value.available(a.value.budget,0),"partial byte must wait");}
 s.clock.now=1000000000;require(take(a,0)==1,"opposite changes must preserve upload fraction");s.clock.now=0;require(!take(a,0),"backward clock cannot mint quota");s.clock.now=1100000000;require(!take(a,0),"clock recovery must not duplicate elapsed time");
 Scope max;Budget huge(max,1,std::numeric_limits<int>::max()-1,1);max.clock.now=3000000000LL;require(take(huge,0)==std::numeric_limits<int>::max(),"large rate saturates safely");max.clock.now=std::numeric_limits<std::int64_t>::max();require(take(huge,0)==std::numeric_limits<int>::max(),"large elapsed safely capped");
}
void identityAndLifetime(){
 Scope first,second;Budget a(first,1,3,0),b(first,2,4,0),foreign(second,1,3,0);require(a.value.budget!=b.value.budget&&a.value.scope!=foreign.value.scope,"tag and scope identities separate");first.clock.now=1000000000;second.clock.now=1000000000;require(take(a,0)==3&&take(b,0)==4&&take(foreign,0)==3,"separate independent budgets");
 biglybtc_shared_quota_v1 rejected;require(biglybtc_shared_quota_acquire_v1(first.value,1,99,0,&rejected)==-3&&!rejected.budget,"conflicting existing limits refuse");for(int bad:{-2,std::numeric_limits<int>::max()})require(biglybtc_shared_quota_acquire_v1(first.value,3,bad,0,&rejected)==-1&&!rejected.budget,"unsupported rates refuse");
 require(biglybtc_shared_quota_acquire_v1(nullptr,1,1,1,&rejected)==-1,"null scope");require(biglybtc_shared_quota_acquire_v1(first.value,0,1,1,&rejected)==-1,"zero tag");
 a.value.retain_scope(a.value.scope);biglybtc_shared_quota_scope_close_v1(first.value);require(!a.value.available(a.value.budget,1)&&foreign.value.available(foreign.value.budget,1)==std::numeric_limits<int>::max(),"closure remains scope-local");a.value.release_scope(a.value.scope);
}
void concurrent(){
 Scope s;Budget a(s,1,1011,0),b(s,1,1011,0);s.clock.now=1000000000;std::atomic_int granted{0};
 auto run=[&](Budget& v){for(int n=0;n<3000;++n)granted+=take(v,0,1);};std::thread x(run,std::ref(a)),y(run,std::ref(b));x.join();y.join();require(granted==1011,"concurrent sessions cannot double-spend common quota");
}
void outlivesRoot(){Clock clock;void* scope=libtorrent::aux::make_shared_quota_scope(Clock::get,&clock);biglybtc_shared_quota_v1 o;require(!biglybtc_shared_quota_acquire_v1(scope,1,1,0,&o),"late descriptor");biglybtc_shared_quota_scope_close_v1(scope);biglybtc_shared_quota_scope_release_v1(scope);require(!o.epoch(o.budget)&&!o.available(o.budget,1),"descriptor must retain closed scope after root release");o.release_budget(o.budget);}
void requestCapacity(){Scope scope;Budget b(scope,1,1,0);void* values[]={b.value.budget};for(int n=0;n<65536;++n)require(b.value.queue_request(b.value.scope,values,1,0,1,1,false)!=0,"bounded request census");require(!b.value.queue_request(b.value.scope,values,1,0,1,1,false)&&!b.value.epoch(b.value.budget),"exhausted request census must revoke actual readiness");}
void passiveEpoch(){Scope scope;Budget b(scope,1,1,0);b.value.enter(b.value.scope);auto future=std::async(std::launch::async,[&]{return b.value.epoch(b.value.budget);});const bool passive=future.wait_for(std::chrono::seconds(1))==std::future_status::ready;b.value.leave(b.value.scope);require(passive&&future.get()!=0,"epoch must not wait on the SDK allocator mutex");}
void longWeightedHistory(){
 Scope scope;constexpr int rate=32*1024*1024,block=1024*1024*1024;
 Budget b(scope,1,rate,0);auto& o=b.value;void* values[]={o.budget};
 std::array<std::uint64_t,2> requests{};std::array<int,2> remaining{};std::array<std::int64_t,2> totals{};
 // Each owner remains backlogged for hundreds of clock quanta. A fulfilled
 // finite request must not be counted as continuously pending demand.
 for(int tick=0;tick<100000;++tick){
  for(int owner=0;owner<2;++owner)if(!remaining[owner]){remaining[owner]=block;requests[owner]=o.queue_request(o.scope,values,1,0,block,owner?3:1,true);require(requests[owner]!=0,"long-history request registration");}
  scope.clock.now+=100000000;
  for(int owner=0;owner<2;++owner){const int n=o.claim_request(o.scope,requests[owner],remaining[owner],false);require(n>=0&&n<=remaining[owner],"long-history bounded claim");totals[owner]+=n;remaining[owner]-=n;if(!remaining[owner])o.cancel_request(o.scope,requests[owner],false);}
 }
 for(int owner=0;owner<2;++owner)if(remaining[owner])o.cancel_request(o.scope,requests[owner],false);
 const auto sum=totals[0]+totals[1];require(sum<=std::int64_t(rate)*10000&&sum>=std::int64_t(rate)*9998,"large service history duplicated or stranded common quota");
 require(totals[0]>std::int64_t(32)*block&&totals[1]>std::int64_t(96)*block,"large weighted history starved one owner");
 const auto difference=totals[0]*3-totals[1];std::cout<<"WEIGHTED_HISTORY owner1="<<totals[0]<<" owner3="<<totals[1]<<" difference="<<difference<<'\n';require(difference<=std::int64_t(8)*block&&difference>=-std::int64_t(8)*block,"fair virtual history failed after repeated 64-bit score normalization");
}
void capacity(){
 Scope scope;std::vector<biglybtc_shared_quota_v1> kept(4096);for(std::size_t n=0;n<kept.size();++n)require(biglybtc_shared_quota_acquire_v1(scope.value,n+1,1,0,&kept[n])==0,"bounded census");biglybtc_shared_quota_v1 extra;require(biglybtc_shared_quota_acquire_v1(scope.value,9999,1,0,&extra)==-4&&!extra.budget,"complete capacity refusal");kept[0].release_budget(kept[0].budget);kept[0]={};require(biglybtc_shared_quota_acquire_v1(scope.value,9999,1,0,&extra)==0,"released cell reusable");extra.release_budget(extra.budget);for(auto& v:kept)if(v.budget)v.release_budget(v.budget);
}
}
int main(){try{tiny();updates();fractionAndClock();identityAndLifetime();concurrent();outlivesRoot();capacity();requestCapacity();passiveEpoch();longWeightedHistory();std::cout<<"PASS_SHARED_QUOTA_CORE "<<checks<<" assertions;actual callback implementation;two owners;long weighted service history;no SDK session/transport qualification\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
