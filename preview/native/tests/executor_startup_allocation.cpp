#include "executor_startup_allocation.hpp"

#include <atomic>
#include <utility>

namespace {
thread_local bool failNextAllocation = false;
std::atomic<int> injectedFailures{0};
std::size_t failedAllocationBytes = 0;
std::thread::id failedAllocationThread;
}

namespace biglybtc::tests::startup_allocation {
void arm() noexcept { failNextAllocation = true; }
void disarm() noexcept { failNextAllocation = false; }
Observation observation() noexcept {
  return {injectedFailures.load(), failedAllocationBytes, failedAllocationThread};
}
}

// GNU ld --wrap redirects only this test target's unresolved ordinary new
// calls. Keep the real allocator undefined in this TU, so the linker forwards
// successful allocations to the C++/sanitizer runtime rather than bypassing it.
// The qualified jthread stop-state allocation uses _Znwm on Linux x86_64.
extern "C" void* __real__Znwm(std::size_t bytes);
extern "C" void* __wrap__Znwm(std::size_t bytes) {
  if (std::exchange(failNextAllocation, false)) {
    failedAllocationBytes = bytes;
    failedAllocationThread = std::this_thread::get_id();
    ++injectedFailures;
    throw biglybtc::tests::startup_allocation::StartupAllocationFailure{};
  }
  return __real__Znwm(bytes);
}
