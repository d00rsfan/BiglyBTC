// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTDBImpl.applyRF: Copyright (C) Azureus Software, Inc.
#pragma once
#include <bit>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <algorithm>
#include <climits>
namespace biglybtc::services::azureus_db_runtime::survey {
struct Value {std::int64_t created=0;std::int8_t replication=-1;};
struct Observation {std::int64_t wall=0,monotonic=0;std::optional<std::int64_t> previous;std::int32_t jitter=0;};
struct Decision {
 bool eligible=false;
 std::optional<std::int64_t> next;
 bool monotonicObserved=false,wallObserved=false,jitterConsumed=false;
};
enum class Error {Capacity,Jitter};
inline constexpr std::uint8_t factor(std::int8_t value)noexcept{return value==-1?255:static_cast<std::uint8_t>(value)&15;}
inline constexpr std::uint8_t frequency(std::int8_t value)noexcept{return value==-1?255:(static_cast<std::uint8_t>(value)>>4)&15;}
namespace detail {
inline constexpr std::int64_t add(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)+std::bit_cast<std::uint64_t>(b));}
inline constexpr std::int64_t sub(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)-std::bit_cast<std::uint64_t>(b));}
}
// Supplied observations only. A decision grants no DB/network/profile authority.
inline std::expected<Decision,Error> eligibility(std::int8_t diversification,std::span<const Value> values,const Observation& observation)noexcept{
 if(values.size()>256){return std::unexpected(Error::Capacity);}Decision result;result.next=observation.previous;
 if(diversification!=1||values.empty()){return result;}unsigned periodHours=UINT_MAX;std::int64_t oldest=INT64_MAX;
 for(const auto& value:values){const auto copies=factor(value.replication);if(copies==255||copies==0){return result;}periodHours=std::min(periodHours,unsigned(frequency(value.replication)));oldest=std::min(oldest,value.created);}
 if(periodHours==0){result.eligible=true;return result;}result.monotonicObserved=true;const auto now=observation.monotonic;
 if(observation.previous&&*observation.previous>now){return result;}if(observation.jitter< -900000||observation.jitter>=900000){return std::unexpected(Error::Jitter);}
 result.wallObserved=result.jitterConsumed=true;const std::int64_t period=std::int64_t(periodHours)*60*60*1000;
 const auto offset=detail::sub(observation.wall,oldest)%period;auto next=detail::add(detail::add(detail::sub(now,offset),period),observation.jitter);
 if(next<detail::add(now,30*60*1000)){next=detail::add(next,period);}result.next=next;result.eligible=observation.previous.has_value();return result;
}
}
