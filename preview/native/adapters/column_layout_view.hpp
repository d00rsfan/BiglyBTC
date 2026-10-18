#pragma once

#include "biglybtc/column_layout.hpp"
#include "biglybtc/classic_preferences.hpp"
#include "profile.hpp"

#include <optional>
#include <stop_token>

namespace biglybtc::storage {

struct ColumnLayoutsView {
  std::optional<core::ColumnLayouts> snapshot;
  std::optional<RecoverySource> recoverySource;
  std::optional<core::ClassicPreferences> nativePreferences;
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
// this call and lease. Invalid/unsupported private preferences fail the optional
// read as a whole. No global config, path chasing, scans, UI or normalization.
std::expected<ColumnLayoutsView, Error> readColumnLayoutsView(
    const ProfileLease& lease, std::stop_token stop = {});

// Explicit native-only update of an existing Primary tables.config. Baseline
// must be pristine/raw-tree-consistent, with the same complete bytes as the
// current Primary under this held lease. The Document+source arguments are a
// byte baseline, not an independent profile-identity capability. Other/released
// leases fail their own validation; same-byte replacement is permitted.
// Unknown/malformed private schemas and non-Primary/missing recovery are never
// promoted or overwritten. Only BiglyBTC.ClassicColumns.v1 is edited; original
// layout/plugin root bytes/order remain under the pure codec's preservation.
// Cancel is observed before entering ConfigFile::save. Once synchronous save
// starts, report its actual outcome even if observer requests stop. Errors after
// a rename can mean replacement already occurred; preserve ConfigFile messages
// and recovery files. No inode CAS or stronger durability guarantee is added.
// Success owns a fresh, pristine parse suitable as a second-save baseline;
// its allocation/parse are staged before save. No createMissing, extra lease,
// directory scan, automatic recovery cleanup, UI or payload access.
std::expected<std::shared_ptr<const core::Document>, Error> saveClassicPreferencesView(
    const ProfileLease& lease, const core::Document& baseline,
    RecoverySource baselineSource, const core::ClassicPreferences& preferences,
    std::stop_token stop = {}, void (*observer)(SaveStage) = nullptr);


// Explicit first native preference save, only when tables.config and both fixed
// recovery candidates are absent. Start from an empty original dictionary and
// prepare the complete owned pristine success baseline before publication.
// ConfigFile::createMissing performs the no-overwrite transaction under the
// borrowed held lease. It never promotes recovery or replaces an existing leaf.
// Stop is checked before synchronous creation only; once creation starts its
// actual outcome wins over later stop. Post-publication errors can mean the
// primary already exists. No stronger race, inode-CAS or durability guarantee
// than ConfigFile::createMissing is added, and no lease is acquired or consumed.
std::expected<std::shared_ptr<const core::Document>, Error> createClassicPreferencesView(
    const ProfileLease& lease, const core::ClassicPreferences& preferences,
    std::stop_token stop = {}, void (*observer)(SaveStage) = nullptr);

} // namespace biglybtc::storage
