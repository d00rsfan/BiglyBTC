#pragma once

#include "source_rate_average.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace biglybtc::core {

// Scope is supplied by the actual owner; payload is never a combined fallback.
enum class QueueRateScope { OriginalCombined, OwnerCombined, OriginalPayload, TargetPayload };
struct QueueRateStamp {
  std::uint64_t identity = 0, sequence = 0; // Owner or exact target admission.
  std::int64_t monotonicMillis = 0;
  bool operator==(const QueueRateStamp&) const = default;
};
struct QueueRateSample {
  QueueRateStamp stamp;
  QueueRateScope scope = QueueRateScope::OwnerCombined;
  std::optional<std::int64_t> receivedBytes; // Cumulative within this identity.
};

// One genuine update at most per nominal source1500ms cycle; no catch-up ticks.
// Original MovingImmediateAverage has ten populated-as-observed double slots.
class QueueGlobalReceiveAverage final {
public:
  static constexpr std::int64_t IntervalMillis = 1500, MaximumGapMillis = 3000;
  std::optional<std::int32_t> observe(const QueueRateSample&) noexcept;
  std::size_t sampleCount() const noexcept;
private:
  std::optional<QueueRateSample> previous_;
  std::int64_t tickMillis_ = 0, tickReceived_ = 0;
  std::array<double, 10> values_{};
  double total_ = 0;
  std::uint32_t position_ = 0;
  std::optional<std::int32_t> last_;
};

// PEPeerManagerStatsImpl payload smoothing; same existing source Average ring.
// Caller supplies an actual source tick identity, not fabricated missing ticks.
class QueuePayloadRateSmoothing final {
public:
  std::optional<std::int64_t> observe(const QueueRateSample&,
      std::int32_t sourceTickCount, int configuredSeconds = 30) noexcept;
  SourceSmoothingPolicy policy() const noexcept { return policy_; }
private:
  std::optional<QueueRateSample> previous_;
  std::optional<SourceRateAverage> average_;
  SourceSmoothingPolicy policy_{};
  std::int64_t usedReceived_ = 0;
  std::int32_t lastTick_ = 0;
};

struct QueueEtaSample {
  QueueRateStamp stamp;
  std::int64_t wallMillis = 0;
  std::optional<std::int64_t> peerRemaining, diskRemainingExcludingDnd;
  std::optional<std::int64_t> smoothedPayloadBytesPerSecond;
  std::optional<std::int64_t> startedMonotonicMillis, finishedMonotonicMillis;
};
class QueueSmoothedEta final {
public:
  // The Java int expression10000*365*24*3600 wraps BEFORE widening to long.
  static constexpr std::int64_t InfiniteSeconds = 1827387392;
  std::optional<std::int64_t> observe(const QueueEtaSample&) noexcept;
private:
  std::optional<QueueRateStamp> previous_;
  std::optional<std::int64_t> last_, calculatedWall_;
  std::int64_t wall_ = 0;
};

} // namespace biglybtc::core
