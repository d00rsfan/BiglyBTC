#include "runtime_metrics_sampler.hpp"

#include <libtorrent/session_stats.hpp>

#include <utility>

namespace biglybtc::engine::detail {

bool RuntimeMetricsSampler::map(std::span<const libtorrent::stats_metric> metrics) noexcept {
  latest_.reset(); catalog_.clear(); sequence_ = 0; ownerElapsed_ = 0;
  if (metrics.empty() || metrics.size() > core::MaximumRuntimeMetrics) { return false; }
  try {
    std::vector<Definition> staged;
    staged.reserve(metrics.size());
    for (const auto& metric : metrics) {
      if (!metric.name || metric.value_index < 0) { return false; }
      std::size_t length = 0;
      // LT catalog entries are C strings; bound the metadata scan to257 bytes.
      while (length <= core::MaximumRuntimeMetricNameBytes && metric.name[length]) { ++length; }
      if (!length || length > core::MaximumRuntimeMetricNameBytes) { return false; }
      const std::string_view name(metric.name, length);
      core::RuntimeMetricType type;
      switch (metric.type) {
        case libtorrent::metric_type_t::counter: type = core::RuntimeMetricType::Counter; break;
        case libtorrent::metric_type_t::gauge: type = core::RuntimeMetricType::Gauge; break;
        default: return false;
      }
      for (const auto& prior : staged) {
        if (prior.name == name || prior.index == metric.value_index) { return false; }
      }
      staged.push_back({std::string(name), metric.value_index, type, core::runtimeMetricGroup(name)});
    }
    catalog_ = std::move(staged);
    return true;
  } catch (...) {
    // This optional catalog must never escape into the essential map's catch.
    return false;
  }
}

bool RuntimeMetricsSampler::capture(std::span<const std::int64_t> counters,
    std::uint64_t sequence, std::int64_t ownerElapsedNanoseconds) noexcept {
  latest_.reset(); // Unavailable is distinct from retaining a stale successful snapshot.
  if (catalog_.empty() || !sequence || sequence <= sequence_ || ownerElapsedNanoseconds < 0
      || ownerElapsedNanoseconds < ownerElapsed_) { return false; }
  for (const auto& metric : catalog_) {
    if (static_cast<std::size_t>(metric.index) >= counters.size()) { return false; }
  }
  try {
    auto snapshot = std::make_shared<core::RuntimeMetrics>();
    snapshot->sequence = sequence;
    snapshot->ownerElapsedNanoseconds = ownerElapsedNanoseconds;
    snapshot->metrics.reserve(catalog_.size());
    for (const auto& metric : catalog_) {
      snapshot->metrics.push_back({metric.name, metric.type, metric.group,
          counters[static_cast<std::size_t>(metric.index)]});
    }
    // No input spans, LT name pointers or alert allocation survives this copy.
    latest_ = std::move(snapshot); sequence_ = sequence; ownerElapsed_ = ownerElapsedNanoseconds;
    return true;
  } catch (...) { return false; }
}

} // namespace biglybtc::engine::detail
