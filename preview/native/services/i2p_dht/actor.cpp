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
#include <QRandomGenerator>
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
  std::optional<std::int32_t> azureusInstance;
  int azureusClockOffset=0;
  AzureusLocalRead azureusLocalRead;
  AzureusMetainfoRead azureusMetainfoRead;
  static constexpr std::size_t MaximumMetainfo=4096;
  struct MetainfoTransfer {
    std::uint64_t operation=0,ticket=0,resolver=0;
    Phase phase=Phase::Idle;
    Contact peer;
    Id handler{},key{};
    std::int64_t connection=0;
    Clock::time_point end{},next{};
    bool written=false;
    Bytes bytes;
    std::vector<bool> received;
    std::size_t count=0;
  } metainfo;
  bool enabled=false,pumping=false,wakeQueued=false,notificationQueued=false,inboundRequested=false,fullInbound=false;
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
    stopInbound();stopMetainfo(Phase::Closed);timer.stop();cancelPending();lookup.close();enabled=false;scope=0;localDestination.clear();azureusLocalRead={};azureusMetainfoRead={};
    if(channel&&onThread()&&ownedScope){channel->disableDhtDatagrams(ownedScope);}notify();
  }
  void safeClose()noexcept{try{close();}catch(...){timer.stop();stopInbound();stopMetainfo(Phase::Closed);enabled=false;scope=0;lookup.close();}}
  void safePump()noexcept{try{pump();}catch(...){safeClose();}}
  Result<Bytes> transaction(std::int64_t stamp){
    if(!transactions.advance(stamp)){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid DHT transaction clock"});}
    if(!transactions.available()){return std::unexpected(Error{ErrorCode::TooLarge,"DHT recent transaction window is full"});}
    std::array<Byte,8> tx{};for(unsigned tries=0;tries<8;++tries){if(RAND_bytes(tx.data(),int(tx.size()))!=1){break;}
      if(transactions.remember(tx,stamp)){return Bytes(tx.begin(),tx.end());}}
    return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT transaction entropy unavailable"});
  }
  void stopInbound()noexcept{
    stopMaintenance();inboundRequested=false;fullInbound=false;if(responder){responder->close();}responder.reset();router.reset();routingContacts.clear();importedContacts.clear();routingProtocols.clear();options.savedContacts.clear();
    if (metainfo.phase==Phase::Running) { stopMetainfo(Phase::Cancelled); }
    auto tickets=std::move(replyTickets);replyTickets.clear();
    if(channel&&onThread()){for(const auto ticket:tickets){channel->cancelDhtDatagram(ticket);}}
  }
  bool enableInbound(bool allMethods){
    if(!onThread()||!current()||inboundRequested||!channel->dhtScopeObservation(scope).sourceBound){return false;}
    inboundRequested=true;fullInbound=allMethods;channel->requestDhtEffects(scope);notify();return true;
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
    auto made=inbound::Responder::create(local,{},{},azureusInstance,options.azureusReplicas);if(!made){return false;}
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
    if(!inboundCurrent(end)||!learnRouting(actual,true,end)||!rememberRoutingProtocol(actual,version,end)){return {};}
    inbound::RoutingSnapshot result;
    if(method==Method::FindNode||method==Method::GetPeers||method==Method::AzureusFindValue){
      auto candidates=router->closest(target,10,true);if(!candidates){return {};}
      for(std::size_t i=0;i<candidates->count;++i){
        const auto id=candidates->values[i].id;auto at=std::find_if(routingContacts.begin(),routingContacts.end(),[&](const auto& c){return c.id==id;});
        if(at==routingContacts.end()){return {};}result.closest.push_back(*at);
      }
      std::sort(result.closest.begin(),result.closest.end(),[&](const auto& a,const auto& b){for(std::size_t i=0;i<target.size();++i){const auto left=Byte(a.id[i]^target[i]),right=Byte(b.id[i]^target[i]);if(left!=right){return left<right;}}return false;});
      if(result.closest.size()>10){result.closest.resize(10);}
    }
    if (method==Method::AzureusFindValue) {
      for (const auto& contact:result.closest) {
        if (contact.id==options.localId||contact.id==actual.id) { continue; }
        const auto known=std::ranges::find(routingProtocols,contact.id,&RoutingProtocolObservation::id);
        if (known!=routingProtocols.end()&&known->hash==contact.hash&&known->port==contact.port) {
          const auto protocol=std::bit_cast<std::uint8_t>(known->protocolVersion);
          if (protocol>=MinimumAzureusProtocol) { result.closestAzureus.push_back({contact,protocol}); }
        }
      }
    }
    if(method==Method::AnnouncePeer||method==Method::AzureusStore){auto local=router->localInClosest(target,10,true);if(!local){return {};}result.allowStore=existing||*local;}
    return result;
  }
  void inboundPacket(const i2cp::InitialChannel::DhtDatagram& data,unsigned version){
    if(!inboundCurrent(data.end)){return;}if(!prepareInbound()){stopInbound();return;}
    const auto expected=scope;auto reply=responder->receive({data.payload,data.sender?&*data.sender:nullptr,data.fromPort,data.toPort},QDateTime::currentMSecsSinceEpoch(),{options.inboundSleeping,options.inboundSuspended},
        [this,expected,end=data.end]{return scope==expected&&inboundCurrent(end);},
        [this,end=data.end,version](const Contact& actual,Method method,const Id& target,bool existing){return routeInbound(actual,method,target,existing,version,end);},
        azureusLocalRead);
    if(!reply){if(!inboundCurrent(data.end)){return;}return;}
    if(!*reply||!inboundCurrent(data.end)){return;}if(!(*reply)->recipient.destination){stopInbound();return;}
    auto sent=channel->submitDhtReply(scope,(*reply)->recipient.destination->wire,(*reply)->payload,(*reply)->toPort,data.end);
    if(sent){replyTickets.push_back(*sent);}
  }
  void metainfoPacket(const i2cp::InitialChannel::DhtDatagram& data,const Message& message) {
    if (!inboundCurrent(data.end)||!data.sender||data.toPort!=options.queryPort
        ||options.inboundSuspended||(options.inboundSleeping&&!message.wakeRequested)) { return; }
    const Contact requester{message.id,data.sender->hash,data.fromPort,data.sender};
    if (!validContact(requester)) { return; }
    const auto& request=std::get<AzureusReadPacket>(message.azureus->payload);
    if (request.type==AzureusReadPacketType::Reply) { acceptMetainfo(requester,request);return; }
    if (!azureusMetainfoRead) { return; }
    auto whole=azureusMetainfoRead(request.handler,request.key);
    if (!whole||whole->empty()||whole->size()>MaximumMetainfo||static_cast<std::size_t>(request.start)>=whole->size()) { return; }
    const auto remaining=whole->size()-static_cast<std::size_t>(request.start);
    if (request.length&&static_cast<std::size_t>(request.length)>remaining) { return; }
    const auto length=request.length?static_cast<std::size_t>(request.length):remaining;
    auto tx=transaction(now());
    if (!tx) { return; }
    AzureusReadPacket response{AzureusReadPacketType::Reply,request.handler,request.key,
        QDateTime::currentMSecsSinceEpoch()+azureusClockOffset,request.connection,request.start,
        static_cast<std::int32_t>(length),static_cast<std::int32_t>(whole->size()),{}};
    const auto fragment=View(*whole).subspan(static_cast<std::size_t>(request.start),length);
    response.fragment.assign(fragment.begin(),fragment.end());
    Message outgoing;
    outgoing.method=Method::AzureusData;
    outgoing.id=options.localId;
    outgoing.transaction=std::move(*tx);
    outgoing.wakeRequested=true;
    outgoing.azureus=AzureusMessage{*azureusInstance,0,std::move(response)};
    const auto bytes=encode(outgoing);
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(data.end-Clock::now()).count();
    // Write under the current source permit without deferring the response
    // past this turn. A busy socket can take the peer's ordinary read retry.
    if (!bytes||!inboundCurrent(data.end)||!channel->ownedTransportIdle()
        ||channel->dhtScopeObservation(scope).queuedWrites||left<=0) { return; }
    const auto written=channel->submitDhtDatagram(requester.destination->wire,*bytes,true,requester.port,
        static_cast<unsigned>(std::min<std::int64_t>(left,30000)));
    if (written) { replyTickets.push_back(*written); }
  }
  Result<std::uint64_t> startMetainfo(const Contact& peer,const Id& handler,const Id& key,unsigned timeout) {
    if (!onThread()||!current()||!azureusInstance||!inboundRequested||metainfo.phase==Phase::Running
        ||!validContact(peer)||peer.hash==localHash||!timeout||timeout>120000||!nextOperation) {
      return std::unexpected(Error{ErrorCode::InvalidFormat,"Current I2P metainfo route required"});
    }
    Contact endpoint=peer;
    if (peer.destination) {
      auto destination=decodeDestination(peer.destination->wire);
      if (!destination||destination->hash!=peer.hash) {
        return std::unexpected(Error{ErrorCode::InvalidFormat,"I2P metainfo peer identity changed"});
      }
      endpoint.destination=std::move(*destination);
    }
    std::int64_t connection=0;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&connection),static_cast<int>(sizeof(connection)))!=1||!connection) {
      return std::unexpected(Error{ErrorCode::CryptoFailure,"I2P metainfo connection unavailable"});
    }
    MetainfoTransfer transfer;
    transfer.operation=nextOperation;
    transfer.phase=Phase::Running;
    transfer.peer=std::move(endpoint);
    transfer.handler=handler;
    transfer.key=key;
    transfer.connection=connection;
    transfer.next=Clock::now();
    transfer.end=transfer.next+std::chrono::milliseconds(timeout);
    metainfo=std::move(transfer);
    nextOperation=detail::nextOperationId(nextOperation);
    schedule();
    return metainfo.operation;
  }
  void stopMetainfo(Phase phase) noexcept {
    if (metainfo.ticket&&channel) { channel->cancelDhtDatagram(metainfo.ticket); }
    if (metainfo.resolver&&channel) { channel->cancelDestinationLookup(metainfo.resolver); }
    metainfo.ticket=0;
    metainfo.resolver=0;
    metainfo.phase=phase;
    metainfo.received.clear();
    if (phase!=Phase::Complete) { metainfo.bytes.clear(); }
  }
  bool metainfoDispatch(const i2cp::InitialChannel::DhtDispatch& result) {
    if (metainfo.phase!=Phase::Running||!metainfo.ticket||result.ticket!=metainfo.ticket) { return false; }
    metainfo.ticket=0;
    metainfo.written|=result.written;
    metainfo.next=Clock::now()+(result.written?std::chrono::milliseconds(5000):std::chrono::milliseconds(250));
    return true;
  }
  void acceptMetainfo(const Contact& peer,const AzureusReadPacket& packet) {
    if (metainfo.phase!=Phase::Running||!metainfo.written||Clock::now()>=metainfo.end
        ||!peer.destination||!metainfo.peer.destination||peer.id!=metainfo.peer.id
        ||peer.hash!=metainfo.peer.hash||peer.port!=metainfo.peer.port
        ||peer.destination->wire!=metainfo.peer.destination->wire||packet.connection!=metainfo.connection
        ||packet.handler!=metainfo.handler||packet.key!=metainfo.key||packet.fragment.empty()
        ||static_cast<std::size_t>(packet.total)>MaximumMetainfo) { return; }
    const auto total=static_cast<std::size_t>(packet.total);
    const auto start=static_cast<std::size_t>(packet.start);
    if (metainfo.bytes.empty()) {
      metainfo.bytes.resize(total);
      metainfo.received.resize(total);
    }
    if (metainfo.bytes.size()!=total) { return; }
    for (std::size_t index=0;index<packet.fragment.size();++index) {
      if (metainfo.received[start+index]&&metainfo.bytes[start+index]!=packet.fragment[index]) { return; }
    }
    for (std::size_t index=0;index<packet.fragment.size();++index) {
      if (!metainfo.received[start+index]) {
        metainfo.received[start+index]=true;
        metainfo.bytes[start+index]=packet.fragment[index];
        ++metainfo.count;
      }
    }
    if (metainfo.count==total) { stopMetainfo(Phase::Complete);notify(); }
  }
  bool resolveMetainfoPeer(Clock::time_point stamp) {
    if (metainfo.peer.destination) { return true; }
    if (metainfo.resolver) {
      auto found=channel->destinationLookupObservation(metainfo.resolver);
      if (found&&found->phase==i2cp::InitialChannel::LookupPhase::Pending) { return false; }
      metainfo.resolver=0;
      if (!found||found->phase!=i2cp::InitialChannel::LookupPhase::Resolved||!found->destination||
          found->requested!=metainfo.peer.hash||found->destination->hash!=metainfo.peer.hash) {
        stopMetainfo(Phase::Failed);notify();return false;
      }
      metainfo.peer.destination=std::move(found->destination);
      return true;
    }
    const auto busy=channel->destinationLookupObservation();
    if (busy&&busy->phase==i2cp::InitialChannel::LookupPhase::Pending) { return false; }
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(metainfo.end-stamp).count();
    if (left<=0) { return false; }
    const auto lookup=channel->lookupDestination(metainfo.peer.hash,static_cast<unsigned>(std::min<std::int64_t>(left,30000)));
    if (lookup) { metainfo.resolver=*lookup; }
    else { metainfo.next=stamp+std::chrono::milliseconds(250); }
    return false;
  }
  void sendMetainfo(bool heldInbound) {
    if (metainfo.phase!=Phase::Running) { return; }
    const auto stamp=Clock::now();
    if (stamp>=metainfo.end) { stopMetainfo(Phase::TimedOut);notify();return; }
    if (stamp<metainfo.next||metainfo.ticket) { return; }
    if (!heldInbound) { channel->requestDhtEffects(scope);return; }
    if (!inboundCurrent(metainfo.end)) { return; }
    if (!resolveMetainfoPeer(stamp)) { return; }
    if (!channel->dhtScopeObservation(scope).writeSlots) {
      metainfo.next=stamp+std::chrono::milliseconds(250);
      return;
    }
    auto tx=transaction(now());
    if (!tx) { stopMetainfo(Phase::Failed);notify();return; }
    AzureusReadPacket packet{AzureusReadPacketType::Request,metainfo.handler,metainfo.key,
        QDateTime::currentMSecsSinceEpoch()+azureusClockOffset,metainfo.connection,0,0,0,{}};
    if (!metainfo.received.empty()) {
      const auto missing=std::find(metainfo.received.begin(),metainfo.received.end(),false);
      const auto present=std::find(missing,metainfo.received.end(),true);
      packet.start=static_cast<std::int32_t>(missing-metainfo.received.begin());
      packet.length=static_cast<std::int32_t>(present-missing);
    }
    Message outgoing;
    outgoing.method=Method::AzureusData;
    outgoing.id=options.localId;
    outgoing.transaction=std::move(*tx);
    outgoing.wakeRequested=true;
    outgoing.azureus=AzureusMessage{*azureusInstance,0,std::move(packet)};
    const auto bytes=encode(outgoing);
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(metainfo.end-Clock::now()).count();
    if (!bytes) { stopMetainfo(Phase::Failed);notify();return; }
    if (left<=0) { return; }
    const auto ticket=channel->submitDhtDatagram(metainfo.peer.destination->wire,*bytes,true,metainfo.peer.port,
        static_cast<unsigned>(std::min<std::int64_t>(left,30000)));
    if (ticket) { metainfo.ticket=*ticket; }
    else { metainfo.next=stamp+std::chrono::milliseconds(250); }
  }
  bool resumeInbound(){
    if(!onThread()||!inboundRequested){return false;}if(!current()){close();return false;}if(!channel->dhtScopeObservation(scope).granted){return false;}
    const bool starting=!responder;if(!prepareInbound()){stopInbound();return false;}if(starting){notify();}
    if(channel->dhtScopeObservation(scope).queued||options.routingMaintenance
        ||metainfo.phase==Phase::Running||(lookup.running()&&lookup.options().azureus&&lookup.options().announce)){pump(true);}return inboundRequested&&current();
  }
  bool begin(const Options& value,AzureusLocalRead read,AzureusMetainfoRead metainfo){
    if(!onThread()||enabled||!channel||((value.routingMaintenance||!value.savedContacts.empty())&&!value.inbound)||value.savedContacts.size()>MaximumSavedContacts||value.seeds.empty()||value.seeds.size()>20||!value.parallelism||value.parallelism>3
        ||((read||metainfo||value.azureusReplicas)&&!value.azureusValues)){return false;}
    auto identity=channel->ownedLeaseDestination();if(!identity){return false;}
    Contact local{value.localId,identity->hash,value.queryPort,{}};if(!validContact(local)){return false;}
    if(std::any_of(value.seeds.begin(),value.seeds.end(),[&](const auto& c){return !validContact(c)||c.hash==identity->hash;})){return false;}
    // Validate/canonicalize saved rows as inert bounded data before enabling
    // any DHT scope. This does not import router or source authority.
    auto encoded=encodeSavedContacts(value.savedContacts);if(!encoded){return false;}auto saved=decodeSavedContacts(*encoded);if(!saved){return false;}
    options=value;options.savedContacts=std::move(*saved);
    for(auto& c:options.seeds){if(c.destination){auto parsed=decodeDestination(c.destination->wire);if(!parsed||parsed->hash!=c.hash){return false;}c.destination=std::move(*parsed);}}
    localDestination=identity->wire;localHash=identity->hash;
    azureusInstance.reset();
    if (options.azureusValues) {
      auto& random=*QRandomGenerator::system();
      azureusInstance=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(random.generate()));
      azureusClockOffset=random.bounded(-120000,120000);
    }
    if(!channel->enableDhtDatagrams(value.queryPort)){return false;}scope=channel->dhtScopeGeneration();enabled=true;
    if ((value.inbound||value.azureusReplicas||read||metainfo)&&!enableInbound(value.inbound)) { close();return false; }
    azureusLocalRead=std::move(read);azureusMetainfoRead=std::move(metainfo);notify();return true;
  }
  Result<std::uint64_t> start(const Id& target,bool seed,bool announce,unsigned timeout,std::optional<AzureusLookup> azureus={}){
    if(!onThread()||!current()||lookup.running()||(azureus&&!azureusInstance)){return std::unexpected(Error{ErrorCode::InvalidFormat,"Current idle explicitly bound DHT actor required"});}
    if (azureus&&announce&&(!azureusLocalRead||!inboundCurrent(Clock::time_point::max())||!azureusLocalRead(target))) {
      return std::unexpected(Error{ErrorCode::InvalidFormat,"Current source-owned Azureus publication required"});
    }
    if(!nextOperation){return std::unexpected(Error{ErrorCode::TooLarge,"DHT operation identifier space exhausted"});}
    cancelPending();
    const auto operation=nextOperation;
    if(!lookup.start(operation,{target,seed,announce,timeout,options.parallelism,azureus},options.seeds,now())){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid finite DHT lookup options"});}
    if(options.routingMaintenance){pauseRefresh();}
    nextOperation=detail::nextOperationId(operation);if(options.routingMaintenance){maintenanceApplicationLookup=std::pair{operation,target};}pump();return operation;
  }
  void pump(bool heldInbound=false){
    if(pumping||!onThread()||!enabled){return;}
    if(!current()){close();return;}pumping=true;struct Guard{bool& v;~Guard(){v=false;}} guard{pumping};
    const bool clientWasRunning=lookup.running();bool progressed=false;lookup.tick(now());expireMaintenance(now());if(!current()){return;}
    for(std::size_t steps=0;steps<8;++steps){auto result=channel->takeDhtDispatch();if(!result){break;}progressed=true;
      auto server=std::find(replyTickets.begin(),replyTickets.end(),result->ticket);
      if(server!=replyTickets.end()){replyTickets.erase(server);}else if(!metainfoDispatch(*result)&&!maintenanceDispatch(*result)){lookup.dispatched(result->ticket,result->written,now());}
    }
    const bool receiveNow=!inboundRequested||(heldInbound&&channel->dhtScopeObservation(scope).granted);
    if(inboundRequested&&!receiveNow&&channel->dhtScopeObservation(scope).queued){channel->requestDhtEffects(scope);}
    for(std::size_t steps=0;receiveNow&&steps<8;++steps){
      if(inboundRequested&&(!inboundCurrent(Clock::time_point::max())||!channel->dhtScopeObservation(scope).writeSlots)){break;}
      auto data=channel->takeDhtDatagram();if(!data){break;}progressed=true;if(Clock::now()>=data->end){continue;}
      auto message=decode(data->payload);if(!message){continue;}
      if(message->kind==Kind::Query){
        if (message->method==Method::AzureusData) { metainfoPacket(*data,*message);continue; }
        const bool allowed=message->method==Method::AzureusStore?options.azureusReplicas
            :fullInbound||message->method==Method::AzureusFindValue;
        if (inboundRequested&&allowed) { inboundPacket(*data,message->version); }
        continue;
      }
      if(data->sender){continue;}
      std::erase_if(message->nodes,[&](const auto& c){return c.hash==localHash||c.id==options.localId;});
      if(!message->azureus&&maintenanceReply(*message,data->fromPort,data->end)){continue;}
      if (message->azureus) {
        if (auto* contacts=std::get_if<std::vector<AzureusContact>>(&message->azureus->payload)) {
          std::erase_if(*contacts,[&](const auto& c){return c.contact.hash==localHash||c.contact.id==options.localId;});
        }
      }
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
      std::optional<AzureusValue> publication;
      const bool publishing=lookup.options().azureus&&lookup.options().announce;
      if (publishing) {
        if (!heldInbound) { channel->requestDhtEffects(scope);break; }
        if (!inboundCurrent(Clock::time_point::max())||!azureusLocalRead
            ||!(publication=azureusLocalRead(lookup.options().target))) { lookup.cancel();break; }
      }
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
      if (const auto& azureus=lookup.options().azureus) {
        const auto wall=QDateTime::currentMSecsSinceEpoch()+azureusClockOffset;
        if (publication&&node.phase==NodePhase::Replied) {
          publication->created=wall;
          message.method=Method::AzureusStore;
          message.azureus=AzureusMessage{*azureusInstance,0,AzureusStore{{{message.target,{std::move(*publication)}}},wall}};
        } else {
          message.method=Method::AzureusFindValue;
          message.azureus=AzureusMessage{*azureusInstance,0,AzureusFindValue{message.target,azureus->maximum,azureus->flags,wall}};
        }
      }
      auto packet=encode(message);if(!packet){lookup.failed(*index);continue;}
      const auto left=lookup.end()-now();if(left<=0){lookup.tick(now());break;}
      // announce_peer is raw18 to target Q+1; get_peers is signed17 to Q.
      const bool signedQuery=message.method!=Method::AnnouncePeer;
      const auto port=std::uint16_t(node.contact.port+(signedQuery?0:1));
      auto ticket=channel->submitDhtDatagram(node.contact.destination->wire,*packet,signedQuery,port,unsigned(std::min<std::int64_t>(30000,left)));
      if(!ticket){lookup.failed(*index);continue;}if(!lookup.queued(*index,std::move(*tx),*ticket,intended)){channel->cancelDhtDatagram(*ticket);}else{progressed=true;}
    }
    lookup.tick(now());
    sendMetainfo(heldInbound);
    if(heldInbound){maintainRouting();}if(!current()){return;}
    if(maintenanceNeedsGrant(now())){channel->requestDhtEffects(scope);}
    if(!lookup.running()){cancelPending();}
    auto end=maintenanceWake(now());
    if (metainfo.phase==Phase::Running) {
      auto wake=metainfo.end;
      if (!metainfo.ticket) {
        wake=std::min(wake,std::max(metainfo.next,Clock::now()+std::chrono::milliseconds(100)));
      }
      const std::int64_t millis=std::chrono::duration_cast<std::chrono::milliseconds>(wake.time_since_epoch()).count();
      end=end?std::min(*end,millis):millis;
    }
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
bool Actor::begin(const Options& value,AzureusLocalRead read,AzureusMetainfoRead metainfo){
  try{return impl_->begin(value,std::move(read),std::move(metainfo));}catch(...){impl_->safeClose();return false;}}
Result<std::uint64_t> Actor::lookupPeers(const Id& target,bool seed,unsigned timeout){try{return impl_->start(target,seed,false,timeout);}catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}}
Result<std::uint64_t> Actor::announceSelf(const Id& target,bool seed,unsigned timeout){try{return impl_->start(target,seed,true,timeout);}catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}}
Result<std::uint64_t> Actor::lookupAzureusValues(const Id& target,AzureusLookup options,unsigned timeout){
  try{return impl_->start(target,false,false,timeout,options);}
  catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}
}
Result<std::uint64_t> Actor::publishAzureusValue(const Id& target,AzureusLookup options,unsigned timeout){
  try{return impl_->start(target,false,true,timeout,options);}
  catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}
}
Result<std::uint64_t> Actor::readAzureusMetainfo(const Contact& peer,const Id& handler,const Id& key,unsigned timeout){
  try{return impl_->startMetainfo(peer,handler,key,timeout);}
  catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"I2P metainfo reader unavailable"});}
}
Actor::MetainfoObservation Actor::metainfoObservation()const {
  if (!impl_->onThread()) { return {}; }
  const auto& transfer=impl_->metainfo;
  return {transfer.operation,transfer.phase,transfer.phase==Phase::Complete?transfer.bytes:Bytes{}};
}
bool Actor::enableInbound(){try{return impl_->enableInbound(true);}catch(...){impl_->stopInbound();return false;}}
void Actor::stopInbound(){if(impl_->onThread()){impl_->stopInbound();}}
bool Actor::inboundEnabled()const noexcept{return impl_->onThread()&&impl_->enabled&&impl_->inboundRequested&&bool(impl_->responder)&&impl_->channel&&impl_->scope==impl_->channel->dhtScopeGeneration();}
bool Actor::resumeAuthorizedInbound(){try{return impl_->resumeInbound();}catch(...){impl_->stopInbound();return false;}}
bool Actor::cancel(std::uint64_t op){try{
  if (!impl_->onThread()) { return false; }
  if (impl_->metainfo.phase==Phase::Running&&impl_->metainfo.operation==op) {
    impl_->stopMetainfo(Phase::Cancelled);impl_->schedule();impl_->notify();return true;
  }
  if (!impl_->lookup.running()||impl_->lookup.snapshot().operation!=op) { return false; }
  impl_->lookup.cancel();impl_->cancelPending();impl_->timer.stop();
  if(impl_->options.routingMaintenance||impl_->metainfo.phase==Phase::Running){impl_->schedule();}impl_->notify();return true;
}catch(...){impl_->safeClose();return false;}}
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
