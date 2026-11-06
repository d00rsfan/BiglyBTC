// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_literal_filter.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringConverter>
#include <algorithm>
#include <map>
#include <set>

namespace biglybtc::search {
namespace {
using V=SubscriptionValueError;
constexpr core::Limits Limits{65536,65536,100000,64};
#include "subscription_numeric_json.inc"
char lower(char c)noexcept{return c>='A'&&c<='Z'?static_cast<char>(c+('a'-'A')):c;}
bool literal(QChar c)noexcept{
  const auto value=c.unicode();return (value>='a'&&value<='z')||
      (value>='A'&&value<='Z')||(value>='0'&&value<='9')||value=='_'||value=='-';
}
std::expected<std::vector<std::string>,V> tokens(const QJsonValue& value){
  std::vector<std::string> out;
  // Original MapUtils.getMapString ignores non-String JSON values.
  if(!value.isString()){return out;}const auto text=value.toString();
  if(text.size()>4096){return std::unexpected(V::Limit);}
  qsizetype at=0;
  while(at<text.size()){
    // StringTokenizer(raw," ") splits ONLY ASCII U+0020, not tabs/NBSP.
    while(at<text.size()&&text[at]==QLatin1Char(' ')){++at;}
    if(at==text.size()){break;}const auto begin=at;
    while(at<text.size()&&text[at]!=QLatin1Char(' ')){
      if(!literal(text[at])){return std::unexpected(V::Unsupported);}++at;
    }
    if(at-begin>256||out.size()==128){return std::unexpected(V::Limit);}
    auto token=text.mid(begin,at-begin).toLatin1().toStdString();
    std::ranges::transform(token,token.begin(),lower);out.push_back(std::move(token));
  }return out;
}
}
std::expected<SubscriptionScanFilter,V> parseSubscriptionScanFilter(std::optional<std::string> raw){try{
  SubscriptionScanFilter out;out.literal.originalJson=raw;
  if(!raw){auto numeric=parseSubscriptionNumericFilter(std::nullopt);if(!numeric){return std::unexpected(numeric.error());}out.numeric=std::move(*numeric);return out;}
  auto trimmed=std::string_view(*raw);const auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
  while(!trimmed.empty()&&space(trimmed.front())){trimmed.remove_prefix(1);}while(!trimmed.empty()&&space(trimmed.back())){trimmed.remove_suffix(1);}
  if(trimmed=="null"){auto numeric=parseSubscriptionNumericFilter(raw);if(!numeric){return std::unexpected(numeric.error());}out.numeric=std::move(*numeric);return out;}
  auto object=Json::parse(*raw);if(!object){return std::unexpected(object.error());}if(object->fields.size()>256){return std::unexpected(V::Limit);}
  auto include=tokens(object->value.value("text_filter")),exclude=tokens(object->value.value("text_filter_out"));
  if(!include){return std::unexpected(include.error());}if(!exclude){return std::unexpected(exclude.error());}
  // Retain exact numeric lexemes (3 versus3.0/1e0, Unicode numeric strings).
  // This is a private parse input; actual signed JSON/body is never rewritten.
  std::string numericJson="{";bool first=true;
  for(const auto& [key,range]:object->members){
    if(key=="text_filter"||key=="text_filter_out"){continue;}
    if(!first){numericJson+=',';}first=false;
    numericJson.append(object->raw,range.first,range.second-range.first);
  }numericJson+='}';auto numeric=parseSubscriptionNumericFilter(std::move(numericJson));
  if(!numeric){return std::unexpected(numeric.error());}
  out.numeric=std::move(*numeric);out.numeric.originalJson=raw;
  out.literal.include=std::move(*include);out.literal.exclude=std::move(*exclude);return out;
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}
std::expected<bool,V> isSubscriptionLiteralFiltered(const SubscriptionLiteralFilter& filter,const std::optional<QString>& name){try{
  if(!name){return true;}if(!filter.active()){return false;}
  if(name->size()>65536){return std::unexpected(V::Limit);}
  if(std::ranges::any_of(*name,[](QChar c){return c.unicode()>127;})){return std::unexpected(V::Unsupported);}
  auto folded=name->toLatin1().toStdString();std::ranges::transform(folded,folded.begin(),lower);
  for(const auto& token:filter.include){if(folded.find(token)==std::string::npos){return true;}}
  for(const auto& token:filter.exclude){if(folded.find(token)!=std::string::npos){return true;}}
  return false;
}catch(const std::bad_alloc&){return std::unexpected(V::Memory);}}
} // namespace biglybtc::search
