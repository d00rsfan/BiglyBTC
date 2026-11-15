// SPDX-License-Identifier: GPL-2.0-or-later
// Original30-minute main / one-hour small-network cadence and fixed timer jitter.
void Owner::cacheTick(){
 for(std::size_t i=0;i<lanes_.size();++i){
  if(!lanes_[i]||!lanes_[i]->ready||!lanes_[i]->actor){continue;}auto& lane=*lanes_[i];const auto facts=lane.actor->cacheMaintenanceFacts();
  if(facts.active!=lane.cacheObservedActive){lane.cacheObservedActive=facts.active;if(!facts.active){lane.cacheAfter=Clock::now()+lane.cacheInterval;}changed();}
  if(facts.active||lane.cachePending||Clock::now()<lane.cacheAfter){continue;}lane.cachePending=true;
  if(!gate(Clock::now()+std::chrono::seconds(5),[this,i]{auto& lane=*lanes_[i];lane.cachePending=false;if(!allowed(i)){lane.cacheAfter=Clock::now()+std::chrono::minutes(1);return;}auto started=lane.actor->republishCachedValues(QDateTime::currentMSecsSinceEpoch(),lane.cachePeriod);if(started){lane.cacheWarned=false;lane.cacheAfter=Clock::now()+lane.cacheInterval;changed();}else{lane.cacheAfter=Clock::now()+(started.error()==udp::Failure::Authority?std::chrono::seconds(5):std::chrono::seconds(60));if(started.error()!=udp::Failure::Authority&&!lane.cacheWarned){lane.cacheWarned=true;qWarning("BiglyBTC dht.cache network=%d state=deferred reason=%d retry_seconds=60",lane.network,int(started.error()));}}},[this,i]{lanes_[i]->cachePending=false;lanes_[i]->cacheAfter=Clock::now()+std::chrono::minutes(1);})){lane.cachePending=false;lane.cacheAfter=Clock::now()+std::chrono::seconds(5);}
 }
}
