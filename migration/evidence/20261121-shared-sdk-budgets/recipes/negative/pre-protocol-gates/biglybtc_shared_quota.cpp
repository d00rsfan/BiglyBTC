// SPDX-License-Identifier: BSD-3-Clause
#include "libtorrent/aux_/biglybtc_shared_quota.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace {
constexpr int infinity=std::numeric_limits<int>::max();
constexpr std::int64_t second=1000000000,quantum=100000000;
struct Scope;
struct Direction {int rate=0;std::int64_t credit=0,remainder=0,last=0;};
struct Budget {
 std::atomic_uint references{1};Scope* scope;std::uint64_t tag;std::atomic_uint64_t version{1};
 std::array<Direction,2> directions;
 Budget(Scope*,std::uint64_t,int,int);
};
void release_budget(void*) noexcept;
struct Request {
 std::uint64_t id=0,score=0;std::vector<Budget*> budgets;
 int direction=0,remaining=0,priority=1,candidate=0,held=0,granted=0;
 std::int64_t report=0;
 ~Request(){for(auto* b:budgets)release_budget(b);}
};
struct Scope {
 std::atomic_uint references{1};std::recursive_mutex mutex;std::atomic_bool closed{false};bool planned=false,dirty=false;
 libtorrent::aux::shared_quota_clock clock;void* clock_context;
 std::unordered_map<std::uint64_t,Budget*> budgets;
 std::unordered_map<std::uint64_t,std::unique_ptr<Request>> requests;
 std::uint64_t next_request=1;std::array<std::uint64_t,2> cursor{},baseline{};std::int64_t last_plan=0;
 Scope(libtorrent::aux::shared_quota_clock fn,void* context):clock(fn),clock_context(context){}
};
Budget::Budget(Scope* s,std::uint64_t t,int up,int down):scope(s),tag(t){
 s->references.fetch_add(1,std::memory_order_relaxed);const auto now=s->clock(s->clock_context);
 directions[0].rate=up;directions[1].rate=down;for(auto& d:directions)d.last=now;
}
bool rates(int up,int down){return up>=-1&&up<infinity&&down>=-1&&down<infinity;}
void retain_scope(void* raw) noexcept {static_cast<Scope*>(raw)->references.fetch_add(1,std::memory_order_relaxed);}
void release_scope(void* raw) noexcept {auto* s=static_cast<Scope*>(raw);if(s&&s->references.fetch_sub(1,std::memory_order_acq_rel)==1)delete s;}
void enter(void* raw) noexcept {static_cast<Scope*>(raw)->mutex.lock();}
void leave(void* raw) noexcept {static_cast<Scope*>(raw)->mutex.unlock();}
void retain_budget(void* raw) noexcept {static_cast<Budget*>(raw)->references.fetch_add(1,std::memory_order_relaxed);}
void release_budget(void* raw) noexcept {
 auto* b=static_cast<Budget*>(raw);if(!b)return;auto* s=b->scope;bool last=false;
 {std::lock_guard lock(s->mutex);last=b->references.fetch_sub(1,std::memory_order_acq_rel)==1;if(last)s->budgets.erase(b->tag);}
 if(last){delete b;release_scope(s);}
}
std::int64_t ceiling(const Direction& d){return std::min(std::int64_t(infinity),std::int64_t(d.rate)*3);}
void refill(Direction& d,std::int64_t now){
 if(now<=d.last)return;
 const auto elapsed=std::int64_t(std::min(std::uint64_t(now)-std::uint64_t(d.last),std::uint64_t(second*3)));d.last=now;
 if(d.rate<=0){d.remainder=0;return;}const auto numerator=std::int64_t(d.rate)*elapsed+d.remainder;
 const auto add=numerator/second;d.remainder=numerator%second;const auto cap=ceiling(d);
 if(d.credit>=cap-add){d.credit=cap;d.remainder=0;}else d.credit+=add;
}
std::uint64_t epoch(void* raw) noexcept {auto& b=*static_cast<Budget*>(raw);return b.scope->closed.load(std::memory_order_acquire)?0:b.version.load(std::memory_order_acquire);}
int limit(void* raw,int direction) noexcept {auto& b=*static_cast<Budget*>(raw);std::lock_guard lock(b.scope->mutex);return direction<0||direction>1?-1:b.directions[direction].rate;}
int available_at(Direction& d){return d.rate<0?0:d.rate==0?infinity:int(std::max(std::int64_t(0),d.credit));}
int available(void* raw,int direction) noexcept {
 auto& b=*static_cast<Budget*>(raw);std::lock_guard lock(b.scope->mutex);if(b.scope->closed||direction<0||direction>1)return 0;
 auto& d=b.directions[direction];refill(d,b.scope->clock(b.scope->clock_context));return available_at(d);
}
void use(void* raw,int direction,int amount) noexcept {
 auto& b=*static_cast<Budget*>(raw);std::lock_guard lock(b.scope->mutex);if(b.scope->closed||direction<0||direction>1||amount<0)return;
 auto& d=b.directions[direction];if(d.rate<=0)return;if(d.credit<std::numeric_limits<std::int64_t>::min()+amount){b.scope->closed=true;return;}d.credit-=amount;
}
void give_back(void* raw,int direction,int amount) noexcept {
 auto& b=*static_cast<Budget*>(raw);std::lock_guard lock(b.scope->mutex);if(b.scope->closed||direction<0||direction>1||amount<0)return;
 auto& d=b.directions[direction];if(d.rate>0)d.credit=std::min(ceiling(d),d.credit+amount);
}
void unhold(Request& r){if(!r.held)return;for(auto* b:r.budgets)give_back(b,r.direction,r.held);const auto undo=(std::uint64_t(r.held)<<32)/std::uint64_t(r.priority);r.score-=std::min(r.score,undo);r.held=0;}
int eligible(const Request& r,std::int64_t now){
 if(r.candidate<=0||r.remaining<=r.held||r.held||now<r.report||std::uint64_t(now)-std::uint64_t(r.report)>std::uint64_t(second*3))return 0;
 int n=std::min(r.candidate,r.remaining);for(auto* b:r.budgets)n=std::min(n,available_at(b->directions[r.direction]));return n;
}
void score(Request& r,int amount){const auto delta=(std::uint64_t(amount)<<32)/std::uint64_t(r.priority);if(r.score>std::numeric_limits<std::uint64_t>::max()-delta){auto& s=*r.budgets[0]->scope;auto minimum=r.score;for(const auto& [id,q]:s.requests){(void)id;if(q->candidate>0)minimum=std::min(minimum,q->score);}for(auto& [id,q]:s.requests){(void)id;q->score-=std::min(q->score,minimum);}for(auto& baseline:s.baseline)baseline-=std::min(baseline,minimum);}r.score=std::min(r.score,std::numeric_limits<std::uint64_t>::max()-delta)+delta;}
void reserve(Request& r,int amount){if(amount<=0)return;for(auto* b:r.budgets)use(b,r.direction,amount);r.held+=amount;score(r,amount);r.budgets[0]->scope->cursor[r.direction]=r.id;}
void plan(Scope& s,std::int64_t now){
 if(s.planned&&!s.dirty&&(now<s.last_plan||std::uint64_t(now)-std::uint64_t(s.last_plan)<std::uint64_t(quantum)))return;
 s.planned=true;s.dirty=false;s.last_plan=now;
 // Once per common clock quantum:all SDK managers contribute demand. This
 // prevents both double refill and incompatible per-limiter fair winners.
 for(auto& [id,b]:s.budgets){(void)id;for(auto& d:b->directions)refill(d,now);}
 std::array<std::unordered_map<Budget*,std::int64_t>,2> weights;
 std::vector<Request*> active;
 for(auto& [id,q]:s.requests){(void)id;if(q->held&&(now<q->report||std::uint64_t(now)-std::uint64_t(q->report)>std::uint64_t(second*3)))unhold(*q);if(!eligible(*q,now))continue;active.push_back(q.get());for(auto* b:q->budgets)if(b->directions[q->direction].rate>0)weights[q->direction][b]+=q->priority;}
 std::array<std::unordered_map<Budget*,int>,2> distribution;
 for(int direction=0;direction<2;++direction)for(const auto& [b,w]:weights[direction]){(void)w;distribution[direction][b]=available_at(b->directions[direction]);}
 for(auto* r:active){int n=std::min(r->candidate,r->remaining);for(auto* b:r->budgets){auto& d=b->directions[r->direction];if(d.rate>0)n=std::min(n,int(std::int64_t(distribution[r->direction][b])*r->priority/weights[r->direction][b]));}reserve(*r,n);}
 // Largest-rounding/blocked-resource leftovers are assigned to complete
 // requests in one fair ordered pass,not independent channel winners.
 std::sort(active.begin(),active.end(),[&](auto* a,auto* b){if(a->direction!=b->direction)return a->direction<b->direction;if(a->score!=b->score)return a->score<b->score;auto cursor=s.cursor[a->direction];const bool aa=a->id>cursor,bb=b->id>cursor;if(aa!=bb)return aa;return a->id<b->id;});
 for(auto* r:active){int n=std::min(r->candidate,r->remaining)-r->held;for(auto* b:r->budgets)n=std::min(n,available_at(b->directions[r->direction]));reserve(*r,n);}
 for(int direction=0;direction<2;++direction){auto minimum=std::numeric_limits<std::uint64_t>::max();for(auto* r:active)if(r->direction==direction)minimum=std::min(minimum,r->score);if(minimum!=std::numeric_limits<std::uint64_t>::max())s.baseline[direction]=minimum;}
}
std::uint64_t queue_request(void* raw,void* const* values,int count,int direction,int bytes,int priority,bool local_ready) noexcept {
 if(!raw||!values||count<=0||count>4096||direction<0||direction>1||bytes<=0||priority<=0)return 0;
 auto& s=*static_cast<Scope*>(raw);std::lock_guard lock(s.mutex);
 try{if(s.closed)return 0;if(s.requests.size()==65536||s.next_request==std::numeric_limits<std::uint64_t>::max()){s.closed=true;return 0;}
  auto r=std::make_unique<Request>();r->id=s.next_request++;r->direction=direction;r->remaining=bytes;r->priority=priority;r->candidate=local_ready?bytes:0;r->report=s.clock(s.clock_context);
  std::vector<Budget*> selected;selected.reserve(count);for(int n=0;n<count;++n){auto* b=static_cast<Budget*>(values[n]);if(!b||b->scope!=&s)return 0;selected.push_back(b);}
  std::sort(selected.begin(),selected.end(),std::less<Budget*>{});selected.erase(std::unique(selected.begin(),selected.end()),selected.end());
  r->budgets.reserve(selected.size());for(auto* b:selected){retain_budget(b);r->budgets.push_back(b);}
  r->score=s.baseline[direction];
  const auto id=r->id;if(s.requests.empty())s.dirty=true;s.requests.emplace(id,std::move(r));return id;
 }catch(...){s.closed=true;return 0;}
}
int claim_request(void* raw,std::uint64_t id,int wanted,bool full) noexcept {
 if(!raw||!id||wanted<0)return 0;
 auto& s=*static_cast<Scope*>(raw);std::lock_guard lock(s.mutex);
 try{if(s.closed)return 0;auto found=s.requests.find(id);if(found==s.requests.end())return 0;auto& r=*found->second;r.report=s.clock(s.clock_context);r.candidate=std::min(wanted,r.remaining);if(!wanted){unhold(r);return 0;}
  bool unlimited=true;for(auto* b:r.budgets)unlimited=unlimited&&b->directions[r.direction].rate==0;
  if(unlimited){unhold(r);const int n=std::min(wanted,r.remaining);r.remaining-=n;r.granted+=n;return n;}
  if(full){
   if(wanted>r.remaining)return 0;
   const int missing=std::max(0,wanted-r.held);bool slack=true;
   // Match the SDK's ordinary fast path: a complete small request may use
   // surplus while leaving at least one second of unreserved quota for other
   // queued owners. Otherwise byte-at-a-time encrypted reads wait a full SDK
   // tick even when the common bucket is nearly full.
   for(auto* b:r.budgets){auto& d=b->directions[r.direction];refill(d,r.report);slack=slack&&d.rate>=0&&(d.rate==0||std::int64_t(available_at(d))-missing>=d.rate);}
   if(missing&&!slack)return 0;
   reserve(r,missing);
  }else plan(s,r.report);
  if(full&&r.held<wanted){return 0;}const int n=std::min(wanted,r.held);const int rest=r.held-n;r.held=rest;unhold(r);r.held=0;r.remaining-=n;r.granted+=n;return n;
 }catch(...){s.closed=true;return 0;}
}
void cancel_request(void* raw,std::uint64_t id,bool refund) noexcept {if(!raw||!id)return;auto& s=*static_cast<Scope*>(raw);std::lock_guard lock(s.mutex);auto found=s.requests.find(id);if(found==s.requests.end())return;auto& r=*found->second;unhold(r);if(refund)for(auto* b:r.budgets)give_back(b,r.direction,r.granted);s.requests.erase(found);}
std::int64_t monotone(void*) noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
namespace libtorrent::aux {
void* make_shared_quota_scope(shared_quota_clock clock,void* context){if(!clock)return nullptr;return new Scope(clock,context);}
// The admitted runtime cohort uses one SDK image for all session roles. A
// second allocator/runtime namespace has a separate lifetime and cannot borrow
// this image's budget. Reject it before invoking any foreign callback.
bool owns_shared_quota(biglybtc_shared_quota_v1 const& o) noexcept {
 return o.retain_scope==retain_scope&&o.release_scope==release_scope
  &&o.enter==enter&&o.leave==leave&&o.retain_budget==retain_budget
  &&o.release_budget==release_budget&&o.epoch==epoch&&o.limit==limit
  &&o.available==available&&o.use==use&&o.give_back==give_back
  &&o.queue_request==queue_request&&o.claim_request==claim_request
  &&o.cancel_request==cancel_request;
}
}
extern "C" std::uint64_t biglybtc_shared_quota_features_v1() noexcept {return TORRENT_BIGLYBTC_SHARED_QUOTA_ABI;}
extern "C" void* biglybtc_shared_quota_scope_create_v1() noexcept {try{return libtorrent::aux::make_shared_quota_scope(monotone,nullptr);}catch(...){return nullptr;}}
extern "C" void biglybtc_shared_quota_scope_close_v1(void* raw) noexcept {if(!raw)return;auto& s=*static_cast<Scope*>(raw);std::lock_guard lock(s.mutex);s.closed=true;s.requests.clear();}
extern "C" void biglybtc_shared_quota_scope_release_v1(void* raw) noexcept {release_scope(raw);}
extern "C" int biglybtc_shared_quota_acquire_v1(void* raw,std::uint64_t tag,int up,int down,biglybtc_shared_quota_v1* out) noexcept {
 if(!out)return -1;
 *out={};if(!raw||!tag||!rates(up,down))return -1;
 try{auto& s=*static_cast<Scope*>(raw);std::lock_guard lock(s.mutex);if(s.closed)return -2;Budget* b=nullptr;
  if(auto found=s.budgets.find(tag);found!=s.budgets.end()){b=found->second;if(b->directions[0].rate!=up||b->directions[1].rate!=down)return -3;retain_budget(b);}
  else{if(s.budgets.size()==4096)return -4;b=new Budget(&s,tag,up,down);try{s.budgets.emplace(tag,b);}catch(...){delete b;release_scope(&s);throw;}}
  out->scope=&s;out->budget=b;out->retain_scope=retain_scope;out->release_scope=release_scope;out->enter=enter;out->leave=leave;out->retain_budget=retain_budget;out->release_budget=release_budget;out->epoch=epoch;out->limit=limit;out->available=available;out->use=use;out->give_back=give_back;out->queue_request=queue_request;out->claim_request=claim_request;out->cancel_request=cancel_request;return 0;
 }catch(...){return -5;}
}
extern "C" int biglybtc_shared_quota_set_v1(void* raw,int up,int down) noexcept {
 if(!raw||!rates(up,down))return -1;
 auto& b=*static_cast<Budget*>(raw);std::lock_guard lock(b.scope->mutex);if(b.scope->closed)return -2;
 if(b.directions[0].rate==up&&b.directions[1].rate==down)return 0;
 if(b.version==std::numeric_limits<std::uint64_t>::max()){b.scope->closed=true;return -2;}
 for(auto& [id,q]:b.scope->requests){(void)id;if(std::find(q->budgets.begin(),q->budgets.end(),&b)!=q->budgets.end())unhold(*q);}
 const int next[]={up,down};for(int i=0;i<2;++i){auto& d=b.directions[i];refill(d,b.scope->clock(b.scope->clock_context));if(d.rate==next[i])continue;d.rate=next[i];d.remainder=0;if(d.rate>0)d.credit=std::min(d.credit,ceiling(d));else d.credit=0;}
 ++b.version;b.scope->dirty=true;return 0;
}
