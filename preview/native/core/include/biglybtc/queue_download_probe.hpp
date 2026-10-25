#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace biglybtc::core {
// Older native providers expose exact payload only. Qualified optional traffic
// providers also expose protocol; original/combined/payload scopes stay explicit.
// NativeCombined has independently qualified LT payload+protocol provenance;
// it does not claim byte-for-byte Java wire accounting or any IP/DHT totals.
enum class QueueProbeScope { OriginalCombined, NativePayload, NativeCombined };
enum class QueueProbeState { Other, Queued, Downloading };
struct QueueProbeConfig {
  bool enabled = false, testActive = false;
  std::int32_t testSeconds = 120, retestMinutes = 30;
};
struct QueueProbeInput {
  std::uint64_t token = 0;
  std::uint32_t position = 1;
  QueueProbeState state = QueueProbeState::Other;
  QueueProbeScope scope = QueueProbeScope::NativePayload;
  // Both primitives describe the SAME already accepted owner status snapshot.
  std::optional<std::int64_t> receivedBytes, observedMillis;
  // Only an actual original-compatible smoothed-ETA provider may supply this.
  std::optional<std::int64_t> smoothedEtaSeconds;
};
struct QueueProbeContext {
  std::int64_t nowMillis = 0, ownerStartedMillis = 0;
  std::int64_t downloadLimitKiB = 0;
  // Original 1.5s moving-immediate source average, already converted to KiB/s.
  // An instantaneous payload rate is not a replacement for this observation.
  std::optional<std::int64_t> averagedDownloadKiB;
};
struct QueueProbeObservation {
  std::uint64_t token = 0;
  bool active = false;
  QueueProbeScope scope = QueueProbeScope::NativePayload;
  std::optional<std::int32_t> bytesPerSecond;
  std::optional<std::int64_t> etaSeconds;
};
struct QueueProbePlan {
  std::vector<QueueProbeObservation> observations; // Same order as inputs.
  bool unavailable = false;
};
enum class QueueProbeError { Configuration, Facts, Clock, Limit, Memory };
// Serialized worker ownership; no clock, timer, callbacks, I/O or engine API.
// Failed validation/allocation leaves all prior state unchanged.
class QueueDownloadProbes final {
public:
  void cancel() noexcept { active_.reset();saturatedSince_.reset(); }
  std::expected<QueueProbePlan, QueueProbeError> observe(const QueueProbeConfig&,
      std::span<const QueueProbeInput>, const QueueProbeContext&);
private:
  struct Entry {
    QueueProbeObservation result;
    std::optional<std::int64_t> startedMillis, baselineBytes, baselineMillis;
    std::optional<std::int64_t> lastBytes, lastObservedMillis;
  };
  std::vector<Entry> entries_; // Sorted by immutable admission token.
  std::optional<std::int64_t> lastMillis_, ownerStartedMillis_, saturatedSince_;
  std::optional<std::uint64_t> active_;
};
} // namespace biglybtc::core
