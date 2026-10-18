#pragma once

#include "biglybtc/engine_statistics.hpp"
#include "library_model.hpp"

#include <QMetaType>
#include <QString>

#include <memory>
#include <optional>

namespace biglybtc::ui {

// One explicitly owned experimental transfer engine, not app/global totals.
// Immutable GUI packets; Window separately admits the real LibraryModel target.
enum class EngineStatisticsState { Preparing, Observed, Ended };
struct EngineStatisticsPacket {
  quint64 session = 0;
  quint64 operation = 0;
  quint64 sequence = 0;
  Target target{};
  QString name;
  EngineStatisticsState state = EngineStatisticsState::Preparing;
  std::optional<core::EngineStatistics> sample{};
  bool successful = false;
};
using EngineStatisticsPresentation = std::shared_ptr<const EngineStatisticsPacket>;

// Pure packet/ordering admission. A page may first receive an already-observed
// cached packet. A higher operation is a fresh run; lower or terminal same-run
// packets never replace the current facts. No timers or observations are added.
inline bool acceptEngineStatistics(const EngineStatisticsPresentation& current,
    const EngineStatisticsPresentation& incoming, quint64 expectedSession) {
  const auto valid = [expectedSession](const EngineStatisticsPresentation& packet) {
    if (!packet || expectedSession == 0 || packet->session != expectedSession
        || packet->operation == 0 || packet->sequence == 0
        || packet->target.generation == 0 || packet->target.hash.isEmpty()
        || packet->target.hash.size() > 256 || packet->name.size() > 8192
        || (packet->sample && !core::validEngineStatistics(*packet->sample))) { return false; }
    switch (packet->state) {
      case EngineStatisticsState::Preparing:
        return packet->sequence == 1 && !packet->sample && !packet->successful;
      case EngineStatisticsState::Observed:
        return packet->sequence > 1 && packet->sample && !packet->successful;
      case EngineStatisticsState::Ended:
        return packet->sequence > 1;
    }
    return false;
  };
  if (!valid(incoming)) { return false; }
  if (!current) { return true; }
  if (!valid(current) || incoming->operation < current->operation) { return false; }
  if (incoming->operation > current->operation) { return true; }
  if (current->state == EngineStatisticsState::Ended || incoming->sequence <= current->sequence
      || incoming->target != current->target) { return false; }
  if (current->sample && incoming->sample) {
    const auto& before = *current->sample; const auto& after = *incoming->sample;
    // Equal observation time denotes the same actual snapshot, not another
    // measurement. A newer terminal sample can contain coalesced observations.
    if (incoming->state == EngineStatisticsState::Ended
        && after.elapsedNanoseconds == before.elapsedNanoseconds && after != before) { return false; }
    if (after.counters.received < before.counters.received || after.counters.sent < before.counters.sent
        || after.counters.payloadReceived < before.counters.payloadReceived
        || after.counters.payloadSent < before.counters.payloadSent
        || after.counters.received - after.counters.payloadReceived
            < before.counters.received - before.counters.payloadReceived
        || after.counters.sent - after.counters.payloadSent
            < before.counters.sent - before.counters.payloadSent
        || after.elapsedNanoseconds < before.elapsedNanoseconds
        || (incoming->state == EngineStatisticsState::Observed
            && after.elapsedNanoseconds == before.elapsedNanoseconds)) { return false; }
  }
  return true;
}

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::EngineStatisticsPresentation)
