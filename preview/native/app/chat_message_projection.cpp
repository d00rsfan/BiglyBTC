// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_message_projection.hpp"
#include "participant_name.hpp"
#include <QObject>
#include <QTimeZone>
#include <set>
namespace biglybtc::app {
void projectChatMessages(ui::ChatViewSnapshot& out,std::span<const services::msgsync::RoomMessage> messages,std::map<QByteArray,QDateTime>& times,const QDateTime& now,const services::msgsync::ChatPreferences& preferences,bool requireKnown){
 out.messages.clear();out.participants.clear();std::set<QByteArray> retained;std::map<QByteArray,qsizetype> participants;
 for(const auto& message:messages){
  const QByteArray id(reinterpret_cast<const char*>(message.id.data()),message.id.size()),key(message.publicKey.data(),message.publicKey.size());retained.insert(id);
  const auto [time,inserted]=times.try_emplace(id,message.timestamp?QDateTime::fromMSecsSinceEpoch(*message.timestamp,QTimeZone::utc()):now.addSecs(-message.ageSeconds));(void)inserted;
  auto name=QString::fromUtf8(message.nickname.data(),message.nickname.size());if(name.isEmpty()){const auto original=services::msgsync::participantShortName(std::string_view(message.publicKey));name=original?QString::fromLatin1(original->data(),original->size()):QObject::tr("Unknown participant");}
  const auto settings=preferences.participants.find(message.publicKey);const auto flags=settings==preferences.participants.end()?services::msgsync::ChatParticipantSettings{}:settings->second;
  if((!requireKnown||settings!=preferences.participants.end())&&services::msgsync::visibleChatMessage(flags.ignored,!message.text.empty(),message.origin,preferences.display)){out.messages.push_back({id,key,name,QString::fromUtf8(message.text.data(),message.text.size()),time->second,message.local,message.type==1});}
  const auto [position,first]=participants.try_emplace(key,out.participants.size());if(first){out.participants.push_back({key,name,flags.ignored,flags.pinned,message.local});}else{out.participants[position->second].nickname=name;}
 }
 // Java replays the sorted message list when rebuilding participant nicknames.
 // Keep historical per-message names, and the final replay name in the roster.
 std::erase_if(times,[&](const auto& pair){return !retained.contains(pair.first);});
}
}
