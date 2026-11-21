// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/tag_rate_limits.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
namespace biglybtc::engine {class SdkTagRateGroup;}
namespace biglybtc::core {
 // Passive exact SDK class readiness,not profile/source permission. Optional
 // shared epochs read only an atomic cell,without SDK work or source locks.
class TagRateReadiness final {
public:
 TagRateReadiness()=default;
 bool currentFor(std::uint64_t tag,SavedTagRateLimits limits)const noexcept{
  const auto state=state_.lock();
  const auto shared=sharedRead_?shared_.lock():std::shared_ptr<const void>{};
  return epoch_&&state&&state->load(std::memory_order_acquire)==epoch_
      &&(!sharedRead_||(shared&&sharedEpoch_&&sharedRead_(shared.get())==sharedEpoch_))
      &&tag==tag_&&limits.upload==limits_.upload&&limits.download==limits_.download;
 }
private:
 friend class biglybtc::engine::SdkTagRateGroup;
 TagRateReadiness(std::weak_ptr<const std::atomic_uint64_t> state,std::uint64_t epoch,
     std::uint64_t tag,SavedTagRateLimits limits):state_(std::move(state)),epoch_(epoch),tag_(tag),limits_(limits){}
 TagRateReadiness(std::weak_ptr<const std::atomic_uint64_t> state,std::uint64_t epoch,
     std::uint64_t tag,SavedTagRateLimits limits,std::weak_ptr<const void> shared,
     std::uint64_t (*read)(const void*)noexcept,std::uint64_t sharedEpoch)
     :state_(std::move(state)),epoch_(epoch),tag_(tag),limits_(limits),shared_(std::move(shared)),sharedRead_(read),sharedEpoch_(sharedEpoch){}
 std::weak_ptr<const std::atomic_uint64_t> state_;
 std::uint64_t epoch_=0,tag_=0;SavedTagRateLimits limits_{};
 std::weak_ptr<const void> shared_;
 std::uint64_t (*sharedRead_)(const void*)noexcept=nullptr;
 std::uint64_t sharedEpoch_=0;
};
}
