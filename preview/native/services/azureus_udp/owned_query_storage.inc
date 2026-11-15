// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the sole owned UDP actor. No profile I/O or ordinary DB reads.
std::expected<Ticket,Failure> OwnedPingTransport::queryStorage(Peer peer,std::int8_t version,query::RequestBody body,QObject* receiver,QueryStoreCallback callback,std::chrono::nanoseconds timeout){
 if(body.headerLength!=6){return std::unexpected(Failure::Encode);}std::size_t count=0;for(const auto& group:body.groups){if(group.suffixes.size()>384-count){return std::unexpected(Failure::Capacity);}count+=group.suffixes.size();}
 return submit(std::move(peer),version,receiver,detailedCallback<query::Decoded>(std::move(callback)),timeout,1038,[body=std::move(body)](packet::udp::EncodeFields header)->std::expected<std::string,Failure>{auto wire=query::encodeRequest({header,body});if(!wire){return std::unexpected(Failure::Encode);}return std::move(*wire);});
}
void OwnedPingTransport::finishQueryReply(const std::shared_ptr<QueryReply>& job){
 if(!job->live){return;}job->live=false;job->prefixes.clear();job->keys.clear();job->matches.clear();std::erase(queryReplies_,job);if(queryReplies_.empty()){queryReplyDeadline_.stop();}
}
void OwnedPingTransport::cancelQueryReplies(){
 queryReplyDeadline_.stop();while(!queryReplies_.empty()){auto job=queryReplies_.back();finishQueryReply(job);}
}
void OwnedPingTransport::expireQueryReplies(){
 const auto jobs=queryReplies_;for(const auto& job:jobs){if(Clock::now()>=job->end){++statistics_.timeouts;finishQueryReply(job);}}
}
void OwnedPingTransport::answerQuery(const Peer& peer,query::Decoded request,std::int64_t receivedAt){
 if(state_!=State::Bound||queryReplies_.size()>=32){++statistics_.serviceCapacityDrops;return;}
 const auto& body=std::get<query::RequestBody>(request.body());if(body.headerLength!=6){++statistics_.malformed;return;}
 try{auto job=std::make_shared<QueryReply>(peer,std::move(request));job->receivedAt=receivedAt;job->end=Clock::now()+std::chrono::seconds(5);for(const auto& group:std::get<query::RequestBody>(job->request.body()).groups){if(group.suffixes.size()>384-job->prefixes.size()){++statistics_.serviceCapacityDrops;return;}for(const auto& suffix:group.suffixes){job->prefixes.push_back(group.prefix+suffix);}}
  queryReplies_.push_back(job);if(!queryReplyDeadline_.isActive()){queryReplyDeadline_.start();}queryReplyGate(job);
 }catch(...){++statistics_.serviceCapacityDrops;}
}
void OwnedPingTransport::queryReplyGate(const std::shared_ptr<QueryReply>& job){
 if(!job->live){return;}if(state_!=State::Bound||Clock::now()>=job->end){finishQueryReply(job);return;}
 if(!normalRouter_){queryReplyReady(job);return;}const auto phase=++job->phase;auto once=std::make_shared<std::atomic_bool>(false);QPointer<OwnedPingTransport> self(this);
 auto completed=[self,job,phase,once](bool accepted){if(once->exchange(true)||!self){return;}if(!self->owner()){QMetaObject::invokeMethod(self,[self,job,phase]{if(self&&job->live&&job->phase==phase){self->finishQueryReply(job);}},Qt::QueuedConnection);return;}if(!job->live||job->phase!=phase){return;}if(!accepted||!self->normalCurrent()||self->state_!=State::Bound||Clock::now()>=job->end){++self->statistics_.authorityErrors;self->finishQueryReply(job);return;}self->queryReplyReady(job);};
 bool accepted=false;try{if(job->initialized&&job->nextKey<job->keys.size()&&localReadPreflight_){accepted=localReadPreflight_(job->keys[job->nextKey],std::move(completed));}else if(normalPreflight_){accepted=normalPreflight_(std::move(completed));}}catch(...){accepted=false;}
 if(self&&!accepted&&job->live&&job->phase==phase){++statistics_.serviceCapacityDrops;finishQueryReply(job);}
}
void OwnedPingTransport::queryReplyReady(const std::shared_ptr<QueryReply>& job){
 if(!job->live||state_!=State::Bound||Clock::now()>=job->end){finishQueryReply(job);return;}auto* db=activeDatabase();if(!db||!nodeCache_||(normalRouter_&&!normalCurrent())||(!allowed(job->peer)&&!ownedRoutingPeer(job->peer))){finishQueryReply(job);return;}
 const auto& incoming=job->request.header().fields();
 const auto header=[&](std::int32_t action){packet::udp::EncodeFields h;h.kind=packet::udp::Kind::Reply;h.action=action;h.network=config_.network;h.localVersion=config_.version;h.remoteVersion=incoming.originatorVersion;h.connection=incoming.connection;h.transaction=incoming.transaction;h.instance=config_.instance;h.flags=config_.flags;h.flags2=config_.flags2;h.now=QDateTime::currentMSecsSinceEpoch();h.receivedAt=job->receivedAt;return h;};
 const auto send=[&](const std::string& wire){if(wire.size()>static_cast<std::size_t>(maximumDatagram)||(normalRouter_&&!normalCurrent())){++statistics_.authorityErrors;return;}const auto sent=writePacket(wire,job->peer,job->end);if(!sent){return;}observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);if(*sent==static_cast<qint64>(wire.size())){++statistics_.sent;}else{++statistics_.ioErrors;}};
 if(!job->initialized){
  if(!incoming.originator||!sameIp(originator(job->peer),*incoming.originator)){
   remote_error::ReplyFields reply;reply.header=header(1032);reply.type=1;reply.originator=remote_error::Endpoint{originator(job->peer)};auto wire=remote_error::encodeReply(reply);if(wire){send(*wire);}++statistics_.serviceAddressRejected;finishQueryReply(job);return;
  }
  auto identity=nodeCache_->lookup(find::nodes::ResolvedEndpoint{*incoming.originator,{}},incoming.originatorVersion);if(!identity){++statistics_.authorityErrors;finishQueryReply(job);return;}find::nodes::Contact sender;sender.id=identity->id;sender.address=*incoming.originator;sender.protocol=incoming.originatorVersion;sender.instance=incoming.instance;sender.flags=incoming.flags;sender.skew=job->request.header().skew()->delta;job->sender=sender;
  if(normalRouter_&&(sender.address!=originator(job->peer)||(dynamicRoutingScope(config_.routingScope)?!learnRoutingContact(sender,true):!normalAlive(sender)))){++statistics_.authorityErrors;finishQueryReply(job);return;}
  auto matches=db->queryStoredKeys(job->prefixes);if(!matches){++statistics_.policyErrors;finishQueryReply(job);return;}job->matches=std::move(*matches);if(normalKeyBlocks_){for(auto& key:job->matches){if(key&&normalKeyBlocks_->blocked(*key)){key.reset();}}}for(const auto& key:job->matches){if(key){job->keys.push_back(*key);}}std::sort(job->keys.begin(),job->keys.end());job->keys.erase(std::unique(job->keys.begin(),job->keys.end()),job->keys.end());job->initialized=true;
 }else{++job->nextKey;}
 if(normalRouter_&&job->nextKey<job->keys.size()){
  QPointer<OwnedPingTransport> self(this);QMetaObject::invokeMethod(this,[self,job]{if(self&&job->live){self->queryReplyGate(job);}},Qt::QueuedConnection);return;
 }
 // A key preflight may withdraw local data. Never repeat an ordinary read or
 // return a newly appeared key on a permit obtained for an earlier mapping.
 auto current=db->queryStoredKeys(job->prefixes);if(current&&normalKeyBlocks_){for(auto& key:*current){if(key&&normalKeyBlocks_->blocked(*key)){key.reset();}}}if(!current||*current!=job->matches){finishQueryReply(job);return;}if(job->sender){observeSurveyQuery(*job->sender,*current);}query::ReplyFields reply;reply.header=header(1039);reply.body.entries=std::move(*current);for(auto& key:reply.body.entries){if(key){*key=key->substr(18,2);}}auto wire=query::encodeReply(reply);if(wire){send(*wire);}else{++statistics_.malformed;}finishQueryReply(job);
}
