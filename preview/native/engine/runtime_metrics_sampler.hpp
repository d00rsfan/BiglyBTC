#pragma once

#include "biglybtc/runtime_metrics.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace libtorrent { struct stats_metric; }

namespace biglybtc::engine::detail {

// Optional same-worker conversion only. Never requests/pops alerts, reads a
// clock, calls a sink or changes peer accounting. Failure means unavailable.
class RuntimeMetricsSampler final {
public:
  bool map(std::span<const libtorrent::stats_metric>) noexcept;
  bool capture(std::span<const std::int64_t>, std::uint64_t sequence,
      std::int64_t ownerElapsedNanoseconds) noexcept;
  std::shared_ptr<const core::RuntimeMetrics> latest() const noexcept { return latest_; }
private:
  struct Definition {
    std::string name;
    int index;
    core::RuntimeMetricType type;
    core::RuntimeMetricGroup group;
  };
  std::vector<Definition> catalog_;
  std::shared_ptr<const core::RuntimeMetrics> latest_;
  std::uint64_t sequence_ = 0;
  std::int64_t ownerElapsed_ = 0;
};

} // namespace biglybtc::engine::detail
