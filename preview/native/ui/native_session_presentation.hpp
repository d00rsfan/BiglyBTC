#pragma once

#include "biglybtc/engine_statistics.hpp"
#include "biglybtc/source_rate_values.hpp"
#include "biglybtc/effective_rate_limits.hpp"

#include <QMetaType>
#include <memory>
#include <optional>

namespace biglybtc::ui {

// Absolute counters of one persistent native engine epoch. They are neither
// operation sums nor saved lifetime totals. Identity is process-local in stage A.
enum class NativeSessionState { Starting, Idle, Active, Retiring, Closing, Closed, Uncertified };
struct NativeSessionPacket {
  quint64 session = 0;
  quint64 applicationRun = 0;
  quint64 epoch = 0;
  quint64 sequence = 0;
  NativeSessionState state = NativeSessionState::Starting;
  std::optional<core::EngineStatistics> sample{};
  qint64 ownerElapsedNanoseconds = 0;
  std::optional<core::SourceRateValues> sourceRates{};
  core::SessionRateLimits rateLimits{};
};
using NativeSessionPresentation = std::shared_ptr<const NativeSessionPacket>;

inline bool acceptNativeSession(const NativeSessionPresentation& current,
    const NativeSessionPresentation& incoming, quint64 expectedSession) {
  const auto valid = [expectedSession](const NativeSessionPresentation& p) {
    if (!p || !expectedSession || p->session != expectedSession || !p->applicationRun
        || !p->epoch || !p->sequence || p->ownerElapsedNanoseconds < 0
        || !core::validSessionRateLimits(p->rateLimits)
        || (p->sourceRates && p->sourceRates->smoothing != core::sourceSmoothingPolicy(p->sourceRates->smoothing.windowSeconds))
        || (p->sample && (!core::validEngineStatistics(*p->sample)
            || p->sample->elapsedNanoseconds > p->ownerElapsedNanoseconds))) { return false; }
    switch (p->state) {
      case NativeSessionState::Starting: return !p->sample;
      case NativeSessionState::Idle: case NativeSessionState::Active:
      case NativeSessionState::Retiring: case NativeSessionState::Closing:
      case NativeSessionState::Closed: case NativeSessionState::Uncertified: return true;
    }
    return false;
  };
  if (!valid(incoming)) { return false; }
  if (!current) { return true; }
  if (!valid(current) || current->applicationRun != incoming->applicationRun
      || incoming->epoch < current->epoch) { return false; }
  if (incoming->epoch > current->epoch) { return true; }
  if (current->state == NativeSessionState::Closed || incoming->sequence <= current->sequence
      || incoming->ownerElapsedNanoseconds < current->ownerElapsedNanoseconds
      || (current->sample && !incoming->sample)) { return false; }
  if (current->sample && incoming->sample) {
    const auto& a = *current->sample;
    const auto& b = *incoming->sample;
    if (b.elapsedNanoseconds < a.elapsedNanoseconds
        || b.counters.received < a.counters.received || b.counters.sent < a.counters.sent
        || b.counters.payloadReceived < a.counters.payloadReceived
        || b.counters.payloadSent < a.counters.payloadSent
        || b.counters.received - b.counters.payloadReceived < a.counters.received - a.counters.payloadReceived
        || b.counters.sent - b.counters.payloadSent < a.counters.sent - a.counters.payloadSent
        || (b.elapsedNanoseconds == a.elapsedNanoseconds && b != a)) { return false; }
  }
  return true;
}
} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::NativeSessionPresentation)
