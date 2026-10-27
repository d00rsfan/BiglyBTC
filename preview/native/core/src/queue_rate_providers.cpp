#include "biglybtc/queue_rate_providers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace biglybtc::core {
namespace {
bool valid(const QueueRateStamp& stamp) noexcept {
  return stamp.identity && stamp.sequence && stamp.monotonicMillis >= 0;
}
bool combined(QueueRateScope scope) noexcept {
  return scope == QueueRateScope::OriginalCombined || scope == QueueRateScope::OwnerCombined;
}
bool payload(QueueRateScope scope) noexcept {
  return scope == QueueRateScope::OriginalPayload || scope == QueueRateScope::TargetPayload;
}
bool gap(const QueueRateStamp& before, const QueueRateStamp& after) noexcept {
  // A newer accepted sequence may share a millisecond; this is not a gap.
  return after.monotonicMillis < before.monotonicMillis
      || after.monotonicMillis - before.monotonicMillis > QueueGlobalReceiveAverage::MaximumGapMillis;
}
bool stale(const QueueRateStamp& before, const QueueRateStamp& after) noexcept {
  return before.identity == after.identity && after.sequence <= before.sequence;
}
}

std::size_t QueueGlobalReceiveAverage::sampleCount() const noexcept {
  return std::min<std::uint32_t>(position_, 10);
}
std::optional<std::int32_t> QueueGlobalReceiveAverage::observe(const QueueRateSample& sample) noexcept {
  if (previous_ && stale(previous_->stamp, sample.stamp)) { return {}; }
  if (!valid(sample.stamp) || !combined(sample.scope) || !sample.receivedBytes || *sample.receivedBytes < 0) {
    *this = {}; return {};
  }
  if (previous_ && (sample.stamp.identity != previous_->stamp.identity || sample.scope != previous_->scope)) {
    *this = {};
  }
  if (previous_ && (gap(previous_->stamp, sample.stamp) || *sample.receivedBytes < *previous_->receivedBytes)) {
    *this = {}; return {};
  }
  if (!previous_) {
    previous_ = sample; tickMillis_ = sample.stamp.monotonicMillis; tickReceived_ = *sample.receivedBytes;
    return {}; // Bytes before the first accepted observation are not a burst.
  }
  previous_ = sample;
  if (sample.stamp.monotonicMillis - tickMillis_ < IntervalMillis) { return last_; }
  // Monotone nonnegative counters make this subtraction checked and bounded.
  const auto delta = *sample.receivedBytes - tickReceived_;
  total_ -= values_[position_ % 10];
  total_ += static_cast<double>(delta);
  values_[position_++ % 10] = static_cast<double>(delta);
  if (position_ == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) { position_ %= 10; }
  const auto count = sampleCount();
  if (position_ % 10 == 0) {
    total_ = 0;
    for (std::size_t i = 0; i < count; ++i) { total_ += values_[i]; }
  }
  const double kib = ((total_ / static_cast<double>(count)) * 1000.0 / 1500.0) / 1024.0;
  tickMillis_ = sample.stamp.monotonicMillis; tickReceived_ = *sample.receivedBytes;
  if (!std::isfinite(kib) || kib < 0) { *this = {}; return {}; }
  // Java double->int saturates; it does not narrow/wrap the high bits.
  last_ = kib >= static_cast<double>(std::numeric_limits<std::int32_t>::max())
      ? std::numeric_limits<std::int32_t>::max() : static_cast<std::int32_t>(kib);
  return last_;
}

std::optional<std::int64_t> QueuePayloadRateSmoothing::observe(const QueueRateSample& sample,
    std::int32_t tick, int configured) noexcept {
  if (previous_ && stale(previous_->stamp, sample.stamp)) { return {}; }
  if (!valid(sample.stamp) || !payload(sample.scope) || !sample.receivedBytes || *sample.receivedBytes < 0 || tick < 0) {
    *this = {}; return {};
  }
  if (previous_ && (sample.stamp.identity != previous_->stamp.identity || sample.scope != previous_->scope)) {
    *this = {};
  }
  if (previous_ && (gap(previous_->stamp, sample.stamp) || *sample.receivedBytes < *previous_->receivedBytes || tick < lastTick_)) {
    *this = {}; return {};
  }
  if (!previous_) {
    policy_ = sourceSmoothingPolicy(configured);
    const auto ring = SourceRateAverage::create(policy_.intervalSeconds * 1000, policy_.windowSeconds,
                                               sample.stamp.monotonicMillis);
    if (!ring) { *this = {}; return {}; }
    average_ = *ring; usedReceived_ = *sample.receivedBytes;
    previous_ = sample; lastTick_ = tick;
    return {};
  }
  // The ORIGINAL old interval gates seeing/applying the new setting.
  if (tick != lastTick_ && tick % policy_.intervalSeconds == 0) {
    const auto desired = sourceSmoothingPolicy(configured);
    if (desired != policy_) {
      const auto ring = SourceRateAverage::create(desired.intervalSeconds * 1000, desired.windowSeconds,
                                                 sample.stamp.monotonicMillis);
      if (!ring) { *this = {}; return {}; }
      average_ = *ring; policy_ = desired;
    }
    if (!average_->add(*sample.receivedBytes - usedReceived_, sample.stamp.monotonicMillis)) {
      *this = {}; return {};
    }
    usedReceived_ = *sample.receivedBytes;
  }
  const auto rate = average_->average(sample.stamp.monotonicMillis);
  if (!rate || *rate < 0) { *this = {}; return {}; }
  previous_ = sample; lastTick_ = tick;
  return *rate;
}

std::optional<std::int64_t> QueueSendRateSmoothing::observe(const QueueRateSample& sample,
    std::int32_t tick,int configured) noexcept {
  if(!combined(sample.scope)&&!payload(sample.scope)){*this={};return {};}
  if(scope_&&*scope_!=sample.scope){average_={};}
  scope_=sample.scope;auto value=sample;
  // Arithmetic is channel-independent; this private adapter never relabels
  // externally published scope or passes payload as a combined fallback.
  value.scope=QueueRateScope::TargetPayload;
  return average_.observe(value,tick,configured);
}

std::optional<std::int64_t> QueueSmoothedEta::observe(const QueueEtaSample& sample) noexcept {
  if (previous_ && stale(*previous_, sample.stamp)) { return {}; }
  if (!valid(sample.stamp) || sample.wallMillis < 0 || !sample.peerRemaining || !sample.diskRemainingExcludingDnd
      || *sample.peerRemaining < 0 || *sample.diskRemainingExcludingDnd < 0) { *this = {}; return {}; }
  if (previous_ && sample.stamp.identity != previous_->identity) { *this = {}; }
  if (previous_ && (gap(*previous_, sample.stamp) || sample.wallMillis < wall_)) { *this = {}; return {}; }
  const auto remaining = std::max(*sample.peerRemaining, *sample.diskRemainingExcludingDnd);
  if (remaining == 0) {
    if (!sample.startedMonotonicMillis || !sample.finishedMonotonicMillis || *sample.startedMonotonicMillis < 0
        || *sample.finishedMonotonicMillis < *sample.startedMonotonicMillis
        || *sample.finishedMonotonicMillis > sample.stamp.monotonicMillis) { *this = {}; return {}; }
  } else if (!sample.smoothedPayloadBytesPerSecond || *sample.smoothedPayloadBytesPerSecond < 0) {
    *this = {}; return {};
  }
  previous_ = sample.stamp; wall_ = sample.wallMillis;
  // Java's uncalculated0 during its first900 wall milliseconds is not a known ETA.
  if (!calculatedWall_ && sample.wallMillis <= 900) { return {}; }
  if (!calculatedWall_ || sample.wallMillis - *calculatedWall_ > 900) {
    if (remaining == 0) {
      const auto elapsed = (*sample.finishedMonotonicMillis - *sample.startedMonotonicMillis) / 1000;
      last_ = elapsed > 1 ? -elapsed : 0;
    } else {
      const auto rate = *sample.smoothedPayloadBytesPerSecond;
      const auto eta = rate == 0 ? InfiniteSeconds : remaining / rate;
      last_ = eta == 0 ? 1 : eta;
    }
    calculatedWall_ = sample.wallMillis;
  }
  return last_;
}
} // namespace biglybtc::core
