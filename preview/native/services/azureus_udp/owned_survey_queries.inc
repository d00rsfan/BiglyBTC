// SPDX-License-Identifier: GPL-2.0-or-later
void OwnedPingTransport::observeSurveyQuery(const find::nodes::Contact& sender,const std::vector<std::optional<std::string>>& keys){
 if(!surveyBook_||!normalDatabase_){return;}auto at=surveyBook_->states.find(sender.id);if(at==surveyBook_->states.end()){return;}auto& entry=*at->second;entry.contact=sender;entry.state.updateInstance(sender.instance);entry.touched=++surveyBook_->touch;
 for(const auto& key:keys){if(!key){continue;}auto id=normalDatabase_->mappingIdentity(*key);if(id&&*id){auto inserted=entry.state.add(**id);if(!inserted&&inserted.error()==survey_state::StateError::Capacity){entry.state.clearKnown();static_cast<void>(entry.state.add(**id));}}}
}
void OwnedPingTransport::surveyBuildQueries(const std::shared_ptr<SurveyJob>& job){
 std::vector<survey_graph::Node> nodes;std::vector<find::nodes::Contact> contacts;std::vector<std::string> keys;for(const auto& [id,contact]:job->neighbours){survey_graph::Node node;node.id=id;node.address=contact.address.bytes;node.addressLength=contact.address.length;node.protocol=contact.protocol;nodes.push_back(node);contacts.push_back(contact);}for(const auto& candidate:job->candidates){keys.push_back(candidate.subject.key);}
 auto graph=survey_graph::topology(job->local,job->k,nodes,keys);if(!graph){cancelSurvey(Failure::State);return;}job->topology=std::move(*graph);job->knowledge.resize(nodes.size());for(std::size_t i=0;i<nodes.size();++i){job->knowledge[i].id=nodes[i].id;}
 for(const auto& queryPlan:job->topology->queries){
  auto peer=routingPeer(contacts[queryPlan.node]);if(!peer||!allowed(*peer)){cancelSurvey(Failure::Authority);return;}SurveyJob::QueryPeer target;target.contact=contacts[queryPlan.node];target.peer=*peer;
  auto previous=surveyBook_->states.find(target.contact.id);if(previous!=surveyBook_->states.end()){target.entry=previous->second;target.entry->touched=++surveyBook_->touch;}
  query::RequestBody request;std::size_t offset=0;
  for(const auto& group:queryPlan.request.groups){query::KeyGroup next;next.prefix=group.prefix;for(const auto& suffix:group.suffixes){if(offset>=queryPlan.keys.size()){cancelSurvey(Failure::State);return;}const auto key=queryPlan.keys[offset++];if(!target.entry||!target.entry->state.contains(job->candidates[key].subject.incarnation)){next.suffixes.push_back(suffix);target.keys.push_back(key);}}request.groups.push_back(std::move(next));}
  packet::udp::EncodeFields header;header.action=1038;header.localVersion=config_.version;header.remoteVersion=target.contact.protocol;header.network=config_.network;header.connection=INT64_MIN;header.originator=originator(endpoint());auto packets=query::splitRequest(request,header);if(!packets){cancelSurvey(Failure::Encode);return;}target.packets=std::move(*packets);target.replies.reserve(target.keys.size());job->queries.push_back(std::move(target));
 }
 job->stage=SurveyJob::Stage::Queries;++job->epoch;
}
void OwnedPingTransport::surveyQueryStep(const std::shared_ptr<SurveyJob>& job,std::size_t index){
 surveyPermit(job,index,false,{},[this,job,index]{
  if(!surveyAlive(job)||job->stage!=SurveyJob::Stage::Queries||index>=job->queries.size()){return;}auto& peer=job->queries[index];if(peer.done||peer.waiting||peer.next>=peer.packets.size()){return;}const auto serial=peer.serial;peer.waiting=true;const auto timeout=std::min(job->end-Clock::now(),Clock::duration(std::chrono::seconds(30)));QPointer<OwnedPingTransport> self(this);
  auto sent=queryStorage(peer.peer,peer.contact.protocol,peer.packets[peer.next],this,[self,job,index,serial](auto result){if(self){self->surveyQueryReply(job,index,serial,std::move(result));}},timeout);
  if(!sent){peer.waiting=false;cancelSurvey(sent.error());return;}peer.ticket=*sent;++surveyFacts_.queries;
 });
}
void OwnedPingTransport::surveyQueryReply(const std::shared_ptr<SurveyJob>& job,std::size_t index,std::uint64_t serial,QueryStoreOutcome result){
 if(!surveyAlive(job)||job->stage!=SurveyJob::Stage::Queries||index>=job->queries.size()){return;}auto& peer=job->queries[index];if(peer.serial!=serial||!peer.waiting||peer.done){return;}peer.waiting=false;peer.ticket={};
 const auto* packet=result?std::get_if<query::Decoded>(&*result):nullptr;if(!packet){peer.failed=peer.done=true;++surveyFacts_.failed;return;}
 const auto& response=std::get<query::ReplyBody>(packet->body());if(peer.instance&&*peer.instance!=packet->header().fields().instance){peer.failed=peer.done=true;++surveyFacts_.failed;return;}peer.instance=packet->header().fields().instance;
 std::size_t expected=0;for(const auto& group:peer.packets[peer.next].groups){expected+=group.suffixes.size();}if(response.entries.size()!=expected){peer.badSize=peer.done=true;return;}peer.replies.insert(peer.replies.end(),response.entries.begin(),response.entries.end());++peer.next;++surveyFacts_.replies;if(peer.next==peer.packets.size()){peer.done=true;}
}
void OwnedPingTransport::surveyProjectReply(const std::shared_ptr<SurveyJob>& job){
 if(!job->topology){cancelSurvey(Failure::State);return;}if(job->project>=job->queries.size()){surveyBuildRepairs(job);return;}auto& peer=job->queries[job->project++];auto contact=peer.contact;if(peer.instance){contact.instance=*peer.instance;}
 auto existing=surveyBook_->states.find(contact.id);std::shared_ptr<SurveyBook::Entry> entry;
 if(existing!=surveyBook_->states.end()){entry=existing->second;}else{
  if(surveyBook_->states.size()>=150){auto oldest=std::min_element(surveyBook_->states.begin(),surveyBook_->states.end(),[](const auto& a,const auto& b){return a.second->touched<b.second->touched;});if(oldest!=surveyBook_->states.end()){surveyBook_->states.erase(oldest);}}
  auto state=survey_state::ContactState::create(contact.instance,surveyMono(),QRandomGenerator::system()->bounded(3600000U));if(!state){cancelSurvey(Failure::Capacity);return;}entry=std::make_shared<SurveyBook::Entry>(contact,std::move(*state));surveyBook_->states.emplace(contact.id,entry);
 }
 entry->contact=contact;entry->state.updateInstance(contact.instance);entry->state.updateUseTime(surveyMono());entry->touched=++surveyBook_->touch;peer.entry=entry;
 bool counted=false;
 if(peer.failed){entry->state.failure();}else{
  entry->state.success();if(!peer.badSize&&peer.replies.size()==peer.keys.size()){
   counted=true;
   for(std::size_t i=0;i<peer.keys.size();++i){const auto& candidate=job->candidates[peer.keys[i]];const auto id=candidate.subject.incarnation;const auto& reply=peer.replies[i];if(!reply){entry->state.remove(id);continue;}if(reply->size()<2||reply->size()>=20){continue;}
    auto identity=normalDatabase_->mappingIdentity(candidate.subject.key);if(!identity||!*identity||**identity!=id){continue;}const std::vector<std::string> prefix{candidate.subject.key.substr(0,6)};auto matches=normalDatabase_->queryStoredKeys(prefix);if(!matches||matches->empty()||!(*matches)[0]){continue;}const auto& key=*(*matches)[0];if(*reply==key.substr(key.size()-reply->size())){auto added=entry->state.add(id);if(!added&&added.error()==survey_state::StateError::Capacity){entry->state.clearKnown();static_cast<void>(entry->state.add(id));}}else{entry->state.remove(id);}
   }
  }
 }
 const auto at=std::ranges::find(job->topology->nodes,contact.id,&survey_graph::Node::id);if(at==job->topology->nodes.end()){cancelSurvey(Failure::State);return;}const auto node=std::size_t(at-job->topology->nodes.begin());auto& known=job->knowledge[node];known.hasState=true;known.responseCounted=counted;known.consecutiveFailures=entry->state.failures();
 for(std::size_t key=0;key<job->candidates.size();++key){if(entry->state.contains(job->candidates[key].subject.incarnation)){known.knownKeys.push_back(key);}}
}
void OwnedPingTransport::surveyBuildRepairs(const std::shared_ptr<SurveyJob>& job){
 std::vector<std::int8_t> factors;for(const auto& candidate:job->candidates){factors.push_back(candidate.factor);}auto plan=survey_graph::repairs(*job->topology,*localId_,job->knowledge,factors);if(!plan){cancelSurvey(Failure::State);return;}
 std::map<std::size_t,std::size_t> targets;
 for(std::size_t key=0;key<plan->targetsByKey.size();++key){for(const auto node:plan->targetsByKey[key]){
  auto [at,inserted]=targets.emplace(node,job->repairs.size());if(inserted){const auto& id=job->topology->nodes[node].id;auto contact=std::ranges::find(normalContacts_,id,&find::nodes::Contact::id);if(contact==normalContacts_.end()){cancelSurvey(Failure::Authority);return;}auto endpoint=routingPeer(*contact);if(!endpoint){cancelSurvey(Failure::Authority);return;}SurveyJob::RepairPeer peer;peer.contact=*contact;peer.peer=*endpoint;auto state=surveyBook_->states.find(id);if(state==surveyBook_->states.end()){cancelSurvey(Failure::State);return;}peer.entry=state->second;job->repairs.push_back(std::move(peer));}job->repairs[at->second].keys.push_back(key);
 }}
 for(const auto& assumed:plan->assumed){const auto& id=job->topology->nodes[assumed.node].id;auto state=surveyBook_->states.find(id);if(state!=surveyBook_->states.end()){auto added=state->second->state.add(job->candidates[assumed.key].subject.incarnation);if(!added&&added.error()==survey_state::StateError::Capacity){state->second->state.clearKnown();static_cast<void>(state->second->state.add(job->candidates[assumed.key].subject.incarnation));}}}
 job->stage=SurveyJob::Stage::Repairs;++job->epoch;job->queries.clear();job->knowledge.clear();job->topology.reset();
}
