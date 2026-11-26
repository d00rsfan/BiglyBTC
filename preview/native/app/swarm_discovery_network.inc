// SPDX-License-Identifier: GPL-2.0-or-later
// Original related-content semantics on the existing single shared UDP owner.
void Owner::rcmSnapshot(std::shared_ptr<const SwarmDiscoverySnapshot> value){
 if(value&&(!swarmSnapshot_||value->revision!=swarmSnapshot_->revision)){if(swarmSnapshot_&&swarmSnapshot_->settings!=value->settings){++rcmSearchEpoch_;rcmStopSearch("Search settings changed");rcmForeign_.clear();}swarmSnapshot_=std::move(value);changed();}
}
void Owner::rcmRefresh(){
 if(rcmMetadataPending_||closing_||!shared_->swarmProfile){return;}rcmMetadataPending_=true;
 auto snapshot=std::make_shared<std::shared_ptr<const SwarmDiscoverySnapshot>>();const auto profile=shared_->swarmProfile;
 if(!gate(Clock::now()+std::chrono::seconds(90),[this,snapshot]{rcmMetadataPending_=false;rcmSnapshot(std::move(*snapshot));if(rcmFacts_.status=="Opening saved discoveries…"){rcmFacts_.status="Saved discoveries loaded";changed();}},[this]{rcmMetadataPending_=false;rcmFacts_.status="Saved discoveries are unreadable; existing files retained";changed();},[profile,snapshot](std::stop_token)->Broker::Result{
  auto value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=std::move(*value);return Broker::Data{};
 })){rcmMetadataPending_=false;}
}
bool Owner::rcmAllowed(const RcmLookup& job)const{
 const auto& search=job.group->mergeSearch;
 return !closing_&&job.live->load()&&(!search||search->live->load())
     &&allowed(job.lane)&&active_->operation->data.related.contains(job.live.get());
}
bool Owner::rcmGate(std::shared_ptr<RcmLookup> job,Clock::time_point end,biglybtc::core::MoveOnlyFunction<void()> effect,biglybtc::core::MoveOnlyFunction<void()> failed,
    std::shared_ptr<std::optional<std::int32_t>> reserved,std::shared_ptr<std::string> payload,std::vector<std::string> received,std::string expectedPayload,
    std::shared_ptr<std::optional<SwarmTagProbe>> probe){
 if(closing_||!job->live->load()||job->lane>=3||!lanes_[job->lane]||!lanes_[job->lane]->ready||!shared_->swarmProfile){return false;}
 const auto profile=shared_->swarmProfile;const auto policy=lanes_[job->lane]->policy;const auto versions=lanes_[job->lane]->versions;
 const auto parent=job->parent,advertised=job->advertised;const auto lane=job->lane;const bool derived=job->derived,publishing=job->publish;const auto size=job->fileSize;const int level=job->level;const bool explicitLookup=job->explicitLookup;
 const auto mergeSource=job->group->mergeSearch?job->group->mergeSearch->live:nullptr;
 const bool attributes=job->group->attributes.has_value();
 const auto tagLookup=!attributes ? SwarmTagLookup::Publication :
     job->group->attributes->libraryScan ? SwarmTagLookup::LibraryScan : SwarmTagLookup::Direct;
 auto okay=std::make_shared<bool>(false);auto snapshot=std::make_shared<std::shared_ptr<const SwarmDiscoverySnapshot>>();auto failure=std::make_shared<biglybtc::core::MoveOnlyFunction<void()>>(std::move(failed));
 return gate(end,[this,job,okay,snapshot,effect=std::move(effect),failure]()mutable{
  rcmSnapshot(std::move(*snapshot));if(!*okay||!job->live->load()){job->live->store(false);if(*failure){(*failure)();}return;}
  active_->operation->data.lanes[job->lane]=true;active_->operation->data.related.insert(job->live.get());effect();
 },[failure]{if(*failure){(*failure)();}},[profile,policy,versions,parent,advertised,lane,derived,publishing,size,level,explicitLookup,attributes,tagLookup,okay,snapshot,reserved,payload,received=std::move(received),expectedPayload=std::move(expectedPayload),probe,end,mergeSource](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||Clock::now()>=end||(mergeSource&&!mergeSource->load())){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Discovery operation expired"});}
  auto current=policy->validate();if(!current){return std::unexpected(current.error());}auto value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=*value;const auto& options=(*value)->settings;
  if(!options.enabled||(derived&&!explicitLookup&&level>=options.depth)||(publishing?lane==2:(options.useBigly?lane!=2:lane==2))){return Broker::Data{};}
  auto valid=profile->permitted(parent,derived);if(!valid){return std::unexpected(valid.error());}if(!*valid){return Broker::Data{};}
  if (publishing) {
   auto encoded=profile->publication(parent,advertised,size);
   if (!encoded) { return std::unexpected(encoded.error()); }
   if (!*encoded || (!expectedPayload.empty() && **encoded!=expectedPayload)) { return Broker::Data{}; }
   if (payload) { *payload=std::move(**encoded); }
  }
  else if(size){auto source=profile->source(parent);if(!source){return std::unexpected(source.error());}if(!*source||std::ranges::find((**source).fileSizes,size)==(**source).fileSizes.end()){return Broker::Data{};}}
  if(!attributes&&!received.empty()){const auto from=size?rcm::sizeAssociationKey(size):std::array<std::uint8_t,20>{};const std::string related=size?std::string(reinterpret_cast<const char*>(from.data()),20):parent;auto ingested=profile->ingest(received,related,level+1,explicitLookup);if(!ingested){return std::unexpected(ingested.error());}value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=*value;}
  if (probe) {
   auto captured=profile->beginSwarmTagProbe(parent,tagLookup);
   if (attributes && (!captured || !*captured)) { return Broker::Data{}; }
   if (captured && *captured) { probe->emplace(std::move(**captured)); }
  }
  if(reserved){auto next=versions->next(QDateTime::currentMSecsSinceEpoch());if(!next){return std::unexpected(next.error());}*reserved=*next;}*okay=true;return Broker::Data{};
 });
}
bool Owner::rcmQueue(std::string parent,std::string advertised,std::int64_t size,int level,bool explicitLookup,
    bool derived,std::uint64_t attributeRequest,bool libraryScan,std::optional<engine::SwarmMergeSearchTarget> mergeSearch) {
 if(parent.size()!=20||rcmPending_.size()>=128||!swarmSnapshot_||!swarmSnapshot_->settings.enabled){return false;}
 auto group=std::make_shared<RcmGroup>();
 group->mergeSearch=std::move(mergeSearch);
 if (attributeRequest) {
  auto& tags=group->attributes.emplace(RcmDirectTags{});
  tags.request=attributeRequest;
  tags.libraryScan=libraryScan;
 }
 const bool publish=!advertised.empty();const auto key=size?rcm::sizeAssociationKey(size):rcm::associationKey(parent);
 for(std::size_t lane=0;lane<3;++lane){if(rcmPending_.size()>=128){break;}if(!lanes_[lane]||!lanes_[lane]->ready||(publish?lane==2:(swarmSnapshot_->settings.useBigly?lane!=2:lane==2))){continue;}
  const bool duplicate=std::ranges::any_of(rcmLookups_,[&](const auto& job){return !job->finished&&job->lane==lane&&job->parent==parent&&job->advertised==advertised&&job->fileSize==size&&
      job->group->attributes.has_value()==bool(attributeRequest);});if(duplicate){continue;}
  group->seen.emplace(lane,key);auto job=std::make_shared<RcmLookup>();job->parent=parent;job->advertised=advertised;job->publish=publish;job->fileSize=size;job->level=level;job->explicitLookup=explicitLookup;job->derived=derived;job->key=key;job->lane=lane;job->group=group;job->end=Clock::now()+std::chrono::seconds(attributeRequest?30:60);rcmPending_.push_back(std::move(job));++group->roots;
  if (attributeRequest) { break; }
 }
 return group->roots!=0;
}
bool Owner::rcmBegin(std::shared_ptr<RcmLookup> job){
 if(closing_||job->finished||!job->live->load()||Clock::now()>=job->end||!lanes_[job->lane]||!lanes_[job->lane]->ready||rcmLookups_.size()>=4){return false;}
 auto& lane=*lanes_[job->lane];if(auto known=lane.diversifications->find(job->key,QDateTime::currentMSecsSinceEpoch())){return rcmGate(job,job->end,[this,job,type=known->type]{if(rcmAllowed(*job)){rcmDiversify(job,type);rcmFinish(job);}else{rcmCancel(job);}},[this,job]{rcmCancel(job);});}const auto route=lane.actor->routingSnapshot();std::vector<control::AdmittedContact> seeds;if(route){for(std::size_t n=0;n<route->contactCount;++n){const auto& candidate=route->contacts[n].contact;if(candidate.id!=lane.local.id){seeds.push_back({candidate,true});}}}
 std::ranges::sort(seeds,[&](const auto& a,const auto& b){for(unsigned n=0;n<20;++n){const auto x=a.contact.id[n]^job->key[n],y=b.contact.id[n]^job->key[n];if(x!=y){return x<y;}}return false;});if(seeds.size()>20){seeds.resize(20);}if(seeds.empty()){return false;}
 control::LookupOptions options;options.target=job->key;options.localId=lane.local.id;options.maximumValues=30;options.flags=0;options.kind=control::LookupKind::Value;options.timeoutMillis=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(job->end-Clock::now()).count());options.addressScope=owned_?control::LookupAddressScope::OwnedLoopback:control::LookupAddressScope::PublicValue;if(!owned_){options.maximumContacts=control::LookupController::MaximumContacts;}
 if(!job->control.start(shared_->generation,++lookupId_,options,seeds,monotonic())){return false;}rcmLookups_.push_back(job);rcmPump(std::move(job));return true;
}
void Owner::rcmCancel(std::shared_ptr<RcmLookup> job){
 job->live->store(false);job->finished=true;if(job->publish){job->group->publication=0;}if(lanes_[job->lane]&&lanes_[job->lane]->actor){for(const auto& [node,ticket]:job->tickets){(void)node;lanes_[job->lane]->actor->cancel(ticket);}}job->tickets.clear();rcmFinish(job);
}
void Owner::rcmPump(std::shared_ptr<RcmLookup> job){
 if(job->finished||job->queued||closing_){return;}job->queued=true;
 std::shared_ptr<std::optional<SwarmTagProbe>> probe;
 auto* direct=job->group->attributes ? &*job->group->attributes : nullptr;
 if (direct && !direct->requested) {
  direct->requested=true;
  probe=std::make_shared<std::optional<SwarmTagProbe>>();
 } else if (job->publish && !job->fileSize && !job->depth && !job->tagProbeAttempted) {
  job->tagProbeAttempted=true;
  probe=std::make_shared<std::optional<SwarmTagProbe>>();
 }
 if(!rcmGate(job,std::min(job->end,Clock::now()+std::chrono::seconds(10)),[this,job,probe]{
  job->queued=false;if(!rcmAllowed(*job)){rcmCancel(job);return;}
  if (probe && *probe) {
   if (job->group->attributes) { job->group->attributes->capture.emplace(std::move(**probe)); }
   else { job->tagProbe.emplace(std::move(**probe)); }
  }
  auto& actor=*lanes_[job->lane]->actor;auto intents=job->control.takeIntents(monotonic());if(!intents){rcmCancel(job);return;}
  for(const auto& intent:*intents){const auto peer=peerOf(intent.contact);if(!actor.attachKnownPeer(peer,intent.contact.protocol,job->end)){applied(job,job->control.finish(intent.node,control::LookupNodeState::Rejected,monotonic()));continue;}
   auto sent=actor.findValue(peer,intent.contact.protocol,intent.target,0,intent.maximumValues,this,[this,job,node=intent.node](auto outcome){rcmReply(job,node,std::move(outcome));},std::min(std::chrono::milliseconds(15000),std::chrono::duration_cast<std::chrono::milliseconds>(job->end-Clock::now())));
   if(sent){if(job->group->attributes){job->group->attributes->observed=true;}if(!job->observed){job->observed=true;++rcmFacts_.lookups;}job->tickets[intent.node]=*sent;applied(job,job->control.sent(intent.node,sent->value,monotonic()));}else{applied(job,job->control.finish(intent.node,control::LookupNodeState::Failed,monotonic()));}
  }
  if(const auto cancels=job->control.cancellationIntents()){for(const auto& item:*cancels){const auto at=job->tickets.find(item.node);if(at!=job->tickets.end()){actor.cancel(at->second);}}}
  if(control::lookupTerminal(job->control.state())){rcmFinish(job);}changed();
 },[this,job]{job->queued=false;rcmCancel(job);rcmFacts_.status="Discovery source is unavailable or no longer permits Public lookup";changed();},{},{},{},{},probe)){job->queued=false;rcmCancel(job);}
}
void Owner::rcmReply(std::shared_ptr<RcmLookup> job,std::uint64_t node,udp::FindValueOutcome outcome){
 if(job->finished||closing_){return;}auto reply=std::make_shared<udp::FindValueOutcome>(std::move(outcome));std::vector<std::string> received;if(*reply){if(const auto* body=std::get_if<udp::lookup::ReplyBody>(&(*reply)->body())){if(const auto* found=std::get_if<udp::lookup::FoundValues>(&body->result)){for(const auto& value:found->values){if(received.size()<30){received.push_back(value.bytes);}}}}}
 if(!rcmGate(job,std::min(job->end,Clock::now()+std::chrono::seconds(10)),[this,job,node,reply]{if(!rcmAllowed(*job)){rcmCancel(job);return;}const auto* body=*reply?std::get_if<udp::lookup::ReplyBody>(&(*reply)->body()):nullptr;
  if(!body){applied(job,job->control.finish(node,control::LookupNodeState::Failed,monotonic()));job->tickets.erase(node);}
  else if(const auto* found=std::get_if<udp::lookup::FoundValues>(&body->result)){
   for (const auto& value : found->values) {
    if (job->values.size()>=30) { break; }
    if (job->values.insert(value.bytes).second) {
     if (auto& direct=job->group->attributes; direct && direct->replies.size()<30) {
      if (direct->seen.insert(value.bytes).second) { direct->replies.push_back(value.bytes); }
     } else if (job->tagProbe) {
      job->tagReplies.push_back(value.bytes);
     } else if (job->group->mergeSearch && job->group->mergeReplies.size()<30) {
      job->group->mergeReplies.insert(value.bytes);
     }
    }
   }
   if(found->diversification!=1&&!job->diversified){rcmDiversify(job,found->diversification);}applied(job,job->control.values(node,found->values,body->continuation,found->diversification,monotonic()));if(!body->continuation){job->tickets.erase(node);}
  }else if(const auto* found=std::get_if<udp::lookup::FoundContacts>(&body->result)){std::vector<control::AdmittedContact> contacts;for(const auto& c:found->contacts){if(lanes_[job->lane]->actor->attachKnownPeer(peerOf(c),c.protocol,job->end)){contacts.push_back({c,true});}}applied(job,job->control.contacts(node,contacts,body->continuation,monotonic()));if(!body->continuation){job->tickets.erase(node);}}
  later([this,job]{rcmPump(job);});changed();
 },[this,job]{rcmCancel(job);},{},{},std::move(received))){rcmCancel(job);}
}
void Owner::rcmFinish(std::shared_ptr<RcmLookup> job){
 if(job->reported){return;}job->finished=true;job->reported=true;
 if (job->group->attributes) {
  if (job->group->roots && --job->group->roots==0) { rcmCompleteDirectTags(job); }
 } else if (job->group->mergeSearch) {
  if (job->group->roots && --job->group->roots==0) { rcmFinishMergeSearch(*job->group); }
 } else {
  rcmCompleteSwarmTags(job);
 }
 if(job->publish){job->group->hits+=job->values.size();job->group->diversified|=job->diversified;if(job->group->roots){--job->group->roots;}rcmAwaitPublish_.push_back(job);}
 ++rcmFacts_.completed;changed();
}
void Owner::rcmFinishMergeSearch(const RcmGroup& group){
 const auto& search=*group.mergeSearch;
 if(closing_||resettingNetwork_||!search.live->load()){return;}
 const auto hash=search.hash.bytes();
 const std::string own(reinterpret_cast<const char*>(hash.data()),hash.size());
 std::map<std::string,std::string> messages;
 for(const auto& bytes:group.mergeReplies){
  const auto item=rcm::decodeAssociation(bytes,{},0,true,0);
  if(!item||item->hash.size()!=20||item->hash==own||!(item->networks&1)){continue;}
  const auto id=rcm::base32(item->hash);
  const auto title=QString::fromUtf8(item->title.data(),static_cast<qsizetype>(item->title.size())).left(64);
  const auto name=QString::fromUtf8(search.fileName.data(),static_cast<qsizetype>(search.fileName.size())).right(80).simplified().toUtf8().toStdString();
  auto text="Download magnet:?xt=urn:btih:"+id+"&dn="+QUrl::toPercentEncoding(title).toStdString()
      +"[[$dn]] may also contain "+(name.empty()?"this file":"file '"+name+"'")
      +" (same file size)";
  messages.try_emplace(id,std::move(text));
 }
 if(!messages.empty()){ratingNoticeStart(search.hash,search,std::move(messages));}
}
void Owner::rcmCompleteSwarmTags(const std::shared_ptr<RcmLookup>& job) {
 if (closing_ || !job->live->load() || !job->tagProbe || job->tagReplies.empty() ||
     !lanes_[job->lane] || !lanes_[job->lane]->ready || !shared_->swarmProfile) { return; }
 const auto profile=shared_->swarmProfile;
 const auto policy=lanes_[job->lane]->policy;
 const auto pending=std::make_shared<std::optional<SwarmTagUpdate>>();
 const auto capture=std::move(*job->tagProbe);
 job->tagProbe.reset();
 auto replies=std::move(job->tagReplies);
 static_cast<void>(gate(Clock::now()+std::chrono::seconds(5),[this,job,pending] {
  if (closing_ || !job->live->load() || !*pending) { return; }
  std::lock_guard lock(shared_->mutex);
  if (!shared_->stop && shared_->swarmTagUpdates.size()<16) {
   shared_->swarmTagUpdates.push_back(std::move(**pending));
  }
 },[] {},[profile,policy,pending,capture=std::move(capture),replies=std::move(replies),job](std::stop_token stop)->Broker::Result {
  if (stop.stop_requested() || !job->live->load()) { return Broker::Data{}; }
  auto validated=policy->validate();
  if (!validated) { return std::unexpected(validated.error()); }
  auto update=profile->completeSwarmTagProbe(capture,replies);
  if (!update) { return std::unexpected(update.error()); }
  *pending=std::move(*update);
  return Broker::Data{};
 }));
}
bool Owner::rcmTagRequestCurrent(std::uint64_t request)const {
 return (rcmTagLookup_&&rcmTagLookup_->running&&rcmTagLookup_->request==request)||
     (rcmTagScan_&&rcmTagScan_->active.contains(request));
}
void Owner::rcmFinishDirectTags(std::uint64_t request,std::string status,std::vector<std::string> tags,
    std::string title,bool complete) {
 if(rcmTagScan_){
  auto& scan=*rcmTagScan_;
  const auto lookup=scan.active.find(request);
  if(lookup!=scan.active.end()){
   ++scan.finished;
   if(!complete){++scan.unavailable;}
   else{
    const auto now=QDateTime::currentMSecsSinceEpoch();
    for(auto& tag:tags){
     if(rcmTagFindingKeys_.contains({lookup->second,tag})){continue;}
     if(rcmTagFindings_.size()==rcm::MaximumTagScanFindings){scan.resultLimit=true;break;}
     rcmTagFindingKeys_.emplace(lookup->second,tag);
     rcmTagFindings_.push_back({lookup->second,title,std::move(tag),now});
    }
   }
   scan.active.erase(lookup);
   rcmPublishTagScan("Scanning Public Swarm Tags…",true);
   return;
  }
 }
 if (!rcmTagLookup_ || rcmTagLookup_->request!=request || !rcmTagLookup_->running) { return; }
 rcmTagLookup_=std::make_shared<const SwarmTagLookupSnapshot>(
     SwarmTagLookupSnapshot{request,rcmTagLookup_->hash,std::move(status),std::move(tags),false});
 changed();
}
void Owner::rcmStartDirectTags(std::string hash) {
 if (hash.size()!=20 || nextTagLookup_==std::numeric_limits<std::uint64_t>::max() ||
     (rcmTagLookup_ && rcmTagLookup_->running)) { return; }
 const auto request=++nextTagLookup_;
 if(rcmTagScan_){
  rcmTagLookup_=std::make_shared<const SwarmTagLookupSnapshot>(
      SwarmTagLookupSnapshot{request,hash,"A library Tag Discovery scan is running",{},false});
  changed();
  return;
 }
 rcmTagLookup_=std::make_shared<const SwarmTagLookupSnapshot>(
     SwarmTagLookupSnapshot{request,hash,"Looking up Swarm Tags…",{},true});
 if (!rcmQueue(std::move(hash),{},0,0,true,false,request)) {
  rcmFinishDirectTags(request,"No available Public discovery route");
  return;
 }
 changed();
}
void Owner::rcmCompleteDirectTags(const std::shared_ptr<RcmLookup>& job) {
 auto& direct=*job->group->attributes;
 const auto request=direct.request;
 if (closing_ || !direct.capture || !direct.observed || !lanes_[job->lane] ||
     !lanes_[job->lane]->ready || !shared_->swarmProfile) {
  rcmFinishDirectTags(request,"The Public Swarm Tag source is unavailable");
  return;
 }
 const auto profile=shared_->swarmProfile;
 const auto policy=lanes_[job->lane]->policy;
 auto result=std::make_shared<std::optional<SwarmAttributeResult>>();
 auto capture=std::move(*direct.capture);
 auto replies=std::move(direct.replies);
 const auto failed=[this,request]{rcmFinishDirectTags(request,"The Public Swarm Tag source changed");};
 if (!gate(Clock::now()+std::chrono::seconds(5),[this,request,result,failed] {
  if (closing_ || !*result || !rcmTagRequestCurrent(request)) { failed(); return; }
  if (auto& update=(**result).saved; update) {
   std::lock_guard lock(shared_->mutex);
   if (!shared_->stop && shared_->swarmTagUpdates.size()<16) {
    shared_->swarmTagUpdates.push_back(std::move(*update));
   }
  }
  rcmFinishDirectTags(request,"Swarm Tag lookup finished",std::move((**result).found),
      std::move((**result).torrentTitle),true);
 },failed,[profile,policy,result,capture=std::move(capture),replies=std::move(replies)](std::stop_token stop)->Broker::Result {
  if (stop.stop_requested()) { return Broker::Data{}; }
  auto validated=policy->validate();
  if (!validated) { return std::unexpected(validated.error()); }
  auto read=profile->completeDirectTagLookup(capture,replies);
  if (!read) { return std::unexpected(read.error()); }
  *result=std::move(*read);
  return Broker::Data{};
 })) { failed(); }
}
void Owner::rcmPublishTagScan(std::string status,bool running){
 if(!rcmTagScan_){return;}
 const auto& scan=*rcmTagScan_;
 auto view=std::make_shared<SwarmTagScanSnapshot>();
 view->request=scan.request;
 view->total=scan.subjects.size();
 view->finished=scan.finished;
 view->unavailable=scan.unavailable;
 view->running=running;
 view->resultLimit=scan.resultLimit;
 view->status=std::move(status);
 if(rcmTagScanView_&&rcmTagScanView_->findings&&
     rcmTagScanView_->findings->size()==rcmTagFindings_.size()){
  view->findings=rcmTagScanView_->findings;
 }else{
  view->findings=std::make_shared<const std::vector<SwarmTagScanFinding>>(rcmTagFindings_);
 }
 rcmTagScanView_=std::move(view);
 changed();
}
void Owner::rcmStartTagScan(){
 if(rcmTagScan_||nextTagLookup_==std::numeric_limits<std::uint64_t>::max()){return;}
 const auto request=++nextTagLookup_;
 rcmTagScan_.emplace().request=request;
 rcmPublishTagScan("Reading the Tag Discovery library…",true);
 const auto profile=shared_->swarmProfile;
 const auto subjects=std::make_shared<std::vector<std::string>>();
 const auto failed=[this,request]{
  if(rcmTagScan_&&rcmTagScan_->request==request){rcmStopTagScan("Tag Discovery library is unavailable");}
 };
 if(!gate(Clock::now()+std::chrono::seconds(15),[this,request,subjects]{
  if(!rcmTagScan_||rcmTagScan_->request!=request){return;}
  rcmTagScan_->subjects=std::move(*subjects);
  rcmTagScan_->loading=false;
  rcmPublishTagScan("Scanning Public Swarm Tags…",true);
 },failed,[profile,subjects](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()){
   return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Tag Discovery scan cancelled"});
  }
  auto read=profile->libraryTagScanSubjects();
  if(!read){return std::unexpected(read.error());}
  *subjects=std::move(*read);
  return Broker::Data{};
 })){failed();}
}
void Owner::rcmStopTagScan(std::string status){
 if(!rcmTagScan_){return;}
 rcmPublishTagScan(std::move(status),false);
 rcmTagScan_.reset();
 const auto cancel=[this](const auto& jobs){
  for(const auto& job:jobs){
   if(!job->finished&&job->group->attributes&&job->group->attributes->libraryScan){rcmCancel(job);}
  }
 };
 cancel(rcmPending_);cancel(rcmLookups_);
}
void Owner::rcmDismissTags(const rcm::TagScanSelections& selections){
 if(!rcmTagScanView_){return;}
 std::set<std::tuple<std::string_view,std::string_view,std::int64_t>> selected;
 for(const auto& row:selections){
  if(row.request<=rcmTagScanView_->request){selected.emplace(row.hash,row.tag,row.foundOn);}
 }
 if(!std::erase_if(rcmTagFindings_,[&](const auto& row){
  if(!selected.contains({row.hash,row.tag,row.foundOn})){return false;}
  rcmTagFindingKeys_.erase({row.hash,row.tag});
  return true;
 })){return;}
 auto next=std::make_shared<rcm::TagScanSnapshot>(*rcmTagScanView_);
 next->findings=std::make_shared<const std::vector<SwarmTagScanFinding>>(rcmTagFindings_);
 rcmTagScanView_=std::move(next);
 changed();
}
void Owner::rcmPumpTagScan(){
 if(!rcmTagScan_||rcmTagScan_->loading||(rcmTagLookup_&&rcmTagLookup_->running)){return;}
 auto& scan=*rcmTagScan_;
 if(scan.next==scan.subjects.size()){
  if(scan.active.empty()){rcmStopTagScan("Tag Discovery scan finished");}
  return;
 }
 if(!swarmSnapshot_){return;}
 if(!swarmSnapshot_->settings.enabled){rcmStopTagScan("Swarm Discovery is disabled");return;}
 const auto route=std::ranges::any_of(lanes_,[&](const auto& lane){
  return lane&&lane->ready&&(swarmSnapshot_->settings.useBigly ? lane->network==4 : lane->network!=4);
 });
 if(!route){
  if(scan.active.empty()){
   scan.unavailable+=scan.subjects.size()-scan.next;
   scan.finished+=scan.subjects.size()-scan.next;
   rcmStopTagScan("No available Public route for Tag Discovery");
  }
  return;
 }
 while(scan.next<scan.subjects.size()&&scan.active.size()<2&&rcmPending_.size()<128){
  if(nextTagLookup_==std::numeric_limits<std::uint64_t>::max()){
   rcmStopTagScan("Tag Discovery request limit reached");return;
  }
  const auto& hash=scan.subjects[scan.next];
  const auto request=++nextTagLookup_;
  if(!rcmQueue(hash,{},0,0,true,false,request,true)){break;}
  scan.active.emplace(request,hash);
  ++scan.next;
 }
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
bool Owner::rcmReadPermit(std::size_t lane,std::string key,biglybtc::core::MoveOnlyFunction<void(bool)> done){
 const auto found=localRcmKeys_[lane].find(key);if(found==localRcmKeys_[lane].end()){return false;}
 const auto local=found->second;const auto profile=shared_->swarmProfile;const auto policy=lanes_[lane]->policy;auto accepted=std::make_shared<bool>(false);auto callback=std::make_shared<biglybtc::core::MoveOnlyFunction<void(bool)>>(std::move(done));
 return gate(Clock::now()+std::chrono::seconds(10),[this,lane,key=std::move(key),local,accepted,callback]{active_->operation->data.lanes[lane]=true;const auto current=localRcmKeys_[lane].find(key);if(current==localRcmKeys_[lane].end()||current->second!=local){(*callback)(false);return;}if(!*accepted){PublicationClock clock;static_cast<void>(lanes_[lane]->database->removeLocal(key,0,clock));localRcmKeys_[lane].erase(key);}(*callback)(allowed(lane));},[callback]{(*callback)(false);},[local,profile,policy,accepted](std::stop_token)->Broker::Result{
  auto current=policy->validate();if(!current){return std::unexpected(current.error());}auto parent=profile->permitted(local.parent);if(!parent){return std::unexpected(parent.error());}if(!*parent){return Broker::Data{};}auto encoded=profile->publication(local.parent,local.advertised,local.fileSize);if(!encoded){return std::unexpected(encoded.error());}*accepted=*encoded&&**encoded==local.payload;return Broker::Data{};
 });
}
void Owner::rcmCommand(SwarmCommand command){
 if(closing_||!shared_->swarmProfile){return;}
 if(command.kind==SwarmCommand::Kind::Search){if(command.search){rcmStartSearch(std::move(*command.search));}return;}
 if(command.kind==SwarmCommand::Kind::CancelSearch){rcmStopSearch("Search cancelled");return;}
 if(command.kind==SwarmCommand::Kind::Attributes){rcmStartDirectTags(std::move(command.hash));return;}
 if(command.kind==SwarmCommand::Kind::ScanTags){rcmStartTagScan();return;}
 if(command.kind==SwarmCommand::Kind::CancelTagScan){rcmStopTagScan("Tag Discovery scan cancelled");return;}
 if(command.kind==SwarmCommand::Kind::DismissTag){rcmDismissTags(command.tagSelections);return;}
 if(command.kind==SwarmCommand::Kind::MergeSearch){
  if(!command.mergeSearch||!command.mergeSearch->live->load()){return;}
  auto search=std::move(*command.mergeSearch);
  const auto raw=search.hash.bytes();
  if(raw.size()!=20){return;}
  const auto size=search.fileSize;
  rcmQueue(std::string(reinterpret_cast<const char*>(raw.data()),raw.size()),{},size,0,true,false,0,false,std::move(search));
  return;
 }
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
 if(!gate(Clock::now()+std::chrono::seconds(90),[this,snapshot]{rcmSavePending_=false;rcmSnapshot(std::move(*snapshot));},[this]{rcmSavePending_=false;rcmFacts_.status="Discovery cache save is unavailable; data retained";changed();},[profile,snapshot](std::stop_token)->Broker::Result{auto saved=profile->flush();if(!saved){return std::unexpected(saved.error());}auto value=profile->refresh();if(!value){return std::unexpected(value.error());}*snapshot=std::move(*value);return Broker::Data{};})){rcmSavePending_=false;}
}
void Owner::rcmDrainSave(){
 if(!rcmCloseSave_){return;}if(auto ready=Broker::take(rcmCloseSave_)){if(!*ready){rcmFacts_.status="Discovery close save is unavailable";changed();}Broker::acknowledge(rcmCloseSave_);}if(Broker::released(rcmCloseSave_)){shared_->networkIo->release(rcmCloseSave_);rcmCloseSave_.reset();}
}
void Owner::rcmTick(){
 if(closing_){return;}const auto now=Clock::now();if(now<rcmTickAfter_){return;}rcmTickAfter_=now+std::chrono::seconds(1);
 std::deque<SwarmCommand> commands;{std::lock_guard lock(shared_->mutex);commands.swap(shared_->swarmCommands);}for(auto& command:commands){rcmCommand(std::move(command));}rcmSearchTick();
 if(now>=rcmRefreshAfter_){rcmRefreshAfter_=now+std::chrono::seconds(30);rcmRefresh();}
 rcmPumpTagScan();
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

void Owner::rcmDiversify(std::shared_ptr<RcmLookup> job,std::int8_t type){
 job->diversified=true;job->group->diversified=true;if((type!=2&&type!=3)||job->depth>=4){return;}auto record=remoteRecord(job->lane,job->key,type);if(!record){return;}remoteRemember(job->lane,*record,[](bool){});if(job->publish){return;}
 const auto a=QRandomGenerator::system()->bounded(10U),b=(a+1+QRandomGenerator::system()->bounded(9U))%10;for(auto offset:core::azureusDiversificationOffsets(*record,false,false,{a,b})){const auto key=msg::diversifiedDiscoveryKey(job->key,offset);if(!key||job->group->seen.size()>=32||rcmPending_.size()>=128||!job->group->seen.emplace(job->lane,*key).second){continue;}auto child=std::make_shared<RcmLookup>();child->parent=job->parent;child->fileSize=job->fileSize;child->key=*key;child->lane=job->lane;child->group=job->group;child->derived=job->derived;child->explicitLookup=job->explicitLookup;child->level=job->level;child->depth=job->depth+1;child->end=job->end;
  if (job->group->attributes||job->group->mergeSearch) { ++job->group->roots; }
  rcmPending_.push_back(std::move(child));}
}
