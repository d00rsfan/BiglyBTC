#include "../app/source_rate_projection.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

using biglybtc::app::SourceRateProjection;
using biglybtc::core::EngineCounters;
using biglybtc::core::EngineStatistics;
using biglybtc::core::SourceRateValues;
namespace {
constexpr std::int64_t Second = 1000000000;
std::size_t assertions = 0, families = 0;
void check(bool value, const char* expression) {
  ++assertions; if (!value) { throw std::runtime_error(expression); }
}
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
EngineStatistics sample(EngineCounters counters, std::int64_t elapsed) {
  return {counters, elapsed, {}};
}
template<class Function> void family(const char* name, Function function) {
  try { function(); ++families; }
  catch (const std::exception& failure) { throw std::runtime_error(std::string(name) + ": " + failure.what()); }
}
constexpr EngineCounters Baseline{1000, 2000, 800, 1600};
constexpr EngineCounters Advanced{2000, 2500, 1600, 2000};
constexpr SourceRateValues FirstWindow{80, 20, 40, 10, 33, 16};
void establish(SourceRateProjection& projection) {
  CHECK(!projection.observe(sample(Baseline, 0), 0, 1));
  CHECK(projection.observe(sample(Advanced, Second), Second, 2) == SourceRateValues{});
}
}
int main() {
  try {
    family("first observed absolute tail is baseline only", [] {
      SourceRateProjection projection;
      CHECK(!projection.observe(sample(Baseline, 0), 0, 1));
      CHECK(projection.observe(sample(Baseline, Second), Second, 2) == SourceRateValues{});
      CHECK(projection.observe(sample(Baseline, 2 * Second), 2 * Second, 3) == SourceRateValues{});
    });
    family("four component windows and combined default thirty", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
    });
    family("partial bucket excluded then ten second decay", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(projection.observe(sample(Advanced, 11 * Second), 11 * Second, 3) == FirstWindow);
      const auto expired = projection.observe(sample(Advanced, 12 * Second), 12 * Second, 4);
      CHECK(expired == (SourceRateValues{0, 0, 0, 0, 33, 16}));
      CHECK(projection.observe(sample(Advanced, 32 * Second), 32 * Second, 5) == SourceRateValues{});
    });
    family("delivery timestamp uses milliseconds without interval division", [] {
      SourceRateProjection projection;
      CHECK(!projection.observe(sample({}, 0), 0, 1));
      CHECK(projection.observe(sample({100, 50, 80, 40}, Second / 2), Second / 2, 2) == SourceRateValues{});
      CHECK(projection.observe(sample({100, 50, 80, 40}, Second + Second / 2), Second + Second / 2, 3)
          == (SourceRateValues{8, 2, 4, 1, 3, 1}));
    });
    family("repeated actual sample neither ages nor adds", [] {
      SourceRateProjection projection; establish(projection);
      const auto actual = sample(Advanced, 2 * Second);
      CHECK(projection.observe(actual, 2 * Second, 3) == FirstWindow);
      CHECK(projection.observe(actual, 100 * Second, 4) == FirstWindow);
      CHECK(projection.observe(sample(Advanced, 101 * Second), 101 * Second, 5) == SourceRateValues{});
    });
    family("missing before first actual sample does not create a zero", [] {
      SourceRateProjection projection;
      CHECK(!projection.observe({}, 0, 1));
      CHECK(!projection.observe({}, Second, 2));
      CHECK(!projection.observe(sample(Baseline, 0), 2 * Second, 3));
      CHECK(projection.observe(sample(Baseline, Second), 3 * Second, 4) == SourceRateValues{});
    });
    family("missing gap requires a fresh actual observation", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
      CHECK(!projection.observe({}, 3 * Second, 4));
      CHECK(!projection.observe(sample(Advanced, 2 * Second), 4 * Second, 5));
      CHECK(projection.observe(sample(Advanced, 5 * Second), 5 * Second, 6) == FirstWindow);
    });
    family("gap does not reset cumulative baseline or invent intermediate samples", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
      CHECK(!projection.observe({}, 20 * Second, 4));
      const EngineCounters later{3500, 2500, 2800, 2000};
      CHECK(projection.observe(sample(later, 21 * Second), 21 * Second, 5)
          == (SourceRateValues{0, 0, 0, 0, 33, 16}));
      CHECK(projection.observe(sample(later, 22 * Second), 22 * Second, 6)
          == (SourceRateValues{120, 30, 0, 0, 83, 16}));
    });
    family("invalid counter structure cannot replace baseline", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(!projection.observe(sample({100, 20, 101, 10}, 2 * Second), 2 * Second, 3));
      CHECK(!projection.observe(sample({-1, 20, 0, 10}, 2 * Second), 2 * Second, 3));
      CHECK(!projection.observe(sample(Advanced, -1), 2 * Second, 3));
      auto badRates = sample(Advanced, 2 * Second);
      badRates.rates = biglybtc::core::EngineRates{1, 0, 2, 0, 0, 0};
      CHECK(!projection.observe(badRates, 2 * Second, 3));
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
    });
    family("all raw counter regressions rejected without reset", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(!projection.observe(sample({1999, 2500, 1600, 2000}, 2 * Second), 2 * Second, 3));
      CHECK(!projection.observe(sample({2000, 2499, 1600, 2000}, 2 * Second), 2 * Second, 3));
      CHECK(!projection.observe(sample({2000, 2500, 1599, 2000}, 2 * Second), 2 * Second, 3));
      CHECK(!projection.observe(sample({2000, 2500, 1600, 1999}, 2 * Second), 2 * Second, 3));
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
    });
    family("derived protocol regression rejected independently", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(!projection.observe(sample({2100, 2500, 1800, 2000}, 2 * Second), 2 * Second, 3));
      CHECK(!projection.observe(sample({2000, 2600, 1600, 2200}, 2 * Second), 2 * Second, 3));
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
    });
    family("sequence and owner time prevalidation", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(!projection.observe(sample(Advanced, 2 * Second), 2 * Second, 0));
      CHECK(!projection.observe(sample(Advanced, 2 * Second), 2 * Second, 2));
      CHECK(!projection.observe(sample(Advanced, 2 * Second), -1, 3));
      CHECK(!projection.observe(sample(Advanced, 2 * Second), Second - 1, 3));
      CHECK(!projection.observe(sample(Advanced, 3 * Second), 2 * Second, 3));
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
    });
    family("actual observation time regression is not a new baseline", [] {
      SourceRateProjection projection; establish(projection);
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
      CHECK(!projection.observe(sample(Advanced, Second), 3 * Second, 4));
      CHECK(projection.observe(sample(Advanced, 4 * Second), 4 * Second, 5) == FirstWindow);
    });
    family("equal actual time must preserve the whole raw sample", [] {
      SourceRateProjection projection; establish(projection);
      auto changed = sample(Advanced, Second);
      changed.rates = biglybtc::core::EngineRates{10, 0, 10, 0, 0, 0};
      CHECK(!projection.observe(changed, 2 * Second, 3));
      CHECK(!projection.observe(sample({2001, 2500, 1601, 2000}, Second), 2 * Second, 3));
      CHECK(projection.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
    });
    family("source projection leaves raw interval sample unchanged", [] {
      SourceRateProjection projection;
      CHECK(!projection.observe(sample({}, 0), 0, 1));
      auto raw = sample({1000, 500, 800, 400}, Second);
      raw.rates = biglybtc::core::EngineRates{1000, 500, 800, 400, 200, 100};
      const auto before = raw;
      CHECK(projection.observe(raw, Second, 2) == SourceRateValues{});
      CHECK(raw == before);
      CHECK(projection.observe(sample(raw.counters, 2 * Second), 2 * Second, 3) == FirstWindow);
      CHECK(raw == before);
    });
    family("maximum cumulative counters use checked differences", [] {
      SourceRateProjection projection; const auto maximum = std::numeric_limits<std::int64_t>::max();
      CHECK(!projection.observe(sample({}, 0), 0, 1));
      const EngineCounters large{maximum, maximum, maximum - 10, maximum - 20};
      CHECK(projection.observe(sample(large, Second), Second, 2) == SourceRateValues{});
      CHECK(projection.observe(sample(large, 2 * Second), 2 * Second, 3)
          == (SourceRateValues{922337203685477579LL, 1, 922337203685477578LL, 2,
                              307445734561825860LL, 307445734561825860LL}));
    });
    family("maximum owner nanoseconds does not overflow millisecond conversion", [] {
      SourceRateProjection projection; const auto maximum = std::numeric_limits<std::int64_t>::max();
      CHECK(!projection.observe(sample({}, 0), maximum - 2 * Second, 1));
      CHECK(projection.observe(sample({60, 0, 60, 0}, Second), maximum - Second, 2) == SourceRateValues{});
      CHECK(projection.observe(sample({60, 0, 60, 0}, 2 * Second), maximum, 3)
          == (SourceRateValues{6, 0, 0, 0, 2, 0}));
    });
    family("new owner has independent fixed state and sequence cannot wrap", [] {
      SourceRateProjection first, second;
      establish(first); CHECK(first.observe(sample(Advanced, 2 * Second), 2 * Second, 3) == FirstWindow);
      CHECK(!second.observe(sample(Advanced, 0), 0, std::numeric_limits<std::uint64_t>::max() - 2));
      CHECK(second.observe(sample(Advanced, Second), Second, std::numeric_limits<std::uint64_t>::max() - 1) == SourceRateValues{});
      CHECK(second.observe(sample(Advanced, 2 * Second), 2 * Second, std::numeric_limits<std::uint64_t>::max()) == SourceRateValues{});
      CHECK(!second.observe(sample(Advanced, 3 * Second), 3 * Second, 1));
      CHECK(first.observe(sample(Advanced, 3 * Second), 3 * Second, 4) == FirstWindow);
    });
    std::cout << "PASS " << families << " source-rate projection families; " << assertions
              << " assertions; sample-delivery windows only, raw intervals unchanged\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL " << failure.what() << '\n'; return EXIT_FAILURE;
  }
}
