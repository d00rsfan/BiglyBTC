// Private bounded FIND_NODE refresh policy; no source, I/O or grant callback.
#pragma once
#include "dht.hpp"
#include <algorithm>
#include <limits>
namespace biglybtc::services::i2p::dht::detail {
inline constexpr std::int64_t RoutingTickMillis=10000,RoutingRpcMillis=30000,RoutingRefreshMillis=300000;
inline constexpr std::size_t RoutingAccuracy=5,RoutingParallelism=3,RoutingPending=64;
inline bool routingCloser(const Id& a,const Id& b,const Id& target)noexcept{
  for(std::size_t i=0;i<target.size();++i){const auto x=Byte(a[i]^target[i]),y=Byte(b[i]^target[i]);if(x!=y){return x<y;}}return false;
}
class RoutingRefresh final {
public:
  struct Target {Id id{};std::int64_t end=0;};
  bool begin(Target target,std::span<const Contact> seeds,std::int64_t now){
    if(running_||now<0||target.end<=now||target.end-now>RoutingRefreshMillis||seeds.empty()||seeds.size()>MaximumContacts){return false;}
    for(const auto& seed:seeds){if(!validContact(seed)){return false;}}
    nodes_.clear();nodes_.reserve(MaximumContacts);target_=target;last_=now;running_=true;
    for(const auto& seed:seeds){offer(seed);}settle();return true;
  }
  bool running()const noexcept{return running_;}
  const Target& target()const noexcept{return target_;}
  const std::vector<Node>& nodes()const noexcept{return nodes_;}
  std::vector<Node>& nodes()noexcept{return nodes_;}
  void clear()noexcept{nodes_.clear();running_=false;}
  void tick(std::int64_t now)noexcept{
    if(!running_){return;}if(now<last_||now>=target_.end){running_=false;return;}last_=now;
    for(auto& node:nodes_){if(node.end&&now>=node.end&&node.phase!=NodePhase::Replied){node.phase=NodePhase::Failed;}}
    settle();
  }
  std::optional<std::size_t> next(std::int64_t now){
    tick(now);if(!running_){return {};}
    const auto active=std::count_if(nodes_.begin(),nodes_.end(),[](const auto& node){return node.phase==NodePhase::Resolving||node.phase==NodePhase::Queued||node.phase==NodePhase::Sent;});
    if(static_cast<std::size_t>(active)>=RoutingParallelism){return {};}
    const auto order=closest();for(std::size_t i=0;i<std::min(RoutingAccuracy,order.count);++i){if(nodes_[order.values[i]].phase==NodePhase::Candidate){return order.values[i];}}return {};
  }
  void pause()noexcept{
    for(auto& node:nodes_){if(node.phase==NodePhase::Resolving||node.phase==NodePhase::Queued||node.phase==NodePhase::Sent){node.phase=NodePhase::Candidate;node.ticket=0;}}
    // Keep global and already-admitted per-RPC ends. Preemption cannot extend
    // either deadline, mark a contact dead or reuse a transaction identifier.
  }
  bool offer(const Contact& contact){
    if(!validContact(contact)){return false;}if(std::any_of(nodes_.begin(),nodes_.end(),[&](const auto& old){return old.contact.id==contact.id||old.contact.hash==contact.hash;})){return false;}
    if(nodes_.size()==MaximumContacts){return false;}Node node;node.contact=contact;nodes_.push_back(std::move(node));return true;
  }
  std::optional<std::size_t> match(const Message& message,std::uint16_t from)const noexcept{
    for(std::size_t i=0;i<nodes_.size();++i){const auto& node=nodes_[i];if(!node.transaction.empty()&&node.transaction==message.transaction&&node.contact.port==from){return i;}}return {};
  }
  void settle()noexcept{
    if(!running_){return;}
    if(std::any_of(nodes_.begin(),nodes_.end(),[](const auto& node){return node.phase==NodePhase::Resolving||node.phase==NodePhase::Queued||node.phase==NodePhase::Sent;})){return;}
    const auto order=closest();for(std::size_t i=0;i<std::min(RoutingAccuracy,order.count);++i){if(nodes_[order.values[i]].phase!=NodePhase::Replied){return;}}running_=false;
  }
private:
  Target target_;std::vector<Node> nodes_;std::int64_t last_=0;bool running_=false;
  struct Order {std::size_t count=0;std::array<std::size_t,MaximumContacts> values{};};
  Order closest()const noexcept{
    Order order;for(std::size_t i=0;i<nodes_.size();++i){if(nodes_[i].phase!=NodePhase::Failed){order.values[order.count++]=i;}}
    std::sort(order.values.begin(),order.values.begin()+static_cast<std::ptrdiff_t>(order.count),[&](auto a,auto b){return routingCloser(nodes_[a].contact.id,nodes_[b].contact.id,target_.id);});return order;
  }
};
}
