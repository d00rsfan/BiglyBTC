// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace biglybtc::services::i2p::dht::detail {
// Correlation memory only; this type cannot authorize a packet or source.
class TransactionHistory final {
public:
  using Id=std::array<std::uint8_t,8>;
  static constexpr std::size_t Capacity=4096;
  // A lookup lasts at most120s. Keep IDs longer: one bounded native queue30s
  // plus the request and reply I2CP datagram expirations of60s each.
  static constexpr std::int64_t RetentionMilliseconds=150000;
  TransactionHistory(){entries_.reserve(Capacity);}
  bool advance(std::int64_t now)noexcept{
    if(now<0||now<last_||now>std::numeric_limits<std::int64_t>::max()-RetentionMilliseconds){return false;}
    last_=now;
    const auto first=std::find_if(entries_.begin(),entries_.end(),[&](const auto& value){return value.expires>now;});
    entries_.erase(entries_.begin(),first);return true;
  }
  std::size_t size()const noexcept{return entries_.size();}
  bool available()const noexcept{return entries_.size()<Capacity;}
  bool contains(const Id& id)const noexcept{return std::any_of(entries_.begin(),entries_.end(),[&](const auto& value){return value.id==id;});}
  std::optional<std::int64_t> earliestExpiry()const noexcept{return entries_.empty()?std::nullopt:std::optional<std::int64_t>{entries_.front().expires};}
  std::int64_t wakeup(std::int64_t deadline)const noexcept{return available()?deadline:std::min(deadline,entries_.front().expires);}
  bool remember(const Id& id,std::int64_t now){
    if(!advance(now)||!available()||contains(id)){return false;}
    entries_.push_back({id,now+RetentionMilliseconds});return true;
  }
private:
  struct Entry {Id id;std::int64_t expires;};
  std::vector<Entry> entries_;std::int64_t last_=0;
};
// Zero is an exhausted sentinel. UINT64_MAX may be issued once, never wrapped.
inline std::uint64_t nextOperationId(std::uint64_t issued)noexcept{
  return !issued||issued==std::numeric_limits<std::uint64_t>::max()?0:issued+1;
}
} // namespace biglybtc::services::i2p::dht::detail
