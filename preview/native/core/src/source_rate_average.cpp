#include "biglybtc/source_rate_average.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace biglybtc::core {
namespace {
std::int64_t addWrapped(std::int64_t a, std::int64_t b) noexcept {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a) + std::bit_cast<std::uint64_t>(b));
}
std::int64_t subtractWrapped(std::int64_t a, std::int64_t b) noexcept {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a) - std::bit_cast<std::uint64_t>(b));
}
}
SourceRateAverage::SourceRateAverage(int refresh, int period, std::int64_t now) noexcept
    : refresh_(refresh), period_(period),
      buckets_(static_cast<std::size_t>(period * 1000 / refresh + 2)),
      lastUpdate_(now / refresh), lastMillis_(now) {}
std::expected<SourceRateAverage, SourceRateError> SourceRateAverage::create(
    int refresh, int period, std::int64_t now) {
  if (refresh < 1000 || refresh > 20000 || refresh % 1000 != 0
      || period < 1 || period > 1800 || period * 1000 < refresh) {
    return std::unexpected(SourceRateError::Configuration);
  }
  if (now < 0) { return std::unexpected(SourceRateError::ClockRange); }
  return SourceRateAverage(refresh, period, now);
}
std::expected<void, SourceRateError> SourceRateAverage::check(std::int64_t now) const noexcept {
  if (now < 0) { return std::unexpected(SourceRateError::ClockRange); }
  if (now < lastMillis_) { return std::unexpected(SourceRateError::ClockRegression); }
  return {};
}
void SourceRateAverage::update(std::int64_t factor) noexcept {
  const auto size = static_cast<std::int64_t>(buckets_);
  // factor <= INT64_MAX/1000; +/-1803 cannot overflow on admitted clocks.
  if (lastUpdate_ < factor - size) { lastUpdate_ = factor - size - 1; }
  if (initialized_) {
    for (auto i = lastUpdate_ + 1; i <= factor; ++i) {
      values_[static_cast<std::size_t>(i % size)] = 0;
    }
    values_[static_cast<std::size_t>((factor + 1) % size)] = 0;
  }
  lastUpdate_ = factor;
}
std::expected<void, SourceRateError> SourceRateAverage::add(std::int64_t value, std::int64_t now) {
  if (auto valid = check(now); !valid) { return valid; }
  lastMillis_ = now;
  if (!initialized_ && value != 0) { initialized_ = true; }
  if (initialized_) {
    const auto factor = now / refresh_; update(factor);
    auto& slot = values_[static_cast<std::size_t>(factor % static_cast<std::int64_t>(buckets_))];
    slot = addWrapped(slot, value);
  }
  return {};
}
std::int64_t SourceRateAverage::sumSlots(int slots, std::int64_t factor) noexcept {
  update(factor);
  std::int64_t result = 0;
  if (initialized_) {
    const auto end = factor + static_cast<std::int64_t>(buckets_);
    for (auto i = end - slots; i < end; ++i) {
      result = addWrapped(result, values_[static_cast<std::size_t>(i % static_cast<std::int64_t>(buckets_))]);
    }
  }
  return result;
}
std::expected<std::int64_t, SourceRateError> SourceRateAverage::sum(std::int64_t now) {
  if (auto valid = check(now); !valid) { return std::unexpected(valid.error()); }
  lastMillis_ = now;
  if (!initialized_) { return 0; }
  return sumSlots(static_cast<int>(buckets_ - 2), now / refresh_);
}
std::expected<std::int64_t, SourceRateError> SourceRateAverage::average(std::int64_t now) {
  auto result = sum(now);
  if (!result) { return std::unexpected(result.error()); }
  return *result / period_; // Positive denominator; Java/C++ both truncate toward0.
}
std::expected<std::int64_t, SourceRateError> SourceRateAverage::point(std::int64_t now) {
  if (auto valid = check(now); !valid) { return std::unexpected(valid.error()); }
  const auto factor = now / refresh_;
  // Original Java indexes -1 after an allocated zero-time point query. Report
  // unknown explicitly; this rejected input receives no equality credit.
  if (initialized_ && factor == 0) { return std::unexpected(SourceRateError::ClockRange); }
  lastMillis_ = now; update(factor);
  return initialized_ ? values_[static_cast<std::size_t>((factor - 1) % static_cast<std::int64_t>(buckets_))] : 0;
}
std::expected<std::int64_t, SourceRateError> SourceRateAverage::averageForMillis(int requested, std::int64_t now) {
  int slots = requested <= 0 ? static_cast<int>(buckets_ - 2) : requested / refresh_;
  slots = std::clamp(slots, 1, static_cast<int>(buckets_ - 2));
  if (slots == 1) { return point(now); }
  if (auto valid = check(now); !valid) { return std::unexpected(valid.error()); }
  lastMillis_ = now;
  // Whole-second refresh and bounded period imply denominator>=1 and product
  // <=3,240,000. Java's exotic subsecond divide-by0 configurations are excluded.
  const auto denominator = period_ * slots / static_cast<int>(buckets_ - 2);
  return sumSlots(slots, now / refresh_) / denominator;
}
std::expected<void, SourceRateError> SourceRateAverage::clear(std::int64_t now) {
  if (now < 0) { return std::unexpected(SourceRateError::ClockRange); }
  values_.fill(0); initialized_ = false; lastUpdate_ = now / refresh_; lastMillis_ = now;
  return {};
}
SourceSmoothingPolicy sourceSmoothingPolicy(int raw) noexcept {
  const auto window = std::clamp(raw, 10, 1800);
  return {window, std::clamp(window / 60, 1, 20)};
}
SourceRateSmoothing::SourceRateSmoothing(SourceSmoothingPolicy policy, SourceRateAverage receive,
    SourceRateAverage send, std::int64_t now) noexcept
    : policy_(policy), receive_(std::move(receive)), send_(std::move(send)), lastMillis_(now) {}
std::expected<SourceRateSmoothing, SourceRateError> SourceRateSmoothing::create(int raw, std::int64_t now) {
  const auto policy = sourceSmoothingPolicy(raw);
  auto receive = SourceRateAverage::create(policy.intervalSeconds * 1000, policy.windowSeconds, now);
  if (!receive) { return std::unexpected(receive.error()); }
  auto send = SourceRateAverage::create(policy.intervalSeconds * 1000, policy.windowSeconds, now);
  if (!send) { return std::unexpected(send.error()); }
  return SourceRateSmoothing(policy, std::move(*receive), std::move(*send), now);
}
std::expected<void, SourceRateError> SourceRateSmoothing::tick(int tickCount, int raw,
    const SourceTrafficTotals& totals, std::int64_t now) {
  if (now < 0) { return std::unexpected(SourceRateError::ClockRange); }
  if (now < lastMillis_) { return std::unexpected(SourceRateError::ClockRegression); }
  if (tickCount % policy_.intervalSeconds != 0) { lastMillis_ = now; return {}; }
  const auto desired = sourceSmoothingPolicy(raw);
  if (desired.windowSeconds != policy_.windowSeconds) {
    auto receive = SourceRateAverage::create(desired.intervalSeconds * 1000, desired.windowSeconds, now);
    auto send = SourceRateAverage::create(desired.intervalSeconds * 1000, desired.windowSeconds, now);
    if (!receive || !send) { return std::unexpected(SourceRateError::ClockRange); }
    policy_ = desired; receive_ = std::move(*receive); send_ = std::move(*send);
  }
  const auto received = addWrapped(totals.dataReceived, totals.protocolReceived);
  const auto sent = addWrapped(totals.dataSent, totals.protocolSent);
  // Prechecked monotonic time and validated private configuration make both
  // adds nonfailing; all byte arithmetic intentionally wraps as Java long.
  const auto rx = receive_.add(subtractWrapped(received, lastReceived_), now);
  const auto tx = send_.add(subtractWrapped(sent, lastSent_), now);
  if (!rx) { return rx; }
  if (!tx) { return tx; }
  lastReceived_ = received; lastSent_ = sent; lastMillis_ = now;
  return {};
}
std::expected<SourceSmoothedRates, SourceRateError> SourceRateSmoothing::rates(std::int64_t now) {
  if (now < 0) { return std::unexpected(SourceRateError::ClockRange); }
  if (now < lastMillis_) { return std::unexpected(SourceRateError::ClockRegression); }
  auto receive = receive_.average(now), send = send_.average(now);
  if (!receive) { return std::unexpected(receive.error()); }
  if (!send) { return std::unexpected(send.error()); }
  lastMillis_ = now;
  return SourceSmoothedRates{*receive, *send};
}
std::int32_t narrowSourceRate(std::int64_t value) noexcept {
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(value)));
}
std::int32_t sourceCombinedIntRate(std::int64_t payload, std::int64_t protocol) noexcept {
  return narrowSourceRate(addWrapped(payload, protocol));
}
std::int64_t sourceActivityCombinedRate(std::int64_t payload, std::int32_t protocol) noexcept {
  return addWrapped(payload, static_cast<std::int64_t>(protocol));
}
} // namespace biglybtc::core
