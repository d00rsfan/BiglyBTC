// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "attention_policy.hpp"
#include <QString>
#include <algorithm>
#include <QVector>
#include <cstdint>
#include <map>
#include <set>
#include <span>

namespace biglybtc::services::msgsync {
// BuddyPluginBeta.ChatMessage.getNickLocations: raw UTF-16 text, case-sensitive,
// ASCII nickname spaces become NBSP, non-overlapping hits with char boundaries.
// Qt's Unicode-version classification is qualified by selected JDK17 traces;
// this does not claim exhaustive equality across later Unicode additions.
inline QVector<int> notificationNickLocations(const QString& text,QString nickname){
 nickname.replace(u' ',QChar(0xa0));QVector<int> result;if(nickname.isEmpty()){return result;}
 const auto word=[](QChar c){return c.isLetter()||c.category()==QChar::Number_DecimalDigit;};
 qsizetype pos=0;
 while(pos<text.size()){
  pos=text.indexOf(nickname,pos,Qt::CaseSensitive);if(pos<0){break;}
  const auto end=pos+nickname.size();
  if((pos==0||!word(text[pos-1]))&&(end==text.size()||!word(text[end]))){
   if(result.isEmpty()){result.push_back(static_cast<int>(nickname.size()));}
   result.push_back(static_cast<int>(pos));
  }
  pos=end;
 }
 return result;
}

struct ChatPendingNotification {
 std::string key;std::uint64_t epoch=0;std::uint32_t count=0;
 bool disabled=false,mention=false,alive=true;
};
enum class ChatNotificationIcon {Idle,Message,Mention,Dim};
struct ChatNotificationPulse {ChatNotificationIcon icon=ChatNotificationIcon::Idle;bool sound=false;};
// The SWT timer is2500ms. Its previous set contains ChatInstance identities,
// not message counts or names. A new owner epoch therefore starts a new edge.
// This pure policy performs no IO, network, timer scheduling or audio playback.
class ChatPendingNotifications final {
public:
 static constexpr int IntervalMilliseconds=2500;
 void reset()noexcept{pending_.clear();previous_.clear();ticks_=0;running_=false;icon_=ChatNotificationIcon::Idle;}
 void update(std::uint64_t generation,std::span<const ChatPendingNotification> rooms){
  if(generation<generation_){return;}
  if(generation!=generation_){reset();generation_=generation;}
  std::map<std::string,ChatPendingNotification> unique;
  if(generation){for(const auto& room:rooms){if(!room.alive||!room.count){continue;}
   auto [it,inserted]=unique.try_emplace(room.key,room);
   if(!inserted){if(it->second.epoch<room.epoch){it->second=room;}
    else if(it->second.epoch==room.epoch){it->second.count=std::max(it->second.count,room.count);it->second.mention=it->second.mention||room.mention;}}
  }}
  pending_=std::move(unique);
  if(!pending_.empty()&&!running_){ticks_=0;previous_.clear();running_=true;}
 }
 ChatNotificationPulse tick(const ChatNotificationSettings& settings){
  if(!running_){return {};}
  // Original cancels an empty timer on its next tick, not on an intervening
  // read. A room read and refilled before that tick retains the previous set.
  if(pending_.empty()){reset();return {};}
  ++ticks_;std::set<Identity> current;bool hasNew=false,hasMine=false;
  for(const auto& [key,room]:pending_){
   // Original has_mine is deliberately evaluated before the disnot filter.
   hasMine=hasMine||room.mention;
   if(!room.disabled){Identity id{key,room.epoch};hasNew=hasNew||!previous_.contains(id);current.insert(std::move(id));}
  }
  ChatNotificationPulse pulse;
  if(!current.empty()){
   pulse.sound=hasNew&&settings.soundEnabled;
   pulse.icon=settings.flashEnabled&&(ticks_%2==0)?ChatNotificationIcon::Dim
       :hasMine?ChatNotificationIcon::Mention:ChatNotificationIcon::Message;
  }
  previous_=std::move(current);icon_=pulse.icon;return pulse;
 }
 bool running()const noexcept{return running_;}
 ChatNotificationIcon icon()const noexcept{return icon_;}
private:
 using Identity=std::pair<std::string,std::uint64_t>;
 std::uint64_t generation_=0;std::uint32_t ticks_=0;bool running_=false;
 ChatNotificationIcon icon_=ChatNotificationIcon::Idle;
 std::map<std::string,ChatPendingNotification> pending_;std::set<Identity> previous_;
};
}
