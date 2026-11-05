// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_routing.hpp"
#include <algorithm>
#include <array>
namespace biglybtc::services::i2p::dht {
Result<std::vector<SavedContact>> selectSavedRoutingContacts(
    const azureus_router::RouterBestContacts& best,std::span<const Contact> attachments,
    std::span<const SavedContact> imported,std::span<const RoutingProtocolObservation> observed){
  using namespace azureus_router;
  if(best.count>MaximumRouterContacts||attachments.size()>MaximumRouterContacts||imported.size()>MaximumSavedContacts||observed.size()>MaximumRouterContacts){
    return std::unexpected(Error{ErrorCode::TooLarge,"Bounded I2P routing export required"});
  }
  try{
    std::vector<const Contact*> index;index.reserve(attachments.size());
    for(const auto& contact:attachments){index.push_back(&contact);}
    std::sort(index.begin(),index.end(),[](const auto* a,const auto* b){return a->id<b->id;});
    if(std::adjacent_find(index.begin(),index.end(),[](const auto* a,const auto* b){return a->id==b->id;})!=index.end()){
      return std::unexpected(Error{ErrorCode::InvalidFormat,"Ambiguous I2P routing attachments"});
    }
    std::vector<const RoutingProtocolObservation*> versions;versions.reserve(observed.size());
    for(const auto& value:observed){versions.push_back(&value);}
    std::sort(versions.begin(),versions.end(),[](const auto* a,const auto* b){return a->id<b->id;});
    if(std::adjacent_find(versions.begin(),versions.end(),[](const auto* a,const auto* b){return a->id==b->id;})!=versions.end()){
      return std::unexpected(Error{ErrorCode::InvalidFormat,"Ambiguous I2P protocol observations"});
    }
    std::vector<SavedContact> out;out.reserve(std::min(best.count,MaximumSavedContacts));
    std::array<bool,MaximumRouterContacts> used{};
    for(unsigned pass=0;pass<4;++pass){
      for(std::size_t i=0;i<best.count&&out.size()<MaximumSavedContacts;++i){
        if(used[i]){continue;}const auto& entry=best.values[i];const auto& state=entry.contact;
        // Java imported_state is keyed by ID; a repeated import replaces the
        // metadata. Only membership of that map affects export priority.
        const SavedContact* old=nullptr;
        for(auto at=imported.rbegin();at!=imported.rend();++at){if(at->contact.id==state.id){old=&*at;break;}}
        if((pass==0&&!(old&&state.alive()))||(pass==1&&!state.alive())||(pass==2&&!(old&&!state.failing()))){continue;}
        const auto found=std::lower_bound(index.begin(),index.end(),state.id,[](const auto* value,const Id& id){return value->id<id;});
        if(found==index.end()||(*found)->id!=state.id||!validContact(**found)){continue;}
        const auto& contact=**found;
        // Local transport is AZ4. Remote protocol remains conservative when
        // no imported or actually observed version is available; hints grant
        // neither remote capabilities nor permission to send a request.
        const auto version=std::lower_bound(versions.begin(),versions.end(),state.id,[](const auto* value,const Id& id){return value->id<id;});
        const auto* actual=version!=versions.end()&&(*version)->id==state.id&&(*version)->hash==contact.hash&&(*version)->port==contact.port?*version:nullptr;
        const auto protocol=state.local?std::int8_t{4}:actual?actual->protocolVersion:
            old&&old->contact.hash==contact.hash&&old->contact.port==contact.port?old->protocolVersion:std::int8_t{0};
        out.push_back({contact,entry.timeAlive,1,protocol});used[i]=true;
      }
    }
    return out;
  }catch(...){return std::unexpected(Error{ErrorCode::CryptoFailure,"I2P routing export resources unavailable"});}
}
}
