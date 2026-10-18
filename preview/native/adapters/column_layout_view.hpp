#pragma once

#include "biglybtc/column_layout.hpp"
#include "profile.hpp"

#include <optional>
#include <stop_token>

namespace biglybtc::storage {

struct ColumnLayoutsView {
  std::optional<core::ColumnLayouts> snapshot;
  std::optional<RecoverySource> recoverySource;
  bool missing() const noexcept { return !snapshot.has_value(); }
};

// Synchronously borrow the held, unmoved lease. Read only fixed tables.config
// and its qualified .bak/.saving candidates using core::Limits{} and default
// projection limits; never acquire/release a lease, save or rewrite any file.
// Missing means the three candidates were observed absent. A loaded empty root
// has an engaged snapshot/source; diagnosed table/field faults stay in snapshot.
// ConfigFile::Missing can hide malformed recovery candidates, so confirm exact
// candidate absence before returning missing. Separate reads are not atomic
// against external edits. Stop is checked before/after reader/projection calls,
// not inside blocking reads. Result owns raw/projection data independently of
// this call and lease. No global config, path chasing, scans, UI or normalization.
std::expected<ColumnLayoutsView, Error> readColumnLayoutsView(
    const ProfileLease& lease, std::stop_token stop = {});

} // namespace biglybtc::storage
