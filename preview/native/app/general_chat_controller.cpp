// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_controller.hpp"
#include <QPointer>
#include <QTimeZone>
#include <set>
namespace biglybtc::app {
namespace {QString q(const std::string& value){return QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size()));}QByteArray b(std::string_view value){return {value.data(),static_cast<qsizetype>(value.size())};}}
GeneralChatController::GeneralChatController(QObject* parent):QObject(parent){timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,&GeneralChatController::poll);timer_.start();}
void GeneralChatController::setService(std::shared_ptr<GeneralChats> value){
 if(service_==value){return;}if(service_){static_cast<void>(service_->setReadingRoom({}));}service_=std::move(value);times_.clear();ratingViews_.clear();
 if(service_){static_cast<void>(service_->setReadingRoom(readingRoom_.toUtf8().toStdString()));}
 if(service_){generation_=0;sequence_=0;poll();}else{auto packet=std::make_shared<ui::DistributedRatingsPacket>();packet->session=generation_;const QPointer<GeneralChatController> self(this);emit distributedRatingsReady(std::move(packet));if(!self){return;}ui::ChatViewSnapshot out;out.generation=generation_;out.sequence=++sequence_;out.availability=tr("Open a profile to use Public chats");emit snapshotReady(std::move(out));}
}
bool GeneralChatController::available(){if(service_){return true;}emit notice(tr("Public chats are not available for this session"));return false;}
void GeneralChatController::joinRoom(QString key){if(available()&&!service_->joinRoom(key.toUtf8().toStdString())){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::selectRoom(QString key){if(service_){static_cast<void>(service_->selectRoom(key.toUtf8().toStdString()));}}
void GeneralChatController::readingRoom(QString key){if(readingRoom_==key){return;}readingRoom_=std::move(key);if(service_){static_cast<void>(service_->setReadingRoom(readingRoom_.toUtf8().toStdString()));}}
void GeneralChatController::leaveRoom(QString key){if(available()&&!service_->leaveRoom(key.toUtf8().toStdString())){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::send(QString key,QString text){if(!service_||!service_->send(key.toUtf8().toStdString(),text.toUtf8().toStdString())){emit sendFinished(key,text,false);}}
void GeneralChatController::options(QString key,bool favourite,bool save){if(available()&&!service_->options(key.toUtf8().toStdString(),favourite,save)){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::nicknameMode(QString key,bool shared){if(available()&&!service_->nicknameMode(key.toUtf8().toStdString(),shared)){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::nickname(QString key,QString value,bool shared){if(available()&&!service_->nickname(key.toUtf8().toStdString(),value.toUtf8().toStdString(),shared)){emit notice(tr("Chat command queue is full"));}}
void GeneralChatController::requestStop()noexcept{if(service_){service_->requestStop();}}
void GeneralChatController::poll(){
 if(!service_){return;}const auto service=service_;const QPointer<GeneralChatController> self(this);
 for(auto& result:service->takeResults()){
  if(result.kind==GeneralChatResult::Kind::Send){emit sendFinished(q(result.key),q(result.text),!result.failure);if(!self||service_!=service){return;}}
  if(result.failure){emit notice(q(result.failure->message));if(!self||service_!=service){return;}}
 }
 const auto value=service->snapshot();if(!value||(value->generation==generation_&&value->sequence==sequence_)){return;}
 if(value->generation!=generation_){times_.clear();}generation_=value->generation;sequence_=value->sequence;
 if(value->ratings!=ratingViews_){ratingViews_=value->ratings;auto packet=std::make_shared<ui::DistributedRatingsPacket>();packet->session=generation_;for(const auto& row:ratingViews_){packet->rows.push_back({q(row->hash),q(row->status),QString::fromStdU16String(row->ratings.comments()),row->ratings.average(),row->ratings.count(),row->ratings.commentCount(),row->storesConfirmed,row->running,row->current,row->publishing});}emit distributedRatingsReady(std::move(packet));if(!self||service_!=service){return;}}
 ui::ChatViewSnapshot out;out.generation=generation_;out.sequence=sequence_;out.availability=q(value->availability);out.selected=q(value->selected);
 const auto& dht=value->torrentDht;
 out.dhtStatus=tr("%1; bound networks: %2; eligible torrents: %3; pending: %4; lookups sent: %5; peer records stored: %6; withdrawn: %7; discovered peers: %8; delivered peers: %9")
     .arg(q(dht.state)).arg(dht.readyNetworks).arg(dht.eligible).arg(dht.pending).arg(dht.lookups).arg(dht.publications).arg(dht.withdrawals).arg(dht.discoveredPeers).arg(dht.deliveredPeers);
 for(const auto& row:value->rooms){const auto& settings=row.settings;out.rooms.push_back({q(settings.key),q(settings.nickname),q(row.status),settings.favourite,settings.saveHistory,settings.readOnly,row.joined,row.canSend,static_cast<int>(row.unread),settings.sharedNickname});}
 std::set<QByteArray> retained,participants;const auto now=QDateTime::currentDateTimeUtc();
 for(const auto& message:value->messages){const QByteArray id(reinterpret_cast<const char*>(message.id.data()),message.id.size());const auto key=b(message.publicKey);retained.insert(id);auto [time,inserted]=times_.try_emplace(id,message.timestamp?QDateTime::fromMSecsSinceEpoch(*message.timestamp,QTimeZone::utc()):now.addSecs(-message.ageSeconds));(void)inserted;auto name=q(message.nickname);if(name.isEmpty()){name=QString::fromLatin1(key.toHex().first(12));}out.messages.push_back({id,key,name,q(message.text),time->second,message.local});if(participants.insert(key).second){out.participants.push_back({key,name});}}
 std::erase_if(times_,[&](const auto& entry){return !retained.contains(entry.first);});emit snapshotReady(std::move(out));
}
}
