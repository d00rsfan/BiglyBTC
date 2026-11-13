// SPDX-License-Identifier: GPL-2.0-only
#include "read_schedule.hpp"
#include <algorithm>
#include <limits>
#include <set>
namespace biglybtc::plugins::rating::publication {
namespace {constexpr std::int64_t hour=60*60*1000;std::int64_t add(std::int64_t a,std::int64_t b){return a>INT64_MAX-b?INT64_MAX:a+b;}}
std::int64_t ReadSchedule::period(const ReadHint& hint,std::int64_t now)noexcept{
 if(hint.complete){return hint.stopped&&hint.scrapeSeeds==std::optional<std::int32_t>{0}?2*hour:8*hour;}
 return hint.addedMillis>0&&now>hint.addedMillis&&now-hint.addedMillis>24*hour?4*hour:hour;
}
std::expected<void,ReadSchedule::Failure> ReadSchedule::update(std::span<const ReadHint> hints){
 if(hints.size()>maximumDownloads){return std::unexpected(Failure::Limit);}std::set<std::string_view> seen;
 for(const auto& hint:hints){if(hint.id.empty()||hint.id.size()>128||hint.addedMillis<0||!seen.insert(hint.id).second){return std::unexpected(Failure::Invalid);}}
 std::erase_if(entries_,[&](const auto& pair){return !seen.contains(pair.first);});
 for(const auto& hint:hints){auto [at,created]=entries_.try_emplace(hint.id,Entry{hint,0,!hint.scored});if(!created){at->second.hint=hint;}else if(!hint.scored){next_.reset();}}
 return {};
}
std::expected<void,ReadSchedule::Failure> ReadSchedule::request(std::string_view id,bool priority){
 auto at=entries_.find(id);if(at==entries_.end()){return std::unexpected(Failure::Missing);}auto& entry=at->second;if(!entry.requested||priority){entry.last=priority?-1:0;entry.requested=true;}next_.reset();return {};
}
std::optional<std::string> ReadSchedule::take(std::int64_t now){
 if(now<0||active_||(next_&&*next_>now)){return {};}
 Entry* selected=nullptr;std::int64_t earliest=0;Entry* priority=nullptr;
 for(auto& [id,entry]:entries_){(void)id;if(!entry.requested){continue;}if(entry.last==-1){selected=&entry;priority=&entry;earliest=now;}
  else if(entry.last==0){if(!selected||earliest>now||!entry.hint.complete){selected=&entry;}earliest=now;}
  else{const auto due=add(entry.last,period(entry.hint,now));if(earliest==0||due<earliest){earliest=due;selected=&entry;}}
 }
 if(priority){selected=priority;earliest=now;}if(!selected){next_.reset();return {};}next_=earliest;if(earliest>now){return {};}selected->last=now;active_=true;return selected->hint.id;
}
void ReadSchedule::complete(std::int64_t now)noexcept{active_=false;next_=add(std::max<std::int64_t>(0,now),20000);}
void ReadSchedule::reset()noexcept{entries_.clear();next_.reset();active_=false;}
}
