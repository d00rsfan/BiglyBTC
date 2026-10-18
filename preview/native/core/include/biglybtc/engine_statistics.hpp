#pragma once

#include <cstdint>
#include <expected>
#include <optional>

namespace biglybtc::core {

// Primitive snapshots of ONE owned libtorrent session. Combined counters include
// payload plus peer protocol, not IP/tracker/DHT totals. Never saved lifetime or
// all-application counters; the caller creates a fresh accumulator per owner.
struct EngineCounters {
  std::int64_t received = 0;
  std::int64_t sent = 0;
  std::int64_t payloadReceived = 0;
  std::int64_t payloadSent = 0;
  bool operator==(const EngineCounters&) const = default;
};
struct EngineRates {
  std::int64_t received = 0;
  std::int64_t sent = 0;
  std::int64_t payloadReceived = 0;
  std::int64_t payloadSent = 0;
  std::int64_t protocolReceived = 0;
  std::int64_t protocolSent = 0;
  bool operator==(const EngineRates&) const = default;
};
struct EngineStatistics {
  EngineCounters counters{};
  // Since the FIRST accepted observation, not engine creation or application
  // uptime. The caller supplies nonnegative steady-clock observation timestamps.
  std::int64_t elapsedNanoseconds = 0;
  std::optional<EngineRates> rates{};
  bool operator==(const EngineStatistics&) const = default;
};
enum class EngineStatisticsError { InvalidCounters, InvalidTime, CounterRegression };

class EngineStatisticsAccumulator final {
public:
  // Raw counters must be nonnegative and combined>=payload; all four raw and
  // both derived protocol counters must never decrease. Timestamps are >=0 and
  // strictly increase after the first sample. Rejections leave state unchanged;
  // fixed priority is InvalidCounters, InvalidTime, then CounterRegression.
  // First observation has elapsed0/no rates. Subsequent rates are exact floors
  // of delta*1e9/dt. If any one rate exceeds int64, accept totals/time but omit
  // the whole rate group. No wrapping, saturation, allocation, IO, clock read or
  // dependency on Qt/libtorrent; observed primitives are owned independently.
  std::expected<EngineStatistics, EngineStatisticsError> observe(
      EngineCounters counters, std::int64_t monotonicNanoseconds) noexcept;
private:
  struct Observation {
    EngineCounters counters;
    std::int64_t time;
    std::int64_t firstTime;
  };
  std::optional<Observation> previous_{};
};

// Checks primitive structure, not provenance or a previous sample. Nonnegative
// rate components must sum to combined with independently floored difference
// 0..1 in each direction; rates at elapsed0 are invalid. No overflowing sum.
bool validEngineStatistics(const EngineStatistics&) noexcept;

} // namespace biglybtc::core
