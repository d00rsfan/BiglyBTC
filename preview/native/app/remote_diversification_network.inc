// SPDX-License-Identifier: GPL-2.0-or-later
// All consumers share one original ledger per profile/source and DHT network.
// STORE replies arrive after the dispatch permit is released. Remembering a
// job-derived key/type is metadata only; persistence and every outgoing branch
// still acquire their own current source/network/target permit.
std::optional<core::AzureusRemoteDiversification> Owner::remoteRecord(std::size_t i,const nodes::NodeId& key,std::int8_t type){
 if(closing_||shared_->stop.load()||i>=lanes_.size()||!lanes_[i]||!lanes_[i]->ready||(type!=2&&type!=3)){return {};}const auto now=QDateTime::currentMSecsSinceEpoch();auto& ledger=*lanes_[i]->diversifications;if(auto old=ledger.find(key,now)){return old;}
 const auto a=QRandomGenerator::system()->bounded(10U),b=(a+1+QRandomGenerator::system()->bounded(9U))%10;
 return ledger.remember({key,type,now+172800000+QRandomGenerator::system()->bounded(86400000U),{a,b}},now);
}
void Owner::remoteRemember(std::size_t i,core::AzureusRemoteDiversification record,std::function<void(bool)> done){
 if(closing_||i>=lanes_.size()||!lanes_[i]||!lanes_[i]->policy||!lanes_[i]->versions){done(false);return;}auto& lane=*lanes_[i];
 auto actual=lane.diversifications->find(record.key,QDateTime::currentMSecsSinceEpoch());if(!actual||*actual!=record){done(false);return;}if(lane.diversifications->persisted(record)){done(true);return;}
 if(lane.remoteWaiters.size()>=256){done(false);return;}lane.remoteWaiters.push_back({record,Clock::now()+std::chrono::seconds(10),std::move(done)});
}
void Owner::remoteTick(){
 for(std::size_t i=0;i<lanes_.size();++i){if(!lanes_[i]){continue;}auto& lane=*lanes_[i];
  auto waits=std::exchange(lane.remoteWaiters,{});for(auto& wait:waits){if(Clock::now()>=wait.end){wait.done(false);}else{lane.remoteWaiters.push_back(std::move(wait));}}
  if(lane.remoteSaving||Clock::now()<lane.remoteRetry||!lane.ready||!lane.policy||!lane.versions){continue;}
  auto updates=lane.diversifications->dirty(QDateTime::currentMSecsSinceEpoch(),64);if(updates.empty()){continue;}lane.remoteSaving=true;const auto policy=lane.policy;const auto versions=lane.versions;
  auto complete=[this,i,updates](bool saved){auto& lane=*lanes_[i];lane.remoteSaving=false;lane.remoteRetry=Clock::now()+std::chrono::seconds(saved?0:30);if(!saved){qWarning("BiglyBTC dht.diversification network=%d state=save-deferred retry_seconds=30",lane.network);}if(saved){for(const auto& record:updates){static_cast<void>(lane.diversifications->markPersisted(record));}}
   auto waiting=std::exchange(lane.remoteWaiters,{});for(auto& wait:waiting){if(std::ranges::find(updates,wait.record)!=updates.end()){wait.done(saved&&lane.diversifications->persisted(wait.record)&&Clock::now()<wait.end);}else{lane.remoteWaiters.push_back(std::move(wait));}}
  };
  if(!gate(Clock::now()+std::chrono::seconds(5),[complete]{complete(true);},[complete]{complete(false);},[policy,versions,updates](std::stop_token stop)->Broker::Result{
   if(stop.stop_requested()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"DHT diversification save cancelled"));}auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}auto saved=storage::saveAzureusRemoteDiversifications(*versions,updates);if(!saved){return std::unexpected(saved.error());}return Broker::Data{};
  })){complete(false);}
 }
}
