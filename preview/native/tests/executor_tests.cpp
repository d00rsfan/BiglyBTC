#include "biglybtc/executor.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using namespace biglybtc::core;

namespace {
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

Ticket accepted(std::expected<Ticket, SubmitError> result) {
  require(result.has_value(), "operation was unexpectedly rejected");
  return std::move(*result);
}

WorkResult completed(Ticket& ticket) {
  require(ticket.result().wait_for(5s) == std::future_status::ready,
          "accepted operation did not resolve");
  return ticket.result().get();
}

struct Gate {
  std::mutex mutex;
  std::condition_variable_any changed;
  bool released = false;
  std::promise<void> entered;

  WorkResult wait(std::stop_token stop) {
    std::unique_lock lock(mutex);
    entered.set_value();
    changed.wait(lock, stop, [this] { return released; });
    return stop.stop_requested() ? WorkResult(std::unexpected(WorkError::Cancelled))
                                 : WorkResult{};
  }
  void awaitEntry() {
    require(entered.get_future().wait_for(5s) == std::future_status::ready,
            "worker did not enter operation");
  }
  void release() {
    {
      std::lock_guard lock(mutex);
      released = true;
    }
    changed.notify_all();
  }
};

void lifecycleAndCapacity() {
  bool invalid = false;
  try { Executor zero(0); } catch (const std::invalid_argument&) { invalid = true; }
  require(invalid, "zero capacity accepted");
  invalid = false;
  try { Executor enormous(4097); } catch (const std::invalid_argument&) { invalid = true; }
  require(invalid, "unbounded capacity accepted");

  Gate gate;
  Executor executor(2);
  auto before = executor.submit([](std::stop_token) -> WorkResult { return {}; });
  require(!before && before.error() == SubmitError::NotStarted, "implicit startup");
  require(executor.start() && !executor.start(), "startup is not exactly once");
  auto empty = executor.submit({});
  require(!empty && empty.error() == SubmitError::EmptyOperation, "empty operation accepted");
  auto active = accepted(executor.submit([&gate](std::stop_token stop) { return gate.wait(stop); }));
  gate.awaitEntry();
  std::vector<int> sequence;
  auto first = accepted(executor.submit([&](std::stop_token) -> WorkResult {
    sequence.push_back(1); return {};
  }));
  auto second = accepted(executor.submit([&](std::stop_token) -> WorkResult {
    sequence.push_back(2); return {};
  }));
  auto overload = executor.submit([](std::stop_token) -> WorkResult { return {}; });
  require(!overload && overload.error() == SubmitError::Overloaded,
          "queue capacity did not apply backpressure");
  require(executor.pending() == 2, "pending count includes active operation");
  gate.release();
  require(completed(active).has_value(), "active operation failed");
  require(completed(first).has_value() && completed(second).has_value(), "FIFO work failed");
  require(sequence == std::vector<int>({1, 2}), "FIFO order changed");
  executor.requestShutdown();
  auto stopped = executor.submit([](std::stop_token) -> WorkResult { return {}; });
  require(!stopped && stopped.error() == SubmitError::Stopped && !executor.start(),
          "stopped executor restarted");
  Executor neverStarted(1);
  neverStarted.requestShutdown();
  require(!neverStarted.start(), "shutdown before startup was ignored");
}

struct Owned {
  std::atomic<int>& destroyed;
  ~Owned() { ++destroyed; }
};

void cancellationAndOwnership() {
  std::atomic<int> destroyed{0};
  std::atomic<int> invoked{0};
  Gate gate;
  Executor executor(4);
  require(executor.start(), "startup failed");
  auto active = accepted(executor.submit([&gate](std::stop_token stop) { return gate.wait(stop); }));
  gate.awaitEntry();
  auto queued = accepted(executor.submit(
      [owned = std::make_unique<Owned>(destroyed), &invoked](std::stop_token) -> WorkResult {
        ++invoked; return {};
      }));
  queued.cancel();
  gate.release();
  require(completed(active).has_value(), "uncancelled active operation failed");
  auto cancelled = completed(queued);
  require(!cancelled && cancelled.error() == WorkError::Cancelled, "queued cancellation lost");
  require(invoked == 0 && destroyed == 1, "cancelled operation ran or retained its capture");

  auto owned = accepted(executor.submit(
      [value = std::make_unique<Owned>(destroyed)](std::stop_token) -> WorkResult {
        require(value != nullptr, "move-only capture lost"); return {};
      }));
  require(completed(owned).has_value() && destroyed == 2, "completion preceded capture cleanup");
  auto throwing = accepted(executor.submit([](std::stop_token) -> WorkResult {
    throw std::runtime_error("test failure");
  }));
  auto failed = completed(throwing);
  require(!failed && failed.error() == WorkError::Failed, "exception escaped worker");
  auto survivor = accepted(executor.submit([](std::stop_token) -> WorkResult { return {}; }));
  require(completed(survivor).has_value(), "worker did not survive operation exception");
}

void shutdownAndReentrantStop() {
  std::atomic<int> destroyed{0};
  std::atomic<bool> callbackRan{false};
  std::promise<void> callbackCompleted;
  auto callbackFinished = callbackCompleted.get_future();
  Gate gate;
  Executor executor(2);
  require(executor.start(), "startup failed");
  auto active = accepted(executor.submit([&](std::stop_token stop) -> WorkResult {
    std::stop_callback callback(stop, [&] {
      require(executor.pending() == 0, "shutdown left pending work");
      auto rejected = executor.submit([](std::stop_token) -> WorkResult { return {}; });
      require(!rejected && rejected.error() == SubmitError::Stopped,
              "stop callback could enqueue work");
      executor.requestShutdown();
      callbackRan = true;
      callbackCompleted.set_value();
    });
    const auto result = gate.wait(stop);
    // Keep the registration alive until invocation; the wait's later stop
    // callback can wake the worker before this callback has been reached.
    require(callbackFinished.wait_for(5s) == std::future_status::ready,
            "registered legacy stop callback did not complete");
    return result;
  }));
  gate.awaitEntry();
  auto queued = accepted(executor.submit(
      [value = std::make_unique<Owned>(destroyed)](std::stop_token) -> WorkResult { return {}; }));
  executor.requestShutdown();
  auto cancelled = completed(queued);
  require(!cancelled && cancelled.error() == WorkError::Cancelled && destroyed == 1,
          "shutdown did not resolve and release queued operation");
  cancelled = completed(active);
  require(!cancelled && cancelled.error() == WorkError::Cancelled && callbackRan,
          "active cancellation or reentrant stop failed");
  executor.requestShutdown();
}

void concurrentProducers() {
  constexpr std::size_t producers = 4;
  constexpr std::size_t count = 64;
  std::array<std::atomic<int>, producers * count> executions{};
  std::array<std::vector<Ticket>, producers> tickets;
  std::atomic<int> active{0};
  std::atomic<bool> concurrent{false};
  Executor executor(1024);
  require(executor.start(), "startup failed");
  {
    std::vector<std::jthread> threads;
    for (std::size_t p = 0; p < producers; ++p) {
      threads.emplace_back([&, p] {
        for (std::size_t i = 0; i < count; ++i) {
          const auto index = p * count + i;
          tickets[p].push_back(accepted(executor.submit([&, index](std::stop_token) -> WorkResult {
            if (active.fetch_add(1) != 0) { concurrent = true; }
            ++executions[index];
            std::this_thread::yield();
            --active;
            return {};
          })));
        }
      });
    }
  }
  for (auto& producer : tickets) {
    for (auto& ticket : producer) {
      require(completed(ticket).has_value(), "concurrent producer work failed");
    }
  }
  require(!concurrent && active == 0, "executor ran more than one operation at once");
  for (const auto& execution : executions) {
    require(execution == 1, "accepted operation did not execute exactly once");
  }
}
} // namespace

int main() {
  try {
    lifecycleAndCapacity();
    cancellationAndOwnership();
    shutdownAndReentrantStop();
    concurrentProducers();
    std::cout << "PASS: lifecycle, backpressure, FIFO, cancellation, ownership, reentrant stop, concurrency\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
