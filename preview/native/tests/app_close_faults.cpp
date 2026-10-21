#include "app_close_faults.hpp"
#include "biglybtc/executor.hpp"
#include <libtorrent/session.hpp>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {
std::mutex controlMutex;
// The pointer is used only under controlMutex; no pointer escapes to TLS.
struct Control {
  const std::thread::id controller = std::this_thread::get_id();
  bool rejectSubmit = false;
  bool failAllocationAfterJoin = false;
  std::size_t rejected = 0, joined = 0, failedAllocations = 0;
};
Control* active = nullptr;
thread_local bool failOneAllocation = false;
}
namespace biglybtc::test::app_close_fault {
struct Scope::Control { ::Control value; };
Scope::Scope() : control_(std::make_unique<Control>()) {
  const std::lock_guard lock(controlMutex);
  if (active) { throw std::logic_error("Overlapping app close fault scopes"); }
  active = &control_->value;
}
Scope::~Scope() {
  const std::lock_guard lock(controlMutex);
  if (active == &control_->value) { active = nullptr; }
}
void Scope::rejectNextSubmit() {
  const std::lock_guard lock(controlMutex); control_->value.rejectSubmit = true;
}
void Scope::failFirstAllocationAfterJoinedSession() {
  const std::lock_guard lock(controlMutex); control_->value.failAllocationAfterJoin = true;
}
std::size_t Scope::rejectedSubmissions() const {
  const std::lock_guard lock(controlMutex); return control_->value.rejected;
}
std::size_t Scope::joinedSessions() const {
  const std::lock_guard lock(controlMutex); return control_->value.joined;
}
std::size_t Scope::allocationFailures() const {
  const std::lock_guard lock(controlMutex); return control_->value.failedAllocations;
}
}

using SubmitResult = std::expected<biglybtc::core::Ticket,biglybtc::core::SubmitError>;
SubmitResult realSubmit(biglybtc::core::Executor*,biglybtc::core::Operation)
  asm("__real__ZN8biglybtc4core8Executor6submitESt18move_only_functionIJFSt8expectedIvNS0_9WorkErrorEESt10stop_tokenEEE");
SubmitResult wrapSubmit(biglybtc::core::Executor*,biglybtc::core::Operation)
  asm("__wrap__ZN8biglybtc4core8Executor6submitESt18move_only_functionIJFSt8expectedIvNS0_9WorkErrorEESt10stop_tokenEEE");
SubmitResult wrapSubmit(biglybtc::core::Executor* executor,biglybtc::core::Operation operation) {
  {
    const std::lock_guard lock(controlMutex);
    if (active && active->controller == std::this_thread::get_id() && active->rejectSubmit) {
      active->rejectSubmit = false; ++active->rejected;
      return std::unexpected(biglybtc::core::SubmitError::Overloaded);
    }
  }
  return realSubmit(executor,std::move(operation));
}

void realSessionDestructor(libtorrent::session*) noexcept asm("__real__ZN10libtorrent7sessionD1Ev");
void wrapSessionDestructor(libtorrent::session*) noexcept asm("__wrap__ZN10libtorrent7sessionD1Ev");
void wrapSessionDestructor(libtorrent::session* session) noexcept {
  realSessionDestructor(session); // Actual LT shutdown/join completes first.
  const std::lock_guard lock(controlMutex);
  if (active && active->controller != std::this_thread::get_id() && active->failAllocationAfterJoin) {
    active->failAllocationAfterJoin = false; ++active->joined;
    failOneAllocation = true;
  }
}
void* realNew(std::size_t) asm("__real__Znwm");
void* wrapNew(std::size_t) asm("__wrap__Znwm");
void* wrapNew(std::size_t size) {
  if (failOneAllocation) {
    failOneAllocation = false;
    {
      const std::lock_guard lock(controlMutex);
      if (active) { ++active->failedAllocations; }
    }
    throw std::bad_alloc{};
  }
  return realNew(size);
}
