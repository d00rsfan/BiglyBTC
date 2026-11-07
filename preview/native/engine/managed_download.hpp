#pragma once
#include "biglybtc/library.hpp"
#include "biglybtc/queue_policy.hpp"
#include "biglybtc/queue_target_statistics.hpp"
#include "biglybtc/queue_observations.hpp"
#include "profile.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>
namespace biglybtc::engine {
enum class ManagedPhase { Checking, Queued, Downloading, Seeding, Paused, Stopping, Stopped, Failed, Allocating };
enum class VerifiedSeedingPhase { None, Checking, Released, Cancelled, Failed };
enum class ManagedRecheckPhase { Checking, Retiring, Finished, Cancelled, Failed };
struct ManagedRecheckProgress {
  ManagedRecheckPhase phase=ManagedRecheckPhase::Checking;
  // Actual readable bytes fed into the v1 hasher (virtual padding included),
  // never SDK progress scaling or missing zero-fill; total is logical extent.
  std::int64_t scannedBytes=0,totalBytes=0;
  int goodPieces=0,badPieces=0,missingPieces=0,unknownPieces=0;
  bool pieceMapSaved=false,saveUncertain=false;
};
struct ManagedDownloadStatus {
  core::TorrentHash hash;
  std::uint64_t admission = 0;
  ManagedPhase phase = ManagedPhase::Checking;
  std::int64_t completed = 0, bytes = 0, received = 0, sent = 0;
  int downloadRate = 0, uploadRate = 0, pieces = 0, totalPieces = 0;
  bool complete = false;
  bool retained = false;
  bool checked = false, selectedComplete = false;
  std::optional<storage::Error> failure{};
  std::optional<core::QueueReason> queueReason{};
  std::optional<std::int32_t> seedingRank{};
  bool firstPriority = false;
  std::uint32_t unavailableQueueInputs = 0;
  bool forceStart = false, manuallyPaused = false;
  // Last accepted checked original-file observation, never a persisted guess.
  std::optional<std::array<std::int32_t,4>> queueFilePriority{};
  // Optional exact last-accepted admission prefix; not a final/retired receipt.
  std::optional<std::int64_t> combinedReceived{}, combinedSent{};
  // Original saved payload prefix + actual accepted admission payload delta.
  // Missing means unavailable, never a persisted guess or retired receipt.
  std::optional<std::int64_t> cumulativeReceived{},cumulativeSent{};
  std::optional<core::QueueTargetStatistics> queueTargetStatistics{};
  // Current mapped-admission request latency; absent for stock mmap/lost facts.
  std::optional<std::int64_t> diskReadLatencyMillis{},diskWriteLatencyMillis{};
  std::optional<std::int32_t> staleRankOffset{};
  // Last committed original completion-group position, not LT queue index.
  std::optional<std::uint32_t> queuePosition{};
  std::shared_ptr<const storage::Error> queuePositionFailure{};
  VerifiedSeedingPhase verifiedSeeding=VerifiedSeedingPhase::None;
  bool verificationHeld=false;
  // Exact current admission/provider facts, unavailable without its retained
  // same-image observer. These are neither global nor retired final receipts.
  std::optional<core::QueueObservedFacts> queueSourceFacts{};
  std::optional<std::int32_t> queueIdleUploadSeconds{}; // Signed known-negative differs from unavailable.
  std::optional<std::int64_t> queueSmoothedPayloadUploadRate{};
  // Exact admission prefixes under the same accepted payload reset epoch.
  std::optional<std::int64_t> discardedBytes{}, hashFailedBytes{};
  core::TorrentAllocationMode allocationMode=core::TorrentAllocationMode::None;
  std::int64_t allocationBytes=0,allocationTotalBytes=0;
  bool allocationComplete=false;
  std::optional<ManagedRecheckProgress> recheck{};
};
enum class MagnetPhase { Resolving, Retiring, Imported, Failed, Cancelled };
struct PendingMagnetStatus {
  core::TorrentHash hash;
  std::uint64_t admission = 0;
  std::string name;
  MagnetPhase phase = MagnetPhase::Resolving;
  bool retained = false;
  std::optional<storage::Error> failure{};
  // Actual completed fresh import only, never a recovered already-present row.
  bool newlyImported=false;std::uint64_t importGeneration=0;
  std::optional<storage::Error> historyWarning{}; // Auxiliary: import still succeeded.

};
struct ManagedDownloadOptions {
  core::TorrentHash hash;
  unsigned short ownedLoopbackPeerPort = 0;
  bool forceStart = false;
  bool queued = false;
  // Queue-policy admission: verify under stop_when_ready, then retain pause.
  bool manuallyPaused = false;
  bool verifyExisting = false;
  // A caller-supplied real monotone deadline; absent uses five minutes.
  std::optional<std::chrono::steady_clock::time_point> verificationDeadline{};
  core::TorrentAllocationMode allocationMode=core::TorrentAllocationMode::None;
  bool mixedNetworks=false; // Explicit prepared Public+I2P choice; not a route grant.
};
} // namespace biglybtc::engine
