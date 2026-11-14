// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_windows.hpp"
#include <QApplication>
#include <QEvent>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QScreen>
#include <algorithm>
#include <limits>
namespace biglybtc::ui {
ChatWindows::ChatWindows(QWidget* owner,QTabWidget* tabs):QObject(owner),owner_(owner),tabs_(tabs){setObjectName("chat-window-manager");}
ChatWindows::~ChatWindows(){clear();}
bool ChatWindows::valid(quint64 id,const QString& key)const{auto it=entries_.find(id);if(it==entries_.end()){return false;}const auto& e=it->second;if(!e.bound||e.generation!=catalog_.generation||(!key.isEmpty()&&key!=e.key)){return false;}const auto room=std::ranges::find(catalog_.rooms,e.key,&ChatRoomView::key);return room!=catalog_.rooms.end()&&room->ownerEpoch==e.epoch&&room->privateChat==e.privateChat&&room->joined;}
void ChatWindows::setCatalog(ChatViewSnapshot value){
 if(value.generation<catalog_.generation||(value.generation==catalog_.generation&&value.sequence<catalog_.sequence)){return;}const QPointer<ChatWindows> self(this);if(value.generation!=catalog_.generation){clear();if(!self){return;}seenPrivate_.clear();}catalog_=std::move(value);std::vector<quint64> stale;
 for(const auto& [id,e]:entries_){const auto row=std::ranges::find(catalog_.rooms,e.key,&ChatRoomView::key);if(row==catalog_.rooms.end()||row->ownerEpoch!=e.epoch||row->privateChat!=e.privateChat){stale.push_back(id);}}
 for(auto id:stale){closeView(id);if(!self){return;}}
 const auto rooms=catalog_.rooms;for(const auto& r:rooms){if(r.privateChat&&r.joined&&r.ownerEpoch&&seenPrivate_.insert({r.key,r.ownerEpoch}).second){openRoom(r.key);if(!self){return;}}}
}
void ChatWindows::openRoom(QString key,Mode mode){
 const auto room=std::ranges::find(catalog_.rooms,key,&ChatRoomView::key);if(room==catalog_.rooms.end()||!room->joined||!room->ownerEpoch||!catalog_.generation||!tabs_||!owner_){return;}const bool popout=mode==Mode::Popout||(mode==Mode::Configured&&!catalog_.windowSettings.toSidebar);
 for(const auto& [id,e]:entries_){(void)id;if(e.key==key&&e.epoch==room->ownerEpoch&&e.popout==popout){if(e.container){if(popout){e.container->showNormal();e.container->raise();e.container->activateWindow();}else{tabs_->setCurrentWidget(e.container);}}return;}}
 if(entries_.size()>=64||next_==std::numeric_limits<quint64>::max()){emit notice(tr("Close a chat window before opening another"));return;}
 Entry entry;entry.id=++next_;entry.generation=catalog_.generation;entry.epoch=room->ownerEpoch;entry.key=key;entry.privateChat=room->privateChat;entry.popout=popout;QWidget* container=nullptr;
 if(popout){const bool top=!catalog_.windowSettings.standalone;const auto flags=Qt::Window|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowCloseButtonHint|Qt::WindowMinimizeButtonHint|Qt::WindowMaximizeButtonHint|(top?Qt::WindowStaysOnTopHint:Qt::WindowFlags{});container=new QWidget(top?owner_.data():nullptr,flags);container->setAttribute(Qt::WA_DeleteOnClose);container->setWindowIcon(owner_->windowIcon());container->setWindowTitle(key);container->setMinimumSize(300,150);container->resize(500,500);container->installEventFilter(this);}
 else{container=new QWidget(tabs_);}container->setObjectName("chat-window");container->setProperty("BiglyBTC.chatViewID",entry.id);container->setProperty("chat-generation",entry.generation);container->setProperty("chat-epoch",entry.epoch);container->setProperty("chat-private",entry.privateChat);auto* layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);auto* page=new ChatPage(container);page->setFixedRoom(key);layout->addWidget(page);entry.container=container;entry.page=page;entry.snapshot=catalog_;entry.snapshot.sequence=0;entry.snapshot.selected=key;entry.snapshot.rooms={*room};entry.snapshot.rooms[0].canSend=false;entry.snapshot.rooms[0].joined=false;entry.snapshot.rooms[0].status=tr("Opening chat window…");entry.snapshot.messages.clear();entry.snapshot.participants.clear();entry.snapshot.notifications.clear();entry.snapshot.boundView=0;page->setSnapshot(entry.snapshot);const auto id=entry.id;entries_.emplace(id,std::move(entry));connectPage(id);connect(container,&QObject::destroyed,this,[this,id]{closeView(id,false);});
 if(!popout){QString title=key;title.replace('&',"&&");tabs_->addTab(container,title);tabs_->setTabToolTip(tabs_->indexOf(container),QStringLiteral("<p>%1</p>").arg(key.toHtmlEscaped()));}
 const QPointer<ChatWindows> self(this);const QPointer<QWidget> widget(container);emit acquireRequested(catalog_.generation,key,room->ownerEpoch,id);if(!self||!widget||!entries_.contains(id)){return;}if(popout){widget->show();widget->raise();widget->activateWindow();}else if(tabs_){tabs_->setCurrentWidget(widget);}emit readingChanged();
}
void ChatWindows::connectPage(quint64 id){
 auto* page=entries_.at(id).page.data();const auto gen=entries_.at(id).generation;
 connect(page,&ChatPage::sendRequested,this,[this,id,gen](QString key,QString text){if(valid(id,key)){emit sendRequested(gen,id,std::move(text));}});
 connect(page,&ChatPage::privateChatRequested,this,[this,id,gen](QString key,quint64,QByteArray peer){if(valid(id,key)&&!entries_.at(id).privateChat){emit privateRequested(gen,id,std::move(peer));}});
 connect(page,&ChatPage::nicknameRequested,this,[this,id,gen](QString key,QString text,bool expected){if(valid(id,key)){emit nicknameRequested(gen,id,std::move(text),expected);}});
 connect(page,&ChatPage::nicknameModeRequested,this,[this,id,gen](QString key,bool shared){if(valid(id,key)){emit nicknameModeRequested(gen,id,shared);}});
 connect(page,&ChatPage::displayPreferenceRequested,this,[this,id,gen](QString key,bool ratings,bool expected,bool value){if(valid(id,key)){emit displayPreferenceRequested(gen,id,ratings,expected,value);}});
 connect(page,&ChatPage::participantPreferenceRequested,this,[this,id,gen](QString key,QByteArray peer,bool pinned,bool expected,bool value){if(valid(id,key)){emit participantPreferenceRequested(gen,id,std::move(peer),pinned,expected,value);}});
 connect(page,&ChatPage::keepAliveRequested,this,[this,id,gen](QString key,quint64,bool expected,bool value){if(valid(id,key)){emit keepAliveRequested(gen,id,expected,value);}});
 connect(page,&ChatPage::windowSettingsRequested,this,[this,id,gen](auto expected,auto value){if(valid(id)){emit windowSettingsRequested(gen,id,expected,value);}});
 connect(page,&ChatPage::notificationSettingsRequested,this,[this,id,gen](auto expected,auto value){if(valid(id)){emit notificationSettingsRequested(gen,id,std::move(expected),std::move(value));}});
 connect(page,&ChatPage::privateChatPolicyRequested,this,[this,id,gen](int expected,int value){if(valid(id)){emit privatePolicyRequested(gen,id,expected,value);}});
 connect(page,&ChatPage::notificationSoundRequested,this,&ChatWindows::notificationSoundRequested);
 connect(page,&ChatPage::publicRoomRequested,this,[this,id,gen](QString key){if(valid(id)){emit publicRoomRequested(gen,id,std::move(key));}});
 connect(page,&ChatPage::externalLinkRequested,this,&ChatWindows::externalLinkRequested);
 connect(page,&ChatPage::popoutRequested,this,[this,id](QString key){if(valid(id,key)){openRoom(key,Mode::Popout);}});
 connect(page,&ChatPage::tabRequested,this,[this,id](QString key){if(valid(id,key)){openRoom(key,Mode::Tab);}});
}
void ChatWindows::setViewSnapshot(quint64 id,ChatViewSnapshot value){auto found=entries_.find(id);if(found==entries_.end()){return;}auto& e=found->second;const auto room=std::ranges::find(value.rooms,e.key,&ChatRoomView::key);if(value.generation!=e.generation||value.selected!=e.key||room==value.rooms.end()||room->ownerEpoch!=e.epoch||room->privateChat!=e.privateChat||value.sequence<e.snapshot.sequence){return;}e.bound=value.boundView==id;e.snapshot=std::move(value);const QPointer<ChatWindows> self(this);if(e.page){e.page->setSnapshot(e.snapshot);}if(self){emit readingChanged();}}
void ChatWindows::closeView(quint64 id,bool closeWidget){auto found=entries_.find(id);if(found==entries_.end()){return;}auto e=std::move(found->second);entries_.erase(found);if(!e.popout&&tabs_&&e.container){const auto index=tabs_->indexOf(e.container);if(index>=0){tabs_->removeTab(index);}}if(e.container&&closeWidget){e.container->deleteLater();}const QPointer<ChatWindows> self(this);emit releaseRequested(e.generation,id);if(self){emit readingChanged();}}
void ChatWindows::closeFromOwner(quint64 id,QString reason){const QPointer<ChatWindows> self(this);closeView(id);if(self&&!reason.isEmpty()){emit notice(std::move(reason));}}
bool ChatWindows::closeTab(QWidget* page){for(const auto& [id,e]:entries_){if(!e.popout&&e.container==page){closeView(id);return true;}}return false;}
void ChatWindows::clear(){std::vector<quint64> ids;for(const auto& [id,e]:entries_){(void)e;ids.push_back(id);}const QPointer<ChatWindows> self(this);for(auto id:ids){closeView(id);if(!self){return;}}}
void ChatWindows::sendFinished(quint64 id,QString key,QString text,bool accepted){auto found=entries_.find(id);if(found==entries_.end()||(!key.isEmpty()&&key!=found->second.key)||!found->second.page){return;}auto* page=found->second.page.data();if(accepted){page->submissionAccepted(found->second.key,text);}else{page->submissionFailed(found->second.key);}}
ChatReadContext ChatWindows::readingContext()const{ChatReadContext out{catalog_.generation,{},0};const auto active=QApplication::activeWindow();for(const auto& [id,e]:entries_){if(!valid(id)||!e.container||!e.container->isVisible()||e.container->isMinimized()){continue;}if((e.popout&&active==e.container)||(!e.popout&&owner_&&active==owner_&&tabs_&&tabs_->currentWidget()==e.container)){return {e.generation,e.key,e.epoch};}}return out;}
bool ChatWindows::eventFilter(QObject* watched,QEvent* event){if(event->type()==QEvent::Close){for(const auto& [id,e]:entries_){if(e.popout&&e.container==watched){closeView(id,false);return false;}}}if(event->type()==QEvent::WindowActivate||event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Show||event->type()==QEvent::Hide||event->type()==QEvent::WindowStateChange){QTimer::singleShot(0,this,[this]{emit readingChanged();});}return QObject::eventFilter(watched,event);}
}
