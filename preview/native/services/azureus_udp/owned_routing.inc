// Included in namespace azureus_udp. The normal actor owns all RPC/contact
// state; no external seeded/closest snapshots or completion callbacks exist.
struct OwnedPingTransport::Routing final {
  std::uint64_t id=0;
  Clock::time_point deadline;
  azureus_control::LookupController controller;
  struct Rpc {std::uint64_t node=0;Ticket ticket;Peer peer;bool received=false;};
  std::array<Rpc,azureus_control::LookupController::MaximumParallelism> rpcs{};
  bool finished=false,finalizing=false,integration=true;
  std::shared_ptr<RoutingSnapshot> terminalFailure=std::make_shared<RoutingSnapshot>();
  std::optional<Failure> failure;
};
bool OwnedPingTransport::ownedRoutingPeer(const Peer& peer)const noexcept{
  return dynamicRoutingScope(config_.routingScope)&&state_==State::Bound
      &&peer.port&&(config_.routingScope==RoutingScope::PublicBound?publicAddress(peer.address):peer.address.isLoopback())&&peer.address.scopeId().isEmpty()
      &&peer.address.protocol()==socket_.localAddress().protocol()&&peer!=endpoint();
}
bool OwnedPingTransport::isRoutingPeer(const Peer& peer)const noexcept{
  return owner()&&ownedRoutingPeer(peer)&&allowed(peer)&&bool(normalRouter_);
}
std::optional<Peer> OwnedPingTransport::routingPeer(const find::nodes::Contact& contact)const{
  if(contact.protocol<51||contact.protocol>55||!contact.address.port){return {};}
  Peer peer;peer.port=contact.address.port;
  if(contact.address.length==4){quint32 ip=0;for(unsigned i=0;i<4;++i){ip=(ip<<8)|contact.address.bytes[i];}peer.address=QHostAddress(ip);}
  else if(contact.address.length==16){Q_IPV6ADDR ip;std::copy_n(contact.address.bytes.begin(),16,ip.c);peer.address=QHostAddress(ip);}
  else{return {};}
  return ownedRoutingPeer(peer)?std::optional{peer}:std::nullopt;
}
std::expected<void,Failure> OwnedPingTransport::attachKnownPeer(Peer peer,std::int8_t protocolHint,Clock::time_point absoluteEnd){
  if(!owner()){return std::unexpected(Failure::Thread);}
  if(protocolHint<51||protocolHint>55){return std::unexpected(Failure::Version);}
  if(Clock::now()>=absoluteEnd){return std::unexpected(Failure::Deadline);}
  const auto prior=knownDeadline_;knownDeadline_=absoluteEnd;const auto restore=qScopeGuard([&]{knownDeadline_=prior;});
  if(!normalCurrent()||!ownedRoutingPeer(peer)){return std::unexpected(Failure::Authority);}
  find::nodes::Contact contact;contact.address=originator(peer);contact.protocol=protocolHint;
  const auto id=nodeCache_->lookup({contact.address,{}},protocolHint);if(!id){return std::unexpected(Failure::Authority);}contact.id=id->id;
  if(!localId_||contact.id==*localId_){return std::unexpected(Failure::Authority);}
  if(std::ranges::any_of(normalContacts_,[&](const auto& old){return old.id==contact.id&&old.address!=contact.address;})){return std::unexpected(Failure::Authority);}
  if((!allowed(peer)&&peers_.size()==config_.maximumPeers)||(std::ranges::find(normalContacts_,contact.address,&find::nodes::Contact::address)==normalContacts_.end()&&normalContacts_.size()==config_.maximumPeers)){return std::unexpected(Failure::Capacity);}
  if(!learnRoutingContact(contact,false,RoutingProtocolSource::ProbeHint)){return std::unexpected(Failure::Authority);}
  dispatchRoutingPings();captureRouting();return {};
}
bool OwnedPingTransport::learnRoutingContact(const find::nodes::Contact& supplied,bool alive,RoutingProtocolSource source){
  if(!normalCurrent()||!localId_||supplied.id==*localId_||learnedRouting_==UINT64_MAX){return false;}
  const auto peer=routingPeer(supplied);if(!peer){return false;}
  const auto id=nodeCache_->lookup(find::nodes::ResolvedEndpoint{supplied.address,{}},supplied.protocol);
  if(!id||id->id!=supplied.id){return false;}
  // Restricted IDs can alias endpoints. Never replace a different attachment.
  for(const auto& old:normalContacts_){if(old.id==supplied.id&&old.address!=supplied.address){return false;}}
  const auto previous=std::ranges::find(normalContacts_,supplied.address,&find::nodes::Contact::address);
  const bool inserted=previous==normalContacts_.end(),alreadyAllowed=allowed(*peer);
  if((inserted&&normalContacts_.size()==config_.maximumPeers)||(!alreadyAllowed&&peers_.size()==config_.maximumPeers)){return false;}
  // Finish allocations BEFORE permission/router mutation. The attachment is in
  // place before contactKnown can enqueue NodeAdded, including replacement IDs.
  const auto index=inserted?normalContacts_.size():static_cast<std::size_t>(previous-normalContacts_.begin());
  normalContacts_.reserve(config_.maximumPeers);peers_.reserve(config_.maximumPeers);
  const auto old=inserted?std::optional<NormalContact>{}:std::optional<NormalContact>{normalContacts_[index]};
  NormalContact contact(supplied,alive?RoutingProtocolSource::Packet:source);
  // A repeated hint/third-party record cannot downgrade an endpoint observation.
  if(old&&!alive&&static_cast<unsigned>(old->protocolSource)>static_cast<unsigned>(contact.protocolSource)){contact=*old;}
  // NodeCache/allocations can outlive the permit or an external close. This
  // is the final preparation fence BEFORE its first attachment/permission effect.
  if(!normalCurrent()){return false;}
  if(inserted){normalContacts_.push_back(contact);}else{normalContacts_[index]=contact;}
  const auto rollback=[&]{if(inserted){normalContacts_.pop_back();}else{normalContacts_[index]=*old;}if(!alreadyAllowed){peers_.pop_back();}};
  if(!allowPeer(*peer)){if(inserted){normalContacts_.pop_back();}else{normalContacts_[index]=*old;}return false;}
  if(!normalCurrent()){rollback();return false;}
  if(alive){if(!normalAlive(contact)){rollback();return false;}}
  else{
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    const auto known=normalRouter_->contactKnown({.id=contact.id,.instance=contact.instance,.sleeping=(static_cast<std::uint8_t>(contact.flags)&1U)!=0},now);
    const auto retained=normalRouter_->find(contact.id);
    if(!known||*known==azureus_router::RouterChange::Dropped||!retained||!*retained){rollback();return false;}
  }
  ++learnedRouting_;return true;
}
void OwnedPingTransport::captureRouting()noexcept{
  const auto failed=[&]()noexcept{
    normalSeeded_=false;routingSnapshot_.reset();if(!routing_){return;}routing_->failure=Failure::Capacity;
    try{if(!routing_->finished){static_cast<void>(routing_->controller.cancel(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()));}}catch(...){}
    if(routing_->finished&&routing_->terminalFailure){auto value=std::move(routing_->terminalFailure);value->generation=statisticsGeneration_;value->operation=routing_->id;value->finished=true;value->failure=Failure::Capacity;routingSnapshot_=std::move(value);}
  };
  try{
    if(!dynamicRoutingScope(config_.routingScope)){routingSnapshot_.reset();return;}
    if(routingRevision_==UINT64_MAX){failed();return;}
    auto snapshot=std::make_shared<RoutingSnapshot>();snapshot->generation=statisticsGeneration_;snapshot->operation=routing_?routing_->id:0;snapshot->revision=++routingRevision_;
    for(const auto& contact:normalContacts_){if(snapshot->contactCount==snapshot->contacts.size()){failed();return;}snapshot->contacts[snapshot->contactCount++]={contact,contact.protocolSource};}
    snapshot->finished=routing_&&routing_->finished;snapshot->seeded=normalSeeded_;snapshot->learned=learnedRouting_;snapshot->rejected=rejectedRouting_;snapshot->pings=routingPings_;
    if(routing_){auto lookup=routing_->controller.snapshot();if(!lookup){failed();return;}snapshot->lookup=std::make_shared<const azureus_control::LookupSnapshot>(std::move(*lookup));snapshot->failure=routing_->failure;}
    routingSnapshot_=std::move(snapshot);
  }catch(...){failed();}
}
std::shared_ptr<const RoutingSnapshot> OwnedPingTransport::routingSnapshot()const noexcept{
  return owner()?routingSnapshot_:nullptr;
}
void OwnedPingTransport::routingFailed(Failure failure)noexcept{
  normalSeeded_=false;
  if(routing_&&!routing_->finished){routing_->failure=failure;try{static_cast<void>(routing_->controller.cancel(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()));pumpRouting(routing_);}catch(...){routingSnapshot_.reset();}}
  else{captureRouting();}
}
void OwnedPingTransport::answerRoutingPing(const Peer& peer,packet::Decoded request,std::int64_t receivedAt){
  auto completion=[this,peer,request=std::move(request),receivedAt](bool accepted)mutable{
    if(state_!=State::Bound||!normalRouter_){return;}if(!accepted){++statistics_.authorityErrors;return;}
    const auto& header=request.header().fields();
    if(!normalCurrent()||!header.originator||*header.originator!=originator(peer)){++statistics_.serviceAddressRejected;return;}
    const auto id=nodeCache_->lookup(find::nodes::ResolvedEndpoint{*header.originator,{}},header.originatorVersion);if(!id){++statistics_.authorityErrors;return;}
    find::nodes::Contact contact;contact.id=id->id;contact.address=*header.originator;contact.protocol=header.originatorVersion;contact.instance=header.instance;contact.flags=header.flags;
    if(!learnRoutingContact(contact,true)){++statistics_.authorityErrors;return;}answer(peer,request,receivedAt);dispatchRoutingPings();captureRouting();
  };
  if(!normalPreflight_||!normalPreflight_(std::move(completion))){++statistics_.serviceCapacityDrops;}
}
std::expected<std::uint64_t,Failure> OwnedPingTransport::integrateRouting(Peer seed,std::chrono::nanoseconds timeout){
  const auto entered=Clock::now();
  if(!owner()){return std::unexpected(Failure::Thread);}
  if(!normalCurrent()||!normalPreflight_||!ownedRoutingPeer(seed)||!localId_){return std::unexpected(Failure::Authority);}
  if(timeout<=std::chrono::nanoseconds::zero()||timeout>std::chrono::seconds(30)){return std::unexpected(Failure::Deadline);}
  const auto end=entered+timeout;
  if((routing_&&!routing_->finished)||nextRouting_==UINT64_MAX){return std::unexpected(Failure::Capacity);}
  auto contact=normalDatabase_->localContact();if(!contact||!*contact){return std::unexpected(Failure::State);}
  find::nodes::Contact first;first.address=originator(seed);first.protocol=config_.version;
  const auto derived=nodeCache_->lookup(find::nodes::ResolvedEndpoint{first.address,{}},first.protocol);if(!derived){return std::unexpected(Failure::Authority);}first.id=derived->id;
  if(Clock::now()>=end){return std::unexpected(Failure::Deadline);}
  if(first.id==*localId_||!learnRoutingContact(first,false,RoutingProtocolSource::ProbeHint)){return std::unexpected(Failure::Authority);}
  return startRouting(*localId_,end,true);
}
std::expected<std::uint64_t,Failure> OwnedPingTransport::startRouting(find::nodes::NodeId target,Clock::time_point end,bool integration){
  if(!normalCurrent()||!normalPreflight_||!localId_){return std::unexpected(Failure::Authority);}
  if((routing_&&!routing_->finished)||nextRouting_==UINT64_MAX){return std::unexpected(Failure::Capacity);}
  auto round=std::make_shared<Routing>();round->id=++nextRouting_;round->deadline=end;round->integration=integration;
  const auto remaining=end-Clock::now();if(remaining<=Clock::duration::zero()){return std::unexpected(Failure::Deadline);}
  auto closest=normalRouter_->closest(target,20,false);if(!closest){return std::unexpected(Failure::State);}
  std::vector<azureus_control::AdmittedContact> seeds;
  for(std::size_t i=0;i<closest->count;++i){const auto& candidate=closest->values[i];if(candidate.local){continue;}const auto attachment=std::ranges::find(normalContacts_,candidate.id,&find::nodes::Contact::id);if(attachment!=normalContacts_.end()&&routingPeer(*attachment)){seeds.push_back({*attachment,true});}}
  const auto less=[&](const auto& a,const auto& b){for(std::size_t i=0;i<target.size();++i){const auto x=a.contact.id[i]^target[i],y=b.contact.id[i]^target[i];if(x!=y){return x<y;}}return false;};std::sort(seeds.begin(),seeds.end(),less);if(seeds.size()>20){seeds.resize(20);}if(seeds.empty()){return std::unexpected(Failure::Peer);}
  azureus_control::LookupOptions options;options.kind=azureus_control::LookupKind::Node;if(config_.routingScope==RoutingScope::PublicBound){options.addressScope=azureus_control::LookupAddressScope::PublicNode;}options.target=target;options.localId=*localId_;options.parallelism=integration?8:5;options.accuracy=integration?20:10;options.maximumValues=1;options.timeoutMillis=std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();if(!normalRouter_->lookupStarted(QDateTime::currentMSecsSinceEpoch(),now)){return std::unexpected(Failure::State);}if(!round->controller.start(statisticsGeneration_,round->id,options,seeds,now)){return std::unexpected(Failure::State);}
  routing_=round;routingDeadline_.setInterval(std::max(Clock::duration::zero(),end-Clock::now()));routingDeadline_.start();pumpRouting(round);return round->id;
}

bool OwnedPingTransport::cancelRouting(std::uint64_t id){
  if(!owner()||!routing_||routing_->id!=id||routing_->finished){return false;}
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();routing_->failure=Failure::Cancelled;static_cast<void>(routing_->controller.cancel(now));pumpRouting(routing_);return true;
}
void OwnedPingTransport::finishRouting(const std::shared_ptr<Routing>& round){
  if(routing_!=round||round->finished){return;}
  const auto state=round->controller.state();
  if(!azureus_control::lookupTerminal(state)||std::ranges::any_of(round->rpcs,[](const auto& rpc){return rpc.node!=0;})){captureRouting();return;}
  if(state==azureus_control::LookupState::Complete&&!round->failure){
    // This call is reached only from a currently granted source permit. An
    // exhausted result can be empty, exactly like the original closest callback.
    if(Clock::now()>=round->deadline){round->failure=Failure::Timeout;round->finished=true;routingDeadline_.stop();captureRouting();return;}
    if(!normalCurrent()){
      if(round->finalizing){return;}round->finalizing=true;
      if(!normalPreflight_||!normalPreflight_([this,round](bool accepted){if(routing_!=round||round->finished){return;}round->finalizing=false;if(!accepted||!normalCurrent()){round->failure=Failure::Authority;round->finished=true;routingDeadline_.stop();captureRouting();return;}finishRouting(round);})){round->failure=Failure::Capacity;round->finished=true;routingDeadline_.stop();captureRouting();}return;
    }
    const auto actual=normalRouter_->snapshot();if(!actual||actual->limited()){round->failure=Failure::Capacity;round->finished=true;routingDeadline_.stop();captureRouting();return;}
    if(round->integration){normalSeeded_=true;const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();if(!normalRouter_->scheduleSeedRefresh(now)){round->failure=Failure::State;normalSeeded_=false;}}
  }else if(!round->failure){round->failure=state==azureus_control::LookupState::TimedOut?Failure::Timeout:state==azureus_control::LookupState::Closed?Failure::Closed:Failure::Cancelled;}
  round->finished=true;routingDeadline_.stop();captureRouting();
}
void OwnedPingTransport::pumpRouting(const std::shared_ptr<Routing>& round){
  if(routing_!=round||round->finished){return;}
  const auto instant=Clock::now();const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(instant.time_since_epoch()).count();
  if(instant>=round->deadline){round->failure=Failure::Timeout;static_cast<void>(round->controller.cancel(now));}
  auto intents=round->controller.takeIntents(now);if(!intents){round->failure=Failure::State;static_cast<void>(round->controller.cancel(now));}
  // No new send without a fresh source permit. Deadline/cancel processing
  // below is pure and may always progress while the I/O worker is blocked.
  if(intents&&!intents->empty()){
    if(!normalCurrent()){for(const auto& intent:*intents){static_cast<void>(round->controller.finish(intent.node,azureus_control::LookupNodeState::Failed,now));}round->failure=Failure::Authority;static_cast<void>(round->controller.cancel(now));}
    else{for(const auto& intent:*intents){
      auto free=std::ranges::find(round->rpcs,std::uint64_t{0},&Routing::Rpc::node);const auto peer=routingPeer(intent.contact);
      if(free==round->rpcs.end()||!peer||!allowed(*peer)){static_cast<void>(round->controller.finish(intent.node,azureus_control::LookupNodeState::Rejected,now));continue;}
      const auto remaining=round->deadline-Clock::now();if(remaining<=Clock::duration::zero()){static_cast<void>(round->controller.finish(intent.node,azureus_control::LookupNodeState::Rejected,now));continue;}
      free->node=intent.node;free->peer=*peer;
      if(!normalRouter_->recordLookup(intent.target,QDateTime::currentMSecsSinceEpoch(),now)){*free={};round->failure=Failure::State;static_cast<void>(round->controller.finish(intent.node,azureus_control::LookupNodeState::Failed,now));continue;}
      // A refresh has an overall five-minute budget; each actual NODE RPC
      // retains the transport's independent thirty-second request ceiling.
      const auto requestBudget=std::min(remaining,std::chrono::duration_cast<Clock::duration>(std::chrono::seconds{30}));
      auto sent=findNode(*peer,intent.contact.protocol,intent.target,this,[this,round,node=intent.node](FindNodeOutcome outcome){routingReply(round,node,std::move(outcome));},requestBudget);
      if(!sent){*free={};static_cast<void>(round->controller.finish(intent.node,azureus_control::LookupNodeState::Failed,now));round->failure=sent.error();continue;}
      free->ticket=*sent;if(!round->controller.sent(intent.node,sent->value,now)){round->failure=Failure::State;static_cast<void>(round->controller.cancel(now));}
    }}
  }
  if(round->failure){static_cast<void>(round->controller.cancel(now));}
  if(round->controller.state()!=azureus_control::LookupState::Running){for(auto& rpc:round->rpcs){if(!rpc.node){continue;}
    if(rpc.received){static_cast<void>(round->controller.finish(rpc.node,azureus_control::LookupNodeState::Cancelled,now));rpc={};}
    else{static_cast<void>(cancel(rpc.ticket));} // false may mean a real callback is already queued.
  }}
  finishRouting(round);
}
void OwnedPingTransport::routingReply(const std::shared_ptr<Routing>& round,std::uint64_t node,FindNodeOutcome outcome){
  if(routing_!=round||round->finished){return;}
  auto rpc=std::ranges::find(round->rpcs,node,&Routing::Rpc::node);if(rpc==round->rpcs.end()){return;}rpc->received=true;const auto peer=rpc->peer;
  if(round->controller.state()!=azureus_control::LookupState::Running||Clock::now()>=round->deadline){pumpRouting(round);return;}
  auto resumed=[this,round,node,peer,outcome=std::move(outcome)](bool accepted)mutable{
    if(routing_!=round||round->finished){return;}auto live=std::ranges::find(round->rpcs,node,&Routing::Rpc::node);if(live==round->rpcs.end()){return;}
    // Profile I/O may finish after the timer became due but before its Qt
    // event. Settle the actual received slot before touching any contact.
    if(round->controller.state()!=azureus_control::LookupState::Running||Clock::now()>=round->deadline){pumpRouting(round);return;}
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    if(!accepted||!normalCurrent()){round->failure=Failure::Authority;static_cast<void>(round->controller.finish(node,azureus_control::LookupNodeState::Failed,now));static_cast<void>(round->controller.cancel(now));}
    else if(!outcome){
      if(outcome.error()==Failure::Timeout){const auto id=nodeCache_->lookup(find::nodes::ResolvedEndpoint{originator(peer),{}},config_.version);if(id){static_cast<void>(normalRouter_->contactDead(id->id,now));}}
      static_cast<void>(round->controller.finish(node,outcome.error()==Failure::Timeout?azureus_control::LookupNodeState::Timeout:azureus_control::LookupNodeState::Failed,now));
    }else{
      const auto& header=outcome->header().fields();const auto id=nodeCache_->lookup(find::nodes::ResolvedEndpoint{originator(peer),{}},header.version);const auto* body=std::get_if<find::ReplyBody>(&outcome->body());
      if(!id||!body||body->providerFailures){round->failure=Failure::Authority;static_cast<void>(round->controller.finish(node,azureus_control::LookupNodeState::Failed,now));}
      else{
        find::nodes::Contact respondent;respondent.id=id->id;respondent.address=originator(peer);respondent.protocol=header.version;respondent.instance=header.instance;respondent.flags=header.flags;
        std::vector<azureus_control::AdmittedContact> contacts;contacts.reserve(body->contacts.size());
        if(!learnRoutingContact(respondent,true)){round->failure=Failure::Authority;}
        for(const auto& contact:body->contacts){
          if(localId_&&contact.id==*localId_){continue;}
          if(!routingPeer(contact)){if(rejectedRouting_==UINT64_MAX){round->failure=Failure::Capacity;break;}++rejectedRouting_;continue;}
          if(!learnRoutingContact(contact,false)){round->failure=Failure::Capacity;break;}contacts.push_back({contact,true});
        }
        if(round->failure){static_cast<void>(round->controller.finish(node,azureus_control::LookupNodeState::Failed,now));static_cast<void>(round->controller.cancel(now));}
        else{static_cast<void>(round->controller.contacts(node,contacts,false,now));}
      }
    }
    if(round->failure){static_cast<void>(round->controller.cancel(now));}
    *live={};if(!round->failure){dispatchRoutingPings();}pumpRouting(round);
  };
  if(!normalPreflight_||!normalPreflight_(std::move(resumed))){round->failure=Failure::Capacity;const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();static_cast<void>(round->controller.finish(node,azureus_control::LookupNodeState::Failed,now));*rpc={};static_cast<void>(round->controller.cancel(now));pumpRouting(round);}
}
void OwnedPingTransport::dispatchRoutingPings(){
  if(!dynamicRoutingScope(config_.routingScope)||!normalCurrent()){return;}
  const auto intents=normalRouter_->takeIntents();if(!intents){routingFailed(Failure::State);return;}
  if(intents->count>64-routingIntents_.size()){routingFailed(Failure::Capacity);return;}
  for(std::size_t i=0;i<intents->count;++i){routingIntents_.push_back(intents->values[i]);}
  while(!routingIntents_.empty()){
    const auto free=std::find_if(routingPingsPending_.begin(),routingPingsPending_.end(),[](const auto& value){return !value;});if(free==routingPingsPending_.end()){break;}
    const auto intent=routingIntents_.front();routingIntents_.pop_front();const auto state=normalRouter_->find(intent.id);if(!state||!*state){continue;}
    if((**state).local||(intent.kind==azureus_router::RouterIntentKind::NodeAdded&&(**state).hasBeenAlive)){continue;}
    if(std::ranges::any_of(routingPingsPending_,[&](const auto& old){return old&&old->id==intent.id;})){continue;}
    const auto found=std::ranges::find(normalContacts_,intent.id,&find::nodes::Contact::id);if(found==normalContacts_.end()){routingFailed(Failure::Authority);return;}const auto peer=routingPeer(*found);if(!peer){routingFailed(Failure::Authority);return;}
    const auto index=static_cast<std::size_t>(free-routingPingsPending_.begin());const auto protocol=found->protocol;
    auto sent=ping(*peer,protocol,this,[this,index,id=intent.id,peer=*peer](Outcome outcome){
      routingPingsPending_[index].reset();if(state_!=State::Bound||!normalPreflight_){return;}
      if(!normalPreflight_([this,id,peer,outcome=std::move(outcome)](bool accepted)mutable{
        if(state_!=State::Bound){return;}if(!accepted||!normalCurrent()){routingFailed(Failure::Authority);return;}
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        if(outcome){find::nodes::Contact contact;contact.id=id;contact.address=originator(peer);contact.protocol=outcome->header().fields().version;contact.instance=outcome->header().fields().instance;contact.flags=outcome->header().fields().flags;if(!learnRoutingContact(contact,true)){routingFailed(Failure::Authority);return;}}
        else if(outcome.error()==Failure::Timeout){static_cast<void>(normalRouter_->contactDead(id,now));}
        dispatchRoutingPings();captureRouting();
      })){routingFailed(Failure::Capacity);}
    });
    if(!sent){routingFailed(sent.error());return;}*free=RoutingPing{*sent,intent.id};if(routingPings_==UINT64_MAX){routingFailed(Failure::Capacity);return;}++routingPings_;
  }
}
