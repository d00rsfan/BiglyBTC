#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace biglybtc::core {

// Explicit stepped-monotonic milliseconds and serialized calls only. This is
// arithmetic preparation, not a sampler or an engine/GUI integration.
enum class SourceRateError { Configuration, ClockRange, ClockRegression };

class SourceRateAverage final {
public:
  static constexpr std::size_t MaximumBuckets = 1802;
  // Whole-second refreshes cover the inspected production paths. Wider Java
  // factory inputs and cloneFrom's mutable sharing are deliberately excluded.
  static std::expected<SourceRateAverage, SourceRateError> create(
      int refreshMillis, int periodSeconds, std::int64_t nowMillis);
  std::expected<void, SourceRateError> add(std::int64_t value, std::int64_t nowMillis);
  std::expected<std::int64_t, SourceRateError> sum(std::int64_t nowMillis);
  std::expected<std::int64_t, SourceRateError> average(std::int64_t nowMillis);
  std::expected<std::int64_t, SourceRateError> averageForMillis(
      int averagePeriodMillis, std::int64_t nowMillis);
  std::expected<std::int64_t, SourceRateError> point(std::int64_t nowMillis);
  // An explicit clear starts a fresh empty clock domain and may move backwards.
  std::expected<void, SourceRateError> clear(std::int64_t nowMillis);
  int refreshMillis() const noexcept { return refresh_; }
  int periodSeconds() const noexcept { return period_; }
  std::size_t bucketCount() const noexcept { return buckets_; }
private:
  SourceRateAverage(int refresh, int period, std::int64_t now) noexcept;
  std::expected<void, SourceRateError> check(std::int64_t now) const noexcept;
  void update(std::int64_t factor) noexcept;
  std::int64_t sumSlots(int slots, std::int64_t factor) noexcept;
  int refresh_, period_;
  std::size_t buckets_;
  std::int64_t lastUpdate_, lastMillis_;
  std::array<std::int64_t, MaximumBuckets> values_{};
  bool initialized_ = false;
};

struct SourceSmoothingPolicy {
  int windowSeconds = 30;
  int intervalSeconds = 1;
  bool operator==(const SourceSmoothingPolicy&) const = default;
};
SourceSmoothingPolicy sourceSmoothingPolicy(int rawSeconds) noexcept;

struct SourceTrafficTotals {
  std::int64_t dataReceived = 0, protocolReceived = 0;
  std::int64_t dataSent = 0, protocolSent = 0;
};
struct SourceSmoothedRates {
  std::int64_t received = 0, sent = 0;
  bool operator==(const SourceSmoothedRates&) const = default;
};
class SourceRateSmoothing final {
public:
  static std::expected<SourceRateSmoothing, SourceRateError> create(
      int configuredSeconds, std::int64_t nowMillis);
  // Tests the old interval before applying a changed setting. Totals are
  // explicit source-domain inputs, never sums of native operation tails.
  std::expected<void, SourceRateError> tick(int tickCount, int configuredSeconds,
      const SourceTrafficTotals&, std::int64_t nowMillis);
  std::expected<SourceSmoothedRates, SourceRateError> rates(std::int64_t nowMillis);
  SourceSmoothingPolicy policy() const noexcept { return policy_; }
private:
  SourceRateSmoothing(SourceSmoothingPolicy, SourceRateAverage, SourceRateAverage,
      std::int64_t) noexcept;
  SourceSmoothingPolicy policy_;
  SourceRateAverage receive_, send_;
  std::int64_t lastReceived_ = 0, lastSent_ = 0, lastMillis_;
};

// Java's specified wrapping/narrowing, not C++ signed overflow or saturation.
std::int32_t narrowSourceRate(std::int64_t) noexcept;
std::int32_t sourceCombinedIntRate(std::int64_t payload, std::int64_t protocol) noexcept;
std::int64_t sourceActivityCombinedRate(std::int64_t payload, std::int32_t protocol) noexcept;

} // namespace biglybtc::core
