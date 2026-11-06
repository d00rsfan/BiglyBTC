// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_download_values.hpp"
#include "biglybtc/bencode.hpp"
#include <new>
namespace biglybtc::search::detail {
namespace {
using E=SubscriptionDownloadValueError;
constexpr core::Limits Limits{4*1024*1024,4*1024*1024,100000,64};
constexpr std::size_t MaximumSavedSids=4096;
std::string encodedString(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string raw(const core::Document& doc,const core::Node& node){return doc.originalBytes().substr(node.begin,node.end-node.begin);}
std::expected<void,E> replace(core::Document& doc,std::string_view name,std::string value){auto result=doc.replaceValue(name,std::move(value));if(!result){return std::unexpected(result.error()==core::EditError::Limit?E::Limit:E::Malformed);}return {};}
}
std::optional<std::int64_t> subscriptionRawScrapeCache(SubscriptionRawScrapeCounts counts)noexcept{
  if(!counts.seeds||!counts.peers||*counts.seeds<0||*counts.peers<0){return {};}
  const auto seeds=static_cast<std::uint64_t>(*counts.seeds)&0x00ffffffULL;
  const auto peers=static_cast<std::uint64_t>(*counts.peers)&0x00ffffffULL;
  return static_cast<std::int64_t>((seeds<<32)|peers);
}
std::expected<SubscriptionDownloadValues,E> planSubscriptionDownloadValues(std::string attributes,std::string_view sid,bool newAssociation,SubscriptionRawScrapeCounts counts){try{
  if(attributes.size()>Limits.inputBytes){return std::unexpected(E::Limit);}auto doc=core::Document::parse(std::move(attributes),Limits);if(!doc||doc->root().type!=core::Node::Type::Dictionary){return std::unexpected(E::Malformed);}
  SubscriptionDownloadValues out;std::optional<std::string> changedMap;
  // Original addAssociationSupport returns before associationAdded on duplicate.
  if(newAssociation){
    if(sid.size()!=10){return std::unexpected(E::Malformed);}const auto* map=doc->find(SubscriptionDownloadMapAttribute);
    if(map&&map->type!=core::Node::Type::Dictionary){return std::unexpected(E::Malformed);}
    auto subscriptionMap=core::Document::parse(map?raw(*doc,*map):"de",Limits);if(!subscriptionMap){return std::unexpected(E::Malformed);}
    const auto* members=subscriptionMap->find("s");if(members&&members->type!=core::Node::Type::List){return std::unexpected(E::Malformed);}if(members&&members->children.size()>MaximumSavedSids){return std::unexpected(E::Limit);}
    bool found=false;if(members){for(const auto& member:members->children){if(member.type!=core::Node::Type::Bytes){return std::unexpected(E::Malformed);}if(member.bytes==sid){found=true;}}}
    if(!found){
      if(members&&members->children.size()==MaximumSavedSids){return std::unexpected(E::Limit);}
      std::string values="l";if(members){for(const auto& member:members->children){values+=raw(*subscriptionMap,member);}}values+=encodedString(sid);values+='e';
      auto replaced=replace(*subscriptionMap,"s",std::move(values));if(!replaced){return std::unexpected(replaced.error());}changedMap=subscriptionMap->serialize();out.sidChanged=true;
    }
  }
  const auto cache=subscriptionRawScrapeCache(counts);
  if(cache){
    const auto* source=doc->find("scsrc");const auto* previous=doc->find("scrapecache");
    if((source&&source->type!=core::Node::Type::Integer)||(previous&&previous->type!=core::Node::Type::Integer)){return std::unexpected(E::Malformed);}
    out.cacheChanged=!source||source->integer!=1||!previous||previous->integer!=*cache;
    if(out.cacheChanged){auto first=replace(*doc,"scsrc","i1e");if(!first){return std::unexpected(first.error());}auto second=replace(*doc,"scrapecache","i"+std::to_string(*cache)+"e");if(!second){return std::unexpected(second.error());}}
  }
  if(changedMap){auto replaced=replace(*doc,SubscriptionDownloadMapAttribute,std::move(*changedMap));if(!replaced){return std::unexpected(replaced.error());}}
  out.changed=out.cacheChanged||out.sidChanged;out.attributes=out.changed?doc->serialize():doc->originalBytes();return out;
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
} // namespace biglybtc::search::detail
