// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_subscription_result_source.hpp"
#include "subscription_numeric_filter.hpp"
#include "subscription_literal_filter.hpp"
#include "subscription_values.hpp"
#include "subscription_association_values.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <thread>

namespace biglybtc::search {
namespace {
using E=SavedRssSourceError;
std::expected<std::optional<QString>,E> text(const QJsonObject& object,const char* key){
  const auto value=object.value(QLatin1String(key));if(value.isUndefined()||value.isNull()){return std::optional<QString>{};}if(!value.isString()){return std::unexpected(E::Malformed);}return std::optional{value.toString()};
}
// Same signed-decimal UTF-16 rules as the numeric codec; no trimming/coercion.
std::expected<std::int64_t,E> savedLong(const QString& value){
  if(value.isEmpty()){return std::unexpected(E::Malformed);}qsizetype at=0;bool negative=false;if(value[0]==QLatin1Char('-')||value[0]==QLatin1Char('+')){negative=value[0]==QLatin1Char('-');++at;}if(at==value.size()){return std::unexpected(E::Malformed);}
  const auto limit=negative?std::numeric_limits<std::int64_t>::min():-std::numeric_limits<std::int64_t>::max();const auto multiplyLimit=limit/10;std::int64_t result=0;
  for(;at<value.size();++at){const QChar c=value[at];if(c.category()!=QChar::Number_DecimalDigit||c.unicodeVersion()>QChar::Unicode_13_0){return std::unexpected(E::Malformed);}const int digit=c.digitValue();if(digit<0||digit>9||result<multiplyLimit){return std::unexpected(E::Malformed);}result*=10;if(result<limit+digit){return std::unexpected(E::Malformed);}result-=digit;}return negative?result:-result;
}
std::expected<std::int64_t,E> number(const QJsonObject& object,const char* key,std::int64_t fallback){auto value=text(object,key);if(!value){return std::unexpected(value.error());}if(!*value){return fallback;}return savedLong(**value);}
std::expected<detail::SubscriptionRawScrapeCounts,E> rawScrapeCounts(const QJsonObject& object){auto seeds=number(object,"s",-1),peers=number(object,"p",-1);if(!seeds){return std::unexpected(seeds.error());}if(!peers){return std::unexpected(peers.error());}const auto present=[&](const char* key){const auto item=object.value(QLatin1String(key));return !item.isUndefined()&&!item.isNull();};return detail::SubscriptionRawScrapeCounts{present("s")?std::optional<std::int64_t>{*seeds}:std::nullopt,present("p")?std::optional<std::int64_t>{*peers}:std::nullopt};}
std::int32_t savedCount(std::int64_t count){return count<0?0:std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(count));}
std::expected<void,E> propertyShapes(const QJsonObject& object){
  for(const auto* key:{"c","ct","cdp","l"}){if(!text(object,key)){return std::unexpected(E::Malformed);}}
  const auto tags=object.value("tgs");if(!tags.isUndefined()&&!tags.isNull()){if(!tags.isArray()){return std::unexpected(E::Malformed);}for(const auto& value:tags.toArray()){if(!value.isString()&&!value.isNull()){return std::unexpected(E::Malformed);}}}
  auto rank=text(object,"r");if(!rank){return std::unexpected(rank.error());}if(*rank){
    // Common decimal Float.parseFloat syntax only. Hex/NaN/infinity and other
    // unproved forms refuse, rather than issuing preparation from a partial map.
    const auto value=**rank;static const QRegularExpression decimalRank(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?[fFdD]?$"));if(!decimalRank.match(value).hasMatch()){return std::unexpected(E::Unsupported);}auto number=value;if(number.endsWith('f',Qt::CaseInsensitive)||number.endsWith('d',Qt::CaseInsensitive)){number.chop(1);}bool valid=false;const float parsed=number.toFloat(&valid);if(!valid||!std::isfinite(parsed)){return std::unexpected(E::Unsupported);}
  }return {};
}

std::expected<std::optional<std::array<std::byte,20>>,E> expectedHash(const QJsonObject& object){
  auto hash=text(object,"h");if(!hash){return std::unexpected(hash.error());}if(!*hash){return std::optional<std::array<std::byte,20>>{};}const auto raw=(**hash).toLatin1();if(raw.size()!=32||QString::fromLatin1(raw)!=**hash){return std::unexpected(E::Unsupported);}std::array<std::byte,20> result{};std::uint32_t pending=0;unsigned bits=0;std::size_t at=0;
  for(char c:raw){if(c>='a'&&c<='z'){c=static_cast<char>(c-'a'+'A');}const int value=c>='A'&&c<='Z'?c-'A':c>='2'&&c<='7'?c-'2'+26:-1;if(value<0){return std::unexpected(E::Malformed);}pending=(pending<<5)|static_cast<unsigned>(value);bits+=5;if(bits>=8){bits-=8;result[at++]=std::byte((pending>>bits)&255);}}
  if(at!=20||bits){return std::unexpected(E::Malformed);}return std::optional{result};
}
std::expected<bool,E> preparation(const core::Node& metadata){
  // These real Original prepareDownload effects have not been implemented here.
  if(metadata.find("cat")){return std::unexpected(E::Unsupported);}if(const auto* tag=metadata.find("tag")){if(tag->type!=core::Node::Type::Integer){return std::unexpected(E::Malformed);}if(tag->integer>=0){return std::unexpected(E::Unsupported);}}
  const auto* history=metadata.find("history");if(history&&history->find("nets")){return std::unexpected(E::Unsupported);}
  // Original defaults Referer to true; its actual engine root is supported,
  // without accepting browser/custom/auth headers from a caller.
  const auto* referer=history?history->find("dl_with_ref"):nullptr;if(!referer){return true;}if(referer->type!=core::Node::Type::Integer||(referer->integer!=0&&referer->integer!=1)){return std::unexpected(E::Malformed);}return referer->integer==1;
}
}
struct SavedSubscriptionResultSnapshot::Impl {
  SavedSubscriptionSourceSnapshot source;
  SubscriptionResultGetDescription description;
  explicit Impl(SavedSubscriptionSourceSnapshot value):source(std::move(value)){}
};
SavedSubscriptionResultSnapshot::SavedSubscriptionResultSnapshot(std::unique_ptr<Impl> value)noexcept:impl_(std::move(value)){}
SavedSubscriptionResultSnapshot::SavedSubscriptionResultSnapshot(SavedSubscriptionResultSnapshot&&)noexcept=default;
SavedSubscriptionResultSnapshot& SavedSubscriptionResultSnapshot::operator=(SavedSubscriptionResultSnapshot&&)noexcept=default;
SavedSubscriptionResultSnapshot::~SavedSubscriptionResultSnapshot()=default;
const SubscriptionResultGetDescription& SavedSubscriptionResultSnapshot::description()const noexcept{return impl_->description;}
const detail::SubscriptionAutomaticPolicyValues* SavedSubscriptionResultSnapshot::automaticPolicy()const noexcept{return impl_->source.automaticPolicy();}
std::expected<bool,E> SavedSubscriptionResultSnapshot::automaticResultFiltered(const SavedSubscriptionSourceSnapshot& source,const SubscriptionResultValue& row){try{
  if(!source.automaticPolicy()||!source.numericFilter()||!source.literalFilter()){return std::unexpected(E::Unsupported);}QJsonParseError error;const auto document=QJsonDocument::fromJson(QByteArray::fromStdString(row.json),&error);if(error.error!=QJsonParseError::NoError||!document.isObject()){return std::unexpected(E::Malformed);}const auto object=document.object();auto shape=propertyShapes(object);if(!shape){return std::unexpected(shape.error());}
  auto name=text(object,"n");auto size=number(object,"lb",0),seeds=number(object,"s",-1),peers=number(object,"p",-1);if(!name||!size||!seeds||!peers){return std::unexpected(E::Malformed);}auto literal=isSubscriptionLiteralFiltered(*source.literalFilter(),*name);if(!literal){return std::unexpected(literal.error()==SubscriptionValueError::Memory?E::Memory:literal.error()==SubscriptionValueError::Limit?E::Limit:E::Unsupported);}return *literal||isSubscriptionNumericFiltered(*source.numericFilter(),{name->has_value(),*size,savedCount(*seeds),savedCount(*peers)});
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<detail::SubscriptionRawScrapeCounts,E> SavedSubscriptionResultSnapshot::frozenRawScrapeCounts(std::string_view json){try{QJsonParseError error;const auto value=QJsonDocument::fromJson(QByteArray(json.data(),static_cast<qsizetype>(json.size())),&error);if(error.error!=QJsonParseError::NoError||!value.isObject()){return std::unexpected(E::Malformed);}auto shape=propertyShapes(value.object());if(!shape){return std::unexpected(shape.error());}return rawScrapeCounts(value.object());}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<std::string,E> SavedSubscriptionResultSnapshot::freezeAcquisitionSource(const storage::ProfileLease& lease,std::uint64_t session)const{if(!impl_){return std::unexpected(E::Lease);}return impl_->source.freezeAcquisitionSource(lease,session);}
std::expected<std::uint64_t,E> SavedSubscriptionResultSnapshot::current(const storage::ProfileLease& lease,std::uint64_t session)const{if(!impl_){return std::unexpected(E::Lease);}return impl_->source.current(lease,session);}
std::expected<SavedSubscriptionResultSnapshot,E> SavedSubscriptionResultSnapshot::read(const storage::ProfileLease& lease,const std::filesystem::path& profile,std::uint64_t session,SavedSubscriptionResultSelector selected){return readImpl(lease,profile,session,std::move(selected),Purpose::Manual);}
std::expected<SavedSubscriptionResultSnapshot,E> SavedSubscriptionResultSnapshot::readAutomatic(const storage::ProfileLease& lease,const std::filesystem::path& profile,std::uint64_t session,SavedSubscriptionResultSelector selected){return readImpl(lease,profile,session,std::move(selected),Purpose::Automatic);}
std::expected<SavedSubscriptionResultSnapshot,E> SavedSubscriptionResultSnapshot::readImpl(const storage::ProfileLease& lease,const std::filesystem::path& profile,std::uint64_t session,SavedSubscriptionResultSelector selected,Purpose purpose){try{
  if(selected.resultKey.size()!=10){return std::unexpected(E::Ambiguous);}auto parent=purpose==Purpose::Automatic?SavedSubscriptionSourceSnapshot::readAutomatic(lease,profile,session,selected.subscription):SavedSubscriptionSourceSnapshot::read(lease,profile,session,selected.subscription);if(!parent){return std::unexpected(parent.error());}
  if(!parent->metadata().subscribed||!parent->metadata().enabled){return std::unexpected(E::Unsupported);}auto supported=preparation(parent->selectedSourceRow());if(!supported){return std::unexpected(supported.error());}auto headers=parent->resultHeaderPolicy();if(!headers){return std::unexpected(headers.error());}
  const auto& rows=parent->results().rows;const auto found=std::ranges::find(rows,selected.resultKey,&SubscriptionResultValue::key);if(found==rows.end()||found->read||found->deleted){return std::unexpected(E::Unsupported);}
  // Parent already checked bounded UTF8/JSON and duplicate keys for this exact
  // owning saved row; this projection never serializes or edits that document.
  QJsonParseError error;const auto document=QJsonDocument::fromJson(QByteArray::fromStdString(found->json),&error);if(error.error!=QJsonParseError::NoError||!document.isObject()){return std::unexpected(E::Malformed);}const auto object=document.object();auto shape=propertyShapes(object);if(!shape){return std::unexpected(shape.error());}
  auto name=text(object,"n");auto size=number(object,"lb",0);auto seeds=number(object,"s",-1);auto peers=number(object,"p",-1);auto hash=expectedHash(object);if(!name){return std::unexpected(name.error());}if(!size){return std::unexpected(size.error());}if(!seeds){return std::unexpected(seeds.error());}if(!peers){return std::unexpected(peers.error());}if(!hash){return std::unexpected(hash.error());}if(*name&&(**name).size()>65536){return std::unexpected(E::Limit);}
  for(const auto* field:{"ts","gr","v","co","ad"}){auto parsed=number(object,field,0);if(!parsed){return std::unexpected(parsed.error());}}
  SavedSubscriptionResultValue value;value.name=(*name).value_or(QString{});value.size=*size;value.seeds=savedCount(*seeds);value.peers=savedCount(*peers);value.expectedV1Hash=std::move(*hash);
  auto rawCounts=rawScrapeCounts(object);if(!rawCounts){return std::unexpected(rawCounts.error());}value.rawScrapeCounts=std::move(*rawCounts);
  if(*seeds>=0&&*peers>=0){value.initialScrapeCache=static_cast<std::int64_t>(((static_cast<std::uint64_t>(*seeds)&0x00ffffffULL)<<32)|(static_cast<std::uint64_t>(*peers)&0x00ffffffULL));}
  auto associations=detail::parseSubscriptionAssociations(std::string(parent->selectedSourceBytes()));if(!associations){return std::unexpected(associations.error()==SubscriptionValueError::Memory?E::Memory:E::Malformed);}if(detail::subscriptionAssociationCapacity(*associations,value.expectedV1Hash)==detail::SubscriptionAssociationCapacity::OriginalRandomEvictionRequired){return std::unexpected(E::Unsupported);}
  if(!parent->literalFilter()){return std::unexpected(E::Unsupported);}auto literalFiltered=isSubscriptionLiteralFiltered(*parent->literalFilter(),*name);if(!literalFiltered){return std::unexpected(literalFiltered.error()==SubscriptionValueError::Memory?E::Memory:literalFiltered.error()==SubscriptionValueError::Limit?E::Limit:E::Unsupported);}if(*literalFiltered){return std::unexpected(E::Unsupported);}
  if(!*name||!parent->numericFilter()||isSubscriptionNumericFiltered(*parent->numericFilter(),{true,value.size,value.seeds,value.peers})){return std::unexpected(E::Unsupported);}
  // Original getDownloadLink: dbl first; magnet dbl chooses any nonnull dl;
  // null dbl falls back to dl. Empty/plugin/chosen magnet never tries pl/other.
  auto button=text(object,"dbl");auto download=text(object,"dl");if(!button||!download){return std::unexpected(E::Malformed);}std::optional<QString> link=*button;if(link&&link->startsWith(QLatin1String("magnet:"),Qt::CaseInsensitive)&&*download){link=*download;}if(!link){link=*download;}if(!link){return std::unexpected(E::Unsupported);}
  const auto raw=link->toLatin1();if(QString::fromLatin1(raw)!=*link){return std::unexpected(E::Unsupported);}auto route=SavedSubscriptionSourceSnapshot::resultUrlContext(std::string_view(raw.constData(),static_cast<std::size_t>(raw.size())));if(!route){return std::unexpected(route.error());}
  auto data=std::make_unique<Impl>(std::move(*parent));static_cast<RssGetDescription&>(data->description)=data->source.description();data->description.literalGetUrl=raw.toStdString();data->description.selected=std::move(selected);data->description.result=std::move(value);
  if(*supported){const auto& root=data->source.description().context.rootPage;if(!root.isEmpty()){const auto referer=root.toLatin1();if(QString::fromLatin1(referer)!=root||referer.size()>2048||!std::ranges::all_of(referer,[](unsigned char c){return c>=33&&c<=126;})){return std::unexpected(E::Unsupported);}data->description.referer=referer.toStdString();}}
  if(data->description.literalGetUrl.starts_with("https://")){auto trust=data->source.resultTrust(lease,session);if(!trust){return std::unexpected(trust.error());}data->description.trustAnchors=std::move(*trust);}
  else{data->description.trustAnchors.reset();}
  auto current=data->source.current(lease,session);if(!current){return std::unexpected(current.error());}return SavedSubscriptionResultSnapshot(std::move(data));
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
} // namespace biglybtc::search
