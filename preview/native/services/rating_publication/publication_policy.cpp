// SPDX-License-Identifier: GPL-2.0-only
#include "publication_policy.hpp"
#include <algorithm>
#include <bit>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::plugins::rating::publication {
namespace {
constexpr std::size_t maximumLimit = 8192;
constexpr std::int64_t delay = 10000;
bool labelValid(std::string_view s) {
  return !s.empty() && s.size() <= 64 && std::ranges::all_of(s, [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  });
}
template<class T> std::expected<void, Error> supply(std::vector<T>& target, std::size_t& index, std::span<const T> values) {
  if (values.empty() || values.size() > Inputs::maximumScalars) { return std::unexpected(Error::Limit); }
  try { std::vector<T> copy(values.begin(), values.end()); target.swap(copy); index = 0; }
  catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
  return {};
}
}
std::expected<void, Error> Inputs::setChoices(std::span<const std::int32_t> values) { return supply(choices_, choiceIndex_, values); }
std::expected<void, Error> Inputs::setTimes(std::span<const std::int64_t> values) { return supply(times_, timeIndex_, values); }

namespace detail {
struct State : std::enable_shared_from_this<State> {
  Limits limits;
  std::vector<DownloadView> downloads;
  std::vector<std::uint64_t> queue, dispatches;
  std::vector<WriteView> writes;
  std::vector<TimerView> timers;
  std::optional<std::uint64_t> active;
  std::uint64_t epoch = 1, nextRunnable = 0, nextWrite = 0, nextTimer = 0;
  bool destroyed = false, closed = false;
  explicit State(Limits l) : limits(l) {
    downloads.reserve(l.downloads); queue.reserve(l.publications); dispatches.reserve(l.dispatches);
    writes.reserve(l.writeHistory); timers.reserve(l.timerHistory);
  }
  std::size_t pendingTimers() const { return static_cast<std::size_t>(std::ranges::count(timers, false, &TimerView::fired)); }
  std::size_t controlLoad() const { return dispatches.size() + pendingTimers() + (active ? 1U : 0U); }
  std::expected<void, Error> ready() const { if (closed) { return std::unexpected(Error::Closed); } return {}; }
  std::expected<std::size_t, Error> validate(const Download& d) const {
    if (closed) { return std::unexpected(Error::Closed); }
    const auto owner = d.owner_.lock();
    if (!owner) { return std::unexpected(Error::Expired); }
    if (owner.get() != this) { return std::unexpected(Error::Foreign); }
    if (d.epoch_ != epoch || d.id_ == 0 || d.id_ > downloads.size()) { return std::unexpected(Error::Stale); }
    return static_cast<std::size_t>(d.id_ - 1);
  }
  void event(Receipt& receipt, EventKind kind, std::uint64_t id = 0, std::uint64_t number = 0,
             bool critical = false, const DownloadView* download = nullptr) const {
    static_assert(Receipt::capacity >= 8);
    if (receipt.size >= Receipt::capacity) { std::terminate(); }
    Event e; e.kind = kind; e.id = id; e.number = number; e.criticalRegion = critical;
    if (download) {
      std::ranges::copy(download->label, e.label.begin()); e.flag = download->removed;
    }
    receipt.events[receipt.size++] = e;
  }
  Completion completionHandle(std::uint64_t id) {
    Completion result; result.owner_ = weak_from_this(); result.id_ = id; result.epoch_ = epoch; return result;
  }
  Timer timerHandle(std::uint64_t id) {
    Timer result; result.owner_ = weak_from_this(); result.id_ = id; result.epoch_ = epoch; return result;
  }
  bool writeRoom() const { return writes.size() < limits.writeHistory || std::ranges::any_of(writes, &WriteView::completed); }
  void evictWrite() {
    if (writes.size() == limits.writeHistory) {
      writes.erase(std::ranges::find(writes, true, &WriteView::completed));
    }
  }
  void evictTimer() {
    if (timers.size() == limits.timerHistory) {
      timers.erase(std::ranges::find(timers, true, &TimerView::fired));
    }
  }
  void remove(std::uint64_t id) {
    auto& d = downloads[static_cast<std::size_t>(id - 1)];
    d.cachedRating = false;
    std::erase(queue, id);
    d.cachedLookup = false;
  }
  std::expected<Receipt, Error> complete(std::uint64_t id, std::uint64_t handleEpoch, Inputs& input) {
    if (closed) { return std::unexpected(Error::Closed); }
    if (epoch != handleEpoch) { return std::unexpected(Error::Stale); }
    auto write = std::ranges::find(writes, id, &WriteView::id);
    if (write == writes.end()) { return std::unexpected(Error::Stale); }
    if (write->completed) { return std::unexpected(Error::AlreadyConsumed); }
    if (!active || *active != id) { return std::unexpected(Error::Stale); }
    if (!queue.empty() && input.timeIndex_ >= input.times_.size()) { return std::unexpected(Error::Input); }
    // Every active write reserved one timer and its future dispatcher delivery.
    // Enqueue cannot consume those slots, so a valid completion never overloads.
    Receipt receipt;
    event(receipt, EventKind::Complete, id, 1);
    write->completed = true; active.reset();
    if (!queue.empty()) {
      event(receipt, EventKind::Clock, static_cast<std::uint64_t>(input.timeIndex_), 0, true);
      const auto now = input.times_[input.timeIndex_++];
      const auto when = std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(now) + static_cast<std::uint64_t>(delay));
      evictTimer();
      const auto timerId = ++nextTimer;
      timers.push_back({timerId, write->runnable, when, false});
      event(receipt, EventKind::Timer, timerId, write->runnable, true);
      receipt.events[receipt.size - 1].when = when;
      receipt.timer = timerHandle(timerId);
    }
    return receipt;
  }
  std::expected<Receipt, Error> fire(std::uint64_t id, std::uint64_t handleEpoch) {
    if (closed) { return std::unexpected(Error::Closed); }
    if (epoch != handleEpoch) { return std::unexpected(Error::Stale); }
    auto timer = std::ranges::find(timers, id, &TimerView::id);
    if (timer == timers.end()) { return std::unexpected(Error::Stale); }
    if (timer->fired) { return std::unexpected(Error::AlreadyConsumed); }
    Receipt receipt;
    event(receipt, EventKind::Fire, id);
    timer->fired = true;
    dispatches.push_back(timer->runnable); // consumes this pending timer's reservation
    event(receipt, EventKind::Dispatch, timer->runnable);
    return receipt;
  }
};
}

std::expected<Receipt, Error> Completion::complete(Inputs& input) const {
  const auto owner = owner_.lock();
  if (!owner) { return std::unexpected(Error::Expired); }
  return owner->complete(id_, epoch_, input);
}
std::expected<Receipt, Error> Timer::fire() const {
  const auto owner = owner_.lock();
  if (!owner) { return std::unexpected(Error::Expired); }
  return owner->fire(id_, epoch_);
}
Policy::Policy(std::shared_ptr<detail::State> s) : state_(std::move(s)) {}
std::expected<Policy, Error> Policy::create(Limits l) {
  for (auto value : {l.downloads, l.publications, l.dispatches, l.writeHistory, l.timerHistory}) {
    if (value == 0 || value > maximumLimit) { return std::unexpected(Error::Limit); }
  }
  try { return Policy(std::make_shared<detail::State>(l)); }
  catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
Policy::~Policy() { close(); }
Policy::Policy(Policy&& other) noexcept : state_(std::move(other.state_)) {}
Policy& Policy::operator=(Policy&& other) noexcept {
  if (this != &other) { close(); state_ = std::move(other.state_); } return *this;
}
std::expected<Download, Error> Policy::registerDownload(std::string_view label, bool removed) {
  if (!state_) { return std::unexpected(Error::Expired); }
  if (auto ok = state_->ready(); !ok) { return std::unexpected(ok.error()); }
  if (!labelValid(label)) { return std::unexpected(Error::Input); }
  if (std::ranges::any_of(state_->downloads, [&](const auto& d) { return d.label == label; })) { return std::unexpected(Error::Input); }
  if (state_->downloads.size() == state_->limits.downloads) { return std::unexpected(Error::Limit); }
  try {
    DownloadView record{state_->downloads.size() + 1, std::string(label), removed, false, false};
    state_->downloads.push_back(std::move(record));
  } catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
  Download d; d.owner_ = state_; d.epoch_ = state_->epoch; d.id_ = state_->downloads.back().id; return d;
}
std::expected<void, Error> Policy::setRemoved(const Download& d, bool removed) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto index = state_->validate(d); if (!index) { return std::unexpected(index.error()); }
  state_->downloads[*index].removed = removed; return {};
}
std::expected<void, Error> Policy::seedCacheMembership(const Download& d) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto index = state_->validate(d); if (!index) { return std::unexpected(index.error()); }
  state_->downloads[*index].cachedRating = state_->downloads[*index].cachedLookup = true; return {};
}
std::expected<void, Error> Policy::setSourceDestroyed(bool value) {
  if (!state_) { return std::unexpected(Error::Expired); }
  if (auto ok = state_->ready(); !ok) { return std::unexpected(ok.error()); }
  state_->destroyed = value; return {};
}
std::expected<Receipt, Error> Policy::enqueue(const Download& token, bool priority, Inputs& input) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto index = state_->validate(token); if (!index) { return std::unexpected(index.error()); }
  auto& s = *state_; const auto& d = s.downloads[*index];
  const auto found = std::ranges::find(s.queue, d.id);
  const auto afterRemoval = s.queue.size() - (found == s.queue.end() ? 0U : 1U);
  const bool needsChoice = !d.removed && !priority && afterRemoval != 0;
  if (!d.removed) {
    if (afterRemoval == s.limits.publications || s.controlLoad() == s.limits.dispatches ||
        s.nextRunnable == std::numeric_limits<std::uint64_t>::max()) { return std::unexpected(Error::Limit); }
    if (needsChoice && (input.choiceIndex_ >= input.choices_.size() || input.choices_[input.choiceIndex_] < 0 ||
        static_cast<std::size_t>(input.choices_[input.choiceIndex_]) >= afterRemoval)) { return std::unexpected(Error::Input); }
  }
  Receipt receipt; s.event(receipt, EventKind::Removed, d.id, 0, false, &d);
  if (d.removed) { s.remove(d.id); return receipt; }
  if (found != s.queue.end()) { s.queue.erase(found); }
  std::size_t position = 0;
  if (needsChoice) {
    s.event(receipt, EventKind::RandomBound, afterRemoval, 0, true);
    position = static_cast<std::size_t>(input.choices_[input.choiceIndex_++]);
    s.event(receipt, EventKind::Choice, position);
  }
  s.queue.insert(s.queue.begin() + static_cast<std::ptrdiff_t>(position), d.id);
  s.dispatches.push_back(++s.nextRunnable); s.event(receipt, EventKind::Dispatch, s.nextRunnable);
  return receipt;
}
std::expected<Receipt, Error> Policy::remove(const Download& d) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto index = state_->validate(d); if (!index) { return std::unexpected(index.error()); }
  state_->remove(state_->downloads[*index].id); return Receipt{};
}
std::expected<Receipt, Error> Policy::dispatchOne(Inputs& input, CompletionMode mode) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(); !ok) { return std::unexpected(ok.error()); }
  if (mode != CompletionMode::Deferred && mode != CompletionMode::Inline) { return std::unexpected(Error::Input); }
  Receipt receipt;
  if (s.dispatches.empty()) { receipt.emptyDispatch = true; return receipt; }
  const bool write = !s.destroyed && s.dispatches.size() == 1 && !s.active && !s.queue.empty();
  if (write) {
    if (!s.writeRoom() || s.pendingTimers() == s.limits.timerHistory ||
        s.nextWrite == std::numeric_limits<std::uint64_t>::max() || s.nextTimer == std::numeric_limits<std::uint64_t>::max()) {
      return std::unexpected(Error::Limit);
    }
    if (mode == CompletionMode::Inline && s.queue.size() > 1 && input.timeIndex_ >= input.times_.size()) { return std::unexpected(Error::Input); }
  }
  const auto runnable = s.dispatches.front(); s.dispatches.erase(s.dispatches.begin());
  s.event(receipt, EventKind::Run, runnable);
  if (s.destroyed) { return receipt; }
  s.event(receipt, EventKind::QueueSize, s.dispatches.size());
  if (!s.dispatches.empty() || s.active) { return receipt; }
  if (!write) { s.event(receipt, EventKind::Log, 0); return receipt; }
  const auto download = s.queue.front(); s.queue.erase(s.queue.begin());
  s.evictWrite(); const auto id = ++s.nextWrite;
  s.writes.push_back({id, download, runnable, false}); s.active = id;
  s.event(receipt, EventKind::Log, s.queue.size());
  s.event(receipt, EventKind::Write, id, download, false, &s.downloads[static_cast<std::size_t>(download - 1)]);
  receipt.write = s.completionHandle(id);
  if (mode == CompletionMode::Inline) {
    auto completed = s.complete(id, s.epoch, input);
    if (!completed) { std::terminate(); } // preflight guarantees scalar/control capacity
    for (std::size_t i = 0; i < completed->size; ++i) { receipt.events[receipt.size++] = completed->events[i]; }
    receipt.timer = completed->timer;
  }
  return receipt;
}
std::expected<Completion, Error> Policy::completion(std::uint64_t id) const {
  if (!state_) { return std::unexpected(Error::Expired); }
  if (auto ok = state_->ready(); !ok) { return std::unexpected(ok.error()); }
  if (std::ranges::find(state_->writes, id, &WriteView::id) == state_->writes.end()) { return std::unexpected(Error::Stale); }
  return state_->completionHandle(id);
}
std::expected<Timer, Error> Policy::timer(std::uint64_t id) const {
  if (!state_) { return std::unexpected(Error::Expired); }
  if (auto ok = state_->ready(); !ok) { return std::unexpected(ok.error()); }
  if (std::ranges::find(state_->timers, id, &TimerView::id) == state_->timers.end()) { return std::unexpected(Error::Stale); }
  return state_->timerHandle(id);
}
std::expected<Receipt, Error> Policy::complete(const Completion& h, Inputs& input) {
  if (!state_) { return std::unexpected(Error::Expired); }
  if (h.owner_.lock() != state_) { return std::unexpected(Error::Foreign); } return h.complete(input);
}
std::expected<Receipt, Error> Policy::fire(const Timer& h) {
  if (!state_) { return std::unexpected(Error::Expired); }
  if (h.owner_.lock() != state_) { return std::unexpected(Error::Foreign); } return h.fire();
}
std::expected<std::size_t, Error> Policy::insertionBound(const Download& download)const {
 if(!state_){return std::unexpected(Error::Expired);}auto& s=*state_;if(auto ready=s.ready();!ready){return std::unexpected(ready.error());}auto bound=s.validate(download);if(!bound){return std::unexpected(bound.error());}return s.queue.size()-static_cast<std::size_t>(std::ranges::count(s.queue,download.id()));
}
std::size_t Policy::pendingDispatches()const noexcept{return state_?state_->dispatches.size():0;}
Snapshot Policy::snapshot() const {
  if (!state_) { return {}; }
  const auto& s = *state_;
  return {s.downloads, s.queue, s.dispatches, s.writes, s.timers, s.active.has_value(), s.destroyed, s.closed};
}
CloseReport Policy::close() noexcept {
  if (!state_ || state_->closed) { return {}; }
  auto& s = *state_;
  CloseReport result{s.queue.size(), s.dispatches.size(), s.active ? 1U : 0U, s.pendingTimers()};
  s.closed = true; ++s.epoch; s.active.reset();
  s.queue.clear(); s.dispatches.clear(); s.writes.clear(); s.timers.clear(); s.downloads.clear(); return result;
}
}
