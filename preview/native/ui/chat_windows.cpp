// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_windows.hpp"
#include <QApplication>
#include <QEvent>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QScreen>
#include <QKeyEvent>
#include <QWindow>
#include <algorithm>
#include <limits>
#include <array>
#include <utility>
namespace biglybtc::ui {

namespace {
void fitChatGeometry(QWidget* widget){
 if(!widget||!widget->isVisible()||widget->isMinimized()||widget->isMaximized()||widget->isFullScreen()||!widget->windowHandle()){return;}
 const auto requested=widget->property("chat-initial-frame");const auto frame=requested.isValid()?requested.toRect():widget->frameGeometry();const auto* screen=QGuiApplication::screenAt(frame.topLeft());if(!screen){screen=widget->screen();}if(!screen){return;}const auto available=screen->availableGeometry();const auto margins=widget->windowHandle()->frameMargins();
 const int maxWidth=std::max(1,available.width()-margins.left()-margins.right()),maxHeight=std::max(1,available.height()-margins.top()-margins.bottom());
 const int width=std::clamp(frame.width()-margins.left()-margins.right(),std::min(widget->minimumWidth(),maxWidth),maxWidth),height=std::clamp(frame.height()-margins.top()-margins.bottom(),std::min(widget->minimumHeight(),maxHeight),maxHeight);
 const int left=available.left()+margins.left(),top=available.top()+margins.top(),right=std::max(left,available.right()-margins.right()-width+1),bottom=std::max(top,available.bottom()-margins.bottom()-height+1);
 const QRect client(std::clamp(frame.left()+margins.left(),left,right),std::clamp(frame.top()+margins.top(),top,bottom),width,height);widget->setProperty("chat-initial-frame",QVariant{});if(widget->geometry()!=client){widget->setGeometry(client);}
}
}
ChatWindows::ChatWindows(QWidget* owner,QTabWidget* tabs):QObject(owner),owner_(owner),tabs_(tabs){setObjectName("chat-window-manager");geometryTimer_.setInterval(1000);geometryTimer_.setSingleShot(true);connect(&geometryTimer_,&QTimer::timeout,this,&ChatWindows::saveGeometry);}
ChatWindows::~ChatWindows(){clear();}
bool ChatWindows::valid(quint64 id,const QString& key)const{auto it=entries_.find(id);if(it==entries_.end()){return false;}const auto& e=it->second;if(e.pending||!e.bound||e.generation!=catalog_.generation||(!key.isEmpty()&&key!=e.key)){return false;}const auto room=std::ranges::find(catalog_.rooms,e.key,&ChatRoomView::key);return room!=catalog_.rooms.end()&&room->ownerEpoch==e.epoch&&room->privateChat==e.privateChat&&room->joined;}
void ChatWindows::setCatalog(ChatViewSnapshot value){
 if(value.generation<catalog_.generation||(value.generation==catalog_.generation&&value.sequence<catalog_.sequence)){return;}const QPointer<ChatWindows> self(this);if(value.generation!=catalog_.generation){clear();if(!self){return;}seenPrivate_.clear();seenAttempts_.clear();}catalog_=std::move(value);std::vector<quint64> stale;
 for(const auto& [id,e]:entries_){if(e.pending){const auto parent=std::ranges::find(catalog_.rooms,e.parent,&ChatRoomView::key);if(parent==catalog_.rooms.end()||parent->ownerEpoch!=e.parentEpoch||!parent->joined){stale.push_back(id);}continue;}const auto row=std::ranges::find(catalog_.rooms,e.key,&ChatRoomView::key);if(row==catalog_.rooms.end()||row->ownerEpoch!=e.epoch||row->privateChat!=e.privateChat){stale.push_back(id);}}
 for(auto id:stale){closeView(id);if(!self){return;}}
 std::erase_if(seenPrivate_,[this](const auto& p){return std::ranges::none_of(catalog_.rooms,[&](const auto& r){return r.key==p.first&&r.ownerEpoch==p.second;});});
 std::erase_if(seenAttempts_,[this](auto request){return std::ranges::none_of(catalog_.privateAttempts,[&](const auto& a){return a.request==request;})&&std::ranges::none_of(entries_,[&](const auto& p){return p.second.pending&&p.second.request==request;});});
 const auto attempts=catalog_.privateAttempts;for(const auto& a:attempts){if(seenAttempts_.insert(a.request).second){openAttempt(a);if(!self){return;}}if(!a.openedRoom.isEmpty()){finishAttempt(catalog_.generation,a.request,a.openedRoom,{});if(!self){return;}}}resolveAttempts();if(!self){return;}
 const auto rooms=catalog_.rooms;for(const auto& r:rooms){if(r.privateChat&&r.joined&&r.ownerEpoch&&seenPrivate_.insert({r.key,r.ownerEpoch}).second){openRoom(r.key);if(!self){return;}}}
}

quint64 ChatWindows::createEntry(Entry entry,ChatViewSnapshot initial){
 if(!owner_||!tabs_||entries_.size()>=64||next_==std::numeric_limits<quint64>::max()){emit notice(tr("Close a chat window before opening another"));return 0;}entry.id=++next_;const auto id=entry.id;const auto key=entry.key;QWidget* container=nullptr;
 if(entry.popout){const bool top=!catalog_.windowSettings.standalone;const auto flags=Qt::Window|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowCloseButtonHint|Qt::WindowMinimizeButtonHint|Qt::WindowMaximizeButtonHint|(top?Qt::WindowStaysOnTopHint:Qt::WindowFlags{});container=new QWidget(top?owner_.data():nullptr,flags);container->setAttribute(Qt::WA_DeleteOnClose);container->setWindowIcon(owner_->windowIcon());container->setWindowTitle(key);container->setMinimumSize(300,150);const auto initialFrame=initialGeometry();container->setProperty("chat-initial-frame",initialFrame);container->setGeometry(initialFrame);container->installEventFilter(this);}
 else{container=new QWidget(tabs_);}container->setObjectName("chat-window");container->setProperty("BiglyBTC.chatViewID",id);container->setProperty("chat-generation",entry.generation);container->setProperty("chat-epoch",entry.epoch);container->setProperty("chat-private",entry.privateChat);container->setProperty("chat-pending-request",entry.request);auto* layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);auto* page=new ChatPage(container);page->setFixedRoom(key);layout->addWidget(page);entry.container=container;entry.page=page;entry.snapshot=std::move(initial);entry.snapshot.sequence=0;entry.snapshot.selected=key;entry.snapshot.messages.clear();entry.snapshot.participants.clear();entry.snapshot.notifications.clear();entry.snapshot.privateAttempts.clear();entry.snapshot.boundView=0;page->setSnapshot(entry.snapshot);const bool popout=entry.popout;entries_.emplace(id,std::move(entry));connectPage(id);connect(container,&QObject::destroyed,this,[this,id]{closeView(id,false);});
 const QPointer<ChatWindows> self(this);if(!popout){QString title=key;title.replace('&',"&&");tabs_->addTab(container,title);if(!self){return 0;}tabs_->setTabToolTip(tabs_->indexOf(container),QStringLiteral("<p>%1</p>").arg(key.toHtmlEscaped()));}return id;
}
void ChatWindows::showEntry(quint64 id){const auto it=entries_.find(id);if(it==entries_.end()||!it->second.container){return;}const auto w=it->second.container;if(it->second.popout){w->showNormal();w->raise();w->activateWindow();}else if(tabs_){tabs_->setCurrentWidget(w);}emit readingChanged();}
void ChatWindows::openRoom(QString key,Mode mode){
 const auto room=std::ranges::find(catalog_.rooms,key,&ChatRoomView::key);if(room==catalog_.rooms.end()||!room->joined||!room->ownerEpoch||!catalog_.generation||!tabs_||!owner_){return;}const bool popout=mode==Mode::Popout||(mode==Mode::Configured&&!catalog_.windowSettings.toSidebar);
 if(mode==Mode::Configured){for(const auto& [id,e]:entries_){if(!e.pending&&e.popout&&e.key==key&&e.epoch==room->ownerEpoch){showEntry(id);return;}}}
 for(const auto& [id,e]:entries_){if(!e.pending&&e.key==key&&e.epoch==room->ownerEpoch&&e.popout==popout){showEntry(id);return;}}
 Entry entry;entry.generation=catalog_.generation;entry.epoch=room->ownerEpoch;entry.key=key;entry.privateChat=room->privateChat;entry.popout=popout;auto initial=catalog_;initial.rooms={*room};initial.rooms[0].canSend=false;initial.rooms[0].joined=false;initial.rooms[0].status=tr("Opening chat window…");const auto generation=entry.generation,epoch=entry.epoch;const QPointer<ChatWindows> self(this);const auto id=createEntry(std::move(entry),std::move(initial));if(!self||!id){return;}emit acquireRequested(generation,key,epoch,id);if(self){showEntry(id);}
}
void ChatWindows::openAttempt(const ChatPrivateAttemptView& attempt){
 if(!attempt.request||!attempt.parentEpoch||attempt.participant.isEmpty()){return;}const auto parent=std::ranges::find(catalog_.rooms,attempt.parent,&ChatRoomView::key);if(parent==catalog_.rooms.end()||parent->privateChat||parent->ownerEpoch!=attempt.parentEpoch||!parent->joined){return;}
 Entry entry;entry.generation=catalog_.generation;entry.key=tr("Private connection %1 — %2").arg(attempt.request).arg(attempt.nickname);entry.privateChat=true;entry.popout=!catalog_.windowSettings.toSidebar;entry.pending=true;entry.request=attempt.request;entry.parentEpoch=attempt.parentEpoch;entry.parent=attempt.parent;entry.completedKey=attempt.openedRoom;auto initial=catalog_;ChatRoomView row;row.key=entry.key;row.privateChat=true;row.status=tr("Connecting to the signed participant in %1… Close this window to cancel.").arg(attempt.parent);initial.rooms={row};const auto generation=entry.generation;const QPointer<ChatWindows> self(this);const auto id=createEntry(std::move(entry),std::move(initial));if(!self){return;}if(!id){emit cancelPrivateRequested(generation,attempt.request,attempt.parent,attempt.parentEpoch);return;}showEntry(id);
}
void ChatWindows::finishAttempt(quint64 generation,quint64 request,QString key,QString error){
 for(auto& [id,e]:entries_){(void)id;if(!e.pending||e.generation!=generation||e.request!=request){continue;}if(!error.isEmpty()){e.pendingDone=true;e.snapshot.rooms[0].status=std::move(error);++e.snapshot.sequence;if(e.page){e.page->setSnapshot(e.snapshot);}return;}if(!key.isEmpty()){e.completedKey=std::move(key);}break;}resolveAttempts();
}
void ChatWindows::resolveAttempts(){
 std::vector<quint64> ids;for(const auto& [id,e]:entries_){if(e.pending&&!e.pendingDone&&!e.completedKey.isEmpty()){ids.push_back(id);}}const QPointer<ChatWindows> self(this);
 for(auto id:ids){auto it=entries_.find(id);if(it==entries_.end()){continue;}auto& e=it->second;const auto room=std::ranges::find(catalog_.rooms,e.completedKey,&ChatRoomView::key);if(room==catalog_.rooms.end()){continue;}if(!room->privateChat||!room->ownerEpoch||!room->joined){finishAttempt(e.generation,e.request,{},tr("Private connection source changed"));if(!self){return;}continue;}
  e.key=room->key;e.epoch=room->ownerEpoch;e.pending=false;e.bound=false;seenPrivate_.insert({e.key,e.epoch});e.snapshot=catalog_;e.snapshot.sequence=0;e.snapshot.selected=e.key;e.snapshot.rooms={*room};e.snapshot.rooms[0].joined=false;e.snapshot.rooms[0].canSend=false;e.snapshot.rooms[0].status=tr("Opening chat window…");e.snapshot.messages.clear();e.snapshot.participants.clear();e.snapshot.notifications.clear();e.snapshot.privateAttempts.clear();e.snapshot.boundView=0;const auto generation=e.generation,epoch=e.epoch;const auto key=e.key;
  if(e.container){e.container->setProperty("chat-epoch",epoch);e.container->setProperty("chat-pending-request",quint64(0));e.container->setWindowTitle(key);if(!e.popout&&tabs_){auto title=key;title.replace('&',"&&");tabs_->setTabText(tabs_->indexOf(e.container),title);}}if(e.page){e.page->replaceFixedRoom(key);e.page->setSnapshot(e.snapshot);}if(!self){return;}emit acquireRequested(generation,key,epoch,id);if(!self){return;}
 }
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
 connect(page,&ChatPage::postingRequested,this,[this,id,gen](QString key,quint64,bool expected,bool value){if(valid(id,key)){emit postingRequested(gen,id,expected,value);}});
 connect(page,&ChatPage::notificationSoundRequested,this,&ChatWindows::notificationSoundRequested);
 connect(page,&ChatPage::publicRoomRequested,this,[this,id,gen](QString key){if(valid(id)){emit publicRoomRequested(gen,id,std::move(key));}});
 connect(page,&ChatPage::externalLinkRequested,this,&ChatWindows::externalLinkRequested);
 connect(page,&ChatPage::popoutRequested,this,[this,id](QString key){if(valid(id,key)){openRoom(key,Mode::Popout);}});
 connect(page,&ChatPage::tabRequested,this,[this,id](QString key){if(valid(id,key)){openRoom(key,Mode::Tab);}});
}
void ChatWindows::setViewSnapshot(quint64 id,ChatViewSnapshot value){auto found=entries_.find(id);if(found==entries_.end()){return;}auto& e=found->second;const auto room=std::ranges::find(value.rooms,e.key,&ChatRoomView::key);if(value.generation!=e.generation||value.selected!=e.key||room==value.rooms.end()||room->ownerEpoch!=e.epoch||room->privateChat!=e.privateChat||value.sequence<e.snapshot.sequence){return;}e.bound=value.boundView==id;e.snapshot=std::move(value);const QPointer<ChatWindows> self(this);if(e.page){e.page->setSnapshot(e.snapshot);}if(self){emit readingChanged();}}
void ChatWindows::closeView(quint64 id,bool closeWidget){auto found=entries_.find(id);if(found==entries_.end()){return;}auto e=std::move(found->second);entries_.erase(found);if(!e.popout&&tabs_&&e.container){const auto index=tabs_->indexOf(e.container);if(index>=0){tabs_->removeTab(index);}}if(e.container&&closeWidget){e.container->deleteLater();}const QPointer<ChatWindows> self(this);if(e.pending){if(!e.pendingDone){emit cancelPrivateRequested(e.generation,e.request,e.parent,e.parentEpoch);}}else{emit releaseRequested(e.generation,id);}if(self){emit readingChanged();}}
void ChatWindows::closeFromOwner(quint64 id,QString reason){const QPointer<ChatWindows> self(this);closeView(id);if(self&&!reason.isEmpty()){emit notice(std::move(reason));}}
bool ChatWindows::closeTab(QWidget* page){for(const auto& [id,e]:entries_){if(!e.popout&&e.container==page){closeView(id);return true;}}return false;}
void ChatWindows::clear(){geometryTimer_.stop();saveGeometry();std::vector<quint64> ids;for(const auto& [id,e]:entries_){(void)e;ids.push_back(id);}const QPointer<ChatWindows> self(this);for(auto id:ids){closeView(id);if(!self){return;}}}
void ChatWindows::sendFinished(quint64 id,QString key,QString text,bool accepted){auto found=entries_.find(id);if(found==entries_.end()||(!key.isEmpty()&&key!=found->second.key)||!found->second.page){return;}auto* page=found->second.page.data();if(accepted){page->submissionAccepted(found->second.key,text);}else{page->submissionFailed(found->second.key);}}
ChatReadContext ChatWindows::readingContext()const{ChatReadContext out{catalog_.generation,{},0};const auto active=QApplication::activeWindow();for(const auto& [id,e]:entries_){if(!valid(id)||!e.container||!e.container->isVisible()||e.container->isMinimized()){continue;}if((e.popout&&active==e.container)||(!e.popout&&owner_&&active==owner_&&tabs_&&tabs_->currentWidget()==e.container)){return {e.generation,e.key,e.epoch};}}return out;}
QRect ChatWindows::initialGeometry()const{
 const auto* screen=owner_?owner_->screen():QGuiApplication::primaryScreen();auto available=screen?screen->availableGeometry():QRect(0,0,1920,1080);QRect rect(0,0,500,500);rect.moveCenter(available.center());
 const auto parts=QString::fromStdString(catalog_.windowSettings.position).split(',');if(parts.size()==4){std::array<int,4> v{};bool valid=true;for(int i=0;i<4;++i){bool ok=false;v[i]=parts[i].toInt(&ok);valid&=ok;}if(valid){if(const auto* previousScreen=QGuiApplication::screenAt(QPoint(v[0],v[1]))){available=previousScreen->availableGeometry();}rect=QRect(std::clamp(v[0],available.left(),available.right()),std::clamp(v[1],available.top(),available.bottom()),std::clamp(v[2],300,32768),std::clamp(v[3],150,32768));}}
 bool cascade=false;int x=0,y=0;for(const auto& [id,e]:entries_){(void)id;if(e.popout&&e.container){const auto r=e.container->frameGeometry();x=std::max(x,r.x());y=std::max(y,r.y());cascade=true;}}if(cascade){rect.moveTopLeft(QPoint(x+16,y+16));}
 if(const auto* cascadedScreen=QGuiApplication::screenAt(rect.topLeft())){available=cascadedScreen->availableGeometry();}
 rect.setWidth(std::min(rect.width(),available.width()));rect.setHeight(std::min(rect.height(),available.height()));rect.moveLeft(std::clamp(rect.x(),available.left(),available.right()-rect.width()+1));rect.moveTop(std::clamp(rect.y(),available.top(),available.bottom()-rect.height()+1));return rect;
}
void ChatWindows::rememberGeometry(QWidget* widget){if(widget&&widget->isVisible()&&!widget->isMinimized()&&!widget->isMaximized()&&!widget->isFullScreen()){pendingGeometry_=widget->frameGeometry();geometryTimer_.start();}}
void ChatWindows::saveGeometry(){if(!pendingGeometry_||!catalog_.generation||!catalog_.preferencesReady){return;}const auto r=*std::exchange(pendingGeometry_,{});auto next=catalog_.windowSettings;next.position=QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).toStdString();if(next!=catalog_.windowSettings){emit geometryRequested(catalog_.generation,catalog_.windowSettings,std::move(next));}}
bool ChatWindows::eventFilter(QObject* watched,QEvent* event){
 if(event->type()==QEvent::Show){const QPointer<QWidget> widget(qobject_cast<QWidget*>(watched));QTimer::singleShot(0,this,[widget]{fitChatGeometry(widget);});}

 if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){for(const auto& [id,e]:entries_){(void)id;if(e.popout&&e.container==watched){const auto widget=e.container;widget->close();return true;}}}

 if(event->type()==QEvent::Move||event->type()==QEvent::Resize){rememberGeometry(qobject_cast<QWidget*>(watched));}
if(event->type()==QEvent::Close){const QPointer<QObject> target(watched);const QPointer<ChatWindows> self(this);saveGeometry();if(!self||!target){return true;}for(const auto& [id,e]:entries_){if(e.popout&&e.container==watched){closeView(id,false);return !target;}}}if(event->type()==QEvent::WindowActivate||event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Show||event->type()==QEvent::Hide||event->type()==QEvent::WindowStateChange){QTimer::singleShot(0,this,[this]{emit readingChanged();});}return QObject::eventFilter(watched,event);}
}
