#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110L
#error BiglyBTC requires the qualified C++23 move_only_function facility
#endif

namespace biglybtc::core {

enum class WorkError { Cancelled, Failed };
enum class SubmitError { NotStarted, Stopped, Overloaded, EmptyOperation };
using WorkResult = std::expected<void, WorkError>;
using Operation = std::move_only_function<WorkResult(std::stop_token)>;

class Ticket final {
public:
  Ticket(Ticket&&) noexcept = default;
  Ticket& operator=(Ticket&&) noexcept = default;
  Ticket(const Ticket&) = delete;
  Ticket& operator=(const Ticket&) = delete;
  void cancel() { cancellation_.request_stop(); }
  std::future<WorkResult>& result() { return result_; }
private:
  friend class Executor;
  Ticket(std::future<WorkResult> result, std::stop_source cancellation)
      : result_(std::move(result)), cancellation_(std::move(cancellation)) {}
  std::future<WorkResult> result_;
  std::stop_source cancellation_;
};

class Executor final {
public:
  explicit Executor(std::size_t queueCapacity);
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  ~Executor();
  bool start();
  std::expected<Ticket, SubmitError> submit(Operation operation);
  void requestShutdown();
  std::size_t pending() const;
private:
  struct Work {
    Operation operation;
    std::promise<WorkResult> completion;
    std::stop_source cancellation;
  };
  void run(std::stop_token stop);
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable_any changed_;
  std::deque<Work> queue_;
  std::optional<std::stop_source> active_;
  bool started_ = false;
  bool accepting_ = false;
  bool stopped_ = false;
  std::jthread worker_;
};

} // namespace biglybtc::core
