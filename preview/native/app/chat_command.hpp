// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../services/msgsync/chat_composer.hpp"
namespace biglybtc::app {
enum class ChatCommandKind {Text,Action,Join,Nickname,Ignore,Help,Clone,Missing,Unsupported,Private};
struct ParsedChatCommand {ChatCommandKind kind=ChatCommandKind::Text;QString value;bool remove=false;QString privateText={};};
inline bool chatCommandSpace(QChar c){return c==u' '||c==u'\t'||c==u'\n'||c==u'\r'||c==u'\f'||c==u'\v';}
inline ParsedChatCommand parseChatCommand(const QString& message){
 for(const auto* diagnostic:{"!dump!","!sort!","!flood!","!spongy!","!ftux!"}){if(message==QLatin1StringView(diagnostic)){return {ChatCommandKind::Unsupported,{}};}}
 if(!message.startsWith(u'/')){return {ChatCommandKind::Text,message};}qsizetype first=0;while(first<message.size()&&!chatCommandSpace(message[first])){++first;}const auto command=message.left(first).toLower();qsizetype rest=first;while(rest<message.size()&&chatCommandSpace(message[rest])){++rest;}const bool hasParameter=first<message.size();const auto value=message.mid(rest);
 if(command==QStringLiteral("/help")){return {ChatCommandKind::Help,{}};}if(command==QStringLiteral("/clone")){return {ChatCommandKind::Clone,{}};}
 const auto parameter=[&](ChatCommandKind kind){return ParsedChatCommand{hasParameter?kind:ChatCommandKind::Missing,value};};
 if(command==QStringLiteral("/join")||command==QStringLiteral("/pjoin")){auto result=parameter(ChatCommandKind::Join);if(result.value.startsWith(u'"')&&result.value.endsWith(u'"')){if(result.value.size()<2){return ParsedChatCommand{ChatCommandKind::Missing,{}};}result.value=result.value.mid(1,result.value.size()-2);}return result;}
 if(command==QStringLiteral("/nick")){return parameter(ChatCommandKind::Nickname);}if(command==QStringLiteral("/me")){auto result=parameter(ChatCommandKind::Action);result.value=services::msgsync::composer::trimChatInput(message.mid(3));return result;}
 if(command==QStringLiteral("/msg")||command==QStringLiteral("/query")){auto result=parameter(ChatCommandKind::Private);qsizetype end=rest;while(end<message.size()&&!chatCommandSpace(message[end])){++end;}result.value=message.mid(rest,end-rest);result.privateText=services::msgsync::composer::trimChatInput(message.mid(end));return result;}
 if(command==QStringLiteral("/ignore")){auto result=parameter(ChatCommandKind::Ignore);qsizetype end=rest;while(end<message.size()&&!chatCommandSpace(message[end])){++end;}result.value=message.mid(rest,end-rest);if(result.value==QStringLiteral("-r")&&end<message.size()){while(end<message.size()&&chatCommandSpace(message[end])){++end;}result.value=message.mid(end);result.remove=true;}return result;}
 return {ChatCommandKind::Unsupported,{}};
}
}
