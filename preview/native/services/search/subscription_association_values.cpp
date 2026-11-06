// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_association_values.hpp"
#include <algorithm>
#include <set>
namespace biglybtc::search::detail {
namespace {
using E=SubscriptionValueError;
constexpr core::Limits Limits{4*1024*1024,4*1024*1024,100000,64};
constexpr std::size_t OriginalDefaultMaximum=256;
bool unique(const core::Node& node){if(node.type==core::Node::Type::Dictionary){std::set<std::string_view> keys;for(std::size_t i=0;i<node.children.size();i+=2){if(!keys.insert(node.children[i].bytes).second){return false;}}}return std::ranges::all_of(node.children,[](const auto& child){return unique(child);});}
}
std::expected<SubscriptionAssociationValues,E> parseSubscriptionAssociations(std::string raw){try{
  auto document=core::Document::parse(std::move(raw),Limits);if(!document||document->root().type!=core::Node::Type::Dictionary||!unique(document->root())){return std::unexpected(E::Malformed);}SubscriptionAssociationValues out;out.metadata=std::make_shared<const core::Document>(std::move(*document));const auto* values=out.metadata->find("assoc");if(!values){return out;}if(values->type!=core::Node::Type::List){return std::unexpected(E::Malformed);}if(values->children.size()>4096){return std::unexpected(E::Limit);}out.originalList=out.metadata->originalBytes().substr(values->begin,values->end-values->begin);
  for(const auto& value:values->children){const auto* hash=value.find("h");const auto* when=value.find("w");if(value.type!=core::Node::Type::Dictionary||!hash||hash->type!=core::Node::Type::Bytes||hash->bytes.size()!=20||!when||when->type!=core::Node::Type::Integer){return std::unexpected(E::Malformed);}SubscriptionAssociationValue row;for(std::size_t i=0;i<20;++i){row.hash[i]=std::byte(static_cast<unsigned char>(hash->bytes[i]));}row.when=when->integer;row.encoded=out.metadata->originalBytes().substr(value.begin,value.end-value.begin);out.rows.push_back(std::move(row));}return out;
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
SubscriptionAssociationCapacity subscriptionAssociationCapacity(const SubscriptionAssociationValues& state,const std::optional<SubscriptionAssociationHash>& known)noexcept{
  if(known&&std::ranges::any_of(state.rows,[&](const auto& row){return row.hash==*known;})){return SubscriptionAssociationCapacity::AlreadyPresent;}return state.rows.size()<OriginalDefaultMaximum?SubscriptionAssociationCapacity::AppendWithoutEviction:SubscriptionAssociationCapacity::OriginalRandomEvictionRequired;
}
std::expected<SubscriptionAssociationValueChange,E> planSubscriptionAssociationValue(const SubscriptionAssociationValues& state,const SubscriptionAssociationHash& hash,std::int64_t now){try{
  const auto capacity=subscriptionAssociationCapacity(state,hash);if(capacity==SubscriptionAssociationCapacity::AlreadyPresent){if(!state.originalList){return std::unexpected(E::Malformed);}return SubscriptionAssociationValueChange{*state.originalList,false};}
  // Original appends, then removes RandomUtils.nextInt(MAX_ASSOCIATIONS-16).
  // No guessed eviction or caller-controlled index is accepted in this slice.
  if(capacity==SubscriptionAssociationCapacity::OriginalRandomEvictionRequired){return std::unexpected(E::Unsupported);}std::string list="l";for(const auto& row:state.rows){list+=row.encoded;}list+="d1:h20:";list.append(reinterpret_cast<const char*>(hash.data()),hash.size());list+="1:wi"+std::to_string(now)+"ee";list+='e';if(list.size()>Limits.inputBytes){return std::unexpected(E::Limit);}return SubscriptionAssociationValueChange{std::move(list),true};
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
} // namespace biglybtc::search::detail
