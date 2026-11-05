// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
#include "transaction_history.hpp"
#include "inbound_responder.hpp"
#include "routing_maintenance.hpp"
#include "saved_routing.hpp"
#include <bit>
#include <deque>
#include "../azureus_router/router.hpp"
#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <openssl/rand.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <new>
namespace biglybtc::services::i2p::dht {
struct Actor::Impl {
  using Clock=std::chrono::steady_clock;
  Actor* q;QPointer<i2cp::InitialChannel> channel;QTimer timer;Options options;
  Lookup lookup;std::uint64_t nextOperation=1,scope=0,resolver=0;std::optional<std::size_t> resolving;
  detail::TransactionHistory transactions;Bytes localDestination;Hash localHash{};
  bool enabled=false,pumping=false,wakeQueued=false,notificationQueued=false,inboundRequested=false;
  std::unique_ptr<inbound::Responder> responder;
  std::unique_ptr<azureus_router::RouterOwner> router;
  std::vector<Contact> routingContacts;
  std::vector<SavedContact> importedContacts;
  std::vector<RoutingProtocolObservation> routingProtocols;
  std::vector<std::uint64_t> replyTickets;
#include "routing_maintenance.inc"
  explicit Impl(Actor* owner,i2cp::InitialChannel& c):q(owner),channel(&c),timer(owner){
    timer.setSingleShot(true);timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer,&QTimer::timeout,q,[this]{safePump();});
    QObject::connect(&c,&i2cp::InitialChannel::dhtChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::destinationLookupChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::leaseSetSubmitted,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::stateChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::closed,q,[this]{safeClose();});
    QObject::connect(&c,&i2cp::InitialChannel::failed,q,[this]{safeClose();});
    QObject::connect(&c,&QObject::destroyed,q,[this]{channel=nullptr;safeClose();});
  }
  bool onThread()const{return QThread::currentThread()==q->thread()&&(!channel||channel->thread()==q->thread());}
  static std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
  void notify(){
    if(notificationQueued){return;}notificationQueued=true;
    const QPointer<Actor> owner(q);if(!QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}owner->impl_->notificationQueued=false;emit owner->changed();},Qt::QueuedConnection)){notificationQueued=false;throw std::bad_alloc();}
  }
  void schedule(){
    if(wakeQueued){return;}wakeQueued=true;const QPointer<Actor> owner(q);
    if(!QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}owner->impl_->wakeQueued=false;owner->impl_->safePump();},Qt::QueuedConnection)){wakeQueued=false;safeClose();}
  }
  bool current()const{
    if(!enabled||!channel||scope!=channel->dhtScopeGeneration()){return false;}
    if(channel->state()==i2cp::InitialChannel::State::Reconfiguring){return true;}
    auto identity=channel->ownedLeaseDestination();return identity&&identity->wire==localDestination;
  }
  void cancelPending(){
    if(channel){if(resolver){channel->cancelDestinationLookup(resolver);}for(const auto& n:lookup.nodes()){if(n.ticket){channel->cancelDhtDatagram(n.ticket);}}}
    resolver=0;resolving.reset();
  }
  void close(){
    const auto ownedScope=scope;
    stopInbound();timer.stop();cancelPending();lookup.close();enabled=false;scope=0;localDestination.clear();
    if(channel&&onThread()&&ownedScope){channel->disableDhtDatagrams(ownedScope);}notify();
  }
  void safeClose()noexcept{try{close();}catch(...){timer.stop();stopInbound();enabled=false;scope=0;lookup.close();}}
  void safePump()noexcept{try{pump();}catch(...){safeClose();}}
  Result<Bytes> transaction(std::int64_t stamp){
    if(!transactions.advance(stamp)){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid DHT transaction clock"});}
    if(!transactions.available()){return std::unexpected(Error{ErrorCode::TooLarge,"DHT recent transaction window is full"});}
    std::array<Byte,8> tx{};for(unsigned tries=0;tries<8;++tries){if(RAND_bytes(tx.data(),int(tx.size()))!=1){break;}
      if(transactions.remember(tx,stamp)){return Bytes(tx.begin(),tx.end());}}
    return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT transaction entropy unavailable"});
  }
  void stopInbound()noexcept{
    stopMaintenance();inboundRequested=false;if(responder){responder->close();}responder.reset();router.reset();routingContacts.clear();importedContacts.clear();routingProtocols.clear();options.savedContacts.clear();
    auto tickets=std::move(replyTickets);replyTickets.clear();
    if(channel&&onThread()){for(const auto ticket:tickets){channel->cancelDhtDatagram(ticket);}}
  }
  bool enableInbound(){
    if(!onThread()||!current()||inboundRequested||!channel->dhtScopeObservation(scope).sourceBound){return false;}
    inboundRequested=true;channel->requestDhtEffects(scope);notify();return true;
  }
  bool inboundCurrent(Clock::time_point end)const{
    return inboundRequested&&Clock::now()<end&&current()
        &&channel->dhtScopeObservation(scope).granted;
  }
  bool learnRouting(const Contact& contact,bool alive,Clock::time_point end=Clock::time_point::max()){
    if(!validContact(contact)||!router){return false;}
    // Allocate the owning attachment before changing the routing tree. Existing
    // instance0 attachments retain their original identity, as in the source.
    auto found=std::find_if(routingContacts.begin(),routingContacts.end(),[&](const auto& c){return c.id==contact.id;});
    bool inserted=false;
    if(found==routingContacts.end()){
      Contact prepared=contact;if(!inboundCurrent(end)){return false;}
      std::erase_if(routingContacts,[&](const auto& c){auto known=router->find(c.id);return known&&!known->has_value();});
      if(routingContacts.size()==azureus_router::MaximumRouterContacts){return false;}
      routingContacts.push_back(std::move(prepared));inserted=true;
    }
    if(!inboundCurrent(end)){if(inserted){routingContacts.pop_back();}return false;}
    azureus_router::ContactInput input;input.id=contact.id;input.maxFailLive=3;input.maxFailUnknown=2;
    auto result=alive?router->contactAlive(input,now(),QDateTime::currentMSecsSinceEpoch()):router->contactKnown(input,now());
    if(!result||*result==azureus_router::RouterChange::Dropped){if(inserted){routingContacts.pop_back();}return bool(result);}
    return true;
  }
  bool rememberRoutingProtocol(const Contact& contact,unsigned version,Clock::time_point end){
    if(version>255||!inboundCurrent(end)||!router){return false;}
    const auto attached=std::find_if(routingContacts.begin(),routingContacts.end(),[&](const auto& c){return c.id==contact.id&&c.hash==contact.hash&&c.port==contact.port;});
    if(attached==routingContacts.end()){return true;} // No attachment, no version hint; routing policy is unchanged.
    const auto protocol=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(version));
    auto found=std::find_if(routingProtocols.begin(),routingProtocols.end(),[&](const auto& value){return value.id==contact.id;});
    if(found!=routingProtocols.end()){
      if(found->hash!=contact.hash||found->port!=contact.port){return true;}found->protocolVersion=protocol;return true;
    }
    std::erase_if(routingProtocols,[&](const auto& value){auto known=router->find(value.id);return known&&!known->has_value();});
    if(routingProtocols.size()==azureus_router::MaximumRouterContacts||!inboundCurrent(end)){return false;}
    routingProtocols.push_back({contact.id,contact.hash,contact.port,protocol});return true;
  }
  Result<std::vector<SavedContact>> exportSavedContacts()const{
    const auto reject=[]()->Result<std::vector<SavedContact>>{return std::unexpected(Error{ErrorCode::InvalidFormat,"Current held I2P routing source required"});};
    if(!onThread()||!responder||!router||!inboundCurrent(Clock::time_point::max())){return reject();}
    auto best=router->bestContacts(now(),QDateTime::currentMSecsSinceEpoch());
    if(!best){return std::unexpected(Error{ErrorCode::InvalidFormat,"Current I2P routing export unavailable"});}
    auto rows=selectSavedRoutingContacts(*best,routingContacts,importedContacts,routingProtocols);
    if(!inboundCurrent(Clock::time_point::max())){return reject();}return rows;
  }
  bool prepareInbound(){
    if(responder){return true;}if(!inboundCurrent(Clock::time_point::max())){return false;}
    auto identity=channel->ownedLeaseDestination();if(!identity){return false;}
    Contact local{options.localId,localHash,options.queryPort,*identity};
    auto made=inbound::Responder::create(local);if(!made){return false;}
    auto tree=azureus_router::RouterOwner::create(options.localId,{10,2,5,false},scope,now(),QDateTime::currentMSecsSinceEpoch());if(!tree){return false;}
    routingContacts.reserve(azureus_router::MaximumRouterContacts);routingProtocols.reserve(azureus_router::MaximumRouterContacts);replyTickets.reserve(8);
    if(!inboundCurrent(Clock::time_point::max())){return false;}
    router=std::move(*tree);routingContacts.push_back(std::move(local));responder=std::move(*made);
    if(!router->setSleeping(options.inboundSleeping,now())||!router->setSuspended(options.inboundSuspended,now())){return false;}
    // Import contactKnown only; old timeAlive is never given to RouterOwner.
    // Membership/version metadata remains distinct from live protocol hints.
    // A previous NID/query port for this same Destination is still our own
    // identity, never a remote import or an imported-priority membership.
    std::erase_if(options.savedContacts,[&](const auto& saved){return saved.contact.hash==localHash;});
    for(const auto& saved:options.savedContacts){if(!inboundCurrent(Clock::time_point::max())||!learnRouting(saved.contact,false)){return false;}}
    importedContacts=std::move(options.savedContacts);options.savedContacts.clear();
    for(const auto& seed:options.seeds){if(!inboundCurrent(Clock::time_point::max())||!learnRouting(seed,false)){return false;}}
    if(options.routingMaintenance){const auto stamp=now();if(stamp>std::numeric_limits<std::int64_t>::max()-detail::RoutingTickMillis){stopInbound();return false;}maintenanceNextTick=stamp+detail::RoutingTickMillis;maintenanceDead.reserve(MaximumContacts);}
    return true;
  }
  std::optional<inbound::RoutingSnapshot> routeInbound(const Contact& actual,Method method,const Id& target,bool existing,unsigned version,Clock::time_point end){
    if(!inboundCurrent(end)||!learnRouting(actual,true,end)){return {};}
    inbound::RoutingSnapshot result;
    if(method==Method::FindNode||method==Method::GetPeers){
      auto candidates=router->closest(target,10,true);if(!candidates){return {};}
      for(std::size_t i=0;i<candidates->count;++i){
        const auto id=candidates->values[i].id;auto at=std::find_if(routingContacts.begin(),routingContacts.end(),[&](const auto& c){return c.id==id;});
        if(at==routingContacts.end()){return {};}result.closest.push_back(*at);
      }
      std::sort(result.closest.begin(),result.closest.end(),[&](const auto& a,const auto& b){for(std::size_t i=0;i<target.size();++i){const auto left=Byte(a.id[i]^target[i]),right=Byte(b.id[i]^target[i]);if(left!=right){return left<right;}}return false;});
      if(result.closest.size()>10){result.closest.resize(10);}
    }
    if(method==Method::AnnouncePeer){auto local=router->localInClosest(target,10,true);if(!local){return {};}result.allowStore=existing||*local;}
    if(!rememberRoutingProtocol(actual,version,end)){return {};}return result;
  }
  void inboundPacket(const i2cp::InitialChannel::DhtDatagram& data,unsigned version){
    if(!inboundCurrent(data.end)){return;}if(!prepareInbound()){stopInbound();return;}
    const auto expected=scope;auto reply=responder->receive({data.payload,data.sender?&*data.sender:nullptr,data.fromPort,data.toPort},QDateTime::currentMSecsSinceEpoch(),{options.inboundSleeping,options.inboundSuspended},
        [this,expected,end=data.end]{return scope==expected&&inboundCurrent(end);},
        [this,end=data.end,version](const Contact& actual,Method method,const Id& target,bool existing){return routeInbound(actual,method,target,existing,version,end);});
    if(!reply){if(!inboundCurrent(data.end)){return;}return;}
    if(!*reply||!inboundCurrent(data.end)){return;}if(!(*reply)->recipient.destination){stopInbound();return;}
    auto sent=channel->submitDhtReply(scope,(*reply)->recipient.destination->wire,(*reply)->payload,(*reply)->toPort,data.end);
    if(sent){replyTickets.push_back(*sent);}
  }
  bool resumeInbound(){
    if(!onThread()||!inboundRequested){return false;}if(!current()){close();return false;}if(!channel->dhtScopeObservation(scope).granted){return false;}
    const bool starting=!responder;if(!prepareInbound()){stopInbound();return false;}if(starting){notify();}
    if(channel->dhtScopeObservation(scope).queued||options.routingMaintenance){pump(true);}return inboundRequested&&current();
  }
  bool begin(const Options& value){
    if(!onThread()||enabled||!channel||((value.routingMaintenance||!value.savedContacts.empty())&&!value.inbound)||value.savedContacts.size()>MaximumSavedContacts||value.seeds.empty()||value.seeds.size()>20||!value.parallelism||value.parallelism>3){return false;}
    auto identity=channel->ownedLeaseDestination();if(!identity){return false;}
    Contact local{value.localId,identity->hash,value.queryPort,{}};if(!validContact(local)){return false;}
    if(std::any_of(value.seeds.begin(),value.seeds.end(),[&](const auto& c){return !validContact(c)||c.hash==identity->hash;})){return false;}
    // Validate/canonicalize saved rows as inert bounded data before enabling
    // any DHT scope. This does not import router or source authority.
    auto encoded=encodeSavedContacts(value.savedContacts);if(!encoded){return false;}auto saved=decodeSavedContacts(*encoded);if(!saved){return false;}
    options=value;options.savedContacts=std::move(*saved);
    for(auto& c:options.seeds){if(c.destination){auto parsed=decodeDestination(c.destination->wire);if(!parsed||parsed->hash!=c.hash){return false;}c.destination=std::move(*parsed);}}
    localDestination=identity->wire;localHash=identity->hash;
    if(!channel->enableDhtDatagrams(value.queryPort)){return false;}scope=channel->dhtScopeGeneration();enabled=true;if(value.inbound&&!enableInbound()){close();return false;}notify();return true;
  }
  Result<std::uint64_t> start(const Id& target,bool seed,bool announce,unsigned timeout){
    if(!onThread()||!current()||lookup.running()){return std::unexpected(Error{ErrorCode::InvalidFormat,"Current idle explicitly bound DHT actor required"});}
    if(!nextOperation){return std::unexpected(Error{ErrorCode::TooLarge,"DHT operation identifier space exhausted"});}
    cancelPending();
    const auto operation=nextOperation;
    if(!lookup.start(operation,{target,seed,announce,timeout,options.parallelism},options.seeds,now())){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid finite DHT lookup options"});}
    if(options.routingMaintenance){pauseRefresh();}
    nextOperation=detail::nextOperationId(operation);if(options.routingMaintenance){maintenanceApplicationLookup=std::pair{operation,target};}pump();return operation;
  }
  void pump(bool heldInbound=false){
    if(pumping||!onThread()||!enabled){return;}
    if(!current()){close();return;}pumping=true;struct Guard{bool& v;~Guard(){v=false;}} guard{pumping};
    const bool clientWasRunning=lookup.running();bool progressed=false;lookup.tick(now());expireMaintenance(now());if(!current()){return;}
    for(std::size_t steps=0;steps<8;++steps){auto result=channel->takeDhtDispatch();if(!result){break;}progressed=true;
      auto server=std::find(replyTickets.begin(),replyTickets.end(),result->ticket);
      if(server!=replyTickets.end()){replyTickets.erase(server);}else if(!maintenanceDispatch(*result)){lookup.dispatched(result->ticket,result->written,now());}
    }
    const bool receiveNow=!inboundRequested||(heldInbound&&channel->dhtScopeObservation(scope).granted);
    if(inboundRequested&&!receiveNow&&channel->dhtScopeObservation(scope).queued){channel->requestDhtEffects(scope);}
    for(std::size_t steps=0;receiveNow&&steps<8;++steps){
      if(inboundRequested&&(!inboundCurrent(Clock::time_point::max())||!channel->dhtScopeObservation(scope).writeSlots)){break;}
      auto data=channel->takeDhtDatagram();if(!data){break;}progressed=true;if(Clock::now()>=data->end){continue;}
      auto message=decode(data->payload);if(!message){continue;}
      if(message->kind==Kind::Query){if(inboundRequested){inboundPacket(*data,message->version);}continue;}
      if(data->sender){continue;}
      std::erase_if(message->nodes,[&](const auto& c){return c.hash==localHash||c.id==options.localId;});
      if(maintenanceReply(*message,data->fromPort,data->end)){continue;}
      std::optional<Contact> actual;
      if(inboundRequested){for(const auto& n:lookup.nodes()){if(n.phase==NodePhase::Sent&&n.transaction==message->transaction&&n.contact.port==data->fromPort){actual=n.contact;break;}}}
      if(lookup.reply(*message,data->fromPort,now())&&actual&&inboundCurrent(data->end)&&prepareInbound()){
        if(!learnRouting(*actual,true,data->end)){stopInbound();continue;}
        if(message->kind==Kind::Reply&&message->id==actual->id&&!rememberRoutingProtocol(*actual,message->version,data->end)){stopInbound();continue;}
        for(const auto& c:message->nodes){if(!inboundCurrent(data->end)||!learnRouting(c,false,data->end)){stopInbound();break;}}
      }
    }
    if(!current()){close();return;}
    if(resolver){
      auto result=channel->destinationLookupObservation(resolver);
      if(result&&result->operation==resolver&&result->phase!=i2cp::InitialChannel::LookupPhase::Pending){
        if(resolving&&result->phase==i2cp::InitialChannel::LookupPhase::Resolved&&result->destination){if(!lookup.resolved(*resolving,*result->destination)){lookup.failed(*resolving);}}
        else if(resolving){
          lookup.failed(*resolving);
        }
        resolver=0;resolving.reset();progressed=true;
      }
    }
    std::optional<std::int64_t> transactionWake;
    // At most three new RPCs per turn. No reentrant callback/foreign code is
    // invoked; channel signals are queued current-state notifications.
    for(unsigned steps=0;steps<3&&lookup.running()&&channel->state()==i2cp::InitialChannel::State::SessionCreated;++steps){
      auto index=lookup.next(now());if(!index){break;}const auto node=lookup.nodes().at(*index);
      if(!node.contact.destination){
        if(resolver){break;}
        // The provider shares this channel's single HostLookup slot. Yield to
        // its real completion notification, keeping this lookup's original end.
        const auto currentLookup=channel->destinationLookupObservation();
        if(currentLookup&&currentLookup->phase==i2cp::InitialChannel::LookupPhase::Pending){break;}
        auto op=channel->lookupDestination(node.contact.hash,30000);
        if(!op){lookup.failed(*index);continue;}if(!lookup.resolving(*index)){channel->cancelDestinationLookup(*op);continue;}resolver=*op;resolving=*index;progressed=true;continue;
      }
      // Server replies and client RPCs share the existing eight slots. An
      // unsent candidate waits for a real dispatch wake without consuming a
      // transaction or extending the lookup end. Lifetime exhaustion differs.
      const auto capacity=channel->dhtScopeObservation(scope);
      if(!capacity.writeSlots){if(capacity.lifetimeExhausted){lookup.failed(*index);progressed=true;continue;}break;}
      const auto intended=now();if(!transactions.advance(intended)){close();return;}
      if(!transactions.available()){transactionWake=transactions.wakeup(lookup.end());break;}
      auto tx=transaction(intended);if(!tx){close();return;}
      Message message;message.transaction=*tx;message.id=options.localId;message.target=lookup.options().target;
      message.method=node.phase==NodePhase::Replied?Method::AnnouncePeer:Method::GetPeers;message.noSeed=lookup.options().seed;message.seed=lookup.options().seed;message.token=node.token;
      auto packet=encode(message);if(!packet){lookup.failed(*index);continue;}
      const auto left=lookup.end()-now();if(left<=0){lookup.tick(now());break;}
      // announce_peer is raw18 to target Q+1; get_peers is signed17 to Q.
      const bool signedQuery=message.method!=Method::AnnouncePeer;
      const auto port=std::uint16_t(node.contact.port+(signedQuery?0:1));
      auto ticket=channel->submitDhtDatagram(node.contact.destination->wire,*packet,signedQuery,port,unsigned(std::min<std::int64_t>(30000,left)));
      if(!ticket){lookup.failed(*index);continue;}if(!lookup.queued(*index,std::move(*tx),*ticket,intended)){channel->cancelDhtDatagram(*ticket);}else{progressed=true;}
    }
    lookup.tick(now());
    if(heldInbound){maintainRouting();}if(!current()){return;}
    if(maintenanceNeedsGrant(now())){channel->requestDhtEffects(scope);}
    if(!lookup.running()){cancelPending();}
    auto end=maintenanceWake(now());
    if(lookup.running()){
      end=end?std::min(*end,lookup.end()):lookup.end();if(transactionWake){end=std::min(*end,*transactionWake);}
      for(const auto& n:lookup.nodes()){if(n.phase==NodePhase::Resolving||n.phase==NodePhase::Queued||n.phase==NodePhase::Sent){end=std::min(*end,n.end);}}
    }
    if(end){timer.start(int(std::max<std::int64_t>(1,*end-now())));}else{timer.stop();}
    if(!inboundRequested||!heldInbound||progressed||clientWasRunning){notify();}
  }
};
Actor::Actor(i2cp::InitialChannel& c,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,c)){}
Actor::~Actor(){impl_->safeClose();}
bool Actor::begin(const Options& value){try{return impl_->begin(value);}catch(...){impl_->safeClose();return false;}}
Result<std::uint64_t> Actor::lookupPeers(const Id& target,bool seed,unsigned timeout){try{return impl_->start(target,seed,false,timeout);}catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}}
Result<std::uint64_t> Actor::announceSelf(const Id& target,bool seed,unsigned timeout){try{return impl_->start(target,seed,true,timeout);}catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}}
bool Actor::enableInbound(){try{return impl_->enableInbound();}catch(...){impl_->stopInbound();return false;}}
void Actor::stopInbound(){if(impl_->onThread()){impl_->stopInbound();}}
bool Actor::inboundEnabled()const noexcept{return impl_->onThread()&&impl_->enabled&&impl_->inboundRequested&&bool(impl_->responder)&&impl_->channel&&impl_->scope==impl_->channel->dhtScopeGeneration();}
bool Actor::resumeAuthorizedInbound(){try{return impl_->resumeInbound();}catch(...){impl_->stopInbound();return false;}}
bool Actor::cancel(std::uint64_t op){try{if(!impl_->onThread()||!impl_->lookup.running()||impl_->lookup.snapshot().operation!=op){return false;}impl_->lookup.cancel();impl_->cancelPending();impl_->timer.stop();if(impl_->options.routingMaintenance){impl_->schedule();}impl_->notify();return true;}catch(...){impl_->safeClose();return false;}}
bool Actor::boundChannel(const i2cp::InitialChannel& channel)const noexcept{return impl_->onThread()&&impl_->channel.data()==&channel;}
Result<std::vector<SavedContact>> Actor::exportSavedContacts()const{try{return impl_->exportSavedContacts();}catch(...){return std::unexpected(Error{ErrorCode::CryptoFailure,"I2P routing export resources unavailable"});}}
void Actor::close(){if(impl_->onThread()){impl_->safeClose();}}
Snapshot Actor::observation()const{if(!impl_->onThread()){return {};}return impl_->lookup.snapshot();}
Actor::InboundObservation Actor::inboundObservation()const noexcept{
  try{
    if(!impl_->onThread()){return {};}const auto& s=*impl_;InboundObservation value;
    value.requested=s.inboundRequested;value.retained=s.current();value.ready=bool(s.responder)&&value.retained;value.scope=s.scope;
    if(s.channel){const auto state=s.channel->dhtScopeObservation(s.scope);value.sourceCurrent=state.sourceBound;value.grantActive=state.granted;}
    if(s.responder){const auto counts=s.responder->counts();if(counts){value.requests=counts->requests;value.replyPrepared=counts->replyPrepared;value.tokenRejected=counts->tokenRejected;value.tokenBindings=counts->tokenBindings;value.peerKeys=counts->peers.keys;value.peerValues=counts->peers.values;}else{value.ready=false;}}
    return value;
  }catch(...){return {};}
}
} // namespace biglybtc::services::i2p::dht
