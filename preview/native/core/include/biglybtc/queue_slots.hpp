#pragma once
#include <cstdint>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::core {
enum class QueueSlotErrorCode { InvalidFacts, Limit, Memory };
struct QueueSlotError { QueueSlotErrorCode code; std::string field; };
inline constexpr std::size_t MaximumQueueSlotOwners=8192;
struct QueueSlotCounts {
  std::int64_t maximumActive=0, downloads=0;
  std::int64_t currentWaitingOrSeeding=0, currentStalled=0;
  std::int64_t globalActive=0, globalWaiting=0, globalStalled=0;
  std::int32_t maximumStalled=0, reserved=0, flexible=0;
};
struct QueueSlotBudget {
  std::int64_t maximumSeeders=0, currentLimit=0, globalLimit=0;
  std::int64_t currentUsed=0, globalUsed=0;
  bool underCurrent=false, underGlobal=false, overCurrent=false, overGlobal=false;
};
// Counts include actual allocated reserved owners AND still-free placeholders.
// Flexible slots extend only the global guard, never the ordinary rank quota.
// Original formulas are exact when their signed32 intermediates fit; overflow
// is refused rather than wrapping into unlimited/new authority.
std::expected<QueueSlotBudget,QueueSlotError> evaluateQueueSlotBudget(const QueueSlotCounts&);
enum class QueueLightPhase { Never, Eligible, Cooldown };
struct QueueLightMemory {
  QueueLightPhase phase=QueueLightPhase::Never;
  std::optional<std::int64_t> lostEligibilityMillis,lastObservationMillis;
};
struct QueueLightInput {
  // Actual queued lightweight tracker provider, not inferred from a scrape.
  bool providerAvailable=false, hasFreeReservedSlot=false;
  std::int32_t rank=0;
  std::optional<std::int32_t> rankIgnoringZeroPeers;
  std::optional<bool> privateTorrent;
};
struct QueueLightTransition {
  QueueLightMemory memory;
  bool known=false, changed=false, recentlyEligible=false;
  // Original MAX_VALUE sentinel means never; eligible=0, cooldown=elapsed.
  std::int64_t eligibilityMillis=INT64_MAX;
};
// No clocks/IO; one per-admission memory, original120s eligibility cooldown and
// strictly <30s reservation grace. Unknown provider/metadata forbids grants.
std::expected<QueueLightTransition,QueueSlotError> updateQueueLightSeed(
    const QueueLightMemory&,const QueueLightInput&,std::int64_t nowMillis);
struct QueueReservedState { std::vector<std::uint64_t> holders; };
struct QueueReservedObservation {
  std::uint64_t token=0;
  bool retain=false; // Actual seeding/preparing/ready/waiting state only.
  bool request=false, recentlyEligible=false; // Already passed ordinary guards.
};
struct QueueReservedPlan {
  QueueReservedState state;
  std::vector<std::uint64_t> granted;
  std::int64_t available=0;
  bool coverageKnown=false;
};
// Ordered pure plan. Incomplete cohort coverage retains old reservations but
// grants none. Shrinking removes free slots first; active holders are retained
// until actually inactive. No manual pause/stop may be supplied as a request.
std::expected<QueueReservedPlan,QueueSlotError> planQueueReservedSlots(
    std::int32_t configured,const QueueReservedState&,
    std::span<const QueueReservedObservation>,bool completeCoverage);
}
