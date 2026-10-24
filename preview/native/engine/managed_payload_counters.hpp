#pragma once

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/version.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

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

static_assert(LIBTORRENT_VERSION_NUM == 20012,
    "Managed raw reset boundaries require the reviewed LT2.0.12 source");
#if !defined(TORRENT_DISABLE_EXTENSIONS) && TORRENT_ABI_VERSION == 1 \
    && !TORRENT_USE_ASSERTS && !TORRENT_USE_INVARIANT_CHECKS \
    && TORRENT_USE_I2P == 1 && !defined(TORRENT_DISABLE_LOGGING)

class ManagedPayloadPlugin final : public libtorrent::torrent_plugin {
public:
  ManagedPayloadPlugin(libtorrent::torrent_handle handle, std::shared_ptr<ManagedPayloadCounters> counters)
      : handle_(std::move(handle)), counters_(std::move(counters)) { counters_->installed(); }
  bool on_pause() noexcept override { paused_ = true; return false; }
  bool on_resume() noexcept override {
    if (!paused_) { return false; } // Duplicate active resume does not reset m_stat.
    paused_ = false;
    try {
      // native_handle is weak. Its temporary strong pointer and raw stat copy
      // exist only during this LT callback, before the standard resume handler.
      const auto torrent = handle_.native_handle();
      if (!torrent) { counters_->fail(); return false; }
      const auto raw = torrent->statistics();
      if (!torrent->are_files_checked()
          && (raw.total_payload_download() != 0 || raw.total_payload_upload() != 0)) {
        counters_->fail(); return false; // No supported recheck after payload.
      }
      counters_->seal(raw.total_payload_download(), raw.total_payload_upload());
    } catch (...) { counters_->fail(); }
    return false; // Never override standard LT pause/resume behavior.
  }
  void on_files_checked() noexcept override {
    try {
      const auto torrent = handle_.native_handle();
      if (!torrent) { counters_->fail(); return; }
      const auto raw = torrent->statistics();
      // For qualified managed admission this is the initial check, before any
      // payload; files_checked subsequently starts its first announcing epoch.
      if (raw.total_payload_download() != 0 || raw.total_payload_upload() != 0) { counters_->fail(); }
    } catch (...) { counters_->fail(); }
  }
private:
  libtorrent::torrent_handle handle_; // Weak; never dereferenced by the worker.
  std::shared_ptr<ManagedPayloadCounters> counters_;
  bool paused_ = true;
};

inline bool attachManagedPayloadCounters(libtorrent::add_torrent_params& parameters,
    const std::shared_ptr<ManagedPayloadCounters>& counters) noexcept {
  try {
    parameters.extensions.push_back([counters](const libtorrent::torrent_handle& handle, libtorrent::client_data_t)
        -> std::shared_ptr<libtorrent::torrent_plugin> {
      try { return std::make_shared<ManagedPayloadPlugin>(handle, counters); }
      catch (...) { counters->fail(); return {}; }
    });
    return true;
  } catch (...) { counters->fail(); return false; }
}
#else
inline bool attachManagedPayloadCounters(libtorrent::add_torrent_params&,
    const std::shared_ptr<ManagedPayloadCounters>& counters) noexcept {
  counters->fail(); return false; // No ABI fallback or guessed zero counters.
}
#endif
} // namespace biglybtc::engine::detail
