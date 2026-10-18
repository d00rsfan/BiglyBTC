#include "biglybtc/executor.hpp"
#include "executor_startup_allocation.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std::chrono_literals;
using namespace biglybtc::core;
namespace startup = biglybtc::tests::startup_allocation;

namespace {
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

struct Probe {
  Executor& executor;
  std::atomic<int>& destroyed;
  std::atomic<bool>& unlocked;
  std::thread::id& destroyedOn;

  ~Probe() noexcept {
    destroyedOn = std::this_thread::get_id();
    auto result = executor.submit([](std::stop_token) -> WorkResult { return {}; });
    unlocked = executor.pending() == 0 && !result && result.error() == SubmitError::NotStarted;
    ++destroyed;
  }
};
}

int main() {
  try {
    const auto caller = std::this_thread::get_id();
    std::atomic<int> calls{0}, destroyed{0};
    std::atomic<bool> unlocked{false};
    std::thread::id destroyedOn;
    // These observed resources must outlive Executor's destructor/join, even
    // after the hook has made a promise ready but has not returned yet.
    std::promise<void> finalized;
    auto done = finalized.get_future();
    Executor executor(1);
    Finalizer hook = [resource = std::make_unique<Probe>(executor, destroyed, unlocked, destroyedOn),
                      &calls]() noexcept {
      ++calls;
    };
    bool failed = false;
    startup::arm();
    try {
      executor.start(std::move(hook));
    } catch (const startup::StartupAllocationFailure&) {
      failed = true;
    }
    startup::disarm();
    const auto observed = startup::observation();
    require(failed && observed.failures == 1 && observed.bytes != 0 &&
            observed.thread == caller && !hook,
            "intended initiating-thread startup allocation failure/ownership transfer not observed");
    require(calls == 0 && destroyed == 1 && destroyedOn == caller && unlocked,
            "failed start ran its hook or released its capture under lock/on worker");
    auto notStarted = executor.submit([](std::stop_token) -> WorkResult { return {}; });
    require(!notStarted && notStarted.error() == SubmitError::NotStarted,
            "failed startup committed accepting/started state");
    require(executor.start([&]() noexcept { ++calls; finalized.set_value(); }),
            "failed startup could not be retried");
    auto accepted = executor.submit([](std::stop_token) -> WorkResult { return {}; });
    require(accepted.has_value(), "retry did not accept work");
    require(accepted->result().wait_for(5s) == std::future_status::ready &&
            accepted->result().get().has_value(), "retry operation failed");
    executor.requestShutdown();
    require(done.wait_for(5s) == std::future_status::ready && calls == 1,
            "retry hook did not run exactly once");
    std::cout << "PASS: deterministic jthread stop-state allocation failure (" << observed.bytes
              << " bytes), uncommitted state, unlocked rejected capture cleanup, retry finalizer exactly once\n";
  } catch (const std::exception& error) {
    startup::disarm();
    std::cerr << error.what() << '\n';
    return 1;
  }
}
