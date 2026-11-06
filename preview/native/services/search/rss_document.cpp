// SPDX-License-Identifier: GPL-2.0-or-later
#include "rss_document.hpp"
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
namespace biglybtc::search {
namespace {
struct Failure {RssError code;};
void require(bool value,RssError code){if(!value){throw Failure{code};}}
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
struct Node {QString local,full,value;std::vector<std::pair<QString,QString>> attrs;std::vector<std::size_t> children;};
struct Tree {
  std::vector<Node> nodes;
  const Node* child(const Node& n,QStringView name)const{for(const auto at:n.children){if(nodes[at].local.compare(name,Qt::CaseInsensitive)==0){return &nodes[at];}}return nullptr;}
};
const QString* attribute(const Node& n,QStringView name){for(const auto& pair:n.attrs){if(pair.first.compare(name,Qt::CaseInsensitive)==0){return &pair.second;}}return nullptr;}
Tree parse(const QByteArray& bytes,const RssLimits& limits){
  Tree tree;QXmlStreamReader xml(bytes);std::vector<std::size_t> stack;std::size_t count=0,text=0;
  const auto boundedText=[&](QStringView value){const auto n=static_cast<std::size_t>(value.size());require(n<=limits.fieldChars&&n<=limits.textChars-text,RssError::Limit);text+=n;};
  while(!xml.atEnd()){
    const auto token=xml.readNext();
    require(token!=QXmlStreamReader::DTD&&token!=QXmlStreamReader::EntityReference,RssError::ForbiddenDtd);
    if(token==QXmlStreamReader::StartElement){
      require(stack.size()<limits.depth&&count<limits.nodes,RssError::Limit);++count;
      Node node;node.local=xml.name().toString();node.full=xml.qualifiedName().toString();boundedText(node.full);
      for(const auto& a:xml.attributes()){
        require(count<limits.nodes,RssError::Limit);++count;boundedText(a.qualifiedName());boundedText(a.value());node.attrs.emplace_back(a.qualifiedName().toString(),a.value().toString());
      }
      const auto at=tree.nodes.size();if(!stack.empty()){tree.nodes[stack.back()].children.push_back(at);}tree.nodes.push_back(std::move(node));stack.push_back(at);
    }else if(token==QXmlStreamReader::Characters&&!stack.empty()){
      const auto value=xml.text();boundedText(value);auto& out=tree.nodes[stack.back()].value;require(static_cast<std::size_t>(out.size()+value.size())<=limits.fieldChars,RssError::Limit);out+=value;
    }else if(token==QXmlStreamReader::EndElement){require(!stack.empty(),RssError::Xml);stack.pop_back();}
  }
  require(!xml.hasError()&&!tree.nodes.empty()&&stack.empty(),RssError::Xml);return tree;
}
std::optional<std::int32_t> integer(QString value){
  if(!ascii(value)||!QRegularExpression(QStringLiteral("^[+-]?[0-9]+$")).match(value).hasMatch()){return {};}
  bool ok=false;const auto number=value.toInt(&ok,10);return ok?std::optional<std::int32_t>{number}:std::nullopt;
}
std::int32_t add32(std::int32_t a,std::int32_t b){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));}
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
bool absoluteUrl(QString value){
  // Validation only. Preserve original strings; Qt never resolves/rewrites it.
  if(!ascii(value)||value.contains(QRegularExpression(QStringLiteral("[\\x00-\\x20<>\\\\]")))){return false;}
  auto lower=value.toLower();for(const auto& prefix:{QStringLiteral("tor:"),QStringLiteral("i2p:")}){if(lower.startsWith(prefix)){value.remove(0,prefix.size());lower.remove(0,prefix.size());break;}}
  if(lower.startsWith(QStringLiteral("http://"))||lower.startsWith(QStringLiteral("https://"))){const QUrl url(value,QUrl::StrictMode);return url.isValid()&&!url.host().isEmpty();}
  return lower.startsWith(QStringLiteral("magnet:"))||lower.startsWith(QStringLiteral("bc:"))||lower.startsWith(QStringLiteral("bctp:"))||lower.startsWith(QStringLiteral("dht:"));
}
QString reconstruct(QString value,const RssContext& context){
  const auto lower=value.toLower();for(const auto prefix:{"http://","https://","tor:http://","tor:https://","i2p:http://","i2p:https://","azplug:","magnet:","bc:","bctp:","dht:"}){if(lower.startsWith(QLatin1StringView(prefix))){return value;}}
  return (value.startsWith(u'/')?context.rootPage:context.basePage)+value;
}
} // namespace
namespace {
struct Reader {
  const Tree& tree;const RssContext& context;const RssLimits& limits;RssDocument& output;
  std::size_t channel=0,item=0,outputChars=0;
  void issue(QString field,RssIssueCode code){require(output.issues.size()<limits.issues,RssError::Limit);output.issues.push_back({channel,item,std::move(field),code});}
  void count(std::optional<std::int32_t>& target,QString text,QString field){
    auto decoded=html(plain(text));if(!decoded){target.reset();issue(std::move(field),RssIssueCode::OriginalMalformedEntity);return;}
    text=*decoded;text.remove(u',');text.remove(u' ');
    if(!ascii(text)){target.reset();issue(std::move(field),RssIssueCode::UnsupportedNumber);return;}
    if(const auto value=integer(text)){target=value;}else{issue(std::move(field),RssIssueCode::InvalidNumber);}
  }
  void setSize(RssResult& result,const QString& text,std::int64_t minimum){
    if(const auto value=size(text);value&&*value>=minimum){result.size=value;}
    else if(!value){issue(QStringLiteral("size"),RssIssueCode::InvalidNumber);}
  }
  void push(RssResult result){
    require(output.results.size()<limits.items,RssError::Limit);
    for(const auto* text:{&result.title,&result.uid,&result.publicationText,&result.detailsLink,&result.torrentLink,&result.hash}){
      if(*text){const auto n=static_cast<std::size_t>((*text)->size());require(n<=limits.fieldChars&&n<=limits.textChars-outputChars,RssError::Limit);outputChars+=n;}
    }
    output.results.push_back(std::move(result));
  }
  void readItem(const Node& node,const Node& parent){
    RssResult result;result.channel=channel;result.item=item;
    const auto* title=tree.child(node,u"title");if(title){result.title=html(plain(title->value));if(!result.title){issue(QStringLiteral("title"),RssIssueCode::OriginalMalformedEntity);}}
    if(const auto* date=tree.child(node,output.atom?u"published":u"pubdate")){result.publicationText=date->value;}
    if(const auto* uid=tree.child(node,output.atom?u"id":u"guid");uid&&!trim(uid->value).isEmpty()){result.uid=trim(uid->value);}
    std::optional<QString> details,download,potentialDetails,itemHash,itemMagnet;bool gotPair=false,headUnknown=false,fallbackUnknown=false;
    std::optional<std::int32_t> seeds,peers,completed;
    const auto setLink=[&](QString value){download=xmlUnescape(std::move(value));headUnknown=false;};
    const auto reconstructedDownload=[&](){return download?reconstruct(*download,context):QString{};};
    const auto setHash=[&](const QString& value){
      result.hash=normalizedHash(value);
      if(!result.hash){issue(QStringLiteral("hash"),RssIssueCode::InvalidHash);}
      else if(!download&&canonical32(*result.hash)){download=QStringLiteral("magnet:?xt=urn:btih:")+result.hash->toUpper();}
    };
    if(const auto* link=tree.child(node,u"link")){
      QString value;
      if(output.atom){if(const auto* href=attribute(*link,u"href")){value=trim(*href);}}
      else{value=trim(link->value);}
      const auto* base=tree.child(parent,u"link");
      if(value.startsWith(QStringLiteral("//"))){value=(base&&base->value.toLower().startsWith(QStringLiteral("https"))?QStringLiteral("https:"):QStringLiteral("http:"))+value;}
      else if(value.startsWith(u'/')){if(base){value=base->value+(base->value.endsWith(u'/')?value.mid(1):value);}else{value.clear();}}
      if(!value.isEmpty()){
        if(absoluteUrl(value)){
          const auto lower=value.toLower();if(lower.startsWith(QStringLiteral("http"))||lower.startsWith(QStringLiteral("tor:http"))||lower.startsWith(QStringLiteral("i2p:http"))){details=xmlUnescape(value);}
        }else{issue(QStringLiteral("link"),RssIssueCode::UnsupportedUrl);}
      }
    }
    if(!details&&result.uid&&absoluteUrl(*result.uid)&&result.uid->toLower().startsWith(QStringLiteral("http"))){details=xmlUnescape(*result.uid);}
    const bool vuze=std::ranges::any_of(node.children,[&](auto n){return tree.nodes[n].full.startsWith(QStringLiteral("vuze:"),Qt::CaseInsensitive);});
    for(const auto index:node.children){
      const auto& child=tree.nodes[index];const auto local=child.local.toLower(),full=child.full.toLower();const auto value=trim(child.value);
      if(local==QStringLiteral("enclosure")){
        const auto* type=attribute(child,u"type");if(type&&type->compare(QStringLiteral("application/x-bittorrent"),Qt::CaseInsensitive)==0){
          if(const auto* url=attribute(child,u"url")){setLink(*url);}if(const auto* length=attribute(child,u"length")){setSize(result,*length,1024*1024);}
        }
      }else if(local==QStringLiteral("contentlength")){setSize(result,value,0);}
      else if(local==QStringLiteral("comments")){
        if(value.startsWith(QStringLiteral("http"))&&absoluteUrl(value)){potentialDetails=value;}
      }else if(local==QStringLiteral("link")||local==QStringLiteral("guid")){
        const auto lower=value.toLower();
        if(absoluteUrl(value)){
          if(lower.endsWith(QStringLiteral(".torrent"))||lower.startsWith(QStringLiteral("magnet:"))||lower.startsWith(QStringLiteral("bc:"))||lower.startsWith(QStringLiteral("bctp:"))||lower.startsWith(QStringLiteral("dht:"))){setLink(value);}
          else if(local==QStringLiteral("link")&&!vuze){headUnknown=true;issue(QStringLiteral("torrentLink"),RssIssueCode::UnavailableHead);}
        }else{
          const auto* type=attribute(child,u"type");const auto* href=attribute(child,u"href");
          if(type&&href&&type->compare(QStringLiteral("application/x-bittorrent"),Qt::CaseInsensitive)==0){
            if(absoluteUrl(trim(*href))){setLink(trim(*href));}else{issue(QStringLiteral("href"),RssIssueCode::UnsupportedUrl);}
          }
        }
      }else if(local==QStringLiteral("content")&&output.atom){
        const auto* src=attribute(child,u"src");const auto* type=attribute(child,u"type");
        if(src&&((type&&type->compare(QStringLiteral("application/x-bittorrent"),Qt::CaseInsensitive)==0)||src->contains(QStringLiteral(".torrent"),Qt::CaseInsensitive))){
          if(absoluteUrl(*src)){setLink(*src);}else{issue(QStringLiteral("content"),RssIssueCode::UnsupportedUrl);}
        }
      }else if(full==QStringLiteral("vuze:size")){setSize(result,value,0);}
      else if(full==QStringLiteral("vuze:seeds")){gotPair=true;count(result.seeds,value,QStringLiteral("seeds"));}
      else if(full==QStringLiteral("vuze:peers")){gotPair=true;count(result.peers,value,QStringLiteral("peers"));}
      else if(full==QStringLiteral("vuze:superseeds")){gotPair=true;}
      else if(full==QStringLiteral("vuze:downloadurl")){setLink(value);}
      else if(full==QStringLiteral("vuze:assethash")){setHash(value);}
      else if(local==QStringLiteral("seeds")||local==QStringLiteral("seeders")){if(const auto n=integer(value)){seeds=n;}}
      else if(local==QStringLiteral("peers")||local==QStringLiteral("leechers")){if(local==QStringLiteral("leechers")||!peers||*peers<0){if(const auto n=integer(value)){peers=n;}}}
      else if(local==QStringLiteral("grabs")||local==QStringLiteral("completed")){if(const auto n=integer(value)){completed=n;}}
      else if(local==QStringLiteral("infohash")||local==QStringLiteral("info_hash")||local==QStringLiteral("torrent_sha1")){itemHash=value;}
      else if(local==QStringLiteral("magneturi")){itemMagnet=value;}
    }
    if(!gotPair&&seeds&&peers&&*seeds>=0&&*peers>=0){result.seeds=seeds;result.peers=peers;gotPair=true;}
    // Description regex fallbacks are not replaced by approximate HTML parsing.
    // They remain an explicit gap even while exact structured fields are useful.
    const auto* description=tree.child(node,u"description");
    if(!gotPair&&description&&!trim(description->value).isEmpty()){issue(QStringLiteral("description heuristics"),RssIssueCode::UnsupportedFallback);}
    if(const auto* torrent=tree.child(node,u"torrent")){
      if(!result.size||*result.size<=0){if(const auto* length=tree.child(*torrent,u"contentLength")){
        bool ok=false;const auto raw=trim(length->value);const auto number=raw.toLongLong(&ok);if(ok&&ascii(raw)){setSize(result,QString::number(number)+QStringLiteral(" B"),0);}
      }}
      // RSSEngine keeps this local getDownloadLink() result, then assigns the
      // raw nested magnet text to it. Do not substitute the raw optional, or
      // reconstruct it again after an empty nested setter: basePage matters.
      auto nestedDownload=reconstructedDownload();
      if(nestedDownload.isEmpty()){if(const auto* magnet=tree.child(*torrent,u"magnetURI")){nestedDownload=trim(magnet->value);download=xmlUnescape(nestedDownload);}}
      if(!result.hash||result.hash->isEmpty()){if(const auto* hash=tree.child(*torrent,u"infoHash")){
        const auto value=trim(hash->value);setHash(value);
        if(nestedDownload.isEmpty()){if(const auto normalized=normalizedHash(value);normalized&&canonical32(*normalized)){
          // Conditional on the prior HEAD-dependent branch: retain uncertainty.
          download=QStringLiteral("magnet:?xt=urn:btih:")+normalized->toUpper();
        }}
      }}
      if(!gotPair){if(const auto* trackers=tree.child(*torrent,u"trackers")){
        std::int32_t maximum=-1,bestSeeds=0,bestPeers=0;bool valid=true;
        for(const auto group:trackers->children){for(const auto at:tree.nodes[group].children){const auto& tracker=tree.nodes[at];if(tracker.local.compare(QStringLiteral("tracker"),Qt::CaseInsensitive)!=0){continue;}
          const auto* a=attribute(tracker,u"seeds");const auto* b=attribute(tracker,u"peers");const auto s=a?integer(trim(*a)):std::optional<std::int32_t>{-1};const auto p=b?integer(trim(*b)):std::optional<std::int32_t>{-1};
          if(!s||!p){valid=false;break;}const auto total=add32(*s,*p);if(total>maximum){maximum=total;bestSeeds=*s;bestPeers=*p;}
        }if(!valid){break;}}
        if(valid&&maximum>=0){result.seeds=std::max(0,bestSeeds);result.peers=std::max(0,bestPeers);gotPair=true;}
        else if(!valid){issue(QStringLiteral("tracker counts"),RssIssueCode::InvalidNumber);}
      }}
    }
    for(const auto at:node.children){const auto& child=tree.nodes[at];if(child.local.compare(QStringLiteral("attr"),Qt::CaseInsensitive)!=0){continue;}
      const auto* name=attribute(child,u"name");const auto* value=attribute(child,u"value");if(!name||!value){continue;}
      if(*name==QStringLiteral("seeders")&&!gotPair){count(result.seeds,*value,QStringLiteral("seeds"));}
      else if(*name==QStringLiteral("peers")&&!gotPair){count(result.peers,*value,QStringLiteral("peers"));}
      else if(*name==QStringLiteral("infohash")&&!itemHash){itemHash=*value;}
      else if(*name==QStringLiteral("magneturl")&&!itemMagnet){itemMagnet=*value;}
    }
    if(!result.size||*result.size<=0){if(const auto* value=tree.child(node,u"size")){setSize(result,trim(value->value),0);}}
    if(itemHash&&!result.hash){setHash(*itemHash);}if(itemMagnet&&(!download||download->isEmpty())){download=xmlUnescape(*itemMagnet);}
    if(completed&&*completed>=0){result.completed=completed;}
    if(reconstructedDownload().isEmpty()&&result.title&&result.title->contains(QStringLiteral("magnet:"),Qt::CaseInsensitive)){fallbackUnknown=true;issue(QStringLiteral("title magnet fallback"),RssIssueCode::UnsupportedFallback);}
    if(reconstructedDownload().isEmpty()){download=details?reconstruct(*details,context):QString{};}
    const auto dlink=download?reconstruct(*download,context):QString{};const auto cdp=details?reconstruct(*details,context):QString{};
    // The final CDP replacement is conditional on CDP==download. A missing
    // HEAD (or deliberately unsupported title-magnet provider) can change that
    // equality, so do not export a guessed final detail URL either.
    std::optional<QString> cdpReplacement;
    if(result.uid&&absoluteUrl(*result.uid)&&result.uid->toLower().startsWith(QStringLiteral("http"))){cdpReplacement=xmlUnescape(*result.uid);}
    else if(potentialDetails){cdpReplacement=xmlUnescape(*potentialDetails);}
    const bool detailsUnknown=(headUnknown||fallbackUnknown)&&!cdp.isEmpty()&&cdpReplacement&&reconstruct(*cdpReplacement,context)!=cdp;
    if(cdp.isEmpty()||(!cdp.isEmpty()&&cdp==dlink)){
      if(result.uid&&absoluteUrl(*result.uid)&&result.uid->toLower().startsWith(QStringLiteral("http"))){details=xmlUnescape(*result.uid);potentialDetails.reset();}
      if(potentialDetails){details=xmlUnescape(*potentialDetails);}
    }
    result.detailsLink=details?std::optional<QString>{reconstruct(*details,context)}:std::nullopt;
    if(detailsUnknown){result.detailsLink.reset();issue(QStringLiteral("detailsLink"),headUnknown?RssIssueCode::UnavailableHead:RssIssueCode::UnsupportedFallback);}
    result.headUnavailable=headUnknown;result.torrentLink=headUnknown||fallbackUnknown?std::nullopt:std::optional<QString>{dlink};
    // A conditional nested magnet is not a hash fact until the effective
    // download choice is settled. Independent explicit hashes remain known.
    if(!result.hash&&!headUnknown&&!fallbackUnknown&&download&&download->startsWith(QStringLiteral("magnet:"),Qt::CaseInsensitive)){
      const auto found=QRegularExpression(QStringLiteral("xt=urn:(?:btih|sha1):([^&]+)"),QRegularExpression::CaseInsensitiveOption).match(*download);
      if(found.hasMatch()){
        const auto value=normalizedHash(found.captured(1));
        if(value&&canonical32(*value)){result.hash=value->toUpper();}
        else{issue(QStringLiteral("magnet hash extraction"),RssIssueCode::UnsupportedFallback);}
      }else if(download->contains(QStringLiteral("xt=urn:btmh:"),Qt::CaseInsensitive)){
        issue(QStringLiteral("multihash truncation"),RssIssueCode::UnsupportedFallback);
      }
    }
    push(std::move(result));
  }
};
} // namespace
std::expected<RssDocument,RssError> parseRssDocument(QByteArray bytes,RssContext context,RssLimits limits){try{
  const RssLimits maximum;
  require(limits.bytes<=maximum.bytes&&limits.nodes<=maximum.nodes&&limits.depth<=maximum.depth&&limits.items<=maximum.items&&limits.fieldChars<=maximum.fieldChars&&limits.textChars<=maximum.textChars&&limits.issues<=maximum.issues,RssError::Limit);
  require(static_cast<std::size_t>(bytes.size())<=limits.bytes&&static_cast<std::size_t>(context.rootPage.size())<=limits.fieldChars&&static_cast<std::size_t>(context.basePage.size())<=limits.fieldChars,RssError::Limit);
  const auto tree=parse(bytes,limits);RssDocument output;output.source=std::move(bytes);const auto& root=tree.nodes.front();output.atom=root.local.compare(QStringLiteral("feed"),Qt::CaseInsensitive)==0;Reader reader{tree,context,limits,output};
  std::vector<std::size_t> channels;if(output.atom){channels.push_back(0);}else{for(const auto at:root.children){if(tree.nodes[at].local.compare(QStringLiteral("channel"),Qt::CaseInsensitive)==0){channels.push_back(at);}}}
  for(const auto at:channels){const auto& channel=tree.nodes[at];reader.channel=output.channels.size();reader.item=0;RssChannel info;info.index=reader.channel;
    if(const auto* title=tree.child(channel,u"title")){info.title=title->value;}
    for(const auto child:channel.children){const auto& value=tree.nodes[child];if(value.full.compare(QStringLiteral("vuze:auto_dl_enabled"),Qt::CaseInsensitive)==0&&value.value.compare(QStringLiteral("true"),Qt::CaseInsensitive)!=0){info.declaredAutoDownload=false;}}
    output.channels.push_back(std::move(info));
    for(const auto child:channel.children){const auto& node=tree.nodes[child];if(node.local.compare(output.atom?QStringLiteral("entry"):QStringLiteral("item"),Qt::CaseInsensitive)==0){reader.readItem(node,channel);++reader.item;}}
  }
  return output;
}catch(const Failure& failure){return std::unexpected(failure.code);}catch(const std::bad_alloc&){return std::unexpected(RssError::Memory);}}
} // namespace biglybtc::search
