// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
namespace biglybtc::search::detail {
inline constexpr std::string_view SubscriptionDownloadMapAttribute="Plugin.<internal>.azsubs.subs_info";
enum class SubscriptionDownloadValueError {Malformed,Limit,Memory};
struct SubscriptionRawScrapeCounts {std::optional<std::int64_t> seeds,peers;};
// Raw saved Long values BEFORE missing/negative UI normalization/int narrowing.
std::optional<std::int64_t> subscriptionRawScrapeCache(SubscriptionRawScrapeCounts)noexcept;
struct SubscriptionDownloadValues {
  std::string attributes;
  bool changed=false,cacheChanged=false,sidChanged=false;
};
// PURE bytes -> bytes. These values grant no file/write/intake/source authority.
// The future private owner must supply actual attributes, source SID/raw counts,
// and the actual subscription-side association plan's changed bit. False skips
// the manager callback entirely; it must not inspect/repair that plugin map.
// Scrape preparation is independent and occurs even when the callback is skipped.
std::expected<SubscriptionDownloadValues,SubscriptionDownloadValueError> planSubscriptionDownloadValues(
    std::string encodedAttributes,std::string_view rawShortId,bool newSubscriptionAssociation,
    SubscriptionRawScrapeCounts);
} // namespace biglybtc::search::detail
