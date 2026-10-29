// SPDX-License-Identifier: GPL-2.0-or-later
#include "router.hpp"
#include <algorithm>
#include <bit>
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
  struct Contact {bool used=false;ContactInput input;RouterContact state;std::int64_t firstFailOrAlive=0,lastAdded=0,firstAlive=0;};
  struct Node {
    NodeId prefix{};std::uint16_t depth=0,left=None,right=None;bool containsLocal=false;
    std::int64_t lastLookupWall=0;
    std::uint16_t count=0,replacementCount=0;
    std::array<std::uint16_t,32> bucket{},replacements{};
    bool leaf()const noexcept{return left==None;}
  };
  const std::thread::id owner=std::this_thread::get_id();const NodeId local;const RouterConfig config;const std::uint64_t generation;
  std::uint64_t revision=1,capacityDrops=0,lostIntents=0,suppressedKnown=0;
  std::int64_t now=0,recentStart=0,wall=0;std::uint32_t consecutiveDead=0;
  std::int64_t lastLookupWall=0;unsigned seedTicks=0;
  bool sleeping=false,suspended=false;std::uint16_t nodeCount=1,smallest=None;
  std::array<Node,MaximumRouterNodes> nodes{};std::array<Contact,MaximumRouterContacts> contacts{};
  std::array<NodeId,MaximumRouterContacts> recent{};std::size_t recentCount=0;RouterIntents intents;
  Impl(NodeId id,RouterConfig policy,std::uint64_t gen,std::int64_t time,std::int64_t wallTime):local(id),config(policy),generation(gen),now(time),recentStart(time),wall(wallTime){
    nodes[0].containsLocal=true;nodes[0].count=1;nodes[0].bucket[0]=0;
    auto& contact=contacts[0];contact.used=true;contact.input.id=id;contact.state.id=id;contact.state.local=true;contact.state.bucket=true;contact.state.hasBeenAlive=true;contact.firstFailOrAlive=time;contact.firstAlive=wallTime;
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
    c.state={input.id,input.instance,0,false,bucket,alive,false};if(alive){c.firstFailOrAlive=now;c.firstAlive=wall;}return index;
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
    auto& c=contacts[index];const auto previousTime=c.firstFailOrAlive;c.state.pingOutstanding=false;c.state.failures=0;c.state.hasBeenAlive=true;c.firstFailOrAlive=now;if(!c.firstAlive){c.firstAlive=wall;}
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
  void closest(std::uint16_t index,const NodeId& target,std::size_t requested,bool liveOnly,ClosestContacts& out)const noexcept{
    const auto& node=nodes[index];
    if(node.leaf()){
      for(std::uint16_t i=0;i<node.count;++i){const auto& value=contacts[node.bucket[i]].state;
        if(!(liveOnly&&value.failing())){out.values[out.count++]=value;}}
    }else{
      const bool one=bit(target,node.depth);closest(one?node.left:node.right,target,requested,liveOnly,out);
      if(out.count<requested){closest(one?node.right:node.left,target,requested,liveOnly,out);}
    }
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
std::expected<std::unique_ptr<RouterOwner>,RouterError> RouterOwner::create(NodeId id,RouterConfig config,std::uint64_t generation,std::int64_t now,std::optional<std::int64_t> wall){
  if(!generation||now<0||!config.k||config.k>32||!config.b||config.b>16||config.replacements>32){return std::unexpected(RouterError::Invalid);}
  try{return std::unique_ptr<RouterOwner>(new RouterOwner(std::make_unique<Impl>(id,config,generation,now,wall.value_or(now))));}catch(const std::bad_alloc&){return std::unexpected(RouterError::Memory);}
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
std::expected<RouterChange,RouterError> RouterOwner::contactAlive(const ContactInput& input,std::int64_t now,std::optional<std::int64_t> wall)noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}if(!validInput(input)){return std::unexpected(RouterError::Invalid);}const auto begun=impl_->begin(now);if(!begun){return std::unexpected(begun.error());}impl_->wall=wall.value_or(now);return impl_->add(input,true);
}
std::expected<RouterChange,RouterError> RouterOwner::contactDead(const NodeId& id,std::int64_t now,bool force)noexcept{
  const auto begun=impl_->begin(now);if(!begun){return std::unexpected(begun.error());}if(impl_->suspended||id==impl_->local){return RouterChange::Ignored;}
  if(impl_->consecutiveDead!=std::numeric_limits<std::uint32_t>::max()){++impl_->consecutiveDead;}
  const auto index=impl_->findContact(id);if(index==None||(impl_->consecutiveDead>=100&&!force)){return RouterChange::Ignored;}return impl_->removeOrFail(impl_->nodes[impl_->leaf(id)],index,force);
}
std::expected<void,RouterError> RouterOwner::setSleeping(bool value,std::int64_t now)noexcept{const auto begun=impl_->begin(now);if(!begun){return begun;}impl_->sleeping=value;return {};}
std::expected<void,RouterError> RouterOwner::setSuspended(bool value,std::int64_t now)noexcept{const auto begun=impl_->begin(now);if(!begun){return begun;}impl_->suspended=value;return {};}
std::expected<ClosestContacts,RouterError> RouterOwner::closest(const NodeId& target,std::size_t requested,bool liveOnly)const noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}if(!requested||requested>MaximumRouterContacts){return std::unexpected(RouterError::Invalid);}
  ClosestContacts result;impl_->closest(0,target,requested,liveOnly,result);return result;
}
std::expected<bool,RouterError> RouterOwner::localInClosest(const NodeId& target,std::size_t requested,bool liveOnly)const noexcept{
  const auto result=closest(target,requested,liveOnly);if(!result){return std::unexpected(result.error());}
  bool found=false;std::size_t closer=0;
  for(std::size_t n=0;n<result->count;++n){const auto& contact=result->values[n];if(contact.id==impl_->local){found=true;continue;}
    for(std::size_t i=0;i<target.size();++i){const auto a=contact.id[i]^target[i],b=impl_->local[i]^target[i];
      if(a!=b){closer+=a<b;break;}}
  }return found&&closer<requested;
}
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

std::expected<void,RouterError> RouterOwner::lookupStarted(std::int64_t wall,std::int64_t now)noexcept{
  if(wall<0){return std::unexpected(RouterError::Clock);}const auto begun=impl_->begin(now);if(!begun){return begun;}impl_->lastLookupWall=wall;return {};
}
std::expected<void,RouterError> RouterOwner::recordLookup(const NodeId& target,std::int64_t wall,std::int64_t now)noexcept{
  if(wall<0){return std::unexpected(RouterError::Clock);}const auto begun=impl_->begin(now);if(!begun){return begun;}impl_->nodes[impl_->leaf(target)].lastLookupWall=wall;return {};
}
std::expected<void,RouterError> RouterOwner::scheduleSeedRefresh(std::int64_t now)noexcept{const auto begun=impl_->begin(now);if(!begun){return begun;}impl_->seedTicks=6;return {};}
std::expected<RouterRefreshes,RouterError> RouterOwner::maintenanceTick(std::int64_t now,std::int64_t wall,const std::function<void(std::span<std::uint8_t>)>& random)noexcept{
  if(wall<0||!random){return std::unexpected(RouterError::Invalid);}const auto begun=impl_->begin(now);if(!begun){return std::unexpected(begun.error());}RouterRefreshes out;
  if(impl_->suspended){return out;}
  try{
    // Source randomized DFS, one greatest-failure/non-outstanding contact in
    // the first eligible leaf. Tie order remains the actual bucket order.
    const auto ping=[&](auto&& self,std::uint16_t index)->bool{
      auto& node=impl_->nodes[index];if(!node.leaf()){std::array<std::uint8_t,1> coin{};random(coin);const bool one=(coin[0]&1U)!=0;return self(self,one?node.left:node.right)||self(self,one?node.right:node.left);}
      std::uint16_t selected=None;std::uint32_t fails=0;
      for(std::uint16_t i=0;i<node.count;++i){const auto at=node.bucket[i];const auto& c=impl_->contacts[at].state;if(!c.local&&!c.pingOutstanding&&c.failures>fails){selected=at;fails=c.failures;}}
      if(selected==None){return false;}auto& c=impl_->contacts[selected].state;c.pingOutstanding=true;impl_->enqueue(RouterIntentKind::Ping,c.id);return true;
    };static_cast<void>(ping(ping,0));
    if(impl_->seedTicks&&--impl_->seedTicks==0){
      const auto refresh=[&](auto&& self,std::uint16_t index)->void{
        const auto& node=impl_->nodes[index];if(index==impl_->smallest){return;}
        if(wall>=node.lastLookupWall&&wall-node.lastLookupWall<=120000){return;}
        if(!node.leaf()){self(self,node.left);self(self,node.right);return;}if(node.containsLocal){return;}
        if(out.count==out.targets.size()){throw RouterError::Exhausted;}auto& target=out.targets[out.count++];random(target);
        for(std::uint16_t bit=0;bit<node.depth;++bit){const auto mask=static_cast<std::uint8_t>(0x80U>>(bit%8));target[bit/8]=static_cast<std::uint8_t>((target[bit/8]&~mask)|(node.prefix[bit/8]&mask));}
      };refresh(refresh,0);
    }
    if(wall<impl_->lastLookupWall||wall-impl_->lastLookupWall>300000){if(out.count==out.targets.size()){throw RouterError::Exhausted;}random(out.targets[out.count++]);impl_->lastLookupWall=wall;}
    return out;
  }catch(...){increment(impl_->lostIntents);return std::unexpected(RouterError::Exhausted);}
}
std::expected<RouterBestContacts,RouterError> RouterOwner::bestContacts(std::int64_t now,std::optional<std::int64_t> wall)const noexcept{
  if(!impl_->onOwner()){return std::unexpected(RouterError::Thread);}if(now<impl_->now){return std::unexpected(RouterError::Clock);}RouterBestContacts out;
  // DHTRouterImpl uses TreeSet with a signed32 narrowed age difference.
  // That comparator is not transitive after wrap, so sorted insertion/std::set
  // are not equivalent. Preserve JDK17 TreeMap put/fixAfterInsertion order in
  // this fixed-capacity, allocation-free export projection, including ties.
  struct Entry {RouterExportContact value;std::uint16_t parent=None,left=None,right=None;bool red=false;};
  std::array<Entry,MaximumRouterContacts> tree{};std::uint16_t root=None,count=0;
  const auto red=[&](std::uint16_t node){return node!=None&&tree[node].red;};
  const auto rotate=[&](std::uint16_t pivot,bool left){
    const auto child=left?tree[pivot].right:tree[pivot].left;
    if(left){tree[pivot].right=tree[child].left;if(tree[child].left!=None){tree[tree[child].left].parent=pivot;}}
    else{tree[pivot].left=tree[child].right;if(tree[child].right!=None){tree[tree[child].right].parent=pivot;}}
    tree[child].parent=tree[pivot].parent;
    if(tree[pivot].parent==None){root=child;}else if(tree[tree[pivot].parent].left==pivot){tree[tree[pivot].parent].left=child;}else{tree[tree[pivot].parent].right=child;}
    if(left){tree[child].left=pivot;}else{tree[child].right=pivot;}tree[pivot].parent=child;
  };
  const auto add=[&](RouterExportContact value){
    std::uint16_t at=root,parent=None;std::int32_t comparison=0;
    while(at!=None){parent=at;comparison=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(tree[at].value.timeAlive)-static_cast<std::uint64_t>(value.timeAlive)));if(!comparison){return;}at=comparison<0?tree[at].left:tree[at].right;}
    const auto inserted=count++;auto& entry=tree[inserted];entry.value=value;entry.parent=parent;
    if(parent==None){root=inserted;return;}if(comparison<0){tree[parent].left=inserted;}else{tree[parent].right=inserted;}entry.red=true;
    auto node=inserted;
    while(node!=root&&red(tree[node].parent)){
      auto p=tree[node].parent;const auto g=tree[p].parent;const bool left=tree[g].left==p;const auto uncle=left?tree[g].right:tree[g].left;
      if(red(uncle)){tree[p].red=false;tree[uncle].red=false;tree[g].red=true;node=g;}
      else{if(node==(left?tree[p].right:tree[p].left)){node=p;rotate(node,left);p=tree[node].parent;}tree[p].red=false;const auto grand=tree[p].parent;tree[grand].red=true;rotate(grand,!left);}
    }tree[root].red=false;
  };
  const auto collect=[&](auto&& self,std::uint16_t index)->void{
    const auto& node=impl_->nodes[index];if(!node.leaf()){self(self,node.left);self(self,node.right);return;}
    for(std::uint16_t i=0;i<node.count;++i){const auto& c=impl_->contacts[node.bucket[i]];const auto age=c.state.failures||!c.firstAlive?std::int64_t{0}:std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(wall.value_or(now))-static_cast<std::uint64_t>(c.firstAlive));add({c.state,age});}
  };collect(collect,0);
  const auto ordered=[&](auto&& self,std::uint16_t at)->void{if(at==None){return;}self(self,tree[at].left);out.values[out.count++]=tree[at].value;self(self,tree[at].right);};ordered(ordered,root);return out;
}
} // namespace biglybtc::services::azureus_router
