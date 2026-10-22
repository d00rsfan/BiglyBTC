#include "biglybtc/source_rate_average.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace biglybtc::core;
namespace {
std::size_t assertions = 0, families = 0;
void check(bool result, const char* expression) {
  ++assertions;
  if (!result) { throw std::runtime_error(expression); }
}
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
SourceRateAverage average(int refresh = 1000, int period = 10, std::int64_t now = 100000) {
  auto value = SourceRateAverage::create(refresh, period, now);
  CHECK(value.has_value()); return *value;
}
template<class Function> void family(const char* name, Function function) {
  try { function(); ++families; }
  catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}
}
int main() {
  try {
    family("empty and lazy zero", [] {
      auto value = average(); CHECK(value.average(100000) == 0);
      CHECK(value.add(0, 110000)); CHECK(value.sum(110000) == 0);
      CHECK(value.point(110000) == 0);
    });
    family("current partial excluded and full denominator warmup", [] {
      auto value = average(); CHECK(value.add(1000, 100000));
      CHECK(value.average(100999) == 0); CHECK(value.average(101000) == 100);
      CHECK(value.point(101000) == 1000); CHECK(value.average(110000) == 100);
      CHECK(value.average(111000) == 0);
    });
    family("separate source buckets", [] {
      auto value = average(); CHECK(value.add(100, 100000)); CHECK(value.add(200, 100900));
      CHECK(value.add(400, 101000)); CHECK(value.sum(101000) == 300);
      CHECK(value.sum(102000) == 700);
    });
    family("signed correction and truncation", [] {
      auto value = average(); CHECK(value.add(-509, 100000)); CHECK(value.average(101000) == -50);
      CHECK(value.add(200, 101000)); CHECK(value.sum(102000) == -309);
      CHECK(value.average(102000) == -30);
    });
    family("same slot Java long wrap", [] {
      auto value = average(); CHECK(value.add(std::numeric_limits<std::int64_t>::max(), 100000));
      CHECK(value.add(std::numeric_limits<std::int64_t>::max(), 100000));
      CHECK(value.sum(101000) == -2); CHECK(value.average(101000) == 0);
    });
    family("sum Java long wrap", [] {
      auto value = average(); CHECK(value.add(std::numeric_limits<std::int64_t>::max(), 100000));
      CHECK(value.add(std::numeric_limits<std::int64_t>::max(), 101000));
      CHECK(value.sum(102000) == -2);
    });
    family("minimum signed value", [] {
      auto value = average(); CHECK(value.add(std::numeric_limits<std::int64_t>::min(), 100000));
      CHECK(value.sum(101000) == std::numeric_limits<std::int64_t>::min());
      CHECK(value.average(101000) == -922337203685477580LL);
    });
    family("explicit clear resets clock domain", [] {
      auto value = average(); CHECK(value.add(1234, 100000)); CHECK(value.clear(50000));
      CHECK(value.average(50000) == 0); CHECK(value.add(700, 50000)); CHECK(value.sum(51000) == 700);
    });
    family("idle ring clears stale data", [] {
      auto value = average(); CHECK(value.add(120, 100000)); CHECK(value.sum(999000) == 0);
      CHECK(value.add(15, 999000)); CHECK(value.sum(1000000) == 15);
    });
    family("requested millisecond periods", [] {
      auto value = average(); CHECK(value.add(100, 100000)); CHECK(value.add(200, 101000));
      CHECK(value.add(300, 102000)); CHECK(value.averageForMillis(-1, 103000) == 60);
      CHECK(value.averageForMillis(1, 103000) == 300);
      CHECK(value.averageForMillis(2000, 103000) == 250);
      CHECK(value.averageForMillis(std::numeric_limits<int>::max(), 103000) == 60);
    });
    family("point deliberately has no per second division", [] {
      auto value = average(2000, 10); CHECK(value.add(1000, 100000));
      CHECK(value.point(102000) == 1000); CHECK(value.averageForMillis(1, 102000) == 1000);
      CHECK(value.averageForMillis(4000, 102000) == 250); CHECK(value.average(102000) == 100);
    });
    family("fixed maximum capacity", [] {
      auto value = average(1000, 1800); CHECK(value.bucketCount() == SourceRateAverage::MaximumBuckets);
      CHECK(value.add(1800, 100000)); CHECK(value.average(101000) == 1);
      CHECK(value.average(1900000) == 1); CHECK(value.average(1901000) == 0);
    });
    family("largest nonnegative clock", [] {
      auto value = average(1000, 10, 9223372036854774000LL);
      CHECK(value.add(9, 9223372036854774000LL));
      CHECK(value.sum(std::numeric_limits<std::int64_t>::max()) == 9);
      CHECK(value.point(std::numeric_limits<std::int64_t>::max()) == 9);
    });
    family("invalid configuration has no constructed object", [] {
      CHECK(!SourceRateAverage::create(100, 10, 100000));
      CHECK(!SourceRateAverage::create(1500, 10, 100000));
      CHECK(!SourceRateAverage::create(21000, 30, 100000));
      CHECK(!SourceRateAverage::create(1000, 0, 100000));
      CHECK(!SourceRateAverage::create(1000, 1801, 100000));
      CHECK(!SourceRateAverage::create(20000, 10, 100000));
      CHECK(!SourceRateAverage::create(1000, 10, -1));
    });
    family("rejected malformed clock does not change admitted state", [] {
      auto value = average(); CHECK(value.add(100, 100000));
      const auto backward = value.add(999, 99000);
      const auto negative = value.sum(-1);
      CHECK(!backward && backward.error() == SourceRateError::ClockRegression);
      CHECK(!negative && negative.error() == SourceRateError::ClockRange);
      CHECK(value.sum(101000) == 100);
    });
    family("zero factor point divergence explicit", [] {
      auto value = average(1000, 10, 0); CHECK(value.add(7, 0));
      auto point = value.point(0); CHECK(!point && point.error() == SourceRateError::ClockRange);
      CHECK(value.sum(1000) == 7);
    });
    family("configuration normalization", [] {
      CHECK(sourceSmoothingPolicy(30) == (SourceSmoothingPolicy{30, 1}));
      CHECK(sourceSmoothingPolicy(std::numeric_limits<int>::min()) == (SourceSmoothingPolicy{10, 1}));
      CHECK(sourceSmoothingPolicy(119) == (SourceSmoothingPolicy{119, 1}));
      CHECK(sourceSmoothingPolicy(120) == (SourceSmoothingPolicy{120, 2}));
      CHECK(sourceSmoothingPolicy(std::numeric_limits<int>::max()) == (SourceSmoothingPolicy{1800, 20}));
    });
    family("production smoothing denominator", [] {
      auto value = SourceRateSmoothing::create(30, 100000); CHECK(value);
      CHECK(value->tick(1, 30, {3000, 300, 1200, 60}, 100000));
      CHECK(value->rates(100000) == (SourceSmoothedRates{0, 0}));
      CHECK(value->rates(101000) == (SourceSmoothedRates{110, 42}));
      CHECK(value->rates(131000) == (SourceSmoothedRates{0, 0}));
    });
    family("old interval before reconfiguration and baseline retained", [] {
      auto value = SourceRateSmoothing::create(120, 100000); CHECK(value);
      CHECK(value->tick(2, 120, {1200, 100, 0, 0}, 100000));
      CHECK(value->tick(3, 30, {1800, 150, 0, 0}, 101000));
      CHECK(value->policy() == (SourceSmoothingPolicy{120, 2}));
      CHECK(value->rates(102000) == (SourceSmoothedRates{10, 0}));
      CHECK(value->tick(4, 30, {1800, 150, 0, 0}, 102000));
      CHECK(value->policy() == (SourceSmoothingPolicy{30, 1}));
      CHECK(value->rates(103000) == (SourceSmoothedRates{21, 0}));
    });
    family("smoothing counter wrap", [] {
      auto value = SourceRateSmoothing::create(10, 100000); CHECK(value);
      CHECK(value->tick(1, 10, {std::numeric_limits<std::int64_t>::max(), 0, 0, 0}, 100000));
      CHECK(value->tick(2, 10, {std::numeric_limits<std::int64_t>::min(), 0, 0, 0}, 101000));
      CHECK(value->rates(102000) == (SourceSmoothedRates{-922337203685477580LL, 0}));
    });
    family("smoothing clock rejection before reset", [] {
      auto value = SourceRateSmoothing::create(30, 100000); CHECK(value);
      CHECK(value->tick(1, 30, {300, 0, 0, 0}, 100000));
      CHECK(!value->tick(2, 120, {999, 0, 0, 0}, 99000));
      CHECK(value->policy() == (SourceSmoothingPolicy{30, 1}));
      CHECK(value->rates(101000) == (SourceSmoothedRates{10, 0}));
    });
    family("int32 narrowing and Activity promotion are different", [] {
      CHECK(narrowSourceRate(2147483648LL) == std::numeric_limits<std::int32_t>::min());
      CHECK(narrowSourceRate(4294967295LL) == -1);
      CHECK(narrowSourceRate(-4294967297LL) == -1);
      CHECK(sourceCombinedIntRate(4294967303LL, 2147483651LL) == -2147483638);
      CHECK(sourceActivityCombinedRate(4294967303LL, -2147483645) == 2147483658LL);
      CHECK(sourceActivityCombinedRate(std::numeric_limits<std::int64_t>::max(), 1)
          == std::numeric_limits<std::int64_t>::min());
    });
    std::cout << "PASS " << families << " explicit-time source-rate families; " << assertions
              << " assertions; no timer/backend or Java runtime equality claim\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return EXIT_FAILURE;
  }
}
