#pragma once

#include "profile.hpp"
#include "biglybtc/overall_document.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <stop_token>

namespace biglybtc::storage {

struct OverallStatisticsView {
  // Missing means all three exact candidates were observed absent. Loaded
  // recovery snapshots are readable, but explicit writes require fresh Primary.
  std::shared_ptr<const core::OverallDocument> snapshot;
  std::optional<RecoverySource> recoverySource;
  bool missing() const noexcept { return !snapshot && !recoverySource; }
};
std::expected<OverallStatisticsView, storage::Error> readOverallStatisticsView(
    const storage::ProfileLease& lease, std::stop_token stop = {});

enum class OverallSaveDisposition { Unchanged, Committed };
struct OverallStatisticsSave {
  OverallStatisticsView current;
  OverallSaveDisposition disposition;
};
// Exact BiglyBT.statistics only. Borrow the held lease; never reacquire, chase
// payload paths, sample counters or derive deltas. Baseline must be pristine
// Primary, or explicit all-missing. Recovered/changed state is Conflict.
// Prepare all owned return data before ConfigFile save/createMissing. Check stop
// before commit only, never turn an actual completed publication into Cancelled.
// Existing ConfigFile sync/rename errors remain errors (possibly published);
// reread/reconcile after uncertainty. No journal or exactly-once epoch claim.
std::expected<OverallStatisticsSave, storage::Error> saveOverallStatisticsView(
    const storage::ProfileLease& lease, const OverallStatisticsView& baseline,
    const core::OverallValues& values, std::stop_token stop = {},
    void (*observer)(storage::SaveStage) = nullptr);

} // namespace biglybtc::storage
