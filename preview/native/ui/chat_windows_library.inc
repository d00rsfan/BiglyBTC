// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the ui namespace; this registry owns views, never network identities.
void LibraryWindow::ensureChatWindows(){
 if(chatWindows_){return;}chatWindows_=new ChatWindows(this,mainTabs_);auto* w=chatWindows_.data();
 connect(w,&ChatWindows::cancelPrivateRequested,this,&LibraryWindow::chatPrivateCancelRequested);
 connect(w,&ChatWindows::acquireRequested,this,&LibraryWindow::chatViewAcquireRequested);
 connect(w,&ChatWindows::releaseRequested,this,&LibraryWindow::chatViewReleaseRequested);
 connect(w,&ChatWindows::sendRequested,this,&LibraryWindow::chatViewSendRequested);
 connect(w,&ChatWindows::privateRequested,this,&LibraryWindow::chatViewPrivateRequested);
 connect(w,&ChatWindows::nicknameRequested,this,&LibraryWindow::chatViewNicknameRequested);
 connect(w,&ChatWindows::nicknameModeRequested,this,&LibraryWindow::chatViewNicknameModeRequested);
 connect(w,&ChatWindows::displayPreferenceRequested,this,&LibraryWindow::chatViewDisplayPreferenceRequested);
 connect(w,&ChatWindows::participantPreferenceRequested,this,&LibraryWindow::chatViewParticipantPreferenceRequested);
 connect(w,&ChatWindows::keepAliveRequested,this,&LibraryWindow::chatViewKeepAliveRequested);
 connect(w,&ChatWindows::windowSettingsRequested,this,&LibraryWindow::chatViewWindowSettingsRequested);
 connect(w,&ChatWindows::notificationSettingsRequested,this,&LibraryWindow::chatViewNotificationsRequested);
 connect(w,&ChatWindows::privatePolicyRequested,this,&LibraryWindow::chatViewPrivatePolicyRequested);
 connect(w,&ChatWindows::notificationSoundRequested,this,&LibraryWindow::chatNotificationSoundRequested);
 connect(w,&ChatWindows::publicRoomRequested,this,&LibraryWindow::chatViewPublicRoomRequested);
 connect(w,&ChatWindows::geometryRequested,this,&LibraryWindow::chatWindowSettingsRequested);
 connect(w,&ChatWindows::externalLinkRequested,this,&LibraryWindow::openExternalChatLink);
 connect(w,&ChatWindows::notice,this,&LibraryWindow::setMessage);
 connect(w,&ChatWindows::readingChanged,this,[this]{updateChatReadContext();});
}
void LibraryWindow::setChatViewSnapshot(quint64 id,ChatViewSnapshot value){if(!closing_&&chatWindows_){chatWindows_->setViewSnapshot(id,std::move(value));}}
void LibraryWindow::closeChatView(quint64 id,QString reason){if(chatWindows_){chatWindows_->closeFromOwner(id,std::move(reason));}}
void LibraryWindow::chatViewSendFinished(quint64 id,QString key,QString text,bool accepted){if(!closing_&&chatWindows_){chatWindows_->sendFinished(id,std::move(key),std::move(text),accepted);}}
void LibraryWindow::connectChatWindowActions(ChatPage* page){
 connect(page,&ChatPage::popoutRequested,this,[this](QString key){ensureChatWindows();chatWindows_->openRoom(std::move(key),ChatWindows::Mode::Popout);});
 connect(page,&ChatPage::tabRequested,this,[this](QString key){ensureChatWindows();chatWindows_->openRoom(std::move(key),ChatWindows::Mode::Tab);});
 connect(page,&ChatPage::windowSettingsRequested,this,[this,page](auto expected,auto value){if(!closing_&&page->snapshotGeneration()==chatSnapshot_.generation){emit chatWindowSettingsRequested(chatSnapshot_.generation,expected,value);}});
}
ChatViewSnapshot LibraryWindow::publicChatSnapshot()const{
 auto value=chatSnapshot_;value.privateAttempts.clear();const auto selected=std::ranges::find(value.rooms,value.selected,&ChatRoomView::key);
 if(selected==value.rooms.end()||selected->privateChat){value.selected=lastPublicChatSelection_;value.messages.clear();value.participants.clear();}
 value.rooms.removeIf([](const auto& r){return r.privateChat;});value.notifications.removeIf([](const auto& r){return r.privateChat;});return value;
}

void LibraryWindow::chatPrivateAttemptFinished(quint64 generation,quint64 request,QString key,QString error){if(!closing_&&chatWindows_){chatWindows_->finishAttempt(generation,request,std::move(key),std::move(error));}}
