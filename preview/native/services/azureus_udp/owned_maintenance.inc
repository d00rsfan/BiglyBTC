// SPDX-License-Identifier: GPL-2.0-or-later
// Same actor/permit as ordinary routing; cache contacts never restore Alive.
std::expected<void,Failure> OwnedPingTransport::restoreRoutingContacts(std::span<const SavedRoutingContact> saved){
  if(!normalCurrent()||saved.size()>32||!localId_){return std::unexpected(Failure::Authority);}
  for(const auto& record:saved){if(!record.contact.udp||!record.contact.address){return std::unexpected(Failure::Authority);}find::nodes::Contact value;value.protocol=record.contact.protocol;value.address=*record.contact.address;
    const auto id=nodeCache_->lookup({value.address,{}},value.protocol);if(!id){return std::unexpected(Failure::Authority);}value.id=id->id;if(value.id==*localId_){if(std::ranges::find(importedRouting_,value.id)==importedRouting_.end()){if(importedRouting_.size()==32){return std::unexpected(Failure::Capacity);}importedRouting_.push_back(value.id);}continue;}
    if(!routingPeer(value)){continue;}if(!learnRoutingContact(value,false)){return std::unexpected(Failure::Capacity);}
    if(std::ranges::find(importedRouting_,value.id)==importedRouting_.end()){if(importedRouting_.size()==32){return std::unexpected(Failure::Capacity);}importedRouting_.push_back(value.id);}
  }captureRouting();return {};
}
std::expected<std::vector<SavedRoutingContact>,Failure> OwnedPingTransport::exportRoutingContacts(){
  if(!normalCurrent()||!localId_){return std::unexpected(Failure::Authority);}const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();auto best=normalRouter_->bestContacts(now,QDateTime::currentMSecsSinceEpoch());if(!best){return std::unexpected(Failure::State);}
  std::vector<SavedRoutingContact> out;std::array<bool,azureus_router::MaximumRouterContacts> used{};
  for(unsigned pass=0;pass<4;++pass){for(std::size_t i=0;i<best->count&&out.size()<32;++i){if(used[i]){continue;}const auto& entry=best->values[i];const auto& state=entry.contact;const bool imported=std::ranges::find(importedRouting_,state.id)!=importedRouting_.end();
    if((pass==0&&!(imported&&state.alive()))||(pass==1&&!state.alive())||(pass==2&&!(imported&&!state.failing()))){continue;}
    find::nodes::Snapshot value;value.udp=true;
    if(state.local){value.protocol=config_.version;value.address=originator(endpoint());}else{const auto attached=std::ranges::find(normalContacts_,state.id,&find::nodes::Contact::id);if(attached==normalContacts_.end()||!routingPeer(*attached)){continue;}value.protocol=attached->protocol;value.address=attached->address;}
    out.push_back({value,entry.timeAlive});used[i]=true;
  }}return out;
}
std::expected<std::uint64_t,Failure> OwnedPingTransport::integrateKnownRouting(std::chrono::nanoseconds timeout){
  const auto entered=Clock::now();if(!owner()||!localId_){return std::unexpected(Failure::Thread);}if(timeout<=std::chrono::nanoseconds::zero()||timeout>std::chrono::seconds(30)){return std::unexpected(Failure::Deadline);}return startRouting(*localId_,entered+timeout,true);
}
void OwnedPingTransport::nextRoutingRefresh(){
  if(!normalCurrent()||(routing_&&!routing_->finished)||routingRefreshes_.empty()){return;}const auto target=routingRefreshes_.front();routingRefreshes_.pop_front();maintenance_.pending=routingRefreshes_.size();auto sent=startRouting(target,Clock::now()+std::chrono::minutes(5),false);
  if(sent){if(maintenance_.refreshes==UINT64_MAX){routingFailed(Failure::Capacity);return;}++maintenance_.refreshes;}else{maintenance_.failure=sent.error();}captureRouting();
}
std::expected<void,Failure> OwnedPingTransport::maintainRouting(std::int64_t wall){
  if(!normalCurrent()){return std::unexpected(Failure::Authority);}const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  auto targets=normalRouter_->maintenanceTick(now,wall,[](std::span<std::uint8_t> bytes){for(auto& byte:bytes){byte=static_cast<std::uint8_t>(QRandomGenerator::system()->generate());}});
  if(!targets||targets->count>64-routingRefreshes_.size()||maintenance_.ticks==UINT64_MAX){routingFailed(Failure::Capacity);maintenance_.failure=Failure::Capacity;return std::unexpected(Failure::Capacity);}
  ++maintenance_.ticks;for(std::size_t i=0;i<targets->count;++i){routingRefreshes_.push_back(targets->targets[i]);}maintenance_.pending=routingRefreshes_.size();dispatchRoutingPings();nextRoutingRefresh();captureRouting();return {};
}
MaintenanceFacts OwnedPingTransport::maintenanceFacts()const noexcept{return owner()?maintenance_:MaintenanceFacts{};}
