// SPDX-License-Identifier: GPL-2.0-or-later
// Original VCC/FeatureAvailability and DHTDBImpl startup survey gate.
#include "survey_policy.hpp"
#include <QChar>
#include <QString>
#include <bit>
#include <climits>
#include <set>
namespace biglybtc::services::azureus_db_runtime::survey {
namespace {
bool unique(const core::Node& node){
 if(node.type!=core::Node::Type::Dictionary||node.children.size()%2){return false;}std::set<std::string_view> keys;
 for(std::size_t i=0;i<node.children.size();i+=2){if(node.children[i].type!=core::Node::Type::Bytes||!keys.insert(node.children[i].bytes).second){return false;}}return true;
}
std::int64_t originalLong(std::string_view bytes){
 // Java UTF-8 new String retains an initial BOM, so parseLong rejects it.
 if(bytes.starts_with("\xef\xbb\xbf")){return 0;}const auto text=QString::fromUtf8(bytes.data(),qsizetype(bytes.size()));if(text.isEmpty()){return 0;}
 qsizetype at=0;bool negative=false;if(text[0]==QLatin1Char('-')||text[0]==QLatin1Char('+')){negative=text[0]==QLatin1Char('-');at=1;}if(at==text.size()){return 0;}
 const std::int64_t limit=negative?INT64_MIN:-INT64_MAX,multiplyMinimum=limit/10;std::int64_t result=0;
 for(;at<text.size();++at){const auto c=text[at];if(c.category()!=QChar::Number_DecimalDigit){return 0;}const auto digit=c.digitValue();if(digit<0||digit>9||result<multiplyMinimum){return 0;}result*=10;if(result<limit+digit){return 0;}result-=digit;}
 return negative?result:-result;
}
}
std::expected<std::int64_t,PolicyError> savedFeatureFlags(const core::Node& config){
 if(!unique(config)){return std::unexpected(PolicyError::Input);}const auto* cache=config.find("versioncheck.cache.v4");if(!cache){cache=config.find("versioncheck.cache.v6");}if(!cache){return 0;}if(!unique(*cache)){return std::unexpected(PolicyError::Input);}const auto* flags=cache->find("feat_flags");if(!flags){return 0;}if(flags->type!=core::Node::Type::Bytes){return std::unexpected(PolicyError::Input);}return originalLong(flags->bytes);
}
bool surveyEnabled(std::int8_t protocol,std::int32_t network,bool adapter,std::int64_t flags)noexcept{return protocol>=26&&(!adapter||network==1||(std::bit_cast<std::uint64_t>(flags)&0x20)==0);}
}
