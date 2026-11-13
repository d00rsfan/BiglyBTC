// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_composer.hpp"
#include <QUrl>
#include <QVector>
#include <bit>
#include <cstdint>
#include <optional>
namespace biglybtc::services::msgsync {
inline bool transcriptWhitespace(QChar value)noexcept{const auto c=value.unicode();return (c>=9&&c<=13)||(c>=0x1c&&c<=0x20)||c==0x1680||(c>=0x2000&&c<=0x2006)||(c>=0x2008&&c<=0x200a)||c==0x2028||c==0x2029||c==0x205f||c==0x3000;}
inline QString transcriptEncode(const QString& text){return QString::fromLatin1(QUrl::toPercentEncoding(text,"*","~"));}
inline std::optional<QString> transcriptDecode(QString text){
 const auto valid=[](QStringView value){for(qsizetype i=0;i<value.size();++i){if(value[i]!=u'%'){continue;}if(i+2>=value.size()){return false;}for(int n=1;n<=2;++n){const auto c=value[i+n].unicode();if(!((c>=u'0'&&c<=u'9')||(c>=u'a'&&c<=u'f')||(c>=u'A'&&c<=u'F'))){return false;}}i+=2;}return true;};
 if(!valid(text)){const auto last=text.lastIndexOf(u'%');if(last<0||last<text.size()-2){return {};}text.truncate(last);if(!valid(text)){return {};}}text.replace(u'+',u' ');return QUrl::fromPercentEncoding(text.toUtf8());
}
inline QString expandTranscriptEmphasis(const QString& text){
 static const QRegularExpression pattern(QStringLiteral(R"((?i)([\*_]{1,2}|(?:[<\[]([bin])[>\]]))([^\n]+?)(\1|(?:[<\[]/\2[>\]])))"));auto matches=pattern.globalMatch(text);QString out;qsizetype cursor=0;bool any=false;
 while(matches.hasNext()){any=true;const auto match=matches.next();const auto begin=match.capturedStart(1);const bool pad=begin>0&&!transcriptWhitespace(text[begin-1]);if(out.endsWith(QStringLiteral("]]"))){out+=QChar(0xfeff);}const auto token=match.captured(1).toLower();const auto type=token.contains(u'n')?QStringLiteral("normal"):token.size()==1||token.contains(u'i')?QStringLiteral("italic"):QStringLiteral("bold");out+=text.mid(cursor,match.capturedStart()-cursor);if(pad){out+=QChar(0xfeff);}out+=QStringLiteral("chat:")+type+QStringLiteral("[[")+transcriptEncode(match.captured(3))+QStringLiteral("]]");cursor=match.capturedEnd();}
 if(!any){return text;}if(out.endsWith(QStringLiteral("]]"))){out+=QChar(0xfeff);}out+=text.mid(cursor);return out;
}
struct TranscriptSelection {qsizetype start=0,end=0;bool operator==(const TranscriptSelection&)const=default;};
struct TranscriptSearch {
 QString term;std::int32_t index=0;
 TranscriptSelection apply(const QString& log,const QString& input){
  if(input.isEmpty()){term.clear();index=0;return {log.size(),log.size()};}
  if(input==QStringLiteral("\n")){if(!term.isEmpty()){index=std::bit_cast<std::int32_t>(std::uint32_t(index)+1);}}else{term=input.toLower();index=0;}
  return selection(log);
 }
 TranscriptSelection selection(const QString& log)const{
  const auto lowered=log.toLower();auto position=lowered.indexOf(term);if(position<0){return {log.size(),log.size()};}for(std::int32_t n=1;n<=index;++n){const auto next=lowered.indexOf(term,position+1);if(next<0){break;}position=next;}return {position,position+term.size()};
 }
};
enum class TranscriptStyle {Plain,Normal,Bold,Italic,Nickname,Link};
struct TranscriptSpan {QString text;TranscriptStyle style=TranscriptStyle::Plain;QString target={};bool operator==(const TranscriptSpan&)const=default;};
// Interpret only non-executable original chat formatting pseudo-URLs here.
// General URL/link tokenisation is a separate explicit presentation operation.
inline QVector<TranscriptSpan> formattedTranscriptSpans(const QString& text){
 static const QRegularExpression pattern(QStringLiteral(R"((?i)chat:(bold|italic|normal|nick)\[\[(.*?)\]\])"));auto matches=pattern.globalMatch(text);QVector<TranscriptSpan> spans;qsizetype cursor=0;const auto append=[&](QString value,TranscriptStyle style){if(value.isEmpty()){return;}if(!spans.empty()&&spans.back().style==style&&spans.back().target.isEmpty()){spans.back().text+=value;}else{spans.push_back({std::move(value),style});}};
 while(matches.hasNext()){const auto match=matches.next();if(match.capturedStart()>0&&text[match.capturedStart()-1].isLetterOrNumber()){continue;}auto decoded=transcriptDecode(composer::trimChatInput(match.captured(2)));if(!decoded){continue;}append(text.mid(cursor,match.capturedStart()-cursor),TranscriptStyle::Plain);const auto name=match.captured(1).toLower();append(std::move(*decoded),name==QStringLiteral("bold")?TranscriptStyle::Bold:name==QStringLiteral("italic")?TranscriptStyle::Italic:name==QStringLiteral("nick")?TranscriptStyle::Nickname:TranscriptStyle::Normal);cursor=match.capturedEnd();}append(text.mid(cursor),TranscriptStyle::Plain);return spans;
}
inline std::optional<QString> publicTranscriptRoom(const QString& raw){
 const QUrl url(raw,QUrl::StrictMode);if(!url.isValid()||url.scheme().toLower()!=QStringLiteral("chat")||!url.path().isEmpty()||!url.authority().isEmpty()||url.hasFragment()){return {};}const auto query=url.query(QUrl::FullyEncoded);if(query.isEmpty()||query.contains(u'&')||query.contains(u'=')){return {};}auto key=transcriptDecode(query);if(!key||key->isEmpty()||key->toUtf8().size()>4096){return {};}return key;
}
inline bool externalTranscriptLink(const QString& raw){
 const QUrl url(raw,QUrl::StrictMode);const auto scheme=url.scheme().toLower();return url.isValid()&&(scheme==QStringLiteral("https")||scheme==QStringLiteral("http"))&&!url.host().isEmpty();
}
inline QVector<TranscriptSpan> transcriptSpans(const QString& raw,std::int32_t origin=0){
 QVector<TranscriptSpan> result;const auto append=[&](QVector<TranscriptSpan> spans){for(auto& span:spans){if(!result.empty()&&result.back().style==span.style&&result.back().target==span.target){result.back().text+=span.text;}else{result.push_back(std::move(span));}}};
 const auto plain=[&](QString value){append(formattedTranscriptSpans(origin==0?expandTranscriptEmphasis(value):value));};
 qsizetype cursor=0,scan=0;while(scan<raw.size()){
  const auto colon=raw.indexOf(u':',scan);if(colon<0){break;}scan=colon+1;auto start=colon;while(start>0&&(raw[start-1].isLetter()||raw[start-1].category()==QChar::Number_DecimalDigit)){--start;}if(start==colon){continue;}const bool quoted=start>0&&raw[start-1]==u'"';if(quoted){--start;}auto end=colon+1;for(;end<raw.size();++end){const auto c=raw[end];if(QStringView(u"<>{}\\^~").contains(c)||c==QChar(0x60)){break;}if(quoted?c==u'"':c==QChar(0xfeff)||transcriptWhitespace(c)){if(quoted){++end;}break;}}
  if(end<=colon+1){continue;}auto token=raw.mid(start,end-start);if(quoted){if(!token.endsWith(u'"')||token.size()<2){continue;}token=token.mid(1,token.size()-2);}const auto lower=token.toLower();const bool formatting=lower.startsWith(QStringLiteral("chat:bold[["))||lower.startsWith(QStringLiteral("chat:italic[["))||lower.startsWith(QStringLiteral("chat:normal[["))||lower.startsWith(QStringLiteral("chat:nick[["));
  const auto scheme=raw.mid(start+(quoted?1:0),colon-start-(quoted?1:0)).toLower();const bool urlToken=formatting||scheme==QStringLiteral("http")||scheme==QStringLiteral("https")||scheme==QStringLiteral("chat")||scheme==QStringLiteral("ftp")||scheme==QStringLiteral("file")||scheme==QStringLiteral("magnet")||scheme==QStringLiteral("azplug")||scheme==QStringLiteral("javascript")||scheme==QStringLiteral("data");if(!urlToken){continue;}plain(raw.mid(cursor,start-cursor));
  if(formatting){append(formattedTranscriptSpans(token));}else{
   auto target=token;auto display=transcriptDecode(token).value_or(token);const auto label=token.lastIndexOf(QStringLiteral("[["));if(label>0&&token.endsWith(QStringLiteral("]]"))){target=token.left(label);auto description=transcriptDecode(composer::trimChatInput(token.mid(label+2,token.size()-label-4)));static const QRegularExpression looksUrl(QStringLiteral(R"((?i)(?:[a-z][a-z0-9+.-]*:|www\.))"));if(description&&!description->isEmpty()&&!looksUrl.match(*description).hasMatch()){display=*description;}else{display=transcriptDecode(target).value_or(target);}}
   if(externalTranscriptLink(target)||publicTranscriptRoom(target)){append({{std::move(display),TranscriptStyle::Link,std::move(target)}});}else{append({{raw.mid(start,end-start),TranscriptStyle::Plain}});}
  }cursor=end;scan=end;
 }plain(raw.mid(cursor));return result;
}

inline QVector<TranscriptSpan> highlightTranscriptNickname(QVector<TranscriptSpan> spans,QString nickname){
 nickname.replace(u' ',QChar(0xa0));if(nickname.isEmpty()){return spans;}QVector<TranscriptSpan> result;const auto word=[](QChar c){return c.isLetter()||c.category()==QChar::Number_DecimalDigit;};
 for(auto& span:spans){if(span.style==TranscriptStyle::Link){result.push_back(std::move(span));continue;}qsizetype scan=0,cursor=0;while(scan<span.text.size()){const auto found=span.text.indexOf(nickname,scan);if(found<0){break;}const auto end=found+nickname.size();scan=end;if((found>0&&word(span.text[found-1]))||(end<span.text.size()&&word(span.text[end]))){continue;}if(found>cursor){result.push_back({span.text.mid(cursor,found-cursor),span.style,span.target});}result.push_back({nickname,TranscriptStyle::Nickname});cursor=end;}if(cursor<span.text.size()){result.push_back({span.text.mid(cursor),span.style,span.target});}}
 return result;
}

}
