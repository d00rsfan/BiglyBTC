#include "biglybtc/engine_statistics.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Generated primitives and an independent decimal long-division oracle. No
// libtorrent, Qt, IO, Java execution or global/application/lifetime parity.
namespace {
using namespace biglybtc::core;
using I = std::int64_t;
using U = std::uint64_t;
constexpr I Max = std::numeric_limits<I>::max();
constexpr I Second = 1000000000;
std::size_t assertions = 0;
std::size_t cases = 0;
void check(bool condition, const char* message) {
  ++assertions;
  if (!condition) { throw std::runtime_error(message); }
}
template<typename Function> void run(std::string_view name, Function&& function) {
  try { function(); ++cases; }
  catch (const std::exception& failure) { throw std::runtime_error(std::string(name) + ": " + failure.what()); }
}
EngineStatistics observed(EngineStatisticsAccumulator& accumulator, EngineCounters counters, I time) {
  const auto result = accumulator.observe(counters, time);
  check(result.has_value(), "Expected admitted observation");
  check(result->counters == counters && validEngineStatistics(*result), "Accepted result must retain counters and validate");
  return *result;
}
void rejected(EngineStatisticsAccumulator& accumulator, EngineCounters counters, I time, EngineStatisticsError error) {
  const auto result = accumulator.observe(counters, time);
  check(!result && result.error() == error, "Rejection category mismatch");
}
std::string normalized(std::string value) {
  const auto begin = value.find_first_not_of('0');
  return begin == std::string::npos ? "0" : value.substr(begin);
}
bool decimalLess(const std::string& left, const std::string& right) {
  return left.size() != right.size() ? left.size() < right.size() : left < right;
}
std::string decimalSubtract(std::string left, const std::string& right) {
  int borrow = 0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    const auto index = left.size() - i - 1;
    int digit = left[index] - '0' - borrow;
    if (i < right.size()) { digit -= right[right.size() - i - 1] - '0'; }
    borrow = digit < 0 ? 1 : 0;
    if (borrow) { digit += 10; }
    left[index] = static_cast<char>('0' + digit);
  }
  if (borrow) { throw std::runtime_error("Invalid independent decimal subtraction"); }
  return normalized(std::move(left));
}
// Decimal product is formed by appending nine zeroes; long division uses string
// comparison/subtraction, never the production binary quotient/remainder path.
std::optional<I> referenceRate(I delta, I time) {
  std::string numerator = std::to_string(delta) + "000000000";
  const std::string divisor = std::to_string(time);
  std::string remainder = "0", quotient;
  for (const auto digit : numerator) {
    remainder = normalized(remainder + digit);
    int next = 0;
    while (!decimalLess(remainder, divisor)) {
      remainder = decimalSubtract(std::move(remainder), divisor);
      if (++next > 9) { throw std::runtime_error("Invalid independent decimal division digit"); }
    }
    quotient.push_back(static_cast<char>('0' + next));
  }
  quotient = normalized(std::move(quotient));
  if (decimalLess(std::to_string(Max), quotient)) { return std::nullopt; }
  I result = 0;
  for (const auto digit : quotient) { result = result * 10 + (digit - '0'); }
  return result;
}
void referenceInterval(EngineCounters delta, I time, const EngineStatistics& actual) {
  const std::array<I, 6> values{delta.received, delta.sent, delta.payloadReceived, delta.payloadSent,
      delta.received - delta.payloadReceived, delta.sent - delta.payloadSent};
  std::array<I, 6> expected{};
  bool fits = true;
  for (std::size_t i = 0; i < values.size(); ++i) {
    const auto rate = referenceRate(values[i], time);
    if (!rate) { fits = false; }
    else { expected[i] = *rate; }
  }
  check(actual.rates.has_value() == fits, "Independent rational availability mismatch");
  if (fits) {
    check(*actual.rates == EngineRates{expected[0], expected[1], expected[2], expected[3], expected[4], expected[5]},
        "Exact six floor rates differ from independent decimal rational oracle");
  }
}
} // namespace

static_assert(noexcept(std::declval<EngineStatisticsAccumulator&>().observe({}, 0)));
static_assert(noexcept(validEngineStatistics({})));

int main() {
  try {
    run("first-observation-has-owned-raw-totals-but-no-invented-rate", [] {
      EngineStatisticsAccumulator accumulator;
      const auto value = observed(accumulator, {100, 70, 80, 50}, 123456);
      check(value.elapsedNanoseconds == 0 && !value.rates, "First sample is not a zero rate or engine/application uptime");
    });
    run("zero-first-timestamp-and-zero-counters-are-real-facts", [] {
      EngineStatisticsAccumulator accumulator;
      const auto first = observed(accumulator, {}, 0);
      check(first.elapsedNanoseconds == 0 && !first.rates, "Known zero counters do not manufacture first rate");
      const auto second = observed(accumulator, {}, 7);
      check(second.elapsedNanoseconds == 7 && second.rates == EngineRates{}, "Unchanged counters over real interval have known zero rates");
    });
    run("one-second-six-rate-mapping", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {5, 7, 3, 4}, 10);
      const auto value = observed(accumulator, {105, 207, 83, 154}, Second + 10);
      check(value.elapsedNanoseconds == Second && value.rates == EngineRates{100, 200, 80, 150, 20, 50}, "Counter/direction/protocol mapping");
    });
    run("rates-use-previous-interval-not-total-elapsed-or-raw-counters", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {100, 200, 90, 100}, 100);
      (void)observed(accumulator, {101, 202, 91, 101}, Second + 100);
      const auto value = observed(accumulator, {103, 208, 92, 103}, 2 * Second + 100);
      check(value.elapsedNanoseconds == 2 * Second && value.rates == EngineRates{2, 6, 1, 2, 1, 4}, "Previous interval and first elapsed are separate");
    });
    run("fractional-nanosecond-rate-floors-and-one-byte-rounding-gap", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {}, 0);
      const auto value = observed(accumulator, {3, 2, 1, 1}, 3);
      check(value.rates == EngineRates{1000000000, 666666666, 333333333, 333333333, 666666666, 333333333},
          "Independent floors permit total minus components of exactly one");
      referenceInterval({3, 2, 1, 1}, 3, value);
    });
    run("large-product-that-fits-after-division-remains-exact", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {}, 0);
      const auto value = observed(accumulator, {Max, Max, Max - 1, 1}, Second);
      check(value.rates == EngineRates{Max, Max, Max - 1, 1, 1, Max - 1}, "Do not reject intermediate delta*1e9 overflow when result fits");
    });
    run("maximum-time-and-counters-have-safe-fractional-division", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {}, 0);
      const auto value = observed(accumulator, {Max, Max - 1, 1, Max - 2}, Max);
      check(value.elapsedNanoseconds == Max, "Maximum elapsed retained");
      referenceInterval({Max, Max - 1, 1, Max - 2}, Max, value);
    });
    run("one-overflow-omits-whole-rate-group-and-still-advances-state", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {}, 0);
      const EngineCounters counters{10000000000LL, 0, 10000000000LL, 0};
      const auto overflow = observed(accumulator, counters, 1);
      check(overflow.elapsedNanoseconds == 1 && !overflow.rates, "Unrepresentable rate is unknown, never wrapped or saturated");
      const auto next = observed(accumulator, counters, Second + 1);
      check(next.elapsedNanoseconds == Second + 1 && next.rates == EngineRates{}, "Accepted overflow sample is the next interval baseline");
    });
    run("rate-fit-boundary-before-and-after-int64", [] {
      for (const auto delta : {I{9223372036LL}, I{9223372037LL}, Max}) {
        EngineStatisticsAccumulator accumulator;
        (void)observed(accumulator, {}, 0);
        const auto value = observed(accumulator, {delta, 0, 0, 0}, 1);
        referenceInterval({delta, 0, 0, 0}, 1, value);
        check(value.rates.has_value() == (delta == 9223372036LL), "Exact rate int64 admission boundary");
      }
    });
    run("every-negative-raw-field-is-invalid-without-mutation", [] {
      const std::array<EngineCounters, 4> bad{{{-1,0,0,0}, {0,-1,0,0}, {0,0,-1,0}, {0,0,0,-1}}};
      for (const auto counters : bad) {
        EngineStatisticsAccumulator accumulator;
        rejected(accumulator, counters, 50, EngineStatisticsError::InvalidCounters);
        const auto value = observed(accumulator, {2,2,1,1}, 0);
        check(!value.rates && value.elapsedNanoseconds == 0, "Rejected first raw fields did not initialize time/state");
      }
    });
    run("total-smaller-than-payload-invalid-in-either-direction", [] {
      EngineStatisticsAccumulator accumulator;
      rejected(accumulator, {2,4,3,1}, 0, EngineStatisticsError::InvalidCounters);
      rejected(accumulator, {4,2,1,3}, 0, EngineStatisticsError::InvalidCounters);
      const auto value = observed(accumulator, {4,4,4,4}, 0);
      check(!value.rates, "Invalid shapes must not publish a first baseline");
    });
    run("negative-first-time-does-not-initialize", [] {
      EngineStatisticsAccumulator accumulator;
      rejected(accumulator, {}, -1, EngineStatisticsError::InvalidTime);
      rejected(accumulator, {}, std::numeric_limits<I>::min(), EngineStatisticsError::InvalidTime);
      check(!observed(accumulator, {}, 0).rates, "Invalid first times must not become baseline");
    });
    run("equal-backward-negative-time-rejections-retain-previous-interval", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {10,20,5,10}, 5);
      for (const auto time : {I{-1}, I{0}, I{4}, I{5}}) {
        rejected(accumulator, {30,40,10,20}, time, EngineStatisticsError::InvalidTime);
      }
      const auto next = observed(accumulator, {12,24,6,12}, Second + 5);
      check(next.elapsedNanoseconds == Second && next.rates == EngineRates{2,4,1,2,1,2}, "Rejected time cannot alter totals or origin");
    });
    run("each-raw-regression-rejected-independently", [] {
      const std::array<EngineCounters, 4> bad{{{9,10,5,5}, {10,9,5,5}, {10,10,4,5}, {10,10,5,4}}};
      for (const auto counters : bad) {
        EngineStatisticsAccumulator accumulator;
        (void)observed(accumulator, {10,10,5,5}, 0);
        rejected(accumulator, counters, 1, EngineStatisticsError::CounterRegression);
        check(observed(accumulator, {11,11,6,6}, Second).rates == EngineRates{1,1,1,1,0,0}, "Rejected raw regression must not advance baseline");
      }
    });
    run("protocol-regression-despite-monotone-raw-fields-is-rejected", [] {
      for (const bool receive : {false,true}) {
        EngineStatisticsAccumulator accumulator;
        (void)observed(accumulator, {10,10,5,5}, 0);
        rejected(accumulator, receive ? EngineCounters{11,10,7,5} : EngineCounters{10,11,5,7}, 1,
            EngineStatisticsError::CounterRegression);
        check(observed(accumulator, {12,12,7,7}, Second).rates == EngineRates{2,2,2,2,0,0}, "Protocol regression must not change accepted state");
      }
    });
    run("fixed-error-priority-is-counters-then-time-then-regression", [] {
      EngineStatisticsAccumulator accumulator;
      (void)observed(accumulator, {10,10,5,5}, 10);
      rejected(accumulator, {-1,10,5,5}, -1, EngineStatisticsError::InvalidCounters);
      rejected(accumulator, {9,10,5,5}, 10, EngineStatisticsError::InvalidTime);
      rejected(accumulator, {9,10,5,5}, 11, EngineStatisticsError::CounterRegression);
      check(observed(accumulator, {10,10,5,5}, 11).rates == EngineRates{}, "All local rejections leave state identical");
    });
    run("copied-accumulators-and-returned-primitives-have-independent-state", [] {
      EngineStatisticsAccumulator first;
      const auto retained = observed(first, {10,20,5,10}, 0);
      auto second = first;
      (void)observed(first, {20,30,10,15}, Second);
      const auto value = observed(second, {12,24,6,12}, Second);
      check(value.rates == EngineRates{2,4,1,2,1,2}, "Copy must not borrow another accumulator's mutable baseline");
      check(retained == EngineStatistics{{10,20,5,10},0,std::nullopt}, "Returned primitive result cannot mutate later");
    });
    run("fresh-accumulator-is-the-explicit-new-owner-boundary", [] {
      EngineStatisticsAccumulator old;
      (void)observed(old, {100,100,90,90}, 100);
      rejected(old, {}, 101, EngineStatisticsError::CounterRegression);
      EngineStatisticsAccumulator fresh;
      const auto value = observed(fresh, {}, 0);
      check(value.elapsedNanoseconds == 0 && !value.rates, "New owner must not stitch prior session counters or time");
    });
    run("validator-admits-first-overflow-and-rounding-shaped-snapshots", [] {
      check(validEngineStatistics({{},0,{}}), "Valid first state");
      check(validEngineStatistics({{Max,Max,Max,0},Max,{}}), "Valid rate-unavailable later state");
      check(validEngineStatistics({{},1,EngineRates{3,5,1,2,1,2}}), "One-unit floor gap in each direction");
      check(validEngineStatistics({{},1,EngineRates{Max,Max,Max,Max,0,0}}), "Maximum rates do not overflow validation");
      check(validEngineStatistics({{},1,EngineRates{Max,Max,Max-1,Max-1,1,1}}), "Exact maximum component sums");
    });
    run("validator-rejects-all-malformed-raw-time-and-rate-signs", [] {
      for (const auto counters : std::array<EngineCounters,6>{{{-1,0,0,0},{0,-1,0,0},{0,0,-1,0},{0,0,0,-1},{0,0,1,0},{0,0,0,1}}}) {
        check(!validEngineStatistics({counters,1,{}}), "Malformed raw counters cannot be displayed as engine facts");
      }
      check(!validEngineStatistics({{},-1,{}}), "Negative elapsed is invalid");
      check(!validEngineStatistics({{},0,EngineRates{}}), "First observation cannot claim measured zero rates");
      for (const auto rates : std::array<EngineRates,6>{{{-1,0,0,0,0,0},{0,-1,0,0,0,0},{0,0,-1,0,0,0},{0,0,0,-1,0,0},{0,0,0,0,-1,0},{0,0,0,0,0,-1}}}) {
        check(!validEngineStatistics({{},1,rates}), "Every negative rate is rejected");
      }
    });
    run("validator-rejects-component-overflow-or-rounding-gap-above-one", [] {
      const std::array<EngineRates,6> bad{{{1,0,2,0,0,0},{0,1,0,2,0,0},{1,0,1,0,1,0},
          {0,1,0,1,0,1},{Max,0,Max,0,Max,0},{3,0,1,0,0,0}}};
      for (const auto rates : bad) { check(!validEngineStatistics({{},1,rates}), "Invalid/overflowing sums cannot pass validator"); }
      check(!validEngineStatistics({{},1,EngineRates{0,3,0,1,0,0}}), "Send-direction excessive gap rejected");
    });
    run("seeded-random-exact-rationals-independent-decimal-oracle", [] {
      std::mt19937_64 random(0x4249474c59425443ULL);
      const std::array<I,5> times{1,Second-1,Second,Second+1,Max};
      for (std::size_t i = 0; i < 512; ++i) {
        const I baseReceived = static_cast<I>(random() % 1000);
        const I baseSent = static_cast<I>(random() % 1000);
        const EngineCounters base{baseReceived,baseSent,baseReceived/2,baseSent/2};
        const I down = static_cast<I>(random() % (static_cast<U>(Max-base.received)+1));
        const I up = static_cast<I>(random() % (static_cast<U>(Max-base.sent)+1));
        const EngineCounters delta{down,up,static_cast<I>(random() % (static_cast<U>(down)+1)),
            static_cast<I>(random() % (static_cast<U>(up)+1))};
        const I interval = i % 2 ? times[i % times.size()] : static_cast<I>(random() % static_cast<U>(Max))+1;
        const EngineCounters current{base.received+delta.received,base.sent+delta.sent,
            base.payloadReceived+delta.payloadReceived,base.payloadSent+delta.payloadSent};
        EngineStatisticsAccumulator accumulator;
        (void)observed(accumulator, base, 0);
        const auto value = observed(accumulator, current, interval);
        check(value.elapsedNanoseconds == interval, "Random elapsed must be exact");
        referenceInterval(delta, interval, value);
      }
    });
    std::cout << "PASS " << cases << " engine-statistics generated cases; " << assertions
        << " assertions; 512 independent rational vectors; no Java/LT runtime equality credit\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL engine-statistics: " << failure.what() << '\n';
    return 1;
  }
}
