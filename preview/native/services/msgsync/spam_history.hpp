// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
namespace biglybtc::services::msgsync {
enum class SpamHistoryError {Key,History,Capacity};
// MsgSyncHandler4209aa5 selected spammer history policy, without automatic flood
// banning. Relay history is unsigned routing metadata, never identity authority.
// Call only after actual signature/room/source checks. It does not drop ledger
// entries, ban addresses, persist settings or turn a direct PK hit into a ban.
class SpamHistory final {
 using Key=std::array<unsigned char,4>;
 std::map<std::string,std::deque<std::string>,std::less<>> entries_;std::set<Key> bad_;
 static bool validKey(std::string_view key){return !key.empty()&&key.size()<=512;}
 static Key block(std::string_view value,std::size_t offset){Key out{};for(std::size_t i=0;i<4;++i){out[i]=static_cast<unsigned char>(value[offset+i]);}return out;}
 void compute(){std::map<Key,std::size_t> counts;for(const auto& [key,histories]:entries_){(void)key;for(const auto& history:histories){for(std::size_t i=0;i+4<=history.size();i+=4){++counts[block(history,i)];}}}bad_.clear();for(const auto& [key,histories]:entries_){(void)key;for(const auto& history:histories){std::size_t maximum=0;Key selected{};for(std::size_t i=0;i+4<=history.size();i+=4){const auto value=block(history,i);const auto count=counts.at(value);if(count>maximum){maximum=count;selected=value;}}if(maximum>1){bad_.insert(selected);}else{for(std::size_t i=0;i+4<=history.size();i+=4){bad_.insert(block(history,i));}}}}}
 void add(std::deque<std::string>& records,std::string_view history){if(history.empty()){return;}records.emplace_back(history);if(records.size()>16){records.pop_front();}compute();}
public:
 bool marked(std::string_view key)const{return entries_.contains(key);}
 std::expected<void,SpamHistoryError> mark(std::string key,bool value,std::span<const std::string> retained={}){
  if(!validKey(key)){return std::unexpected(SpamHistoryError::Key);}if(!value){if(entries_.erase(key)){compute();}return {};}
  if(marked(key)){return {};}if(entries_.size()>=4096){return std::unexpected(SpamHistoryError::Capacity);}for(const auto& history:retained){if(history.size()>80){return std::unexpected(SpamHistoryError::History);}}
  auto& records=entries_[std::move(key)];for(const auto& history:retained){add(records,history);}return {};
 }
 std::expected<bool,SpamHistoryError> accept(std::string_view key,std::string_view history){
  if(!validKey(key)){return std::unexpected(SpamHistoryError::Key);}if(history.size()>80){return std::unexpected(SpamHistoryError::History);}if(auto found=entries_.find(key);found!=entries_.end()){add(found->second,history);}for(std::size_t i=0;i+4<=history.size();i+=4){if(bad_.contains(block(history,i))){return false;}}return true;
 }
 void clear(){entries_.clear();bad_.clear();}
 std::size_t retained(std::string_view key)const{const auto found=entries_.find(key);return found==entries_.end()?0:found->second.size();}
};
}
