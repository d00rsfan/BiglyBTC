#include "biglybtc/peer_payload_ledger.hpp"

#include <algorithm>
#include <limits>

namespace biglybtc::core {
namespace {
bool validEndpoint(const PeerNumericEndpoint& p) noexcept {
  if (p.port == 0) { return false; }
  if (p.family == PeerAddressFamily::V4) {
    return p.scope == 0 && std::all_of(p.address.begin() + 4, p.address.end(),
        [](auto value) { return value == 0; });
  }
  return p.family == PeerAddressFamily::V6;
}
bool validSample(const PeerPayloadSample& p) noexcept {
  return p.received >= 0 && p.sent >= 0 && p.receivedRate >= 0 && p.sentRate >= 0;
}
} // namespace

PeerPayloadLedger::PeerPayloadLedger(PeerPayloadIdentity identity) noexcept {
  state_.identity = identity;
  if (!identity.applicationRun || !identity.profileSession || !identity.engineEpoch) {
    noteLoss(PeerPayloadIssue::InvalidIdentity);
  }
}
void PeerPayloadLedger::noteLoss(PeerPayloadIssue value) noexcept {
  issues_.fetch_or(static_cast<std::uint32_t>(value), std::memory_order_relaxed);
  if (value != PeerPayloadIssue::None) { rebaseline_.store(true, std::memory_order_release); }
}
void PeerPayloadLedger::issue(PeerPayloadRecord& record, PeerPayloadIssue value) noexcept {
  record.issues |= static_cast<std::uint32_t>(value); noteLoss(value);
}
bool PeerPayloadLedger::advance(PeerPayloadRecord* record) noexcept {
  if (state_.sequence == std::numeric_limits<std::uint64_t>::max()) {
    noteLoss(PeerPayloadIssue::SequenceExhausted); return false;
  }
  ++state_.sequence;
  if (record) { record->sequence = state_.sequence; }
  return true;
}
PeerPayloadRecord* PeerPayloadLedger::find(std::uint64_t token) noexcept {
  if (token == 0 || token > state_.count) { noteLoss(PeerPayloadIssue::InvalidToken); return nullptr; }
  return &state_.records[static_cast<std::size_t>(token - 1)];
}
bool PeerPayloadLedger::eventTime(PeerPayloadRecord& record, std::int64_t now) noexcept {
  if (now < 0 || now < record.eventNanoseconds) { issue(record, PeerPayloadIssue::Clock); return false; }
  record.eventNanoseconds = now; return true;
}
std::uint64_t PeerPayloadLedger::attach(std::uint64_t operation,
    const PeerNumericEndpoint& endpoint, PeerNetworkClass network, std::int64_t now) noexcept {
  try {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { noteLoss(PeerPayloadIssue::Contention); return 0; }
    if (!state_.identity.applicationRun || !state_.identity.profileSession || !state_.identity.engineEpoch) { return 0; }
    if (!operation) { noteLoss(PeerPayloadIssue::InvalidIdentity); return 0; }
    if (now < 0) { noteLoss(PeerPayloadIssue::Clock); return 0; }
    if (state_.count == state_.records.size()) { noteLoss(PeerPayloadIssue::Capacity); return 0; }
    if (!advance()) { return 0; }
    auto& record = state_.records[state_.count];
    record.connection = ++state_.count; record.operation = operation; record.sequence = state_.sequence;
    record.attachedNanoseconds = now; record.eventNanoseconds = now;
    if (validEndpoint(endpoint)) { record.remote = endpoint; }
    else { issue(record, PeerPayloadIssue::EndpointUnavailable); }
    if (network == PeerNetworkClass::Local || network == PeerNetworkClass::NonLocal) { record.network = network; }
    return record.connection;
  } catch (...) { noteLoss(PeerPayloadIssue::Synchronization); return 0; }
}
void PeerPayloadLedger::connected(std::uint64_t token, std::int64_t now) noexcept {
  try {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { noteLoss(PeerPayloadIssue::Contention); return; }
    auto* record = find(token); if (!record || !advance(record)) { return; }
    if (record->phase == PeerPayloadPhase::Disconnected || record->phase == PeerPayloadPhase::Released) {
      issue(*record, PeerPayloadIssue::Lifecycle); return;
    }
    if (eventTime(*record, now)) { record->phase = PeerPayloadPhase::Connected; }
  } catch (...) { noteLoss(PeerPayloadIssue::Synchronization); }
}
void PeerPayloadLedger::observe(std::uint64_t token, std::int64_t now,
    const std::optional<PeerPayloadSample>& sample, bool disconnect) noexcept {
  try {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { noteLoss(PeerPayloadIssue::Contention); return; }
    auto* record = find(token); if (!record || !advance(record)) { return; }
    if (record->phase == PeerPayloadPhase::Released || record->phase == PeerPayloadPhase::Disconnected) {
      issue(*record, PeerPayloadIssue::Lifecycle); return;
    }
    if (rebaseline_.exchange(false, std::memory_order_acq_rel)) {
      for (std::size_t i = 0; i < state_.count; ++i) { state_.records[i].byteBaselineAvailable = false; }
    }
    const bool contiguous = record->byteBaselineAvailable;
    record->byteBaselineAvailable = false;
    // Lifecycle can be known even when the clock or payload read was lost.
    if (disconnect) { record->disconnectObserved = true; record->phase = PeerPayloadPhase::Disconnected; }
    record->latestSampleAvailable = false;
    if (!eventTime(*record, now)) { return; }
    if (!sample) { issue(*record, PeerPayloadIssue::SampleUnavailable); return; }
    if (!validSample(*sample)) { issue(*record, PeerPayloadIssue::InvalidSample); return; }
    if (record->lastObserved && (sample->received < record->lastObserved->received
        || sample->sent < record->lastObserved->sent)) {
      issue(*record, PeerPayloadIssue::CounterRegression); return;
    }
    const auto accumulate = [&](std::optional<std::int64_t>& prefix, std::int64_t raw,
        std::int64_t previous, PeerPayloadIssue overflow) {
      if (hasPeerPayloadIssue(record->issues, overflow)) { return; }
      if (!prefix) { prefix = 0; }
      if (!contiguous) { return; }
      const auto delta = raw - previous; // Nonnegative monotone operands above.
      if (delta > std::numeric_limits<std::int64_t>::max() - *prefix) {
        prefix.reset(); issue(*record, overflow); return;
      }
      *prefix += delta;
    };
    accumulate(record->observedReceivedBytes, sample->received,
        record->lastObserved ? record->lastObserved->received : sample->received, PeerPayloadIssue::ReceivedPrefixOverflow);
    accumulate(record->observedSentBytes, sample->sent,
        record->lastObserved ? record->lastObserved->sent : sample->sent, PeerPayloadIssue::SentPrefixOverflow);
    record->lastObserved = *sample; record->observedNanoseconds = now; record->latestSampleAvailable = true;
    record->byteBaselineAvailable = !disconnect;
  } catch (...) { noteLoss(PeerPayloadIssue::Synchronization); }
}
void PeerPayloadLedger::released(std::uint64_t token) noexcept {
  try {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { noteLoss(PeerPayloadIssue::Contention); return; }
    auto* record = find(token); if (!record || !advance(record)) { return; }
    if (record->phase == PeerPayloadPhase::Released) { issue(*record, PeerPayloadIssue::Lifecycle); return; }
    if (!record->disconnectObserved) { issue(*record, PeerPayloadIssue::MissingDisconnect); }
    record->phase = PeerPayloadPhase::Released;
    record->byteBaselineAvailable = false;
  } catch (...) { noteLoss(PeerPayloadIssue::Synchronization); }
}
std::optional<PeerPayloadSnapshot> PeerPayloadLedger::snapshot() const noexcept {
  try {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { rebaseline_.store(true, std::memory_order_release); issues_.fetch_or(static_cast<std::uint32_t>(PeerPayloadIssue::Contention),
        std::memory_order_relaxed); return std::nullopt; }
    auto result = state_; result.issues = issues_.load(std::memory_order_relaxed); return result;
  } catch (...) {
    rebaseline_.store(true, std::memory_order_release);
    issues_.fetch_or(static_cast<std::uint32_t>(PeerPayloadIssue::Synchronization), std::memory_order_relaxed);
    return std::nullopt;
  }
}

} // namespace biglybtc::core
