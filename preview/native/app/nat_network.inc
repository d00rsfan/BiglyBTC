// SPDX-License-Identifier: GPL-2.0-or-later
// Uses the existing shared listener, source broker and fixed room epochs.
void Owner::natStart(std::size_t i){
 if(i>=2||!allowed(i)||!lanes_[i]||!lanes_[i]->ready){return;}auto& lane=*lanes_[i];
 if(lane.nat&&lane.nat->facts().closed){lane.nat.reset();}if(lane.natDirectory&&lane.natDirectory->facts().closed){lane.natDirectory.reset();}
 auto hooks=[this,i]{nat::SessionHooks h;h.afterRelease=[this](auto fn){return later(std::move(fn));};
  h.revision=[this,i]()->std::optional<std::uint64_t>{return allowed(i)?std::optional{shared_->generation}:std::nullopt;};
  h.withPermit=[this,i](auto end,auto fn){return gate(end,std::move(fn),[this,i]{if(lanes_[i]){if(lanes_[i]->nat){lanes_[i]->nat->revoke();}if(lanes_[i]->natDirectory){lanes_[i]->natDirectory->close();}}});};return h;};
 if(!lane.nat){auto h=hooks();h.peer=[this,i](const auto& contact,auto end){if(!allowed(i)||!lanes_[i]||!lanes_[i]->nat){return false;}const auto& a=contact.transport();udp::Peer actual{address(a.bytes,a.length),a.port};return bool(lanes_[i]->actor->leaseTransferPeer(actual,nat::handlerKey(),contact.exportSnapshot(),lanes_[i]->nat.get(),shared_->generation,end));};h.observed=[this,i](const auto& contact){natObserved(i,contact);};auto made=nat::Session::open(*lane.actor,shared_->generation,std::move(h));if(made){lane.nat=std::move(*made);}}
 if(!lane.natDirectory){auto made=nat::Directory::open(*lane.actor,lane.local,lane.database,shared_->generation,hooks(),owned_,{},[this,i](auto record,auto done){natRemember(i,std::move(record),std::move(done));});if(made){lane.natDirectory=std::move(*made);lane.natDirectory->restoreDiversifications(lane.natDiversifications,QDateTime::currentMSecsSinceEpoch());}}
}
void Owner::natTry(std::shared_ptr<RoomToken> token,std::size_t i,msg::RoomPeer logical,bool force){
 if(i>=2||natPreparing_||closing_||!token||!token->active||!lanes_[i]||!lanes_[i]->nat||!lanes_[i]->natDirectory){return;}
 auto external=msg::RoomSession::externalPeer(logical);if(!external||!external->address||(owned_?!logical.endpoint.address.isLoopback():!publicAddress(logical.endpoint))){return;}
 const auto now=Clock::now();const auto key=std::to_string(token->epoch)+"/"+std::to_string(i)+"/"+logical.contact;const auto old=natAttempts_.find(key);
 if(old!=natAttempts_.end()&&(old->second->active||now-old->second->started<(force?std::chrono::seconds(45):std::chrono::seconds(120)))){return;}
 const auto roomActive=std::ranges::count_if(natAttempts_,[&](const auto& p){return p.second->active&&p.second->token==token;});const auto laneActive=std::ranges::count_if(natAttempts_,[&](const auto& p){return p.second->active&&p.second->lane==i;});if(roomActive>=3||laneActive>=3){return;}
 std::erase_if(natAttempts_,[&](const auto& p){return !p.second->active&&(!p.second->token->active||now-p.second->started>=std::chrono::seconds(120));});if(natAttempts_.size()>=4096){return;}
 auto work=std::make_shared<NatAttempt>(NatAttempt{token,i,std::move(logical),now,true});natAttempts_[key]=work;
 nodes::Contact announced;announced.protocol=external->protocol;announced.address=*external->address;nodes::NodeCache cache;auto target=nat::RoutedContact::observed(std::move(announced),contact(work->logical.endpoint).address,cache);if(!target){work->active=false;return;}
 if(work->logical.rendezvous){auto hint=msg::RoomSession::contactHint(work->logical.lane,msg::NodeHint{{},*work->logical.rendezvous});if(hint&&hint->endpoint.address.protocol()==work->logical.endpoint.address.protocol()&&(owned_?hint->endpoint.address.isLoopback():publicAddress(hint->endpoint))){nodes::NodeCache relayCache;auto relay=nat::RoutedContact::observed(contact(hint->endpoint,hint->version),contact(hint->endpoint,hint->version).address,relayCache);if(relay){auto id=lanes_[i]->nat->punch(*relay,*target,this,[this,work](auto result){natResult(work,std::move(result));});if(!id){work->active=false;}return;}}}
 auto result=lanes_[i]->natDirectory->read(*target,this,[this,work,target=*target](auto relay)mutable{
  if(closing_||!work->token->active||!relay||!relay->has_value()||!lanes_[work->lane]||!lanes_[work->lane]->nat){work->active=false;return;}
  auto id=lanes_[work->lane]->nat->punch(std::move(**relay),std::move(target),this,[this,work](auto result){natResult(work,std::move(result));});if(!id){work->active=false;}
 });if(!result){work->active=false;}
}
void Owner::natResult(const std::shared_ptr<NatAttempt>& work,nat::TunnelOutcome outcome){
 work->active=false;if(!outcome||closing_||!work->token->active){return;}const auto& a=outcome->contact.transport();const udp::Peer actual{address(a.bytes,a.length),a.port};
 gate(Clock::now()+std::chrono::seconds(3),[this,work,actual]{const auto it=rooms_.find(work->token->key);if(!allowed(work->lane)||!work->token->active||!active_->operation->data.keys.contains(work->token->epoch)||it==rooms_.end()||it->second.token!=work->token||!it->second.session){return;}
  auto logical=work->logical;const auto mapped=std::ranges::find(it->second.lanes,work->lane);if(mapped==it->second.lanes.end()){return;}logical.lane=std::size_t(mapped-it->second.lanes.begin());if(!it->second.session->updateRoute(logical,actual)){return;}
  for(auto& [id,call]:privateOutgoing_){(void)id;if(!call->finished&&call->parent==work->token&&call->lane==work->lane&&call->target.contact==logical.contact){call->target.endpoint=actual;}}
 });
}
void Owner::natObserved(std::size_t i,const nat::RoutedContact& contact){
 const auto external=contact.exportSnapshot();const auto& a=contact.transport();const udp::Peer actual{address(a.bytes,a.length),a.port};
 gate(Clock::now()+std::chrono::seconds(3),[this,i,external,actual]{if(!allowed(i)){return;}for(auto& [key,room]:rooms_){(void)key;if(!room.token||!room.token->active||!active_->operation->data.keys.contains(room.token->epoch)||!room.session){continue;}const auto mapped=std::ranges::find(room.lanes,i);if(mapped!=room.lanes.end()){if(room.session->observeRoute(std::size_t(mapped-room.lanes.begin()),external,actual)){for(auto& [id,call]:privateOutgoing_){(void)id;if(call->finished||call->parent!=room.token||call->lane!=i){continue;}auto peer=msg::RoomSession::externalPeer(call->target);if(peer&&peer->address==external.address&&peer->protocol==external.protocol){call->target.endpoint=actual;}}}}}});
}

void Owner::natSave(std::size_t i,bool reachable){
 auto& lane=*lanes_[i];if(lane.natSavePending||lane.natSavedReachable==std::optional{reachable}){return;}lane.natSavePending=true;const auto lease=shared_->lease;const auto policy=lane.policy;const auto network=lane.network;
 if(!gate(Clock::now()+std::chrono::seconds(10),[this,i,reachable]{lanes_[i]->natSavePending=false;lanes_[i]->natSavedReachable=reachable;},[this,i]{lanes_[i]->natSavePending=false;},[lease,policy,network,reachable](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"NAT reachability save cancelled"));}auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}auto file=storage::ConfigFile::load(*lease,"biglybt.config");if(!file){return std::unexpected(file.error());}if(!file->document().replaceValue("Plugin.DHT.dht.reachable."+std::to_string(network),reachable?"i1e":"i0e")){return std::unexpected(failure(storage::ErrorCode::Io,"NAT reachability could not be encoded"));}auto saved=file->save(*lease);if(!saved){return std::unexpected(saved.error());}return Broker::Data{};
 })){lane.natSavePending=false;}
}
void Owner::natPublish(std::size_t i,std::optional<nat::RoutedContact> relay){
 auto& lane=*lanes_[i];if(lane.natPublishing||!lane.natDirectory){return;}lane.natPublishing=true;const auto policy=lane.policy;const auto versions=lane.versions;auto reserved=std::make_shared<std::optional<std::int32_t>>();
 if(!gate(Clock::now()+std::chrono::seconds(10),[this,i,reserved,relay]{auto& lane=*lanes_[i];lane.natPublishing=false;if(!*reserved||!lane.natDirectory||!lane.nat){return;}const auto selected=lane.nat->selectedRendezvous();if((!natPreparing_&&(bool(selected)!=bool(relay)||(selected&&relay&&selected->external().address!=relay->external().address)))||(natPreparing_&&relay)){return;}active_->operation->data.lanes[i]=true;
  if(relay?lane.natDirectory->publish(*relay,**reserved):lane.natDirectory->withdraw(**reserved)){lane.natPublished=bool(relay);lane.natPublishedRelay=relay;lane.natNextPublish=Clock::now()+std::chrono::hours(1);changed();}
 },[this,i]{lanes_[i]->natPublishing=false;},[policy,versions,reserved](std::stop_token stop)->Broker::Result{if(stop.stop_requested()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"NAT publication cancelled"));}auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}auto value=versions->next(QDateTime::currentMSecsSinceEpoch());if(!value){return std::unexpected(value.error());}*reserved=*value;return Broker::Data{};})){lane.natPublishing=false;}
}
void Owner::natTick(std::size_t i){
 if(i>=2||natPreparing_||!allowed(i)){return;}auto& lane=*lanes_[i];if(!lane.nat||!lane.natDirectory){return;}static_cast<void>(lane.actor->updateReachability());const auto reach=lane.actor->reachabilityFacts();if(reach.accurate){natSave(i,reach.reachable);}const auto now=Clock::now();
 auto selected=lane.nat->selectedRendezvous();if(lane.natRelay&&!selected){lane.natFailed.push_back(lane.natRelay->external().id);if(lane.natFailed.size()>16){lane.natFailed.pop_front();}lane.natRelay.reset();lane.natCandidates.clear();lane.natNextSelect=now;}
 if(reach.reachable){if(selected){lane.nat->clearRendezvous();}lane.natRelay.reset();lane.natCandidates.clear();for(auto ticket:lane.natProbes){lane.actor->cancel(ticket);}lane.natProbes.clear();if(lane.natPublished){natPublish(i,{});}return;}
 if(selected){const bool changed=!lane.natPublishedRelay||lane.natPublishedRelay->external().address!=selected->external().address;if(changed||now>=lane.natNextPublish){natPublish(i,selected);}return;}
 if(lane.natPublished){natPublish(i,{});}
 if(lane.natCandidates.empty()&&lane.natProbes.empty()&&now>=lane.natNextSelect){const auto routing=lane.actor->routingSnapshot();if(routing){for(std::size_t j=0;j<routing->contactCount;++j){const auto& c=routing->contacts[j];if(c.nodeStatus>=0&&(c.nodeStatus&1)&&c.protocolSource==udp::RoutingProtocolSource::Packet&&c.contact.id!=lane.local.id&&std::ranges::find(lane.natFailed,c.contact.id)==lane.natFailed.end()){lane.natCandidates.push_back(c.contact);}}}std::shuffle(lane.natCandidates.begin(),lane.natCandidates.end(),*QRandomGenerator::global());lane.natNextSelect=now+std::chrono::minutes(5);}
 if(lane.natCandidates.empty()||now<lane.natNextProbe){return;}const auto candidate=lane.natCandidates.back();lane.natCandidates.pop_back();lane.natNextProbe=now+std::chrono::seconds(1);auto receipt=std::make_shared<udp::Ticket>();
 auto ticket=lane.actor->ping(peerOf(candidate),candidate.protocol,this,[this,i,candidate,receipt](auto outcome){if(!lanes_[i]||closing_){return;}std::erase(lanes_[i]->natProbes,*receipt);if(!outcome){return;}gate(Clock::now()+std::chrono::seconds(3),[this,i,candidate]{auto& lane=*lanes_[i];if(!allowed(i)||!lane.nat||lane.nat->selectedRendezvous()||lane.actor->reachabilityFacts().reachable){return;}nodes::NodeCache cache;auto relay=nat::RoutedContact::observed(candidate,candidate.address,cache);if(relay&&lane.nat->selectRendezvous(*relay)){lane.natRelay=std::move(*relay);lane.natCandidates.clear();for(auto ticket:lane.natProbes){lane.actor->cancel(ticket);}lane.natProbes.clear();changed();}});},std::chrono::seconds(5));if(ticket){*receipt=*ticket;lane.natProbes.push_back(*ticket);}
}

void Owner::natPrepare(){
 if(!natPreparing_){natPreparing_=true;for(std::size_t i=0;i<2;++i){if(!lanes_[i]){continue;}auto& lane=*lanes_[i];lane.natCandidates.clear();for(auto ticket:lane.natProbes){lane.actor->cancel(ticket);}lane.natProbes.clear();if(lane.nat){lane.nat->clearRendezvous();}}}
 const auto deadline=Clock::time_point(std::chrono::nanoseconds(shared_->prepareUntil.load()));if(Clock::now()>=deadline){return;}bool done=true;
 for(std::size_t i=0;i<2;++i){if(!lanes_[i]){continue;}auto& lane=*lanes_[i];if(lane.natPublished&&!lane.natPublishing){natPublish(i,{});}if(lane.natPublished||lane.natPublishing||(lane.natDirectory&&lane.natDirectory->facts().active)){done=false;}}
 if(done){changed();publish();shared_->prepared=true;}
}

void Owner::natRemember(std::size_t i,core::AzureusRemoteDiversification record,std::function<void(bool)> done){
 if(closing_||!lanes_[i]||!lanes_[i]->policy||!lanes_[i]->versions){done(false);return;}const auto policy=lanes_[i]->policy;const auto versions=lanes_[i]->versions;
 if(!gate(Clock::now()+std::chrono::seconds(5),[this,i,record,done]{auto& records=lanes_[i]->natDiversifications;const auto found=std::ranges::find(records,record.key,&core::AzureusRemoteDiversification::key);if(found!=records.end()){*found=record;}else if(records.size()<512){records.push_back(record);}done(true);},[done]{done(false);},[policy,versions,record](std::stop_token stop)->Broker::Result{if(stop.stop_requested()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"NAT diversification save cancelled"));}auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}const std::array updates{record};auto saved=storage::saveAzureusRemoteDiversifications(*versions,updates);if(!saved){return std::unexpected(saved.error());}return Broker::Data{};})){done(false);}
}
