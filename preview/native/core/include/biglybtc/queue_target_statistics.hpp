#pragma once

#include "biglybtc/source_rate_average.hpp"
#include <cstdint>
#include <expected>
#include <optional>

namespace biglybtc::core {
enum class QueueTargetStatisticsError { Facts, Clock, Stale };
// The original Java constant's initializer is an INT expression before its
// widening to long (Constants.java:122), despite the ten-thousand-year comment.
inline constexpr std::int64_t SourceEtaInfinity = 1827387392;
// Caller supplies max(peer block remaining, disk remaining excluding DND).
// Completion needs an independently observed elapsed duration; absence is not0.
std::expected<std::int64_t,QueueTargetStatisticsError> sourceSmoothedEta(
    std::int64_t remainingBytes,std::int64_t smoothedPayloadBytesPerSecond,
    std::optional<std::int64_t> completedElapsedMillis = {}) noexcept;

struct QueueTargetSample {
  std::uint64_t admission=0,sequence=0;
  std::int64_t observedMillis=0;
  // Same accepted native payload status, not saved lifetime/global counters.
  // Native binding supplies LT selected remaining (wanted minus wanted_done),
  // not the original independent max(peer blocks, disk excluding DND) facts.
  std::optional<std::int64_t> payloadReceived{},remainingBytes{};
  std::optional<std::int64_t> completedElapsedMillis{};
  int smoothingSeconds=30;
  bool operator==(const QueueTargetSample&) const = default;
};
struct QueueTargetStatistics {
  std::uint64_t admission=0,sequence=0;
  std::int64_t observedMillis=0;
  std::optional<std::int64_t> smoothedPayloadReceiveBytesPerSecond{},etaSeconds{};
  SourceSmoothingPolicy smoothing{};
};
// One admission, worker-owned, passive. Caller clocks every actual accepted
// observation; no query, timer, UI, I/O or inferred data-arrival timestamps.
// Missing counters reset the baseline; first observation never charges old data.
class QueueTargetEta final {
public:
  std::expected<QueueTargetStatistics,QueueTargetStatisticsError> observe(const QueueTargetSample&) noexcept;
private:
  std::optional<QueueTargetSample> previous_;
  std::optional<SourceRateAverage> average_;
  SourceSmoothingPolicy policy_{};
  std::int64_t baseline_=0,firstMillis_=0,lastSmoothedReceived_=0;
  std::optional<std::int64_t> lastEtaMillis_;
  QueueTargetStatistics last_{};
};

// Source timing only: activity must already be authoritative. Missing disk/
// threshold facts do not create a zero offset or certify an inactive interval.
struct QueueStaleSeedInput {
  std::int64_t nowMillis=0;
  bool activityKnown=false,seeding=false,thresholdBranch=false;
  bool wasActive=false,active=false;
};
class QueueStaleSeedTiming final {
public:
  std::expected<std::optional<std::int32_t>,QueueTargetStatisticsError>
      observe(const QueueStaleSeedInput&) noexcept;
  // Original rank recalculation clears its offset when rank<=offset (non-test).
  void clearAppliedOffset() noexcept { offset_=0; }
  bool operator==(const QueueStaleSeedTiming&) const = default;
private:
  std::optional<std::int64_t> now_,staleSince_;
  std::int64_t lastRefresh_=0;
  std::int32_t offset_=0;
  bool uncertain_=false;
};
} // namespace biglybtc::core
