#pragma once

#include "biglybtc/saved_history.hpp"
#include "profile.hpp"

#include <stop_token>

namespace biglybtc::storage {

struct SavedHistoryDayView {
  std::optional<core::SavedHistoryDay> snapshot;
  bool missing() const noexcept { return !snapshot; }
};
// Borrow one held lease; open only stats/YYYY/MM/DD.dat for the requested UTC
// date, using no-follow directory descriptors and the existing bounded regular
// file reader (1MiB). Missing component means no snapshot; empty/header-only file
// is present with unknown totals/hours. No scans, fallback/cache.dat, writes,
// directory creation, live manager/clock or extra lease. Checks cancellation
// around reads/projection, not by interrupting syscalls. Separate pathname/fd
// identity checks detect observed replacement, not a transaction against races.
std::expected<SavedHistoryDayView, Error> readSavedHistoryDayView(
    const ProfileLease& lease, core::HistoryDate date, std::stop_token stop = {});

} // namespace biglybtc::storage
