#include "source_rate_projection.hpp"

#include <utility>

namespace biglybtc::app {
namespace {
bool monotonic(const core::EngineCounters& before, const core::EngineCounters& after) noexcept {
  // validEngineStatistics has established nonnegative total>=payload, so every
  // subtraction is within0..INT64_MAX and has no signed overflow.
  return after.received >= before.received && after.sent >= before.sent
      && after.payloadReceived >= before.payloadReceived && after.payloadSent >= before.payloadSent
      && after.received - after.payloadReceived >= before.received - before.payloadReceived
      && after.sent - after.payloadSent >= before.sent - before.payloadSent;
}
}

std::optional<core::SourceRateValues> SourceRateProjection::observe(
    const std::optional<core::EngineStatistics>& sample,
    std::int64_t ownerElapsedNanoseconds, std::uint64_t sequence) noexcept {
  if (failed_ || !sequence || sequence <= sequence_ || ownerElapsedNanoseconds < 0
      || ownerElapsedNanoseconds < ownerElapsed_) { return {}; }
  if (!sample) {
    // A missing observation creates a gap, not a zero event or a new baseline.
    sequence_ = sequence; ownerElapsed_ = ownerElapsedNanoseconds; gap_ = true;
    return {};
  }
  if (!core::validEngineStatistics(*sample)
      || sample->elapsedNanoseconds > ownerElapsedNanoseconds) { return {}; }
  if (previous_) {
    if (sample->elapsedNanoseconds < previous_->elapsedNanoseconds
        || !monotonic(previous_->counters, sample->counters)) { return {}; }
    if (sample->elapsedNanoseconds == previous_->elapsedNanoseconds) {
      if (*sample != *previous_) { return {}; }
      // Phase/status publication of the exact same sample cannot age windows
      // or charge its tail again. It can retain last-observed known values.
      sequence_ = sequence; ownerElapsed_ = ownerElapsedNanoseconds;
      return gap_ ? std::optional<core::SourceRateValues>{} : last_;
    }
    if (ownerElapsedNanoseconds <= ownerElapsed_) { return {}; }
  }
  const auto millis = ownerElapsedNanoseconds / 1000000;
  if (!previous_) {
    auto bucket = core::SourceRateAverage::create(1000, 10, millis);
    auto smoothing = core::SourceRateSmoothing::create(30, millis);
    if (!bucket || !smoothing) { failed_ = true; return {}; }
    payloadReceived_ = *bucket; protocolReceived_ = *bucket;
    payloadSent_ = *bucket; protocolSent_ = *bucket;
    smoothing_ = std::move(*smoothing);
    baseline_ = sample->counters; previous_ = *sample;
    sequence_ = sequence; ownerElapsed_ = ownerElapsedNanoseconds; gap_ = false;
    return {}; // No burst from bytes that predate the first actual observation.
  }

  const auto& a = previous_->counters;
  const auto& b = sample->counters;
  const auto protocolReceived = b.received - b.payloadReceived;
  const auto protocolSent = b.sent - b.payloadSent;
  const auto previousProtocolReceived = a.received - a.payloadReceived;
  const auto previousProtocolSent = a.sent - a.payloadSent;
  // Monotonicity prevalidation makes all differences nonnegative and <=MAX.
  // Relative payload+protocol equals the relative combined counter, also<=MAX.
  const core::SourceTrafficTotals relative{
      b.payloadReceived - baseline_.payloadReceived,
      protocolReceived - (baseline_.received - baseline_.payloadReceived),
      b.payloadSent - baseline_.payloadSent,
      protocolSent - (baseline_.sent - baseline_.payloadSent)};
  if (!payloadReceived_->add(b.payloadReceived - a.payloadReceived, millis)
      || !protocolReceived_->add(protocolReceived - previousProtocolReceived, millis)
      || !payloadSent_->add(b.payloadSent - a.payloadSent, millis)
      || !protocolSent_->add(protocolSent - previousProtocolSent, millis)
      // Default interval1 admits every explicit delivery. This is a window over
      // actual delivery deltas, not a fabricated original SimpleTimer schedule.
      || !smoothing_->tick(0, 30, relative, millis)) {
    failed_ = true; return {};
  }
  const auto rxPayload = payloadReceived_->average(millis);
  const auto rxProtocol = protocolReceived_->average(millis);
  const auto txPayload = payloadSent_->average(millis);
  const auto txProtocol = protocolSent_->average(millis);
  const auto smooth = smoothing_->rates(millis);
  if (!rxPayload || !rxProtocol || !txPayload || !txProtocol || !smooth) {
    failed_ = true; return {};
  }
  last_ = core::SourceRateValues{*rxPayload, *rxProtocol, *txPayload, *txProtocol,
      smooth->received, smooth->sent};
  previous_ = *sample; sequence_ = sequence;
  ownerElapsed_ = ownerElapsedNanoseconds; gap_ = false;
  return last_;
}

} // namespace biglybtc::app
