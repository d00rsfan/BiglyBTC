// Inside Actor::Impl: one channel consumer and one timer, no IP transport.
  std::deque<azureus_router::RouterIntent> maintenanceIntents;
  std::deque<detail::RoutingRefresh::Target> maintenanceTargets;
  std::array<std::optional<Node>,detail::RoutingParallelism> maintenancePings;
  detail::RoutingRefresh maintenanceRefresh;
  std::vector<Id> maintenanceDead;
  std::uint64_t maintenanceResolver=0;
  std::size_t maintenanceResolveIndex=0;
  bool maintenanceResolvePing=false;
  std::int64_t maintenanceNextTick=0;
  std::optional<std::pair<std::uint64_t,Id>> maintenanceApplicationLookup;
  std::optional<std::int64_t> maintenanceTransactionWake;
  bool maintenanceEnabled()const{return options.routingMaintenance&&inboundRequested&&bool(router);}
  Node* maintenanceResolvingNode(){
    if(maintenanceResolvePing){return maintenanceResolveIndex<maintenancePings.size()&&maintenancePings[maintenanceResolveIndex]?&*maintenancePings[maintenanceResolveIndex]:nullptr;}
    return maintenanceResolveIndex<maintenanceRefresh.nodes().size()?&maintenanceRefresh.nodes()[maintenanceResolveIndex]:nullptr;
  }
  void stopMaintenance()noexcept{
    if(channel){if(maintenanceResolver){channel->cancelDestinationLookup(maintenanceResolver);}for(auto& node:maintenancePings){if(node&&node->ticket){channel->cancelDhtDatagram(node->ticket);}}for(auto& node:maintenanceRefresh.nodes()){if(node.ticket){channel->cancelDhtDatagram(node.ticket);}}}
    maintenanceResolver=0;for(auto& node:maintenancePings){node.reset();}maintenanceRefresh.clear();maintenanceIntents.clear();maintenanceTargets.clear();maintenanceDead.clear();maintenanceNextTick=0;maintenanceTransactionWake.reset();maintenanceApplicationLookup.reset();
  }
  void pauseRefresh(){
    if(maintenanceResolver&&!maintenanceResolvePing){channel->cancelDestinationLookup(maintenanceResolver);maintenanceResolver=0;}
    for(auto& node:maintenanceRefresh.nodes()){if(node.ticket){channel->cancelDhtDatagram(node.ticket);}}
    maintenanceRefresh.pause();
  }
  void expireMaintenance(std::int64_t stamp){
    if(!maintenanceEnabled()){return;}
    for(auto& node:maintenancePings){if(node&&node->end&&stamp>=node->end&&node->phase!=NodePhase::Failed){
      if(node->ticket){channel->cancelDhtDatagram(node->ticket);}if(node->phase==NodePhase::Sent||node->phase==NodePhase::Resolving){node->phase=NodePhase::Failed;}
      else{node->phase=NodePhase::Candidate;node->end=0;node->ticket=0;node->transaction.clear();} // Local unsent pressure is not peer death.
    }}
    for(const auto& node:maintenanceRefresh.nodes()){if(node.phase==NodePhase::Sent&&stamp>=node.end){if(maintenanceDead.size()==MaximumContacts){stopInbound();return;}maintenanceDead.push_back(node.contact.id);}}
    maintenanceRefresh.tick(stamp);
    if(maintenanceResolver){auto* node=maintenanceResolvingNode();if(!node||node->phase==NodePhase::Failed||(!maintenanceRefresh.running()&&!maintenanceResolvePing)){channel->cancelDestinationLookup(maintenanceResolver);maintenanceResolver=0;}}
    while(!maintenanceTargets.empty()&&stamp>=maintenanceTargets.front().end){maintenanceTargets.pop_front();}
  }
  bool maintenanceDispatch(const i2cp::InitialChannel::DhtDispatch& value){
    for(auto& node:maintenancePings){if(node&&node->ticket==value.ticket){if(node->phase==NodePhase::Queued){if(value.written){node->phase=NodePhase::Sent;}else{node->phase=NodePhase::Candidate;node->end=0;node->ticket=0;node->transaction.clear();}}return true;}}
    for(auto& node:maintenanceRefresh.nodes()){if(node.ticket==value.ticket){if(node.phase==NodePhase::Queued){node.phase=value.written?NodePhase::Sent:NodePhase::Failed;}return true;}}return false;
  }
  bool maintenanceReply(const Message& message,std::uint16_t port,Clock::time_point receiveEnd){
    if(!maintenanceEnabled()||!inboundCurrent(receiveEnd)){return false;}
    for(auto& node:maintenancePings){
      if(!node||node->transaction.empty()||node->transaction!=message.transaction||node->contact.port!=port){continue;}
      if(node->phase!=NodePhase::Sent||now()>=node->end){return true;}
      if(message.kind==Kind::Reply&&message.id==node->contact.id){if(!learnRouting(node->contact,true,receiveEnd)||!rememberRoutingProtocol(node->contact,message.version,receiveEnd)){stopInbound();return true;}node.reset();}
      else if(message.kind==Kind::Error){static_cast<void>(router->contactDead(node->contact.id,now()));node.reset();}
      return true;
    }
    const auto matched=maintenanceRefresh.match(message,port);if(!matched){return false;}auto& node=maintenanceRefresh.nodes()[*matched];
    if(node.phase!=NodePhase::Sent||now()>=node.end){return true;}
    if(message.kind==Kind::Error){node.phase=NodePhase::Failed;static_cast<void>(router->contactDead(node.contact.id,now()));maintenanceRefresh.settle();return true;}
    if(message.kind!=Kind::Reply||message.id!=node.contact.id||!message.hasNodes||message.hasPeers){return true;}
    const auto actual=node.contact;node.phase=NodePhase::Replied;
    if(!learnRouting(actual,true,receiveEnd)||!rememberRoutingProtocol(actual,message.version,receiveEnd)){stopInbound();return true;}
    for(const auto& contact:message.nodes){if(contact.id==options.localId||contact.hash==localHash){continue;}if(!inboundCurrent(receiveEnd)||!learnRouting(contact,false,receiveEnd)){stopInbound();return true;}maintenanceRefresh.offer(contact);}
    maintenanceRefresh.settle();return true;
  }
  bool submitMaintenance(Node& node,Method method,const Id& target,bool ping,std::size_t index){
    if(!inboundCurrent(Clock::time_point::max())||channel->state()!=i2cp::InitialChannel::State::SessionCreated){return false;}
    const auto stamp=now();if(stamp<0||stamp>std::numeric_limits<std::int64_t>::max()-detail::RoutingRpcMillis){stopInbound();return false;}
    if(!node.contact.destination){
      if(maintenanceResolver){return false;}const auto other=channel->destinationLookupObservation();if(other&&other->phase==i2cp::InitialChannel::LookupPhase::Pending){return false;}
      const auto global=ping?stamp+detail::RoutingRpcMillis:maintenanceRefresh.target().end;
      if(!node.end){node.end=std::min(global,stamp+detail::RoutingRpcMillis);}if(node.end<=stamp){node.phase=NodePhase::Failed;return true;}
      auto op=channel->lookupDestination(node.contact.hash,unsigned(std::min<std::int64_t>(20000,node.end-stamp)));if(!op){stopInbound();return false;}
      node.phase=NodePhase::Resolving;maintenanceResolver=*op;maintenanceResolveIndex=index;maintenanceResolvePing=ping;return true;
    }
    if(!channel->dhtScopeObservation(scope).writeSlots){return false;}
    if(!transactions.advance(stamp)){stopInbound();return false;}if(!transactions.available()){maintenanceTransactionWake=transactions.wakeup(ping?stamp+detail::RoutingRpcMillis:maintenanceRefresh.target().end);return false;}
    auto tx=transaction(stamp);if(!tx){stopInbound();return false;}Message request;request.id=options.localId;request.transaction=*tx;request.method=method;request.target=target;auto payload=encode(request);if(!payload){stopInbound();return false;}
    const auto global=ping?stamp+detail::RoutingRpcMillis:maintenanceRefresh.target().end;
    if(!node.end){node.end=std::min(global,stamp+detail::RoutingRpcMillis);}const auto left=node.end-now();if(left<=0){node.phase=NodePhase::Failed;return true;}
    auto ticket=channel->submitDhtDatagram(node.contact.destination->wire,*payload,true,node.contact.port,unsigned(std::min<std::int64_t>(detail::RoutingRpcMillis,left)));
    if(!ticket){stopInbound();return false;}node.transaction=std::move(*tx);node.ticket=*ticket;node.phase=NodePhase::Queued;return true;
  }
  void maintainRouting(){
    if(!maintenanceEnabled()||!inboundCurrent(Clock::time_point::max())){return;}const auto stamp=now();const auto wall=QDateTime::currentMSecsSinceEpoch();maintenanceTransactionWake.reset();
    if(stamp<0||stamp>std::numeric_limits<std::int64_t>::max()-detail::RoutingRefreshMillis){stopInbound();return;}
    if(maintenanceApplicationLookup){
      const auto active=lookup.snapshot();if(active.operation==maintenanceApplicationLookup->first){
        if(!router->lookupStarted(wall,stamp)||!router->recordLookup(maintenanceApplicationLookup->second,wall,stamp)){stopInbound();return;}
      }maintenanceApplicationLookup.reset();
    }
    if(maintenanceNextTick&&stamp>=maintenanceNextTick){
      auto targets=router->maintenanceTick(stamp,wall,[](std::span<std::uint8_t> bytes){if(RAND_bytes(bytes.data(),static_cast<int>(bytes.size()))!=1){throw std::bad_alloc();}});
      maintenanceNextTick=stamp+detail::RoutingTickMillis;
      if(!targets||targets->count>detail::RoutingPending-maintenanceTargets.size()){stopInbound();return;}
      for(std::size_t i=0;i<targets->count;++i){maintenanceTargets.push_back({targets->targets[i],stamp+detail::RoutingRefreshMillis});}
    }
    for(const auto& id:maintenanceDead){if(!inboundCurrent(Clock::time_point::max())){return;}static_cast<void>(router->contactDead(id,now()));}maintenanceDead.clear();
    for(auto& node:maintenancePings){if(node&&node->phase==NodePhase::Failed){if(!inboundCurrent(Clock::time_point::max())){return;}static_cast<void>(router->contactDead(node->contact.id,now()));node.reset();}}
    if(maintenanceResolver){
      auto result=channel->destinationLookupObservation(maintenanceResolver);
      if(result&&result->phase!=i2cp::InitialChannel::LookupPhase::Pending){auto* node=maintenanceResolvingNode();if(node){if(result->phase==i2cp::InitialChannel::LookupPhase::Resolved&&result->destination&&result->destination->hash==node->contact.hash){node->contact.destination=*result->destination;node->phase=NodePhase::Candidate;}else{node->phase=NodePhase::Failed;}}maintenanceResolver=0;}
    }
    auto intents=router->takeIntents();if(!intents){stopInbound();return;}
    for(std::size_t i=0;i<intents->count;++i){const auto& intent=intents->values[i];
      auto pending=std::find_if(maintenanceIntents.begin(),maintenanceIntents.end(),[&](const auto& old){return old.id==intent.id;});
      if(pending!=maintenanceIntents.end()){if(intent.kind==azureus_router::RouterIntentKind::Ping){pending->kind=intent.kind;}continue;}
      if(maintenanceIntents.size()==detail::RoutingPending){stopInbound();return;}maintenanceIntents.push_back(intent);
    }
    // Application lookup owns candidate selection/refresh bandwidth. Already
    // admitted finite Ping RPCs can settle; no new maintenance RPC is admitted.
    if(lookup.running()){return;}
    for(auto& node:maintenancePings){if(node&&node->phase==NodePhase::Candidate){const auto index=static_cast<std::size_t>(&node-maintenancePings.data());submitMaintenance(*node,Method::Ping,{},true,index);if(!maintenanceEnabled()){return;}}}
    while(!maintenanceIntents.empty()){
      const auto free=std::find_if(maintenancePings.begin(),maintenancePings.end(),[](const auto& node){return !node;});if(free==maintenancePings.end()){break;}
      const auto intent=maintenanceIntents.front();maintenanceIntents.pop_front();const auto state=router->find(intent.id);if(!state||!*state){continue;}
      if((**state).local||(intent.kind==azureus_router::RouterIntentKind::NodeAdded&&(**state).hasBeenAlive)){continue;}
      if(std::any_of(maintenancePings.begin(),maintenancePings.end(),[&](const auto& node){return node&&node->contact.id==intent.id;})){continue;}
      const auto attached=std::find_if(routingContacts.begin(),routingContacts.end(),[&](const auto& contact){return contact.id==intent.id;});if(attached==routingContacts.end()){stopInbound();return;}
      Node node;node.contact=*attached;free->emplace(std::move(node));submitMaintenance(**free,Method::Ping,{},true,static_cast<std::size_t>(free-maintenancePings.begin()));if(!maintenanceEnabled()){return;}
    }
    if(!maintenanceRefresh.running()&&!maintenanceTargets.empty()){
      auto target=maintenanceTargets.front();maintenanceTargets.pop_front();if(target.end<=now()){return;}auto closest=router->closest(target.id,detail::RoutingAccuracy,true);if(!closest){stopInbound();return;}std::vector<Contact> seeds;seeds.reserve(MaximumContacts);
      for(std::size_t i=0;i<closest->count&&seeds.size()<MaximumContacts;++i){const auto& value=closest->values[i];if(value.local){continue;}const auto found=std::find_if(routingContacts.begin(),routingContacts.end(),[&](const auto& contact){return contact.id==value.id;});if(found!=routingContacts.end()){seeds.push_back(*found);}}
      if(!seeds.empty()){if(!router->lookupStarted(wall,now())||!router->recordLookup(target.id,wall,now())||!maintenanceRefresh.begin(target,seeds,now())){stopInbound();return;}}
    }
    for(unsigned i=0;i<detail::RoutingParallelism&&maintenanceRefresh.running();++i){const auto next=maintenanceRefresh.next(now());if(!next){break;}if(!submitMaintenance(maintenanceRefresh.nodes()[*next],Method::FindNode,maintenanceRefresh.target().id,false,*next)){break;}if(!maintenanceEnabled()){return;}}
  }
  std::optional<std::int64_t> maintenanceWake(std::int64_t stamp)const{
    if(!maintenanceEnabled()){return {};}
    std::optional<std::int64_t> end;const auto add=[&](std::int64_t value){if(value>stamp){end=end?std::min(*end,value):value;}};
    add(maintenanceNextTick);for(const auto& node:maintenancePings){if(node&&node->end){add(node->end);}}
    if(maintenanceRefresh.running()){add(maintenanceRefresh.target().end);for(const auto& node:maintenanceRefresh.nodes()){if(node.phase==NodePhase::Resolving||node.phase==NodePhase::Queued||node.phase==NodePhase::Sent){add(node.end);}}}
    if(maintenanceTransactionWake){add(*maintenanceTransactionWake);}return end;
  }
  bool maintenanceNeedsGrant(std::int64_t stamp)const{
    if(!maintenanceEnabled()){return false;}if((maintenanceNextTick&&stamp>=maintenanceNextTick)||!maintenanceDead.empty()||maintenanceApplicationLookup){return true;}
    if(std::any_of(maintenancePings.begin(),maintenancePings.end(),[](const auto& node){return node&&node->phase==NodePhase::Failed;})){return true;}
    return !lookup.running()&&(!maintenanceIntents.empty()||!maintenanceTargets.empty()||maintenanceRefresh.running()||std::any_of(maintenancePings.begin(),maintenancePings.end(),[](const auto& node){return node&&node->phase==NodePhase::Candidate;}));
  }
