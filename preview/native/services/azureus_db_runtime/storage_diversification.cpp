// SPDX-License-Identifier: GPL-2.0-or-later
#include "storage_diversification.hpp"
#include <bit>
namespace biglybtc::services::azureus_storage_stats {
namespace {
std::int64_t add(std::int64_t a,std::int64_t b) noexcept {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)+std::bit_cast<std::uint64_t>(b));
}
std::int64_t subtract(std::int64_t a,std::int64_t b) noexcept {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)-std::bit_cast<std::uint64_t>(b));
}
bool suspended(DiversificationClock& clock,DiversificationControl& control) noexcept {
  const auto until=control.suspendedUntilMilliseconds();
  return until>0 && until>clock.nowMilliseconds();
}
}
std::expected<DiversifyingKey,Error> DiversifyingKey::create(
    DiversificationClock& clock,DiversificationControl& control) noexcept {
  const auto now=clock.nowMilliseconds();
  const auto jitter=control.expiryJitterMilliseconds();
  if(!jitter || *jitter<0 || *jitter>=ExpiryJitterBound){return std::unexpected(Error::Invalid);}
  DiversifyingKey result;result.expiry_=add(add(now,MinimumExpiry),*jitter);return result;
}
PolicyTransition DiversifyingKey::changed(std::int32_t entries,std::int32_t bytes,
    DiversificationClock& clock,DiversificationControl& control) noexcept {
  counters_.changed(entries,bytes); // Preserve qualified Java int wrap + negative clamps.
  if(type_==1 && !suspended(clock,control) && (counters_.bytes_>32768 || counters_.entries_>2048)){
    type_=3;return PolicyTransition::Size;
  }
  return PolicyTransition::None;
}
std::expected<PolicyTransition,Error> DiversifyingKey::read(std::span<const std::uint8_t> ip,
    DiversificationClock& clock,DiversificationControl& control) noexcept {
  if(ip.size()!=4 && ip.size()!=16){return std::unexpected(Error::Invalid);}
  if(type_!=1){return PolicyTransition::None;}
  const auto now=clock.nowMilliseconds();
  const bool frequency=subtract(now,counters_.start_)>180000 && counters_.active_ && counters_.bloomEntries_>90;
  if(!counters_.read(ip,now)){return std::unexpected(Error::Invalid);}
  if(frequency && !suspended(clock,control)){type_=2;return PolicyTransition::Frequency;}
  return PolicyTransition::None;
}
DiversificationRead DiversifyingKey::diversification(DiversificationClock& clock) noexcept {
  if(type_!=1 && expiry_<clock.nowMilliseconds()){
    type_=1;return {type_,PolicyTransition::Expired};
  }
  return {type_,PolicyTransition::None};
}
PolicyStats DiversifyingKey::statistics(DiversificationClock& clock) noexcept {
  auto value=counters_.stats();const auto state=diversification(clock);value.diversification=state.type;
  return {value,state.transition};
}
}
