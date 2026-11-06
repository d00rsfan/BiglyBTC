// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_subscription_source.hpp"
namespace biglybtc::search::detail {
// Internal normal-session recovery only. It reads one fixed owned Native intent,
// accepts only its exact before/after file states and never starts network I/O.
std::expected<void,SavedRssSourceError> recoverSubscriptionWrite(
    const storage::ProfileLease&,const std::filesystem::path& profileDirectory);
std::expected<SubscriptionResults,SavedRssSourceError> readSubscriptionHistory(
    const storage::ProfileLease&,const std::filesystem::path&,const SavedSubscriptionSelector&);
}
