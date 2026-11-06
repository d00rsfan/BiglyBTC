// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_values.hpp"
#include "subscription_numeric_filter.hpp"
#include "subscription_literal_filter.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringConverter>
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>

namespace biglybtc::search {
namespace {
using V=SubscriptionValueError;
constexpr core::Limits Limits{4*1024*1024,4*1024*1024,100000,64};
std::string bs(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string bi(std::int64_t n){return "i"+std::to_string(n)+"e";}
std::string hash10(std::string_view s){return QCryptographicHash::hash(QByteArray(s.data(),static_cast<qsizetype>(s.size())),QCryptographicHash::Sha1).first(10).toStdString();}
bool unique(const core::Node& n){
  if(n.type==core::Node::Type::Dictionary){std::set<std::string_view> keys;if(n.children.size()%2){return false;}for(std::size_t i=0;i<n.children.size();i+=2){if(n.children[i].type!=core::Node::Type::Bytes||!keys.insert(n.children[i].bytes).second){return false;}}}
  return std::ranges::all_of(n.children,[](const auto& p){return unique(p);});
}
std::expected<std::shared_ptr<const core::Document>,V> document(std::string bytes){auto d=core::Document::parse(std::move(bytes),Limits);if(!d||d->root().type!=core::Node::Type::Dictionary||!unique(d->root())){return std::unexpected(V::Malformed);}return std::make_shared<const core::Document>(std::move(*d));}
std::optional<std::int64_t> number(const core::Node& n,std::string_view key,std::int64_t fallback){const auto* p=n.find(key);return !p?std::optional{fallback}:p->type==core::Node::Type::Integer?std::optional{p->integer}:std::nullopt;}
std::optional<std::string> bytes(const core::Node& n,std::string_view key,std::size_t cap=4096){const auto* p=n.find(key);if(!p||p->type!=core::Node::Type::Bytes||p->bytes.size()>cap){return {};}return std::string(p->bytes);}
bool empty(const core::Node& n,std::string_view key){const auto* p=n.find(key);return !p||(p->type==core::Node::Type::Bytes&&p->bytes.empty())||(p->type==core::Node::Type::List&&p->children.empty());}
std::string encoded(const core::Node& n){const auto s=core::encodeValue(n,Limits);return s?*s:std::string{};}
std::expected<std::string,V> replace(std::string original,const std::map<std::string,std::string>& fields){auto d=core::Document::parse(std::move(original),Limits);if(!d){return std::unexpected(V::Malformed);}for(const auto& [k,v]:fields){if(!d->replaceValue(k,v)){return std::unexpected(V::Limit);}}return d->serialize();}
std::expected<std::string,V> withoutField(std::string original,std::string_view field){auto d=core::Document::parse(std::move(original),Limits);if(!d||d->root().type!=core::Node::Type::Dictionary){return std::unexpected(V::Malformed);}std::string out="d";const auto& children=d->root().children;for(std::size_t i=0;i<children.size();i+=2){if(children[i].bytes!=field){out+=d->originalBytes().substr(children[i].begin,children[i+1].end-children[i].begin);}}return out+"e";}
std::expected<std::string,V> tombstone(std::string original,std::int64_t day){auto result=replace(std::move(original),{{"deleted",bi(1)},{"dls",bi(day)}});if(!result){return std::unexpected(result.error());}for(const auto* name:{"result_json","tz"}){result=withoutField(std::move(*result),name);if(!result){return std::unexpected(result.error());}}return result;}
// Strict bounded JSON envelope. Qt supplies value semantics; this scanner also
// rejects duplicate decoded keys and retains exact top-level value slices so
// unknown JSON fields survive updates without being reformatted.
struct Json {
  std::string raw;QJsonObject value;
  std::map<std::string,std::pair<std::size_t,std::size_t>> fields,members;
  static std::expected<Json,V> parse(std::string raw){
    if(raw.size()>Limits.inputBytes){return std::unexpected(V::Limit);}QStringDecoder decoder(QStringDecoder::Utf8);const QString text=decoder.decode(QByteArray::fromStdString(raw));
    if(decoder.hasError()||text.contains(QChar(0))){return std::unexpected(V::Malformed);}
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(raw),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject()){return std::unexpected(V::Malformed);}
    Json out;out.raw=std::move(raw);out.value=doc.object();std::size_t at=0,nodes=0;
    const auto whitespace=[&]{while(at<out.raw.size()&&(out.raw[at]==' '||out.raw[at]=='\n'||out.raw[at]=='\r'||out.raw[at]=='\t')){++at;}};
    const auto string=[&]()->std::string_view{const auto begin=at++;while(at<out.raw.size()){if(out.raw[at]=='\\'){at+=2;}else if(out.raw[at++]=='"'){return std::string_view(out.raw).substr(begin,at-begin);}}return {};};
    const auto walk=[&](auto&& self,unsigned depth)->bool{
      whitespace();if(depth>64||++nodes>100000||at>=out.raw.size()){return false;}
      if(out.raw[at]=='{'){
        ++at;whitespace();std::set<QString> keys;if(at<out.raw.size()&&out.raw[at]=='}'){++at;return true;}
        for(;;){whitespace();const auto memberBegin=at;if(at>=out.raw.size()||out.raw[at]!='"'){return false;}const auto token=string();if(token.empty()){return false;}
          const auto keyDoc=QJsonDocument::fromJson("["+QByteArray(token.data(),static_cast<qsizetype>(token.size()))+"]");if(keyDoc.isNull()||!keyDoc.isArray()){return false;}const auto key=keyDoc.array()[0].toString();if(!keys.insert(key).second){return false;}
          whitespace();if(at>=out.raw.size()||out.raw[at++]!=':'){return false;}whitespace();const auto begin=at;if(!self(self,depth+1)){return false;}if(depth==0){out.fields.emplace(key.toUtf8().toStdString(),std::pair{begin,at});out.members.emplace(key.toUtf8().toStdString(),std::pair{memberBegin,at});}whitespace();
          if(at>=out.raw.size()){return false;}const auto end=out.raw[at++];if(end=='}'){return true;}if(end!=','){return false;}
        }
      }
      if(out.raw[at]=='['){++at;whitespace();if(at<out.raw.size()&&out.raw[at]==']'){++at;return true;}for(;;){if(!self(self,depth+1)){return false;}whitespace();if(at>=out.raw.size()){return false;}const auto c=out.raw[at++];if(c==']'){return true;}if(c!=','){return false;}}}
      if(out.raw[at]=='"'){return !string().empty();}
      const auto begin=at;while(at<out.raw.size()&&out.raw[at]!=','&&out.raw[at]!='}'&&out.raw[at]!=']'&&out.raw[at]!=' '&&out.raw[at]!='\n'&&out.raw[at]!='\t'&&out.raw[at]!='\r'){++at;}return at>begin;
    };
    if(!walk(walk,0)){return std::unexpected(V::Ambiguous);}whitespace();if(at!=out.raw.size()){return std::unexpected(V::Malformed);}return out;
  }
  std::string slice(std::string_view key)const{const auto p=fields.find(std::string(key));return p==fields.end()?std::string{}:raw.substr(p->second.first,p->second.second-p->second.first);}
};
std::string quote(const QString& s){const auto value=QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);return value.mid(1,value.size()-2).toStdString();}
std::expected<std::int64_t,V> jsonInteger(const QJsonValue& value){
  if(!value.isDouble()){return std::unexpected(V::Malformed);}const auto lo=value.toInteger(std::numeric_limits<qint64>::min()),hi=value.toInteger(std::numeric_limits<qint64>::max());if(lo!=hi){return std::unexpected(V::Malformed);}return lo;
}
std::string jsonObject(const std::map<std::string,std::string>& fields){std::string s="{";for(const auto& [k,v]:fields){if(s.size()>1){s+=',';}s+=quote(QString::fromUtf8(k.data(),static_cast<qsizetype>(k.size())))+":"+v;}return s+"}";}
std::expected<std::string,V> mergeJson(const std::string& old,const std::map<std::string,std::string>& changed,const std::set<std::string>& removed={}){
  auto doc=Json::parse(old);if(!doc){return std::unexpected(doc.error());}std::map<std::size_t,std::string> order;for(const auto& [key,span]:doc->members){order.emplace(span.first,key);}std::string out="{";std::set<std::string> used;
  for(const auto& [begin,key]:order){if(removed.contains(key)){continue;}const auto span=doc->members.at(key);const auto value=doc->fields.at(key);if(out.size()>1){out+=',';}const auto replacement=changed.find(key);if(replacement==changed.end()){out.append(old,begin,span.second-begin);}else{out.append(old,begin,value.first-begin);out+=replacement->second;}used.insert(key);}
  for(const auto& [key,value]:changed){if(used.contains(key)){continue;}if(out.size()>1){out+=',';}out+=quote(QString::fromUtf8(key.data(),static_cast<qsizetype>(key.size())))+":"+value;}out+='}';if(out.size()>Limits.inputBytes){return std::unexpected(V::Limit);}return out;
}
std::expected<std::string,V> replaceRow(const core::Document& d,std::string_view listName,std::size_t index,const std::string& row){const auto* list=d.find(listName);if(!list||list->type!=core::Node::Type::List||index>=list->children.size()){return std::unexpected(V::Malformed);}std::string value="l";for(std::size_t i=0;i<list->children.size();++i){value+=i==index?row:encoded(list->children[i]);}value+='e';return replace(d.originalBytes(),{{std::string(listName),std::move(value)}});}
std::expected<SubscriptionHistoryCommit,V> commitValues(const SubscriptionIndex& index,std::size_t selected,const SubscriptionResults& prior,const std::vector<SubscriptionResultValue>& rows,std::optional<std::int64_t> scan,std::int64_t newest,std::int64_t now){
  const auto* list=index.source->find("subs");if(!list||selected>=list->children.size()){return std::unexpected(V::Malformed);}const auto& sub=list->children[selected];const auto* history=sub.find("history");
  std::vector<SubscriptionResultValue> finalized=rows,saved,aged;const auto day=now/86400000;for(auto& row:finalized){if(row.deleted&&row.deletedLastSeenDay==0){auto encoded=tombstone(row.encoded,day);if(!encoded){return std::unexpected(encoded.error());}row.encoded=std::move(*encoded);row.deletedLastSeenDay=day;}if(row.deleted&&row.deletedLastSeenDay>0&&day-row.deletedLastSeenDay>14){aged.push_back(std::move(row));}else{saved.push_back(std::move(row));}}if(aged.size()>10000){std::stable_sort(aged.begin(),aged.end(),[](const auto& a,const auto& b){return a.deletedLastSeenDay>b.deletedLastSeenDay;});aged.resize(5000);}for(auto& row:aged){saved.push_back(std::move(row));}
  std::int64_t read=0,unread=0;std::string resultList="l";for(const auto& row:saved){resultList+=row.encoded;if(!row.deleted){if(row.read){++read;}else{++unread;}}}resultList+='e';
  auto resultBytes=replace(prior.source->originalBytes(),{{"results",std::move(resultList)}});if(!resultBytes){return std::unexpected(resultBytes.error());}
  std::map<std::string,std::string> fields{{"num_read",bi(read)},{"num_unread",bi(unread)}};if(scan){fields["last_scan"]=bi(*scan);fields["last_new"]=bi(newest);}
  auto nextHistory=replace(history?encoded(*history):"de",fields);if(!nextHistory){return std::unexpected(nextHistory.error());}auto nextRow=replace(encoded(sub),{{"history",std::move(*nextHistory)}});if(!nextRow){return std::unexpected(nextRow.error());}auto config=replaceRow(*index.source,"subs",selected,*nextRow);if(!config){return std::unexpected(config.error());}
  return SubscriptionHistoryCommit{std::move(*resultBytes),std::move(*config),scan.value_or(0)};
}
std::expected<std::int64_t,V> publication(const RssDocument& doc,const RssResult& row){
  if(!row.publicationText||row.publicationText->isEmpty()){return std::int64_t{0};}
  const auto text=*row.publicationText;QDateTime date;
  if(doc.atom){if(!QRegularExpression(QStringLiteral("(?:Z|[+-][0-9]{2}:[0-9]{2})$")).match(text).hasMatch()){return std::unexpected(V::Unsupported);}date=QDateTime::fromString(text,Qt::ISODateWithMs);if(!date.isValid()){date=QDateTime::fromString(text,Qt::ISODate);}}
  else{if(!QRegularExpression(QStringLiteral("[+-][0-9]{4}$")).match(text).hasMatch()){return std::unexpected(V::Unsupported);}date=QDateTime::fromString(text,Qt::RFC2822Date);}
  if(!date.isValid()){return std::unexpected(V::Unsupported);}return date.toMSecsSinceEpoch();
}
} // namespace

std::expected<SubscriptionIndex,V> parseSubscriptionIndex(std::string raw){try{
  auto source=document(std::move(raw));if(!source){return std::unexpected(source.error());}const auto* list=(*source)->find("subs");if(!list||list->type!=core::Node::Type::List){return std::unexpected(V::Malformed);}if(list->children.size()>4096){return std::unexpected(V::Limit);}SubscriptionIndex out;out.source=*source;std::map<std::string,std::size_t> ids;
  for(std::size_t i=0;i<list->children.size();++i){const auto& node=list->children[i];SubscriptionSavedRow row;row.selected.sourceIndex=i;const auto pk=bytes(node,"public_key"),name=bytes(node,"name");const auto* singleton=node.find("sin_details");const auto singletonKey=singleton?bytes(*singleton,"key",2048):std::nullopt;
    // Index every usable path identity BEFORE checking version/name/support.
    // A malformed sibling can still collide with the same body/results stem.
    if(pk&&!pk->empty()){row.selected.publicKey=*pk;}if(singletonKey&&!singletonKey->empty()){row.selected.singletonKey=*singletonKey;}if(row.selected.singletonKey||!row.selected.publicKey.empty()){row.selected.shortId=hash10(row.selected.singletonKey.value_or(row.selected.publicKey));}
    bool collision=false;if(!row.selected.shortId.empty()){const auto [old,added]=ids.emplace(row.selected.shortId,i);if(!added){collision=true;out.rows[old->second].issue=V::Ambiguous;}}
    const auto version=number(node,"version",0),az=number(node,"az_version",1);if(node.type!=core::Node::Type::Dictionary||!pk||pk->empty()||!version||*version<1||*version>std::numeric_limits<std::int32_t>::max()||!az||*az!=1||!name||(singleton&&(!singletonKey||singletonKey->empty()))){row.issue=collision?V::Ambiguous:V::Malformed;out.rows.push_back(std::move(row));continue;}
    row.selected.version=*version;row.name=QString::fromUtf8(name->data(),static_cast<qsizetype>(name->size()));if(node.find("local_name")){const auto local=bytes(node,"local_name");if(!local){row.issue=V::Malformed;}else{row.name=QString::fromUtf8(local->data(),static_cast<qsizetype>(local->size()));}}
    const auto subscribed=number(node,"subscribed",0),anonymous=number(node,"is_anonymous",0);const auto* hist=node.find("history");const core::Node fallback(core::Node::Type::Dictionary);const auto& h=hist?*hist:fallback;const auto enabled=number(h,"enabled",1),automatic=number(h,"auto_dl",0),scan=number(h,"last_scan",0),last=number(h,"last_new",0),over=number(h,"interval_override",0),maximum=number(h,"max_results",-1),notification=number(h,"post_noti",0);
    if(!subscribed||!anonymous||!enabled||!automatic||!scan||!last||!over||!maximum||!notification||h.type!=core::Node::Type::Dictionary||(*subscribed!=0&&*subscribed!=1)||(*anonymous!=0&&*anonymous!=1)||(*enabled!=0&&*enabled!=1)||(*automatic!=0&&*automatic!=1)||*scan<0||*last<0){row.issue=V::Malformed;}
    else{row.subscribed=*subscribed==1;row.enabled=*enabled==1;row.autoDownload=*automatic==1;row.anonymous=*anonymous==1;row.lastScan=*scan;row.lastNew=*last;row.intervalOverride=*over;row.maxResults=*maximum;if(*notification!=0){row.issue=V::Unsupported;}}
    const auto readCount=number(h,"num_read",0),unreadCount=number(h,"num_unread",0);if(!readCount||!unreadCount||*readCount<0||*unreadCount<0){row.issue=V::Malformed;}else{row.readCount=*readCount;row.unreadCount=*unreadCount;}
    if(row.autoDownload||row.anonymous||!empty(node,"dep")||!empty(node,"eonr")){row.issue=V::Unsupported;}if(const auto* nets=h.find("nets");nets&&(nets->type!=core::Node::Type::Bytes||(!nets->bytes.empty()&&nets->bytes!="Public"))){row.issue=V::Unsupported;}if(const auto age=number(h,"max_age_secs",-1);!age||*age>0){row.issue=V::Unsupported;}if(collision){row.issue=V::Ambiguous;}out.rows.push_back(std::move(row));
  }return out;
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}

std::expected<SubscriptionDefinition,V> parseSubscriptionDefinition(std::string raw,const SubscriptionSavedRow& row,const core::Document* catalog){try{
  if(row.issue){return std::unexpected(*row.issue);}auto body=document(std::move(raw));if(!body){return std::unexpected(body.error());}const auto* vuze=(*body)->find("vuze");const auto* components=vuze?vuze->find("components"):nullptr;
  if(!components||components->type!=core::Node::Type::List||components->children.size()!=1){return std::unexpected(V::Malformed);}const auto& component=components->children[0];const auto type=number(component,"type",-1);const auto* content=component.find("content");const auto* details=content?content->find("details"):nullptr;
  if(!type||*type!=16||!details||details->type!=core::Node::Type::Dictionary||bytes(*details,"public_key")!=std::optional{row.selected.publicKey}||number(*details,"version",0)!=std::optional{row.selected.version}){return std::unexpected(V::Malformed);}
  const auto jsonBytes=bytes(*details,"json",Limits.inputBytes);if(!jsonBytes){return std::unexpected(V::Malformed);}auto json=Json::parse(*jsonBytes);if(!json){return std::unexpected(json.error());}auto engineId=jsonInteger(json->value.value("engine_id"));if(!engineId){return std::unexpected(engineId.error());}
  for(const auto* key:{"search_term","networks"}){const auto value=json->value.value(QLatin1String(key));if(!value.isUndefined()&&(!value.isString()||(!value.toString().isEmpty()&&(std::string_view(key)!="networks"||value.toString()!="Public")))){return std::unexpected(V::Unsupported);}}
  for(const auto* key:{"options"}){const auto v=json->value.value(QLatin1String(key));if(!v.isUndefined()&&(!v.isObject()||!v.toObject().isEmpty())){return std::unexpected(V::Unsupported);}}
  const auto schedule=json->value.value("schedule");if(!schedule.isObject()||schedule.toObject().isEmpty()){return std::unexpected(V::Unsupported);}auto interval=jsonInteger(schedule.toObject().value("interval"));if(!interval){return std::unexpected(interval.error());}
  SubscriptionDefinition out;out.engineId=*engineId;out.intervalMinutes=*interval;
  const auto filters=json->value.value("filters");auto policy=parseSubscriptionScanFilter(filters.isUndefined()?std::nullopt:std::optional{json->slice("filters")});if(!policy){return std::unexpected(policy.error());}out.numericFilter=std::make_shared<const SubscriptionNumericFilter>(std::move(policy->numeric));out.literalFilter=std::make_shared<const SubscriptionLiteralFilter>(std::move(policy->literal));
  if(catalog){const auto* engines=catalog->find("engines");if(!engines||engines->type!=core::Node::Type::List||!unique(catalog->root())){return std::unexpected(V::Malformed);}for(const auto& engine:engines->children){if(number(engine,"id",std::numeric_limits<std::int64_t>::min())==std::optional{*engineId}){if(out.engine){return std::unexpected(V::Ambiguous);}auto value=document(encoded(engine));if(!value){return std::unexpected(value.error());}out.engine=*value;}}}
  if(!out.engine){const auto engines=json->value.value("engines");const auto embedded=engines.toObject().value(QString::number(static_cast<qlonglong>(*engineId))).toObject().value("content");if(!engines.isObject()||!embedded.isString()){return std::unexpected(V::Unsupported);}const auto value=embedded.toString().toLatin1();const auto decoded=QByteArray::fromBase64Encoding(value,QByteArray::AbortOnBase64DecodingErrors);if(!decoded||decoded.decoded.toBase64()!=value){return std::unexpected(V::Malformed);}auto engine=document(decoded.decoded.toStdString());if(!engine){return std::unexpected(engine.error());}if(number((*engine)->root(),"id",0)!=std::optional{*engineId}){return std::unexpected(V::Malformed);}out.engine=*engine;out.embedded=true;}
  return out;
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}

std::expected<SubscriptionResults,V> parseSubscriptionResults(std::string raw){try{
  auto source=document(std::move(raw));if(!source){return std::unexpected(source.error());}const auto* list=(*source)->find("results");if(!list||list->type!=core::Node::Type::List){return std::unexpected(V::Malformed);}if(list->children.size()>16384){return std::unexpected(V::Limit);}SubscriptionResults out;out.source=*source;std::set<std::string> keys;
  for(const auto& n:list->children){SubscriptionResultValue row;const auto key=bytes(n,"key",10);const auto read=number(n,"read",-1);if(n.type!=core::Node::Type::Dictionary||!key||key->size()!=10||!read||(*read!=0&&*read!=1)||!keys.insert(*key).second){return std::unexpected(V::Ambiguous);}row.key=*key;row.read=*read==1;row.encoded=encoded(n);
    if(n.find("key2")){row.secondaryKey=bytes(n,"key2",10);if(!row.secondaryKey||row.secondaryKey->size()!=10){return std::unexpected(V::Malformed);}}
    row.deleted=n.find("deleted")!=nullptr;const auto day=number(n,"dls",0),time=number(n,"tz",0);if(!day||!time||*day<0){return std::unexpected(V::Malformed);}row.deletedLastSeenDay=*day;row.timeFoundSeconds=*time;
    if(!row.deleted){auto json=bytes(n,"result_json",Limits.inputBytes);if(!json||!Json::parse(*json)){return std::unexpected(V::Malformed);}row.json=std::move(*json);}out.rows.push_back(std::move(row));
  }return out;
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}

std::int64_t subscriptionNextScan(std::int64_t last,std::int64_t over,std::int64_t interval,std::int64_t fallback,std::int64_t now)noexcept{
  constexpr auto maximum=std::numeric_limits<std::int64_t>::max();const auto period=over>0?over:interval<=0?(fallback>0?fallback:120):interval;if((over<=0&&(period==std::numeric_limits<std::int32_t>::max()||period==maximum))||period>maximum/60000){return maximum;}if(last==0){return now;}return last>maximum-period*60000?maximum:last+period*60000;
}
std::int64_t subscriptionRetryTime(std::int64_t failed,unsigned count)noexcept{std::int64_t delay=600000;for(unsigned i=1;i<count&&delay<28800000;++i){delay=std::min<std::int64_t>(delay*2,28800000);}return failed>std::numeric_limits<std::int64_t>::max()-delay?std::numeric_limits<std::int64_t>::max():failed+delay;}

std::expected<SubscriptionHistoryCommit,V> reconcileSubscriptionResults(const SubscriptionIndex& index,std::size_t selected,const SubscriptionResults& prior,const RssDocument& incoming,std::int64_t engineId,std::int64_t now,const SubscriptionNumericFilter* numericFilter,const SubscriptionLiteralFilter* literalFilter){try{
  if(selected>=index.rows.size()||index.rows[selected].issue||now<0||incoming.results.size()>4096||!incoming.issues.empty()){return std::unexpected(V::Unsupported);}const auto& sub=index.rows[selected];auto rows=prior.rows;std::map<std::string,std::size_t> primary,secondary;for(std::size_t i=0;i<rows.size();++i){primary.emplace(rows[i].key,i);if(rows[i].secondaryKey&&!secondary.emplace(*rows[i].secondaryKey,i).second){return std::unexpected(V::Ambiguous);}}
  std::int64_t newest=sub.lastNew;const auto day=now/86400000;
  for(const auto& result:incoming.results){
    // Match the fresh WebResult getter phase before history reconciliation.
    // Saved-result normalization belongs to the separate acquisition phase.
    if(literalFilter){auto filtered=isSubscriptionLiteralFiltered(*literalFilter,result.title);if(!filtered){return std::unexpected(filtered.error());}if(*filtered){continue;}}
    if(numericFilter&&isSubscriptionNumericFiltered(*numericFilter,{result.title.has_value(),result.size.value_or(-1),result.seeds.value_or(-1),result.peers.value_or(-1)})){continue;}
    if(index.markLibraryRead&&result.hash&&!result.hash->isEmpty()){return std::unexpected(V::Unsupported);}const auto name=result.title.value_or(QString{});auto published=publication(incoming,result);if(!published){return std::unexpected(published.error());}const auto key=hash10(std::to_string(engineId)+":"+name.toUtf8().toStdString());std::optional<std::string> uid;if(result.uid&&!result.uid->isEmpty()){uid=hash10(std::to_string(engineId)+":"+result.uid->toUtf8().toStdString());}
    auto found=primary.find(key);std::optional<std::size_t> at;if(found!=primary.end()){at=found->second;}else if(uid){const auto second=secondary.find(*uid);if(second!=secondary.end()){at=second->second;}}
    if(at&&rows[*at].deleted){auto updated=replace(rows[*at].encoded,{{"dls",bi(day)}});if(!updated){return std::unexpected(updated.error());}rows[*at].encoded=std::move(*updated);rows[*at].deletedLastSeenDay=day;continue;}
    std::map<std::string,std::string> fields{{"n",quote(name)},{"tf",quote(QString::number(static_cast<qlonglong>(now/1000)))},{"ts",quote(QString::number(static_cast<qlonglong>(*published)))},{"d",quote(QStringLiteral("unknown"))},{"s",quote(QString::number(result.seeds&&*result.seeds>0?*result.seeds:-1))},{"p",quote(QString::number(result.peers.value_or(-1)))},{"lb",quote(QString::number(static_cast<qlonglong>(result.size.value_or(0))))}};
    if(result.torrentLink){fields["dl"]=quote(*result.torrentLink);}if(result.detailsLink){fields["cdp"]=quote(*result.detailsLink);}if(result.hash){fields["h"]=quote(*result.hash);}if(result.uid){fields["u"]=quote(*result.uid);}
    if(at){auto old=Json::parse(rows[*at].json);if(!old){return std::unexpected(old.error());}if(old->fields.contains("tf")){fields["tf"]=old->slice("tf");}if(old->fields.contains("ts")){auto oldTs=old->value.value("ts").toString().toLongLong();if(oldTs!=0&&(*published==0||oldTs<*published)){fields["ts"]=old->slice("ts");}}
      std::set<std::string> absent;for(const auto* key:{"u","h","dl","cdp"}){if(!fields.contains(key)){absent.insert(key);}}auto json=mergeJson(rows[*at].json,fields,absent);if(!json){return std::unexpected(json.error());}std::map<std::string,std::string> change{{"result_json",bs(*json)}};if(uid){change["key2"]=bs(*uid);}auto updated=replace(rows[*at].encoded,change);if(!updated){return std::unexpected(updated.error());}if(!uid){auto stripped=withoutField(std::move(*updated),"key2");if(!stripped){return std::unexpected(stripped.error());}*updated=std::move(*stripped);}rows[*at].encoded=std::move(*updated);rows[*at].json=std::move(*json);rows[*at].secondaryKey=uid;
    }else{if(rows.size()==16384){return std::unexpected(V::Limit);}SubscriptionResultValue row;row.key=key;row.secondaryKey=uid;row.read=sub.lastScan==0;row.timeFoundSeconds=now/1000;row.json=jsonObject(fields);std::map<std::string,std::string> bfields{{"key",bs(key)},{"read",bi(row.read?1:0)},{"result_json",bs(row.json)},{"tz",bi(now/1000)}};if(uid){bfields["key2"]=bs(*uid);}row.encoded="d";for(const auto& [k,v]:bfields){row.encoded+=bs(k)+v;}row.encoded+='e';primary.emplace(key,rows.size());if(uid){secondary.emplace(*uid,rows.size());}rows.push_back(std::move(row));newest=now;}
  }
  if(sub.maxResults>0){auto live=std::ranges::count_if(rows,[](const auto& r){return !r.deleted;});for(auto& row:rows){if(live<=sub.maxResults){break;}if(!row.deleted){auto changed=tombstone(row.encoded,day);if(!changed){return std::unexpected(changed.error());}row.encoded=std::move(*changed);row.deleted=true;row.deletedLastSeenDay=day;--live;}}}
  return commitValues(index,selected,prior,rows,now,newest,now);
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}

std::expected<SubscriptionHistoryCommit,V> changeSubscriptionResultState(const SubscriptionIndex& index,std::size_t selected,const SubscriptionResults& prior,const std::vector<std::string>& keys,bool read,bool deleted,std::int64_t now){try{
  if(selected>=index.rows.size()||index.rows[selected].issue||keys.empty()||keys.size()>256||now<0){return std::unexpected(V::Unsupported);}std::set<std::string> exact(keys.begin(),keys.end());if(exact.size()!=keys.size()){return std::unexpected(V::Ambiguous);}auto rows=prior.rows;bool changed=false;
  for(auto& row:rows){if(!exact.erase(row.key)){continue;}if(!read&&!deleted&&index.markLibraryRead){auto json=Json::parse(row.json);if(!json){return std::unexpected(json.error());}const auto hash=json->value.value("h");if(hash.isString()&&!hash.toString().isEmpty()){return std::unexpected(V::Unsupported);}}if(row.deleted&&!deleted){return std::unexpected(V::Unsupported);}if((deleted&&row.deleted)||(!deleted&&row.read==read)){continue;}changed=true;auto value=deleted?tombstone(row.encoded,now/86400000):replace(row.encoded,{{"read",bi(read?1:0)}});if(!value){return std::unexpected(value.error());}row.encoded=std::move(*value);if(deleted){row.deleted=true;row.deletedLastSeenDay=now/86400000;row.json.clear();}else{row.read=read;}}if(!exact.empty()){return std::unexpected(V::Ambiguous);}if(!changed){return SubscriptionHistoryCommit{prior.source->originalBytes(),index.source->originalBytes(),0};}return commitValues(index,selected,prior,rows,std::nullopt,0,now);
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}
} // namespace biglybtc::search
