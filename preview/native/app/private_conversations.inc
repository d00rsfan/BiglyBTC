// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the actual Public actor. No new sockets, profile identities,
// public child lookups, child history files or private-key serialisation.
void Owner::privateFinished(const std::shared_ptr<PrivateOutgoing>& work,std::optional<storage::Error> error,std::string key){
 if(work->finished){return;}work->finished=true;if(work->ticket&&lanes_[work->lane]&&lanes_[work->lane]->actor){lanes_[work->lane]->actor->cancelTransfer(*work->ticket);}work->ticket.reset();work->exchange.reset();result(work->command,std::move(error),std::move(key));changed();
}
void Owner::privateOpen(Command command){
 const auto it=rooms_.find(command.key);
 if(!command.privateSelection||it==rooms_.end()||!it->second.core||it->second.core->isPrivate()||!it->second.token||!it->second.token->active||it->second.token->epoch!=command.privateSelection->epoch||!it->second.participantKeys.contains(command.privateSelection->publicKey)||it->second.core->publicKey()==command.privateSelection->publicKey||privateOutgoing_.size()>=16){result(command,failure(storage::ErrorCode::Conflict,"Private chat participant or parent room changed"));return;}
 const auto token=it->second.token;
 if(!later([this,command,token]()mutable{
  const auto it=rooms_.find(token->key);if(it==rooms_.end()||it->second.token!=token||!token->active||closing_){result(command,failure(storage::ErrorCode::Cancelled,"Parent room closed"));return;}auto& room=it->second;
  const auto hint=room.core->participantContact(command.privateSelection->publicKey);if(!hint){result(command,failure(storage::ErrorCode::Conflict,"Participant has no signed parent message"));return;}
  auto target=msg::RoomSession::contactHint(0,*hint);if(!target||(owned_?!target->endpoint.address.isLoopback():!publicAddress(target->endpoint))){result(command,failure(storage::ErrorCode::Unsupported,"Participant contact is unavailable on Public"));return;}
  const auto lane=target->endpoint.address.protocol()==QAbstractSocket::IPv4Protocol?0U:1U;if(!lanes_[lane]||!lanes_[lane]->ready||!lanes_[lane]->actor){result(command,failure(storage::ErrorCode::Unsupported,"Participant address family is unavailable"));return;}
  auto exchange=room.core->initiatePrivate(command.privateSelection->publicKey);if(!exchange){result(command,failure(storage::ErrorCode::Unsupported,"Private chat authentication could not start"));return;}
  auto first=exchange->first();if(!first){result(command,failure(storage::ErrorCode::Unsupported,"Private chat keys could not be prepared"));return;}
  auto work=std::make_shared<PrivateOutgoing>();work->command=std::move(command);work->parent=token;work->peer=work->command.privateSelection->publicKey;work->lane=lane;target->lane=lane;work->target=std::move(*target);work->exchange.emplace(std::move(*exchange));work->end=Clock::now()+std::chrono::seconds(65);privateOutgoing_[work->command.id]=work;privateCall(work,std::move(*first));
 })){result(command,failure(storage::ErrorCode::Busy,"Private chat worker is busy"));}
}
void Owner::privateCall(const std::shared_ptr<PrivateOutgoing>& work,msg::PrivateRequest request){
 if(work->finished||closing_){return;}const auto parent=rooms_.find(work->parent->key);if(parent==rooms_.end()||parent->second.token!=work->parent||!work->parent->active){privateFinished(work,failure(storage::ErrorCode::Cancelled,"Parent chat changed"));return;}
 auto envelope=parent->second.core->privateRequestEnvelope(request);if(!envelope){privateFinished(work,failure(storage::ErrorCode::Unsupported,"Private handshake could not be encrypted"));return;}
 const auto handler=std::string(parent->second.core->handler());const auto end=std::min(work->end,Clock::now()+std::chrono::seconds(30));
 if(!gate(end,[this,work,handler,envelope=std::move(*envelope),end]()mutable{
  if(work->finished||!work->parent->active||!allowed(work->lane)||!active_->operation->data.keys.contains(work->parent->epoch)){privateFinished(work,failure(storage::ErrorCode::Conflict,"Private handshake authority changed"));return;}
  auto& actor=*lanes_[work->lane]->actor;if(!actor.allowPeer(work->target.endpoint)){privateFinished(work,failure(storage::ErrorCode::Unsupported,"Private participant endpoint was refused"));return;}
  auto ticket=actor.callTransfer(work->target.endpoint,work->target.version,handler,std::move(envelope),this,[this,work](auto outcome){work->ticket.reset();if(work->finished){return;}if(!later([this,work,outcome=std::move(outcome)]()mutable{privateCompleted(work,std::move(outcome));})){privateFinished(work,failure(storage::ErrorCode::Busy,"Private handshake worker is busy"));}},end);
  if(ticket){work->ticket=*ticket;}else{privateFinished(work,failure(storage::ErrorCode::Busy,"Private handshake transport is busy"));}
 },[this,work]{privateFinished(work,failure(storage::ErrorCode::Conflict,"Private handshake source expired"));})){privateFinished(work,failure(storage::ErrorCode::Busy,"Private handshake source is busy"));}
}
void Owner::privateCompleted(const std::shared_ptr<PrivateOutgoing>& work,udp::TransferOutcome outcome){
 if(work->finished||closing_){return;}const auto parent=rooms_.find(work->parent->key);if(!outcome||parent==rooms_.end()||parent->second.token!=work->parent||!work->parent->active||!work->exchange){privateFinished(work,failure(storage::ErrorCode::Cancelled,"Private participant did not respond or parent changed"));return;}
 auto reply=parent->second.core->privateResponse(*outcome);if(!reply||reply->error){privateFinished(work,storage::Error{storage::ErrorCode::Unsupported,reply&&reply->error?*reply->error:"Invalid private handshake response"});return;}
 if(reply->keys){auto request=work->exchange->keysReply(*reply);if(!request){privateFinished(work,failure(storage::ErrorCode::Conflict,"Private participant key or authentication differs"));return;}privateCall(work,std::move(*request));return;}
 auto channel=work->exchange->finish(*reply);if(!channel){privateFinished(work,failure(storage::ErrorCode::Conflict,"Private participant authentication failed"));return;}auto core=parent->second.core->privateChild(std::move(*channel));if(!core){privateFinished(work,failure(storage::ErrorCode::Conflict,"Signed parent participant is no longer available"));return;}auto prepared=std::make_shared<msg::Room>(std::move(*core));
 const auto nickname=privateNickname(work->parent,work->peer);
 if(!gate(work->end,[this,work,prepared,nickname]{if(work->finished||!work->parent->active||!allowed(work->lane)||!active_->operation->data.keys.contains(work->parent->epoch)){privateFinished(work,failure(storage::ErrorCode::Conflict,"Private conversation source changed"));return;}auto key=privateInstall(work->parent,prepared,work->target,work->peer,false,nickname);if(!key){privateFinished(work,failure(storage::ErrorCode::Busy,"Private conversation could not be attached"));return;}selected_=*key;privateFinished(work,{},std::move(*key));},[this,work]{privateFinished(work,failure(storage::ErrorCode::Conflict,"Private conversation source expired"));})){privateFinished(work,failure(storage::ErrorCode::Busy,"Private conversation source is busy"));}
}
std::string Owner::privateNickname(const std::shared_ptr<RoomToken>& parent,std::string_view remote){
 const auto it=rooms_.find(parent->key);std::string nickname;if(it!=rooms_.end()&&it->second.token==parent&&it->second.core){auto messages=it->second.core->messages(QDateTime::currentMSecsSinceEpoch());if(messages){for(const auto& m:*messages){if(m.publicKey==remote){nickname=m.nickname;}}}}if(nickname.empty()){auto name=msg::participantShortName(remote);nickname=name?*name:"Participant";}return nickname;
}
std::optional<std::string> Owner::privateInstall(const std::shared_ptr<RoomToken>& parent,std::shared_ptr<msg::Room> core,msg::RoomPeer peer,std::string_view remote,bool incoming,std::string nickname){
 const auto it=rooms_.find(parent->key);if(it==rooms_.end()||it->second.token!=parent||!parent->active||!it->second.core||!it->second.participantKeys.contains(std::string(remote))||rooms_.size()>=512||privateSequence_==std::numeric_limits<std::uint64_t>::max()){return {};}

 const auto key=parent->key+" - "+nickname+(incoming?" (incoming)[":" (outgoing)[")+std::to_string(privateSequence_++)+"]";if(key.size()>4096||rooms_.contains(key)){return {};}
 Room room;room.settings=it->second.settings;room.settings.key=key;room.settings.privateChat=true;room.settings.favourite=false;room.settings.saveHistory=false;room.settings.readOnly=false;room.settings.disableIndicators=false;room.settings.lastMessageInfo.reset();room.core=std::move(core);peer.uid={};room.privatePeer=std::move(peer);room.privateParentKey=parent->key;room.token=std::make_shared<RoomToken>();room.token->epoch=++epoch_;room.token->key=key;room.token->configKey=parent->configKey;room.token->stamp=parent->stamp;auto [added,inserted]=rooms_.emplace(key,std::move(room));if(!inserted){return {};}
 rebind(added->second);if(!added->second.session){added->second.token->active=false;rooms_.erase(added);return {};}added->second.status="Private conversation connected";observe(added->second,false);changed();return key;
}
void Owner::privateAnswer(std::shared_ptr<RoomToken> token,std::size_t lane,std::shared_ptr<const udp::TransferRequest> request,msg::PrivateReply reply){
 const auto parent=rooms_.find(token->key);if(parent==rooms_.end()||parent->second.token!=token||!token->active||closing_){return;}auto envelope=parent->second.core->privateReplyEnvelope(reply);if(!envelope){return;}
 gate(request->deadline,[this,token,lane,request,envelope=std::move(*envelope)]()mutable{if(token->active&&allowed(lane)&&active_->operation->data.keys.contains(token->epoch)){static_cast<void>(lanes_[lane]->actor->answerTransfer(request->receipt,std::move(envelope)));}});
}
void Owner::privateIncoming(std::shared_ptr<RoomToken> token,std::size_t lane,std::shared_ptr<const udp::TransferRequest> request,msg::PrivateRequest value){
 const auto parent=rooms_.find(token->key);if(parent==rooms_.end()||parent->second.token!=token||!token->active||parent->second.core->isPrivate()||closing_){return;}
 const auto reject=[&](std::string text){privateAnswer(token,lane,request,{{},{},std::move(text)});};
 if(token->key.starts_with("Statistics:")){reject("Private chat disabled for statistical channels");return;}
 const auto address=request->peer.address.toString();if(!privateRate_.contains(address)&&privateRate_.size()>=64){reject("Connection refused - address overloaded");return;}auto& rate=privateRate_[address];if(Clock::now()-rate.first>=std::chrono::seconds(60)){rate={Clock::now(),0};}if(++rate.second>8){reject("Connection refused - address overloaded");return;}
 const auto id=std::pair{token->epoch,value.activity};auto found=privateIncoming_.find(id);
 if(value.phase==msg::PrivatePhase::Keys){
  if(found!=privateIncoming_.end()){const auto& prior=found->second;if(prior.lane==lane&&prior.peer==request->peer&&prior.user==value.userKey&&prior.first==value.packet){privateAnswer(token,lane,request,prior.reply);}else{reject("Connection identity changed");}return;}
  if(privateIncoming_.size()>=17){reject("Connection refused - peer overloaded");return;}auto exchange=parent->second.core->respondPrivate(value);if(!exchange){reject("Private authentication failed");return;}auto reply=exchange->first();if(!reply){reject("Private keys unavailable");return;}PrivateIncoming incoming{token,lane,request->peer,value.userKey,value.packet,{},*reply,Clock::now()+std::chrono::seconds(60)};incoming.exchange.emplace(std::move(*exchange));privateIncoming_.emplace(id,std::move(incoming));privateAnswer(token,lane,request,std::move(*reply));return;
 }
 if(found==privateIncoming_.end()){reject("Connection expired");return;}auto incoming=std::move(found->second);privateIncoming_.erase(found);if(incoming.lane!=lane||incoming.peer!=request->peer||incoming.user!=value.userKey||Clock::now()>=incoming.end||!incoming.exchange){reject("Connection identity changed");return;}
 auto channel=incoming.exchange->finish(value);if(!channel){reject("Private authentication failed");return;}const std::string remote(channel->peerKey());if(!parent->second.participantKeys.contains(remote)||!parent->second.core->participantContact(remote)){reject("Private chat requires you send at least one message to the main chat first");return;}
 auto child=parent->second.core->privateChild(std::move(*channel));if(!child){reject("Private identity unavailable");return;}auto prepared=std::make_shared<msg::Room>(std::move(*child));auto success=parent->second.core->privateReplyEnvelope({});if(!success){return;}std::array<std::string,3> failures;const std::array<std::string,3> reasons{"Private conversation could not be accepted","Private chat disabled by recipient","Recipient will only accept private chats from pinned participants"};for(std::size_t i=0;i<reasons.size();++i){auto encrypted=parent->second.core->privateReplyEnvelope({{},{},reasons[i]});if(!encrypted){return;}failures[i]=std::move(*encrypted);}const auto nickname=privateNickname(token,remote);
 gate(request->deadline,[this,token,lane,request,prepared,remote,nickname,reasons,success=std::move(*success),failures=std::move(failures)]()mutable{
  if(!token->active||!allowed(lane)||!active_->operation->data.keys.contains(token->epoch)||!active_->operation->data.privateChecked){return;}
  bool accepted=!active_->operation->data.privateRefusal;std::optional<std::string> key;if(accepted){auto contact=msg::RoomSession::contactMap(request->peer);if(contact&&lanes_[lane]->actor->allowPeer(request->peer)){key=privateInstall(token,prepared,msg::RoomPeer{lane,request->peer,55,{},std::move(*contact)},remote,true,nickname);}accepted=key.has_value();}
  std::size_t error=0;if(active_->operation->data.privateRefusal){for(std::size_t i=1;i<reasons.size();++i){if(*active_->operation->data.privateRefusal==reasons[i]){error=i;}}}
  static_cast<void>(lanes_[lane]->actor->answerTransfer(request->receipt,accepted?std::move(success):std::move(failures[error])));changed();
 },{}, {},remote);
}
void Owner::privateTick(){
 std::erase_if(privateIncoming_,[](const auto& entry){return Clock::now()>=entry.second.end||!entry.second.parent->active;});
 std::erase_if(privateRate_,[](const auto& entry){return Clock::now()-entry.second.first>=std::chrono::seconds(60);});
 for(const auto& [id,work]:privateOutgoing_){(void)id;if(!work->finished&&(closing_||!work->parent->active||Clock::now()>=work->end)){privateFinished(work,failure(storage::ErrorCode::Cancelled,"Private conversation expired or parent closed"));}}
 std::erase_if(privateOutgoing_,[](const auto& entry){return entry.second->finished;});
}
void Owner::privateStop(){for(const auto& [id,work]:privateOutgoing_){(void)id;privateFinished(work,failure(storage::ErrorCode::Cancelled,"Chats are closing"));}privateOutgoing_.clear();privateIncoming_.clear();privateRate_.clear();}
