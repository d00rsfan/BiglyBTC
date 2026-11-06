// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/library.hpp"
#include "biglybtc/tag_constraints.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <memory>
namespace biglybtc::app::detail {
// Pure bounded bookkeeping only; this type has no Store, lease or edit effect.
// The real worker's instance/recorder is private and requires actual intake IO.
struct NewDownloadRule {std::int32_t id;core::tags::constraints::Property property;};
using NewDownloadRules=std::vector<NewDownloadRule>;
inline bool sameNewDownloadRule(const core::tags::constraints::Property& a,const core::tags::constraints::Property& b){
  return a.expression==b.expression&&a.enabled==b.enabled&&a.configured==b.configured
      &&a.mode.add==b.mode.add&&a.mode.remove==b.mode.remove&&a.mode.newOnly==b.mode.newOnly;
}
class NewDownloadEvents {
public:
  using Clock=std::chrono::steady_clock;
  static constexpr std::size_t Maximum=4096;
  static constexpr std::int64_t NewMillis=300000;
  enum class Added {Recorded,Duplicate,Old,Full,Unavailable};
  struct Event {
    core::TorrentHash hash;std::int64_t addedMillis;std::uint64_t event;
    Clock::time_point end;std::shared_ptr<const NewDownloadRules> rules;
    std::size_t cursor=0;
  };
  static bool recent(std::int64_t added,std::int64_t wall)noexcept{
    return added>0&&wall>0&&(added>wall||wall-added<NewMillis);
  }
  Added record(const core::TorrentHash& hash,std::int64_t added,std::int64_t wall,Clock::time_point now,std::uint64_t event){
    expire(now);if(!event){return Added::Unavailable;}if(!recent(added,wall)){return Added::Old;}
    const auto prior=std::find_if(recent_.begin(),recent_.end(),[&](const auto& e){return e.event==event;});
    if(prior!=recent_.end()){return prior->hash==hash&&prior->addedMillis==added?Added::Duplicate:Added::Unavailable;}
    if(recent_.size()==Maximum){return Added::Full;}
    const auto remaining=added>=wall?NewMillis:NewMillis-(wall-added);
    const auto duration=std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(remaining));
    if(now>Clock::time_point::max()-duration){return Added::Unavailable;}
    const auto end=now+duration;
    // Reserve both before exposing work; no successful record can lose its
    // duplicate fence merely because the event completed faster than polling.
    recent_.push_back({event,hash,added,end});
    try{events_.push_back({hash,added,event,end,{},0});}catch(...){recent_.pop_back();throw;}
    return Added::Recorded;
  }
  void expire(Clock::time_point now){std::erase_if(events_,[&](const auto& e){return now>=e.end;});std::erase_if(recent_,[&](const auto& e){return now>=e.end;});}
  bool empty()const noexcept{return events_.empty();}
  std::size_t size()const noexcept{return events_.size();}
  Event& front(){return events_.front();}
  void finishTurn(){
    if(events_.empty()){return;}
    if(events_.front().rules&&events_.front().cursor>=events_.front().rules->size()){events_.pop_front();}
    else if(events_.size()>1){std::rotate(events_.begin(),events_.begin()+1,events_.end());}
  }
  void clear()noexcept{events_.clear();recent_.clear();}
private:
  struct Recent {std::uint64_t event;core::TorrentHash hash;std::int64_t addedMillis;Clock::time_point end;};
  std::deque<Event> events_;std::deque<Recent> recent_;
};
} // namespace biglybtc::app::detail
