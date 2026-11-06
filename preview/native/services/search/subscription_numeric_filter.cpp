// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_numeric_filter.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringConverter>
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace biglybtc::search {
namespace {
using V=SubscriptionValueError;
constexpr core::Limits Limits{65536,65536,100000,64};
#include "subscription_numeric_json.inc"
// Java Long.parseLong uses signed decimal UTF-16 chars, ASCII +/- and no
// trimming. Accumulating negatively admits MIN_VALUE without signed overflow.
std::expected<std::int64_t,V> decimal(const QString& text){
  if(text.isEmpty()){return std::unexpected(V::Malformed);}qsizetype at=0;bool negative=false;
  if(text[0]==QLatin1Char('-')||text[0]==QLatin1Char('+')){negative=text[0]==QLatin1Char('-');++at;}
  if(at==text.size()){return std::unexpected(V::Malformed);}const auto limit=negative?std::numeric_limits<std::int64_t>::min():-std::numeric_limits<std::int64_t>::max();const auto multiplyLimit=limit/10;std::int64_t result=0;
  for(;at<text.size();++at){const QChar character=text[at];
    // JDK17 Character's Unicode13 repertoire, not Qt's newer digit additions.
    if(character.category()!=QChar::Number_DecimalDigit||character.unicodeVersion()>QChar::Unicode_13_0){return std::unexpected(V::Malformed);}const int digit=character.digitValue();
    if(digit<0||digit>9||result<multiplyLimit){return std::unexpected(V::Malformed);}result*=10;if(result<limit+digit){return std::unexpected(V::Malformed);}result-=digit;
  }return negative?result:-result;
}
std::expected<std::int64_t,V> importedLong(const Json& object,const char* key){
  const auto value=object.value.value(QLatin1String(key));if(value.isUndefined()){return std::int64_t{-1};}if(value.isString()){return decimal(value.toString());}
  const auto raw=object.slice(key);if(value.isDouble()&&raw.find_first_of(".eE")==std::string::npos){
    // JSON.simple produces Long only for integer syntax. A Double such as3.0
    // or1e0 remains ignored even when Qt can represent it as an integer.
    return decimal(QString::fromLatin1(raw.data(),static_cast<qsizetype>(raw.size())));
  }return std::int64_t{-1};
}
bool activeText(const QJsonValue& value){return value.isString()&&std::ranges::any_of(value.toString(),[](QChar c){return c!=QLatin1Char(' ');});}
}
bool SubscriptionNumericFilter::active()const noexcept{return minSize>=0||maxSize>=0||minSeeds>=0||maxSeeds>=0||minPeers>=0;}
std::expected<SubscriptionNumericFilter,V> parseSubscriptionNumericFilter(std::optional<std::string> raw){try{
  SubscriptionNumericFilter out;out.originalJson=std::move(raw);if(!out.originalJson){return out;}
  if(out.originalJson->size()>Limits.inputBytes){return std::unexpected(V::Limit);}auto trimmed=std::string_view(*out.originalJson);const auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};while(!trimmed.empty()&&space(trimmed.front())){trimmed.remove_prefix(1);}while(!trimmed.empty()&&space(trimmed.back())){trimmed.remove_suffix(1);}if(trimmed=="null"){return out;}
  auto object=Json::parse(*out.originalJson);if(!object){return std::unexpected(object.error());}if(object->fields.size()>256){return std::unexpected(V::Limit);}
  if(activeText(object->value.value("text_filter"))||activeText(object->value.value("text_filter_out"))||object->value.value("category").isString()){return std::unexpected(V::Unsupported);}
  const auto age=importedLong(*object,"max_age");if(!age){return std::unexpected(age.error());}if(*age>=0){return std::unexpected(V::Unsupported);}
  for(auto [key,target]:{std::pair{"min_size",&out.minSize},std::pair{"max_size",&out.maxSize},std::pair{"min_seeds",&out.minSeeds},std::pair{"max_seeds",&out.maxSeeds},std::pair{"min_peers",&out.minPeers}}){const auto value=importedLong(*object,key);if(!value){return std::unexpected(value.error());}*target=*value;}
  return out;
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}
bool isSubscriptionNumericFiltered(const SubscriptionNumericFilter& filter,const SubscriptionNumericResult& result)noexcept{
  return !result.namePresent||(filter.minSize>-1&&result.size<filter.minSize)||(filter.maxSize>-1&&result.size>filter.maxSize)
      ||(filter.minSeeds>-1&&result.seeds<filter.minSeeds)||(filter.maxSeeds>-1&&result.seeds>filter.maxSeeds)
      ||(filter.minPeers>-1&&result.peers<filter.minPeers);
}
} // namespace biglybtc::search
