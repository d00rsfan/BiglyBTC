// SPDX-License-Identifier: GPL-2.0-only
// Derived behavior: RatingsUpdater, Copyright (C) 2004 Aelitis SARL.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::plugins::rating::publication {
namespace detail { struct State; }
enum class Error { Input, Limit, Memory, Expired, Closed, Foreign, AlreadyConsumed, Stale };
struct Limits {
  std::size_t downloads = 1024, publications = 1024, dispatches = 4096;
  std::size_t writeHistory = 1024, timerHistory = 1024;
};
// Owned scalar input batches, borrowed synchronously by a transition. No callbacks,
// global clock, RNG engine, implicit dispatch or retained provider reference.
class Inputs {
public:
  static constexpr std::size_t maximumScalars = 1024;
  std::expected<void, Error> setChoices(std::span<const std::int32_t>);
  std::expected<void, Error> setTimes(std::span<const std::int64_t>);
  std::size_t choicesConsumed() const noexcept { return choiceIndex_; }
  std::size_t timesConsumed() const noexcept { return timeIndex_; }
private:
  friend struct detail::State;
  friend class Policy;
  std::vector<std::int32_t> choices_{0};
  std::vector<std::int64_t> times_{1000};
  std::size_t choiceIndex_ = 0, timeIndex_ = 0;
};
class Download {
public:
  Download() = default;
  std::uint64_t id() const noexcept { return id_; }
private:
  friend struct detail::State;
  friend class Policy;
  std::weak_ptr<detail::State> owner_;
  std::uint64_t id_ = 0, epoch_ = 0;
};
enum class EventKind { Removed, RandomBound, Choice, Dispatch, Run, QueueSize, Log, Write, Complete, Clock, Timer, Fire };
struct Event {
  EventKind kind{};
  // id is the runnable/write/timer identifier selected by kind. Write.number is
  // the registered download ID, Timer.number the captured runnable ID, and
  // Complete.number is one. Labels and scalar intentions are owned by Receipt.
  std::uint64_t id = 0, number = 0;
  std::int64_t when = 0;
  bool flag = false, criticalRegion = false;
  std::array<char, 65> label{};
};
class Completion;
class Timer;
struct Receipt;
class Completion {
public:
  Completion() = default;
  std::uint64_t id() const noexcept { return id_; }
  std::expected<Receipt, Error> complete(Inputs&) const;
private:
  friend struct detail::State;
  friend class Policy;
  std::weak_ptr<detail::State> owner_;
  std::uint64_t id_ = 0, epoch_ = 0;
};
class Timer {
public:
  Timer() = default;
  std::uint64_t id() const noexcept { return id_; }
  std::expected<Receipt, Error> fire() const;
private:
  friend struct detail::State;
  friend class Policy;
  std::weak_ptr<detail::State> owner_;
  std::uint64_t id_ = 0, epoch_ = 0;
};
struct Receipt {
  static constexpr std::size_t capacity = 16;
  std::array<Event, capacity> events{};
  std::size_t size = 0;
  bool emptyDispatch = false;
  std::optional<Completion> write;
  std::optional<Timer> timer;
};
struct DownloadView { std::uint64_t id; std::string label; bool removed, cachedRating, cachedLookup; };
struct WriteView { std::uint64_t id, download, runnable; bool completed; };
struct TimerView { std::uint64_t id, runnable; std::int64_t when; bool fired; };
struct Snapshot {
  std::vector<DownloadView> downloads;
  std::vector<std::uint64_t> publications, dispatches;
  std::vector<WriteView> writes;
  std::vector<TimerView> timers;
  bool writeInProgress = false, sourceDestroyed = false, closed = true;
};
enum class CompletionMode { Deferred, Inline };
struct CloseReport { std::size_t publications = 0, dispatches = 0, writes = 0, timers = 0; };
class Policy {
public:
  static std::expected<Policy, Error> create(Limits = {});
  ~Policy();
  Policy(const Policy&) = delete;
  Policy& operator=(const Policy&) = delete;
  Policy(Policy&&) noexcept;
  Policy& operator=(Policy&&) noexcept;
  std::expected<Download, Error> registerDownload(std::string_view label, bool removed = false);
  std::expected<void, Error> setRemoved(const Download&, bool);
  std::expected<void, Error> seedCacheMembership(const Download&);
  std::expected<void, Error> setSourceDestroyed(bool);
  std::expected<Receipt, Error> enqueue(const Download&, bool priority, Inputs&);
  std::expected<Receipt, Error> remove(const Download&);
  std::expected<Receipt, Error> dispatchOne(Inputs&, CompletionMode = CompletionMode::Deferred);
  std::expected<Completion, Error> completion(std::uint64_t id) const;
  std::expected<Timer, Error> timer(std::uint64_t id) const;
  // Explicit same-owner APIs reject foreign handles; handles also safely deliver
  // directly while their original policy exists, including after a policy move.
  std::expected<Receipt, Error> complete(const Completion&, Inputs&);
  std::expected<Receipt, Error> fire(const Timer&);
  Snapshot snapshot() const;
  CloseReport close() noexcept;
private:
  explicit Policy(std::shared_ptr<detail::State>);
  std::shared_ptr<detail::State> state_;
};
}
