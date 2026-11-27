// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside the normal owner: retire old network work before slot reuse.
void Owner::addressReport(std::size_t i,const udp::AddressReport& report,bool force){
 if(closing_||resettingNetwork_||!provider_||!lanes_[i]||!lanes_[i]->ready){return;}
 const auto epoch=networkEpoch_;const auto actor=QPointer<udp::OwnedPingTransport>(lanes_[i]->actor.get());
 gate(Clock::now()+std::chrono::seconds(5),[this,i,report,force,epoch,actor]{
  if(!allowed(i)||networkEpoch_!=epoch||!actor||lanes_[i]->actor.get()!=actor.data()){return;}
  auto& lane=*lanes_[i];const auto& raw=report.reported();
  services::azureus_identity::ReportInput input{i==1,lane.local.address.length==16?address(lane.local.address.bytes,16):address(lane.local.address.bytes,4),address(raw.bytes,raw.length),report.reporter().address,QDateTime::currentMSecsSinceEpoch(),report.observedContacts(),force};
  auto decision=services::azureus_identity::addressReport(lane.addressReports,input);if(!decision){return;}lane.addressReports=decision->state;
  if(decision->action==services::azureus_identity::ReportAction::Defer){QTimer::singleShot(30000,this,[this,i,report,epoch,actor]{if(!closing_&&!resettingNetwork_&&networkEpoch_==epoch&&actor&&lanes_[i]&&lanes_[i]->actor.get()==actor.data()){addressReport(i,report,true);}});return;}
  if(decision->action!=services::azureus_identity::ReportAction::Recheck||lane.probing){return;}
  // No value from the report is advertised. The provider must independently
  // receive two correlated agreeing observations from different reporter IPs.
  lane.probing=true;auto seeds=lane.seeds;std::erase_if(seeds,[&](const auto& p){return p.endpoint==report.reporter();});seeds.insert(seeds.begin(),{report.reporter(),55});if(seeds.size()>32){seeds.resize(32);}
  const auto accepted=provider_->probeAddress(lane.listener.local,lane.network,std::move(seeds),owned_,[this](auto end,auto effect){return gate(end,std::move(effect));},[this,i]{return allowed(i);},[this,i,epoch,actor](wire::AddressResult result){
   if(networkEpoch_!=epoch||!actor||!lanes_[i]||lanes_[i]->actor.get()!=actor.data()){return;}auto& lane=*lanes_[i];lane.probing=false;if(closing_||resettingNetwork_||!result){return;}lane.seeds=std::move(result->candidates);if(result->external==peerOf(lane.local)){return;}
   QTimer::singleShot(0,this,[this,epoch]{if(networkEpoch_==epoch){reconnectNetwork("Public DHT address changed; reconnecting…");}});
  });if(!accepted){lane.probing=false;}
 });
}
void Owner::reconnectNetwork(std::string reason){
 if(closing_||resettingNetwork_||shared_->stop.load()){return;}
 if(networkEpoch_==std::numeric_limits<std::uint64_t>::max()){availability_="Public network generation exhausted";changed();return;}
 resettingNetwork_=true;reconnectingNetwork_=true;++networkEpoch_;
 clearAutoSpeedProbeRoute();
 for(auto& lane:lanes_){if(lane){lane->ready=false;if(lane->dnsId>=0){QHostInfo::abortHostLookup(lane->dnsId);lane->dnsId=-1;}lane->dns=false;}}
 for(auto& [key,room]:rooms_){(void)key;if(room.session){room.session->close();room.session.reset();}room.lanes.clear();room.status=reason;room.nextLookup={};room.nextPut={};}
 privateStop();for(const auto& [hash,work]:peeks_){(void)hash;peekFinish(work,"Public network reconnecting");}
 for(const auto& [key,work]:ratings_){(void)key;if(work->view.running){ratingDeferred_.push_back({work->hash,work->view.publishing,false,work->view.byHash});}ratingCancel(work,"Public network reconnecting",false);}
 for(const auto& [key,torrent]:torrents_){(void)key;cancelTorrentLookups(torrent);torrent->next={};}
 for(auto& job:torrentLookups_){job->finished=true;}for(auto& job:torrentPending_){job->finished=true;}for(auto& publication:torrentPublications_){publication->done=true;}
 const auto cancelRcm=[this](const auto& jobs){for(const auto& job:jobs){rcmCancel(job);}};cancelRcm(rcmLookups_);cancelRcm(rcmPending_);for(const auto& job:rcmReadyPublish_){job->live->store(false);job->finished=true;}for(const auto& publication:rcmPublications_){publication->finished=true;publication->lookup->live->store(false);}rcmStopSearch("Public network reconnecting");
 for(std::size_t i=0;i<3;++i){auto& lane=lanes_[i];if(!lane){continue;}if(lane->actor){const auto route=lane->actor->routingSnapshot();if(route){for(std::size_t j=0;j<route->contactCount&&parkedSeeds_[i].size()<128;++j){const auto& c=route->contacts[j];if(c.protocolSource!=udp::RoutingProtocolSource::ProbeHint){parkedSeeds_[i].push_back({peerOf(c.contact),c.contact.protocol});}}}}if(lane->natDirectory){lane->natDirectory->close();}if(lane->nat){lane->nat->close();}if(lane->actor){lane->actor->close();}for(auto& wait:lane->localWaiters){wait.done(false);}lane->localWaiters.clear();for(auto& wait:lane->keyBlockWaiters){wait.done(false);}lane->keyBlockWaiters.clear();}
 if(provider_){provider_->close();}for(auto& frame:frames_){if(frame.job){Broker::revoke(frame.job);}}
 availability_=std::move(reason);changed();
}
void Owner::finishNetworkReset(){
 if(!resettingNetwork_||closing_||!frames_.empty()||!crypto_.empty()){return;}
 for(std::size_t i=0;i<3;++i){if(!lanes_[i]){continue;}auto& lane=*lanes_[i];if(lane.database){parkedDatabases_[i]=std::move(lane.database);}if(lane.actor){const auto route=lane.actor->routingSnapshot();if(route){for(std::size_t n=0;n<route->contactCount&&parkedSeeds_[i].size()<128;++n){const auto& c=route->contacts[n];if(c.protocolSource!=udp::RoutingProtocolSource::ProbeHint){parkedSeeds_[i].push_back({peerOf(c.contact),c.contact.protocol});}}}}for(const auto& seed:lane.seeds){if(parkedSeeds_[i].size()<128&&std::ranges::none_of(parkedSeeds_[i],[&](const auto& old){return old.endpoint==seed.endpoint;})){parkedSeeds_[i].push_back(seed);}}lanes_[i].reset();}
 provider_.reset();for(const auto& [key,torrent]:torrents_){(void)key;for(auto& pending:torrent->publicationQueued){pending.clear();}}
 torrentLookups_.clear();torrentPending_.clear();torrentPublications_.clear();torrentPublicationPending_.clear();
 rcmLookups_.clear();rcmPending_.clear();rcmAwaitPublish_.clear();rcmReadyPublish_.clear();rcmPublications_.clear();
 resettingNetwork_=false;attachExhausted_=true;attachAttempts_=0;nextAttach_={};nextTransportConfig_={};readTransportConfig();
}
void Owner::readTransportConfig(){
 if(closing_||resettingNetwork_||transportConfigPending_||provider_){return;}transportConfigPending_=true;nextTransportConfig_=Clock::now()+std::chrono::seconds(30);
 auto port=std::make_shared<unsigned short>(0);const auto lease=shared_->lease;
 if(!gate(Clock::now()+std::chrono::seconds(5),[this,port]{transportConfigPending_=false;attachPort_=*port;attachExhausted_=false;attachEnd_=Clock::now()+std::chrono::seconds(30);nextAttach_={};attachTransport();},[this]{transportConfigPending_=false;availability_="Waiting for an enabled configured Public UDP listener…";changed();},[lease,port](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Network configuration cancelled"));}auto config=storage::ConfigFile::load(*lease,"biglybt.config");if(!config){return std::unexpected(config.error());}const auto* enabled=config->document().find("UDP.Listen.Port.Enable");const auto* value=config->document().find("UDP.Listen.Port");if(!value){value=config->document().find("TCP.Listen.Port");}if((enabled&&enabled->type==core::Node::Type::Integer&&!enabled->integer)||!value||value->type!=core::Node::Type::Integer||value->integer<1||value->integer>65535){return std::unexpected(failure(storage::ErrorCode::Unsupported,"Public UDP listener is disabled or invalid"));}*port=static_cast<unsigned short>(value->integer);return Broker::Data{};
 })){transportConfigPending_=false;}
}
