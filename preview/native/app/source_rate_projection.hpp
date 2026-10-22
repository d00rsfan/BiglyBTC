#pragma once

#include "biglybtc/engine_statistics.hpp"
#include "biglybtc/source_rate_average.hpp"
#include "biglybtc/source_rate_values.hpp"

#include <cstdint>
#include <optional>

namespace biglybtc::app {

// Serialized worker-only value state, default-constructed ONCE per actual
// engine owner. No reset, timer, callbacks, allocations, IO or Qt dependencies.
// The first actual sample is baseline-only. Later differences are attributed
// to their observed delivery milliseconds, not inferred byte-arrival times.
class SourceRateProjection final {
public:
  std::optional<core::SourceRateValues> observe(
      const std::optional<core::EngineStatistics>& sample,
      std::int64_t ownerElapsedNanoseconds, std::uint64_t sequence) noexcept;
private:
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
