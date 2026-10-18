#include "engine_statistics_sampler.hpp"

#include "local_session.hpp"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_stats.hpp>

#include <array>
#include <chrono>
#include <limits>
#include <ratio>
#include <string_view>
#include <type_traits>

namespace biglybtc::engine::detail {
namespace {
using Clock = std::chrono::steady_clock;
// The supported Linux GCC/Clang/libstdc++ lanes expose signed nanosecond steady
// ticks. Make that concrete platform assumption a compile-time gate, avoiding
// an unchecked duration_cast or floating rounding of observation timestamps.
static_assert(Clock::is_steady && std::ratio_equal_v<Clock::period, std::nano>);
static_assert(std::is_integral_v<Clock::rep> && std::is_signed_v<Clock::rep>
    && std::numeric_limits<Clock::rep>::digits <= std::numeric_limits<std::int64_t>::digits);
constexpr std::int64_t CadenceNanoseconds = 200000000;
constexpr std::array<std::string_view, 4> Names{
    "net.recv_bytes", "net.sent_bytes", "net.recv_payload_bytes", "net.sent_payload_bytes"};
} // namespace

SessionStatisticsSampler::SessionStatisticsSampler(
    libtorrent::session& session, const LocalOptions& options) noexcept : session_(session) {
  if (!options.statistics) { return; }
  try {
    callback_ = options.statistics; // Own callback lifetime; optional copy can fail.
    const auto metrics = libtorrent::session_stats_metrics();
    for (std::size_t wanted = 0; wanted < Names.size(); ++wanted) {
      bool found = false;
      for (const auto& metric : metrics) {
        if (!metric.name || std::string_view(metric.name) != Names[wanted]) { continue; }
        if (found || metric.type != libtorrent::metric_type_t::counter || metric.value_index < 0) { return; }
        found = true;
        indices_[wanted] = metric.value_index;
      }
      if (!found) { return; }
      for (std::size_t earlier = 0; earlier < wanted; ++earlier) {
        if (indices_[earlier] == indices_[wanted]) { return; }
      }
    }
    originNanoseconds_ = static_cast<std::int64_t>(Clock::now().time_since_epoch().count());
    if (originNanoseconds_ < 0) { return; }
    enabled_ = true;
    request();
  } catch (...) { disable(); }
}

void SessionStatisticsSampler::disable() noexcept {
  enabled_ = false;
  outstanding_ = false;
  forceNext_ = false;
  // Retain the callback object until ordinary owner-thread destruction. A
  // callback may be the code currently unwinding or reentering request().
}

bool SessionStatisticsSampler::elapsedNow(std::int64_t& elapsed) const noexcept {
  const auto now = static_cast<std::int64_t>(Clock::now().time_since_epoch().count());
  if (now < 0 || now < originNanoseconds_) { return false; }
  elapsed = now - originNanoseconds_; // Both admitted within signed0..MAX.
  return true;
}

void SessionStatisticsSampler::post(std::int64_t elapsed) noexcept {
  if (!enabled_ || outstanding_) { return; }
  outstanding_ = true;
  forceNext_ = false;
  lastPostNanoseconds_ = elapsed;
  try { session_.post_session_stats(); }
  catch (...) { disable(); }
}

void SessionStatisticsSampler::request() noexcept {
  if (!enabled_) { return; }
  forceNext_ = true;
  if (outstanding_) { return; }
  std::int64_t elapsed = 0;
  if (!elapsedNow(elapsed) || elapsed < lastPostNanoseconds_) { disable(); return; }
  post(elapsed);
}

void SessionStatisticsSampler::poll(const std::vector<libtorrent::alert*>& pending) noexcept {
  if (!enabled_) { return; }
  try {
    // If the outstanding response was lost, do not invent a reset/baseline or
    // issue retries. Other dropped alert types leave transfer error handling to
    // the existing owner; this sampler never consumes or removes an alert.
    for (const auto* alert : pending) {
      if (!alert) { disable(); return; }
      const auto* dropped = libtorrent::alert_cast<libtorrent::alerts_dropped_alert>(alert);
      if (dropped && dropped->dropped_alerts.test(libtorrent::session_stats_alert::alert_type)) { disable(); return; }
    }
    for (const auto* alert : pending) {
      const auto* statistics = libtorrent::alert_cast<libtorrent::session_stats_alert>(alert);
      if (!statistics) { continue; }
      if (!outstanding_) { disable(); return; }
      outstanding_ = false;
      const auto counters = statistics->counters();
      for (const int index : indices_) {
        if (index < 0 || index >= counters.size()) { disable(); return; }
      }
      // Copy primitives while this popped batch owns the LT allocation. Neither
      // the span nor any alert/counter pointer escapes to the callback or owner.
      const core::EngineCounters sample{counters[indices_[0]], counters[indices_[1]],
          counters[indices_[2]], counters[indices_[3]]};
      std::int64_t elapsed = 0;
      if (!elapsedNow(elapsed)) { disable(); return; }
      const auto value = accumulator_.observe(sample, elapsed);
      if (!value) { disable(); return; }
      callback_(*value); // Any optional observer exception disables only sampling.
      if (!enabled_) { return; }
    }
    if (outstanding_) { return; }
    std::int64_t elapsed = 0;
    if (!elapsedNow(elapsed) || elapsed < lastPostNanoseconds_) { disable(); return; }
    if (forceNext_ || elapsed - lastPostNanoseconds_ >= CadenceNanoseconds) { post(elapsed); }
  } catch (...) { disable(); }
}

} // namespace biglybtc::engine::detail
