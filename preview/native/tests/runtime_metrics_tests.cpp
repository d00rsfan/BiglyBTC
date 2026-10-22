#include "../engine/runtime_metrics_sampler.hpp"

#include <libtorrent/session_stats.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Target-private GNU --wrap=_Znwm: fail one real allocation in optional copying,
// never replace a production allocator or libtorrent/session API.
namespace { thread_local bool failAllocation = false; }
extern "C" void* __real__Znwm(std::size_t);
extern "C" void* __wrap__Znwm(std::size_t bytes) {
  if (failAllocation) { failAllocation = false; throw std::bad_alloc(); }
  return __real__Znwm(bytes);
}

namespace {
namespace lt = libtorrent;
using namespace biglybtc::core;
using biglybtc::engine::detail::RuntimeMetricsSampler;
std::size_t assertions = 0, families = 0;
void check(bool value, const char* expression) {
  ++assertions; if (!value) { throw std::runtime_error(expression); }
}
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
lt::stats_metric metric(const char* name, int index, lt::metric_type_t type = lt::metric_type_t::counter) {
  return {name, index, type};
}
template<class Function> void family(const char* name, Function function) {
  try { function(); ++families; }
  catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}
}

int main() {
  try {
    family("unavailable before catalog and actual response", [] {
      RuntimeMetricsSampler sampler;
      CHECK(!sampler.latest()); CHECK(!sampler.capture({}, 1, 0));
      const std::array catalog{metric("net.actual", 0)};
      CHECK(sampler.map(catalog)); CHECK(!sampler.latest());
      CHECK(!sampler.capture({}, 1, 0)); CHECK(!sampler.latest());
    });
    family("actual type index order value and metadata", [] {
      RuntimeMetricsSampler sampler;
      const std::array catalog{metric("disk.queued", 2, lt::metric_type_t::gauge),
          metric("net.received", 0), metric("peer.connected", 1, lt::metric_type_t::gauge)};
      const std::array<std::int64_t, 3> counters{125, 3, 7};
      CHECK(sampler.map(catalog)); CHECK(sampler.capture(counters, 9, 123456));
      const auto result = sampler.latest(); CHECK(result && result->metrics.size() == 3);
      CHECK(result->sequence == 9 && result->ownerElapsedNanoseconds == 123456);
      CHECK(result->metrics[0] == (RuntimeMetric{"disk.queued", RuntimeMetricType::Gauge, RuntimeMetricGroup::Disk, 7}));
      CHECK(result->metrics[1] == (RuntimeMetric{"net.received", RuntimeMetricType::Counter, RuntimeMetricGroup::Net, 125}));
      CHECK(result->metrics[2] == (RuntimeMetric{"peer.connected", RuntimeMetricType::Gauge, RuntimeMetricGroup::Peer, 3}));
    });
    family("exact prefix groups preserve future unknown names", [] {
      CHECK(runtimeMetricGroup("disk.a") == RuntimeMetricGroup::Disk);
      CHECK(runtimeMetricGroup("tracker.a") == RuntimeMetricGroup::Tracker);
      CHECK(runtimeMetricGroup("dht.a") == RuntimeMetricGroup::Dht);
      CHECK(runtimeMetricGroup("peer.a") == RuntimeMetricGroup::Peer);
      CHECK(runtimeMetricGroup("net.a") == RuntimeMetricGroup::Net);
      for (const auto* name : {"disk", "Disk.a", "diskette.a", "dhtfoo.a", "future.a", ""}) {
        CHECK(runtimeMetricGroup(name) == RuntimeMetricGroup::Other);
      }
      RuntimeMetricsSampler sampler; const std::array catalog{metric("future.a", 0)};
      const std::array<std::int64_t, 1> counters{42};
      CHECK(sampler.map(catalog)); CHECK(sampler.capture(counters, 1, 0));
      CHECK(sampler.latest()->metrics[0] == (RuntimeMetric{"future.a", RuntimeMetricType::Counter, RuntimeMetricGroup::Other, 42}));
    });
    family("recorded zero signed gauges and full signed64 are exact", [] {
      RuntimeMetricsSampler sampler;
      const std::array catalog{metric("disk.zero", 0, lt::metric_type_t::gauge),
          metric("peer.negative", 1, lt::metric_type_t::gauge), metric("other.minimum", 2), metric("other.maximum", 3)};
      const std::array<std::int64_t, 4> counters{0, -17, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()};
      CHECK(sampler.map(catalog)); CHECK(sampler.capture(counters, 1, 0));
      const auto snapshot = sampler.latest(); CHECK(snapshot && snapshot->metrics.size() == 4);
      for (std::size_t i = 0; i < counters.size(); ++i) { CHECK(snapshot->metrics[i].value == counters[i]); }
    });
    family("names and values are owned after provider mutation", [] {
      RuntimeMetricsSampler sampler; std::string name = "disk.owned-name-longer-than-small-string";
      std::array catalog{metric(name.c_str(), 0)}; std::array<std::int64_t, 1> counters{91};
      CHECK(sampler.map(catalog)); name.assign("changed");
      CHECK(sampler.capture(counters, 1, 100)); const auto snapshot = sampler.latest();
      counters[0] = -500; catalog[0].value_index = 99;
      CHECK(snapshot->metrics[0].name == "disk.owned-name-longer-than-small-string");
      CHECK(snapshot->metrics[0].value == 91);
      CHECK(sampler.capture(counters, 2, 200)); CHECK(sampler.latest()->metrics[0].value == -500);
      CHECK(snapshot->metrics[0].value == 91 && snapshot->sequence == 1);
    });
    family("duplicate names rejected without partial snapshot", [] {
      RuntimeMetricsSampler sampler;
      const std::array catalog{metric("disk.same", 0), metric("disk.same", 1, lt::metric_type_t::gauge)};
      CHECK(!sampler.map(catalog)); CHECK(!sampler.latest());
      const std::array<std::int64_t, 2> counters{1, 2}; CHECK(!sampler.capture(counters, 1, 0));
    });
    family("duplicate indices rejected even with distinct names and types", [] {
      RuntimeMetricsSampler sampler;
      const std::array catalog{metric("disk.first", 0), metric("net.second", 0, lt::metric_type_t::gauge)};
      CHECK(!sampler.map(catalog)); CHECK(!sampler.latest());
    });
    family("null empty overlong and negative-index definitions rejected", [] {
      RuntimeMetricsSampler sampler; std::string tooLong(MaximumRuntimeMetricNameBytes + 1, 'x');
      for (const auto definition : {metric(nullptr, 0), metric("", 0), metric(tooLong.c_str(), 0), metric("disk.a", -1)}) {
        const std::array catalog{definition}; CHECK(!sampler.map(catalog)); CHECK(!sampler.latest());
      }
    });
    family("maximum name bytes admitted without truncation", [] {
      RuntimeMetricsSampler sampler; const std::string name(MaximumRuntimeMetricNameBytes, 'n');
      const std::array catalog{metric(name.c_str(), 0)}; const std::array<std::int64_t, 1> counters{5};
      CHECK(sampler.map(catalog)); CHECK(sampler.capture(counters, 1, 0));
      CHECK(sampler.latest()->metrics[0].name == name);
    });
    family("unknown type is unavailable rather than coerced", [] {
      RuntimeMetricsSampler sampler;
      const std::array catalog{metric("net.type", 0, static_cast<lt::metric_type_t>(29))};
      CHECK(!sampler.map(catalog)); CHECK(!sampler.latest());
    });
    family("case-distinct names are not normalized", [] {
      RuntimeMetricsSampler sampler; const std::array catalog{metric("disk.a", 0), metric("Disk.a", 1)};
      const std::array<std::int64_t, 2> counters{1, 2}; CHECK(sampler.map(catalog));
      CHECK(sampler.capture(counters, 1, 0)); CHECK(sampler.latest()->metrics[0].group == RuntimeMetricGroup::Disk);
      CHECK(sampler.latest()->metrics[1].group == RuntimeMetricGroup::Other);
    });
    family("all index bounds checked before any lookup", [] {
      RuntimeMetricsSampler sampler; const std::array catalog{metric("disk.a", 0), metric("disk.b", 2)};
      CHECK(sampler.map(catalog)); const std::array<std::int64_t, 2> shortInput{5, 6};
      CHECK(!sampler.capture(shortInput, 1, 0)); CHECK(!sampler.latest());
      const std::array hugeIndex{metric("disk.huge", std::numeric_limits<int>::max())};
      CHECK(sampler.map(hugeIndex)); CHECK(!sampler.capture(shortInput, 1, 0));
    });
    family("full catalog cap accepted one extra rejected not truncated", [] {
      RuntimeMetricsSampler sampler; std::vector<std::string> names;
      names.reserve(MaximumRuntimeMetrics + 1);
      for (std::size_t i = 0; i <= MaximumRuntimeMetrics; ++i) { names.push_back("other." + std::to_string(i)); }
      std::vector<lt::stats_metric> catalog; catalog.reserve(names.size());
      std::vector<std::int64_t> counters; counters.reserve(names.size());
      for (std::size_t i = 0; i < names.size(); ++i) {
        catalog.push_back(metric(names[i].c_str(), static_cast<int>(i))); counters.push_back(static_cast<std::int64_t>(i));
      }
      CHECK(sampler.map(std::span<const lt::stats_metric>(catalog.data(), MaximumRuntimeMetrics)));
      CHECK(sampler.capture(counters, 1, 0)); const auto held = sampler.latest();
      CHECK(held->metrics.size() == MaximumRuntimeMetrics && held->metrics.back().value == 1023);
      CHECK(!sampler.map(catalog)); CHECK(!sampler.latest());
      CHECK(held->metrics.size() == MaximumRuntimeMetrics && held->metrics.back().name == "other.1023");
    });
    family("empty catalog is unavailable distinct from recorded zero", [] {
      RuntimeMetricsSampler sampler; CHECK(!sampler.map({}));
      const std::array catalog{metric("disk.actual-zero", 0)}; const std::array<std::int64_t, 1> counters{0};
      CHECK(sampler.map(catalog)); CHECK(sampler.capture(counters, 1, 0));
      CHECK(sampler.latest() && sampler.latest()->metrics[0].value == 0);
      CHECK(!sampler.map({})); CHECK(!sampler.latest());
    });
    family("invalid sequence and time clear optional current snapshot", [] {
      RuntimeMetricsSampler sampler; const std::array catalog{metric("disk.a", 0)};
      const std::array<std::int64_t, 1> counters{7}; CHECK(sampler.map(catalog));
      CHECK(sampler.capture(counters, 9, 100)); const auto held = sampler.latest();
      CHECK(!sampler.capture(counters, 0, 101)); CHECK(!sampler.latest());
      CHECK(!sampler.capture(counters, 9, 101)); CHECK(!sampler.latest());
      CHECK(!sampler.capture(counters, 10, -1)); CHECK(!sampler.latest());
      CHECK(!sampler.capture(counters, 10, 99)); CHECK(!sampler.latest());
      CHECK(sampler.capture(counters, 10, 100)); CHECK(sampler.latest()->sequence == 10);
      CHECK(held->sequence == 9 && held->metrics[0].value == 7);
    });
    family("short response clears current but prior owning readers survive", [] {
      RuntimeMetricsSampler sampler; const std::array catalog{metric("disk.a", 0)};
      const std::array<std::int64_t, 1> counters{8}; CHECK(sampler.map(catalog));
      CHECK(sampler.capture(counters, 1, 0)); const auto held = sampler.latest();
      CHECK(!sampler.capture({}, 2, 1)); CHECK(!sampler.latest());
      CHECK(held->metrics[0].value == 8); CHECK(sampler.capture(counters, 3, 2));
    });
    family("catalog allocation failure contained and later mapping can recover", [] {
      RuntimeMetricsSampler sampler; const std::array catalog{metric("disk.long-name-for-owned-allocation", 0)};
      failAllocation = true;
      const auto mapped = sampler.map(catalog);
      CHECK(!failAllocation); CHECK(!mapped && !sampler.latest()); CHECK(sampler.map(catalog));
      const std::array<std::int64_t, 1> counters{9}; CHECK(sampler.capture(counters, 1, 0));
    });
    family("snapshot allocation failure clears optional only and old snapshot survives", [] {
      RuntimeMetricsSampler sampler; const std::array catalog{metric("disk.a", 0)};
      const std::array<std::int64_t, 1> counters{10}; CHECK(sampler.map(catalog));
      CHECK(sampler.capture(counters, 1, 0)); const auto held = sampler.latest();
      failAllocation = true; const auto captured = sampler.capture(counters, 2, 1);
      CHECK(!failAllocation); CHECK(!captured && !sampler.latest());
      CHECK(held->sequence == 1 && held->metrics[0].value == 10);
      CHECK(sampler.capture(counters, 3, 2)); CHECK(sampler.latest()->sequence == 3);
    });
    family("signed and sequence extremes remain actual metadata", [] {
      RuntimeMetricsSampler sampler; const std::array catalog{metric("net.a", 0)};
      const std::array<std::int64_t, 1> counters{std::numeric_limits<std::int64_t>::max()};
      CHECK(sampler.map(catalog));
      CHECK(sampler.capture(counters, std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::int64_t>::max()));
      CHECK(sampler.latest()->metrics[0].value == std::numeric_limits<std::int64_t>::max());
      CHECK(!sampler.capture(counters, 1, std::numeric_limits<std::int64_t>::max())); CHECK(!sampler.latest());
    });
    std::cout << "PASS " << families << " runtime-metrics families; " << assertions
              << " assertions; generated metadata only, no live session or provider claims\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    failAllocation = false; std::cerr << "FAIL " << error.what() << '\n'; return EXIT_FAILURE;
  }
}
