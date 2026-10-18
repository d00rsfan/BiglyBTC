#include "biglybtc/executor.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using namespace biglybtc::core;

namespace {
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

struct Signal {
  std::promise<void> promise;
  std::shared_future<void> future = promise.get_future().share();
  std::atomic<bool> raised{false};

  void set() noexcept {
    if (!raised.exchange(true)) {
      promise.set_value();
    }
  }
  void await(const char* message) const {
    require(future.wait_for(5s) == std::future_status::ready, message);
  }
  bool ready() const {
    return future.wait_for(0s) == std::future_status::ready;
  }
};

struct Door {
  std::mutex mutex;
  std::condition_variable_any changed;
  bool open = false;

  bool wait() {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, 5s, [this] { return open; });
  }
  void release() {
    {
      std::lock_guard lock(mutex);
      open = true;
    }
    changed.notify_all();
  }
};

struct ActiveGate {
  Door door;
  Signal entered;

  WorkResult wait(std::stop_token stop) {
    entered.set();
    std::unique_lock lock(door.mutex);
    if (!door.changed.wait_for(lock, stop, 5s, [this] { return door.open; })) {
      return std::unexpected(stop.stop_requested() ? WorkError::Cancelled : WorkError::Failed);
    }
    return stop.stop_requested() ? WorkResult{std::unexpected(WorkError::Cancelled)} : WorkResult{};
  }
};

struct Lifetime {
  std::atomic<int> destroyed{0};
  std::thread::id thread;
  Finalizer onRelease;
  Signal released;
};

struct Probe {
  explicit Probe(Lifetime& value) : state(value) {}
  Lifetime& state;

  ~Probe() noexcept {
    state.thread = std::this_thread::get_id();
    ++state.destroyed;
    if (state.onRelease) {
      state.onRelease();
    }
    state.released.set();
  }
};

Ticket accepted(std::expected<Ticket, SubmitError> value) {
  require(value.has_value(), "operation unexpectedly rejected");
  return std::move(*value);
}

WorkResult completed(Ticket& value) {
  require(value.result().wait_for(5s) == std::future_status::ready, "accepted future not resolved");
  return value.result().get();
}

bool allReady(std::vector<Ticket>& tickets) {
  for (auto& ticket : tickets) {
    if (ticket.result().wait_for(0s) != std::future_status::ready) {
      return false;
    }
  }
  return true;
}

bool stopped(Executor& executor) {
  auto result = executor.submit([](std::stop_token) -> WorkResult { return {}; });
  return !result && result.error() == SubmitError::Stopped;
}

void startModesAndRejection() {
  static_assert(std::is_constructible_v<Finalizer, decltype([]() noexcept {})>);
  static_assert(!std::is_constructible_v<Finalizer, decltype([] {})>);
  static_assert(!std::is_copy_constructible_v<Finalizer>);
  // Resolve the original overload explicitly: its out-of-line API is retained.
  bool (Executor::*legacyStart)() = &Executor::start;
  {
    Executor legacy(1);
    require((legacy.*legacyStart)(), "original startup overload unavailable");
    legacy.requestShutdown();
  }

  const auto caller = std::this_thread::get_id();
  std::atomic<int> calls{0}, rejectedCalls{0};
  std::atomic<bool> rejectedUnlocked{false};
  Lifetime hook, rejected, afterStop;
  {
    Executor executor(1);
    rejected.onRelease = [&]() noexcept {
      rejectedUnlocked = executor.pending() == 0;
      executor.requestShutdown();
    };
    require(executor.start([resource = std::make_unique<Probe>(hook), &calls]() noexcept {
      ++calls;
    }), "finalizer startup failed");
    require(!executor.start([resource = std::make_unique<Probe>(rejected), &rejectedCalls]() noexcept {
      ++rejectedCalls;
    }), "duplicate finalizer startup accepted");
    require(rejected.destroyed == 1 && rejected.thread == caller && rejectedUnlocked,
            "rejected hook capture not released unlocked on caller");
    executor.requestShutdown();
    executor.requestShutdown();
    hook.released.await("immediate shutdown did not release finalizer capture");
    require(calls == 1 && hook.destroyed == 1 && hook.thread != caller && rejectedCalls == 0,
            "immediate/rejected hook invocation or ownership changed");
    require(!executor.start(), "stopped executor restarted");
  }
  require(calls == 1, "destructor invoked finalizer again");
  {
    Executor neverStarted(1);
    neverStarted.requestShutdown();
    require(!neverStarted.start([resource = std::make_unique<Probe>(afterStop), &rejectedCalls]() noexcept {
      ++rejectedCalls;
    }), "hook accepted after shutdown-before-start");
    require(afterStop.destroyed == 1 && afterStop.thread == caller && rejectedCalls == 0,
            "never-started hook ran or retained captures");
  }
}

void fullQueueCancellationAndFinalizerReentry() {
  Lifetime activeCapture, queuedOne, queuedTwo, hook;
  ActiveGate activeGate;
  std::atomic<int> calls{0}, invokedQueued{0};
  std::atomic<bool> callbackRan{false}, callbackValid{true}, hookValid{false};
  Signal callbackCompleted;
  std::thread::id worker;
  std::vector<Ticket> tickets;
  tickets.reserve(3);
  const auto caller = std::this_thread::get_id();
  Executor executor(2);
  require(executor.start([&, resource = std::make_unique<Probe>(hook)]() noexcept {
    worker = std::this_thread::get_id();
    ++calls;
    hookValid = allReady(tickets) && activeCapture.destroyed == 1 && queuedOne.destroyed == 1 &&
                queuedTwo.destroyed == 1 && callbackRan && executor.pending() == 0 && stopped(executor);
    executor.requestShutdown();
    executor.requestShutdown();
    hookValid = hookValid && !executor.start();
  }), "finalizer startup failed");
  tickets.push_back(accepted(executor.submit(
      [&, resource = std::make_unique<Probe>(activeCapture)](std::stop_token stop) -> WorkResult {
        std::stop_callback callback(stop, [&] {
          callbackValid = executor.pending() == 0 && stopped(executor);
          executor.requestShutdown();
          callbackRan = true;
          callbackCompleted.set();
        });
        const auto result = activeGate.wait(stop);
        // The wait's later stop callback can wake this worker before the
        // requesting thread reaches our callback. Retain the registration until
        // it actually runs; scope exit could otherwise legally unregister it.
        callbackCompleted.await("registered stop callback did not complete");
        return result;
      })));
  activeGate.entered.await("active work did not enter");
  tickets.push_back(accepted(executor.submit(
      [resource = std::make_unique<Probe>(queuedOne), &invokedQueued](std::stop_token) -> WorkResult {
        ++invokedQueued; return {};
      })));
  tickets.push_back(accepted(executor.submit(
      [resource = std::make_unique<Probe>(queuedTwo), &invokedQueued](std::stop_token) -> WorkResult {
        ++invokedQueued; return {};
      })));
  tickets[1].cancel();
  auto overloaded = executor.submit([](std::stop_token) -> WorkResult { return {}; });
  require(!overloaded && overloaded.error() == SubmitError::Overloaded && executor.pending() == 2,
          "full queue lost its existing admission contract");
  executor.requestShutdown();
  hook.released.await("finalizer did not run after cancellation");
  for (auto& ticket : tickets) {
    const auto result = completed(ticket);
    require(!result && result.error() == WorkError::Cancelled, "shutdown result not cancelled");
  }
  require(calls == 1 && hookValid && callbackValid && invokedQueued == 0,
          "finalizer preceded accepted resolution/cleanup or reentry failed");
  require(activeCapture.thread == worker && hook.thread == worker && worker != caller,
          "active/finalizer capture cleanup left worker");
  require(queuedOne.thread == caller && queuedTwo.thread == caller,
          "queued cancellation capture ownership changed");
}

void completedWorkBeforeFinalizer() {
  Lifetime activeCapture, cancelledCapture, failedCapture, survivorCapture, hook;
  ActiveGate gate;
  std::atomic<int> cancelledInvocations{0}, hookCalls{0};
  std::atomic<bool> hookValid{false};
  std::vector<Ticket> tickets;
  tickets.reserve(4);
  Executor executor(3);
  require(executor.start([&, resource = std::make_unique<Probe>(hook)]() noexcept {
    ++hookCalls;
    hookValid = allReady(tickets) && activeCapture.destroyed == 1 && cancelledCapture.destroyed == 1 &&
                failedCapture.destroyed == 1 && survivorCapture.destroyed == 1;
  }), "normal-completion finalizer startup failed");
  tickets.push_back(accepted(executor.submit(
      [&, resource = std::make_unique<Probe>(activeCapture)](std::stop_token stop) { return gate.wait(stop); })));
  gate.entered.await("normal work did not enter");
  tickets.push_back(accepted(executor.submit(
      [&, resource = std::make_unique<Probe>(cancelledCapture)](std::stop_token) -> WorkResult {
        ++cancelledInvocations; return {};
      })));
  tickets.push_back(accepted(executor.submit(
      [resource = std::make_unique<Probe>(failedCapture)](std::stop_token) -> WorkResult {
        throw std::runtime_error("supplied operation failure");
      })));
  tickets.push_back(accepted(executor.submit(
      [resource = std::make_unique<Probe>(survivorCapture)](std::stop_token) -> WorkResult { return {}; })));
  tickets[1].cancel();
  gate.door.release();
  for (auto& ticket : tickets) {
    require(ticket.result().wait_for(5s) == std::future_status::ready, "normal/failed/cancelled future did not resolve");
  }
  executor.requestShutdown();
  hook.released.await("completed-work finalizer was retained");
  require(hookValid && hookCalls == 1 && cancelledInvocations == 0,
          "normal/failed/cancelled capture cleanup did not precede finalizer");
  require(completed(tickets[0]).has_value(), "normal work result changed");
  const auto cancelled = completed(tickets[1]);
  const auto failed = completed(tickets[2]);
  require(!cancelled && cancelled.error() == WorkError::Cancelled && !failed &&
          failed.error() == WorkError::Failed && completed(tickets[3]).has_value(),
          "normal worker cancellation/exception/survival contract changed");
  require(activeCapture.thread == hook.thread && cancelledCapture.thread == hook.thread &&
          failedCapture.thread == hook.thread && survivorCapture.thread == hook.thread,
          "normally drained captures did not release on the worker");
}

void blockedQueuedDestructorAndConcurrentShutdown() {
  Lifetime queuedCapture, hook;
  ActiveGate activeGate;
  Door releaseDestructor;
  Signal destructorEntered, shutdownReturned;
  std::atomic<bool> destructorReentry{false}, destructorFinished{false}, destructorReleased{false};
  std::atomic<bool> hookValid{false};
  std::atomic<int> hookCalls{0};
  std::vector<Ticket> tickets;
  tickets.reserve(2);
  Executor executor(1);
  queuedCapture.onRelease = [&]() noexcept {
    destructorReentry = executor.pending() == 0 && stopped(executor);
    executor.requestShutdown();
    destructorEntered.set();
    destructorReleased = releaseDestructor.wait();
    destructorFinished = true;
  };
  require(executor.start([&, resource = std::make_unique<Probe>(hook)]() noexcept {
    ++hookCalls;
    hookValid = allReady(tickets) && destructorFinished && queuedCapture.destroyed == 1;
  }), "finalizer startup failed");
  tickets.push_back(accepted(executor.submit([&](std::stop_token stop) { return activeGate.wait(stop); })));
  activeGate.entered.await("active work did not enter");
  tickets.push_back(accepted(executor.submit(
      [resource = std::make_unique<Probe>(queuedCapture)](std::stop_token) -> WorkResult { return {}; })));
  std::jthread shutdownCaller([&] {
    executor.requestShutdown();
    shutdownReturned.set();
  });
  destructorEntered.await("queued destructor was not entered");
  require(tickets[0].result().wait_for(5s) == std::future_status::ready,
          "worker did not finish active work while caller cleanup was blocked");
  require(tickets[1].result().wait_for(0s) != std::future_status::ready && hookCalls == 0,
          "queued future/finalizer advanced through blocked capture cleanup");

  // Other/reentrant shutdown calls must not wait for the first caller's cleanup.
  std::array<Signal, 3> returned;
  {
    std::vector<std::jthread> concurrent;
    for (std::size_t i = 0; i < returned.size(); ++i) {
      concurrent.emplace_back([&, i] {
        executor.requestShutdown();
        executor.requestShutdown();
        returned[i].set();
      });
    }
    for (const auto& signal : returned) {
      signal.await("repeated concurrent shutdown waited on another caller");
    }
  }
  require(!shutdownReturned.ready() && !destructorFinished && hookCalls == 0,
          "blocked cleanup was not retained across concurrent shutdown");
  releaseDestructor.release();
  shutdownReturned.await("shutdown caller did not complete");
  hook.released.await("worker did not finalize after cleanup barrier opened");
  require(destructorReleased && destructorReentry && hookCalls == 1 && hookValid,
          "cleanup barrier, callback reentry or accepted future ordering failed");
  for (auto& ticket : tickets) {
    const auto result = completed(ticket);
    require(!result && result.error() == WorkError::Cancelled, "cancelled result lost");
  }
}

void shutdownFromWorker() {
  Lifetime activeCapture, queuedCapture, hook;
  ActiveGate activeGate;
  std::atomic<bool> destructorValid{false}, callbackValid{false}, hookValid{false};
  std::atomic<int> hookCalls{0}, queuedInvocations{0};
  std::thread::id worker;
  std::vector<Ticket> tickets;
  tickets.reserve(2);
  Executor executor(1);
  queuedCapture.onRelease = [&]() noexcept {
    destructorValid = executor.pending() == 0 && stopped(executor);
    executor.requestShutdown();
  };
  require(executor.start([&, resource = std::make_unique<Probe>(hook)]() noexcept {
    ++hookCalls;
    hookValid = allReady(tickets) && destructorValid && callbackValid && activeCapture.destroyed == 1;
    executor.requestShutdown();
  }), "finalizer startup failed");
  tickets.push_back(accepted(executor.submit(
      [&, resource = std::make_unique<Probe>(activeCapture)](std::stop_token stop) -> WorkResult {
        worker = std::this_thread::get_id();
        std::stop_callback callback(stop, [&] {
          callbackValid = executor.pending() == 0 && stopped(executor);
          executor.requestShutdown();
        });
        const auto released = activeGate.wait(stop);
        if (!released) {
          return released;
        }
        executor.requestShutdown();
        // Existing semantics retain the operation's result after it requests
        // its own cancellation; cancellation does not overwrite a returned OK.
        return {};
      })));
  activeGate.entered.await("worker shutdown operation did not enter");
  tickets.push_back(accepted(executor.submit(
      [&, resource = std::make_unique<Probe>(queuedCapture)](std::stop_token) -> WorkResult {
        ++queuedInvocations; return {};
      })));
  activeGate.door.release();
  hook.released.await("worker-initiated shutdown failed to finalize");
  require(completed(tickets[0]).has_value(), "worker's explicit success result changed");
  const auto cancelled = completed(tickets[1]);
  require(!cancelled && cancelled.error() == WorkError::Cancelled && queuedInvocations == 0,
          "worker shutdown did not cancel queued work");
  require(hookCalls == 1 && hookValid && activeCapture.thread == worker && queuedCapture.thread == worker &&
          hook.thread == worker, "worker shutdown finalizer/order/capture ownership failed");
}

void finalizerCaptureCleanupPrecedesJoin() {
  Lifetime hook;
  Door releaseCapture;
  Signal captureEntered, hostReturned;
  std::atomic<int> calls{0};
  std::atomic<bool> hookReturned{false}, captureValid{false}, captureReleased{false};
  std::thread::id worker;
  auto executor = std::make_unique<Executor>(1);
  auto* borrowed = executor.get();
  hook.onRelease = [&]() noexcept {
    captureValid = hookReturned && borrowed->pending() == 0 && stopped(*borrowed) &&
                   worker == std::this_thread::get_id();
    borrowed->requestShutdown();
    captureEntered.set();
    captureReleased = releaseCapture.wait();
  };
  require(executor->start([&, resource = std::make_unique<Probe>(hook)]() noexcept {
    worker = std::this_thread::get_id();
    ++calls;
    hookReturned = true;
  }), "finalizer startup failed");
  std::jthread host([owner = std::move(executor), &hostReturned]() mutable {
    owner.reset();
    hostReturned.set();
  });
  captureEntered.await("finalizer capture cleanup did not begin");
  require(calls == 1 && captureValid && !hostReturned.ready(),
          "host join preceded worker-owned finalizer capture cleanup");
  releaseCapture.release();
  hostReturned.await("host did not join after finalizer capture release");
  hook.released.await("finalizer capture was retained");
  require(captureReleased && hook.destroyed == 1 && hook.thread == worker,
          "finalizer capture did not release exactly once on worker");
}
} // namespace

int main() {
  try {
    startModesAndRejection();
    fullQueueCancellationAndFinalizerReentry();
    completedWorkBeforeFinalizer();
    blockedQueuedDestructorAndConcurrentShutdown();
    shutdownFromWorker();
    finalizerCaptureCleanupPrecedesJoin();
    std::cout << "PASS: owner-thread finalizer, accepted resolution/cleanup barrier, full queue, reentry, concurrent shutdown, affine captures\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
