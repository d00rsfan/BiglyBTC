// SPDX-License-Identifier: GPL-2.0-or-later
// Included by the sole shared Public network owner. No second socket/session.
bool Owner::torrentAllowed(const Torrent& torrent,std::size_t lane,bool withdrawing)const{
 return allowed(lane)&&torrent.target.live&&(withdrawing||torrent.target.live->load())
     &&active_->operation->data.torrents.contains(torrent.target.live.get());
}
bool Owner::torrentGate(std::shared_ptr<Torrent> torrent,std::size_t lane,bool withdrawing,Clock::time_point end,
    std::move_only_function<void()> effect,std::move_only_function<void()> failed,
    std::shared_ptr<std::optional<std::int32_t>> reserved){
 if(closing_||lane>=lanes_.size()||!lanes_[lane]||!lanes_[lane]->ready||!shared_->torrentSources){return false;}
 const auto policy=lanes_[lane]->policy;const auto versions=lanes_[lane]->versions;const auto sources=shared_->torrentSources;
 auto accepted=std::make_shared<bool>(false),revoked=std::make_shared<bool>(false);const auto target=torrent->target;
 auto denied=std::make_shared<std::move_only_function<void()>>(std::move(failed));
 return gate(end,[this,torrent,lane,withdrawing,accepted,revoked,effect=std::move(effect),denied]()mutable{
  if(*revoked&&torrent->target.live){torrent->target.live->store(false);}
  if(!*accepted||(!withdrawing&&(!torrent->target.live||!torrent->target.live->load()))){torrent->permitted=false;if(*denied){(*denied)();}return;}
  active_->operation->data.lanes[lane]=true;active_->operation->data.torrents.insert(torrent->target.live.get());torrent->permitted=!withdrawing;effect();
 },[denied]{if(*denied){(*denied)();}},[policy,versions,sources,target,withdrawing,reserved,accepted,revoked,end](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||Clock::now()>=end){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Torrent DHT operation expired"));}
  auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}
  auto checked=sources->check(target,withdrawing);if(!checked){return std::unexpected(checked.error());}*accepted=*checked;
  if(!*accepted&&!withdrawing){const auto permission=sources->check(target,true);*revoked=!permission||!*permission;}
  if(*accepted&&reserved){auto version=versions->next(QDateTime::currentMSecsSinceEpoch());if(!version){return std::unexpected(version.error());}*reserved=*version;}
  return Broker::Data{};
 });
}
void Owner::cancelTorrentLookups(const std::shared_ptr<Torrent>& torrent){
 for(const auto& job:torrentLookups_){if(job->torrent!=torrent){continue;}job->finished=true;if(lanes_[job->lane]&&lanes_[job->lane]->actor){for(const auto& [id,ticket]:job->tickets){(void)id;lanes_[job->lane]->actor->cancel(ticket);}}job->tickets.clear();}
 std::erase_if(torrentPending_,[&](const auto& job){return job->torrent==torrent;});
}
void Owner::syncTorrents(){
 std::vector<engine::AzureusTorrentTarget> targets;
 {std::lock_guard lock(shared_->mutex);if(!shared_->torrentTargetsPending){return;}shared_->torrentTargetsPending=false;targets.swap(shared_->torrentTargets);}
 std::set<std::string> retained;
 for(auto& target:targets){if(!target.live||!target.live->load()||!target.admission){continue;}const auto key=target.hash.hex();if(!retained.insert(key).second){continue;}
  auto at=torrents_.find(key);
  if(at!=torrents_.end()&&(at->second->target.admission!=target.admission||at->second->target.live!=target.live)){
   at->second->retiring=true;cancelTorrentLookups(at->second);retiringTorrents_.push_back(at->second);torrents_.erase(at);at=torrents_.end();
  }
  if(at==torrents_.end()){
   auto value=std::make_shared<Torrent>(std::move(target));const auto raw=value->target.hash.bytes();if(raw.size()<20){continue;}
   const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(raw.data()),20),QCryptographicHash::Sha1);
   std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),20,value->root.begin());torrents_.emplace(key,std::move(value));
  }else{at->second->target=std::move(target);}
 }
 for(auto at=torrents_.begin();at!=torrents_.end();){if(!retained.contains(at->first)){
   at->second->retiring=true;if(at->second->target.live){at->second->target.live->store(false);}cancelTorrentLookups(at->second);retiringTorrents_.push_back(at->second);at=torrents_.erase(at);
  }else{++at;}}
 changed();
}
bool Owner::beginTorrentLookup(std::shared_ptr<TorrentLookup> job){
 const auto i=job->lane;if(closing_||job->finished||!job->torrent->target.live||!job->torrent->target.live->load()||Clock::now()>=job->end||!lanes_[i]||!lanes_[i]->ready||torrentLookups_.size()>=8){return false;}
 auto& lane=*lanes_[i];const auto route=lane.actor->routingSnapshot();std::vector<control::AdmittedContact> seeds;
 if(route){for(std::size_t n=0;n<route->contactCount;++n){const auto& candidate=route->contacts[n].contact;if(candidate.id!=lane.local.id){seeds.push_back({candidate,true});}}}
 std::ranges::sort(seeds,[&](const auto& a,const auto& b){for(std::size_t n=0;n<20;++n){const auto x=a.contact.id[n]^job->key[n],y=b.contact.id[n]^job->key[n];if(x!=y){return x<y;}}return false;});if(seeds.size()>20){seeds.resize(20);}if(seeds.empty()){return false;}
 control::LookupOptions options;options.target=job->key;options.localId=lane.local.id;options.maximumValues=30;
 options.kind=job->read?control::LookupKind::Value:control::LookupKind::Node;options.flags=job->read?(job->torrent->target.seeding?2:1):0;
 options.timeoutMillis=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(job->end-Clock::now()).count());
 options.addressScope=owned_?control::LookupAddressScope::OwnedLoopback:job->read?control::LookupAddressScope::PublicValue:control::LookupAddressScope::PublicNode;
 if(!owned_){options.maximumContacts=control::LookupController::MaximumContacts;}
 if(!job->control.start(shared_->generation,++lookupId_,options,seeds,monotonic())){return false;}
 torrentLookups_.push_back(job);pumpTorrentLookup(std::move(job));return true;
}
void Owner::pumpTorrentLookup(std::shared_ptr<TorrentLookup> job){
 if(job->finished||job->queued||closing_){return;}job->queued=true;
 const bool queued=torrentGate(job->torrent,job->lane,false,std::min(job->end,Clock::now()+std::chrono::seconds(10)),[this,job]{
  job->queued=false;if(!torrentAllowed(*job->torrent,job->lane)){job->finished=true;return;}auto& actor=*lanes_[job->lane]->actor;
  auto intents=job->control.takeIntents(monotonic());if(!intents){job->finished=true;return;}
  for(const auto& intent:*intents){const auto peer=peerOf(intent.contact);
   if(!actor.attachKnownPeer(peer,intent.contact.protocol,job->end)){applied(job,job->control.finish(intent.node,control::LookupNodeState::Rejected,monotonic()));continue;}
   const auto timeout=std::min(std::chrono::milliseconds(15000),std::chrono::duration_cast<std::chrono::milliseconds>(job->end-Clock::now()));
   std::expected<udp::Ticket,udp::Failure> sent=std::unexpected(udp::Failure::State);
   if(job->read){sent=actor.findValue(peer,intent.contact.protocol,intent.target,intent.flags,intent.maximumValues,this,[this,job,node=intent.node](auto reply){torrentReply(job,node,std::move(reply));},timeout);}
   else{sent=actor.findNodeDetailed(peer,intent.contact.protocol,intent.target,this,[this,job,node=intent.node](auto reply){torrentNodeReply(job,node,std::move(reply));},timeout);}
   if(sent){if(!job->observed){job->observed=true;++torrentFacts_.lookups;}job->tickets[intent.node]=*sent;applied(job,job->control.sent(intent.node,sent->value,monotonic()));}
   else{applied(job,job->control.finish(intent.node,control::LookupNodeState::Failed,monotonic()));}
  }
  if(const auto cancels=job->control.cancellationIntents()){for(const auto& item:*cancels){const auto at=job->tickets.find(item.node);if(at!=job->tickets.end()){actor.cancel(at->second);}}}
  if(control::lookupTerminal(job->control.state())){finishTorrentLookup(job);}
 },[job]{job->queued=false;job->finished=true;});
 if(!queued){job->queued=false;job->finished=true;job->torrent->next[job->lane]=Clock::now()+std::chrono::seconds(5);}
}
void Owner::torrentNodeReply(std::shared_ptr<TorrentLookup> job,std::uint64_t node,udp::DetailedFindNodeOutcome outcome){
 if(job->finished||closing_){return;}auto reply=std::make_shared<udp::DetailedFindNodeOutcome>(std::move(outcome));
 torrentGate(job->torrent,job->lane,false,std::min(job->end,Clock::now()+std::chrono::seconds(10)),[this,job,node,reply]{
  if(!torrentAllowed(*job->torrent,job->lane)){job->finished=true;return;}
  const auto* decoded=*reply?std::get_if<udp::find::Decoded>(&**reply):nullptr;const auto* body=decoded?std::get_if<udp::find::ReplyBody>(&decoded->body()):nullptr;
  if(!body){applied(job,job->control.finish(node,control::LookupNodeState::Failed,monotonic()));}
  else{std::vector<control::AdmittedContact> contacts;for(const auto& contact:body->contacts){if(lanes_[job->lane]->actor->attachKnownPeer(peerOf(contact),contact.protocol,job->end)){contacts.push_back({contact,true});}}applied(job,job->control.contacts(node,contacts,false,monotonic()));}
  job->tickets.erase(node);later([this,job]{pumpTorrentLookup(job);});
 },[job]{job->finished=true;});
}
void Owner::torrentReply(std::shared_ptr<TorrentLookup> job,std::uint64_t node,udp::FindValueOutcome outcome){
 if(job->finished||closing_){return;}auto reply=std::make_shared<udp::FindValueOutcome>(std::move(outcome));
 torrentGate(job->torrent,job->lane,false,std::min(job->end,Clock::now()+std::chrono::seconds(10)),[this,job,node,reply]{
  if(!torrentAllowed(*job->torrent,job->lane)){job->finished=true;return;}auto& actor=*lanes_[job->lane]->actor;
  const auto* body=*reply?std::get_if<udp::lookup::ReplyBody>(&(*reply)->body()):nullptr;
  if(!body){applied(job,job->control.finish(node,control::LookupNodeState::Failed,monotonic()));job->tickets.erase(node);}
  else{
   if(const auto* values=std::get_if<udp::lookup::FoundValues>(&body->result)){
    if(values->diversification!=1&&!job->diversified){diversifyTorrent(job,values->diversification);}
    applied(job,job->control.values(node,values->values,body->continuation,values->diversification,monotonic()));
    engine::AzureusPeerBatch batch{job->torrent->target.hash,job->torrent->target.admission,job->torrent->target.live,{}};
    for(const auto& record:values->values){const auto parsed=engine::decodeAzureusPeer(record.bytes,record.flags,endpoint(peerOf(record.originator)),owned_);if(!parsed){continue;}
     if(parsed->tcp==endpoint({lanes_[job->lane]->actor->endpoint().address,job->torrent->target.tcpPort})){continue;}
     if(std::ranges::find(batch.peers,*parsed)==batch.peers.end()&&batch.peers.size()<engine::MaximumAzureusPeerHints){batch.peers.push_back(*parsed);}
    }
    if(!batch.peers.empty()){torrentFacts_.discoveredPeers+=batch.peers.size();std::lock_guard lock(shared_->mutex);
     auto at=std::ranges::find_if(shared_->torrentPeers,[&](const auto& old){return old.hash==batch.hash&&old.admission==batch.admission&&old.live==batch.live;});
     if(at==shared_->torrentPeers.end()){if(shared_->torrentPeers.size()<256){shared_->torrentPeers.push_back(std::move(batch));}}
     else{for(const auto& peer:batch.peers){if(at->peers.size()<engine::MaximumAzureusPeerHints&&std::ranges::find(at->peers,peer)==at->peers.end()){at->peers.push_back(peer);}}}
    }
   }else if(const auto* found=std::get_if<udp::lookup::FoundContacts>(&body->result)){
    std::vector<control::AdmittedContact> contacts;for(const auto& contact:found->contacts){if(actor.attachKnownPeer(peerOf(contact),contact.protocol,job->end)){contacts.push_back({contact,true});}}applied(job,job->control.contacts(node,contacts,body->continuation,monotonic()));
   }
   if(!body->continuation){job->tickets.erase(node);}
  }
  later([this,job]{pumpTorrentLookup(job);});changed();
 },[job]{job->finished=true;});
}
void Owner::diversifyTorrent(std::shared_ptr<TorrentLookup> job,std::int8_t type){
 job->diversified=true;if((type!=2&&type!=3)||job->depth>=4){return;}
 const unsigned a=QRandomGenerator::system()->bounded(10U),b=(a+1+QRandomGenerator::system()->bounded(9U))%10;
 auto& fixed=job->torrent->fixedPut[job->lane];auto where=fixed.find(job->key);if(where==fixed.end()){if(fixed.size()>=32){return;}where=fixed.try_emplace(job->key,a,b).first;}
 std::set<unsigned> reads;if(job->read){reads.insert(a);if(type==3){reads.insert(b);}}
 std::set<unsigned> writes;if(job->write){if(type==2){for(unsigned n=0;n<10;++n){writes.insert(n);}}else{writes.insert(where->second.first);writes.insert(where->second.second);}}
 auto offsets=reads;offsets.insert(writes.begin(),writes.end());
 for(const auto offset:offsets){if(job->group->seen.size()>=32||torrentPending_.size()>=128){break;}const auto key=msg::diversifiedDiscoveryKey(job->key,offset);if(!key||!job->group->seen.insert(*key).second){continue;}
  auto child=std::make_shared<TorrentLookup>();child->torrent=job->torrent;child->lane=job->lane;child->key=*key;child->group=job->group;child->depth=job->depth+1;child->end=job->end;child->read=reads.contains(offset);child->write=writes.contains(offset);torrentPending_.push_back(std::move(child));
 }
}
void Owner::finishTorrentLookup(std::shared_ptr<TorrentLookup> job){
 if(job->finished){return;}job->finished=true;
 if(job->write&&!job->diversified&&job->control.state()==control::LookupState::Complete){publishTorrent(job);}
 changed();
}
void Owner::publishTorrent(std::shared_ptr<TorrentLookup> job,bool withdrawing){
 if(closing_||!lanes_[job->lane]||!lanes_[job->lane]->ready||!job->torrent->publicationQueued[job->lane].insert(job->key).second){return;}
 if(torrentPublications_.size()+torrentPublicationPreflights_>=5){
  if(torrentPublicationPending_.size()<128){if(withdrawing){torrentPublicationPending_.emplace_front(job,true);}else{torrentPublicationPending_.emplace_back(job,false);}}
  else{job->torrent->publicationQueued[job->lane].erase(job->key);job->torrent->next[job->lane]=Clock::now()+std::chrono::seconds(5);}
  return;
 }
 ++torrentPublicationPreflights_;
 auto reserved=std::make_shared<std::optional<std::int32_t>>();
 const bool queued=torrentGate(job->torrent,job->lane,withdrawing,Clock::now()+std::chrono::seconds(15),[this,job,withdrawing,reserved]{
  --torrentPublicationPreflights_;job->torrent->publicationQueued[job->lane].erase(job->key);
  if(!torrentAllowed(*job->torrent,job->lane,withdrawing)||!reserved->has_value()){return;}auto& lane=*lanes_[job->lane];
  const std::string key(reinterpret_cast<const char*>(job->key.data()),20);PublicationClock clock;
  const auto flags=job->torrent->target.seeding?db::LocalPublicationFlags::Seeding:db::LocalPublicationFlags::Downloading;
  const auto payload=engine::encodeAzureusPeer(job->torrent->target.tcpPort,job->torrent->target.udpPort,lane.actor->endpoint().port,job->torrent->target.crypto);
  std::optional<services::azureus_db_value::DbValue> value;
  if(withdrawing){auto removed=lane.database->removeLocal(key,**reserved,clock);if(!removed){return;}value=std::move(*removed);
   if(!value){const auto temporary=lane.database->putLocal(key,payload,**reserved,clock,flags);if(!temporary){return;}removed=lane.database->removeLocal(key,**reserved,clock);if(!removed){return;}value=std::move(*removed);}
  }else{auto put=lane.database->putLocal(key,payload,**reserved,clock,flags);if(!put){return;}value=std::move(*put);}
  if(!value){return;}
  if(withdrawing){localTorrentKeys_[job->lane].erase(key);}else{localTorrentKeys_[job->lane].insert_or_assign(key,job->torrent);job->torrent->published[job->lane].try_emplace(job->key);}
  auto publication=std::make_shared<TorrentPublication>();publication->lookup=job;publication->withdrawing=withdrawing;publication->end=Clock::now()+std::chrono::seconds(60);
  publication->value={value->version(),value->creationTime(),withdrawing?std::string{}:payload,lane.local,value->flags(),value->lifeTimeHours(),value->replicationControl()};
  if(withdrawing){const auto at=job->torrent->published[job->lane].find(job->key);if(at!=job->torrent->published[job->lane].end()){publication->destinations=at->second;job->torrent->published[job->lane].erase(at);}}
  else if(const auto snapshot=job->control.snapshot()){for(const auto& node:snapshot->nodes){if(node.state==control::LookupNodeState::Replied){publication->destinations.push_back(node.contact);}}}
  std::ranges::sort(publication->destinations,[&](const auto& a,const auto& b){for(std::size_t n=0;n<20;++n){const auto x=a.id[n]^job->key[n],y=b.id[n]^job->key[n];if(x!=y){return x<y;}}return false;});if(publication->destinations.size()>20){publication->destinations.resize(20);}
  if(publication->destinations.empty()){return;}torrentPublications_.push_back(publication);pumpTorrentPublication(std::move(publication));changed();
 },[this,job,withdrawing]{--torrentPublicationPreflights_;job->torrent->publicationQueued[job->lane].erase(job->key);if(withdrawing){forgetLocalTorrent(job->torrent,job->lane,job->key);job->torrent->published[job->lane].erase(job->key);}else{job->torrent->next[job->lane]=Clock::now()+std::chrono::seconds(30);}},reserved);
 if(!queued){--torrentPublicationPreflights_;job->torrent->publicationQueued[job->lane].erase(job->key);if(withdrawing){forgetLocalTorrent(job->torrent,job->lane,job->key);job->torrent->published[job->lane].erase(job->key);}}
}
void Owner::pumpTorrentPublication(std::shared_ptr<TorrentPublication> publication){
 if(publication->done||publication->queued||closing_||publication->pending||Clock::now()>=publication->end){return;}const auto job=publication->lookup;
 if(publication->next>=publication->destinations.size()){publication->done=true;return;}
 publication->queued=true;const bool queued=torrentGate(job->torrent,job->lane,publication->withdrawing,std::min(publication->end,Clock::now()+std::chrono::seconds(10)),[this,publication,job]{
  publication->queued=false;
  if(!torrentAllowed(*job->torrent,job->lane,publication->withdrawing)){publication->done=true;return;}
  for(unsigned count=0;count<2&&publication->next<publication->destinations.size();++count){const auto candidate=publication->destinations[publication->next++];const auto peer=peerOf(candidate);++publication->pending;
   auto ticket=lanes_[job->lane]->actor->requestStoreToken(peer,candidate.protocol,this,[this,publication,job,peer,version=candidate.protocol](auto outcome){
    const auto* decoded=outcome?std::get_if<udp::find::Decoded>(&*outcome):nullptr;const auto* reply=decoded?std::get_if<udp::find::ReplyBody>(&decoded->body()):nullptr;
    if(closing_||publication->done||!reply){if(publication->pending){--publication->pending;}return;}const auto nonce=reply->randomId;
    if(!torrentGate(job->torrent,job->lane,publication->withdrawing,std::min(publication->end,Clock::now()+std::chrono::seconds(10)),[this,publication,job,peer,version,nonce]{
     if(!torrentAllowed(*job->torrent,job->lane,publication->withdrawing)){if(publication->pending){--publication->pending;}return;}
     udp::StoreRequest request;request.randomId=nonce;request.keys={std::string(reinterpret_cast<const char*>(job->key.data()),20)};request.valueSets={{publication->value}};
     auto sent=lanes_[job->lane]->actor->store(peer,version,std::move(request),this,[this,publication,job,peer,version](auto result){
      if(publication->pending){--publication->pending;}if(closing_||publication->done||!result){return;}const auto* decoded=std::get_if<udp::storage::Decoded>(&*result);const auto* reply=decoded?std::get_if<udp::storage::ReplyBody>(&decoded->body()):nullptr;if(!reply){return;}
      if(publication->withdrawing){++torrentFacts_.withdrawals;}
      else{++torrentFacts_.publications;
       if(job->torrent->target.live&&job->torrent->target.live->load()&&reply->diversification&&!reply->diversification->empty()){const auto type=static_cast<std::int8_t>(reply->diversification->front());if(type!=1){diversifyTorrent(job,type);}}
      }changed();
     },std::chrono::seconds(15));
     // A sent STORE can succeed remotely even when its reply is lost or pause
     // wins the completion race. Withdraw from every actual attempted receiver.
     if(sent&&!publication->withdrawing){auto& destinations=job->torrent->published[job->lane][job->key];const auto candidate=::biglybtc::app::contact(peer,version);if(destinations.size()<20&&std::ranges::none_of(destinations,[&](const auto& old){return old.address==candidate.address;})){destinations.push_back(candidate);}}
     if(!sent&&publication->pending){--publication->pending;}
    },[publication]{if(publication->pending){--publication->pending;}publication->done=true;})){if(publication->pending){--publication->pending;}}
   },std::chrono::seconds(15));if(!ticket&&publication->pending){--publication->pending;}
  }
 },[publication]{publication->queued=false;publication->done=true;});
 if(!queued){publication->queued=false;publication->done=true;}
}
void Owner::forgetLocalTorrent(const std::shared_ptr<Torrent>& torrent,std::size_t lane,const nodes::NodeId& key){
 if(lanes_[lane]&&lanes_[lane]->database){PublicationClock clock;static_cast<void>(lanes_[lane]->database->removeLocal(std::string(reinterpret_cast<const char*>(key.data()),20),0,clock));}localTorrentKeys_[lane].erase(std::string(reinterpret_cast<const char*>(key.data()),20));torrent->permitted=false;
}
bool Owner::torrentReadPermit(std::size_t lane,std::string key,std::move_only_function<void(bool)> done){
 std::shared_ptr<Torrent> local;const auto found=localTorrentKeys_[lane].find(key);if(found!=localTorrentKeys_[lane].end()){local=found->second;}
 auto callback=std::make_shared<std::move_only_function<void(bool)>>(std::move(done));
 if(!local){return gate(Clock::now()+std::chrono::seconds(5),[this,lane,callback]{(*callback)(allowed(lane));},[callback]{(*callback)(false);});}
 const auto policy=lanes_[lane]->policy;const auto sources=shared_->torrentSources;const auto target=local->target;auto accepted=std::make_shared<bool>(false);
 return gate(Clock::now()+std::chrono::seconds(10),[this,lane,key=std::move(key),local,accepted,callback]{
  active_->operation->data.lanes[lane]=true;
  if(!*accepted||!local->target.live||!local->target.live->load()){nodes::NodeId id{};if(key.size()==20){std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()),20,id.begin());forgetLocalTorrent(local,lane,id);}}
  (*callback)(allowed(lane));
 },[callback]{(*callback)(false);},[policy,sources,target,accepted](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||!sources){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Torrent read preflight cancelled"));}auto current=policy->validate();if(!current){return std::unexpected(current.error());}auto source=sources->check(target);if(!source){return std::unexpected(source.error());}*accepted=*source;return Broker::Data{};
 });
}
void Owner::withdrawTorrent(const std::shared_ptr<Torrent>& torrent){
 for(std::size_t lane=0;lane<3;++lane){if(torrent->published[lane].empty()){continue;}auto at=torrent->published[lane].begin();
  auto job=std::make_shared<TorrentLookup>();job->torrent=torrent;job->lane=lane;job->key=at->first;job->group=std::make_shared<TorrentGroup>();job->finished=true;
  if(torrentPublications_.size()<5){publishTorrent(job,true);}
 }
}
void Owner::torrentTick(){
 const auto before=torrentFacts_;const auto now=Clock::now();if(now-lastTorrentTick_<std::chrono::seconds(1)){return;}lastTorrentTick_=now;
 for(auto& job:torrentLookups_){if(job->finished){continue;}if(!job->torrent->target.live||!job->torrent->target.live->load()){cancelTorrentLookups(job->torrent);}else{pumpTorrentLookup(job);}}
 std::erase_if(torrentLookups_,[](const auto& job){return job->finished;});
 while(!torrentPending_.empty()&&torrentLookups_.size()<8){auto job=torrentPending_.front();torrentPending_.pop_front();if(!beginTorrentLookup(job)&&Clock::now()<job->end){torrentPending_.push_back(std::move(job));break;}}
 for(const auto& publication:torrentPublications_){if(now>=publication->end){publication->done=true;}else{pumpTorrentPublication(publication);}}
 std::erase_if(torrentPublications_,[](const auto& publication){return publication->done;});
 while(!torrentPublicationPending_.empty()&&torrentPublications_.size()+torrentPublicationPreflights_<5){
  auto [job,withdrawing]=std::move(torrentPublicationPending_.front());torrentPublicationPending_.pop_front();job->torrent->publicationQueued[job->lane].erase(job->key);
  if(!withdrawing&&(!job->torrent->target.live||!job->torrent->target.live->load()||now>=job->end)){continue;}publishTorrent(std::move(job),withdrawing);
 }
 for(const auto& torrent:retiringTorrents_){withdrawTorrent(torrent);}
 for(const auto& [hash,torrent]:torrents_){(void)hash;if(!torrent->permitted){withdrawTorrent(torrent);}}
 std::erase_if(retiringTorrents_,[](const auto& torrent){return std::ranges::all_of(torrent->published,[](const auto& keys){return keys.empty();});});
 if(!torrents_.empty()){
  auto at=torrents_.upper_bound(torrentCursor_);if(at==torrents_.end()){at=torrents_.begin();}std::size_t checked=0;
  while(checked<std::min<std::size_t>(64,torrents_.size())&&torrentLookups_.size()<8){auto torrent=at->second;torrentCursor_=at->first;++checked;
   for(std::size_t lane=0;lane<3&&torrentLookups_.size()<8;++lane){if(!lanes_[lane]||!lanes_[lane]->ready||!torrent->target.live||!torrent->target.live->load()||now<torrent->next[lane]){continue;}
    const bool active=std::ranges::any_of(torrentLookups_,[&](const auto& job){return !job->finished&&job->torrent==torrent&&job->lane==lane;});if(active){continue;}
    auto job=std::make_shared<TorrentLookup>();job->torrent=torrent;job->lane=lane;job->key=torrent->root;job->end=now+std::chrono::seconds(120);job->group=std::make_shared<TorrentGroup>();job->group->seen.insert(job->key);
    const auto delay=std::min<std::size_t>(3600000,std::max<std::size_t>(120000,torrents_.size()*15000));torrent->next[lane]=now+std::chrono::milliseconds(delay);
    if(!beginTorrentLookup(job)){torrent->next[lane]=now+std::chrono::seconds(5);}
   }
   if(++at==torrents_.end()){at=torrents_.begin();}
  }
 }
 torrentFacts_.deliveredPeers=shared_->deliveredTorrentPeers.load(std::memory_order_relaxed);
 torrentFacts_.eligible=std::ranges::count_if(torrents_,[](const auto& entry){return entry.second->permitted&&entry.second->target.live&&entry.second->target.live->load();});
 torrentFacts_.pending=torrentLookups_.size()+torrentPending_.size()+torrentPublications_.size()+torrentPublicationPreflights_+torrentPublicationPending_.size();torrentFacts_.readyNetworks=std::ranges::count_if(lanes_,[](const auto& lane){return lane&&lane->ready;});
 torrentFacts_.state=torrentFacts_.readyNetworks?"Running":"Connecting";
 if(now-lastTorrentConsole_>=std::chrono::seconds(25)){lastTorrentConsole_=now;engine::detail::runtimeConsole("azureus.torrents")<<" networks="<<torrentFacts_.readyNetworks<<" eligible="<<torrentFacts_.eligible<<" pending="<<torrentFacts_.pending<<" lookups="<<torrentFacts_.lookups<<" stores="<<torrentFacts_.publications<<" withdrawals="<<torrentFacts_.withdrawals<<" peers="<<torrentFacts_.discoveredPeers<<'\n'<<std::flush;}
 bool transportChanged=false;
 for(std::size_t lane=0;lane<3;++lane){if(!lanes_[lane]||!lanes_[lane]->actor){continue;}const auto statistics=lanes_[lane]->actor->statistics();const auto route=lanes_[lane]->actor->routingSnapshot();
  const std::array<std::uint64_t,6> stamp{statistics.sent,statistics.received,statistics.replies,statistics.timeouts,statistics.cancelled,route?route->revision:0};
  if(stamp!=torrentTransportStamp_[lane]){torrentTransportStamp_[lane]=stamp;transportChanged=true;}
 }
 if(before!=torrentFacts_||transportChanged){changed();}
}

void Owner::captureTorrentContacts(std::size_t lane){
 if(!allowed(lane)||!lanes_[lane]||!lanes_[lane]->contacts||!lanes_[lane]->actor){return;}
 const auto exported=lanes_[lane]->actor->exportRoutingContacts();if(!exported){return;}
 std::vector<storage::AzureusSavedContact> values;for(const auto& entry:*exported){if(!entry.contact.address||!entry.contact.udp){continue;}const auto& address=*entry.contact.address;values.push_back({entry.timeAlive,entry.contact.protocol,address.length,address.bytes,address.port});}
 if(values!=lastTorrentContacts_[lane]){lastTorrentContacts_[lane]=std::move(values);contactDirty_[lane]=true;}
}
void Owner::saveTorrentContacts(bool closing){
 for(std::size_t lane=0;lane<3;++lane){if(!contactDirty_[lane]||contactSaves_[lane]||!lanes_[lane]||!lanes_[lane]->contacts){continue;}
  const auto contacts=lanes_[lane]->contacts;const auto values=lastTorrentContacts_[lane];const auto end=closing?closeEnd_:Clock::now()+std::chrono::seconds(5);
  auto submitted=shared_->networkIo->submit([contacts,values,end](std::stop_token stop)->Broker::Result{auto saved=contacts->save(values,stop,end);if(!saved){return std::unexpected(saved.error());}return Broker::Data{};});
  if(submitted){contactSaves_[lane]=std::move(*submitted);contactDirty_[lane]=false;}
 }
}
void Owner::drainTorrentContactSaves(){
 for(auto& handle:contactSaves_){if(!handle){continue;}if(auto ready=Broker::take(handle)){if(*ready){++torrentFacts_.contactSaves;changed();}Broker::acknowledge(handle);}
  if(Broker::released(handle)){shared_->networkIo->release(handle);handle.reset();}
 }
}
