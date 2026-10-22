#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::core {

inline constexpr std::size_t MaximumRuntimeMetrics = 1024;
inline constexpr std::size_t MaximumRuntimeMetricNameBytes = 256;

enum class RuntimeMetricType { Counter, Gauge };
enum class RuntimeMetricGroup { Disk, Tracker, Dht, Peer, Net, Other };

inline RuntimeMetricGroup runtimeMetricGroup(std::string_view name) noexcept {
  if (name.starts_with("disk.")) { return RuntimeMetricGroup::Disk; }
  if (name.starts_with("tracker.")) { return RuntimeMetricGroup::Tracker; }
  if (name.starts_with("dht.")) { return RuntimeMetricGroup::Dht; }
  if (name.starts_with("peer.")) { return RuntimeMetricGroup::Peer; }
  if (name.starts_with("net.")) { return RuntimeMetricGroup::Net; }
  return RuntimeMetricGroup::Other;
}

struct RuntimeMetric {
  std::string name;
  RuntimeMetricType type = RuntimeMetricType::Counter;
  RuntimeMetricGroup group = RuntimeMetricGroup::Other;
  std::int64_t value = 0;
  bool operator==(const RuntimeMetric&) const = default;
};

// Owned values from one accepted session_stats_alert of ONE engine. Sequence
// counts accepted stats responses, not owner phase publications. Raw units and
// signed gauge/counter values are retained; no rates, zeros or units inferred.
struct RuntimeMetrics {
  std::uint64_t sequence = 0;
  std::int64_t ownerElapsedNanoseconds = 0;
  std::vector<RuntimeMetric> metrics;
};

} // namespace biglybtc::core
