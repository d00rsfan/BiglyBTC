#include "biglybtc/source_rate_average.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace biglybtc::core;
std::size_t assertions = 0;
void require(bool value, std::string_view context) {
  ++assertions;
  if (!value) { throw std::runtime_error(std::string(context)); }
}
std::int64_t wrappedAdd(std::int64_t a, std::int64_t b) {
  return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
SourceRateAverage ordinary(std::int64_t now) {
  auto result = SourceRateAverage::create(1000, 10, now);
  require(result.has_value(), "admitted source global Average construction");
  return std::move(*result);
}
SourceRateSmoothing smooth(int configured, std::int64_t now) {
  auto result = SourceRateSmoothing::create(configured, now);
  require(result.has_value(), "admitted source smoothing construction");
  return std::move(*result);
}
std::int64_t get(SourceRateAverage& average, std::int64_t now, std::string_view context) {
  auto result = average.average(now); require(result.has_value(), context); return *result;
}
struct GlobalObservation {
  SourceSmoothingPolicy policy;
  std::array<std::int64_t, 4> totals, rates, noLan;
  std::array<std::int64_t, 2> combined, activity, smoothed;
  bool operator==(const GlobalObservation&) const = default;
};
// Thin input/observation fixture only. Eight production Average instances and
// production SourceRateSmoothing perform all averaging. Cumulative event totals
// wrap like Java; no average/smoothing expected arithmetic is implemented here.
class GlobalFixture {
public:
  GlobalFixture(int config, std::int64_t now)
      : configured(config), smoothing(smooth(config, now)),
        rates_{ordinary(now), ordinary(now), ordinary(now), ordinary(now)},
        noLan_{ordinary(now), ordinary(now), ordinary(now), ordinary(now)} {}
  int configured;
  SourceTrafficTotals totals;
  SourceRateSmoothing smoothing;
  void receive(std::int64_t payload, std::int64_t protocol, bool lan,
      std::int64_t now, std::string_view context) {
    totals.dataReceived = wrappedAdd(totals.dataReceived, payload);
    totals.protocolReceived = wrappedAdd(totals.protocolReceived, protocol);
    record(0, payload, protocol, lan, now, context);
  }
  void send(std::int64_t payload, std::int64_t protocol, bool lan,
      std::int64_t now, std::string_view context) {
    totals.dataSent = wrappedAdd(totals.dataSent, payload);
    totals.protocolSent = wrappedAdd(totals.protocolSent, protocol);
    record(2, payload, protocol, lan, now, context);
  }
  GlobalObservation read(std::int64_t now, std::string_view context) {
    std::array<std::int64_t, 4> raw{}, noLan{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
      raw[i] = get(rates_[i], now, context); noLan[i] = get(noLan_[i], now, context);
    }
    auto smoothed = smoothing.rates(now); require(smoothed.has_value(), context);
    // Java's global config getters change immediately; the smoothing instance
    // can still retain its old interval until an eligible tick. Do not conflate.
    return {sourceSmoothingPolicy(configured),
        {totals.dataReceived, totals.protocolReceived, totals.dataSent, totals.protocolSent},
        {raw[0], narrowSourceRate(raw[1]), raw[2], narrowSourceRate(raw[3])},
        {noLan[0], narrowSourceRate(noLan[1]), noLan[2], narrowSourceRate(noLan[3])},
        {sourceCombinedIntRate(raw[0], raw[1]), sourceCombinedIntRate(raw[2], raw[3])},
        {sourceActivityCombinedRate(raw[0], narrowSourceRate(raw[1])), sourceActivityCombinedRate(raw[2], narrowSourceRate(raw[3]))},
        {smoothed->received, smoothed->sent}};
  }
private:
  std::array<SourceRateAverage, 4> rates_, noLan_;
  void record(std::size_t index, std::int64_t payload, std::int64_t protocol,
      bool lan, std::int64_t now, std::string_view context) {
    require(rates_[index].add(payload, now).has_value(), context);
    require(rates_[index + 1].add(protocol, now).has_value(), context);
    if (!lan) {
      require(noLan_[index].add(payload, now).has_value(), context);
      require(noLan_[index + 1].add(protocol, now).has_value(), context);
    }
  }
};
#include "fixtures/source-rate-java/replay.inc"
} // namespace
int main() {
  try {
    originalJavaRateCases();
    std::cout << "PASS source-rate-java: average_cases=13 global_cases=9 scalar_comparisons=316 checks="
        << assertions << " whole_manager_runtime_credit=0\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL source-rate-java: " << error.what() << '\n'; return 1;
  }
}
