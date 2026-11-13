// SPDX-License-Identifier: GPL-2.0-or-later
// Original azrating DDB requests use the sole shared Public UDP/DHT owner.
bool Owner::ratingGate(std::shared_ptr<RatingWork> work,std::size_t lane,Clock::time_point end,
 std::move_only_function<void()> effect,std::move_only_function<void()> failed,
 std::shared_ptr<std::optional<std::int32_t>> version){
 if((work->view.byHash&&(work->view.publishing||version))||closing_||!work->alive||lane>=2||!lanes_[lane]||!lanes_[lane]->ready){return false;}
 const auto lease=shared_->lease;const auto policy=lanes_[lane]->policy;const auto versions=lanes_[lane]->versions;
 auto source=std::make_shared<std::optional<RatingSource>>();
 auto denied=std::make_shared<std::move_only_function<void()>>(std::move(failed));const bool byHash=work->view.byHash;const auto expected=work->source?std::optional{ratingSourceRevision(*work->source)}:work->expectedSource;
 return gate(end,[this,work,lane,source,effect=std::move(effect),denied]()mutable{
  if(!work->alive||!*source||(work->source&&ratingSourceRevision(*work->source)!=ratingSourceRevision(**source))){if(*denied){(*denied)();}return;}
  work->source=std::move(**source);active_->operation->data.lanes[lane]=true;effect();
 },[denied]{if(*denied){(*denied)();}},[lease,policy,versions,work,byHash,expected,source,version,end](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||Clock::now()>=end){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Rating operation expired"));}
  auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}
  auto current=readRatingSource(*lease,work->hash,byHash);if(!current){return std::unexpected(current.error());}
  if(expected&&ratingSourceRevision(*current)!=*expected){return std::unexpected(failure(storage::ErrorCode::Conflict,"Rating contribution or source changed"));}
  *source=std::move(*current);if(version){auto next=versions->next(QDateTime::currentMSecsSinceEpoch());if(!next){return std::unexpected(next.error());}*version=*next;}return Broker::Data{};
 });
}
void Owner::ratingForget(std::shared_ptr<RatingWork> work){
 for(std::size_t i=0;i<2;++i){for(auto at=localRatingKeys_[i].begin();at!=localRatingKeys_[i].end();){if(work->view.publishing&&work->source&&at->second.hash==work->hash&&at->second.revision==ratingSourceRevision(*work->source)){PublicationClock clock;if(lanes_[i]&&lanes_[i]->database){static_cast<void>(lanes_[i]->database->removeLocal(at->first,0,clock));}at=localRatingKeys_[i].erase(at);}else{++at;}}}
}
void Owner::ratingCancel(std::shared_ptr<RatingWork> work,std::string status,bool removeLocal){
 work->alive=false;work->view.running=false;work->view.current=false;work->view.ratings={};work->view.localFallback=false;work->view.status=std::move(status);if(removeLocal){ratingForget(work);}
 for(auto& lane:work->lanes){lane.finished=true;if(lanes_[lane.lane]&&lanes_[lane.lane]->actor){for(const auto& [id,ticket]:lane.tickets){(void)id;lanes_[lane.lane]->actor->cancel(ticket);}for(const auto& ticket:lane.storeTickets){lanes_[lane.lane]->actor->cancel(ticket);}}lane.tickets.clear();lane.storeTickets.clear();}ratingChanged(work);
}
void Owner::ratingStart(RatingCommand command){
 if(command.byHash&&command.publish){return;}const auto hash=ratingWorkKey(command.hash,command.byHash);
 if(command.byHash&&!command.cancel){if(auto active=ratings_.find(hash);active!=ratings_.end()&&active->second->alive&&active->second->view.running){return;}}
 if(!command.cancel){if(auto active=ratings_.find(hash);active!=ratings_.end()&&active->second->view.running&&active->second->view.publishing!=command.publish){if(ratingDeferred_.size()<64&&std::ranges::none_of(ratingDeferred_,[&](const auto& old){return old.hash==command.hash&&old.publish==command.publish&&old.byHash==command.byHash;})){ratingDeferred_.push_back(std::move(command));}return;}}
 else{std::erase_if(ratingDeferred_,[&](const auto& old){return old.hash==command.hash&&old.byHash==command.byHash;});}
 if(auto old=ratings_.find(hash);old!=ratings_.end()){ratingCancel(old->second,command.cancel?"Rating lookup cancelled":"Rating request replaced",command.cancel);if(command.cancel){return;}ratings_.erase(old);}else if(command.cancel){return;}
 if(ratings_.size()>=128){auto oldest=ratings_.end();for(auto at=ratings_.begin();at!=ratings_.end();++at){if(!at->second->view.running&&(oldest==ratings_.end()||at->second->view.request<oldest->second->view.request)){oldest=at;}}if(oldest==ratings_.end()){return;}ratingCancel(oldest->second,"Rating result released",false);ratings_.erase(oldest);}
 if(ratingSequence_==UINT64_MAX){return;}auto work=std::make_shared<RatingWork>(command.hash);work->view.hash=command.hash.hex();work->view.byHash=command.byHash;work->view.request=++ratingSequence_;work->view.running=true;work->view.publishing=command.publish;work->view.status="Waiting for Public rating network…";work->end=Clock::now()+std::chrono::seconds(command.publish?60:command.byHash?30:20);for(std::size_t i=0;i<2;++i){work->lanes[i].lane=i;}ratingChanged(work);ratings_.emplace(hash,std::move(work));
}
void Owner::ratingAdd(std::shared_ptr<RatingWork> work,RatingLane& lane,const services::azureus_values::Value& value){
 if(!work->alive){return;}std::string identity(reinterpret_cast<const char*>(value.originator.id.data()),20);identity+=value.bytes;
 if(lane.seen.size()>=256||!lane.seen.insert(std::move(identity)).second){return;}
 if(value.bytes.size()>4*1024*1024-work->rawValueBytes){ratingCancel(work,"DDB read byte limit reached");return;}work->rawValueBytes+=value.bytes.size();
 const auto raw=std::span(reinterpret_cast<const std::uint8_t*>(value.bytes.data()),value.bytes.size());auto envelope=services::azureus_ddb::decodeNormalValue(value.flags,raw);if(!envelope){ratingCancel(work,"DDB value exceeds the transport limit");return;}
 if(envelope->fault!=services::azureus_ddb::Fault::None){++work->view.decodeErrors;}
 for(unsigned n=0;n<envelope->count;++n){if(work->decodedEvents==16384){ratingCancel(work,"DDB decoded event limit reached");return;}++work->decodedEvents;++work->view.received;const auto range=envelope->entries[n];auto decoded=plugins::rating::decode(value.bytes.substr(range.offset,range.length));if(!decoded){++work->view.decodeErrors;continue;}const auto added=work->view.ratings.add(decoded->value,true);if(!added||!decoded->complete()){++work->view.decodeErrors;}}
 if(envelope->continuation){const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(lane.chainKey.data()),lane.chainKey.size()),QCryptographicHash::Sha1);nodes::NodeId next{};std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),20,next.begin());if(!lane.readKeys.contains(next)){if(lane.readKeys.size()==32){ratingCancel(work,"DDB continuation limit reached");return;}lane.readKeys.insert(next);lane.pendingReads.push_back({next,next,0,256});}}
}
// Remote diversification is shared across requests within this owner. Like Java,
// read offsets vary; size-write offsets stay fixed for the lifetime of the entry.
bool Owner::ratingNext(std::shared_ptr<RatingWork> work,std::size_t i){
 auto& p=work->lanes[i];if(!work->alive||p.pendingReads.empty()||Clock::now()>=work->end){p.finished=true;return false;}
 for(const auto& [node,ticket]:p.tickets){(void)node;lanes_[i]->actor->cancel(ticket);}for(const auto& ticket:p.storeTickets){lanes_[i]->actor->cancel(ticket);}
 const auto next=p.pendingReads.front();p.pendingReads.pop_front();p.queryKey=next.key;p.chainKey=next.chain;p.depth=next.depth;p.maximumValues=next.maximumValues;p.diversified=false;
 // Retire callbacks before resetting node IDs, including already queued gates.
 p.queryEpoch=0;p.started=false;p.storing=false;p.consumed=0;p.nextStore=0;p.pendingStores=0;p.seen.clear();p.tickets.clear();p.storeTickets.clear();p.destinations.clear();p.control=control::LookupController{};
 later([this,work,i]{ratingPump(work,i);});return true;
}
void Owner::ratingDiversify(std::shared_ptr<RatingWork> work,std::size_t i,std::int8_t type){
 auto& p=work->lanes[i];if(!work->alive||p.diversified||type==1){return;}p.diversified=true;
 if(type!=2&&type!=3){ratingCancel(work,"Unsupported DDB diversification type");return;}if(p.depth>=4){ratingCancel(work,"DDB diversification depth limit reached");return;}
 auto& cache=ratingDiversifications_[i];const auto now=Clock::now();std::erase_if(cache,[now](const auto& entry){return entry.second.expiry<=now;});auto entry=cache.find(p.queryKey);
 if(entry==cache.end()){if(cache.size()==256){ratingCancel(work,"DDB diversification cache limit reached");return;}const auto a=QRandomGenerator::system()->bounded(10U),b=(a+1+QRandomGenerator::system()->bounded(9U))%10;entry=cache.emplace(p.queryKey,RatingDiversification{type,{a,b},now+std::chrono::hours(48)+std::chrono::milliseconds(QRandomGenerator::system()->bounded(86400000U))}).first;}
 // An existing remote key keeps its original type and fixed publication offsets.
 type=entry->second.type;std::array<unsigned,10> offsets{};unsigned count=0;
 if(work->view.publishing){if(type==2){for(unsigned n=0;n<10;++n){offsets[count++]=n;}}else{offsets[0]=entry->second.fixed[0];offsets[1]=entry->second.fixed[1];count=2;}}
 else{offsets[0]=QRandomGenerator::system()->bounded(10U);count=1;if(type==3){offsets[1]=(offsets[0]+1+QRandomGenerator::system()->bounded(9U))%10;count=2;}}
 const auto remaining=p.maximumValues>p.consumed?p.maximumValues-p.consumed:0;if(!work->view.publishing&&!remaining){return;}
 for(unsigned n=0;n<count;++n){const auto key=msg::diversifiedDiscoveryKey(p.queryKey,offsets[n]);if(!key){ratingCancel(work,"DDB key derivation failed");return;}if(p.readKeys.contains(*key)){continue;}if(p.readKeys.size()==32){ratingCancel(work,"DDB branch limit reached");return;}p.readKeys.insert(*key);p.pendingReads.push_back({*key,p.chainKey,p.depth+1,std::uint16_t(remaining)});}
}
void Owner::ratingNetworkFailed(std::shared_ptr<RatingWork> work,std::size_t i,std::string status){
 work->lanes[i].queued=false;if(!work->alive||work->finishing){return;}if(Clock::now()>=work->end){work->lanes[i].finished=true;return;}ratingCancel(work,std::move(status));
}
void Owner::ratingPump(std::shared_ptr<RatingWork> work,std::size_t i){
 auto& lane=work->lanes[i];if(!work->alive||work->finishing||lane.queued||lane.finished||closing_||!lanes_[i]||!lanes_[i]->ready){return;}
 if(lane.storing){ratingStore(work,i);return;}
 if(work->source&&!lane.started){const auto route=lanes_[i]->actor->routingSnapshot();bool candidate=false;if(route){for(std::size_t n=0;n<route->contactCount;++n){candidate|=route->contacts[n].contact.id!=lanes_[i]->local.id;}}if(!candidate){return;}}
 lane.queued=true;
 auto failed=[this,work,i]{ratingNetworkFailed(work,i,"Rating source unavailable or changed");};
 if(!ratingGate(work,i,std::min(work->end,Clock::now()+std::chrono::seconds(10)),[this,work,i]{
  auto& part=work->lanes[i];part.queued=false;if(!work->alive||work->finishing||part.finished){return;}auto& net=*lanes_[i];
  if(!part.started){
   const auto& source=*work->source;const auto& applicationKey=ratingSourceKey(source);const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(applicationKey.data()),applicationKey.size()),QCryptographicHash::Sha1);std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),20,work->key.begin());
   if(!part.keyReady){part.queryKey=work->key;part.chainKey=work->key;part.readKeys.insert(part.queryKey);part.keyReady=true;}
   if(auto cached=ratingDiversifications_[i].find(part.queryKey);cached!=ratingDiversifications_[i].end()&&cached->second.expiry>Clock::now()){ratingDiversify(work,i,cached->second.type);if(work->alive){ratingNext(work,i);}return;}
   if(work->view.publishing){const auto* owned=std::get_if<storage::DistributedRatingSnapshot>(&source);if(!owned){ratingCancel(work,"No owned rating publication target");return;}const auto& local=owned->local.local.value;auto payload=local.encode();if(!local.needsPublishing()||local.score>5||!payload||payload->size()>storage::MaximumDdbRatingBytes){ratingCancel(work,"No publishable rating within the original DDB limit");return;}work->payload=std::move(*payload);}
   std::vector<control::AdmittedContact> seeds;if(const auto routing=net.actor->routingSnapshot()){for(std::size_t n=0;n<routing->contactCount;++n){const auto& c=routing->contacts[n].contact;if(c.id!=net.local.id&&(owned_?peerOf(c).address.isLoopback():publicAddress(peerOf(c)))){seeds.push_back({c,true});}}}
   std::ranges::sort(seeds,[key=part.queryKey](const auto& a,const auto& b){for(std::size_t n=0;n<20;++n){auto x=a.contact.id[n]^key[n],y=b.contact.id[n]^key[n];if(x!=y){return x<y;}}return false;});if(seeds.size()>20){seeds.resize(20);}if(seeds.empty()){return;}
   control::LookupOptions options;options.target=part.queryKey;options.localId=net.local.id;options.maximumValues=work->view.publishing?1:part.maximumValues;options.kind=work->view.publishing?control::LookupKind::Node:control::LookupKind::Value;options.valueBudget=work->view.publishing?control::LookupValueBudget::Bounded:control::LookupValueBudget::DistributedDatabase;options.addressScope=owned_?control::LookupAddressScope::OwnedLoopback:work->view.publishing?control::LookupAddressScope::PublicNode:control::LookupAddressScope::PublicValue;options.maximumContacts=owned_?128:512;options.timeoutMillis=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(work->end-Clock::now()).count());
   if(lookupId_==UINT64_MAX){ratingCancel(work,"DDB lookup sequence exhausted");return;}part.queryEpoch=++lookupId_;if(!part.control.start(shared_->generation,part.queryEpoch,options,seeds,monotonic())){ratingCancel(work,"Rating lookup could not start");return;}part.started=true;work->view.status=work->view.publishing?"Publishing saved rating…":"Reading Public ratings…";
   if(!work->view.publishing){PublicationClock clock;auto local=net.database->read(std::string(reinterpret_cast<const char*>(part.queryKey.data()),20),net.local,256,0,clock);if(local&&*local){for(const auto& value:(**local).values){ratingAdd(work,part,value);}}}
  }
  if(!work->alive){return;}auto intents=part.control.takeIntents(monotonic());if(!intents){ratingCancel(work,"Rating lookup failed");return;}
  for(const auto& intent:*intents){auto peer=peerOf(intent.contact);if(!net.actor->attachKnownPeer(peer,intent.contact.protocol,work->end)){static_cast<void>(part.control.finish(intent.node,control::LookupNodeState::Rejected,monotonic()));continue;}const auto timeout=std::min(std::chrono::milliseconds(15000),std::max(std::chrono::milliseconds(1),std::chrono::duration_cast<std::chrono::milliseconds>(work->end-Clock::now())));std::expected<udp::Ticket,udp::Failure> ticket=std::unexpected(udp::Failure::State);
   if(work->view.publishing){ticket=net.actor->findNodeDetailed(peer,intent.contact.protocol,intent.target,this,[this,work,i,id=intent.node,epoch=part.queryEpoch](auto reply){ratingNodeReply(work,i,id,std::move(reply),epoch);},timeout);}else{ticket=net.actor->findValue(peer,intent.contact.protocol,intent.target,0,intent.maximumValues,this,[this,work,i,id=intent.node,epoch=part.queryEpoch](auto reply){ratingReply(work,i,id,std::move(reply),epoch);},timeout);}
   if(ticket){part.tickets[intent.node]=*ticket;static_cast<void>(part.control.sent(intent.node,ticket->value,monotonic()));}else{static_cast<void>(part.control.finish(intent.node,control::LookupNodeState::Failed,monotonic()));}
  }
  if(auto cancels=part.control.cancellationIntents()){for(const auto& c:*cancels){if(auto at=part.tickets.find(c.node);at!=part.tickets.end()){net.actor->cancel(at->second);}}}
  if(control::lookupTerminal(part.control.state())){
   if(work->view.publishing&&part.control.state()==control::LookupState::Complete){const auto snapshot=part.control.snapshot();if(snapshot){for(const auto& node:snapshot->nodes){if(node.state==control::LookupNodeState::Replied){part.destinations.push_back(node.contact);}}}std::ranges::sort(part.destinations,[key=part.queryKey](const auto& a,const auto& b){for(std::size_t n=0;n<20;++n){const auto x=a.id[n]^key[n],y=b.id[n]^key[n];if(x!=y){return x<y;}}return false;});if(part.destinations.size()>20){part.destinations.resize(20);}part.storing=true;later([this,work,i]{ratingStore(work,i);});}else if(!work->view.publishing){ratingNext(work,i);}else{part.finished=true;}
  }ratingChanged(work);
 },failed)){failed();}
}
void Owner::ratingReply(std::shared_ptr<RatingWork> work,std::size_t i,std::uint64_t node,udp::FindValueOutcome outcome,std::uint64_t epoch){
 if(work->lanes[i].queryEpoch!=epoch||!work->alive||work->finishing||work->lanes[i].finished||closing_){return;}auto reply=std::make_shared<udp::FindValueOutcome>(std::move(outcome));
 auto failed=[this,work,i,epoch]{if(work->lanes[i].queryEpoch!=epoch){return;}ratingNetworkFailed(work,i,"Rating source unavailable or changed");};
 if(!ratingGate(work,i,std::min(work->end,Clock::now()+std::chrono::seconds(10)),[this,work,i,node,reply,epoch]{auto& part=work->lanes[i];if(part.queryEpoch!=epoch||!work->alive||work->finishing||part.finished){return;}const auto* body=*reply?std::get_if<udp::lookup::ReplyBody>(&(*reply)->body()):nullptr;
  if(!body){static_cast<void>(part.control.finish(node,control::LookupNodeState::Failed,monotonic()));part.tickets.erase(node);}else{
   if(const auto* values=std::get_if<udp::lookup::FoundValues>(&body->result)){ratingDiversify(work,i,values->diversification);if(!work->alive){return;}auto applied=part.control.values(node,values->values,body->continuation,1,monotonic());if(!applied){ratingCancel(work,"Unsupported or excessive rating reply");return;}if(auto snapshot=part.control.snapshot()){while(part.consumed<snapshot->results.size()){ratingAdd(work,part,snapshot->results[part.consumed++].value);}}}
   else if(const auto* found=std::get_if<udp::lookup::FoundContacts>(&body->result)){std::vector<control::AdmittedContact> contacts;for(const auto& c:found->contacts){if(lanes_[i]->actor->attachKnownPeer(peerOf(c),c.protocol,work->end)){contacts.push_back({c,true});}}if(!part.control.contacts(node,contacts,body->continuation,monotonic())){ratingCancel(work,"Rating contact budget exceeded");return;}}
   if(!body->continuation){part.tickets.erase(node);}
  }later([this,work,i]{ratingPump(work,i);});ratingChanged(work);
 },failed)){failed();}
}
void Owner::ratingNodeReply(std::shared_ptr<RatingWork> work,std::size_t i,std::uint64_t node,udp::DetailedFindNodeOutcome outcome,std::uint64_t epoch){
 if(work->lanes[i].queryEpoch!=epoch||!work->alive||work->finishing||work->lanes[i].finished||closing_){return;}auto reply=std::make_shared<udp::DetailedFindNodeOutcome>(std::move(outcome));auto failed=[this,work,i,epoch]{if(work->lanes[i].queryEpoch!=epoch){return;}ratingNetworkFailed(work,i,"Rating source unavailable or changed");};
 if(!ratingGate(work,i,std::min(work->end,Clock::now()+std::chrono::seconds(10)),[this,work,i,node,reply,epoch]{auto& part=work->lanes[i];if(part.queryEpoch!=epoch||!work->alive||work->finishing||part.finished){return;}const auto* decoded=*reply?std::get_if<udp::find::Decoded>(&**reply):nullptr;const auto* body=decoded?std::get_if<udp::find::ReplyBody>(&decoded->body()):nullptr;
  if(!body){static_cast<void>(part.control.finish(node,control::LookupNodeState::Failed,monotonic()));}else{std::vector<control::AdmittedContact> contacts;for(const auto& c:body->contacts){if(lanes_[i]->actor->attachKnownPeer(peerOf(c),c.protocol,work->end)){contacts.push_back({c,true});}}if(!part.control.contacts(node,contacts,false,monotonic())){ratingCancel(work,"Rating contact budget exceeded");return;}}part.tickets.erase(node);later([this,work,i]{ratingPump(work,i);});
 },failed)){failed();}
}
void Owner::ratingStore(std::shared_ptr<RatingWork> work,std::size_t i){
 auto& part=work->lanes[i];if(!work->alive||work->finishing||part.finished||part.queued||part.pendingStores||closing_){return;}if(part.nextStore>=part.destinations.size()){ratingNext(work,i);return;}part.queued=true;auto version=std::make_shared<std::optional<std::int32_t>>();auto failed=[this,work,i]{ratingNetworkFailed(work,i,"Rating publication source unavailable or changed");};
 if(!ratingGate(work,i,std::min(work->end,Clock::now()+std::chrono::seconds(10)),[this,work,i,version]{auto& lane=work->lanes[i];lane.queued=false;if(!work->alive||work->finishing||lane.finished){return;}if(!lane.value){PublicationClock clock;auto record=lanes_[i]->database->putLocal(std::string(reinterpret_cast<const char*>(work->key.data()),20),work->payload,**version,clock,db::LocalPublicationFlags::Anonymous);if(!record){ratingCancel(work,"Rating local database is full");return;}lane.value=services::azureus_values::Value{record->version(),record->creationTime(),work->payload,lanes_[i]->local,record->flags(),record->lifeTimeHours(),record->replicationControl()};localRatingKeys_[i].insert_or_assign(std::string(reinterpret_cast<const char*>(work->key.data()),20),RatingLocal{work->hash,ratingSourceRevision(*work->source)});}
  for(unsigned n=0;n<2&&lane.nextStore<lane.destinations.size();++n){const auto candidate=lane.destinations[lane.nextStore++];const auto peer=peerOf(candidate);++lane.pendingStores;
   auto token=lanes_[i]->actor->requestStoreToken(peer,candidate.protocol,this,[this,work,i,peer,protocol=candidate.protocol,epoch=lane.queryEpoch,key=lane.queryKey](auto outcome){auto& p=work->lanes[i];if(p.queryEpoch!=epoch){return;}const auto* decoded=outcome?std::get_if<udp::find::Decoded>(&*outcome):nullptr;const auto* reply=decoded?std::get_if<udp::find::ReplyBody>(&decoded->body()):nullptr;if(!work->alive||work->finishing||closing_||!reply){if(p.pendingStores){--p.pendingStores;}return;}const auto nonce=reply->randomId;auto failed=[this,work,i,epoch]{if(work->lanes[i].queryEpoch!=epoch){return;}if(work->lanes[i].pendingStores){--work->lanes[i].pendingStores;}ratingNetworkFailed(work,i,"Rating source changed before STORE");};
    if(!ratingGate(work,i,std::min(work->end,Clock::now()+std::chrono::seconds(10)),[this,work,i,peer,protocol,nonce,epoch,key]{auto& p=work->lanes[i];if(p.queryEpoch!=epoch){return;}if(!work->alive||work->finishing){if(p.pendingStores){--p.pendingStores;}return;}udp::StoreRequest request;request.randomId=nonce;request.keys={std::string(reinterpret_cast<const char*>(key.data()),20)};request.valueSets={{*p.value}};auto sent=lanes_[i]->actor->store(peer,protocol,std::move(request),this,[this,work,i,epoch](auto result){auto& p=work->lanes[i];if(p.queryEpoch!=epoch){return;}if(p.pendingStores){--p.pendingStores;}if(!work->alive||work->finishing||closing_){return;}if(result){const auto* decoded=std::get_if<udp::storage::Decoded>(&*result);const auto* reply=decoded?std::get_if<udp::storage::ReplyBody>(&decoded->body()):nullptr;if(reply){++work->view.storesConfirmed;if(reply->diversification&&!reply->diversification->empty()){ratingDiversify(work,i,static_cast<std::int8_t>(reply->diversification->front()));}}}ratingChanged(work);},std::chrono::seconds(15));if(sent){++work->view.storesAttempted;p.storeTickets.push_back(*sent);}else if(p.pendingStores){--p.pendingStores;}
    },failed)){failed();}
   },std::chrono::seconds(15));if(token){lane.storeTickets.push_back(*token);}else if(lane.pendingStores){--lane.pendingStores;}
  }ratingChanged(work);
 },failed,part.value?nullptr:version)){failed();}
}
void Owner::ratingFinish(std::shared_ptr<RatingWork> work){
 if(!work->alive||work->finishing){return;}work->finishing=true;
 if(!work->view.byHash&&!work->view.publishing&&!work->view.received&&work->source){const auto& local=std::get<storage::DistributedRatingSnapshot>(*work->source).local.local.value;if(local.needsPublishing()){static_cast<void>(work->view.ratings.add(local,true));work->view.localFallback=true;}}
 const auto lease=shared_->lease;const auto source=work->source;const auto hash=work->hash;const auto publishing=work->view.publishing;const auto byHash=work->view.byHash;const auto average=work->view.ratings.average();auto cached=std::make_shared<bool>(false);std::vector<std::shared_ptr<storage::AzureusPublicPolicy>> policies;for(std::size_t i=0;i<2;++i){if(lanes_[i]&&lanes_[i]->ready){policies.push_back(lanes_[i]->policy);}}
 auto failed=[this,work]{ratingCancel(work,"Rating source unavailable at completion");};
 if(!source||policies.empty()||!gate(Clock::now()+std::chrono::seconds(10),[this,work,cached]{if(!work->alive){return;}work->view.running=false;work->view.current=true;work->view.cacheSaved=*cached;work->view.status=work->view.publishing?(work->view.storesConfirmed?"Rating published":"No rating publication confirmed"):"Ratings updated";work->auditAfter=Clock::now()+std::chrono::seconds(10);ratingChanged(work);},failed,[lease,source,hash,publishing,byHash,average,cached,policies](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Rating completion cancelled"));}bool permitted=false;for(const auto& policy:policies){permitted|=bool(policy->validate());}if(!permitted){return std::unexpected(failure(storage::ErrorCode::Conflict,"Public rating network changed"));}auto fresh=readRatingSource(*lease,hash,byHash);if(!fresh){return std::unexpected(fresh.error());}if(ratingSourceRevision(*fresh)!=ratingSourceRevision(*source)){return std::unexpected(failure(storage::ErrorCode::Conflict,"Rating source changed before completion"));}
  if(!publishing&&!byHash){auto saved=storage::saveSharedRatingAverage(*lease,hash,ratingSourceRevision(*source),average);if(!saved){return std::unexpected(saved.error());}*cached=*saved;}return Broker::Data{};
 })){failed();}
}
bool Owner::ratingReadPermit(std::size_t i,std::string key,std::move_only_function<void(bool)> done){
 const auto found=localRatingKeys_[i].find(key);if(found==localRatingKeys_[i].end()){return false;}const auto source=found->second;
 auto work=std::make_shared<RatingWork>(source.hash);work->expectedSource=source.revision;auto callback=std::make_shared<std::move_only_function<void(bool)>>(std::move(done));
 return ratingGate(work,i,Clock::now()+std::chrono::seconds(5),[this,i,callback]{(*callback)(allowed(i));},[this,i,key=std::move(key),source,callback]{
  const auto at=localRatingKeys_[i].find(key);if(at!=localRatingKeys_[i].end()&&at->second.hash==source.hash&&at->second.revision==source.revision){PublicationClock clock;static_cast<void>(lanes_[i]->database->removeLocal(key,0,clock));localRatingKeys_[i].erase(at);}if(auto result=ratings_.find(source.hash.hex());result!=ratings_.end()&&result->second->source&&ratingSourceRevision(*result->second->source)==source.revision){ratingCancel(result->second,"Published rating source changed");}(*callback)(false);
 });
}
void Owner::ratingTick(){
 std::deque<RatingCommand> commands;{std::lock_guard lock(shared_->mutex);commands.swap(shared_->ratingCommands);}for(auto& command:commands){if(command.publish&&!command.cancel&&ratingRegistrations_.contains(command.hash.hex())){static_cast<void>(ratingEnqueue(command.hash.hex(),true));}else{ratingStart(std::move(command));}}
 for(auto at=ratingDeferred_.begin();at!=ratingDeferred_.end();){const auto active=ratings_.find(ratingWorkKey(at->hash,at->byHash));if(active==ratings_.end()||!active->second->view.running){auto command=std::move(*at);at=ratingDeferred_.erase(at);ratingStart(std::move(command));}else{++at;}}
 const auto now=Clock::now();if(now<ratingTickAfter_){return;}ratingTickAfter_=now+std::chrono::milliseconds(100);ratingScheduleTick();unsigned running=0;
 std::vector<std::shared_ptr<RatingWork>> ordered;ordered.reserve(ratings_.size());for(const auto& [key,work]:ratings_){(void)key;ordered.push_back(work);}std::ranges::stable_sort(ordered,[](const auto& a,const auto& b){return a->view.byHash!=b->view.byHash?a->view.byHash:a->view.request<b->view.request;});
 for(const auto& work:ordered){if(!work->alive){continue;}if(!work->view.running){if(now>=work->auditAfter){work->auditAfter=now+std::chrono::seconds(10);for(std::size_t i=0;i<2;++i){if(lanes_[i]&&lanes_[i]->ready){ratingGate(work,i,now+std::chrono::seconds(5),[]{},[this,work]{ratingCancel(work,"Rating source changed");});break;}}}continue;}if(work->finishing||++running>4){continue;}
  if(now>=work->end){for(auto& p:work->lanes){p.finished=true;if(lanes_[p.lane]){for(const auto& [node,ticket]:p.tickets){(void)node;lanes_[p.lane]->actor->cancel(ticket);}}p.tickets.clear();}if(work->source){ratingFinish(work);}else{ratingCancel(work,"Public rating network unavailable");}continue;}
  bool active=false;for(std::size_t i=0;i<2;++i){auto& p=work->lanes[i];if(lanes_[i]&&lanes_[i]->ready){ratingPump(work,i);active|=!p.finished;}}if(!active&&work->source){ratingFinish(work);}
 }
}
