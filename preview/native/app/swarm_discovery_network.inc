// SPDX-License-Identifier: GPL-2.0-or-later
// Original related-content semantics on the existing single shared UDP owner.
void Owner::rcmSnapshot(std::shared_ptr<const SwarmDiscoverySnapshot> value){
 if(value&&(!swarmSnapshot_||value->revision!=swarmSnapshot_->revision)){if(swarmSnapshot_&&swarmSnapshot_->settings!=value->settings){++rcmSearchEpoch_;rcmStopSearch("Search settings changed");rcmForeign_.clear();}swarmSnapshot_=std::move(value);changed();}
}
void Owner::rcmRefresh(){
 if(rcmMetadataPending_||closing_||!shared_->swarmProfile){return;}rcmMetadataPending_=true;
 auto snapshot=std::make_shared<std::shared_ptr<const SwarmDiscoverySnapshot>>();const auto profile=shared_->swarmProfile;
 if(!gate(Clock::now()+std::chrono::seconds(10),[this,snapshot]{rcmMetadataPending_=false;rcmSnapshot(std::move(*snapshot));if(rcmFacts_.status=="Opening saved discoveries…"){rcmFacts_.status="Saved discoveries loaded";changed();}},[this]{rcmMetadataPending_=false;rcmFacts_.status="Saved discoveries are unreadable; existing files retained";changed();},[profile,snapshot](std::stop_token)->Broker::Result{
  auto value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=std::move(*value);return Broker::Data{};
 })){rcmMetadataPending_=false;}
}
bool Owner::rcmAllowed(const RcmLookup& job)const{return !closing_&&job.live->load()&&allowed(job.lane)&&active_->operation->data.related.contains(job.live.get());}
bool Owner::rcmGate(std::shared_ptr<RcmLookup> job,Clock::time_point end,std::move_only_function<void()> effect,std::move_only_function<void()> failed,
    std::shared_ptr<std::optional<std::int32_t>> reserved,std::shared_ptr<std::string> payload,std::vector<std::string> received,std::string expectedPayload){
 if(closing_||!job->live->load()||job->lane>=3||!lanes_[job->lane]||!lanes_[job->lane]->ready||!shared_->swarmProfile){return false;}
 const auto profile=shared_->swarmProfile;const auto policy=lanes_[job->lane]->policy;const auto versions=lanes_[job->lane]->versions;
 const auto parent=job->parent,advertised=job->advertised;const auto lane=job->lane;const bool derived=job->derived,publishing=job->publish;const auto size=job->fileSize;const int level=job->level;const bool explicitLookup=job->explicitLookup;
 auto okay=std::make_shared<bool>(false);auto snapshot=std::make_shared<std::shared_ptr<const SwarmDiscoverySnapshot>>();auto failure=std::make_shared<std::move_only_function<void()>>(std::move(failed));
 return gate(end,[this,job,okay,snapshot,effect=std::move(effect),failure]()mutable{
  rcmSnapshot(std::move(*snapshot));if(!*okay||!job->live->load()){job->live->store(false);if(*failure){(*failure)();}return;}
  active_->operation->data.lanes[job->lane]=true;active_->operation->data.related.insert(job->live.get());effect();
 },[failure]{if(*failure){(*failure)();}},[profile,policy,versions,parent,advertised,lane,derived,publishing,size,level,explicitLookup,okay,snapshot,reserved,payload,received=std::move(received),expectedPayload=std::move(expectedPayload),end](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||Clock::now()>=end){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Discovery operation expired"});}
  auto current=policy->validate();if(!current){return std::unexpected(current.error());}auto value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=*value;const auto& options=(*value)->settings;
  if(!options.enabled||(derived&&!explicitLookup&&level>=options.depth)||(publishing?lane==2:(options.useBigly?lane!=2:lane==2))){return Broker::Data{};}
  auto valid=profile->permitted(parent,derived);if(!valid){return std::unexpected(valid.error());}if(!*valid){return Broker::Data{};}
  if(publishing){auto source=profile->source(advertised);if(!source){return std::unexpected(source.error());}if(!*source){return Broker::Data{};}if(size&&std::ranges::find((**source).fileSizes,size)==(**source).fileSizes.end()){return Broker::Data{};}auto encoded=rcm::encodeAssociation((**source).item);if(!encoded){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,encoded.error().message});}if(!expectedPayload.empty()&&*encoded!=expectedPayload){return Broker::Data{};}if(payload){*payload=std::move(*encoded);}}
  else if(size){auto source=profile->source(parent);if(!source){return std::unexpected(source.error());}if(!*source||std::ranges::find((**source).fileSizes,size)==(**source).fileSizes.end()){return Broker::Data{};}}
  if(!received.empty()){const auto from=size?rcm::sizeAssociationKey(size):std::array<std::uint8_t,20>{};const std::string related=size?std::string(reinterpret_cast<const char*>(from.data()),20):parent;auto ingested=profile->ingest(received,related,level+1,explicitLookup);if(!ingested){return std::unexpected(ingested.error());}value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=*value;}
  if(reserved){auto next=versions->next(QDateTime::currentMSecsSinceEpoch());if(!next){return std::unexpected(next.error());}*reserved=*next;}*okay=true;return Broker::Data{};
 });
}
void Owner::rcmQueue(std::string parent,std::string advertised,std::int64_t size,int level,bool explicitLookup,bool derived){
 if(parent.size()!=20||rcmPending_.size()>=128||!swarmSnapshot_||!swarmSnapshot_->settings.enabled){return;}
 auto group=std::make_shared<RcmGroup>();const bool publish=!advertised.empty();const auto key=size?rcm::sizeAssociationKey(size):rcm::associationKey(parent);
 for(std::size_t lane=0;lane<3;++lane){if(rcmPending_.size()>=128){break;}if(!lanes_[lane]||!lanes_[lane]->ready||(publish?lane==2:(swarmSnapshot_->settings.useBigly?lane!=2:lane==2))){continue;}
  const bool duplicate=std::ranges::any_of(rcmLookups_,[&](const auto& job){return !job->finished&&job->lane==lane&&job->parent==parent&&job->advertised==advertised&&job->fileSize==size;});if(duplicate){continue;}
  group->seen.emplace(lane,key);auto job=std::make_shared<RcmLookup>();job->parent=parent;job->advertised=advertised;job->publish=publish;job->fileSize=size;job->level=level;job->explicitLookup=explicitLookup;job->derived=derived;job->key=key;job->lane=lane;job->group=group;job->end=Clock::now()+std::chrono::seconds(60);rcmPending_.push_back(std::move(job));++group->roots;
 }
}
bool Owner::rcmBegin(std::shared_ptr<RcmLookup> job){
 if(closing_||job->finished||!job->live->load()||Clock::now()>=job->end||!lanes_[job->lane]||!lanes_[job->lane]->ready||rcmLookups_.size()>=4){return false;}
 auto& lane=*lanes_[job->lane];const auto route=lane.actor->routingSnapshot();std::vector<control::AdmittedContact> seeds;if(route){for(std::size_t n=0;n<route->contactCount;++n){const auto& candidate=route->contacts[n].contact;if(candidate.id!=lane.local.id){seeds.push_back({candidate,true});}}}
 std::ranges::sort(seeds,[&](const auto& a,const auto& b){for(unsigned n=0;n<20;++n){const auto x=a.contact.id[n]^job->key[n],y=b.contact.id[n]^job->key[n];if(x!=y){return x<y;}}return false;});if(seeds.size()>20){seeds.resize(20);}if(seeds.empty()){return false;}
 control::LookupOptions options;options.target=job->key;options.localId=lane.local.id;options.maximumValues=30;options.flags=0;options.kind=control::LookupKind::Value;options.timeoutMillis=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(job->end-Clock::now()).count());options.addressScope=owned_?control::LookupAddressScope::OwnedLoopback:control::LookupAddressScope::PublicValue;if(!owned_){options.maximumContacts=control::LookupController::MaximumContacts;}
 if(!job->control.start(shared_->generation,++lookupId_,options,seeds,monotonic())){return false;}rcmLookups_.push_back(job);rcmPump(std::move(job));return true;
}
void Owner::rcmCancel(std::shared_ptr<RcmLookup> job){
 job->live->store(false);job->finished=true;if(job->publish){job->group->publication=0;}if(lanes_[job->lane]&&lanes_[job->lane]->actor){for(const auto& [node,ticket]:job->tickets){(void)node;lanes_[job->lane]->actor->cancel(ticket);}}job->tickets.clear();rcmFinish(job);
}
void Owner::rcmPump(std::shared_ptr<RcmLookup> job){
 if(job->finished||job->queued||closing_){return;}job->queued=true;
 if(!rcmGate(job,std::min(job->end,Clock::now()+std::chrono::seconds(10)),[this,job]{job->queued=false;if(!rcmAllowed(*job)){rcmCancel(job);return;}auto& actor=*lanes_[job->lane]->actor;auto intents=job->control.takeIntents(monotonic());if(!intents){rcmCancel(job);return;}
  for(const auto& intent:*intents){const auto peer=peerOf(intent.contact);if(!actor.attachKnownPeer(peer,intent.contact.protocol,job->end)){applied(job,job->control.finish(intent.node,control::LookupNodeState::Rejected,monotonic()));continue;}
   auto sent=actor.findValue(peer,intent.contact.protocol,intent.target,0,intent.maximumValues,this,[this,job,node=intent.node](auto outcome){rcmReply(job,node,std::move(outcome));},std::min(std::chrono::milliseconds(15000),std::chrono::duration_cast<std::chrono::milliseconds>(job->end-Clock::now())));
   if(sent){if(!job->observed){job->observed=true;++rcmFacts_.lookups;}job->tickets[intent.node]=*sent;applied(job,job->control.sent(intent.node,sent->value,monotonic()));}else{applied(job,job->control.finish(intent.node,control::LookupNodeState::Failed,monotonic()));}
  }
  if(const auto cancels=job->control.cancellationIntents()){for(const auto& item:*cancels){const auto at=job->tickets.find(item.node);if(at!=job->tickets.end()){actor.cancel(at->second);}}}
  if(control::lookupTerminal(job->control.state())){rcmFinish(job);}changed();
 },[this,job]{job->queued=false;rcmCancel(job);rcmFacts_.status="Discovery source is unavailable or no longer permits Public lookup";changed();})){job->queued=false;rcmCancel(job);}
}
void Owner::rcmReply(std::shared_ptr<RcmLookup> job,std::uint64_t node,udp::FindValueOutcome outcome){
 if(job->finished||closing_){return;}auto reply=std::make_shared<udp::FindValueOutcome>(std::move(outcome));std::vector<std::string> received;if(*reply){if(const auto* body=std::get_if<udp::lookup::ReplyBody>(&(*reply)->body())){if(const auto* found=std::get_if<udp::lookup::FoundValues>(&body->result)){for(const auto& value:found->values){if(received.size()<30){received.push_back(value.bytes);}}}}}
 if(!rcmGate(job,std::min(job->end,Clock::now()+std::chrono::seconds(10)),[this,job,node,reply]{if(!rcmAllowed(*job)){rcmCancel(job);return;}const auto* body=*reply?std::get_if<udp::lookup::ReplyBody>(&(*reply)->body()):nullptr;
  if(!body){applied(job,job->control.finish(node,control::LookupNodeState::Failed,monotonic()));job->tickets.erase(node);}
  else if(const auto* found=std::get_if<udp::lookup::FoundValues>(&body->result)){
   for(const auto& value:found->values){if(job->values.size()<30){job->values.insert(value.bytes);}}
   if(found->diversification!=1){job->diversified=true;job->group->diversified=true;if(!job->publish&&job->depth<4&&(found->diversification==2||found->diversification==3)){
     const auto a=QRandomGenerator::system()->bounded(10U),b=(a+1+QRandomGenerator::system()->bounded(9U))%10;std::array<unsigned,2> offsets{a,b};for(unsigned n=0;n<(found->diversification==2?1U:2U);++n){const auto key=msg::diversifiedDiscoveryKey(job->key,offsets[n]);if(!key||job->group->seen.size()>=32||rcmPending_.size()>=128||!job->group->seen.emplace(job->lane,*key).second){continue;}auto child=std::make_shared<RcmLookup>();child->parent=job->parent;child->fileSize=job->fileSize;child->key=*key;child->lane=job->lane;child->group=job->group;child->derived=job->derived;child->explicitLookup=job->explicitLookup;child->level=job->level;child->depth=job->depth+1;child->end=job->end;rcmPending_.push_back(std::move(child));}
   }}applied(job,job->control.values(node,found->values,body->continuation,found->diversification,monotonic()));if(!body->continuation){job->tickets.erase(node);}
  }else if(const auto* found=std::get_if<udp::lookup::FoundContacts>(&body->result)){std::vector<control::AdmittedContact> contacts;for(const auto& c:found->contacts){if(lanes_[job->lane]->actor->attachKnownPeer(peerOf(c),c.protocol,job->end)){contacts.push_back({c,true});}}applied(job,job->control.contacts(node,contacts,body->continuation,monotonic()));if(!body->continuation){job->tickets.erase(node);}}
  later([this,job]{rcmPump(job);});changed();
 },[this,job]{rcmCancel(job);},{},{},std::move(received))){rcmCancel(job);}
}
void Owner::rcmFinish(std::shared_ptr<RcmLookup> job){
 if(job->reported){return;}job->finished=true;job->reported=true;
 if(job->publish){job->group->hits+=job->values.size();job->group->diversified|=job->diversified;if(job->group->roots){--job->group->roots;}rcmAwaitPublish_.push_back(job);}
 ++rcmFacts_.completed;changed();
}
void Owner::rcmPublish(std::shared_ptr<RcmLookup> job){
 if(closing_||!job->live->load()||rcmPublications_.size()+rcmPublicationPreflights_>=2){return;}++rcmPublicationPreflights_;
 auto payload=std::make_shared<std::string>();auto reserved=std::make_shared<std::optional<std::int32_t>>();
 if(!rcmGate(job,Clock::now()+std::chrono::seconds(10),[this,job,payload,reserved]{--rcmPublicationPreflights_;if(!rcmAllowed(*job)||!reserved->has_value()){return;}auto& lane=*lanes_[job->lane];PublicationClock clock;const std::string key(reinterpret_cast<const char*>(job->key.data()),20);auto local=lane.database->putLocal(key,*payload,**reserved,clock,db::LocalPublicationFlags::Normal);if(!local){return;}
  localRcmKeys_[job->lane].insert_or_assign(key,RcmLocal{job->parent,job->advertised,*payload,job->fileSize});
  auto publication=std::make_shared<RcmPublication>();publication->lookup=job;publication->payload=*payload;publication->end=Clock::now()+std::chrono::seconds(60);publication->value={local->version(),local->creationTime(),*payload,lane.local,local->flags(),local->lifeTimeHours(),local->replicationControl()};
  if(const auto snapshot=job->control.snapshot()){for(const auto& node:snapshot->nodes){if(node.state==control::LookupNodeState::Replied){publication->destinations.push_back(node.contact);}}}if(publication->destinations.size()>20){publication->destinations.resize(20);}if(publication->destinations.empty()){return;}rcmPublications_.push_back(publication);rcmPumpPut(std::move(publication));changed();
 },[this]{--rcmPublicationPreflights_;},reserved,payload)){--rcmPublicationPreflights_;}
}
void Owner::rcmPumpPut(std::shared_ptr<RcmPublication> publication){
 if(closing_||publication->finished||publication->queued||publication->pending){return;}if(Clock::now()>=publication->end||publication->next>=publication->destinations.size()){publication->finished=true;return;}publication->queued=true;const auto job=publication->lookup;
 if(!rcmGate(job,std::min(publication->end,Clock::now()+std::chrono::seconds(10)),[this,publication,job]{publication->queued=false;if(!rcmAllowed(*job)){publication->finished=true;return;}
  for(unsigned n=0;n<2&&publication->next<publication->destinations.size();++n){const auto target=publication->destinations[publication->next++];const auto peer=peerOf(target);++publication->pending;
   auto token=lanes_[job->lane]->actor->requestStoreToken(peer,target.protocol,this,[this,job,publication,peer,version=target.protocol](auto result){const auto* decoded=result?std::get_if<udp::find::Decoded>(&*result):nullptr;const auto* reply=decoded?std::get_if<udp::find::ReplyBody>(&decoded->body()):nullptr;if(closing_||publication->finished||!reply){if(publication->pending){--publication->pending;}return;}const auto cookie=reply->randomId;
    if(!rcmGate(job,std::min(publication->end,Clock::now()+std::chrono::seconds(10)),[this,publication,job,peer,version,cookie]{if(!rcmAllowed(*job)){if(publication->pending){--publication->pending;}return;}udp::StoreRequest request;request.randomId=cookie;request.keys={std::string(reinterpret_cast<const char*>(job->key.data()),20)};request.valueSets={{publication->value}};
     auto sent=lanes_[job->lane]->actor->store(peer,version,std::move(request),this,[this,publication](auto outcome){if(publication->pending){--publication->pending;}if(outcome&&!closing_&&!publication->finished&&std::holds_alternative<udp::storage::Decoded>(*outcome)){++rcmFacts_.publications;changed();}},std::chrono::seconds(15));if(!sent&&publication->pending){--publication->pending;}
    },[publication]{if(publication->pending){--publication->pending;}publication->finished=true;},{},{},{},publication->payload)){if(publication->pending){--publication->pending;}}
   },std::chrono::seconds(15));if(!token&&publication->pending){--publication->pending;}
  }
 },[publication]{publication->queued=false;publication->finished=true;},{},{},{},publication->payload)){publication->queued=false;publication->finished=true;}
}
bool Owner::rcmReadPermit(std::size_t lane,std::string key,std::move_only_function<void(bool)> done){
 const auto found=localRcmKeys_[lane].find(key);if(found==localRcmKeys_[lane].end()){return false;}
 const auto local=found->second;const auto profile=shared_->swarmProfile;const auto policy=lanes_[lane]->policy;auto accepted=std::make_shared<bool>(false);auto callback=std::make_shared<std::move_only_function<void(bool)>>(std::move(done));
 return gate(Clock::now()+std::chrono::seconds(10),[this,lane,key=std::move(key),local,accepted,callback]{active_->operation->data.lanes[lane]=true;const auto current=localRcmKeys_[lane].find(key);if(current==localRcmKeys_[lane].end()||current->second!=local){(*callback)(false);return;}if(!*accepted){PublicationClock clock;static_cast<void>(lanes_[lane]->database->removeLocal(key,0,clock));localRcmKeys_[lane].erase(key);}(*callback)(allowed(lane));},[callback]{(*callback)(false);},[local,profile,policy,accepted](std::stop_token)->Broker::Result{
  auto current=policy->validate();if(!current){return std::unexpected(current.error());}auto parent=profile->permitted(local.parent);if(!parent){return std::unexpected(parent.error());}if(!*parent){return Broker::Data{};}auto source=profile->source(local.advertised);if(!source){return std::unexpected(source.error());}if(!*source){return Broker::Data{};}if(local.fileSize&&std::ranges::find((**source).fileSizes,local.fileSize)==(**source).fileSizes.end()){return Broker::Data{};}auto encoded=rcm::encodeAssociation((**source).item);*accepted=encoded&&*encoded==local.payload;return Broker::Data{};
 });
}
void Owner::rcmCommand(SwarmCommand command){
 if(closing_||!shared_->swarmProfile){return;}
 if(command.kind==SwarmCommand::Kind::Search){if(command.search){rcmStartSearch(std::move(*command.search));}return;}
 if(command.kind==SwarmCommand::Kind::CancelSearch){rcmStopSearch("Search cancelled");return;}
 if(command.kind==SwarmCommand::Kind::Lookup){if(command.hash.size()!=20){return;}rcmFacts_.selected=command.hash;rcmFacts_.status="Looking for related torrents…";rcmQueue(std::move(command.hash),{},command.fileSize,0,true,true);changed();return;}
 if(command.kind==SwarmCommand::Kind::Cycle){rcmAuto();return;}
 if(command.kind==SwarmCommand::Kind::Refresh){rcmRefreshAfter_={};return;}
 auto snapshot=std::make_shared<std::shared_ptr<const SwarmDiscoverySnapshot>>();const auto profile=shared_->swarmProfile;
 if(!gate(Clock::now()+std::chrono::seconds(10),[this,snapshot]{rcmSnapshot(std::move(*snapshot));rcmFacts_.status="Saved discoveries updated";changed();},[this]{rcmFacts_.status="Discovery change could not be saved; existing files retained";changed();},[profile,changes=std::move(command.changes),snapshot](std::stop_token)->Broker::Result{
  auto changed=profile->change(changes);if(!changed){return std::unexpected(changed.error());}auto value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=std::move(*value);return Broker::Data{};
 })){rcmFacts_.status="Discovery command queue is busy";changed();}
}
void Owner::rcmAuto(){
 if(rcmCandidatesPending_||closing_||!shared_->swarmProfile||!swarmSnapshot_||!swarmSnapshot_->settings.enabled){return;}rcmCandidatesPending_=true;const auto profile=shared_->swarmProfile;auto sources=std::make_shared<std::vector<SwarmDiscoverySource>>();
 if(!gate(Clock::now()+std::chrono::seconds(15),[this,sources]{rcmCandidatesPending_=false;if(sources->size()<2){return;}const auto a=QRandomGenerator::system()->bounded(unsigned(sources->size())),b=(a+1+QRandomGenerator::system()->bounded(unsigned(sources->size()-1)))%unsigned(sources->size());const auto& first=(*sources)[a],&second=(*sources)[b];rcmQueue(first.item.hash,second.item.hash);rcmQueue(second.item.hash,first.item.hash);if(!second.fileSizes.empty()){const auto size=second.fileSizes[QRandomGenerator::system()->bounded(unsigned(second.fileSizes.size()))];rcmQueue(second.item.hash,second.item.hash,size);}},[this]{rcmCandidatesPending_=false;rcmFacts_.status="Library discovery sources are unavailable";changed();},[profile,sources](std::stop_token)->Broker::Result{
  auto values=profile->candidates(8);if(!values){return std::unexpected(values.error());}*sources=std::move(*values);return Broker::Data{};
 })){rcmCandidatesPending_=false;}
}
void Owner::rcmSave(bool closing){
 if(!shared_->swarmProfile||rcmSavePending_||rcmCloseSave_){return;}const auto profile=shared_->swarmProfile;
 if(closing){auto submitted=shared_->networkIo->submit([profile](std::stop_token)->Broker::Result{auto value=profile->flush();if(!value){return std::unexpected(value.error());}return Broker::Data{};});if(submitted){rcmCloseSave_=std::move(*submitted);}return;}
 rcmSavePending_=true;auto snapshot=std::make_shared<std::shared_ptr<const SwarmDiscoverySnapshot>>();
 if(!gate(Clock::now()+std::chrono::seconds(10),[this,snapshot]{rcmSavePending_=false;rcmSnapshot(std::move(*snapshot));},[this]{rcmSavePending_=false;rcmFacts_.status="Discovery cache save is unavailable; data retained";changed();},[profile,snapshot](std::stop_token)->Broker::Result{auto saved=profile->flush();if(!saved){return std::unexpected(saved.error());}auto value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=std::move(*value);return Broker::Data{};})){rcmSavePending_=false;}
}
void Owner::rcmDrainSave(){
 if(!rcmCloseSave_){return;}if(auto ready=Broker::take(rcmCloseSave_)){if(!*ready){rcmFacts_.status="Discovery close save is unavailable";changed();}Broker::acknowledge(rcmCloseSave_);}if(Broker::released(rcmCloseSave_)){shared_->networkIo->release(rcmCloseSave_);rcmCloseSave_.reset();}
}
void Owner::rcmTick(){
 if(closing_){return;}const auto now=Clock::now();if(now<rcmTickAfter_){return;}rcmTickAfter_=now+std::chrono::seconds(1);
 std::deque<SwarmCommand> commands;{std::lock_guard lock(shared_->mutex);commands.swap(shared_->swarmCommands);}for(auto& command:commands){rcmCommand(std::move(command));}rcmSearchTick();
 if(now>=rcmRefreshAfter_){rcmRefreshAfter_=now+std::chrono::seconds(30);rcmRefresh();}
 for(const auto& job:rcmLookups_){if(!job->finished){rcmPump(job);}}
 std::erase_if(rcmLookups_,[](const auto& job){return job->finished;});
 const auto attempts=rcmPending_.size();for(std::size_t n=0;n<attempts&&!rcmPending_.empty()&&rcmLookups_.size()<4;++n){auto job=rcmPending_.front();rcmPending_.pop_front();if(Clock::now()>=job->end){rcmCancel(job);}else if(!rcmBegin(job)){rcmPending_.push_back(std::move(job));}}
 for(auto at=rcmAwaitPublish_.begin();at!=rcmAwaitPublish_.end();){auto job=*at;if(job->group->roots){++at;continue;}if(job->group->publication<0){const auto hits=job->group->hits;job->group->publication=(!job->group->diversified&&hits<10&&(hits<=5||QRandomGenerator::system()->bounded(unsigned(hits-5+1))==0))?1:0;}if(job->group->publication==1&&rcmReadyPublish_.size()<64){rcmReadyPublish_.push_back(std::move(job));}at=rcmAwaitPublish_.erase(at);}
 for(const auto& publication:rcmPublications_){rcmPumpPut(publication);}std::erase_if(rcmPublications_,[](const auto& publication){return publication->finished;});
 while(!rcmReadyPublish_.empty()&&rcmPublications_.size()+rcmPublicationPreflights_<2){auto job=rcmReadyPublish_.front();rcmReadyPublish_.pop_front();rcmPublish(std::move(job));}
 if(swarmSnapshot_&&swarmSnapshot_->settings.enabled){if(now>=rcmAutoAfter_){rcmAutoAfter_=now+std::chrono::seconds(30);rcmAuto();}if(now>=rcmSecondaryAfter_){rcmSecondaryAfter_=now+std::chrono::minutes(15);std::size_t count=0;for(const auto& item:swarmSnapshot_->items){if(count==10){break;}if(item.hash.size()==20&&(item.networks&1)&&item.level<swarmSnapshot_->settings.depth){rcmQueue(item.hash,{},0,item.level,false,true);++count;}}}}
 if(now>=rcmSaveAfter_){rcmSaveAfter_=now+std::chrono::minutes(1);rcmSave();}
 const auto pending=rcmPending_.size()+rcmReadyPublish_.size()+rcmAwaitPublish_.size()+rcmPublicationPreflights_;if(pending==0&&rcmLookups_.empty()&&rcmPublications_.empty()&&rcmFacts_.status=="Looking for related torrents…"){rcmFacts_.status="Discovery lookup finished";changed();}if(rcmFacts_.active!=rcmLookups_.size()+rcmPublications_.size()||rcmFacts_.pending!=pending){rcmFacts_.active=rcmLookups_.size()+rcmPublications_.size();rcmFacts_.pending=pending;changed();}
}
