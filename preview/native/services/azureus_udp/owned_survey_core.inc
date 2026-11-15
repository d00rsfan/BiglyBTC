// SPDX-License-Identifier: GPL-2.0-or-later
// Sole serialized actor. All filesystem work remains on the existing worker.
namespace survey_state=database::survey;
namespace survey_graph=database::survey_graph;
struct OwnedPingTransport::SurveyBook {
 struct Entry {find::nodes::Contact contact;survey_state::ContactState state;std::uint64_t touched=0;Entry(find::nodes::Contact c,survey_state::ContactState s):contact(std::move(c)),state(std::move(s)) {}};
 std::map<find::nodes::NodeId,std::shared_ptr<Entry>> states;
 std::map<std::string,std::int64_t> times;
 std::shared_ptr<SurveyJob> job;std::uint64_t touch=0;
};
struct OwnedPingTransport::SurveyJob {
 enum class Stage {Gather,Route,Queries,Project,Repairs};Stage stage=Stage::Gather;
 struct Candidate {database::SurveySubject subject;database::CacheReceipt receipt;std::int8_t factor=0;};
 struct QueryPeer {find::nodes::Contact contact;Peer peer;std::vector<query::RequestBody> packets;std::vector<std::size_t> keys;std::vector<std::optional<std::string>> replies;std::size_t next=0;std::uint64_t serial=0;Ticket ticket;Clock::time_point gateEnd{};bool gating=false,waiting=false,done=false,failed=false,badSize=false;std::optional<std::int32_t> instance;std::shared_ptr<SurveyBook::Entry> entry;};
 struct RepairPeer {find::nodes::Contact contact;Peer peer;std::vector<std::size_t> keys;std::size_t next=0,value=0,sentValues=0;std::uint64_t serial=0;Ticket ticket;Clock::time_point gateEnd{},cookieAt{};std::optional<std::int32_t> cookie;bool gating=false,waiting=false,done=false;std::shared_ptr<SurveyBook::Entry> entry;};
 database::SurveyCycle cycle;bool cycleLive=true;std::size_t cursor=0,project=0;std::uint64_t sequence=0,epoch=1,stepSerial=0,route=0;bool stepPending=false;Clock::time_point stepEnd{},end;
 find::nodes::NodeId local{},routeTarget{};std::size_t k=20;unsigned requeries=0;
 std::vector<Candidate> candidates;std::map<find::nodes::NodeId,find::nodes::Contact> neighbours;
 std::optional<survey_graph::Topology> topology;std::vector<QueryPeer> queries;std::vector<RepairPeer> repairs;std::vector<survey_graph::Knowledge> knowledge;
};
namespace {
std::int64_t surveyMono(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool surveyCloser(const find::nodes::NodeId& a,const find::nodes::NodeId& b,const find::nodes::NodeId& key){for(std::size_t i=0;i<20;++i){const auto x=a[i]^key[i],y=b[i]^key[i];if(x!=y){return x<y;}}return false;}
find::nodes::NodeId surveyKey(std::string_view key){find::nodes::NodeId out{};if(key.size()==20){std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()),20,out.begin());}return out;}
}
bool OwnedPingTransport::surveyAlive(const std::shared_ptr<SurveyJob>& job)const noexcept{return owner()&&state_==State::Bound&&surveyBook_&&surveyBook_->job==job&&surveyFacts_.active&&Clock::now()<job->end;}
bool OwnedPingTransport::surveyOwnsRouting()const noexcept{return surveyBook_&&surveyBook_->job&&surveyBook_->job->route!=0;}
SurveyMaintenanceFacts OwnedPingTransport::surveyMaintenanceFacts()const noexcept{return owner()?surveyFacts_:SurveyMaintenanceFacts{};}
bool OwnedPingTransport::cancelSurveyValues(std::uint64_t operation){if(!owner()||!surveyFacts_.active||surveyFacts_.operation!=operation){return false;}cancelSurvey(Failure::Cancelled);return true;}
void OwnedPingTransport::cancelSurvey(std::optional<Failure> failure){
 surveyDeadline_.stop();if(!surveyBook_||!surveyBook_->job){return;}auto job=std::move(surveyBook_->job);surveyFacts_.active=false;surveyFacts_.failure=failure;
 if(job->route){static_cast<void>(cancelRouting(job->route));job->route=0;}
 for(auto& peer:job->queries){if(peer.ticket.value){cancel(peer.ticket);}}
 for(auto& peer:job->repairs){if(peer.ticket.value){cancel(peer.ticket);}}
 if(job->cycleLive&&normalDatabase_){static_cast<void>(normalDatabase_->endSurvey(job->cycle));job->cycleLive=false;}
 job->candidates.clear();job->queries.clear();job->repairs.clear();job->neighbours.clear();job->knowledge.clear();job->topology.reset();
}
std::expected<std::uint64_t,Failure> OwnedPingTransport::surveyCachedValues(std::chrono::milliseconds duration){
 if(!owner()){return std::unexpected(Failure::Thread);}if(!config_.enableSurveys){return std::unexpected(Failure::State);}if(!normalCurrent()||!normalSeeded_||!normalDatabase_||!localId_||!normalPreflight_||!dynamicRoutingScope(config_.routingScope)){return std::unexpected(Failure::Authority);}
 if(duration<=std::chrono::milliseconds::zero()||duration>std::chrono::minutes(30)){return std::unexpected(Failure::Deadline);}if(surveyFacts_.active||surveyFacts_.operation==UINT64_MAX){return std::unexpected(Failure::Capacity);}
 try{
  if(!surveyBook_){surveyBook_=std::make_shared<SurveyBook>();}struct Wall final:database::lifecycle::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}} clock;
  auto begin=normalDatabase_->beginSurvey(clock);if(!begin){return std::unexpected(Failure::State);}bool installed=false;const auto release=qScopeGuard([&]{if(!installed){static_cast<void>(normalDatabase_->endSurvey(begin->cycle));}});
  auto router=normalRouter_->snapshot();if(!router||router->limited()||router->config().k>64){return std::unexpected(Failure::State);}auto job=std::make_shared<SurveyJob>();job->cycle=begin->cycle;job->local=*localId_;job->k=router->config().k;job->end=Clock::now()+duration;
  std::set<std::string_view> liveKeys;std::set<std::uint64_t> liveIds;for(const auto& subject:begin->live){liveKeys.insert(subject.key);liveIds.insert(subject.incarnation);}std::erase_if(surveyBook_->times,[&](const auto& value){return !liveKeys.contains(value.first);});const auto mono=surveyMono();
  std::erase_if(surveyBook_->states,[&](const auto& value){return value.second->state.expired(mono);});
  for(auto& [id,entry]:surveyBook_->states){(void)id;for(auto it=entry->state.known().begin();it!=entry->state.known().end();){const auto known=*it++;if(!liveIds.contains(known)){entry->state.remove(known);}}}
  const auto operation=surveyFacts_.operation+1;surveyFacts_={};surveyFacts_.operation=operation;surveyFacts_.active=true;surveyBook_->job=job;installed=true;surveyDeadline_.start();surveyStep(job,true);return operation;
 }catch(...){cancelSurvey(Failure::Capacity);return std::unexpected(Failure::Capacity);}
}
void OwnedPingTransport::surveyPermit(const std::shared_ptr<SurveyJob>& job,std::optional<std::size_t> index,bool repair,std::optional<std::string> key,std::move_only_function<void()> next){
 if(!surveyAlive(job)){return;}const auto epoch=job->epoch,serial=++job->sequence;const auto until=Clock::now()+std::chrono::seconds(5);
 if(index){if(repair){if(*index>=job->repairs.size()){return;}auto& peer=job->repairs[*index];peer.serial=serial;peer.gating=true;peer.gateEnd=until;}else{if(*index>=job->queries.size()){return;}auto& peer=job->queries[*index];peer.serial=serial;peer.gating=true;peer.gateEnd=until;}}
 else{job->stepSerial=serial;job->stepPending=true;job->stepEnd=until;}
 QPointer<OwnedPingTransport> self(this);auto once=std::make_shared<std::atomic_bool>(false);
 auto done=[self,job,index,repair,epoch,serial,until,once,next=std::move(next)](bool accepted)mutable{
  if(once->exchange(true)||!self){return;}if(!self->owner()){QMetaObject::invokeMethod(self,[self,job]{if(self&&self->surveyAlive(job)){self->cancelSurvey(Failure::Authority);}},Qt::QueuedConnection);return;}
  if(!self->surveyAlive(job)||job->epoch!=epoch){return;}
  if(index){if(repair){if(*index>=job->repairs.size()||job->repairs[*index].serial!=serial){return;}job->repairs[*index].gating=false;}else{if(*index>=job->queries.size()||job->queries[*index].serial!=serial){return;}job->queries[*index].gating=false;}}
  else{if(job->stepSerial!=serial){return;}job->stepPending=false;}
  if(!accepted||Clock::now()>=until||!self->normalCurrent()){self->cancelSurvey(Failure::Authority);return;}try{next();}catch(...){if(self){self->cancelSurvey(Failure::Capacity);}}
 };
 bool accepted=false;try{if(key&&localReadPreflight_){accepted=localReadPreflight_(*key,std::move(done));}else{accepted=normalPreflight_(std::move(done));}}catch(...){accepted=false;}
 if(self&&!accepted&&!once->load()&&surveyAlive(job)){cancelSurvey(Failure::Capacity);}
}
void OwnedPingTransport::tickSurvey(){
 if(!surveyBook_||!surveyBook_->job){surveyDeadline_.stop();return;}auto job=surveyBook_->job;if(!surveyAlive(job)){cancelSurvey(Failure::Timeout);return;}const auto now=Clock::now();
 if(job->stepPending){if(now>=job->stepEnd){cancelSurvey(Failure::Timeout);}return;}
 for(const auto& peer:job->queries){if(peer.gating&&now>=peer.gateEnd){cancelSurvey(Failure::Timeout);return;}}
 for(const auto& peer:job->repairs){if(peer.gating&&now>=peer.gateEnd){cancelSurvey(Failure::Timeout);return;}}
 try{surveyStep(job);}catch(...){cancelSurvey(Failure::Capacity);}
}
void OwnedPingTransport::surveyStep(const std::shared_ptr<SurveyJob>& job,bool granted){
 if(!surveyAlive(job)){return;}
 if(job->stage==SurveyJob::Stage::Queries){for(std::size_t i=0,active=0;i<job->queries.size();++i){auto& peer=job->queries[i];if(peer.done){continue;}if(active++==3){break;}if(!peer.gating&&!peer.waiting){surveyQueryStep(job,i);}if(!surveyAlive(job)){return;}}if(std::ranges::all_of(job->queries,[](const auto& peer){return peer.done;})){job->stage=SurveyJob::Stage::Project;++job->epoch;job->project=0;}return;}
 if(job->stage==SurveyJob::Stage::Repairs){for(std::size_t i=0,active=0;i<job->repairs.size();++i){auto& peer=job->repairs[i];if(peer.done){continue;}if(active++==3){break;}if(!peer.gating&&!peer.waiting){surveyRepairStep(job,i);}if(!surveyAlive(job)){return;}}if(std::ranges::all_of(job->repairs,[](const auto& peer){return peer.done;})){cancelSurvey();}return;}
 if(job->stage==SurveyJob::Stage::Route&&routing_&&!routing_->finished){return;}
 if(!granted){if(!job->stepPending){surveyPermit(job,{},false,{},[this,job]{surveyStep(job,true);});}return;}
 if(job->stage==SurveyJob::Stage::Gather){surveyGather(job);}else if(job->stage==SurveyJob::Stage::Route){surveyRoute(job);}else{surveyProjectReply(job);}
}
void OwnedPingTransport::surveyGather(const std::shared_ptr<SurveyJob>& job){
 auto page=normalDatabase_->surveyPage(job->cycle,job->cursor,64,256);if(!page){cancelSurvey(Failure::State);return;}job->cursor=page->next;
 for(const auto& mapping:page->mappings){++surveyFacts_.considered;if(normalKeyBlocks_&&normalKeyBlocks_->blocked(mapping.subject.key)){continue;}std::vector<survey_state::Value> values;std::int8_t factor=-1;for(const auto& value:mapping.values){values.push_back({value.creationTime(),value.replicationControl()});factor=std::max(factor,value.replicationControl()==-1?std::int8_t{-1}:std::int8_t(value.replicationControl()&15));}
  survey_state::Observation now;now.wall=QDateTime::currentMSecsSinceEpoch();now.monotonic=surveyMono();const auto found=surveyBook_->times.find(mapping.subject.key);if(found!=surveyBook_->times.end()){now.previous=found->second;}auto selected=survey_state::eligibility(mapping.diversification,values,now);if(!selected){cancelSurvey(Failure::State);return;}
  if(selected->jitterConsumed){now.jitter=int(QRandomGenerator::system()->bounded(1800000U))-900000;selected=survey_state::eligibility(mapping.diversification,values,now);if(!selected){cancelSurvey(Failure::State);return;}}
  if(selected->next){surveyBook_->times.insert_or_assign(mapping.subject.key,*selected->next);}if(selected->eligible){job->candidates.push_back({mapping.subject,mapping.receipt,factor});++surveyFacts_.eligible;}
 }
 if(page->more){return;}static_cast<void>(normalDatabase_->endSurvey(job->cycle));job->cycleLive=false;if(job->candidates.empty()){cancelSurvey();return;}
 auto closest=normalRouter_->closest(job->local,std::min<std::size_t>(job->k*3,100),true);if(!closest){cancelSurvey(Failure::State);return;}std::ranges::sort(std::span(closest->values).first(closest->count),[&](const auto& a,const auto& b){return surveyCloser(a.id,b.id,job->local);});
 auto local=normalDatabase_->localContact();if(!local||!*local){cancelSurvey(Failure::State);return;}job->neighbours.emplace(job->local,**local);
 for(std::size_t i=0;i<std::min<std::size_t>(closest->count,job->k*3)&&job->neighbours.size()<100;++i){const auto& node=closest->values[i];if(node.local){continue;}const auto contact=std::ranges::find(normalContacts_,node.id,&find::nodes::Contact::id);if(contact!=normalContacts_.end()&&contact->protocolSource!=RoutingProtocolSource::ProbeHint&&routingPeer(*contact)){job->neighbours.insert_or_assign(node.id,*contact);}}
 auto far=surveyKey(job->candidates.front().subject.key);for(const auto& mapping:job->candidates){const auto key=surveyKey(mapping.subject.key);if(surveyCloser(far,key,job->local)){far=key;}}job->routeTarget=survey_state::obfuscatedTarget(far);job->stage=SurveyJob::Stage::Route;++job->epoch;
}
void OwnedPingTransport::surveyRoute(const std::shared_ptr<SurveyJob>& job){
 if(!job->route){if(routing_&&!routing_->finished){return;}auto sent=startRouting(job->routeTarget,std::min(job->end,Clock::now()+std::chrono::seconds(30)),false);if(!sent){if(sent.error()!=Failure::Capacity){cancelSurvey(sent.error());}return;}job->route=*sent;++surveyFacts_.lookups;return;}
 if(!routing_||routing_->id!=job->route){cancelSurvey(Failure::Cancelled);return;}if(!routing_->finished){return;}auto route=routing_->controller.snapshot();if(!route){cancelSurvey(Failure::State);return;}job->route=0;
 std::vector<find::nodes::Contact> found;for(const auto& node:route->nodes){if(node.state!=azureus_control::LookupNodeState::Replied){continue;}auto current=std::ranges::find(normalContacts_,node.contact.id,&find::nodes::Contact::id);if(current!=normalContacts_.end()&&current->protocolSource==RoutingProtocolSource::Packet&&routingPeer(*current)){found.push_back(*current);}}
 std::ranges::sort(found,[&](const auto& a,const auto& b){return surveyCloser(a.id,b.id,job->routeTarget);});if(found.size()>job->k){found.resize(job->k);}unsigned hits=0,misses=0;std::optional<find::nodes::NodeId> nearest;
 for(const auto& contact:found){if(job->neighbours.contains(contact.id)){++hits;}else{++misses;if(job->neighbours.size()>=100){break;}job->neighbours.emplace(contact.id,contact);if(!nearest||surveyCloser(contact.id,*nearest,job->local)){nearest=contact.id;}}}
 if(nearest&&misses&&misses*100/(hits+misses)>=25&&job->neighbours.size()<100&&job->requeries++<5){job->routeTarget=*nearest;return;}surveyBuildQueries(job);
}
