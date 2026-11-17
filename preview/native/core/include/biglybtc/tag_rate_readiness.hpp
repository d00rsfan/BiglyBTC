// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_rate_limits.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
namespace biglybtc::engine {class SdkTagRateGroup;}
namespace biglybtc::core {
// Passive exact SDK class readiness,not profile/source permission. Only the
// actual SDK owner can construct a positive witness. Reading never calls SDK.
class TagRateReadiness final {
public:
 TagRateReadiness()=default;
 bool currentFor(std::uint64_t tag,SavedTagRateLimits limits)const noexcept{
  const auto state=state_.lock();return epoch_&&state&&state->load(std::memory_order_acquire)==epoch_
      &&tag==tag_&&limits.upload==limits_.upload&&limits.download==limits_.download;
 }
private:
 friend class biglybtc::engine::SdkTagRateGroup;
 TagRateReadiness(std::weak_ptr<const std::atomic_uint64_t> state,std::uint64_t epoch,
     std::uint64_t tag,SavedTagRateLimits limits):state_(std::move(state)),epoch_(epoch),tag_(tag),limits_(limits){}
 std::weak_ptr<const std::atomic_uint64_t> state_;
 std::uint64_t epoch_=0,tag_=0;SavedTagRateLimits limits_{};
};
}
