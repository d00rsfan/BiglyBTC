// SPDX-License-Identifier: GPL-2.0-or-later
// Original fifteen-minute cadence, captured startup feature policy.
void Owner::surveyTick(){
 for(std::size_t i=0;i<lanes_.size();++i){
  if(!lanes_[i]||!lanes_[i]->ready||!lanes_[i]->actor||!lanes_[i]->surveyAllowed){continue;}auto& lane=*lanes_[i];const auto facts=lane.actor->surveyMaintenanceFacts();
  if(facts.active!=lane.surveyObservedActive){lane.surveyObservedActive=facts.active;if(!facts.active){lane.surveyAfter=Clock::now()+lane.surveyInterval;}changed();}
  if(facts.active||lane.surveyPending||Clock::now()<lane.surveyAfter){continue;}lane.surveyPending=true;
  if(!gate(Clock::now()+std::chrono::seconds(5),[this,i]{auto& lane=*lanes_[i];lane.surveyPending=false;if(!allowed(i)){lane.surveyAfter=Clock::now()+std::chrono::minutes(1);return;}auto started=lane.actor->surveyCachedValues();if(started){lane.surveyWarned=false;lane.surveyAfter=Clock::now()+lane.surveyInterval;changed();}else{lane.surveyAfter=Clock::now()+(started.error()==udp::Failure::Authority?std::chrono::seconds(5):std::chrono::seconds(60));if(started.error()!=udp::Failure::Authority&&!lane.surveyWarned){lane.surveyWarned=true;qWarning("BiglyBTC dht.survey network=%d state=deferred reason=%d retry_seconds=60",lane.network,int(started.error()));}}},[this,i]{lanes_[i]->surveyPending=false;lanes_[i]->surveyAfter=Clock::now()+std::chrono::minutes(1);})){lane.surveyPending=false;lane.surveyAfter=Clock::now()+std::chrono::seconds(5);}
 }
}
