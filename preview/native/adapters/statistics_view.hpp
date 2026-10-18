#pragma once

#include "biglybtc/saved_statistics.hpp"
#include "profile.hpp"

#include <stop_token>

namespace biglybtc::storage {

struct SavedStatisticsView {
  std::optional<core::SavedStatistics> snapshot;
  std::optional<RecoverySource> recoverySource;
  bool missing() const noexcept { return !snapshot.has_value(); }
};

// Borrow the held profile directory. Read only the original fixed default leaf
// BiglyBT.statistics and its .bak/.saving candidates; never widen ConfigFile's
// generic .config filename rule or initialize an original statistics manager.
// Whole input limits are core::Limits{} (4MiB/100000 nodes/depth64).
// A complete dictionary Primary wins, including an empty one. Invalid `all`
// stops recovery. Absent Primary tries Saving then Backup; malformed Primary
// tries Backup then Saving. Unsafe/read/oversize/Memory errors are fatal.
// Missing means all three leaves were observed absent, not invalid or zero data.
// Cancellation surrounds each read/projection, not an interrupted read syscall.
// Reads are separate observations, not an atomic snapshot against other writers.
// Returned raw bytes/unknown fields are owned; no repair, save, scan, extra lease,
// payload access, filename parameter or actual live-session statistics is added.
std::expected<SavedStatisticsView, Error> readSavedStatisticsView(
    const ProfileLease& lease, std::stop_token stop = {});

} // namespace biglybtc::storage
