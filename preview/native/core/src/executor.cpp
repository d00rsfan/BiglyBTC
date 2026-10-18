#include "biglybtc/executor.hpp"

#include <stdexcept>
#include <utility>

namespace biglybtc::core {

struct Executor::ShutdownCleanup final {
  Executor& owner;
  bool registered = false;

  ~ShutdownCleanup() {
    if (!registered) {
      return;
    }
    {
      std::lock_guard lock(owner.mutex_);
      --owner.shutdownCleanups_;
    }
    owner.changed_.notify_all();
  }
};

Executor::Executor(std::size_t queueCapacity) : capacity_(queueCapacity) {
  if (capacity_ == 0 || capacity_ > 4096) {
    throw std::invalid_argument("Executor queue capacity must be between 1 and 4096");
  }
}

Executor::~Executor() {
  requestShutdown();
  // The host owns destruction; tasks cannot destroy their own executor.
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool Executor::start() {
  return start({});
}

bool Executor::start(Finalizer finalizer) {
  // A failed/rejected start releases its hook on this caller, outside the lock.
  Finalizer failedFinalizer;
  std::unique_lock lock(mutex_);
  if (started_ || stopped_) {
    return false;
  }
  finalizer_ = std::move(finalizer);
  try {
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
  } catch (...) {
    failedFinalizer = std::move(finalizer_);
    lock.unlock();
    throw;
  }
  // The new worker must acquire mutex_ before it can observe these flags.
  // Thread-construction failure therefore leaves startup uncommitted.
  started_ = true;
  accepting_ = true;
  return true;
}

std::expected<Ticket, SubmitError> Executor::submit(Operation operation) {
  if (!operation) {
    return std::unexpected(SubmitError::EmptyOperation);
  }
  std::unique_lock lock(mutex_);
  if (stopped_) {
    return std::unexpected(SubmitError::Stopped);
  }
  if (!started_) {
    return std::unexpected(SubmitError::NotStarted);
  }
  if (!accepting_) {
    return std::unexpected(SubmitError::Stopped);
  }
  if (queue_.size() == capacity_) {
    return std::unexpected(SubmitError::Overloaded);
  }
  Work work{std::move(operation), {}, {}};
  Ticket ticket(work.completion.get_future(), work.cancellation);
  queue_.push_back(std::move(work));
  lock.unlock();
  changed_.notify_one();
  return ticket;
}

std::size_t Executor::pending() const {
  std::lock_guard lock(mutex_);
  return queue_.size();
}

void Executor::requestShutdown() {
  // This guard outlives detached Work and stop-source copies, including during
  // unwinding. A worker must not finalize while another caller releases them.
  ShutdownCleanup cleanup{*this};
  std::deque<Work> cancelled;
  std::optional<std::stop_source> active;
  {
    std::lock_guard lock(mutex_);
    ++shutdownCleanups_;
    cleanup.registered = true;
    accepting_ = false;
    stopped_ = true;
    cancelled.swap(queue_);
    active = active_;
  }
  // User stop callbacks may call submit/requestShutdown/pending again.
  if (active) {
    active->request_stop();
  }
  for (auto& work : cancelled) {
    work.cancellation.request_stop();
    work.operation = {};
    work.completion.set_value(std::unexpected(WorkError::Cancelled));
  }
  worker_.request_stop();
  changed_.notify_all();
}

void Executor::run(std::stop_token stop) {
  while (true) {
    std::optional<Work> work;
    {
      std::unique_lock lock(mutex_);
      changed_.wait(lock, stop, [this] { return !queue_.empty() || !accepting_; });
      if (stop.stop_requested() || !accepting_) {
        // The worker stop token is already cancelled. This separate predicate
        // wait must not use it: queued captures/futures may still belong to a
        // shutdown caller, whose callbacks/destructors may reenter this object.
        changed_.wait(lock, [this] { return shutdownCleanups_ == 0; });
        break;
      }
      work.emplace(std::move(queue_.front()));
      queue_.pop_front();
      active_ = work->cancellation;
    }
    WorkResult result;
    if (work->cancellation.stop_requested()) {
      result = std::unexpected(WorkError::Cancelled);
    } else {
      try {
        result = work->operation(work->cancellation.get_token());
      } catch (...) {
        result = std::unexpected(WorkError::Failed);
      }
    }
    work->operation = {};
    {
      std::lock_guard lock(mutex_);
      active_.reset();
    }
    work->completion.set_value(std::move(result));
  }
  // No executor lock is held here. No accepted work or capture remains. The
  // hook is noncancellable; clearing it here keeps affine capture cleanup on
  // the worker as well, before host destruction finishes joining this thread.
  if (finalizer_) {
    finalizer_();
  }
  finalizer_ = {};
}

} // namespace biglybtc::core
