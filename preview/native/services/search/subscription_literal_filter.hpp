// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "subscription_numeric_filter.hpp"

namespace biglybtc::search {
// Passive values only. Original's regex fallback is equivalent to contains
// only in this closed ASCII token/name domain; other active cases refuse.
struct SubscriptionLiteralFilter {
  std::vector<std::string> include,exclude;
  std::optional<std::string> originalJson;
  bool active()const noexcept{return !include.empty()||!exclude.empty();}
};
struct SubscriptionScanFilter {
  SubscriptionNumericFilter numeric;
  SubscriptionLiteralFilter literal;
};
std::expected<SubscriptionScanFilter,SubscriptionValueError> parseSubscriptionScanFilter(
    std::optional<std::string> actualFiltersJson);
// Null names are filtered. With active text, non-ASCII names explicitly refuse;
// no row is silently admitted by omitting Java UNICODE_CASE/Locale.US behavior.
std::expected<bool,SubscriptionValueError> isSubscriptionLiteralFiltered(
    const SubscriptionLiteralFilter&,const std::optional<QString>& actualName);
} // namespace biglybtc::search
