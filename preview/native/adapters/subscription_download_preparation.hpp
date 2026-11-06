// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "subscription_download_values.hpp"
#include <string>
namespace biglybtc::storage {
// Trusted private-coordinator crash seam only; production passes null.
enum class SubscriptionDownloadStateStage { SavingSynced };
// Pure fixed recipe inputs; this value grants no source/write/target authority.
struct FixedSubscriptionDownloadPreparation {
  std::string rawShortId;
  bool newAssociation=false;
  search::detail::SubscriptionRawScrapeCounts rawCounts;
};
} // namespace biglybtc::storage
