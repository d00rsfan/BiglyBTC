// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <climits>
namespace biglybtc::services::msgsync {
class SendPace final {
 static std::int32_t add(std::int32_t a,std::int32_t b)noexcept{return std::bit_cast<std::int32_t>(std::uint32_t(a)+std::uint32_t(b));}
 static std::int32_t subtract(std::int32_t a,std::int32_t b)noexcept{return std::bit_cast<std::int32_t>(std::uint32_t(a)-std::uint32_t(b));}
 std::array<std::int32_t,30> times_{};unsigned next_=0;
public:
 // Original per-handler 30 messages / 60 seconds pacing. The caller schedules
 // its delay without holding a source permit or blocking the room thread.
 std::int32_t reserve(std::int64_t monotonicMillis)noexcept{
  const auto now=std::bit_cast<std::int32_t>(std::uint32_t(monotonicMillis/1000)),limit=subtract(now,60);std::int32_t oldest=INT32_MAX,count=0;
  for(auto time:times_){if(time>0&&time>=limit){++count;if(time<oldest){oldest=time;}}}
  const auto remaining=30-count,seconds=subtract(add(oldest,60),now);std::int32_t delay=0;if(count<7){delay=0;}else if(count<15){delay=1000;}else if(seconds<=0||remaining<=0){delay=2000;}else{delay=std::bit_cast<std::int32_t>(std::uint32_t(seconds)*1000U)/remaining;}
  times_[next_]=now;next_=(next_+1)%times_.size();return delay;
 }
};
}
