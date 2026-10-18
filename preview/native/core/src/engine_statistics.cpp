#include "biglybtc/engine_statistics.hpp"

#include <array>
#include <limits>

namespace biglybtc::core {
namespace {
using I = std::int64_t;
using U = std::uint64_t;
constexpr U Scale = 1000000000;
constexpr U Maximum = static_cast<U>(std::numeric_limits<I>::max());

bool validCounters(const EngineCounters& value) noexcept {
  return value.received >= 0 && value.sent >= 0 && value.payloadReceived >= 0 && value.payloadSent >= 0
      && value.received >= value.payloadReceived && value.sent >= value.payloadSent;
}

// Exact floor(remainder*1e9/divisor) without a wide multiplication. Remainders
// stay below divisor<=INT64_MAX, so each unsigned double/add is <=2*INT64_MAX-2.
// The quotient prefix remains below1e9. No signed overflow or compiler extension.
U fractionalRate(U remainder, U divisor) noexcept {
  U quotient = 0;
  U rest = 0;
  for (unsigned index = 30; index > 0; --index) {
    const unsigned bit = index - 1;
    quotient *= 2;
    rest *= 2;
    if (rest >= divisor) { rest -= divisor; ++quotient; }
    if ((Scale >> bit) & U{1}) {
      rest += remainder;
      if (rest >= divisor) { rest -= divisor; ++quotient; }
    }
  }
  return quotient;
}
std::optional<I> rate(I delta, I elapsed) noexcept {
  const auto value = static_cast<U>(delta);
  const auto divisor = static_cast<U>(elapsed);
  const U whole = value / divisor;
  if (whole > Maximum / Scale) { return std::nullopt; }
  const U base = whole * Scale;
  const U fraction = fractionalRate(value % divisor, divisor);
  if (fraction > Maximum - base) { return std::nullopt; }
  return static_cast<I>(base + fraction);
}
bool validRateDirection(I combined, I payload, I protocol) noexcept {
  if (combined < 0 || payload < 0 || protocol < 0 || payload > combined) { return false; }
  const auto rest = combined - payload;
  return protocol <= rest && rest - protocol <= 1;
}
} // namespace

std::expected<EngineStatistics, EngineStatisticsError> EngineStatisticsAccumulator::observe(
    EngineCounters counters, I monotonicNanoseconds) noexcept {
  if (!validCounters(counters)) { return std::unexpected(EngineStatisticsError::InvalidCounters); }
  if (monotonicNanoseconds < 0 || (previous_ && monotonicNanoseconds <= previous_->time)) {
    return std::unexpected(EngineStatisticsError::InvalidTime);
  }
  if (!previous_) {
    previous_ = Observation{counters, monotonicNanoseconds, monotonicNanoseconds};
    return EngineStatistics{counters, 0, std::nullopt};
  }
  const auto& old = previous_->counters;
  const I protocolReceived = counters.received - counters.payloadReceived;
  const I protocolSent = counters.sent - counters.payloadSent;
  const I oldProtocolReceived = old.received - old.payloadReceived;
  const I oldProtocolSent = old.sent - old.payloadSent;
  if (counters.received < old.received || counters.sent < old.sent
      || counters.payloadReceived < old.payloadReceived || counters.payloadSent < old.payloadSent
      || protocolReceived < oldProtocolReceived || protocolSent < oldProtocolSent) {
    return std::unexpected(EngineStatisticsError::CounterRegression);
  }
  EngineStatistics result{counters, monotonicNanoseconds - previous_->firstTime, std::nullopt};
  const auto interval = monotonicNanoseconds - previous_->time;
  const std::array<I, 6> deltas{counters.received - old.received, counters.sent - old.sent,
      counters.payloadReceived - old.payloadReceived, counters.payloadSent - old.payloadSent,
      protocolReceived - oldProtocolReceived, protocolSent - oldProtocolSent};
  std::array<I, 6> rates{};
  bool fits = true;
  for (std::size_t i = 0; i < deltas.size(); ++i) {
    const auto value = rate(deltas[i], interval);
    if (!value) { fits = false; break; }
    rates[i] = *value;
  }
  if (fits) { result.rates = EngineRates{rates[0], rates[1], rates[2], rates[3], rates[4], rates[5]}; }
  previous_ = Observation{counters, monotonicNanoseconds, previous_->firstTime};
  return result;
}

bool validEngineStatistics(const EngineStatistics& value) noexcept {
  if (!validCounters(value.counters) || value.elapsedNanoseconds < 0) { return false; }
  if (!value.rates) { return true; }
  const auto& rates = *value.rates;
  return value.elapsedNanoseconds > 0
      && validRateDirection(rates.received, rates.payloadReceived, rates.protocolReceived)
      && validRateDirection(rates.sent, rates.payloadSent, rates.protocolSent);
}

} // namespace biglybtc::core
