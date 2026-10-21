#pragma once

#include "biglybtc/saved_history_range.hpp"
#include "profile.hpp"

#include <stop_token>

namespace biglybtc::storage {

// Borrow one held lease. Reads only fixed stats/YYYY/MM/DD.dat regular files,
// one <=1MiB input at a time, through no-follow descriptors. All alone inventories
// valid date names to select its earliest date; every non-dot directory entry
// counts toward the core65536-entry bound. Cache/non-date names stay inert.
// Missing files remain unknown; present empty files remain present. Any unsafe
// path, parser/arithmetic/size failure, observed replacement or cancellation
// fails the entire query with no prefix. Individual fd/path checks are not a
// transaction against concurrent edits across multiple files or the inventory.
// No extra lease, hidden clock, cache, payload access, recovery or writes.
std::expected<core::SavedHistoryRange, Error> readSavedHistoryRangeView(
    const ProfileLease&, core::HistoryRangeRequest, std::stop_token = {});

} // namespace biglybtc::storage
