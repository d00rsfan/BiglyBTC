// SPDX-License-Identifier: GPL-2.0-or-later
#include "lookup.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <new>
namespace biglybtc::services::azureus_control {
namespace {
bool validContact(const Contact& c)noexcept{
  if((c.address.length!=4&&c.address.length!=16)||!c.address.port||c.protocol<51||c.protocol>55){return false;}
  if(c.address.length==4){for(unsigned i=4;i<16;++i){if(c.address.bytes[i]){return false;}}}return true;
}
bool loopback(const Contact& c)noexcept{
  if(c.address.length==4){return c.address.bytes[0]==127;}
  if(c.address.length!=16||c.address.bytes[15]!=1){return false;}
  for(unsigned i=0;i<15;++i){if(c.address.bytes[i]){return false;}}return true;
}
bool validOptions(const LookupOptions& o)noexcept{
  return o.parallelism>0&&o.parallelism<=LookupController::MaximumParallelism&&o.accuracy>0&&o.accuracy<=20&&o.maximumContacts>=o.parallelism&&o.maximumContacts<=LookupController::MaximumContacts&&o.maximumValues>0&&o.maximumValues<=LookupController::MaximumValues&&o.timeoutMillis>0&&o.timeoutMillis<=300000&&(o.flags==0||o.flags==8);
}
int distance(const NodeId& a,const NodeId& b,const NodeId& target)noexcept{
  for(std::size_t i=0;i<a.size();++i){const auto x=a[i]^target[i],y=b[i]^target[i];if(x<y){return -1;}if(x>y){return 1;}}return 0;
}
bool active(LookupNodeState state)noexcept{return state==LookupNodeState::Reserved||state==LookupNodeState::Sent||state==LookupNodeState::Partial;}
bool rpc(LookupNodeState state)noexcept{return state==LookupNodeState::Sent||state==LookupNodeState::Partial;}
bool same(const Contact& a,const Contact& b)noexcept{return a.id==b.id||a.address==b.address;}
}
bool validLookupSnapshot(const LookupSnapshot& s)noexcept{
  if(!s.generation||!s.lookup||!s.revision||!validOptions(s.options)||s.startedMillis<0||s.updatedMillis<s.startedMillis||s.nodes.size()>s.options.maximumContacts||s.results.size()>s.options.maximumValues||s.valueReplies>s.nodes.size()||s.state==LookupState::Idle){return false;}
  if(s.state!=LookupState::Running&&s.state!=LookupState::Stopping&&!lookupTerminal(s.state)){return false;}
  if(s.stop){switch(*s.stop){
    case LookupStop::Exhausted:case LookupStop::Closest:case LookupStop::ValueLimit:case LookupStop::TwoValueReplies:case LookupStop::Deadline:case LookupStop::Cancelled:case LookupStop::Closed:case LookupStop::Capacity:case LookupStop::Unsupported:case LookupStop::Clock:case LookupStop::Memory:case LookupStop::Overflow:break;
    default:return false;
  }}
  if((s.state==LookupState::Running)==s.stop.has_value()||(s.state==LookupState::Stopping&&s.active==0)||(lookupTerminal(s.state)&&s.active)){return false;}
  if(s.stop&&s.active==0){
    const auto expected=*s.stop==LookupStop::Cancelled?LookupState::Cancelled:*s.stop==LookupStop::Closed?LookupState::Closed:*s.stop==LookupStop::Deadline?LookupState::TimedOut:(*s.stop==LookupStop::Capacity||*s.stop==LookupStop::Unsupported||*s.stop==LookupStop::Clock||*s.stop==LookupStop::Memory||*s.stop==LookupStop::Overflow)?LookupState::Failed:LookupState::Complete;
    if(s.state!=expected){return false;}
  }
  std::size_t count=0,values=0;
  for(std::size_t i=0;i<s.nodes.size();++i){const auto& n=s.nodes[i];
    if(n.id!=i+1||n.parent>=n.id||!validContact(n.contact)||n.contact.id==s.options.localId||n.discoveredMillis<s.startedMillis||n.discoveredMillis>s.updatedMillis||n.slices>LookupController::MaximumSlices||n.values>s.results.size()){return false;}
    if(n.owned&&!loopback(n.contact)){return false;}
    if(n.parent){if(n.depth!=s.nodes[n.parent-1].depth+1){return false;}}else if(n.depth){return false;}
    for(std::size_t j=0;j<i;++j){if(same(n.contact,s.nodes[j].contact)){return false;}}
    if(n.intentMillis&&(*n.intentMillis<n.discoveredMillis||*n.intentMillis>s.updatedMillis)){return false;}
    if(n.sentMillis&&(!n.intentMillis||*n.sentMillis<*n.intentMillis||*n.sentMillis>s.updatedMillis||!n.ticket)){return false;}
    if(n.finishedMillis&&(*n.finishedMillis<(n.sentMillis?*n.sentMillis:n.discoveredMillis)||*n.finishedMillis>s.updatedMillis)){return false;}
    if(n.cancellationRequested&&(!s.stop||!rpc(n.state))){return false;}
    if(s.stop&&rpc(n.state)&&!n.cancellationRequested){return false;}
    if(n.contactReply&&n.state!=LookupNodeState::Replied){return false;}
    if((n.state==LookupNodeState::Replied||n.state==LookupNodeState::Partial)&&n.slices==0){return false;}
    switch(n.state){
      case LookupNodeState::Candidate:if(!n.owned||n.intentMillis||n.sentMillis||n.finishedMillis||s.stop){return false;}break;
      case LookupNodeState::Reserved:if(!n.owned||!n.intentMillis||n.sentMillis||n.finishedMillis||s.stop){return false;}break;
      case LookupNodeState::Sent:case LookupNodeState::Partial:if(!n.owned||!n.sentMillis||n.finishedMillis){return false;}break;
      case LookupNodeState::Rejected:if(n.owned||!n.finishedMillis){return false;}break;
      case LookupNodeState::Skipped:if(!n.finishedMillis){return false;}break;
      case LookupNodeState::Replied:case LookupNodeState::Timeout:case LookupNodeState::Cancelled:case LookupNodeState::Closed:if(!n.finishedMillis||!n.sentMillis||!n.ticket){return false;}break;
      case LookupNodeState::Failed:if(!n.finishedMillis){return false;}break;
      default:return false;
    }
    const auto actual=static_cast<std::size_t>(std::count_if(s.results.begin(),s.results.end(),[&](const auto& value){return value.node==n.id;}));
    if(actual!=n.values){return false;}
    count+=active(n.state);values+=n.values;
  }
  if(count!=s.active||count>s.options.parallelism||values!=s.results.size()){return false;}
  for(std::size_t i=0;i<s.results.size();++i){const auto& value=s.results[i];if(!value.node||value.node>s.nodes.size()||!s.nodes[value.node-1].ticket||value.value.bytes.size()>azureus_values::maximumValueBytes||!validContact(value.value.originator)){return false;}
    for(std::size_t j=0;j<i;++j){if(value.value.originator.id==s.results[j].value.originator.id&&value.value.bytes==s.results[j].value.bytes){return false;}}
  }
  return true;
}
LookupNode* LookupController::node(std::uint64_t id)noexcept{return id&&id<=data_.nodes.size()?&data_.nodes[static_cast<std::size_t>(id-1)]:nullptr;}
void LookupController::bump()noexcept{
  if(data_.revision!=std::numeric_limits<std::uint64_t>::max()){++data_.revision;}
  else{stop(LookupStop::Overflow,data_.updatedMillis);}
}
void LookupController::stop(LookupStop reason,std::int64_t now)noexcept{
  if(lookupTerminal(data_.state)){return;}
  if(!data_.stop||reason==LookupStop::Closed||reason==LookupStop::Cancelled){data_.stop=reason;}
  for(auto& value:data_.nodes){
    if(value.state==LookupNodeState::Candidate||value.state==LookupNodeState::Reserved){if(value.state==LookupNodeState::Reserved){--data_.active;}value.state=LookupNodeState::Skipped;value.finishedMillis=now;}
    else if(rpc(value.state)){value.cancellationRequested=true;}
  }
  data_.state=LookupState::Stopping;settle(now);
}
void LookupController::settle(std::int64_t now)noexcept{
  if(data_.stop){
    if(data_.active){data_.state=LookupState::Stopping;return;}
    switch(*data_.stop){
      case LookupStop::Cancelled:data_.state=LookupState::Cancelled;break;
      case LookupStop::Closed:data_.state=LookupState::Closed;break;
      case LookupStop::Deadline:data_.state=LookupState::TimedOut;break;
      case LookupStop::Capacity:case LookupStop::Unsupported:case LookupStop::Clock:case LookupStop::Memory:case LookupStop::Overflow:data_.state=LookupState::Failed;break;
      default:data_.state=LookupState::Complete;break;
    }
  }else if(!data_.active&&std::none_of(data_.nodes.begin(),data_.nodes.end(),[](const auto& value){return value.state==LookupNodeState::Candidate;})){stop(LookupStop::Exhausted,now);}
}
std::expected<void,LookupError> LookupController::clock(std::int64_t now){
  if(now<0||now<data_.updatedMillis){stop(LookupStop::Clock,data_.updatedMillis);bump();return std::unexpected(LookupError::Clock);}
  data_.updatedMillis=now;
  if(now>=deadline_&&data_.state==LookupState::Running){stop(LookupStop::Deadline,now);}
  return {};
}
std::expected<void,LookupError> LookupController::offer(std::span<const AdmittedContact> contacts,std::uint64_t parent,std::int64_t now){
  if(contacts.size()>MaximumContacts){return std::unexpected(LookupError::Limit);}
  std::array<LookupNode,MaximumContacts> additions{};std::size_t count=0;std::uint64_t duplicates=0,rejected=0;
  for(const auto& offered:contacts){const auto& contact=offered.contact;
    if(!validContact(contact)||(offered.owned&&!loopback(contact))){return std::unexpected(LookupError::Invalid);}
    if(contact.id==data_.options.localId||std::any_of(data_.nodes.begin(),data_.nodes.end(),[&](const auto& old){return same(contact,old.contact);})||std::any_of(additions.begin(),additions.begin()+count,[&](const auto& old){return same(contact,old.contact);})){++duplicates;continue;}
    if(data_.nodes.size()+count==data_.options.maximumContacts){return std::unexpected(LookupError::Limit);}
    auto& value=additions[count++];value.id=data_.nodes.size()+count;value.parent=parent;value.contact=contact;value.owned=offered.owned;value.discoveredMillis=now;
    value.depth=parent?data_.nodes[static_cast<std::size_t>(parent-1)].depth+1:0;
    if(!offered.owned){value.state=LookupNodeState::Rejected;value.finishedMillis=now;++rejected;}
  }
  if(duplicates>std::numeric_limits<std::uint64_t>::max()-data_.duplicateContacts||rejected>std::numeric_limits<std::uint64_t>::max()-data_.rejectedContacts){return std::unexpected(LookupError::Overflow);}
  data_.nodes.insert(data_.nodes.end(),additions.begin(),additions.begin()+count);data_.duplicateContacts+=duplicates;data_.rejectedContacts+=rejected;return {};
}
std::expected<void,LookupError> LookupController::start(std::uint64_t generation,std::uint64_t lookup,const LookupOptions& options,std::span<const AdmittedContact> seeds,std::int64_t now){try{
  if(data_.state!=LookupState::Idle){return std::unexpected(LookupError::State);}
  if(!generation||!lookup||now<0||!validOptions(options)){return std::unexpected(LookupError::Invalid);}
  if(seeds.size()>MaximumSeeds){return std::unexpected(LookupError::Limit);}
  if(now>std::numeric_limits<std::int64_t>::max()-options.timeoutMillis){return std::unexpected(LookupError::Overflow);}
  LookupController staged;staged.data_.generation=generation;staged.data_.lookup=lookup;staged.data_.options=options;staged.data_.state=LookupState::Running;staged.data_.startedMillis=now;staged.data_.updatedMillis=now;staged.data_.revision=1;staged.deadline_=now+options.timeoutMillis;
  staged.data_.nodes.reserve(options.maximumContacts);staged.data_.results.reserve(options.maximumValues);
  const auto result=staged.offer(seeds,0,now);if(!result){return result;}staged.settle(now);*this=std::move(staged);return {};
 }catch(const std::bad_alloc&){return std::unexpected(LookupError::Memory);}}
std::expected<std::vector<QueryIntent>,LookupError> LookupController::takeIntents(std::int64_t now){try{
  if(data_.state==LookupState::Idle){return std::unexpected(LookupError::State);}if(lookupTerminal(data_.state)){return std::vector<QueryIntent>{};}
  if(const auto result=clock(now);!result){return std::unexpected(result.error());}
  std::vector<QueryIntent> result;result.reserve(data_.options.parallelism);
  while(data_.state==LookupState::Running&&data_.active<data_.options.parallelism){
    auto closest=data_.nodes.end();for(auto i=data_.nodes.begin();i!=data_.nodes.end();++i){if(i->state==LookupNodeState::Candidate&&(closest==data_.nodes.end()||distance(i->contact.id,closest->contact.id,data_.options.target)<0)){closest=i;}}
    if(closest==data_.nodes.end()){break;}
    std::array<const LookupNode*,MaximumContacts> good{};std::size_t count=0;
    for(const auto& value:data_.nodes){if(value.state==LookupNodeState::Replied&&value.contactReply){good[count++]=&value;}}
    if(count>=data_.options.accuracy){std::sort(good.begin(),good.begin()+count,[&](const auto* a,const auto* b){return distance(a->contact.id,b->contact.id,data_.options.target)<0;});
      if(distance(good[data_.options.accuracy-1]->contact.id,closest->contact.id,data_.options.target)<=0){if(result.empty()){stop(LookupStop::Closest,now);}break;}
    }
    closest->state=LookupNodeState::Reserved;closest->intentMillis=now;++data_.active;
    result.push_back({closest->id,closest->contact,data_.options.target,data_.options.flags,static_cast<std::uint8_t>(data_.options.maximumValues-data_.results.size())});
  }
  settle(now);bump();return result;
 }catch(const std::bad_alloc&){stop(LookupStop::Memory,data_.updatedMillis);bump();return std::unexpected(LookupError::Memory);}}
std::expected<void,LookupError> LookupController::sent(std::uint64_t id,std::uint64_t ticket,std::int64_t now){
  auto* value=node(id);if(!value||value->state!=LookupNodeState::Reserved||data_.state!=LookupState::Running){return std::unexpected(LookupError::Stale);}
  if(!ticket||std::any_of(data_.nodes.begin(),data_.nodes.end(),[&](const auto& old){return old.ticket==ticket;})){return std::unexpected(LookupError::Invalid);}
  if(now<0||now<data_.updatedMillis){return clock(now);}
  // Actual submission already happened. Retain its ticket even if delivery
  // crossed the global deadline, so cancellation cannot lose the live RPC.
  value->state=LookupNodeState::Sent;value->ticket=ticket;value->sentMillis=now;
  if(const auto result=clock(now);!result){return result;}bump();return {};
}
std::expected<void,LookupError> LookupController::contacts(std::uint64_t id,std::span<const AdmittedContact> contacts,bool continuation,std::int64_t now){
  auto* value=node(id);if(!value||!rpc(value->state)){return std::unexpected(LookupError::Stale);}
  if(const auto result=clock(now);!result){return result;}
  const bool sliceLimit=value->slices==MaximumSlices;if(!sliceLimit){++value->slices;}
  value->state=continuation?LookupNodeState::Partial:LookupNodeState::Replied;
  if(!continuation){value->finishedMillis=now;value->cancellationRequested=false;--data_.active;value->contactReply=true;}
  if(sliceLimit){stop(LookupStop::Capacity,now);bump();return std::unexpected(LookupError::Limit);}
  if(data_.stop){settle(now);bump();return {};}
  if(continuation){stop(LookupStop::Unsupported,now);bump();return std::unexpected(LookupError::Unsupported);}
  const auto result=offer(contacts,id,now);
  if(!result){stop(result.error()==LookupError::Overflow?LookupStop::Overflow:LookupStop::Capacity,now);bump();return result;}
  settle(now);bump();return {};
}
std::expected<void,LookupError> LookupController::values(std::uint64_t id,std::span<const Value> values,bool continuation,std::int8_t diversification,std::int64_t now){try{
  auto* value=node(id);if(!value||!rpc(value->state)){return std::unexpected(LookupError::Stale);}
  if(const auto result=clock(now);!result){return result;}
  const bool sliceLimit=value->slices==MaximumSlices;if(!sliceLimit){++value->slices;}
  value->state=continuation?LookupNodeState::Partial:LookupNodeState::Replied;
  if(!continuation){value->finishedMillis=now;value->cancellationRequested=false;--data_.active;}
  if(sliceLimit){stop(LookupStop::Capacity,now);bump();return std::unexpected(LookupError::Limit);}
  if(data_.stop){settle(now);bump();return {};}
  if(values.size()>MaximumValues){stop(LookupStop::Capacity,now);bump();return std::unexpected(LookupError::Limit);}
  std::vector<LookupValue> additions;additions.reserve(values.size());std::uint64_t duplicates=0;
  for(const auto& found:values){
    if(found.bytes.size()>azureus_values::maximumValueBytes||!validContact(found.originator)){stop(LookupStop::Capacity,now);bump();return std::unexpected(LookupError::Invalid);}
    const auto duplicate=[&](const auto& old){return old.value.originator.id==found.originator.id&&old.value.bytes==found.bytes;};
    if(std::any_of(data_.results.begin(),data_.results.end(),duplicate)||std::any_of(additions.begin(),additions.end(),duplicate)){++duplicates;continue;}
    if(data_.results.size()+additions.size()==data_.options.maximumValues){stop(LookupStop::Capacity,now);bump();return std::unexpected(LookupError::Limit);}
    additions.push_back({id,found});
  }
  if(duplicates>std::numeric_limits<std::uint64_t>::max()-data_.duplicateValues){stop(LookupStop::Overflow,now);bump();return std::unexpected(LookupError::Overflow);}
  value->values+=additions.size();for(auto& addition:additions){data_.results.push_back(std::move(addition));}data_.duplicateValues+=duplicates;
  if(!continuation){++data_.valueReplies;}
  if(diversification!=1&&data_.options.flags!=8){stop(LookupStop::Unsupported,now);}
  else if(data_.results.size()>=data_.options.maximumValues){stop(LookupStop::ValueLimit,now);}
  else if(data_.valueReplies>=2&&data_.options.flags!=8){stop(LookupStop::TwoValueReplies,now);}
  settle(now);bump();return {};
 }catch(const std::bad_alloc&){stop(LookupStop::Memory,data_.updatedMillis);bump();return std::unexpected(LookupError::Memory);}}
std::expected<void,LookupError> LookupController::finish(std::uint64_t id,LookupNodeState state,std::int64_t now){
  auto* value=node(id);if(!value||!active(value->state)){return std::unexpected(LookupError::Stale);}
  if(state!=LookupNodeState::Timeout&&state!=LookupNodeState::Cancelled&&state!=LookupNodeState::Closed&&state!=LookupNodeState::Failed&&state!=LookupNodeState::Rejected){return std::unexpected(LookupError::Invalid);}
  if(value->state==LookupNodeState::Reserved&&state!=LookupNodeState::Failed&&state!=LookupNodeState::Rejected){return std::unexpected(LookupError::Invalid);}
  if(state==LookupNodeState::Rejected&&value->state!=LookupNodeState::Reserved){return std::unexpected(LookupError::Invalid);}
  if(const auto result=clock(now);!result){return result;}
  // Deadline may already have skipped a not-yet-sent reserved intent.
  if(!active(value->state)){bump();return {};}
  value->state=state==LookupNodeState::Rejected?LookupNodeState::Skipped:state;value->finishedMillis=now;value->cancellationRequested=false;--data_.active;settle(now);bump();return {};
}
std::expected<void,LookupError> LookupController::cancel(std::int64_t now){
  if(data_.state==LookupState::Idle){return std::unexpected(LookupError::State);}if(lookupTerminal(data_.state)){return {};}
  if(const auto result=clock(now);!result){return result;}stop(LookupStop::Cancelled,now);bump();return {};
}
std::expected<void,LookupError> LookupController::close(std::int64_t now){
  if(data_.state==LookupState::Idle){return std::unexpected(LookupError::State);}if(lookupTerminal(data_.state)){return {};}
  if(const auto result=clock(now);!result){return result;}stop(LookupStop::Closed,now);bump();return {};
}
std::expected<std::vector<CancelIntent>,LookupError> LookupController::cancellationIntents()const{try{
  std::vector<CancelIntent> result;result.reserve(data_.active);for(const auto& value:data_.nodes){if(value.cancellationRequested&&rpc(value.state)){result.push_back({value.id,value.ticket});}}return result;
 }catch(const std::bad_alloc&){return std::unexpected(LookupError::Memory);}}
std::expected<LookupSnapshot,LookupError> LookupController::snapshot()const{try{return data_;}catch(const std::bad_alloc&){return std::unexpected(LookupError::Memory);}}
} // namespace biglybtc::services::azureus_control
