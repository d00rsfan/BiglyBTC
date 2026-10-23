// SPDX-License-Identifier: GPL-2.0-or-later
#include "router.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <thread>

namespace biglybtc::services::azureus_router {
namespace {
constexpr std::uint16_t None=std::numeric_limits<std::uint16_t>::max();
bool bit(const NodeId& id,std::uint16_t depth)noexcept{return (id[depth/8]&(0x80U>>(depth%8)))!=0;}
void increment(std::uint64_t& value)noexcept{if(value!=std::numeric_limits<std::uint64_t>::max()){++value;}}
}
struct RouterOwner::Impl {
  struct Contact {bool used=false;ContactInput input;RouterContact state;std::int64_t firstFailOrAlive=0,lastAdded=0;};
  struct Node {
    NodeId prefix{};std::uint16_t depth=0,left=None,right=None;bool containsLocal=false;
    std::uint16_t count=0,replacementCount=0;
    std::array<std::uint16_t,32> bucket{},replacements{};
    bool leaf()const noexcept{return left==None;}
  };
  const std::thread::id owner=std::this_thread::get_id();const NodeId local;const RouterConfig config;const std::uint64_t generation;
  std::uint64_t revision=1,capacityDrops=0,lostIntents=0,suppressedKnown=0;
  std::int64_t now=0,recentStart=0;std::uint32_t consecutiveDead=0;
  bool sleeping=false,suspended=false;std::uint16_t nodeCount=1,smallest=None;
  std::array<Node,MaximumRouterNodes> nodes{};std::array<Contact,MaximumRouterContacts> contacts{};
  std::array<NodeId,MaximumRouterContacts> recent{};std::size_t recentCount=0;RouterIntents intents;
  Impl(NodeId id,RouterConfig policy,std::uint64_t gen,std::int64_t time):local(id),config(policy),generation(gen),now(time),recentStart(time){
    nodes[0].containsLocal=true;nodes[0].count=1;nodes[0].bucket[0]=0;
    auto& contact=contacts[0];contact.used=true;contact.input.id=id;contact.state.id=id;contact.state.local=true;contact.state.bucket=true;contact.state.hasBeenAlive=true;contact.firstFailOrAlive=time;
  }
  bool onOwner()const noexcept{return owner==std::this_thread::get_id();}
  std::expected<void,RouterError> begin(std::int64_t time)noexcept{
    if(!onOwner()){return std::unexpected(RouterError::Thread);}if(time<0||time<now){return std::unexpected(RouterError::Clock);}
    if(revision==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(RouterError::Exhausted);}now=time;++revision;return {};
  }
  void enqueue(RouterIntentKind kind,const NodeId& id)noexcept{
    if(suspended){return;}for(std::size_t i=0;i<intents.count;++i){if(intents.values[i].kind==kind&&intents.values[i].id==id){return;}}
    if(intents.count==intents.values.size()){increment(lostIntents);return;}intents.values[intents.count++]={kind,id};
  }
  void added(std::uint16_t index)noexcept{auto& contact=contacts[index];if(now-contact.lastAdded>10000){contact.lastAdded=now;enqueue(RouterIntentKind::NodeAdded,contact.state.id);}}
  void pingOldest(Node& node)noexcept{for(std::uint16_t i=0;i<node.count;++i){auto& contact=contacts[node.bucket[i]];
    if(!contact.state.local&&!contact.state.pingOutstanding){contact.state.pingOutstanding=true;enqueue(RouterIntentKind::Ping,contact.state.id);break;}}}
  std::uint16_t leaf(const NodeId& id,bool* inSmallest=nullptr)const noexcept{
    auto index=std::uint16_t{0};bool inside=false;
    for(;;){if(index==smallest){inside=true;}const auto& node=nodes[index];if(node.leaf()){if(inSmallest){*inSmallest=inside;}return index;}index=bit(id,node.depth)?node.left:node.right;}
  }
  std::uint16_t findContact(const NodeId& id)const noexcept{const auto& node=nodes[leaf(id)];
    for(std::uint16_t i=0;i<node.count;++i){if(contacts[node.bucket[i]].state.id==id){return node.bucket[i];}}
    for(std::uint16_t i=0;i<node.replacementCount;++i){if(contacts[node.replacements[i]].state.id==id){return node.replacements[i];}}return None;}
  std::uint16_t freeContact()const noexcept{for(std::size_t i=1;i<contacts.size();++i){if(!contacts[i].used){return static_cast<std::uint16_t>(i);}}return None;}
  std::uint16_t allocate(const ContactInput& input,bool alive,bool bucket)noexcept{
    const auto index=freeContact();if(index==None){increment(capacityDrops);return None;}auto& c=contacts[index];c=Contact{};c.used=true;c.input=input;
    c.state={input.id,input.instance,0,false,bucket,alive,false};if(alive){c.firstFailOrAlive=now;}return index;
  }
  static void erase(std::array<std::uint16_t,32>& entries,std::uint16_t& count,std::uint16_t position)noexcept{for(std::uint16_t i=position;i+1<count;++i){entries[i]=entries[i+1];}--count;}
  std::size_t subtreeContacts(std::uint16_t index)const noexcept{const auto& node=nodes[index];return node.leaf()?node.count:subtreeContacts(node.left)+subtreeContacts(node.right);}
  bool split(std::uint16_t index)noexcept{
    if(static_cast<std::size_t>(nodeCount)+2>nodes.size()||nodes[index].depth>=160){increment(capacityDrops);return false;}
    auto& parent=nodes[index];const auto left=nodeCount++,right=nodeCount++;auto& one=nodes[left];auto& zero=nodes[right];
    one.depth=zero.depth=static_cast<std::uint16_t>(parent.depth+1);one.prefix=zero.prefix=parent.prefix;one.prefix[parent.depth/8]|=static_cast<std::uint8_t>(0x80U>>(parent.depth%8));
    if(parent.containsLocal){one.containsLocal=bit(local,parent.depth);zero.containsLocal=!one.containsLocal;smallest=one.containsLocal?right:left;}
    for(std::uint16_t i=0;i<parent.count;++i){const auto contact=parent.bucket[i];auto& target=bit(contacts[contact].state.id,parent.depth)?one:zero;target.bucket[target.count++]=contact;}
    for(std::uint16_t i=0;i<parent.replacementCount;++i){contacts[parent.replacements[i]].used=false;} // Source discards replacements on split.
    parent.count=parent.replacementCount=0;parent.left=left;parent.right=right;return true;
  }
  void alive(Node& node,std::uint16_t index)noexcept{
    auto& c=contacts[index];const auto previousTime=c.firstFailOrAlive;c.state.pingOutstanding=false;c.state.failures=0;c.state.hasBeenAlive=true;c.firstFailOrAlive=now;
    auto& entries=c.state.bucket?node.bucket:node.replacements;auto& count=c.state.bucket?node.count:node.replacementCount;
    for(std::uint16_t i=0;i<count;++i){if(entries[i]==index){erase(entries,count,i);entries[count++]=index;break;}}
    if(!c.state.bucket&&now-previousTime>30000){pingOldest(node);}
  }
  RouterChange removeOrFail(Node& node,std::uint16_t index,bool force)noexcept{
    auto& c=contacts[index];if(c.state.local){return RouterChange::Ignored;}
    if(config.bootstrapProxy&&!node.replacementCount){return RouterChange::Ignored;}
    c.state.pingOutstanding=false;if(c.state.failures==0){c.firstFailOrAlive=now;}++c.state.failures;
    const auto threshold=c.state.hasBeenAlive?c.input.maxFailLive:c.input.maxFailUnknown;
    if(!force&&c.state.failures<threshold){return RouterChange::Updated;}
    auto& entries=c.state.bucket?node.bucket:node.replacements;auto& count=c.state.bucket?node.count:node.replacementCount;
    for(std::uint16_t i=0;i<count;++i){if(entries[i]==index){erase(entries,count,i);break;}}
    const bool wasBucket=c.state.bucket;c.used=false;
    if(wasBucket&&node.replacementCount){std::uint16_t position=static_cast<std::uint16_t>(node.replacementCount-1);
      for(std::uint16_t i=node.replacementCount;i>0;--i){if(contacts[node.replacements[i-1]].state.hasBeenAlive){position=static_cast<std::uint16_t>(i-1);break;}}
      const auto promoted=node.replacements[position];erase(node.replacements,node.replacementCount,position);contacts[promoted].state.bucket=true;node.bucket[node.count++]=promoted;added(promoted);
    }
    return RouterChange::Removed;
  }
  RouterChange replacement(Node& node,const ContactInput& input,bool live,std::uint16_t maximum)noexcept{
    if(!maximum){return RouterChange::Dropped;}bool tryPing=node.replacementCount==0;
    if(node.replacementCount>=maximum){
      for(std::uint16_t i=0;i<node.replacementCount;++i){
        const auto old=node.replacements[i];if(!contacts[old].state.hasBeenAlive){contacts[old].used=false;erase(node.replacements,node.replacementCount,i);if(live){tryPing=true;}if(node.replacementCount<maximum){break;}}}
      if(live&&node.replacementCount>=maximum){contacts[node.replacements[0]].used=false;erase(node.replacements,node.replacementCount,0);}
    }else if(node.replacementCount){tryPing=true;}
    if(node.replacementCount>=maximum){return RouterChange::Dropped;}
    const auto index=allocate(input,live,false);if(index==None){return RouterChange::Dropped;}node.replacements[node.replacementCount++]=index;if(tryPing){pingOldest(node);}return RouterChange::Replacement;
  }
  RouterChange add(const ContactInput& input,bool live)noexcept{
    if(input.sleeping&&input.id==local){return RouterChange::Ignored;}if(!input.sleeping&&live){consecutiveDead=0;}if(input.id==local){return RouterChange::Ignored;}
    auto index=leaf(input.id);auto existing=findContact(input.id);
    if(input.sleeping){return existing==None?RouterChange::Ignored:removeOrFail(nodes[index],existing,true);}
    if(existing!=None){auto& c=contacts[existing];if(live){alive(nodes[index],existing);}
      if(c.state.bucket&&input.instance&&input.instance!=c.input.instance){c.input=input;c.state.instance=input.instance;added(existing);}return RouterChange::Updated;}
    for(;;){bool inside=false;index=leaf(input.id,&inside);auto& node=nodes[index];
      if(sleeping&&!node.containsLocal&&node.count>=config.k/4){return replacement(node,input,live,1);}
      if(node.count<config.k){const auto contact=allocate(input,live,true);if(contact==None){return RouterChange::Dropped;}node.bucket[node.count++]=contact;added(contact);return RouterChange::Bucket;}
      if(node.containsLocal||node.depth%config.b!=0||inside){
        if((inside&&node.depth%config.b==0&&!node.containsLocal&&subtreeContacts(smallest)>(std::size_t{1}<<config.b)+10240)||freeContact()==None){increment(capacityDrops);return RouterChange::Dropped;}
        if(!split(index)){return RouterChange::Dropped;}
      }else{return replacement(node,input,live,sleeping?1:config.replacements);}
    }
  }
};
RouterOwner::RouterOwner(std::unique_ptr<Impl> value)noexcept:impl_(std::move(value)){}
RouterOwner::~RouterOwner()=default;
std::expected<std::unique_ptr<RouterOwner>,RouterError> RouterOwner::create(NodeId id,RouterConfig config,std::uint64_t generation,std::int64_t now){
  if(!generation||now<0||!config.k||config.k>32||!config.b||config.b>16||config.replacements>32){return std::unexpected(RouterError::Invalid);}
  try{return std::unique_ptr<RouterOwner>(new RouterOwner(std::make_unique<Impl>(id,config,generation,now)));}catch(const std::bad_alloc&){return std::unexpected(RouterError::Memory);}
}
namespace {bool validInput(const ContactInput& input)noexcept{return input.maxFailLive>0&&input.maxFailLive<=100&&input.maxFailUnknown>0&&input.maxFailUnknown<=100;}}
std::expected<RouterChange,RouterError> RouterOwner::contactKnown(const ContactInput& input,std::int64_t now,bool force)noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}if(!validInput(input)){return std::unexpected(RouterError::Invalid);}const auto begun=impl_->begin(now);if(!begun){return std::unexpected(begun.error());}
  if(now-impl_->recentStart>600000){impl_->recentCount=0;impl_->recentStart=now;}
  bool seen=false;for(std::size_t i=0;i<impl_->recentCount;++i){seen|=impl_->recent[i]==input.id;}
  if(seen&&!force){increment(impl_->suppressedKnown);return RouterChange::Ignored;}
  if(!seen){if(impl_->recentCount==impl_->recent.size()){increment(impl_->capacityDrops);return RouterChange::Dropped;}impl_->recent[impl_->recentCount++]=input.id;}
  return impl_->add(input,false);
}
std::expected<RouterChange,RouterError> RouterOwner::contactAlive(const ContactInput& input,std::int64_t now)noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}if(!validInput(input)){return std::unexpected(RouterError::Invalid);}const auto begun=impl_->begin(now);if(!begun){return std::unexpected(begun.error());}return impl_->add(input,true);
}
std::expected<RouterChange,RouterError> RouterOwner::contactDead(const NodeId& id,std::int64_t now,bool force)noexcept{
  const auto begun=impl_->begin(now);if(!begun){return std::unexpected(begun.error());}if(impl_->suspended||id==impl_->local){return RouterChange::Ignored;}
  if(impl_->consecutiveDead!=std::numeric_limits<std::uint32_t>::max()){++impl_->consecutiveDead;}
  const auto index=impl_->findContact(id);if(index==None||(impl_->consecutiveDead>=100&&!force)){return RouterChange::Ignored;}return impl_->removeOrFail(impl_->nodes[impl_->leaf(id)],index,force);
}
std::expected<void,RouterError> RouterOwner::setSleeping(bool value,std::int64_t now)noexcept{const auto begun=impl_->begin(now);if(!begun){return begun;}impl_->sleeping=value;return {};}
std::expected<void,RouterError> RouterOwner::setSuspended(bool value,std::int64_t now)noexcept{const auto begun=impl_->begin(now);if(!begun){return begun;}impl_->suspended=value;return {};}
std::expected<std::optional<RouterContact>,RouterError> RouterOwner::find(const NodeId& id)const noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}const auto index=impl_->findContact(id);return index==None?std::optional<RouterContact>{}:impl_->contacts[index].state;
}
std::expected<RouterStatisticsSnapshot,RouterError> RouterOwner::snapshot()const noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}RouterStatisticsSnapshot result;result.generation_=impl_->generation;result.revision_=impl_->revision;result.config_=impl_->config;
  result.capacityDrops_=impl_->capacityDrops;result.lostIntents_=impl_->lostIntents;result.suppressedKnown_=impl_->suppressedKnown;result.consecutiveDead_=impl_->consecutiveDead;result.sleeping_=impl_->sleeping;result.suspended_=impl_->suspended;
  result.counts_[0]=impl_->nodeCount;
  for(std::uint16_t i=0;i<impl_->nodeCount;++i){const auto& node=impl_->nodes[i];if(!node.leaf()){continue;}++result.counts_[1];result.counts_[2]+=node.count;result.counts_[3]+=node.replacementCount;
    result.buckets_[result.bucketCount_++]={node.prefix,node.depth,node.count,node.replacementCount,node.containsLocal};
    for(std::uint16_t c=0;c<node.count;++c){const auto& contact=impl_->contacts[node.bucket[c]].state;++result.counts_[contact.failing()?6:contact.hasBeenAlive?4:5];}}
  return result;
}
std::expected<RouterIntents,RouterError> RouterOwner::takeIntents()noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}if(impl_->revision==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(RouterError::Exhausted);}
  const auto result=impl_->intents;if(result.count){impl_->intents.count=0;++impl_->revision;}return result;
}
} // namespace biglybtc::services::azureus_router
