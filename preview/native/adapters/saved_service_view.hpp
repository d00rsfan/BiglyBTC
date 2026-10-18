#pragma once

#include "biglybtc/saved_services.hpp"
#include "profile.hpp"

#include <stop_token>

namespace biglybtc::storage {

struct SavedHostingView {
  std::optional<core::SavedHosting> snapshot;
  std::optional<RecoverySource> recoverySource;
  bool missing() const noexcept { return !snapshot.has_value(); }
};
struct SavedSharesView {
  std::optional<core::SavedShares> snapshot;
  std::optional<RecoverySource> recoverySource;
  bool missing() const noexcept { return !snapshot.has_value(); }
};

// Borrow the caller's held lease; never acquire a lease or save/rewrite files.
// Only tracker.config/sharing.config and the qualified loader's .bak/.saving
// candidates are read, under core::Limits{} rather than permissive config caps.
// Missing means all three candidate files were observed absent; a loaded empty
// projection still has snapshot and recoverySource. ConfigFile's Missing can
// also mean absent primary with malformed recovery candidates, so this adapter
// confirms absence with bounded readFile calls before returning a missing view.
// Separate file reads are not a transactional snapshot against external edits.
// Stop is checked before/after reads and projection, not inside blocking reads.
// Raw source bytes/unknown fields stay owned by the core snapshot. Its existing
// duplicate-dictionary-key rejection and explicit native projection caps apply.
// No cache/path chasing, clocks, scans, live tracker/sharing or metadata joins.
std::expected<SavedHostingView, Error> readSavedHostingView(
    const ProfileLease& lease, std::stop_token stop = {});
std::expected<SavedSharesView, Error> readSavedSharesView(
    const ProfileLease& lease, std::stop_token stop = {});

} // namespace biglybtc::storage
