#pragma once

#include <libtorrent/fwd.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

namespace biglybtc::storage { struct OriginalTorrentIdentity; }

namespace biglybtc::engine::detail {

// Essential per-admission counter continuity, separate from the deliberately
// lossy peer/country observer. Only fixed scalars cross the LT/worker boundary.
class ManagedPayloadCounters final {
public:
  struct Epoch { std::uint64_t number = 0; };
  struct Protocol { std::int64_t received = 0, sent = 0; };
  struct Bad { std::int64_t discarded = 0, hashFailed = 0; };
  enum class Read { Accepted, Retry, Failed };
  struct Sample {
    Read state = Read::Failed;
    std::int64_t received = 0, sent = 0;
    std::optional<std::int64_t> combinedReceived{}, combinedSent{};
    std::optional<Bad> bad{};
    // Certified lower bound survives later loss; never a current/final tail.
    std::optional<Bad> badPrefix{};
  };
  void installed(bool combined = false, bool bad = false) noexcept {
    badKnown_.store(bad, std::memory_order_release);
    combinedKnown_.store(combined, std::memory_order_release);
    installed_.store(true, std::memory_order_release);
  }
  void fail() noexcept { failed_.store(true, std::memory_order_release); }
  // Optional traffic uncertainty must not poison safe payload checkpointing.
  void combinedUnavailable() noexcept { combinedKnown_.store(false, std::memory_order_release); }
  void badUnavailable() noexcept { badKnown_.store(false, std::memory_order_release); }
  std::optional<Epoch> epoch() const noexcept {
    try {
      std::lock_guard lock(mutex_);
      if (!installed_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) { return {}; }
      return Epoch{epoch_};
    } catch (...) { failed_.store(true, std::memory_order_release); return {}; }
  }
  // Old callers/old provider: identical essential payload continuity and no
  // authority over protocol/bad fields. All channels share ONE reset epoch lock.
  void seal(std::int64_t received, std::int64_t sent) noexcept { seal(received, sent, {}); }
  void seal(std::int64_t received, std::int64_t sent, std::optional<Protocol> protocol,
      std::optional<Bad> rawBad = {}) noexcept {
    try {
      std::lock_guard lock(mutex_);
      if (failed_.load(std::memory_order_acquire)) { return; }
      if (!fits(receivedBase_, received) || !fits(sentBase_, sent)
          || receivedBase_ + received < publishedReceived_ || sentBase_ + sent < publishedSent_
          || epoch_ == std::numeric_limits<std::uint64_t>::max()) { fail(); return; }
      static_cast<void>(traffic(protocol, receivedBase_ + received, sentBase_ + sent, true));
      static_cast<void>(bad(rawBad, true));
      receivedBase_ += received; sentBase_ += sent; ++epoch_;
    } catch (...) { fail(); }
  }
  // Worker epoch is captured BEFORE the existing synchronous status() read.
  Sample combine(Epoch before, std::int64_t received, std::int64_t sent) noexcept {
    return combine(before, received, sent, {});
  }
  Sample combine(Epoch before, std::int64_t received, std::int64_t sent,
      std::optional<Protocol> protocol, std::optional<Bad> rawBad = {}) noexcept {
    try {
      std::lock_guard lock(mutex_);
      if (!installed_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) { return {}; }
      if (before.number != epoch_) { return {Read::Retry}; }
      if (!fits(receivedBase_, received) || !fits(sentBase_, sent)
          || receivedBase_ + received < publishedReceived_ || sentBase_ + sent < publishedSent_) {
        fail(); return {};
      }
      publishedReceived_ = receivedBase_ + received; publishedSent_ = sentBase_ + sent;
      Sample result{Read::Accepted, publishedReceived_, publishedSent_};
      result.bad = bad(rawBad, false);
      if (badObserved_) { result.badPrefix=Bad{publishedDiscarded_,publishedHashFailed_}; }
      if (const auto combined = traffic(protocol, publishedReceived_, publishedSent_, false)) {
        result.combinedReceived = combined->received; result.combinedSent = combined->sent;
      }
      return result;
    } catch (...) { fail(); return {}; }
  }
private:
  static bool fits(std::int64_t base, std::int64_t value) noexcept {
    return value >= 0 && value <= std::numeric_limits<std::int64_t>::max() - base;
  }
  // Caller already holds mutex_. Missing/regressing/overflowing protocol
  // irreversibly withdraws this admission's optional combined-byte authority.
  std::optional<Protocol> traffic(std::optional<Protocol> raw,
      std::int64_t payloadReceived, std::int64_t payloadSent, bool sealing) noexcept {
    if (!combinedKnown_.load(std::memory_order_acquire)) { return {}; }
    if (!raw || !fits(protocolReceivedBase_, raw->received) || !fits(protocolSentBase_, raw->sent)) {
      combinedUnavailable(); return {};
    }
    const auto received = protocolReceivedBase_ + raw->received, sent = protocolSentBase_ + raw->sent;
    if (received < publishedProtocolReceived_ || sent < publishedProtocolSent_
        || !fits(payloadReceived, received) || !fits(payloadSent, sent)) {
      combinedUnavailable(); return {};
    }
    if (sealing) { protocolReceivedBase_ = received; protocolSentBase_ = sent; }
    else { publishedProtocolReceived_ = received; publishedProtocolSent_ = sent; }
    return Protocol{payloadReceived + received, payloadSent + sent};
  }
  // Optional bad-byte continuity shares the exact payload reset lock/epoch.
  // Missing/saturated/regressing channels latch unavailable, never guessed0.
  std::optional<Bad> bad(std::optional<Bad> raw, bool sealing) noexcept {
    if (!badKnown_.load(std::memory_order_acquire)) { return {}; }
    if (!raw || !fits(discardedBase_, raw->discarded) || !fits(hashFailedBase_, raw->hashFailed)) {
      badUnavailable(); return {};
    }
    const Bad result{discardedBase_ + raw->discarded, hashFailedBase_ + raw->hashFailed};
    if (result.discarded < publishedDiscarded_ || result.hashFailed < publishedHashFailed_) {
      badUnavailable(); return {};
    }
    if (sealing) { discardedBase_ = result.discarded; hashFailedBase_ = result.hashFailed; }
    publishedDiscarded_ = result.discarded; publishedHashFailed_ = result.hashFailed;badObserved_=true;
    return result;
  }
  mutable std::mutex mutex_;
  mutable std::atomic_bool failed_{false};
  std::atomic_bool installed_{false}, combinedKnown_{false}, badKnown_{false};
  std::uint64_t epoch_ = 0;
  std::int64_t receivedBase_ = 0, sentBase_ = 0;
  std::int64_t publishedReceived_ = 0, publishedSent_ = 0;
  std::int64_t protocolReceivedBase_ = 0, protocolSentBase_ = 0;
  std::int64_t publishedProtocolReceived_ = 0, publishedProtocolSent_ = 0;
  bool badObserved_=false;
  std::int64_t discardedBase_ = 0, hashFailedBase_ = 0;
  std::int64_t publishedDiscarded_ = 0, publishedHashFailed_ = 0;
};

// Immutable pin to the approved provider's raw-counter AND selected-swarm ABI.
// Resolve BEFORE preparing payload storage; absent/stock/unbound is unsupported.
// The pin is retained by the installed factory/plugin for its full lifetime.
class ManagedQueueObservations;
struct ManagedPayloadCounterBridge;
std::shared_ptr<const ManagedPayloadCounterBridge> managedPayloadCounterBridge() noexcept;
// Before any state/payload effects, use a raw-info-validated full identity to
// choose exactly one immutable swarm. Header presence or a marker is insufficient:
// only the opaque, same-image-qualified bridge can authorize these flags.
// Failure leaves every parameter unchanged. No metadata/hash rewriting occurs.
bool selectManagedSwarm(libtorrent::add_torrent_params&,
    const storage::OriginalTorrentIdentity&,
    const std::shared_ptr<const ManagedPayloadCounterBridge>&) noexcept;
bool attachManagedPayloadCounters(libtorrent::add_torrent_params&,
    const std::shared_ptr<ManagedPayloadCounters>&,
    std::shared_ptr<const ManagedPayloadCounterBridge>,
    std::shared_ptr<ManagedQueueObservations> = {}) noexcept;
} // namespace biglybtc::engine::detail
