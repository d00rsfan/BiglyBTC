#pragma once
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include <libtorrent/aux_/biglybtc_causal_test.hpp>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace biglybtc::test::causal {
namespace hook = libtorrent::biglybtc_causal;
using namespace std::chrono_literals;
inline std::array<std::int64_t, 4> counters(const core::EngineCounters& value) {
  return {value.received, value.sent, value.payloadReceived, value.payloadSent};
}
class Gates final {
public:
  Gates(bool socket, bool disk) { require(hook::arm(socket, disk), "Causal controller already armed"); }
  ~Gates() {
    // A failed cleanup is a failing TEST PROCESS, never a join under a held
    // causal gate. Normal assertions, cancellation and constructor failures
    // all reach this release path before fixture/peer destruction.
    if (!hook::release_all_and_wait(3s) || !hook::finish()) { std::terminate(); }
  }
  Gates(const Gates&) = delete;
  Gates& operator=(const Gates&) = delete;
};

// One real engine worker, one held generated profile, no concurrent owner API.
// Its cleanup ALWAYS releases all LT gates before any owner/worker join.
class Worker final {
public:
  struct State {
    bool ready = false;
    bool fatal = false;
    bool returned = false;
    bool successful = false;
    bool joined = false;
    bool pollSuccessful = false;
    std::optional<storage::ErrorCode> failure;
    std::optional<engine::RetirementReceipt> receipt;
    std::optional<engine::ApplicationPeerObservation> latest;
    std::optional<engine::CloseReceipt> close;
  };
  explicit Worker(const OwnedEngineFixture& fixture)
      : options_{.profile = fixture.native(), .hash = *fixture.hash,
          .peerPort = fixture.port(), .timeout = 20s, .stop = stop_.get_token()},
        thread_([this] { run(); }) {}
  ~Worker() { shutdown(); }
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  void cancel() noexcept { stop_.request_stop(); }
  State state() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
  bool wait_ready() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 10s, [&] { return state_.ready || state_.fatal; }) && state_.ready && !state_.fatal;
  }
  bool wait_returned() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 8s, [&] { return state_.returned || state_.fatal; }) && state_.returned && !state_.fatal;
  }
  bool poll_once() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!state_.returned || state_.fatal || closing_ || requested_ == std::numeric_limits<std::uint64_t>::max()) { return false; }
    const auto request = ++requested_; changed_.notify_all();
    return changed_.wait_for(lock, 2s, [&] { return completed_ >= request || state_.fatal; })
        && completed_ >= request && !state_.fatal && state_.pollSuccessful;
  }
  void shutdown() noexcept {
    if (!thread_.joinable()) { return; }
    // Disarm future holds; queue BOTH existing releases; await actual execution.
    // No assertion/stop/jthread destructor can skip this ordering.
    if (!hook::release_all_and_wait(3s)) { std::terminate(); }
    stop_.request_stop();
    { std::lock_guard<std::mutex> lock(mutex_); closing_ = true; changed_.notify_all(); }
    thread_.join(); // Underlying LT/OS join has no fabricated time guarantee.
  }
private:
  void run() noexcept {
    std::optional<storage::ProfileLease> lease;
    std::unique_ptr<engine::PersistentSession> owner;
    try {
      auto held = storage::ProfileLease::acquire(options_.profile);
      if (!held) { throw std::runtime_error("Generated causal lease rejected"); }
      lease.emplace(std::move(*held));
      // The private build/runtime qualification guard MUST accept this exact
      // isolated instrumented image. No caller trust bool/manifest bypass here.
      auto created = engine::PersistentSession::create({901, 902, 903});
      if (!created) { throw std::runtime_error("Causal LT runtime image is unqualified"); }
      owner = std::move(*created);
      { std::lock_guard<std::mutex> lock(mutex_); state_.ready = true; changed_.notify_all(); }
      const auto result = engine::continueLocal(*owner, *lease, options_);
      const auto receipt = owner->lastRetirement();
      std::array<std::int64_t, 4> settled{};
      if (receipt && receipt->peerCounters) { settled = counters(receipt->peerCounters->counters()); }
      hook::owner_returned(result.has_value(), receipt && receipt->storageReleased,
          receipt && receipt->peerCounters.has_value(), settled);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.returned = true; state_.successful = result.has_value();
        if (!result) { state_.failure = result.error().code; }
        state_.receipt = receipt; state_.latest = owner->latest(); changed_.notify_all();
      }
      for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return closing_ || completed_ < requested_; });
        if (closing_) { break; }
        lock.unlock();
        const auto polled = owner->poll(); const auto latest = owner->latest();
        lock.lock(); state_.latest = latest; state_.pollSuccessful = polled.has_value();
        ++completed_; changed_.notify_all();
      }
    } catch (...) {
      // owner/lease are OUTSIDE the try scope: catch releases callbacks/jobs
      // before their destructors can abort/join on an exceptional worker path.
      std::lock_guard<std::mutex> lock(mutex_); state_.fatal = true; changed_.notify_all();
    }
    if (!hook::release_all_and_wait(3s)) { std::terminate(); }
    if (owner) {
      const auto close = owner->close(std::chrono::steady_clock::now() + 5s);
      { std::lock_guard<std::mutex> lock(mutex_); state_.close = close; }
      owner.reset();
    }
    lease.reset();
    { std::lock_guard<std::mutex> lock(mutex_); state_.joined = true; changed_.notify_all(); }
  }
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  State state_{};
  bool closing_ = false;
  std::uint64_t requested_ = 0;
  std::uint64_t completed_ = 0;
  std::stop_source stop_;
  engine::LocalOptions options_;
  std::thread thread_; // All other fields exist before thread can enter run().
};
} // namespace biglybtc::test::causal
