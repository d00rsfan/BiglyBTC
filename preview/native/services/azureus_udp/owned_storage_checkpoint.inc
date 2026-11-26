// SPDX-License-Identifier: GPL-2.0-or-later
// Included by the sole serialized UDP actor; no filesystem work occurs here.
std::expected<void,Failure> OwnedPingTransport::setStorageCheckpoint(std::function<bool(std::uint64_t,biglybtc::core::MoveOnlyFunction<void(bool)>)> checkpoint,std::chrono::milliseconds wait){
 if(!owner()){return std::unexpected(Failure::Thread);}if(state_!=State::Bound||!normalDatabase_||!normalRouter_||!checkpoint||storageCheckpoint_||wait<=std::chrono::milliseconds::zero()||wait>std::chrono::seconds(5)){return std::unexpected(Failure::State);}if(!normalDatabase_->localDiversificationRevision()){return std::unexpected(Failure::State);}storageReplyWait_=wait;storageCheckpoint_=std::move(checkpoint);return {};
}
bool OwnedPingTransport::queueStorageReply(Peer peer,std::optional<std::string> key,std::optional<std::uint64_t> content,std::size_t bytes,biglybtc::core::MoveOnlyFunction<void()> ready){
 (void)peer;constexpr std::size_t limit=2*1024*1024;
 if(!owner()||state_!=State::Bound||!storageCheckpoint_||!normalDatabase_||!ready||storageReplies_.size()>=32||bytes>limit||storageReplyBytes_>limit-bytes){++statistics_.serviceCapacityDrops;return false;}auto revision=normalDatabase_->localDiversificationRevision();if(!revision){++statistics_.policyErrors;return false;}
 try{auto pending=std::make_shared<StorageReply>();pending->policy=*revision;pending->key=std::move(key);pending->content=content;pending->bytes=bytes;pending->end=Clock::now()+storageReplyWait_;pending->ready=std::move(ready);storageReplies_.push_back(pending);storageReplyBytes_+=bytes;if(!storageReplyDeadline_.isActive()){storageReplyDeadline_.start();}QPointer<OwnedPingTransport> self(this);
  bool accepted=false;try{accepted=storageCheckpoint_(*revision,[self,pending](bool saved){if(pending->checkpointCallback.exchange(true)){return;}if(!self){return;}if(!self->owner()){QMetaObject::invokeMethod(self,[self,pending]{if(self){self->finishStorageReply(pending,false);}},Qt::QueuedConnection);return;}self->resumeStorageReply(pending,saved);});}catch(...){accepted=false;}
  if(!self){return false;}pending->registering=false;if(!accepted&&pending->live){finishStorageReply(pending,false);}return accepted;
 }catch(...){++statistics_.serviceCapacityDrops;return false;}
}
void OwnedPingTransport::resumeStorageReply(const std::shared_ptr<StorageReply>& pending,bool saved){
 if(!pending->live){return;}if(!saved||state_!=State::Bound||Clock::now()>=pending->end){finishStorageReply(pending,false);return;}
 if(pending->key&&!pending->registering&&localReadPreflight_){if(pending->readPending){return;}pending->readPending=true;QPointer<OwnedPingTransport> self(this);bool accepted=false;
  try{accepted=localReadPreflight_(*pending->key,[self,pending](bool permitted){if(pending->readCallback.exchange(true)){return;}if(!self){return;}if(!self->owner()){QMetaObject::invokeMethod(self,[self,pending]{if(self){self->finishStorageReply(pending,false);}},Qt::QueuedConnection);return;}self->finishStorageReply(pending,permitted);});}catch(...){accepted=false;}if(self&&!accepted&&pending->live){finishStorageReply(pending,false);}return;
 }finishStorageReply(pending,true);
}
void OwnedPingTransport::finishStorageReply(const std::shared_ptr<StorageReply>& pending,bool permitted){
 if(!owner()||!pending->live){return;}bool valid=permitted&&state_==State::Bound&&Clock::now()<pending->end&&normalDatabase_&&normalCurrent();
 if(valid){auto revision=normalDatabase_->localDiversificationRevision();valid=revision&&*revision==pending->policy;}
 if(valid&&pending->key){auto revision=normalDatabase_->readRevision(*pending->key);valid=revision&&*revision==pending->content;}
 auto ready=valid?std::move(pending->ready):biglybtc::core::MoveOnlyFunction<void()>{};retireStorageReply(pending);if(!valid){++statistics_.policyErrors;return;}try{ready();}catch(...){++statistics_.serviceCapacityDrops;}
}
void OwnedPingTransport::retireStorageReply(const std::shared_ptr<StorageReply>& pending){
 if(!pending->live){return;}pending->live=false;storageReplyBytes_-=pending->bytes;pending->bytes=0;pending->ready={};pending->key.reset();std::erase(storageReplies_,pending);if(storageReplies_.empty()){storageReplyDeadline_.stop();}
}
void OwnedPingTransport::expireStorageReplies(){auto pending=storageReplies_;for(const auto& reply:pending){if(Clock::now()>=reply->end){finishStorageReply(reply,false);}}}
void OwnedPingTransport::cancelStorageReplies(){storageReplyDeadline_.stop();auto pending=storageReplies_;for(const auto& reply:pending){retireStorageReply(reply);}}
void OwnedPingTransport::sendCheckpointStore(ServiceReceipt& receipt,std::int64_t receivedAt){
 if(!receipt.admission||!*receipt.admission){++statistics_.policyErrors;return;}const auto incoming=std::visit([](const auto& decoded){return decoded.header().fields();},receipt.request);const auto header=[&](std::int32_t action){packet::udp::EncodeFields h;h.kind=packet::udp::Kind::Reply;h.action=action;h.network=config_.network;h.localVersion=config_.version;h.remoteVersion=incoming.originatorVersion;h.connection=incoming.connection;h.transaction=incoming.transaction;h.instance=config_.instance;h.flags=config_.flags;h.flags2=config_.flags2;h.now=QDateTime::currentMSecsSinceEpoch();h.receivedAt=receivedAt;return h;};
 const auto& decision=**receipt.admission;std::expected<std::string,Failure> wire=std::unexpected(Failure::Encode);
 if(decision.blocked()&&incoming.originatorVersion>=14){remote_error::ReplyFields reply;reply.header=header(1032);receipt.replyAction=1032;reply.type=2;if(decision.blockRequest){reply.request=*decision.blockRequest;}if(decision.blockSignature){reply.signature=*decision.blockSignature;}auto encoded=remote_error::encodeReply(reply);if(encoded){wire=std::move(*encoded);}}
 else{const auto& request=std::get<storage::RequestBody>(std::get<storage::Decoded>(receipt.request).body());storage::ReplyFields reply;reply.header=header(1027);receipt.replyAction=1027;std::optional<std::string> diversification;if(decision.blocked()){diversification=std::string(request.keys.size(),'\0');}else if(decision.diversification){diversification.emplace();for(auto type:*decision.diversification){diversification->push_back(char(std::bit_cast<std::uint8_t>(type)));}}if(diversification){reply.diversification=*diversification;}auto encoded=storage::encodeReply(reply);if(encoded){wire=std::move(*encoded);}}
 sendServiceReply(receipt,std::move(wire));
}
