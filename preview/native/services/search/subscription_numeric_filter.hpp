// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "subscription_values.hpp"
namespace biglybtc::search {
// Values only. Loading these filters never approves a source or acquisition.
// The exact raw object is owned and never rewritten; signed parent bytes stay
// with the real source snapshot, outside this codec.
struct SubscriptionNumericFilter {
  std::int64_t minSize=-1,maxSize=-1,minSeeds=-1,maxSeeds=-1,minPeers=-1;
  std::optional<std::string> originalJson;
  bool active()const noexcept;
};
struct SubscriptionNumericResult {
  bool namePresent=true;
  std::int64_t size=-1;
  std::int32_t seeds=-1,peers=-1;
};
std::expected<SubscriptionNumericFilter,SubscriptionValueError> parseSubscriptionNumericFilter(
    std::optional<std::string> actualFiltersJson);
// Inputs are the actual phase-specific FilterableResult getter values. The
// codec does not equate fresh WebResult -1 with saved-result zero normalization.
bool isSubscriptionNumericFiltered(const SubscriptionNumericFilter&,
    const SubscriptionNumericResult&)noexcept;
} // namespace biglybtc::search
