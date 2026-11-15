// SPDX-License-Identifier: GPL-2.0-or-later
// Local storageKey policy from original core cb8c6cb9 DHTPluginStorageManager.
#pragma once
#include "storage_stats.hpp"
namespace biglybtc::services::azureus_storage_stats {
// Every sample is supplied by the actual serialized operation, never a timer.
class DiversificationClock {
public:
  virtual ~DiversificationClock() = default;
  virtual std::int64_t nowMilliseconds() noexcept = 0;
};
class DiversificationControl {
public:
  virtual ~DiversificationControl() = default;
  // Exact RandomUtils.nextLong(86400000) result, not a new PRNG or seed.
  virtual std::expected<std::int64_t,Error> expiryJitterMilliseconds() noexcept = 0;
  // Original manager suspension deadline; <=0 means not suspended. A positive
  // deadline consumes a fresh wall sample only when suspendDivs is queried.
  virtual std::int64_t suspendedUntilMilliseconds() const noexcept = 0;
};
enum class PolicyTransition { None, Frequency, Size, Expired };
struct DiversificationRead {std::int8_t type=1;PolicyTransition transition=PolicyTransition::None;};
struct PolicyStats {Stats value;PolicyTransition transition=PolicyTransition::None;};
struct DiversificationCheckpoint {std::int8_t type=1;std::int64_t expiry=0;bool operator==(const DiversificationCheckpoint&)const=default;};
class DiversifyingKey final {
public:
  static constexpr std::int64_t MinimumExpiry=172800000,ExpiryJitterBound=86400000;
  static std::expected<DiversifyingKey,Error> create(DiversificationClock&,DiversificationControl&) noexcept;
  // Original deserialisation restores only type/expiry, never read counters or
  // entropy/time samples. Export is observational and consumes no clock sample.
  static std::expected<DiversifyingKey,Error> restore(DiversificationCheckpoint) noexcept;
  DiversificationCheckpoint checkpoint()const noexcept{return {type_,expiry_};}
  PolicyTransition changed(std::int32_t entries,std::int32_t bytes,DiversificationClock&,DiversificationControl&) noexcept;
  std::expected<PolicyTransition,Error> read(std::span<const std::uint8_t>,DiversificationClock&,DiversificationControl&) noexcept;
  DiversificationRead diversification(DiversificationClock&) noexcept;
  PolicyStats statistics(DiversificationClock&) noexcept;
  std::int64_t expiryMilliseconds() const noexcept{return expiry_;}
private:
  Key counters_;
  std::int8_t type_=1;
  std::int64_t expiry_=0;
};
}
