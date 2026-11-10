// SPDX-License-Identifier: GPL-2.0-or-later
// MsgSyncHandler.addMessage + MsgSyncMessage age, pinned original4209aa5.
#include "ledger.hpp"
#include <algorithm>
#include <bit>
#include <new>
#include <limits>

namespace biglybtc::services::msgsync {
namespace {
void increment(std::int32_t& value)noexcept{value=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value)+1U);}
std::string inverted(std::string_view value){std::string out(value);for(char& byte:out){byte=static_cast<char>(static_cast<unsigned char>(byte)^0xffU);}return out;}
}
MessageLedger::MessageLedger(){messages_.reserve(MaximumMessages+1);inverted_.reserve(MaximumTombstones);}
std::int32_t MessageLedger::age(Stored& value,std::int64_t now)noexcept{
  if(now<value.received){value.received=now;return value.age;}
  const auto elapsed=static_cast<std::uint64_t>(now-value.received)/1000;
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value.age)+static_cast<std::uint32_t>(elapsed));
}
Result<AddOutcome> MessageLedger::add(VerifiedMessage verified,std::int64_t now,bool loading){
  if(now<0){return std::unexpected(Error::Clock);}
  const auto& message=verified.message();
  if(std::any_of(messages_.begin(),messages_.end(),[&](const auto& old){return old.verified.message().signature==message.signature;})){return AddOutcome::Duplicate;}
  try{
    auto inverse=inverted(message.signature);
    if(std::find(inverted_.begin(),inverted_.end(),inverse)!=inverted_.end()){return AddOutcome::Tombstoned;}
    const auto incomingAge=std::max<std::int32_t>(0,message.ageSeconds);
    std::size_t point=messages_.size();bool found=false;
    while(point){--point;if(age(messages_[point],now)>=incomingAge){++point;found=true;break;}}
    if(!found){point=0;}
    // Original very-old insertion must not flush a full tombstone cache. A
    // genuinely evicted retained message does refresh/evict its LRU tombstone.
    if(messages_.size()==MaximumMessages&&point==0){
      if(inverted_.size()<MaximumTombstones){inverted_.push_back(std::move(inverse));increment(mutations_);}return AddOutcome::TooOld;
    }
    std::optional<std::string> removed;
    if(messages_.size()==MaximumMessages){removed=inverted(messages_.front().verified.message().signature);}
    if(ordinal_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Limit);}
    const auto ordinal=ordinal_+1;const auto oldCount=messages_.size();
    messages_.insert(messages_.begin()+static_cast<std::ptrdiff_t>(point),Stored{std::move(verified),now,incomingAge,ordinal,now-std::int64_t(incomingAge)*1000});
    ordinal_=ordinal;
    if(removed){
      messages_.erase(messages_.begin());
      if(auto it=std::find(inverted_.begin(),inverted_.end(),*removed);it!=inverted_.end()){inverted_.erase(it);}
      if(inverted_.size()==MaximumTombstones){inverted_.erase(inverted_.begin());}inverted_.push_back(std::move(*removed));
    }
    increment(mutations_);if(!loading&&point>oldCount/2){increment(newMessages_);}return AddOutcome::Added;
  }catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
Result<std::vector<LedgerEntry>> MessageLedger::snapshot(std::int64_t now){
  if(now<0){return std::unexpected(Error::Clock);}
  try{
    std::vector<LedgerEntry> result;result.reserve(messages_.size());
    for(auto& stored:messages_){auto message=stored.verified.message();message.ageSeconds=age(stored,now);result.push_back({std::move(message),std::string(stored.verified.senderKey()),stored.ordinal,stored.timestamp});}return result;
  }catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
Result<SyncBloom> MessageLedger::buildBloom(std::span<const ContactBloomKey> contacts)const{
  if(contacts.size()>128){return std::unexpected(Error::Limit);}
  try{
    std::vector<std::string> keys;keys.reserve(messages_.size()+inverted_.size()+contacts.size());
    for(const auto& message:messages_){keys.push_back(message.verified.message().signature);}
    for(const auto& signature:inverted_){keys.push_back(signature);}
    for(const auto& contact:contacts){
      if(contact.publicKey.size()>512||contact.originalAddressUtf8.size()>512||contact.publicKey.empty()){return std::unexpected(Error::Limit);}
      // Exact original MsgSyncHandler.getString(contact) UTF8 supplied by the
      // adapter; never use a new address formatter, resolver or contact grant.
      std::string key(contact.publicKey);key+=contact.originalAddressUtf8;keys.push_back(std::move(key));
    }
    return buildSyncBloom(keys);
  }catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
void MessageLedger::clear()noexcept{messages_.clear();inverted_.clear();mutations_=0;newMessages_=0;}
}
