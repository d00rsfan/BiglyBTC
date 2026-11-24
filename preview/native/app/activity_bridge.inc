// SPDX-License-Identifier: GPL-2.0-or-later
// Existing serial Host executor; no Public network requirement to inspect saved notifications.
bool SessionBridge::activities(quint64 session,QString action,QVector<ui::ActivityRefView> references){
 if(!session||session!=publishedColumnLayoutSession_||busy_||closingSession_||references.size()>4096){return false;}
 return dispatch([this,session,action=std::move(action),references=std::move(references)](std::stop_token stop){
  checkpoint(stop);if(!host_||host_->columnLayoutSession!=session){throw Failure{{storage::ErrorCode::Conflict,"Notification profile session changed"}};}std::vector<ActivityReference> refs;for(const auto& ref:references){refs.push_back({ref.id.toUtf8().toStdString(),ref.stamp.toStdString()});}const auto now=QDateTime::currentMSecsSinceEpoch();auto result=action=="refresh"?ActivityProfile::read(host_->lease,now):action=="read"?ActivityProfile::readState(host_->lease,refs,true,now):action=="unread"?ActivityProfile::readState(host_->lease,refs,false,now):action=="remove"?ActivityProfile::remove(host_->lease,refs,now):std::expected<ActivitySnapshot,storage::Error>{std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Unknown notification action"})};ui::ActivityView out;out.session=session;
  if(result){out.writable=!result->recovered;out.status=out.writable?tr("Saved notifications"):tr("Recovered notifications; resolve the original file before editing");for(const auto& entry:result->entries){QString key;if(entry.publicChat()){key=text(entry.callbackData.at("key"));}out.rows.push_back({{text(entry.source.id),QByteArray::fromStdString(entry.source.stamp)},text(entry.text),key,QDateTime::fromMSecsSinceEpoch(entry.timestamp),entry.readOn>0,entry.viewed,entry.publicChat()});}
#ifdef BIGLYBTC_HOST_CHAT_PROFILE
   if(auto profile=ChatProfile::open(host_->lease)){if(auto prefs=(*profile)->preferences({})){out.settings=prefs->notifications;}}
#endif
  }else{out.status=text(result.error().message);}
  QMetaObject::invokeMethod(this,[this,session,out=std::move(out)]{const QPointer<SessionBridge> guard(this);if(!profileClosed_&&session==publishedColumnLayoutSession_){emit activitiesReady(out);if(!guard){return;}}completionMonitor_->stop();busy_=false;emit busyChanged(false);},Qt::QueuedConnection);
 },false);
}
bool SessionBridge::activitySettings(quint64 session,ui::ChatNotificationSettings expected,ui::ChatNotificationSettings value){
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
 if(chatController_&&chatController_->activeGeneration()==session){chatController_->notifications(std::move(expected),std::move(value));return true;}
#endif
#ifdef BIGLYBTC_HOST_CHAT_PROFILE
if(!session||session!=publishedColumnLayoutSession_||busy_||closingSession_){return false;}return dispatch([this,session,expected=std::move(expected),value=std::move(value)](std::stop_token stop){checkpoint(stop);if(!host_||host_->columnLayoutSession!=session){throw Failure{{storage::ErrorCode::Conflict,"Notification profile changed"}};}auto profile=take(ChatProfile::open(host_->lease));take(profile->saveNotifications(expected,value));QMetaObject::invokeMethod(this,[this,session]{const QPointer<SessionBridge> guard(this);completionMonitor_->stop();busy_=false;emit busyChanged(false);if(!guard){return;}if(!profileClosed_&&session==publishedColumnLayoutSession_){activities(session,QStringLiteral("refresh"),{});}},Qt::QueuedConnection);},false);
#else
 (void)session;
 (void)expected;
 (void)value;
 return false;
#endif
}
