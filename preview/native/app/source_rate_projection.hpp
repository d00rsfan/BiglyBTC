#pragma once

#include "biglybtc/engine_statistics.hpp"
#include "biglybtc/source_rate_average.hpp"
#include "biglybtc/source_rate_values.hpp"

#include <cstdint>
#include <optional>

namespace biglybtc::core { class Document; }

namespace biglybtc::app {

// Read the already-loaded settings document, never IO. Original Long.intValue
// narrowing precedes sourceSmoothingPolicy's clamps. Absent/wrong type uses 30.
core::SourceSmoothingPolicy configuredSourceSmoothing(const core::Document* settings = nullptr) noexcept;

// Serialized worker-only value state, configured ONCE per actual
// engine owner. No reset, timer, callbacks, allocations, IO or Qt dependencies.
// The first actual sample is baseline-only. Later differences are attributed
// to their observed delivery milliseconds, not inferred byte-arrival times.
class SourceRateProjection final {
public:
  explicit SourceRateProjection(int configuredSeconds = 30) noexcept;
  core::SourceSmoothingPolicy policy() const noexcept { return policy_; }
  std::optional<core::SourceRateValues> observe(
      const std::optional<core::EngineStatistics>& sample,
      std::int64_t ownerElapsedNanoseconds, std::uint64_t sequence) noexcept;
private:
  const core::SourceSmoothingPolicy policy_;
  std::optional<core::SourceRateAverage> payloadReceived_, protocolReceived_;
  std::optional<core::SourceRateAverage> payloadSent_, protocolSent_;
  std::optional<core::SourceRateSmoothing> smoothing_;
  std::optional<core::EngineStatistics> previous_;
  core::EngineCounters baseline_{};
  std::optional<core::SourceRateValues> last_;
  std::int64_t ownerElapsed_ = 0;
  std::uint64_t sequence_ = 0;
  bool gap_ = false;
  bool failed_ = false;
};

} // namespace biglybtc::app
