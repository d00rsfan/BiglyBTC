// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/tag_rate_limits.hpp>
#include <expected>
#include <memory>
#include <optional>
#include <cstdint>
namespace biglybtc::engine {
class SdkTagRateGroup;
class SdkSharedTagBudget;
enum class SdkSharedQuotaError {Unsupported,Thread,Limits,Lifetime,Capacity,Engine};
// One opaque allocator for all sessions of a profile owner's admitted SDK image.
// Separate SDK images cannot borrow its budgets. The allocator
// retains no profile lease. Closing the owner revokes outstanding budgets.
class SdkSharedQuotaScope final {
public:
 static bool supported()noexcept;
 static std::expected<std::unique_ptr<SdkSharedQuotaScope>,SdkSharedQuotaError> create();
 ~SdkSharedQuotaScope();
 SdkSharedQuotaScope(const SdkSharedQuotaScope&)=delete;
 SdkSharedQuotaScope& operator=(const SdkSharedQuotaScope&)=delete;
 std::expected<std::shared_ptr<SdkSharedTagBudget>,SdkSharedQuotaError> acquire(
     std::uint64_t tag,core::SavedTagRateLimits);
 std::expected<void,SdkSharedQuotaError> close()noexcept;
private:
 friend class SdkSharedTagBudget;
 struct Impl;
 explicit SdkSharedQuotaScope(std::shared_ptr<Impl>);
 std::shared_ptr<Impl> impl_;
};
// A SdkTagRateGroup on another SDK-control worker may acknowledge exact
// current limits. Shared policy controls stay with this quota origin.
class SdkSharedTagBudget final {
public:
 ~SdkSharedTagBudget();
 SdkSharedTagBudget(const SdkSharedTagBudget&)=delete;
 SdkSharedTagBudget& operator=(const SdkSharedTagBudget&)=delete;
 std::uint64_t tag()const noexcept;
 bool current()const noexcept;
 // Controls run on the creating worker, outside filesystem/source gates.
 std::optional<core::SavedTagRateLimits> limits()const noexcept;
 std::expected<void,SdkSharedQuotaError> setLimits(core::SavedTagRateLimits);
private:
 friend class SdkSharedQuotaScope;
 friend class SdkTagRateGroup;
 friend class SdkNetworkRates;
 struct Impl;
 struct Witness {
  std::shared_ptr<const void> cookie;
  std::uint64_t (*read)(const void*)noexcept=nullptr;
  std::uint64_t epoch=0;
 };
 explicit SdkSharedTagBudget(std::shared_ptr<Impl>);
 // SDK-class consumers may observe the common cell on their own worker.
 // Public scope/budget controls remain confined to the quota origin worker.
 bool alive()const noexcept;
 bool controls()const noexcept;
 std::optional<core::SavedTagRateLimits> observedLimits()const noexcept;
 Witness witness(core::SavedTagRateLimits)const noexcept;
 int bind(void* session,std::uint32_t group)const noexcept;
 bool matchesBinding(const void* scope,const void* budget)const noexcept;
 static bool supportsNetworkBindings()noexcept;
 int refreshPeer(void*)const noexcept;
 int setProtocolPolicy(void*,int)const noexcept;
 static bool supportsProtocolPolicy()noexcept;
 std::shared_ptr<Impl> impl_;
};
}
