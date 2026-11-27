// SPDX-License-Identifier: GPL-2.0-or-later
void Owner::clearAutoSpeedProbeRoute() {
 speedProbeLane_.reset();
 speedProbeRound_.reset();
 speedProbeSources_={};
 nextSpeedProbe_={};
 if(speedProbeSample_){speedProbeSample_.reset();changed();}
}

void Owner::autoSpeedProbeTick() {
 const auto epoch=shared_->autoSpeedProbeEpoch.load();
 if(epoch!=speedProbeEpoch_){
  speedProbeEpoch_=epoch;
  clearAutoSpeedProbeRoute();
 }
 if(!epoch||closing_||resettingNetwork_||natPreparing_||speedProbeSequence_==UINT64_MAX){
  if(speedProbeLane_){clearAutoSpeedProbeRoute();}
  return;
 }
 const auto ready=[this](std::size_t index){
  const auto& lane=lanes_[index];
  return lane&&lane->ready&&lane->actor&&lane->policy&&lane->policy->current();
 };
 const std::size_t index=ready(0)?0:1;
 if(!ready(index)){
  if(speedProbeLane_){clearAutoSpeedProbeRoute();}
  return;
 }
 if(speedProbeLane_!=index){
  clearAutoSpeedProbeRoute();
  if(speedProbeRoute_==UINT64_MAX){return;}
  speedProbeLane_=index;
  ++speedProbeRoute_;
 }
 const auto now=Clock::now();
 if(speedProbeRound_||now<nextSpeedProbe_){return;}
 nextSpeedProbe_=now+std::chrono::seconds(3);
 const auto policy=lanes_[index]->policy;
 const auto end=now+std::chrono::seconds(2);
 const auto route=speedProbeRoute_;
 gate(end,[this,epoch,index,route]{
  active_->operation->data.lanes[index]=true;
  if(speedProbeEpoch_==epoch&&shared_->autoSpeedProbeEpoch.load()==epoch&&speedProbeRoute_==route
      &&speedProbeLane_==index&&allowed(index)){
   sendAutoSpeedProbes(epoch,index);
  }
 },{},[policy,end](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||Clock::now()>=end){
   return std::unexpected(failure(storage::ErrorCode::Cancelled,"Auto Speed probe expired"));
  }
  const auto current=policy->validate();
  if(!current){return std::unexpected(current.error());}
  return Broker::Data{};
 });
}

void Owner::sendAutoSpeedProbes(std::uint64_t epoch,std::size_t index) {
 if(!lanes_[index]||!lanes_[index]->ready||!lanes_[index]->actor){return;}
 auto& lane=*lanes_[index];
 const auto route=lane.actor->routingSnapshot();
 if(!route){return;}

 auto round=std::make_shared<SpeedProbeRound>();
 round->sample.epoch=epoch;
 round->sample.sequence=++speedProbeSequence_;
 round->sample.route=speedProbeRoute_;
 round->sample.family=index==0?udp::TransportStatisticsFamily::IPv4:udp::TransportStatisticsFamily::IPv6;
 speedProbeRound_=round;
 for(std::size_t slot=0;slot<speedProbeSources_.size();++slot){
  auto& source=speedProbeSources_[slot];
  const nodes::Contact* choice=nullptr;
  for(std::size_t row=0;row<route->contactCount;++row){
   const auto& item=route->contacts[row];
   const auto& candidate=item.contact;
   if(candidate.id==lane.local.id||item.protocolSource!=udp::RoutingProtocolSource::Packet){continue;}
   if(source.id==candidate.id){if(source.failures<3){choice=&candidate;break;}continue;}
   if(!choice&&std::ranges::none_of(speedProbeSources_,[&](const auto& other){return other.id==candidate.id;})){
    choice=&candidate;
   }
  }
  if(!choice){continue;}
  if(source.id!=choice->id){
   if(source.id){++round->sample.replacements;}
   source={choice->id,0};
  }
  const auto end=Clock::now()+std::chrono::seconds(2);
  if(!lane.actor->attachKnownPeer(peerOf(*choice),choice->protocol,end)){continue;}
  ++round->pending;
  const auto sent=Clock::now();
  const auto ticket=lane.actor->ping(peerOf(*choice),choice->protocol,this,[this,round,slot,sent](auto outcome){
   if(speedProbeRound_!=round){return;}
   if(outcome){
    const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-sent).count();
    if(millis>=0&&millis<=2000){round->sample.rtt[slot]=static_cast<std::int32_t>(millis);}
   }
   if(--round->pending==0){finishAutoSpeedProbe(round);}
  },std::chrono::seconds(2));
  if(!ticket){--round->pending;}
 }
 if(!round->pending){finishAutoSpeedProbe(round);}
}

void Owner::finishAutoSpeedProbe(const std::shared_ptr<SpeedProbeRound>& round) {
 if(speedProbeRound_!=round){return;}
 speedProbeRound_.reset();
 if(closing_||resettingNetwork_||natPreparing_||!speedProbeLane_||speedProbeRoute_!=round->sample.route
     ||!lanes_[*speedProbeLane_]->policy->current()
     ||shared_->autoSpeedProbeEpoch.load()!=round->sample.epoch){return;}
 std::int32_t minimum=std::numeric_limits<std::int32_t>::max();
 for(const auto rtt:round->sample.rtt){if(rtt>=0){minimum=std::min(minimum,rtt);}}
 const auto bound=std::int64_t{5}*std::max(minimum,75);
 for(std::size_t slot=0;slot<speedProbeSources_.size();++slot){
  const auto rtt=round->sample.rtt[slot];auto& source=speedProbeSources_[slot];
  source.failures=rtt>=0&&rtt<bound?0:std::min(source.failures+1,3U);
 }
 round->sample.observedMillis=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
 speedProbeSample_=round->sample;changed();
}
