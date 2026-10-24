#pragma once

#include <libtorrent/fwd.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

namespace biglybtc::engine::detail {

// Essential per-admission counter continuity, separate from the deliberately
// lossy peer/country observer. Only fixed scalars cross the LT/worker boundary.
class ManagedPayloadCounters final {
public:
  struct Epoch { std::uint64_t number = 0; };
  enum class Read { Accepted, Retry, Failed };
  struct Sample { Read state = Read::Failed; std::int64_t received = 0, sent = 0; };
  void installed() noexcept { installed_.store(true, std::memory_order_release); }
  void fail() noexcept { failed_.store(true, std::memory_order_release); }
  std::optional<Epoch> epoch() const noexcept {
    try {
      std::lock_guard lock(mutex_);
      if (!installed_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) { return {}; }
      return Epoch{epoch_};
    } catch (...) { failed_.store(true, std::memory_order_release); return {}; }
  }
  // LT thread, immediately before the qualified source resets m_stat. This is
  // the FINAL raw epoch read, not a previously polled prefix or tick estimate.
  void seal(std::int64_t received, std::int64_t sent) noexcept {
    try {
      std::lock_guard lock(mutex_);
      if (failed_.load(std::memory_order_acquire)) { return; }
      if (!fits(receivedBase_, received) || !fits(sentBase_, sent)
          || receivedBase_ + received < publishedReceived_ || sentBase_ + sent < publishedSent_
          || epoch_ == std::numeric_limits<std::uint64_t>::max()) { fail(); return; }
      receivedBase_ += received; sentBase_ += sent; ++epoch_;
    } catch (...) { fail(); }
  }
  // Worker: epoch was read BEFORE the existing synchronous handle.status().
  // A callback crossing that read requires another actual status, never a guess.
  Sample combine(Epoch before, std::int64_t received, std::int64_t sent) noexcept {
    try {
      std::lock_guard lock(mutex_);
      if (!installed_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) { return {}; }
      if (before.number != epoch_) { return {Read::Retry}; }
      if (!fits(receivedBase_, received) || !fits(sentBase_, sent)
          || receivedBase_ + received < publishedReceived_ || sentBase_ + sent < publishedSent_) {
        fail(); return {};
      }
      publishedReceived_ = receivedBase_ + received; publishedSent_ = sentBase_ + sent;
      return {Read::Accepted, publishedReceived_, publishedSent_};
    } catch (...) { fail(); return {}; }
  }
private:
  static bool fits(std::int64_t base, std::int64_t value) noexcept {
    return value >= 0 && value <= std::numeric_limits<std::int64_t>::max() - base;
  }
  mutable std::mutex mutex_;
  mutable std::atomic_bool failed_{false};
  std::atomic_bool installed_{false};
  std::uint64_t epoch_ = 0;
  std::int64_t receivedBase_ = 0, sentBase_ = 0;
  std::int64_t publishedReceived_ = 0, publishedSent_ = 0;
};

// Private source/ABI-bound implementation lives in the one LT shim TU.
bool attachManagedPayloadCounters(libtorrent::add_torrent_params&,
    const std::shared_ptr<ManagedPayloadCounters>&) noexcept;
} // namespace biglybtc::engine::detail
