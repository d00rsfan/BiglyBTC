// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <optional>
namespace biglybtc::app::detail {
// Value-only scheduling state, not a timer, clock provider or effects grant.
// cb8 Handler: 5s callbacks, full period integer-divided by5, time rules on
// callbacks without a full apply. Period0 disables repeat-full, not time work.
class ConstraintCadence final {
public:
  using Clock=std::chrono::steady_clock;
  struct Due {bool full=false,time=false;};
  static Due dueForTick(std::uint32_t tick,std::int32_t seconds,bool fullQueued)noexcept{
    const auto divisor=seconds<=0?0:std::max(10,seconds)/5;
    const bool full=divisor?std::bit_cast<std::int32_t>(tick)%divisor==0:!fullQueued;
    return {full,!full};
  }
  void reset()noexcept{armed_=false;next_.reset();ticks_=0;fullQueued_=false;}
  void fullQueued()noexcept{fullQueued_=true;}
  void start(Clock::time_point now)noexcept{if(!armed_){armed_=true;schedule(now);}}
  bool ready(Clock::time_point now)const noexcept{return next_&&now>=*next_;}
  Due poll(Clock::time_point now,std::int32_t seconds)noexcept{
    if(!armed_){armed_=true;schedule(now);return {};}
    if(!ready(now)){return {};}
    // Original TimerEventPeriodic reschedules after the callback, not a burst
    // of historical callbacks. Unsigned increment preserves Java int wrap.
    ++ticks_;schedule(now);return dueForTick(ticks_,seconds,fullQueued_);
  }
private:
  void schedule(Clock::time_point now)noexcept{
    constexpr auto period=std::chrono::seconds(5);
    if(now>Clock::time_point::max()-period){next_.reset();}else{next_=now+period;}
  }
  bool armed_=false,fullQueued_=false;
  std::uint32_t ticks_=0;
  std::optional<Clock::time_point> next_;
};
} // namespace biglybtc::app::detail
