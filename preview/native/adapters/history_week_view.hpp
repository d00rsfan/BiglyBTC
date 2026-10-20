#pragma once

#include "biglybtc/saved_history_week.hpp"
#include "profile.hpp"

#include <stop_token>

namespace biglybtc::storage {

// Borrow one held lease and perform seven unchanged daily reads, oldest first.
// Missing files/components remain absent rows; present empty files have owned
// snapshots with unknown totals. Observed zero and signed adjustments survive.
// Any daily error/cancellation fails the whole result, never publishes a prefix.
// At most seven1MiB files are parsed sequentially, with one input buffer at a
// time. No scans/cache/recovery/writes/extra lease/clock. Per-file identity
// checks are inherited; this is not a transactional multi-file snapshot.
std::expected<core::SavedHistoryWeek, Error> readSavedHistoryWeekView(
    const ProfileLease& lease, core::HistoryDate endingDate, std::stop_token stop = {});

} // namespace biglybtc::storage
