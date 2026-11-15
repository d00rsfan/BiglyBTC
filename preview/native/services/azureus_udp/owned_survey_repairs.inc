// SPDX-License-Identifier: GPL-2.0-or-later
void OwnedPingTransport::surveyRepairStep(const std::shared_ptr<SurveyJob>& job,std::size_t index){
 if(!surveyAlive(job)||index>=job->repairs.size()){return;}auto& initial=job->repairs[index];if(initial.next>=initial.keys.size()){initial.done=true;return;}const auto key=job->candidates[initial.keys[initial.next]].subject.key;
 surveyPermit(job,index,true,key,[this,job,index]{
  if(!surveyAlive(job)||job->stage!=SurveyJob::Stage::Repairs||index>=job->repairs.size()){return;}auto& peer=job->repairs[index];if(peer.done||peer.waiting||peer.next>=peer.keys.size()){return;}const auto& candidate=job->candidates[peer.keys[peer.next]];
  auto current=normalDatabase_->surveyMapping(candidate.subject.key,candidate.subject.incarnation,candidate.receipt);if(!current){cancelSurvey(Failure::State);return;}if(!*current||(**current).diversification!=1||(normalKeyBlocks_&&normalKeyBlocks_->blocked(candidate.subject.key))){++peer.next;peer.value=0;return;}
  auto contact=std::ranges::find(normalContacts_,peer.contact.id,&find::nodes::Contact::id);if(contact==normalContacts_.end()||contact->protocolSource==RoutingProtocolSource::ProbeHint){cancelSurvey(Failure::Authority);return;}auto endpointNow=routingPeer(*contact);if(!endpointNow||*endpointNow!=peer.peer||contact->protocol!=peer.contact.protocol){cancelSurvey(Failure::Authority);return;}
  if(peer.contact.instance!=contact->instance){peer.contact=*contact;peer.cookie.reset();peer.entry->contact=*contact;peer.entry->state.updateInstance(contact->instance);}if(peer.cookie&&Clock::now()-peer.cookieAt>=std::chrono::seconds(60)){peer.cookie.reset();}
  QPointer<OwnedPingTransport> self(this);const auto serial=peer.serial;const auto budget=std::min(job->end-Clock::now(),Clock::duration(std::chrono::seconds(30)));
  if(!peer.cookie){peer.waiting=true;auto sent=requestStoreToken(peer.peer,peer.contact.protocol,this,[self,job,index,serial](auto result){if(self){self->surveyTokenReply(job,index,serial,std::move(result));}},budget);if(!sent){peer.waiting=false;cancelSurvey(sent.error());return;}peer.ticket=*sent;++surveyFacts_.tokens;return;}
  std::vector<database::lifecycle::DbValue> values;for(const auto& value:(**current).values){if(!value.isLocal()){values.push_back(value);}}
  if(values.empty()){++peer.next;peer.value=0;return;}
  if(peer.value>=values.size()){
   peer.entry->state.success();auto added=peer.entry->state.add(candidate.subject.incarnation);if(!added&&added.error()==survey_state::StateError::Capacity){peer.entry->state.clearKnown();static_cast<void>(peer.entry->state.add(candidate.subject.incarnation));}++peer.next;peer.value=0;return;
  }
  auto local=normalDatabase_->localContact();if(!local||!*local||(**local).id!=job->local){cancelSurvey(Failure::State);return;}auto relayContact=database::lifecycle::ContactHandle::copy(**local);if(!relayContact.get()){cancelSurvey(Failure::Capacity);return;}struct Wall final:database::lifecycle::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}} clock;
  StoreRequest request;request.randomId=*peer.cookie;request.keys={candidate.subject.key};request.valueSets.resize(1);packet::udp::EncodeFields header;header.action=1026;header.localVersion=config_.version;header.remoteVersion=peer.contact.protocol;header.network=config_.network;header.instance=config_.instance;header.connection=INT64_MIN;header.originator=originator(this->endpoint());header.now=clock.nowMilliseconds();header.flags=config_.flags;header.flags2=config_.flags2;
  std::size_t count=0;
  for(std::size_t n=peer.value;n<values.size()&&count<256;++n){
   const auto value=values[n].relay(relayContact,clock);const auto origin=value.originator().get();const auto payload=value.payload().view();if(!origin||!payload){cancelSurvey(Failure::State);return;}request.valueSets[0].push_back({value.version(),value.creationTime(),std::string(*payload),*origin,value.flags(),value.lifeTimeHours(),value.replicationControl()});auto wire=encodeOwnedStoreRequest(request,header);if(!wire||wire->size()>std::size_t(maximumDatagram)){request.valueSets[0].pop_back();break;}++count;
  }
  if(!count){cancelSurvey(Failure::Encode);return;}auto fresh=normalDatabase_->cacheReceipt(candidate.subject.key);auto identity=normalDatabase_->mappingIdentity(candidate.subject.key);if(!fresh||!*fresh||**fresh!=candidate.receipt||!identity||!*identity||**identity!=candidate.subject.incarnation||!normalCurrent()){++peer.next;peer.value=0;return;}
  peer.sentValues=count;peer.waiting=true;auto sent=store(peer.peer,peer.contact.protocol,std::move(request),this,[self,job,index,serial](auto result){if(self){self->surveyStoreReply(job,index,serial,std::move(result));}},budget);if(!sent){peer.waiting=false;cancelSurvey(sent.error());return;}peer.ticket=*sent;++surveyFacts_.packets;surveyFacts_.values+=count;
 });
}
void OwnedPingTransport::surveyTokenReply(const std::shared_ptr<SurveyJob>& job,std::size_t index,std::uint64_t serial,DetailedFindNodeOutcome result){
 if(!surveyAlive(job)||job->stage!=SurveyJob::Stage::Repairs||index>=job->repairs.size()){return;}auto& peer=job->repairs[index];if(peer.serial!=serial||!peer.waiting||peer.done){return;}peer.waiting=false;peer.ticket={};const auto* packet=result?std::get_if<find::Decoded>(&*result):nullptr;
 if(!packet){peer.done=true;peer.entry->state.failure();++surveyFacts_.failed;return;}const auto* reply=std::get_if<find::ReplyBody>(&packet->body());if(!reply){peer.done=true;peer.entry->state.failure();++surveyFacts_.failed;return;}
 peer.cookie=reply->randomId;peer.cookieAt=Clock::now();peer.contact.instance=packet->header().fields().instance;peer.entry->contact=peer.contact;peer.entry->state.updateInstance(peer.contact.instance);
}
void OwnedPingTransport::surveyStoreReply(const std::shared_ptr<SurveyJob>& job,std::size_t index,std::uint64_t serial,StoreOutcome result){
 if(!surveyAlive(job)||job->stage!=SurveyJob::Stage::Repairs||index>=job->repairs.size()){return;}auto& peer=job->repairs[index];if(peer.serial!=serial||!peer.waiting||peer.done){return;}peer.waiting=false;peer.ticket={};const auto* packet=result?std::get_if<storage::Decoded>(&*result):nullptr;
 if(!packet||packet->header().fields().instance!=peer.contact.instance){peer.done=true;peer.entry->state.failure();++surveyFacts_.failed;return;}peer.value+=peer.sentValues;peer.sentValues=0;
}
