// SPDX-License-Identifier: GPL-2.0-or-later
// The UDP owner never touches profile files. The broker saves a frozen policy
// snapshot, then releases waiters inside its actual current source permit.
bool Owner::localCheckpoint(std::size_t i,std::uint64_t revision,biglybtc::core::MoveOnlyFunction<void(bool)> done){
 if(closing_||i>=lanes_.size()||!lanes_[i]||!lanes_[i]->ready||!done){return false;}
 auto& lane=*lanes_[i];
 if(lane.localSavedRevision==revision&&allowed(i)){done(true);return true;}
 if(Clock::now()<lane.localRetry){done(false);return true;}
 if(lane.localWaiters.size()>=32){return false;}
 lane.localWaiters.push_back({revision,Clock::now()+std::chrono::seconds(5),std::move(done)});
 return true;
}
void Owner::localCheckpointTick(){
 for(std::size_t i=0;i<lanes_.size();++i){
  if(!lanes_[i]){continue;}auto& lane=*lanes_[i];
  auto waits=std::exchange(lane.localWaiters,{});
  for(auto& wait:waits){if(Clock::now()>=wait.end){wait.done(false);}else{lane.localWaiters.push_back(std::move(wait));}}
  if(lane.localSaving||Clock::now()<lane.localRetry||!lane.ready||!lane.database||!lane.policy||!lane.versions){continue;}
  const auto revision=lane.database->localDiversificationRevision();
  if(!revision||*revision==lane.localSavedRevision){continue;}
  const auto frozen=lane.database->localDiversificationCheckpoint();if(!frozen){continue;}
  std::map<nodes::NodeId,core::AzureusLocalDiversification> records;
  std::vector<core::AzureusLocalDiversification> updates;
  for(const auto& record:(*frozen)->entries){
   records.insert_or_assign(record.key,record);const auto old=lane.localSaved.find(record.key);
   if(old==lane.localSaved.end()||old->second!=record){updates.push_back(record);}
  }
  for(const auto& [key,old]:lane.localSaved){if(!records.contains(key)){auto clear=old;clear.type=1;updates.push_back(clear);}}
  lane.localSaving=true;const auto policy=lane.policy;const auto versions=lane.versions;const auto savedRevision=(*frozen)->revision;
  auto complete=[this,i,policy,versions,savedRevision,records=std::move(records)](bool saved)mutable{
   auto& lane=*lanes_[i];lane.localSaving=false;
   saved=saved&&!closing_&&lane.ready&&lane.policy==policy&&lane.versions==versions;
   lane.localRetry=Clock::now()+std::chrono::seconds(saved?0:30);
   if(saved){lane.localSaved=std::move(records);lane.localSavedRevision=savedRevision;active_->operation->data.lanes[i]=true;}
   else{qWarning("BiglyBTC dht.local-diversification network=%d state=save-deferred retry_seconds=30",lane.network);}
   auto waiting=std::exchange(lane.localWaiters,{});
   for(auto& wait:waiting){
    if(wait.revision<=savedRevision){wait.done(saved&&wait.revision==savedRevision&&Clock::now()<wait.end);}
    else{lane.localWaiters.push_back(std::move(wait));}
   }
  };
  // Copies share the frozen map without depending on an actor stack frame.
  auto completion=std::make_shared<decltype(complete)>(std::move(complete));
  if(!gate(Clock::now()+std::chrono::seconds(5),[completion]{(*completion)(true);},[completion]{(*completion)(false);},
   [policy,versions,updates=std::move(updates)](std::stop_token stop)->Broker::Result{
    if(stop.stop_requested()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"DHT local checkpoint cancelled"));}
    auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}
    if(!updates.empty()){auto saved=storage::saveAzureusLocalDiversifications(*versions,updates);if(!saved){return std::unexpected(saved.error());}}
    return Broker::Data{};
   })){(*completion)(false);}
 }
}
