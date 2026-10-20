#pragma once

#include "biglybtc/saved_history_month.hpp"
#include "profile.hpp"

#include <stop_token>

namespace biglybtc::storage {

// Borrow one held lease for 30 unchanged daily reads, oldest first. Missing
// files remain absent; present empty files retain owned no-observation snapshots.
// All six signed counters and hourly observations are copied without summing.
// Any daily error/cancellation fails the whole result, never publishes a prefix.
// One inherited bounded 1MiB input buffer is used at a time. No extra lease,
// scans, cache, writes, clock or clamping. Per-file checks are inherited; this
// is not a transactional multi-file snapshot or a calendar-month aggregate.
std::expected<core::SavedHistoryMonth, Error> readSavedHistoryMonthView(
    const ProfileLease& lease, core::HistoryDate endingDate, std::stop_token stop = {});

} // namespace biglybtc::storage
