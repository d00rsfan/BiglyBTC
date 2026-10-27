#pragma once

#include "biglybtc/bencode.hpp"
#include "biglybtc/queue_slots.hpp"
#include "biglybtc/queue_download_probe.hpp"
#include "biglybtc/queue_target_statistics.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace biglybtc::core {
// Numeric values are the original StartStopRulesDefaultPlugin values.
enum class QueueRankMode { None = 0, SeedPeerRatio = 1, SeedCount = 2, Timed = 3, PeerCount = 4 };
enum class QueueFirstPriorityMode { All = 0, Any = 1 };
enum class QueueDownloadOrder { Position = 0, Seeds = 1, Speed = 2, ReverseSeeds = 3,
  Size = 4, ReverseSize = 5, Eta = 6, FilePriority = 7, Remaining = 8 };
struct QueuePolicy {
  std::int32_t maximumDownloads = 4, maximumActive = 4, minimumDownloads = 1;
  bool linkMinimumDownloads = false, ignoreChecking = false;
  bool maximumActiveWhenSeedingEnabled = false;
  std::int32_t maximumActiveWhenSeeding = 0;
  QueueRankMode rankMode = QueueRankMode::SeedPeerRatio;
  QueueDownloadOrder downloadOrder = QueueDownloadOrder::Position;
  bool preferLargerSwarms = true;
  std::int32_t seedCountFallback = 0, minimumPeersToBoostNoSeeds = 1;
  std::int32_t minimumDownloadBytesPerSecond = 512, minimumSeedBytesPerSecond = 512;
  std::int32_t minimumSeedSeconds = 300, minimumSeedActiveSeconds = 900;
  std::int32_t timedMinimumWithPeersSeconds = 0, timedMaximumWithPeersSeconds = 0;
  std::int32_t maximumStalledSeeds = 5;
  bool stalledSeedsIgnoreZeroPeers = true, autoStartZeroPeers = false;
  QueueFirstPriorityMode firstPriorityMode = QueueFirstPriorityMode::Any;
  std::int32_t firstPriorityShareRatio = 500;
  std::int32_t firstPrioritySeedMinutes = 0, firstPriorityActiveMinutes = 0;
  std::int32_t firstPriorityIgnoreSeedPeerRatio = 0, firstPriorityIgnoreIdleMinutes = 0;
  bool firstPriorityIgnoreZeroPeers = true; // Original modern UI; read uses ui=az2 override.
  std::int32_t ignoreShareRatio = 0, ignoreShareRatioSeedStart = 0;
  std::int32_t ignoreSeedCount = 0, ignorePeerSeedRatio = 0, ignorePeerSeedRatioSeedStart = 0;
  bool ignoreZeroPeers = true;
  std::int32_t peersAsFullCopy = 0, fakeFullCopySeedStart = 0;
  bool stopOnceBandwidthMet = true, preventNewSeedsAtUploadLimit = false;
  std::int32_t uploadLimitSlack = 95;
  bool uploadLimitSlackIsPercent = true;
  bool newSeedsMoveTop = true, retainForceStartOnComplete = false;
  // Light seeding still requires its queued tracker/activation provider.
  // Flexible slots are actual global transition headroom, not a larger quota.
  bool lightSeedingEnabled = true;
  std::int32_t reservedLightSeedSlots = 4, flexibleSeedSlots = 5;
  // Original ordinary speed/ETA test controls; native payload scope is explicit.
  std::int32_t downloadProbeSeconds = 120, downloadProbeRetestMinutes = 30;
  bool downloadProbeActive = false;
  bool operator==(const QueuePolicy&) const = default;
};
enum class QueuePolicyErrorCode { InvalidConfiguration, InvalidFacts, Limit, Memory, Unsupported };
struct QueuePolicyError { QueuePolicyErrorCode code; std::string key; };
// Missing doc/keys use original defaults. Present bad types/ranges fail with key.
// Java float Stop Ratio (integer or byte string) becomes thousandths using binary32.
std::expected<QueuePolicy, QueuePolicyError> readQueuePolicy(const Document*);

// State supplied by the worker, never persisted by the reducer. Times are explicit
// monotone milliseconds from the same owner. A new admission starts with {}.
struct QueueActivityMemory {
  bool downloading = false, seeding = false;
  std::optional<std::int64_t> downloadChangeMillis, seedChangeMillis;
  std::optional<std::int64_t> downloadHighLatencyMillis, seedHighLatencyMillis;
  std::optional<std::int64_t> lastObservationMillis;
  std::int64_t timedMinimumMillis = 0;
  QueueStaleSeedTiming staleTiming;
  bool operator==(const QueueActivityMemory&) const = default;
};
enum class QueueTorrentState { Queued, Waiting, Preparing, Checking, Downloading, Seeding, Stopping, Stopped, Failed };
struct QueueSwarm {
  std::int32_t seeds = 0, peers = 0; bool valid = false;
  // Actual original AggregateScrapeResult returns RT_SUCCESS even when its
  // counts are still unknown. Kept distinct from last rank-success authority.
  std::optional<bool> startupSuccess{};
};
struct QueueTorrentFacts {
  std::uint64_t token = 0; // Unique nonzero worker identity, never an array index.
  std::uint32_t position = 1;
  QueueTorrentState state = QueueTorrentState::Queued;
  bool complete = false, persistent = true, controllable = true;
  bool forceStart = false, manuallyPaused = false, moving = false, newlyCompleted = false;
  // Optional original external FP authority; never inferred from tag membership.
  bool externallyFirstPriority = false, externallyNotFirstPriority = false;
  std::optional<std::int32_t> shareRatio; //Java int; −1 infinity, other negatives can be wrapped source arithmetic.
  std::optional<std::int32_t> minimumShareRatio, maximumShareRatio;
  std::optional<std::int64_t> secondsDownloading, secondsSeeding, secondsSinceLastUpload;
  std::optional<std::int64_t> runningMillis, seedingMillis, secondsSinceLastActive;
  std::optional<QueueSwarm> swarm; // Already excludes self. Unknown != known zero.
  std::optional<std::int32_t> connectedLeechers;
  std::optional<std::int64_t> downloadBytesPerSecond, uploadBytesPerSecond;
  std::optional<std::int64_t> smoothedUploadBytesPerSecond;
  std::optional<std::int64_t> diskReadLatencyMillis, diskWriteLatencyMillis;
  std::optional<std::int64_t> sizeBytes, remainingBytes, etaSeconds;
  // Original comparator uses elements1 then3 descending from the four stats.
  std::optional<std::vector<std::int64_t>> filePriority;
  std::int32_t staleRankOffset = 0;
  QueueActivityMemory activity;
  std::optional<QueueProbeObservation> downloadProbe{};
  // Opt in only for an actual admission with measured disk facts. Missing
  // required inputs withdraw stale-rank authority rather than imply zero age.
  bool sourceStaleTiming = false;
};
struct QueueContext {
  std::int64_t nowMillis = 0, ownerStartedMillis = 0;
  bool seedingOnly = false;
  std::optional<std::int64_t> uploadBytesPerSecond, downloadBytesPerSecond;
  std::optional<std::int64_t> smoothedUploadBytesPerSecond;
  std::int64_t uploadLimitBytesPerSecond = 0, downloadLimitBytesPerSecond = 0;
  bool downloadProbeUnavailable = false;
};
enum class QueueAction { Preserve, Run, Queue };
enum class QueueReason {
  Eligible, ManualPause, Stopped, Uncontrollable, Forced, Preparing,
  FirstPriority, MinimumSeedingTime, DownloadLimit, ActiveLimit, StalledLimit,
  UploadLimit, WaitingForScrape, UnknownFacts, IgnoreShareRatio, IgnoreZeroPeers,
  IgnoreSeedCount, IgnorePeerSeedRatio, TimedRotation, Checking
};
struct QueueDecision {
  std::uint64_t token = 0;
  QueueAction action = QueueAction::Preserve;
  QueueReason reason = QueueReason::Eligible;
  std::int32_t seedingRank = 0;
  bool firstPriority = false, firstPriorityKnown = true;
  bool activelyDownloading = false, activelySeeding = false, activityKnown = true;
  bool clearForceStart = false, moveNewSeedToTop = false;
  QueueActivityMemory activity;
  std::optional<std::int32_t> staleRankOffset{};
};
struct QueuePlan {
  std::vector<QueueDecision> decisions; // Input order; stable tokens bind effects.
  // Provider facts missing: effects remain conservative, diagnostic bits nonzero.
  // bit0 unknown swarm, bit1 missing ratio/time/rate, bit2 light slots unavailable,
  // bit3 disk latency unavailable; bit4 missing probe/ETA/combined-byte or
  // configured-bandwidth smoothing inputs (payload-scoped probes stay explicit).
  // bit6 missing authoritative source stale-rank timing facts.
  std::uint32_t unavailableInputs = 0;
  std::optional<QueueSlotBudget> seedSlots; // Last planned current/global guard; no synthetic light owners.
};
inline constexpr std::size_t MaximumQueueTorrents = 8192;
// No I/O, clocks, mutation, callbacks or LT dependency. Error returns no plan.
// Apply Queue effects before Run effects; root revalidates identities/admissions.
std::expected<QueuePlan, QueuePolicyError> evaluateQueue(const QueuePolicy&,
    std::span<const QueueTorrentFacts>, const QueueContext&);
} // namespace biglybtc::core
