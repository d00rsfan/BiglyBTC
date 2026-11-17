// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_rate_limits.hpp>
#include <bit>
#include <utility>
namespace biglybtc::core {
std::int32_t normalizeSavedTagRateLimit(std::int64_t value)noexcept{
 const auto narrowed=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)));
 return narrowed<0?-1:narrowed;
}
std::expected<SavedTagRateLimits,TagRateLimitError> readSavedTagRateLimits(const Node& record)noexcept{
 if(record.type!=Node::Type::Dictionary){return std::unexpected(TagRateLimitError::Record);}
 const auto* config=record.find("c");if(!config){return SavedTagRateLimits{};}
 if(config->type!=Node::Type::Dictionary){return std::unexpected(TagRateLimitError::Configuration);}
 SavedTagRateLimits result;
 for(const auto& [key,out]:{std::pair<std::string_view,std::int32_t*>{"rl.up",&result.upload},{"rl.down",&result.download}}){
  if(const auto* value=config->find(key)){if(value->type!=Node::Type::Integer){return std::unexpected(TagRateLimitError::Rate);}*out=normalizeSavedTagRateLimit(value->integer);}
 }
 return result;
}
bool savedTagUnlimitedRateProperty(std::string_view key,const Node& value)noexcept{
 return (key=="rl.up"||key=="rl.down")&&value.type==Node::Type::Integer&&normalizeSavedTagRateLimit(value.integer)==0;
}
}
