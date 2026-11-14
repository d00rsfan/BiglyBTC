// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_controller.hpp"
#include "chat_message_projection.hpp"
#include <QPointer>
#include <algorithm>
namespace biglybtc::app {
namespace {QString q(const std::string& value){return QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size()));}}
GeneralChatController::GeneralChatController(QObject* parent):QObject(parent){timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,&GeneralChatController::poll);timer_.start();}
void GeneralChatController::setService(std::shared_ptr<GeneralChats> value){
 if(service_==value){return;}const QPointer<GeneralChatController> guard(this);const auto requested=value;const auto observed=value?value->snapshot():nullptr;const auto wantedSession=observed&&observed->generation==downloadSession_?downloadSession_:0;const auto wantedHash=wantedSession?downloadHash_:QString{};if(service_){static_cast<void>(service_->setReadingRoom({}));}service_=std::move(value);times_.clear();ratingViews_.clear();downloadSession_=0;downloadHash_.clear();downloadTimes_.clear();
 if(service_){static_cast<void>(service_->setReadingRoom(readingRoom_.toUtf8().toStdString()));}
 if(service_){generation_=0;sequence_=0;poll();}else{auto packet=std::make_shared<ui::DistributedRatingsPacket>();packet->session=generation_;const QPointer<GeneralChatController> self(this);emit distributedRatingsReady(std::move(packet));if(!self){return;}ui::ChatViewSnapshot out;out.generation=generation_;out.sequence=++sequence_;out.availability=tr("Open a profile to use Public chats");emit snapshotReady(std::move(out));}
 if(guard&&service_==requested&&wantedSession&&!wantedHash.isEmpty()){openDownload(wantedSession,wantedHash);}
}
bool GeneralChatController::available(){if(service_){return true;}emit notice(tr("Public chats are not available for this session"));return false;}
void GeneralChatController::joinRoom(QString key){if(available()&&!service_->joinRoom(key.toUtf8().toStdString())){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::selectRoom(QString key){if(service_){static_cast<void>(service_->selectRoom(key.toUtf8().toStdString()));}}
void GeneralChatController::readingRoom(QString key){if(readingRoom_==key){return;}readingRoom_=std::move(key);if(service_){static_cast<void>(service_->setReadingRoom(readingRoom_.toUtf8().toStdString()));}}
void GeneralChatController::leaveRoom(QString key){if(available()&&!service_->leaveRoom(key.toUtf8().toStdString())){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::send(QString key,QString text){if(!service_||!service_->send(key.toUtf8().toStdString(),text.toUtf8().toStdString())){emit sendFinished(key,text,false);}}
void GeneralChatController::notifications(ChatNotificationSettings expected,ChatNotificationSettings value){if(available()&&!service_->notifications(std::move(expected),std::move(value))){emit notice(tr("Chat notification settings could not be queued"));}}
void GeneralChatController::indications(QString key,quint64 epoch,bool expected,bool value){if(available()&&!service_->indications(key.toUtf8().toStdString(),epoch,expected,value)){emit notice(tr("Chat indication settings are not available for this room"));}}
void GeneralChatController::options(QString key,bool favourite,bool save){if(available()&&!service_->options(key.toUtf8().toStdString(),favourite,save)){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::displayPreference(QString key,bool ratings,bool expected,bool value){if(available()&&!service_->displayPreference(key.toUtf8().toStdString(),ratings,expected,value)){emit notice(tr("Chat preference queue is full"));}}
void GeneralChatController::participantPreference(QString key,QByteArray publicKey,bool pinned,bool expected,bool value){if(available()&&!service_->participantPreference(key.toUtf8().toStdString(),publicKey.toStdString(),pinned,expected,value)){emit notice(tr("Chat preference queue is full"));}}
void GeneralChatController::nicknameMode(QString key,bool shared){if(available()&&!service_->nicknameMode(key.toUtf8().toStdString(),shared)){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::nickname(QString key,QString value,bool shared){if(available()&&!service_->nickname(key.toUtf8().toStdString(),value.toUtf8().toStdString(),shared)){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::watchDownload(quint64 session,QString hash){
 if(!session||hash.isEmpty()){downloadSession_=0;downloadHash_.clear();downloadTimes_.clear();return;}if(service_&&service_->snapshot()->generation!=session){return;}if(downloadSession_!=session||downloadHash_!=hash){downloadTimes_.clear();}downloadSession_=session;downloadHash_=std::move(hash);if(service_){publishDownload(service_->snapshot());}
}
void GeneralChatController::openDownload(quint64 session,QString hash){
 if(!session||hash.isEmpty()){return;}auto parsed=core::TorrentHash::parse(hash.toStdString());if(!parsed){emit notice(tr("Invalid download chat identity"));return;}const QPointer<GeneralChatController> guard(this);watchDownload(session,std::move(hash));if(!guard||!service_||downloadSession_!=session){return;}if(!service_->openDownloadChat(*parsed)){emit notice(tr("Download chat command queue is full"));}
}
void GeneralChatController::publishDownload(const std::shared_ptr<const GeneralChatSnapshot>& value){
 if(!value||!downloadSession_||downloadHash_.isEmpty()){return;}ui::ChatViewSnapshot out;out.generation=downloadSession_;out.sequence=value->sequence;out.availability=q(value->availability);out.preferencesReady=value->preferencesReady;out.hideRatings=value->preferences.display.hideRatings;out.hideSubscriptions=value->preferences.display.hideSearchSubscriptions;out.notificationSettings=value->preferences.notifications;const auto room=std::ranges::find_if(value->downloadRooms,[this](const auto& row){return q(row.hash)==downloadHash_;});if(room!=value->downloadRooms.end()){const auto& row=room->room;const auto& settings=row.settings;out.selected=q(settings.key);out.rooms.push_back({q(settings.key),q(settings.nickname),q(row.status),settings.favourite,settings.saveHistory,settings.readOnly,row.joined,row.canSend,static_cast<int>(row.unread),settings.sharedNickname,{},q(settings.nickname.empty()?settings.defaultNickname:settings.nickname),settings.disableIndicators,row.unseenMention,row.epoch});if(room->messages){projectChatMessages(out,*room->messages,downloadTimes_,QDateTime::currentDateTimeUtc(),value->preferences,true);}}
 else{const auto peek=std::ranges::find_if(value->downloadPeeks,[this](const auto& row){return q(row.hash)==downloadHash_;});out.availability=peek==value->downloadPeeks.end()?tr("Opening download chat…"):q(peek->status);}emit downloadSnapshotReady(downloadSession_,downloadHash_,std::move(out));
}
void GeneralChatController::requestStop()noexcept{if(service_){service_->requestStop();}}
void GeneralChatController::poll(){
 if(!service_){return;}const auto service=service_;const QPointer<GeneralChatController> self(this);
 for(auto& result:service->takeResults()){
  if(!result.failure&&!result.openedRoom.empty()){emit roomOpened(q(result.openedRoom));if(!self||service_!=service){return;}}
  if(result.kind==GeneralChatResult::Kind::Send){emit sendFinished(q(result.key),q(result.text),!result.failure);if(!self||service_!=service){return;}}
  if(result.failure){emit notice(q(result.failure->message));if(!self||service_!=service){return;}}
 }
 const auto value=service->snapshot();if(!value||(value->generation==generation_&&value->sequence==sequence_)){return;}
 if(value->generation!=generation_){times_.clear();}generation_=value->generation;sequence_=value->sequence;
 if(value->ratings!=ratingViews_){ratingViews_=value->ratings;auto packet=std::make_shared<ui::DistributedRatingsPacket>();packet->session=generation_;for(const auto& row:ratingViews_){if(row->byHash){continue;}packet->rows.push_back({q(row->hash),q(row->status),QString::fromStdU16String(row->ratings.comments()),row->ratings.average(),row->ratings.count(),row->ratings.commentCount(),row->storesConfirmed,row->running,row->current,row->publishing});}emit distributedRatingsReady(std::move(packet));if(!self||service_!=service){return;}}
 ui::ChatViewSnapshot out;out.generation=generation_;out.sequence=sequence_;out.availability=q(value->availability);out.preferencesReady=value->preferencesReady;out.hideRatings=value->preferences.display.hideRatings;out.hideSubscriptions=value->preferences.display.hideSearchSubscriptions;out.notificationSettings=value->preferences.notifications;out.selected=q(value->selected);
 const auto& dht=value->torrentDht;
 out.dhtStatus=tr("%1; bound networks: %2; eligible torrents: %3; pending: %4; lookups sent: %5; peer records stored: %6; withdrawn: %7; discovered peers: %8; delivered peers: %9")
     .arg(q(dht.state)).arg(dht.readyNetworks).arg(dht.eligible).arg(dht.pending).arg(dht.lookups).arg(dht.publications).arg(dht.withdrawals).arg(dht.discoveredPeers).arg(dht.deliveredPeers);
 for(const auto& row:value->rooms){const auto& settings=row.settings;out.rooms.push_back({q(settings.key),q(settings.nickname),q(row.status),settings.favourite,settings.saveHistory,settings.readOnly,row.joined,row.canSend,static_cast<int>(row.unread),settings.sharedNickname,{},q(settings.nickname.empty()?settings.defaultNickname:settings.nickname),settings.disableIndicators,row.unseenMention,row.epoch});}
 for(const auto& download:value->downloadRooms){const auto& row=download.room;if(!row.joined||!row.unread){continue;}const auto& settings=row.settings;out.notifications.push_back({q(settings.key),q(settings.nickname),q(row.status),settings.favourite,settings.saveHistory,settings.readOnly,row.joined,row.canSend,static_cast<int>(row.unread),settings.sharedNickname,q(download.hash),q(settings.nickname.empty()?settings.defaultNickname:settings.nickname),settings.disableIndicators,row.unseenMention,row.epoch});}
 publishDownload(value);if(!self||service_!=service){return;}projectChatMessages(out,value->messages,times_,QDateTime::currentDateTimeUtc(),value->preferences,true);emit snapshotReady(std::move(out));
}
}
