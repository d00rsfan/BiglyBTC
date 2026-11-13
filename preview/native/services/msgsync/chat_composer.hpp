// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QStringList>
#include <QRegularExpression>
namespace biglybtc::services::msgsync::composer {
inline constexpr qsizetype MaximumInputCharacters=2048;
inline QString trimChatInput(QString value){qsizetype first=0,last=value.size();while(first<last&&value[first].unicode()<=32){++first;}while(last>first&&value[last-1].unicode()<=32){--last;}return value.mid(first,last-first);}
inline QString chatMention(QStringView existing,qsizetype caret,QString name){
 name.replace(QStringLiteral(" "),QStringLiteral("\\u00a0"));name.prepend(QLatin1Char('@'));if(caret>0&&existing[caret-1]!=QLatin1Char(' ')){name.prepend(QLatin1Char(' '));}if(caret<existing.size()&&existing[caret]!=QLatin1Char(' ')){name.append(QLatin1Char(' '));}return name;
}
// Selected original UI transformation. The actual signed payload byte limit is
// enforced separately by Room; this routine does not grant send authority.
inline QStringList prepareChatText(QString text,bool chunking=true,bool decodeEscapes=true){
 static const QRegularExpression unicode(QStringLiteral(R"((?i)\\u([\dabcdef]{4}))"));auto matches=unicode.globalMatch(decodeEscapes?text:QString{});QString decoded;qsizetype cursor=0;while(matches.hasNext()){const auto match=matches.next();decoded+=text.mid(cursor,match.capturedStart()-cursor);decoded+=QChar(ushort(match.captured(1).toUInt(nullptr,16)));cursor=match.capturedEnd();}decoded+=text.mid(cursor);text=std::move(decoded);bool hacked=false;QStringList out;
 if(chunking&&text.size()>500){
  static const QRegularExpression emphasis(QStringLiteral(R"((?i)([\*_]{1,2}|(?:[<\[]([bin])[>\]]))([^\n]+?)(\1|(?:[<\[]/\2[>\]])))"));auto found=emphasis.globalMatch(text);QString protectedText;cursor=0;while(found.hasNext()){const auto match=found.next();protectedText+=text.mid(cursor,match.capturedStart()-cursor);auto body=match.captured(3);if(body.contains(u' ')){body.replace(QStringLiteral(" "),QStringLiteral("\\u00a0"));hacked=true;}protectedText+=match.captured(1)+body+match.captured(4);cursor=match.capturedEnd();}protectedText+=text.mid(cursor);text=std::move(protectedText);
  const auto append=[&](QString value){if(hacked){value.replace(QStringLiteral("\\u00a0"),QStringLiteral(" "));}if(!value.isEmpty()){out.push_back(std::move(value));}};
  while(text.size()>400){qsizetype pos=399;while(pos>200&&text[pos]!=u' '){--pos;}if(pos<=200){pos=400;}append(trimChatInput(text.left(pos)));text=trimChatInput(text.mid(pos));}
 }
 if(hacked){text.replace(QStringLiteral("\\u00a0"),QStringLiteral(" "));}if(!text.isEmpty()){out.push_back(std::move(text));}return out;
}
// Keep a valid UTF-16 pair intact at the legacy character-chunk boundary before
// each independent UTF-8 conversion. The wire byte cap is still Room's authority.
inline QStringList chatDeliveryParts(QString text){
 auto parts=prepareChatText(std::move(text),true,false);for(qsizetype i=0;i+1<parts.size();++i){if(!parts[i].isEmpty()&&!parts[i+1].isEmpty()&&parts[i].back().isHighSurrogate()&&parts[i+1].front().isLowSurrogate()){parts[i+1].prepend(parts[i].back());parts[i].chop(1);}}return parts;
}
struct ChatInputHistory {
 QStringList messages;int position=-1;QString buffered;
 void submitted(QString text){text=trimChatInput(std::move(text));if(text.isEmpty()||text.toUtf8().size()>16384){return;}messages.prepend(std::move(text));if(messages.size()>32){messages.removeLast();}position=-1;buffered.clear();}
 QString up(const QString& current){++position;if(position<messages.size()){if(position==0){buffered=trimChatInput(current);}return messages[position];}position=int(messages.size())-1;return current;}
 QString down(const QString& current){--position;if(position>=0){return messages[position];}if(position==-1){auto value=buffered;buffered.clear();return value;}position=-1;return current;}
};
}
