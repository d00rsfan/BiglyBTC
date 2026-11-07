// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_document.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <set>
namespace biglybtc::search {
namespace {
using E=MetasearchDocumentError;
struct Failure {E error;};
void require(bool ok,E error){if(!ok){throw Failure{error};}}
// Exact qualified WebResult value helpers, reused without modifying RSS.
QString trim(QString value){qsizetype a=0,b=value.size();while(a<b&&value[a].unicode()<=32){++a;}while(b>a&&value[b-1].unicode()<=32){--b;}return value.mid(a,b-a);}
bool ascii(QStringView value){return std::ranges::all_of(value,[](QChar c){return c.unicode()<128;});}
QString plain(QString value){
  value.replace(QRegularExpression(QStringLiteral("(<(/?[^>]+)>)")),QStringLiteral(" "));
  value.replace(QRegularExpression(QStringLiteral("[ \\t\\n\\x0b\\f\\r]{2,}")),QStringLiteral(" "));
  return trim(value);
}
struct Entity {const char* name;unsigned value;};
constexpr Entity Entities[]={
#include "rss_html40.inc"
};
std::optional<QString> html(QString value){
  QString out;out.reserve(value.size());
  for(qsizetype i=0;i<value.size();++i){
    if(value[i]!=u'&'){out+=value[i];continue;}
    const auto semi=value.indexOf(u';',i+1);if(semi<0){out+=value[i];continue;}
    const auto name=value.mid(i+1,semi-i-1);if(name.isEmpty()){return {};}
    int number=-1;
    if(name.startsWith(u'#')){
      auto digits=name.mid(1);int base=10;if(digits.startsWith(u'x')||digits.startsWith(u'X')){base=16;digits.remove(0,1);}
      const QRegularExpression syntax(base==16?QStringLiteral("^[+-]?[0-9a-fA-F]+$"):QStringLiteral("^[+-]?[0-9]+$"));
      bool ok=false;if(syntax.match(digits).hasMatch()){number=digits.toInt(&ok,base);}if(!ok){number=-1;}
    }else if(ascii(name)){
      const auto key=name.toLatin1();const auto found=std::lower_bound(std::begin(Entities),std::end(Entities),key,
          [](const Entity& a,const QByteArray& b){return QByteArrayView(a.name)<QByteArrayView(b);});
      if(found!=std::end(Entities)&&key==found->name){number=static_cast<int>(found->value);}
    }
    if(number==-1){out+=u'&';out+=name;out+=u';';}else{out+=QChar(static_cast<char16_t>(static_cast<unsigned>(number)));}
    i=semi;
  }
  return out;
}
QString xmlUnescape(QString value){
  // Original UrlUtils replacement order is observable for &amp;gt;.
  for(const auto& pair:std::array<std::pair<QString,QString>,5>{{{QStringLiteral("&amp;"),QStringLiteral("&")},{QStringLiteral("&gt;"),QStringLiteral(">")},{QStringLiteral("&lt;"),QStringLiteral("<")},{QStringLiteral("&quot;"),QStringLiteral("\"")},{QStringLiteral("&apos;"),QStringLiteral("'")}}}){value.replace(pair.first,pair.second);}
  return value;
}
std::optional<std::int32_t> integer(QString value){
  if(!ascii(value)||!QRegularExpression(QStringLiteral("^[+-]?[0-9]+$")).match(value).hasMatch()){return {};}
  bool ok=false;const auto number=value.toInt(&ok,10);return ok?std::optional<std::int32_t>{number}:std::nullopt;
}
std::optional<std::int64_t> size(QString value){
  auto decoded=html(plain(value));if(!decoded){return {};}
  value=*decoded;value.replace(QChar(160),u' ');if(!ascii(value)){return {};}
  value.replace(QRegularExpression(QStringLiteral("<[^>]+>")),QStringLiteral(" "));
  const auto split=QRegularExpression(QStringLiteral("([0-9])([a-zA-Z])")).match(value);if(split.hasMatch()){value.insert(split.capturedStart(2),u' ');}
  const auto parts=value.split(u' ',Qt::SkipEmptyParts);if(parts.isEmpty()){return {};}
  const auto token=trim(parts[0]);bool ok=false;double base=0;
  if(token==QStringLiteral("NaN")){base=std::numeric_limits<double>::quiet_NaN();ok=true;}
  else if(token==QStringLiteral("Infinity")||token==QStringLiteral("+Infinity")){base=std::numeric_limits<double>::infinity();ok=true;}
  else if(token==QStringLiteral("-Infinity")){base=-std::numeric_limits<double>::infinity();ok=true;}
  else if(QRegularExpression(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?[fFdD]?$" )).match(token).hasMatch()){
    auto number=token;if(number.endsWith(u'f')||number.endsWith(u'F')||number.endsWith(u'd')||number.endsWith(u'D')){number.chop(1);}base=number.toDouble(&ok);
  }
  if(!ok){return {};}
  const auto unit=parts.size()>1?parts[1].toLower():QStringLiteral("b");std::uint64_t multiplier=1;
  const auto prefix=QStringLiteral("kmgtpe").indexOf(unit.isEmpty()?QChar{}:unit[0]);
  if(prefix>=0&&(unit.size()==1||unit.mid(1)==QStringLiteral("b")||unit.mid(1)==QStringLiteral("ib"))){for(qsizetype n=0;n<=prefix;++n){multiplier*=1024;}}
  const double result=base*static_cast<double>(multiplier);
  if(std::isnan(result)){return 0;}if(result>=static_cast<double>(std::numeric_limits<std::int64_t>::max())){return std::numeric_limits<std::int64_t>::max();}
  if(result<=static_cast<double>(std::numeric_limits<std::int64_t>::min())){return std::numeric_limits<std::int64_t>::min();}return static_cast<std::int64_t>(result);
}
QString base32(const QByteArray& bytes){
  constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";QString out;unsigned pending=0,bits=0;
  for(const auto raw:bytes){pending=(pending<<8)|static_cast<unsigned char>(raw);bits+=8;while(bits>=5){bits-=5;out+=QLatin1Char(alphabet[(pending>>bits)&31]);}}
  if(bits){out+=QLatin1Char(alphabet[(pending<<(5-bits))&31]);}return out;
}
bool canonical32(QStringView value){return ascii(value)&&value.size()==32&&std::ranges::all_of(value,[](QChar c){const auto u=c.toUpper().unicode();return (u>='A'&&u<='Z')||(u>='2'&&u<='7');});}
std::optional<QString> normalizedHash(QString value){
  value=trim(value);if(value.size()==32){return value;}
  if(value.size()!=40||!QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{40}$")).match(value).hasMatch()){return {};}
  return base32(QByteArray::fromHex(value.toLatin1()));
}
QString reconstruct(QString value,const RssContext& context){
  const auto lower=value.toLower();for(const auto prefix:{"http://","https://","tor:http://","tor:https://","i2p:http://","i2p:https://","azplug:","magnet:","bc:","bctp:","dht:"}){if(lower.startsWith(QLatin1StringView(prefix))){return value;}}
  return (value.startsWith(u'/')?context.rootPage:context.basePage)+value;
}
enum class Kind {Object,Array,String,Number,Boolean,Null};
struct JsonNode {Kind kind=Kind::Null;QString scalar;std::map<QString,std::size_t> fields;std::vector<std::size_t> items;std::size_t begin=0,end=0;};
struct Json {
  const QByteArray& bytes;std::size_t at=0,text=0;std::vector<JsonNode> nodes;
  explicit Json(const QByteArray& input):bytes(input){}
  void space(){while(at<static_cast<std::size_t>(bytes.size())&&(bytes[static_cast<qsizetype>(at)]==' '||bytes[static_cast<qsizetype>(at)]=='\t'||bytes[static_cast<qsizetype>(at)]=='\n'||bytes[static_cast<qsizetype>(at)]=='\r')){++at;}}
  QString string(){
    const auto start=at;require(bytes[static_cast<qsizetype>(at++)]=='"',E::Format);
    while(at<static_cast<std::size_t>(bytes.size())){const auto c=bytes[static_cast<qsizetype>(at++)];if(c=='\\'){require(at<static_cast<std::size_t>(bytes.size()),E::Format);++at;}else if(c=='"'){break;}}
    require(at>start&&bytes[static_cast<qsizetype>(at-1)]=='"',E::Format);const auto decoded=QJsonDocument::fromJson("["+bytes.mid(static_cast<qsizetype>(start),static_cast<qsizetype>(at-start))+"]");require(decoded.isArray()&&decoded.array().size()==1&&decoded.array()[0].isString(),E::Format);auto value=decoded.array()[0].toString();
    for(qsizetype n=0;n<value.size();++n){if(value[n].isHighSurrogate()){require(n+1<value.size()&&value[n+1].isLowSurrogate(),E::Unsupported);++n;}else{require(!value[n].isLowSurrogate(),E::Unsupported);}}
    require(value.size()<=65536&&static_cast<std::size_t>(value.size())<=2*1024*1024-text,E::Limit);text+=static_cast<std::size_t>(value.size());return value;
  }
  std::size_t value(unsigned depth=0){
    require(depth<=64&&nodes.size()<32768,E::Limit);space();require(at<static_cast<std::size_t>(bytes.size()),E::Format);const auto index=nodes.size();nodes.emplace_back();nodes[index].begin=at;const auto token=bytes[static_cast<qsizetype>(at)];
    if(token=='{'||token=='['){nodes[index].kind=token=='{'?Kind::Object:Kind::Array;++at;space();const char closing=token=='{'?'}':']';
      while(at<static_cast<std::size_t>(bytes.size())&&bytes[static_cast<qsizetype>(at)]!=closing){QString key;if(token=='{'){key=string();space();require(bytes[static_cast<qsizetype>(at++)]==':',E::Format);}const auto child=value(depth+1);if(token=='{'){require(nodes[index].fields.emplace(std::move(key),child).second,E::Format);}else{nodes[index].items.push_back(child);}space();if(at<static_cast<std::size_t>(bytes.size())&&bytes[static_cast<qsizetype>(at)]==','){++at;space();}else{break;}}
      require(at<static_cast<std::size_t>(bytes.size())&&bytes[static_cast<qsizetype>(at++)]==closing,E::Format);
    }else if(token=='"'){nodes[index].kind=Kind::String;nodes[index].scalar=string();}
    else{const auto start=at;while(at<static_cast<std::size_t>(bytes.size())&&!QByteArrayView(",]} \t\r\n").contains(bytes[static_cast<qsizetype>(at)])){++at;}const auto lexeme=bytes.mid(static_cast<qsizetype>(start),static_cast<qsizetype>(at-start));nodes[index].scalar=QString::fromLatin1(lexeme);nodes[index].kind=lexeme=="null"?Kind::Null:lexeme=="true"||lexeme=="false"?Kind::Boolean:Kind::Number;
      if(nodes[index].kind==Kind::Number&&!lexeme.contains('.')&&!lexeme.contains('e')&&!lexeme.contains('E')){std::int64_t integer=0;const auto parsed=std::from_chars(lexeme.constData(),lexeme.constData()+lexeme.size(),integer);require(parsed.ec==std::errc{}&&parsed.ptr==lexeme.constData()+lexeme.size(),E::Unsupported);}}
    nodes[index].end=at;return index;
  }
};
std::optional<QString> btih(QString text){
  if(!text.contains(QStringLiteral("magnet:"),Qt::CaseInsensitive)){return {};}const auto match=QRegularExpression(QStringLiteral("(?i)(?:^|[?&])xt=urn:btih:([a-z0-9]+)(?:&|$)")).match(text);if(!match.hasMatch()){return {};}auto value=normalizedHash(match.captured(1));return value&&canonical32(*value)?value:std::nullopt;
}
std::optional<std::string> owned(const std::optional<QString>& s){return s?std::optional{s->toUtf8().toStdString()}:std::nullopt;}
#include "metasearch_advanced_document.inc"
MetasearchRowValues projectedRow(const std::vector<MappedValue>& values,const RssContext& context){
  MetasearchRowValues out;std::optional<QString> title,date,details,torrent,button,hash;
  for(const auto& mapped:values){const auto& mapping=*mapped.mapping;const auto& content=mapped.content;const bool decimal=mapped.decimal;
    if(decimal&&mapping.field!=3&&mapping.field!=4&&mapping.field!=5){out.incomplete=true;continue;}
    const auto setHash=[&](QString value){hash=normalizedHash(std::move(value));if(hash){QByteArray raw;unsigned bits=0,pending=0;constexpr auto alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";bool valid=canonical32(*hash);
      if(valid){for(const auto c:*hash){const auto index=QByteArrayView(alphabet).indexOf(static_cast<char>(c.toUpper().unicode()));pending=(pending<<5)|static_cast<unsigned>(index);bits+=5;if(bits>=8){bits-=8;raw+=static_cast<char>((pending>>bits)&255);}}}
      if(!valid){out.incomplete=true;return;}const auto magnet=QStringLiteral("magnet:?xt=urn:btih:")+QString::fromLatin1(raw.toHex().toUpper());if(!torrent){torrent=magnet;}if(!button){button=magnet;}}};
    switch(mapping.field){
      case 1:title=html(plain(content));if(!title){out.incomplete=true;}break;
      case 2:date=content;break; // Raw publication text only; no epoch/age claim.
      case 3:if(const auto value=size(content);value&&*value>=0){out.size=*value;}break;
      case 4:case 5:{if(decimal){break;}auto decoded=html(plain(content));if(!decoded){out.incomplete=true;break;}decoded->remove(u',');decoded->remove(u' ');if(!ascii(*decoded)){out.incomplete=true;break;}if(const auto value=integer(*decoded)){if(mapping.field==4){out.peers=*value;}else{out.seeds=*value;}}break;}
      case 102:torrent=xmlUnescape(content);break;
      case 103:details=xmlUnescape(content);break;
      case 105:button=xmlUnescape(content);break;
      case 200:if(content.startsWith(QStringLiteral("magnet:"))){if(const auto value=btih(content)){setHash(*value);}else if(content.contains(QStringLiteral("urn:btmh:"),Qt::CaseInsensitive)){out.incomplete=true;}}else{setHash(content);}break;
      default:out.incomplete=true;break;
    }
  }
  if(!hash&&torrent){if(const auto value=btih(reconstruct(*torrent,context))){hash=value;if(!button){button=QStringLiteral("magnet:?xt=urn:btih:")+*hash;}}}
  out.title=owned(title);out.publicationText=owned(date);out.hash=owned(hash);out.rawTorrentLink=owned(torrent);
  if(details){out.detailsLink=reconstruct(*details,context).toUtf8().toStdString();}if(torrent){out.torrentLink=reconstruct(*torrent,context).toUtf8().toStdString();}
  if(button){out.downloadButtonLink=reconstruct(*button,context).toUtf8().toStdString();}else{out.downloadButtonLink=out.torrentLink;}return out;
}
std::optional<MetasearchRowValues> jsonRow(const Json& tree,const JsonNode& entry,const MetasearchRecipe& recipe,const RssContext& context,std::string_view query,PatternBudget& budget){
  std::vector<MappedValue> values;std::set<std::int32_t> mapped;bool incomplete=false;
  const auto get=[&](QStringView name)->std::optional<QString>{const auto found=entry.fields.find(name.toString());if(found==entry.fields.end()){return QString{};}const auto& node=tree.nodes[found->second];if(node.kind==Kind::Null){return QString{};}if(node.kind==Kind::Object||node.kind==Kind::Array){incomplete=true;return {};}return node.scalar;};
  for(const auto& mapping:recipe.mappings){
    QString content;bool decimal=false;if(mapping.name.find("${")!=mapping.name.npos){content=mappedExpression(mapping.name,get,budget);}else{const auto value=get(QString::fromStdString(mapping.name));if(!value){continue;}content=*value;const auto found=entry.fields.find(QString::fromStdString(mapping.name));if(found!=entry.fields.end()){const auto& node=tree.nodes[found->second];decimal=node.kind==Kind::Number&&(content.contains('.')||content.contains('e')||content.contains('E'));}}
    if(content.isEmpty()&&mapped.contains(mapping.field)){continue;}if(!postFilter(mapping,content,query,budget)){return {};}
    if(!content.isEmpty()){mapped.insert(mapping.field);}values.push_back({&mapping,std::move(content),decimal});
  }auto row=projectedRow(values,context);row.incomplete=row.incomplete||incomplete;return row;
}
void bound(const MetasearchRowValues& row,std::size_t& text){
  for(const auto* field:{&row.title,&row.publicationText,&row.detailsLink,&row.torrentLink,&row.downloadButtonLink,&row.hash,&row.uid,&row.rawTorrentLink}){if(*field){require((*field)->size()<=65536&&(*field)->size()<=2*1024*1024-text,E::Limit);text+=(*field)->size();}}
}
} // namespace
std::expected<MetasearchDocument,E> parseMetasearchDocument(const MetasearchRecipe& recipe,const QByteArray& bytes,const RssContext& configured,std::string_view actualQueryUtf8){try{
  require(bytes.size()<=4*1024*1024,E::Limit);require(!bytes.contains('\0'),E::Encoding);const auto unicode=QString::fromUtf8(bytes);require(unicode.toUtf8()==bytes,E::Encoding);RssContext context=configured;
  // Same bounded Original base element behavior as the owned RSS path. The
  // redirected response URL is never substituted for configured root/base.
  const auto base=QRegularExpression(QStringLiteral("<base.*?href=\"([^\"]+)\".*?>"),QRegularExpression::CaseInsensitiveOption).match(unicode);
  if(base.hasMatch()){context.basePage=base.captured(1);}
  MetasearchDocument out;std::size_t text=0;PatternBudget budget;
  if(recipe.kind==MetasearchRecipeKind::Rss){
    const auto declaration=QRegularExpression(QStringLiteral("^\\s*<\\?xml[^>]*encoding\\s*=\\s*['\"]([^'\"]+)['\"]"),QRegularExpression::CaseInsensitiveOption).match(unicode);if(declaration.hasMatch()){const auto encoding=declaration.captured(1).toLower();require(encoding=="utf-8"||encoding=="utf8"||encoding=="us-ascii",E::Encoding);}
    auto parsed=parseRssDocument(bytes,context);require(bool(parsed),E::Format);out.partial=!parsed->issues.empty();for(const auto& row:parsed->results){require(out.rows.size()<1000,E::Limit);MetasearchRowValues value;value.title=owned(row.title);value.uid=owned(row.uid);value.publicationText=owned(row.publicationText);value.detailsLink=owned(row.detailsLink);value.torrentLink=owned(row.torrentLink);value.rawTorrentLink=value.torrentLink;value.downloadButtonLink=value.torrentLink;value.hash=owned(row.hash);value.size=row.size.value_or(-1);value.seeds=row.seeds.value_or(-1);value.peers=row.peers.value_or(-1);value.incomplete=out.partial;bound(value,text);out.rows.push_back(std::move(value));}return out;
  }
  if(recipe.kind==MetasearchRecipeKind::Regex){
    patternSubject(unicode);const auto syntax=inspectMetasearchPattern(recipe.resultPattern);require(bool(syntax)&&!syntax->nullable,E::Unsupported);
    const auto patternText=QString::fromStdString(recipe.resultPattern);
    for(unsigned pass=0;pass<2&&out.rows.empty();++pass){
      const auto flags=pass?QRegularExpression::DotMatchesEverythingOption|QRegularExpression::MultilineOption:QRegularExpression::NoPatternOption;const auto& pattern=compiledPattern(budget,patternText,flags);qsizetype offset=0;std::size_t matches=0;
      while(const auto found=pattern.find(unicode,offset,budget)){
        require(++matches<=1024&&found->capturedEnd()>found->capturedStart(),E::Limit);offset=found->capturedEnd();std::vector<MappedValue> values;bool accepted=true;
        const auto get=[&](QStringView key)->std::optional<QString>{bool ok=false;const auto group=key.toInt(&ok);require(ok&&group>0&&group<=pattern.expression.captureCount(),E::Unsupported);if(found->capturedStart(group)<0){return {};}return found->captured(group);};
        for(const auto& mapping:recipe.mappings){std::optional<QString> content;if(mapping.name.find("${")!=mapping.name.npos){content=mappedExpression(mapping.name,get,budget);}else{content=get(QString::fromStdString(mapping.name));}if(!content){continue;}if(!postFilter(mapping,*content,actualQueryUtf8,budget)){accepted=false;break;}values.push_back({&mapping,std::move(*content),false});}
        // Intentional correction: a failed post-filter rejects the whole row;
        // a later mapped hash cannot resurrect it as Original RegexEngine can.
        if(accepted&&!values.empty()){require(out.rows.size()<1000,E::Limit);auto row=projectedRow(values,context);bound(row,text);out.partial=out.partial||row.incomplete;out.rows.push_back(std::move(row));}
      }
    }return out;
  }
  if(bytes.trimmed()=="null"){return out;}QJsonParseError error;const auto checked=QJsonDocument::fromJson(bytes,&error);require(error.error==QJsonParseError::NoError&&(checked.isArray()||checked.isObject()),E::Format);
  Json tree{bytes};auto root=tree.value();tree.space();require(tree.at==static_cast<std::size_t>(bytes.size()),E::Format);
  if(!recipe.resultPath.empty()){
    const auto parts=QString::fromStdString(recipe.resultPath).split('.');if(tree.nodes[root].kind==Kind::Array&&!parts.front().startsWith('[')&&tree.nodes[root].items.size()==1){root=tree.nodes[root].items.front();}
    for(qsizetype n=0;n<parts.size();++n){const auto& part=parts[n];const auto& node=tree.nodes[root];if(part.startsWith('[')&&part.endsWith(']')){bool ok=false;const auto index=part.mid(1,part.size()-2).toULongLong(&ok);require(ok&&node.kind==Kind::Array&&index<node.items.size(),E::Format);root=node.items[static_cast<std::size_t>(index)];}else{require(node.kind==Kind::Object,E::Format);const auto next=node.fields.find(part);if(next==node.fields.end()){require(n+1==parts.size(),E::Format);return out;}root=next->second;}}
  }
  if(tree.nodes[root].kind==Kind::Null){return out;}
  require(tree.nodes[root].kind==Kind::Array,E::Format);
  for(const auto item:tree.nodes[root].items){if(tree.nodes[item].kind!=Kind::Object){continue;}require(out.rows.size()<1000,E::Limit);auto row=jsonRow(tree,tree.nodes[item],recipe,context,actualQueryUtf8,budget);if(row){bound(*row,text);out.partial=out.partial||row->incomplete;out.rows.push_back(std::move(*row));}}return out;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
} // namespace biglybtc::search
