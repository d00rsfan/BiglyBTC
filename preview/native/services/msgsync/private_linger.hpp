// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
namespace biglybtc::services::msgsync {
inline constexpr bool privateMessageUnseen(std::int32_t seen,std::int32_t probable)noexcept{return seen==0&&probable<5;}
// Original handler linger: poll at5s intervals, stop on all-seen or at60s.
// Application shutdown bypasses this per-conversation lifetime immediately.
class PrivateLinger final {
public:
 using Clock=std::chrono::steady_clock;
 explicit PrivateLinger(Clock::time_point start):next_(start+std::chrono::seconds(5)),end_(start+std::chrono::seconds(60)){}
 bool due(Clock::time_point now)const{return now>=next_||now>=end_;}
 bool poll(Clock::time_point now,std::size_t pending){if(!due(now)){return false;}next_=now+std::chrono::seconds(5);return now>=end_||pending==0;}
private:Clock::time_point next_,end_;
};
}
