// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/library.hpp>
#include <algorithm>
#include <deque>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace biglybtc::app::detail {
// Bounded passive queue only. An actual private durable-intake producer and
// current source/Store fences are still required for every membership effect.
class TrackerTagBirths final {
public:
 struct Rule {std::int32_t id=0;std::vector<std::string> names;bool operator==(const Rule&)const=default;};
 using Rules=std::vector<Rule>;using Cohort=std::shared_ptr<const Rules>;
 enum class Error {Invalid,Bounds,Memory};
 enum class Recorded {Added,Duplicate,NoRules};
 struct Request {core::TorrentHash hash;std::int64_t addedTime=0;std::uint64_t event=0,token=0;std::size_t index=0;Cohort rules;};
 TrackerTagBirths()=default;TrackerTagBirths(const TrackerTagBirths&)=delete;TrackerTagBirths& operator=(const TrackerTagBirths&)=delete;
 std::expected<Cohort,Error> cohort(Rules rules){
  try{
   if(rules.size()>4096){return std::unexpected(Error::Bounds);}std::size_t bytes=0;std::set<std::int32_t> ids;
   for(const auto& rule:rules){if(rule.id<0||!ids.insert(rule.id).second||rule.names.empty()||rule.names.size()>64){return std::unexpected(Error::Invalid);}
    for(const auto& name:rule.names){if(name.size()>2048||name.size()>262144-bytes){return std::unexpected(Error::Bounds);}bytes+=name.size();for(unsigned char c:name){if(c>=128){return std::unexpected(Error::Invalid);}}}
   }
   if(current_&&*current_==rules){return current_;}
   std::erase_if(versions_,[](const auto& weak){return weak.expired();});
   for(const auto& weak:versions_){if(auto previous=weak.lock();previous&&*previous==rules){current_=previous;return previous;}}
   if(versions_.size()==8){return std::unexpected(Error::Bounds);}
   auto next=std::make_shared<const Rules>(std::move(rules));versions_.push_back(next);current_=next;return next;
  }catch(...){return std::unexpected(Error::Memory);}
 }
 std::expected<Recorded,Error> record(const core::TorrentHash& hash,std::int64_t addedTime,std::uint64_t event,const Cohort& rules){
  try{
   if(!event||!rules||std::none_of(versions_.begin(),versions_.end(),[&](const auto& weak){return weak.lock()==rules;})){return std::unexpected(Error::Invalid);}
   if(event<=lastEvent_){return Recorded::Duplicate;}
   if(rules->empty()){lastEvent_=event;return Recorded::NoRules;}
   auto old=std::find_if(queue_.begin(),queue_.end(),[&](const auto& value){return value.hash==hash;});
   if(old==queue_.end()&&queue_.size()==64){return std::unexpected(Error::Bounds);}
   // Reserve the new value before invalidating an earlier same-hash birth.
   const bool hadOld=old!=queue_.end();Entry value{hash,addedTime,event,0,rules};queue_.push_back(std::move(value));
   if(hadOld){
    // deque insertion may invalidate iterators; erase by the saved identity.
    if(pending_&&pending_->hash==hash){pending_.reset();}
    std::erase_if(queue_,[&](const auto& row){return row.hash==hash&&row.event!=event;});
   }
   lastEvent_=event;return Recorded::Added;
  }catch(...){return std::unexpected(Error::Memory);}
 }
 std::expected<std::optional<Request>,Error> next(){
  try{
   if(queue_.empty()){return std::optional<Request>{};}if(pending_){return pending_;}
   if(sequence_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Bounds);}
   const auto& entry=queue_.front();pending_=Request{entry.hash,entry.addedTime,entry.event,++sequence_,entry.cursor,entry.rules};return pending_;
  }catch(...){return std::unexpected(Error::Memory);}
 }
 bool current(const Request& r)const noexcept{
  if(!pending_||queue_.empty()){return false;}const auto& p=*pending_;const auto& e=queue_.front();
  return r.token&&r.token==p.token&&r.event==p.event&&r.hash==p.hash&&r.addedTime==p.addedTime&&r.index==p.index&&r.rules==p.rules
      &&r.event==e.event&&r.hash==e.hash&&r.addedTime==e.addedTime&&r.rules==e.rules&&r.index==e.cursor&&r.index<e.rules->size();
 }
 // These acknowledgements only advance passive data. A successful real save
 // must be reflected by its owner before any cancellation check or callback.
 std::expected<void,Error> accept(const Request& r)noexcept{
  if(!current(r)){return std::unexpected(Error::Invalid);}if(++queue_.front().cursor==queue_.front().rules->size()){queue_.pop_front();}pending_.reset();return {};
 }
 std::expected<void,Error> withdraw(const Request& r)noexcept{
  if(!current(r)){return std::unexpected(Error::Invalid);}queue_.pop_front();pending_.reset();return {};
 }
 std::expected<void,Error> defer(const Request& r){
  if(!current(r)){return std::unexpected(Error::Invalid);}
  try{queue_.push_back(queue_.front());queue_.pop_front();pending_.reset();return {};}catch(...){return std::unexpected(Error::Memory);}
 }
 bool contains(const core::TorrentHash& hash)const noexcept{return std::ranges::any_of(queue_,[&](const auto& entry){return entry.hash==hash;});}
 std::size_t size()const noexcept{return queue_.size();}
 void clear()noexcept{pending_.reset();queue_.clear();current_.reset();versions_.clear();}
private:
 struct Entry {core::TorrentHash hash;std::int64_t addedTime;std::uint64_t event;std::size_t cursor;Cohort rules;};
 std::deque<Entry> queue_;std::vector<std::weak_ptr<const Rules>> versions_;Cohort current_;
 std::optional<Request> pending_;std::uint64_t lastEvent_=0,sequence_=0;
};
}
