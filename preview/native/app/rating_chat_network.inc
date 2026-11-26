// SPDX-License-Identifier: GPL-2.0-only
// Optional original azrating notices share normal Public chat rooms and actors.
void Owner::ratingNoticeStart(core::TorrentHash hash,std::optional<engine::SwarmMergeSearchTarget> mergeSearch,
    std::map<std::string,std::string> mergeMessages){
 const auto key=mergeSearch?"merge/"+hash.hex()+"/"+std::to_string(mergeSearch->fileSize)+"/"+mergeSearch->fileName:hash.hex();
 if(auto old=ratingNotices_.find(key);old!=ratingNotices_.end()){old->second->alive=false;ratingNotices_.erase(old);}
 if(ratingNoticeSequence_==UINT64_MAX){return;}if(ratingNotices_.size()>=64){auto oldest=ratingNotices_.end();for(auto at=ratingNotices_.begin();at!=ratingNotices_.end();++at){if(!at->second->view.running&&(oldest==ratingNotices_.end()||at->second->view.request<oldest->second->view.request)){oldest=at;}}if(oldest==ratingNotices_.end()){return;}ratingNotices_.erase(oldest);}
 auto notice=std::make_shared<RatingNotice>(hash);notice->mergeSearch=std::move(mergeSearch);notice->mergeMessages=std::move(mergeMessages);notice->view.hash=hash.hex();notice->view.request=++ratingNoticeSequence_;notice->view.running=true;notice->view.status="Waiting for Public chat";ratingNotices_.emplace(key,std::move(notice));changed();
}
void Owner::ratingNoticeCancel(core::TorrentHash hash){if(auto at=ratingNotices_.find(hash.hex());at!=ratingNotices_.end()){at->second->alive=false;ratingNoticeDone(at->second,"Rating chat cancelled");}}
std::chrono::milliseconds Owner::ratingNoticeDelay(const RatingNotice& notice,bool settling)const {
 if(notice.mergeSearch&&mergeNoticePeriod_.count()){return mergeNoticePeriod_;}
 using Wait=plugins::rating::publication::NoticeWait;
 return std::chrono::milliseconds(settling?Wait::settleMillis:Wait::pollMillis);
}
void Owner::ratingNoticeOpen(std::shared_ptr<RatingNotice> notice){
 if(!notice->alive||closing_){return;}notice->step=RatingNotice::Step::Opening;
 struct Prepared {std::optional<storage::RatingChatSnapshot> source;std::optional<storage::DownloadChatSnapshot> download;std::optional<ChatIdentity> identity;std::optional<std::string> history;};auto prepared=std::make_shared<Prepared>();
 const auto lease=shared_->lease;const auto downloadSources=shared_->downloadSources;std::vector<std::shared_ptr<storage::AzureusPublicPolicy>> policies;for(std::size_t i=0;i<2;++i){if(lanes_[i]&&lanes_[i]->ready){policies.push_back(lanes_[i]->policy);}}
 auto failed=[this,notice]{if(notice->alive){ratingNoticeDone(notice,"Automatic rating chat unavailable or source changed");}};
 if(!gate(Clock::now()+std::chrono::seconds(10),[this,notice,prepared]{
  if(!notice->alive||(!prepared->source&&!prepared->download)||!prepared->identity){return;}
  // Room crypto opens only after this source job has physically released.
  if(!later([this,notice,prepared]{
   if(!notice->alive||closing_){return;}notice->source=std::move(prepared->source);notice->download=std::move(prepared->download);
   notice->view.room=notice->download?notice->download->roomKey:notice->source->roomKey;const auto& key=notice->view.room;
   if(!rooms_.contains(key)&&rooms_.size()>=256){ratingNoticeDone(notice,"Chat room limit reached");return;}
   auto& room=rooms_[key];if(room.opening){notice->step=RatingNotice::Step::Queued;notice->next=Clock::now()+std::chrono::seconds(1);return;}auto identity=std::move(*prepared->identity);const bool live=room.core&&room.token&&room.token->active&&room.token->stamp==identity.sourceStamp;
   if(!live){auto opened=msg::Room::open(key,identity.uid,std::move(identity.signer),"de",identity.settings.readOnly);if(!opened){ratingNoticeDone(notice,"Download chat identity is unavailable");return;}if(room.token){room.token->active=false;}room.session.reset();room.core=std::make_shared<msg::Room>(std::move(*opened));room.token=std::make_shared<RoomToken>();if(epoch_==UINT64_MAX){ratingNoticeDone(notice,"Chat identity sequence exhausted");return;}room.token->epoch=++epoch_;room.token->key=key;room.token->configKey=ChatProfile::configKey(key);room.token->stamp=identity.sourceStamp;room.background=!identity.settings.favourite&&!identity.settings.saveHistory;if(prepared->history){static_cast<void>(room.core->importHistory(std::move(*prepared->history),QDateTime::currentMSecsSinceEpoch()));}}
   if(notice->download&&(!room.token->download||room.token->download->sourceRevision!=notice->download->sourceRevision)){
    if(epoch_==UINT64_MAX){ratingNoticeDone(notice,"Chat identity sequence exhausted");return;}
    auto token=std::make_shared<RoomToken>();token->epoch=++epoch_;token->key=key;
    token->configKey=ChatProfile::configKey(key);token->stamp=identity.sourceStamp;token->download=notice->download;
    room.token->active=false;room.session.reset();room.token=std::move(token);
    room.nextLookup={};room.nextPut={};room.mergeBackground=!room.download;room.download=true;
   }
   room.settings=std::move(identity.settings);room.backgroundUntil=Clock::now()+std::chrono::minutes(15);notice->token=room.token;rebind(room);observe(room,false);
   if(notice->mergeSearch||notice->source->written){notice->step=RatingNotice::Step::Sync;notice->next=Clock::now()+ratingNoticeDelay(*notice);notice->view.status="Waiting for chat synchronisation";}else{notice->step=RatingNotice::Step::Ready;}changed();
  })){ratingNoticeDone(notice,"Chat crypto queue is full");}
 },failed,[lease,notice,prepared,policies,downloadSources](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||!notice->alive){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Rating chat cancelled"));}bool permitted=false;for(const auto& policy:policies){permitted|=bool(policy->validate());}if(!permitted){return std::unexpected(failure(storage::ErrorCode::Unsupported,"Public chat is unavailable"));}
  if(notice->mergeSearch){
   if(!notice->mergeSearch->live->load()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Swarm Merge search expired"));}
   auto permission=automaticChatPermission(*lease);if(!permission){return std::unexpected(permission.error());}
   auto download=downloadSources->read(notice->hash);if(!download){return std::unexpected(download.error());}
   prepared->download=std::move(*download);
  }else{
   auto source=storage::readRatingChat(*lease,notice->hash);if(!source){return std::unexpected(source.error());}
   prepared->source=std::move(*source);
  }
  const auto& key=prepared->download?prepared->download->roomKey:prepared->source->roomKey;
  auto profile=ChatProfile::open(*lease);if(!profile){return std::unexpected(profile.error());}
  auto identity=(*profile)->identity(key,true);if(!identity){return std::unexpected(identity.error());}
  auto nick=(*profile)->setSharedNickname(key,false);if(!nick){return std::unexpected(nick.error());}
  identity=(*profile)->identity(key,false);if(!identity){return std::unexpected(identity.error());}
  if(prepared->source){
   auto fresh=storage::readRatingChat(*lease,notice->hash);if(!fresh){return std::unexpected(fresh.error());}
   if(fresh->sourceRevision!=prepared->source->sourceRevision){return std::unexpected(failure(storage::ErrorCode::Conflict,"Rating chat contribution changed"));}
  }
  if(identity->settings.saveHistory){auto history=(*profile)->history(key);if(history){prepared->history=std::move(*history);}}
  prepared->identity=std::move(*identity);return Broker::Data{};
 })){failed();}
}
bool Owner::ratingNoticeGate(std::shared_ptr<RatingNotice> notice,biglybtc::core::MoveOnlyFunction<void()> effect){
 const auto token=notice->token.lock();if(!notice->alive||(!notice->source&&!notice->download)||!token||!token->active||closing_){return false;}
 const auto source=notice->source;const auto download=notice->download;const auto lease=shared_->lease;const auto downloadSources=shared_->downloadSources;
 std::vector<std::shared_ptr<storage::AzureusPublicPolicy>> policies;for(std::size_t i=0;i<2;++i){if(lanes_[i]&&lanes_[i]->ready){policies.push_back(lanes_[i]->policy);}}
 auto failed=[this,notice]{if(notice->alive){ratingNoticeDone(notice,"Rating chat source changed or unavailable");}};
 return gate(Clock::now()+std::chrono::seconds(10),[notice,token,effect=std::move(effect),failed]()mutable{
  if(notice->alive&&token->active&&(!notice->mergeSearch||notice->mergeSearch->live->load())){effect();}else{failed();}
 },failed,[lease,notice,source,download,token,policies,downloadSources](std::stop_token stop)->Broker::Result{
  if(stop.stop_requested()||!token->active){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Rating chat cancelled"));}bool permitted=false;for(const auto& policy:policies){permitted|=bool(policy->validateChatIdentity(token->configKey,token->stamp));}if(!permitted){return std::unexpected(failure(storage::ErrorCode::Conflict,"Rating chat identity changed"));}
  if(download){
   if(!notice->mergeSearch->live->load()){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Swarm Merge search expired"));}
   auto current=automaticChatPermission(*lease);if(!current){return std::unexpected(current.error());}
   auto fresh=downloadSources->read(download->hash);
   if(!fresh||fresh->sourceRevision!=download->sourceRevision){return std::unexpected(failure(storage::ErrorCode::Conflict,"Download chat source changed"));}
  }else{
   auto fresh=storage::readRatingChat(*lease,source->rating.local.hash);if(!fresh){return std::unexpected(fresh.error());}
   if(fresh->sourceRevision!=source->sourceRevision){return std::unexpected(failure(storage::ErrorCode::Conflict,"Rating chat contribution changed"));}
  }
  return Broker::Data{};
 });
}
void Owner::ratingNoticeSend(std::shared_ptr<RatingNotice> notice){
 notice->step=RatingNotice::Step::Signing;if(!later([this,notice]{
  if(!notice->alive||(!notice->source&&!notice->download)||closing_){return;}const auto at=rooms_.find(notice->view.room);const auto token=notice->token.lock();if(at==rooms_.end()||at->second.token!=token||!token||!token->active||!at->second.session){ratingNoticeDone(notice,"Download chat is unavailable");return;}auto& room=at->second;
  auto history=room.core->messages(QDateTime::currentMSecsSinceEpoch());if(!history){ratingNoticeDone(notice,"Download chat history is unavailable");return;}
  if(notice->mergeSearch){mergeNoticeSend(notice,room,*history);return;}
  std::vector<std::u16string> texts;texts.reserve(history->size());for(const auto& row:*history){auto decoded=plugins::rating::decodeAttributeText(row.text);if(!decoded){ratingNoticeDone(notice,"Download chat message exceeds its bound");return;}texts.push_back(std::move(*decoded));}std::vector<plugins::rating::publication::NoticeMessage> rows;for(std::size_t i=0;i<history->size();++i){rows.push_back({texts[i],(*history)[i].local});}
  const auto& rating=notice->source->rating.local.local.value;const auto message=plugins::rating::publication::noticeMessage(rating.score,rating.comment.value_or(u""));if(!plugins::rating::publication::shouldSendNotice(notice->source->written,rows,message)){ratingNoticeDone(notice,"Rating notice already present or chat is busy");return;}auto text=plugins::rating::encodeAttributeText(message);if(!text){ratingNoticeDone(notice,"Rating notice exceeds its text bound");return;}auto prepared=room.session->prepareSend(std::move(*text),room.settings.nickname,QDateTime::currentMSecsSinceEpoch(),1);if(!prepared){ratingNoticeDone(notice,"Rating notice was refused");return;}auto pending=std::make_shared<std::optional<msg::PreparedSend>>(std::move(*prepared));notice->step=RatingNotice::Step::Committing;
  if(!ratingNoticeGate(notice,[this,notice,token,pending]{const auto found=rooms_.find(notice->view.room);if(found==rooms_.end()||found->second.token!=token||!found->second.session||!*pending){ratingNoticeDone(notice,"Download chat changed before commit");return;}auto committed=found->second.session->commitSend(std::move(**pending),QDateTime::currentMSecsSinceEpoch());pending->reset();if(!committed){if(++notice->retries<=3){notice->step=RatingNotice::Step::Ready;}else{ratingNoticeDone(notice,"Download chat changed during signing");}return;}notice->view.created=true;found->second.backgroundUntil=Clock::now()+std::chrono::minutes(15);if(notice->source->written){ratingNoticeDone(notice,"Rating notice created");}else{notice->step=RatingNotice::Step::Saving;if(!later([this,notice]{ratingNoticeSave(notice);})){ratingNoticeDone(notice,"Rating notice created; saved mark unavailable");}}changed();})){ratingNoticeDone(notice,"Rating chat commit unavailable");}
 })){ratingNoticeDone(notice,"Rating chat signing queue is full");}
}
void Owner::mergeNoticeSend(const std::shared_ptr<RatingNotice>& notice,Room& room,
    const std::vector<msg::RoomMessage>& history){
 if(!notice->mergeSearch->live->load()){ratingNoticeDone(notice,"Swarm Merge search expired");return;}
 std::erase_if(notice->mergeMessages,[&](const auto& candidate){
  return std::ranges::any_of(history,[&](const auto& row){return row.text.find(candidate.first)!=std::string::npos;});
 });
 const bool help=notice->mergeMessages.empty();
 if(help&&(!notice->view.created||std::ranges::any_of(history,[](const auto& row){
  return row.text.find("http://wiki.biglybt.com/w/Swarm_Merging")!=std::string::npos
      ||row.text.find("http://wiki.vuze.com/w/Swarm_Merging")!=std::string::npos;
 }))){ratingNoticeDone(notice,"Swarm Merge suggestions finished");return;}
 const auto message=help?"See http://wiki.biglybt.com/w/Swarm_Merging[[Swarm%20Merging]] for help":notice->mergeMessages.begin()->second;
 const auto id=help?std::string{}:notice->mergeMessages.begin()->first;
 const auto token=room.token;
 auto prepared=room.session->prepareSend(message,room.settings.nickname,QDateTime::currentMSecsSinceEpoch(),1,{},true);
 if(!prepared){ratingNoticeDone(notice,"Swarm Merge suggestion was refused");return;}
 auto pending=std::make_shared<std::optional<msg::PreparedSend>>(std::move(*prepared));
 notice->step=RatingNotice::Step::Committing;
 if(!ratingNoticeGate(notice,[this,notice,token,pending,id,help]{
  const auto current=rooms_.find(notice->view.room);
  if(current==rooms_.end()||current->second.token!=token||!current->second.session||!*pending){
   ratingNoticeDone(notice,"Download chat changed before commit");return;
  }
  const auto committed=current->second.session->commitSend(std::move(**pending),QDateTime::currentMSecsSinceEpoch());
  pending->reset();
  if(!committed){
   if(++notice->retries<=3){notice->step=RatingNotice::Step::Ready;}
   else{ratingNoticeDone(notice,"Download chat changed during signing");}
   return;
  }
  notice->retries=0;notice->view.created=true;
  current->second.backgroundUntil=Clock::now()+std::chrono::minutes(15);
  if(help){ratingNoticeDone(notice,"Swarm Merge suggestions created");}
  else{notice->mergeMessages.erase(id);notice->step=RatingNotice::Step::Ready;}
  changed();
 })){ratingNoticeDone(notice,"Swarm Merge chat commit unavailable");}
}
void Owner::ratingNoticeSave(std::shared_ptr<RatingNotice> notice){
 if(!notice->alive||!notice->source||closing_){return;}const auto lease=shared_->lease;const auto source=*notice->source;auto failed=[this,notice]{if(notice->alive){ratingNoticeDone(notice,"Rating notice created; saved mark changed");}};
 if(!gate(Clock::now()+std::chrono::seconds(10),[this,notice]{if(notice->alive){ratingNoticeDone(notice,"Rating notice created");}},failed,[lease,notice,source](std::stop_token stop)->Broker::Result{if(stop.stop_requested()||!notice->alive){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Rating chat cancelled"));}auto saved=storage::saveRatingChatWritten(*lease,notice->hash,source.sourceRevision);if(!saved){return std::unexpected(saved.error());}return Broker::Data{};})){failed();}
}
void Owner::ratingNoticeTick(){
 const auto now=Clock::now();for(auto at=rooms_.begin();at!=rooms_.end();){auto& room=at->second;const bool busy=std::ranges::any_of(ratingNotices_,[&](const auto& row){return row.second->alive&&row.second->view.running&&row.second->view.room==at->first;});if(room.background&&(!room.download||room.mergeBackground)&&!room.privateRetiring&&!busy&&!room.historyPending&&room.viewReferences.views().empty()&&room.backgroundUntil<=now){if(room.token){room.token->active=false;}if(room.session){room.session->close();}at=rooms_.erase(at);changed();}else{++at;}}
 unsigned background=0,open=0;for(const auto& [key,room]:rooms_){(void)key;if(room.core&&room.token&&room.token->active){++open;background+=room.background;}open+=room.opening;}
 bool opening=std::ranges::any_of(ratingNotices_,[](const auto& entry){return entry.second->alive&&entry.second->step==RatingNotice::Step::Opening;});const bool ready=std::ranges::any_of(lanes_,[](const auto& lane){return lane&&lane->ready;});
 for(const auto& [key,notice]:ratingNotices_){(void)key;if(!notice->alive||!notice->view.running||notice->next>now){continue;}if(notice->mergeSearch&&!notice->mergeSearch->live->load()){ratingNoticeDone(notice,"Swarm Merge search expired");continue;}switch(notice->step){case RatingNotice::Step::Queued:if(ready&&!opening&&open<16&&background<4){opening=true;ratingNoticeOpen(notice);}break;case RatingNotice::Step::Sync:case RatingNotice::Step::Delay:{const auto room=rooms_.find(notice->view.room);const auto token=notice->token.lock();const bool destroyed=room==rooms_.end()||!token||!token->active||room->second.token!=token;if(destroyed){ratingNoticeDone(notice,"Download chat was closed");break;}if(notice->step==RatingNotice::Step::Sync){const auto state=notice->wait.poll(room->second.session?room->second.session->facts().incomingSync:-3,false);notice->next=now+ratingNoticeDelay(*notice,state==plugins::rating::publication::NoticeWait::Stage::Delay);if(state==plugins::rating::publication::NoticeWait::Stage::Delay){notice->step=RatingNotice::Step::Delay;}}else{notice->wait.afterDelay(false);notice->step=RatingNotice::Step::Ready;}break;}case RatingNotice::Step::Ready:ratingNoticeSend(notice);break;default:break;}}
}
