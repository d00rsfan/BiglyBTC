#pragma once

#include <cstddef>
#include <new>
#include <thread>

namespace biglybtc::tests::startup_allocation {
struct StartupAllocationFailure final : std::bad_alloc {};
struct Observation {
  int failures;
  std::size_t bytes;
  std::thread::id thread;
};

// This dedicated test executable arms only its initiating thread. These calls
// perform no allocation and must be used by that same serialized test caller.
void arm() noexcept;
void disarm() noexcept;
Observation observation() noexcept;
}
