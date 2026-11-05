// SPDX-License-Identifier: GPL-2.0-or-later
// Original policy: Paul Gardner, Copyright (C) Azureus Software, Inc.
#include "inbound_state.hpp"
#include <algorithm>
#include <limits>
#include <openssl/crypto.h>
namespace biglybtc::services::i2p::dht::inbound {
namespace {
StateError authError(azureus_auth::Error error){return error==azureus_auth::Error::Thread?StateError::Thread:StateError::Crypto;}
bool clockValid(std::int64_t now){return now>=0&&now<=std::numeric_limits<std::int64_t>::max()-PeerLifetimeMillis;}
StateResult<Contact> actualContact(const Contact& contact){
  if(!contact.destination||!validContact(contact)){return std::unexpected(StateError::Invalid);}
  auto parsed=decodeDestination(contact.destination->wire);
  if(!parsed||parsed->hash!=contact.hash){return std::unexpected(StateError::Invalid);}
  Contact result=contact;result.destination=std::move(*parsed);return result;
}
}
TokenBindings::TokenBindings(std::unique_ptr<azureus_auth::Authority> value):authority_(std::move(value)){}
TokenBindings::~TokenBindings(){close();}
StateResult<std::unique_ptr<TokenBindings>> TokenBindings::create(std::optional<azureus_auth::Key> supplied){
  try{
    std::unique_ptr<azureus_auth::Authority> value;
    if(supplied){value=std::make_unique<azureus_auth::Authority>(supplied);OPENSSL_cleanse(supplied->data(),supplied->size());}
    else{auto generated=azureus_auth::Authority::create();if(!generated){return std::unexpected(authError(generated.error()));}value=std::move(*generated);}
    return std::unique_ptr<TokenBindings>(new TokenBindings(std::move(value)));
  }catch(...){if(supplied){OPENSSL_cleanse(supplied->data(),supplied->size());}return std::unexpected(StateError::Resource);}
}
StateResult<void> TokenBindings::check()const noexcept{
  if(std::this_thread::get_id()!=thread_){return std::unexpected(StateError::Thread);}
  if(closed_){return std::unexpected(StateError::Closed);}return {};
}
StateResult<azureus_auth::Token2> TokenBindings::remember(const Contact& contact){
  if(auto ok=check();!ok){return std::unexpected(ok.error());}
  try{
    auto actual=actualContact(contact);if(!actual){return std::unexpected(actual.error());}
    auto issued=authority_->issueId(actual->id);if(!issued){return std::unexpected(authError(issued.error()));}
    auto at=std::find_if(bindings_.begin(),bindings_.end(),[&](const auto& entry){return entry.token==issued->value;});
    // Original token_map is keyed by token, including the original first16-NID
    // derivation collision when only the final4 bytes differ: latest full node wins.
    if(at!=bindings_.end()){at->contact=std::move(*actual);bindings_.splice(bindings_.end(),bindings_,at);}
    else{bindings_.push_back({issued->value,std::move(*actual)});if(bindings_.size()>MaximumTokenBindings){bindings_.pop_front();}}
    return issued->value;
  }catch(...){return std::unexpected(StateError::Resource);}
}
StateResult<std::optional<Contact>> TokenBindings::resolve(View token){
  if(auto ok=check();!ok){return std::unexpected(ok.error());}
  if(token.size()!=azureus_auth::Token2{}.size()){return std::optional<Contact>{};}
  try{
    auto at=std::find_if(bindings_.begin(),bindings_.end(),[&](const auto& entry){return std::equal(token.begin(),token.end(),entry.token.begin());});
    if(at==bindings_.end()){return std::optional<Contact>{};}
    auto verified=authority_->verifyId(at->contact.id,token);if(!verified){return std::unexpected(authError(verified.error()));}if(!*verified){return std::optional<Contact>{};}
    auto copy=at->contact;bindings_.splice(bindings_.end(),bindings_,at);return std::optional<Contact>{std::move(copy)};
  }catch(...){return std::unexpected(StateError::Resource);}
}
StateResult<std::size_t> TokenBindings::size()const noexcept{if(auto ok=check();!ok){return std::unexpected(ok.error());}return bindings_.size();}
void TokenBindings::close()noexcept{closed_=true;bindings_.clear();authority_.reset();}
PeerStore::PeerStore(StoreLimits limits):limits_(limits){records_.reserve(limits_.keys);}
StateResult<std::unique_ptr<PeerStore>> PeerStore::create(StoreLimits limits){
  if(!limits.keys||limits.keys>64||!limits.valuesPerKey||limits.valuesPerKey>64){return std::unexpected(StateError::Invalid);}
  try{return std::unique_ptr<PeerStore>(new PeerStore(limits));}catch(...){return std::unexpected(StateError::Resource);}
}
StateResult<void> PeerStore::check()const noexcept{
  if(std::this_thread::get_id()!=thread_){return std::unexpected(StateError::Thread);}
  if(closed_){return std::unexpected(StateError::Closed);}return {};
}
StateResult<std::size_t> PeerStore::expire(std::int64_t now,bool force){
  if(auto ok=check();!ok){return std::unexpected(ok.error());}if(!clockValid(now)){return std::unexpected(StateError::Clock);}
  const auto elapsed=now-lastScan_;if(!force&&elapsed>0&&elapsed<ExpiryScanMillis){return std::size_t{0};}lastScan_=now;
  std::size_t removed=0;
  for(auto at=records_.begin();at!=records_.end();){
    // Original DB removes keys empty at scan entry; newly expired-to-empty
    // mappings survive until the next scan and still produce an empty hit.
    if(at->values.empty()){at=records_.erase(at);continue;}
    const auto before=at->values.size();std::erase_if(at->values,[&](const auto& value){return now>value.created+PeerLifetimeMillis;});removed+=before-at->values.size();++at;
  }return removed;
}
StateResult<StoreResult> PeerStore::store(const Id& key,const Contact& originator,bool seed,std::int64_t now){
  if(auto ok=check();!ok){return std::unexpected(ok.error());}if(!clockValid(now)){return std::unexpected(StateError::Clock);}
  try{
    auto actual=actualContact(originator);if(!actual){return std::unexpected(actual.error());}
    if(auto scanned=expire(now);!scanned){return std::unexpected(scanned.error());}
    auto record=std::find_if(records_.begin(),records_.end(),[&](const auto& row){return row.key==key;});
    if(record==records_.end()){
      if(records_.size()==limits_.keys){return std::unexpected(StateError::Capacity);}
      Record created{key,{}};created.values.reserve(limits_.valuesPerKey);created.values.push_back({actual->id,{actual->hash,seed},now,now});records_.push_back(std::move(created));return StoreResult::Inserted;
    }
    auto value=std::find_if(record->values.begin(),record->values.end(),[&](const auto& row){return row.originator==actual->id;});
    if(value!=record->values.end()){
      value->stored=now;value->created=std::min(value->created,now);
      std::rotate(value,value+1,record->values.end());return StoreResult::StoreTimeRefreshed;
    }
    if(record->values.size()==limits_.valuesPerKey){return std::unexpected(StateError::Capacity);}
    record->values.push_back({actual->id,{actual->hash,seed},now,now});return StoreResult::Inserted;
  }catch(...){return std::unexpected(StateError::Resource);}
}
StateResult<bool> PeerStore::contains(const Id& key)const noexcept{
  if(auto ok=check();!ok){return std::unexpected(ok.error());}
  return std::any_of(records_.begin(),records_.end(),[&](const auto& row){return row.key==key;});
}
StateResult<Read> PeerStore::read(const Id& key,std::int64_t now,unsigned maximum){
  if(auto ok=check();!ok){return std::unexpected(ok.error());}if(!maximum||maximum>16){return std::unexpected(StateError::Invalid);}
  if(auto scanned=expire(now);!scanned){return std::unexpected(scanned.error());}
  try{
    Read result;auto record=std::find_if(records_.begin(),records_.end(),[&](const auto& row){return row.key==key;});if(record==records_.end()){return result;}result.keyExists=true;
    result.peers.reserve(maximum);std::array<Id,16> used{};std::size_t count=0;
    for(const auto& value:record->values){if(result.peers.size()==maximum){break;}
      if(std::any_of(result.peers.begin(),result.peers.end(),[&](const auto& peer){return peer.hash==value.peer.hash;})){continue;}
      result.peers.push_back(value.peer);used[count++]=value.originator;
    }
    for(std::size_t i=0;i<count;++i){auto at=std::find_if(record->values.begin(),record->values.end(),[&](const auto& value){return value.originator==used[i];});if(at!=record->values.end()){std::rotate(at,at+1,record->values.end());}}
    return result;
  }catch(...){return std::unexpected(StateError::Resource);}
}
StateResult<Counts> PeerStore::counts()const noexcept{
  if(auto ok=check();!ok){return std::unexpected(ok.error());}Counts result;result.keys=records_.size();for(const auto& row:records_){result.values+=row.values.size();}return result;
}
void PeerStore::close()noexcept{closed_=true;records_.clear();}
} // namespace biglybtc::services::i2p::dht::inbound
