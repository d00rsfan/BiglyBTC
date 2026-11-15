// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <algorithm>
#include <optional>
#include <utility>
#include <span>
#include <vector>
namespace biglybtc::core {
// Original diverse.dat remote entry. Metadata only; no contact/IO authority.
struct AzureusRemoteDiversification {std::array<std::uint8_t,20> key{};std::int8_t type=1;std::int64_t expiry=0;std::array<unsigned,2> offsets{};bool operator==(const AzureusRemoteDiversification&)const=default;};
inline bool validAzureusDiversification(const AzureusRemoteDiversification& r){return (r.type==2||r.type==3)&&r.expiry>0&&r.offsets[0]<10&&r.offsets[1]<10&&r.offsets[0]!=r.offsets[1];}
// Owned by one source/network actor. Wall-clock expiry matches the Java ledger;
// persisted means this exact record has passed the owner's profile IO broker.
class AzureusDiversificationLedger {
 struct Entry {AzureusRemoteDiversification record;bool persisted=false;};
 std::map<std::array<std::uint8_t,20>,Entry> entries_;
public:
 static constexpr std::size_t MaximumRecords=65536;
 bool restore(std::span<const AzureusRemoteDiversification> records,std::int64_t now){
  auto next=entries_;
  for(const auto& r:records){if(!validAzureusDiversification(r)||r.expiry<=now){continue;}if(!next.contains(r.key)&&next.size()>=MaximumRecords){return false;}next.insert_or_assign(r.key,Entry{r,true});}
  entries_=std::move(next);return true;
 }
 std::optional<AzureusRemoteDiversification> find(const std::array<std::uint8_t,20>& key,std::int64_t now){
  auto at=entries_.find(key);if(at==entries_.end()){return {};}if(at->second.record.expiry<now){entries_.erase(at);return {};}return at->second.record;
 }
 std::optional<AzureusRemoteDiversification> remember(AzureusRemoteDiversification proposed,std::int64_t now,bool durable=false){
  if(auto old=find(proposed.key,now)){return old;}if(!validAzureusDiversification(proposed)||proposed.expiry<=now){return {};}
  if(entries_.size()>=MaximumRecords){std::erase_if(entries_,[now](const auto& e){return e.second.record.expiry<now;});}if(entries_.size()>=MaximumRecords){return {};}
  entries_.emplace(proposed.key,Entry{proposed,durable});return proposed;
 }
 bool persisted(const AzureusRemoteDiversification& r)const{auto at=entries_.find(r.key);return at!=entries_.end()&&at->second.record==r&&at->second.persisted;}
 bool markPersisted(const AzureusRemoteDiversification& r){auto at=entries_.find(r.key);if(at==entries_.end()||at->second.record!=r){return false;}at->second.persisted=true;return true;}
 std::vector<AzureusRemoteDiversification> dirty(std::int64_t now,std::size_t limit)const{
  std::vector<AzureusRemoteDiversification> out;if(!limit){return out;}for(const auto& [key,e]:entries_){(void)key;if(!e.persisted&&e.record.expiry>=now){out.push_back(e.record);if(out.size()>=limit){break;}}}return out;
 }
};
// Supply fresh distinct read offsets. Size PUTs always use the saved offsets;
// frequency PUTs use every fragment. Exhaustive size GET uses all fragments.
inline std::vector<unsigned> azureusDiversificationOffsets(const AzureusRemoteDiversification& r,bool put,bool exhaustive,std::array<unsigned,2> random){
 if(!validAzureusDiversification(r)||random[0]>=10||random[1]>=10||random[0]==random[1]){return {};}
 if((put&&r.type==2)||(!put&&r.type==3&&exhaustive)){return {0,1,2,3,4,5,6,7,8,9};}
 if(put){return {r.offsets[0],r.offsets[1]};}if(r.type==2){return {random[0]};}return {random[0],random[1]};
}
}
