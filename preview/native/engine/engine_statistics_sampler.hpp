#pragma once

#include "biglybtc/engine_statistics.hpp"

#include <libtorrent/fwd.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace biglybtc::engine {
struct LocalOptions;
namespace detail {

// Private, single-owner-thread sampler for one already-owned LT session. It
// consumes only the owner's existing popped alert batch, never a second queue.
// Empty callback means no metric query, clock read, statistics post or alert scan.
// Metric/clock/sample/post/callback errors disable this optional observation;
// they cannot fail a transfer, reconfigure LT, wait for a final alert or save.
// Counter/rate scope is this session's peer payload+protocol only. Last observed
// means just that: no exact teardown-total or full-application lifetime claim.
class SessionStatisticsSampler final {
public:
  SessionStatisticsSampler(libtorrent::session&, const LocalOptions&) noexcept;
  SessionStatisticsSampler(const SessionStatisticsSampler&) = delete;
  SessionStatisticsSampler& operator=(const SessionStatisticsSampler&) = delete;
  SessionStatisticsSampler(SessionStatisticsSampler&&) = delete;
  SessionStatisticsSampler& operator=(SessionStatisticsSampler&&) = delete;
  void poll(const std::vector<libtorrent::alert*>&) noexcept;
  // Explicit stage requests may bypass the ordinary >=200ms cadence. While a
  // post is outstanding, repeated requests coalesce into one fresh next post.
  void request() noexcept;
private:
  bool elapsedNow(std::int64_t&) const noexcept;
  void post(std::int64_t) noexcept;
  void disable() noexcept;
  libtorrent::session& session_;
  std::function<void(const core::EngineStatistics&)> callback_{};
  core::EngineStatisticsAccumulator accumulator_{};
  std::array<int, 4> indices_{{-1, -1, -1, -1}};
  std::int64_t originNanoseconds_ = 0;
  std::int64_t lastPostNanoseconds_ = 0;
  bool enabled_ = false;
  bool outstanding_ = false;
  bool forceNext_ = false;
};

} // namespace detail
} // namespace biglybtc::engine
