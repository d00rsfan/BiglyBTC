// SPDX-License-Identifier: GPL-2.0-or-later
// Included by the UI-thread controller. Only the source owner grants a view.
void GeneralChatController::clearViews(){
 auto old=service_;auto views=std::move(views_);views_.clear();viewJobs_.clear();viewSends_.clear();viewProjections_.clear();viewPreferences_.reset();viewsDirty_=false;
 if(old){for(const auto& [id,v]:views){(void)id;if(!v.acquired){static_cast<void>(old->acquireView(v.generation,v.key.toUtf8().toStdString(),v.epoch,v.id));}static_cast<void>(old->releaseView(v.generation,v.key.toUtf8().toStdString(),v.epoch,v.id));}}
 const QPointer<GeneralChatController> self(this);for(const auto& [id,v]:views){(void)v;emit viewClosed(id,{});if(!self){return;}}
}
void GeneralChatController::watchView(quint64 generation,QString key,quint64 epoch,quint64 id){
 const auto value=service_?service_->snapshot():nullptr;const auto found=value?std::ranges::find(value->rooms,key.toUtf8().toStdString(),[](const auto& r)->const std::string&{return r.settings.key;}):std::vector<GeneralChatRoom>::const_iterator{};
 if(!value||value->closed||value->generation!=generation||!id||!epoch||found==value->rooms.end()||found->epoch!=epoch){emit viewClosed(id,tr("Chat window source is unavailable"));return;}
 if(auto prior=views_.find(id);prior!=views_.end()){if(prior->second.generation!=generation||prior->second.epoch!=epoch||prior->second.key!=key){emit viewClosed(id,tr("Chat window identity changed"));}return;}
 if(views_.size()>=128){emit viewClosed(id,tr("Too many chat windows"));return;}
 ViewBinding view;view.generation=generation;view.epoch=epoch;view.id=id;view.key=std::move(key);view.privateChat=found->settings.privateChat;views_.emplace(id,std::move(view));viewsDirty_=true;retryViews();
}
void GeneralChatController::releaseView(quint64 generation,quint64 id){auto found=views_.find(id);if(found==views_.end()||found->second.generation!=generation){return;}found->second.closing=true;viewsDirty_=true;retryViews();}
void GeneralChatController::retryViews(){
 if(!service_){return;}unsigned count=0;for(auto& [id,v]:views_){if(count>=8){break;}if(v.request||(!v.closing&&v.acquired)){continue;}auto request=v.acquired?service_->releaseView(v.generation,v.key.toUtf8().toStdString(),v.epoch,id):service_->acquireView(v.generation,v.key.toUtf8().toStdString(),v.epoch,id);if(request){v.request=*request;viewJobs_[*request]={id,v.acquired};++count;}}
}
bool GeneralChatController::viewResult(const GeneralChatResult& result){
 auto job=viewJobs_.find(result.request);if(job==viewJobs_.end()){return false;}const auto [id,releasing]=job->second;viewJobs_.erase(job);auto found=views_.find(id);if(found==views_.end()){return true;}found->second.request.reset();viewsDirty_=true;
 if(releasing||result.failure){const bool visible=!found->second.closing;views_.erase(found);if(visible){emit viewClosed(id,result.failure?q(result.failure->message):QString{});}return true;}found->second.acquired=true;return true;
}
void GeneralChatController::keepAlive(quint64 generation,QString key,quint64 epoch,bool expected,bool next){if(service_&&!service_->keepAlive(generation,key.toUtf8().toStdString(),epoch,expected,next)){emit notice(tr("Chat keep-alive request could not be queued"));}}
void GeneralChatController::windowSettings(services::msgsync::ChatWindowSettings expected,services::msgsync::ChatWindowSettings value){if(service_&&!service_->windowSettings(expected,value)){emit notice(tr("Chat window preferences could not be queued"));}}
void GeneralChatController::readingRoomScoped(quint64 generation,QString key,quint64 epoch){
 const auto value=service_?service_->snapshot():nullptr;if(!value||value->generation!=generation){return;}if(!key.isEmpty()){bool match=false;for(const auto& r:value->rooms){match|=q(r.settings.key)==key&&r.epoch==epoch&&r.joined;}for(const auto& r:value->downloadRooms){match|=q(r.room.settings.key)==key&&r.room.epoch==epoch&&r.room.joined;}if(!match){return;}}else{epoch=0;}
 if(readingRoom_==key&&readingEpoch_==epoch){return;}readingRoom_=std::move(key);readingEpoch_=epoch;static_cast<void>(service_->setReadingRoom(readingRoom_.toUtf8().toStdString(),epoch));
}
void GeneralChatController::publishViews(const std::shared_ptr<const GeneralChatSnapshot>& value){
 if(!service_){return;}const auto service=service_;const QPointer<GeneralChatController> self(this);viewsDirty_=false;if(!viewPreferences_||*viewPreferences_!=value->preferences){viewPreferences_=value->preferences;++viewPolicy_;}std::vector<quint64> ids;for(const auto& [id,v]:views_){if(!v.closing){ids.push_back(id);}}
 for(auto id:ids){auto found=views_.find(id);if(found==views_.end()||found->second.closing){continue;}auto& v=found->second;const auto room=std::ranges::find(value->rooms,v.key.toUtf8().toStdString(),[](const auto& r)->const std::string&{return r.settings.key;});
  if(v.generation!=value->generation||room==value->rooms.end()||room->epoch!=v.epoch||room->settings.privateChat!=v.privateChat||!room->joined){views_.erase(found);emit viewClosed(id,tr("Chat window source changed"));if(!self||service_!=service){return;}continue;}
  ui::ChatViewSnapshot out;out.generation=value->generation;out.sequence=++v.sequence;out.selected=v.key;out.availability=q(value->availability);out.preferencesReady=value->preferencesReady;out.hideRatings=value->preferences.display.hideRatings;out.hideSubscriptions=value->preferences.display.hideSearchSubscriptions;out.notificationSettings=value->preferences.notifications;out.privateChatState=value->preferences.privateChatState;out.windowSettings=value->preferences.windows;out.rooms.push_back(viewRoom(*room));
  const auto data=std::ranges::find_if(value->transcripts,[&](const auto& t){return q(t.key)==v.key&&t.epoch==v.epoch&&std::ranges::find(t.views,id)!=t.views.end();});
  if(v.acquired&&data!=value->transcripts.end()&&data->messages){out.boundView=id;auto& cached=viewProjections_[{v.key,v.epoch}];if(cached.source!=data->messages||cached.policy!=viewPolicy_){ui::ChatViewSnapshot projected;projectChatMessages(projected,*data->messages,cached.times,QDateTime::currentDateTimeUtc(),value->preferences,true);cached.messages=std::move(projected.messages);cached.participants=std::move(projected.participants);cached.source=data->messages;cached.policy=viewPolicy_;}out.messages=cached.messages;out.participants=cached.participants;}
  else{out.rooms[0].joined=false;out.rooms[0].canSend=false;out.rooms[0].status=tr("Opening chat window…");}emit viewSnapshotReady(id,std::move(out));if(!self||service_!=service){return;}
 }
 std::erase_if(viewProjections_,[this](const auto& p){return std::ranges::none_of(views_,[&](const auto& v){return !v.second.closing&&v.second.key==p.first.first&&v.second.epoch==p.first.second;});});
}

std::optional<GeneralChatController::ViewBinding> GeneralChatController::currentView(quint64 generation,quint64 id,bool sending)const{
 const auto found=views_.find(id);if(found==views_.end()||found->second.generation!=generation||found->second.closing||!found->second.acquired||!service_){return {};}const auto value=service_->snapshot();if(!value||value->generation!=generation){return {};}const auto& view=found->second;const auto room=std::ranges::find(value->rooms,view.key.toUtf8().toStdString(),[](const auto& r)->const std::string&{return r.settings.key;});if(room==value->rooms.end()||room->epoch!=view.epoch||room->settings.privateChat!=view.privateChat||!room->joined||(sending&&!room->canSend)){return {};}return view;
}
void GeneralChatController::sendView(quint64 generation,quint64 id,QString text){
 auto view=currentView(generation,id,true);if(!view){emit viewSendFinished(id,{},text,false);return;}auto request=service_->send(view->key.toUtf8().toStdString(),text.toUtf8().toStdString(),ChatViewTarget{generation,view->epoch,id});if(request){viewSends_[*request]=id;}else{emit viewSendFinished(id,view->key,text,false);}
}
bool GeneralChatController::viewSendResult(const GeneralChatResult& result){auto found=viewSends_.find(result.request);if(found==viewSends_.end()){return false;}const auto id=found->second;viewSends_.erase(found);emit viewSendFinished(id,q(result.key),q(result.text),!result.failure);return true;}
void GeneralChatController::nicknameView(quint64 generation,quint64 id,QString nickname,bool expected){if(auto v=currentView(generation,id)){if(!service_->nickname(v->key.toUtf8().toStdString(),nickname.toUtf8().toStdString(),expected,ChatViewTarget{generation,v->epoch,id})){emit notice(tr("Chat nickname action could not be queued"));}}}
void GeneralChatController::nicknameModeView(quint64 generation,quint64 id,bool shared){if(auto v=currentView(generation,id)){if(!service_->nicknameMode(v->key.toUtf8().toStdString(),shared,ChatViewTarget{generation,v->epoch,id})){emit notice(tr("Chat nickname mode could not be queued"));}}}
void GeneralChatController::displayPreferenceView(quint64 generation,quint64 id,bool ratings,bool expected,bool value){if(auto v=currentView(generation,id)){if(!service_->displayPreference(v->key.toUtf8().toStdString(),ratings,expected,value,ChatViewTarget{generation,v->epoch,id})){emit notice(tr("Chat display action could not be queued"));}}}
void GeneralChatController::participantPreferenceView(quint64 generation,quint64 id,QByteArray key,bool pinned,bool expected,bool value){if(auto v=currentView(generation,id)){if(!service_->participantPreference(v->key.toUtf8().toStdString(),key.toStdString(),pinned,expected,value,ChatViewTarget{generation,v->epoch,id})){emit notice(tr("Participant action could not be queued"));}}}

void GeneralChatController::privateChatView(quint64 g,quint64 id,QByteArray key){if(auto v=currentView(g,id)){if(!v->privateChat){privateChat(v->key,v->epoch,std::move(key));}}}
void GeneralChatController::keepAliveView(quint64 g,quint64 id,bool expected,bool value){if(auto v=currentView(g,id)){keepAlive(g,v->key,v->epoch,expected,value);}}
void GeneralChatController::windowSettingsView(quint64 g,quint64 id,ChatWindowSettings expected,ChatWindowSettings value){if(currentView(g,id)){windowSettings(expected,value);}}
void GeneralChatController::windowSettingsScoped(quint64 g,ChatWindowSettings expected,ChatWindowSettings value){if(service_&&service_->snapshot()->generation==g){windowSettings(expected,value);}}
void GeneralChatController::notificationsView(quint64 g,quint64 id,ChatNotificationSettings expected,ChatNotificationSettings value){if(currentView(g,id)){notifications(std::move(expected),std::move(value));}}
void GeneralChatController::privatePolicyView(quint64 g,quint64 id,int expected,int value){if(currentView(g,id)){privateChatPolicy(expected,value);}}
void GeneralChatController::publicRoomView(quint64 g,quint64 id,QString key){if(currentView(g,id)){joinRoom(std::move(key));}}
