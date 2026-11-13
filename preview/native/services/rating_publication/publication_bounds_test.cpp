// SPDX-License-Identifier: GPL-2.0-only
#include "publication_policy.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace p = biglybtc::plugins::rating::publication;
namespace {
std::size_t assertions = 0;
void check(bool value, const char* expression, int line) {
  ++assertions;
  if (!value) { std::cerr << "line " << line << ": " << expression << '\n'; std::exit(1); }
}
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
template<class T> T take(std::expected<T, p::Error> value) {
  CHECK(value); return std::move(*value);
}
void ok(std::expected<void, p::Error> value) { CHECK(value); }
template<class T> bool error(const std::expected<T, p::Error>& value, p::Error wanted) {
  return !value && value.error() == wanted;
}
bool kinds(const p::Receipt& r, std::initializer_list<p::EventKind> expected) {
  if (r.size != expected.size()) { return false; }
  std::size_t i = 0;
  for (auto kind : expected) { if (r.events[i++].kind != kind) { return false; } }
  return true;
}
p::Receipt startLast(p::Policy& policy, p::Inputs& input) {
  while (policy.snapshot().dispatches.size() > 1) {
    auto r = take(policy.dispatchOne(input)); CHECK(!r.write); CHECK(r.size == 2);
  }
  return take(policy.dispatchOne(input));
}
void ownershipAndMoves() {
  static_assert(!std::is_copy_constructible_v<p::Policy>);
  static_assert(std::is_nothrow_move_constructible_v<p::Policy>);
  static_assert(std::is_nothrow_move_assignable_v<p::Policy>);
  auto policy = take(p::Policy::create()); p::Inputs input;
  std::string label = "OWNED"; auto d = take(policy.registerDownload(label)); label.assign("OTHER");
  auto receipt = take(policy.enqueue(d, true, input));
  auto snapshot = policy.snapshot(); CHECK(snapshot.downloads[0].label == "OWNED");
  CHECK(std::string(receipt.events[0].label.data()) == "OWNED");
  auto started = startLast(policy, input); CHECK(started.write);
  const auto handle = *started.write;
  auto moved = std::move(policy);
  CHECK(error(policy.enqueue(d, true, input), p::Error::Expired));
  CHECK(moved.snapshot().writeInProgress); CHECK(handle.complete(input));
  CHECK(error(handle.complete(input), p::Error::AlreadyConsumed));
  auto& alias = moved; moved = std::move(alias);
  CHECK(moved.snapshot().writes.size() == 1);
  auto target = take(p::Policy::create()); auto old = take(target.registerDownload("OLD"));
  take(target.enqueue(old, true, input)); auto abandoned = *startLast(target, input).write;
  target = std::move(moved);
  CHECK(error(abandoned.complete(input), p::Error::Expired));
  CHECK(target.snapshot().downloads[0].label == "OWNED");
  const auto closed = target.close(); CHECK(closed.writes == 0); CHECK(target.snapshot().closed);
  CHECK(error(handle.complete(input), p::Error::Closed));
  CHECK(error(target.enqueue(d, true, input), p::Error::Closed));
  CHECK(target.close().dispatches == 0);
  CHECK(snapshot.downloads[0].label == "OWNED");
  CHECK(std::string(receipt.events[0].label.data()) == "OWNED");
  p::Completion expired; p::Timer expiredTimer; p::Receipt ownedReceipt;
  {
    auto temporary = take(p::Policy::create()); auto a = take(temporary.registerDownload("A"));
    auto b = take(temporary.registerDownload("B"));
    ownedReceipt = take(temporary.enqueue(a, true, input)); take(temporary.enqueue(b, true, input));
    expired = *startLast(temporary, input).write;
    auto finished = take(expired.complete(input)); CHECK(finished.timer); expiredTimer = *finished.timer;
  }
  CHECK(error(expired.complete(input), p::Error::Expired));
  CHECK(error(expiredTimer.fire(), p::Error::Expired));
  CHECK(std::string(ownedReceipt.events[0].label.data()) == "A");
}
void foreignAndOnceOnly() {
  auto first = take(p::Policy::create()), second = take(p::Policy::create()); p::Inputs input;
  auto a = take(first.registerDownload("A")); auto b = take(first.registerDownload("B"));
  CHECK(error(second.enqueue(a, true, input), p::Error::Foreign));
  CHECK(error(first.enqueue(p::Download{}, true, input), p::Error::Expired));
  take(first.enqueue(a, true, input)); take(first.enqueue(b, true, input));
  const auto h1 = *startLast(first, input).write;
  CHECK(error(second.complete(h1, input), p::Error::Foreign));
  auto completed = take(h1.complete(input)); CHECK(completed.timer); const auto timer = *completed.timer;
  CHECK(error(second.fire(timer), p::Error::Foreign));
  take(first.enqueue(b, true, input)); auto h2 = *startLast(first, input).write;
  CHECK(first.snapshot().writeInProgress);
  CHECK(error(h1.complete(input), p::Error::AlreadyConsumed));
  CHECK(first.snapshot().writeInProgress); CHECK(first.snapshot().writes.back().id == h2.id());
  auto fired = take(timer.fire()); CHECK(kinds(fired, {p::EventKind::Fire, p::EventKind::Dispatch}));
  CHECK(error(timer.fire(), p::Error::AlreadyConsumed)); CHECK(first.snapshot().dispatches.size() == 1);
  auto skipped = take(first.dispatchOne(input)); CHECK(!skipped.write); CHECK(first.snapshot().writeInProgress);
  const std::array<std::int64_t, 1> now{2000}; ok(input.setTimes(now)); take(h2.complete(input));
  CHECK(!first.snapshot().writeInProgress);
  CHECK(error(p::Completion{}.complete(input), p::Error::Expired)); CHECK(error(p::Timer{}.fire(), p::Error::Expired));
}
void orderingAndRandomBounds() {
  auto policy = take(p::Policy::create()); p::Inputs input;
  auto a = take(policy.registerDownload("A")), b = take(policy.registerDownload("B")), c = take(policy.registerDownload("C"));
  take(policy.enqueue(a, true, input)); take(policy.enqueue(b, true, input)); take(policy.enqueue(c, true, input));
  std::array<std::int32_t, 1> choice{1}; ok(input.setChoices(choice)); choice[0] = 0;
  auto r = take(policy.enqueue(a, false, input));
  CHECK(kinds(r, {p::EventKind::Removed, p::EventKind::RandomBound, p::EventKind::Choice, p::EventKind::Dispatch}));
  CHECK(r.events[1].id == 2); CHECK(r.events[1].criticalRegion); CHECK(!r.events[0].criticalRegion);
  CHECK(r.events[2].id == 1); CHECK(input.choicesConsumed() == 1);
  CHECK(policy.snapshot().publications == std::vector<std::uint64_t>{c.id(), a.id(), b.id()});
  auto failed = policy.enqueue(b, false, input); CHECK(error(failed, p::Error::Input));
  CHECK(policy.snapshot().publications == std::vector<std::uint64_t>{c.id(), a.id(), b.id()});
  const std::array<std::int32_t, 1> negative{-1}; ok(input.setChoices(negative));
  CHECK(error(policy.enqueue(b, false, input), p::Error::Input)); CHECK(input.choicesConsumed() == 0);
  take(policy.enqueue(b, true, input)); CHECK(input.choicesConsumed() == 0);
  auto started = startLast(policy, input); CHECK(started.write);
  CHECK(kinds(started, {p::EventKind::Run, p::EventKind::QueueSize, p::EventKind::Log, p::EventKind::Write}));
  CHECK(started.events[2].id == 2); CHECK(std::string(started.events[3].label.data()) == "B");
  CHECK(started.events[3].number == b.id());
  auto completed = take(started.write->complete(input));
  CHECK(kinds(completed, {p::EventKind::Complete, p::EventKind::Clock, p::EventKind::Timer}));
  CHECK(completed.events[1].criticalRegion); CHECK(completed.events[2].criticalRegion);
  CHECK(completed.events[2].number == started.events[0].id);
  auto c2 = take(policy.registerDownload("D")); take(policy.enqueue(c2, true, input));
  // A pending delay does not prevent a newly posted runnable from starting a write.
  const auto next = startLast(policy, input); CHECK(next.write); CHECK(policy.snapshot().timers.size() == 1);
  const auto before = policy.snapshot();
  CHECK(error(next.write->complete(input), p::Error::Input));
  CHECK(policy.snapshot().writeInProgress); CHECK(policy.snapshot().timers.size() == before.timers.size());
  const std::array<std::int64_t, 1> late{9000}; ok(input.setTimes(late));
  take(next.write->complete(input)); CHECK(policy.snapshot().timers.back().when == 19000);
}
void reservedControlCapacity() {
  auto policy = take(p::Policy::create({4, 3, 2, 2, 2})); p::Inputs input;
  auto a = take(policy.registerDownload("A")), b = take(policy.registerDownload("B")), c = take(policy.registerDownload("C"));
  take(policy.enqueue(a, true, input)); take(policy.enqueue(b, true, input));
  auto h = *startLast(policy, input).write;
  take(policy.enqueue(c, true, input)); // active completion and this runnable fill control capacity
  const auto before = policy.snapshot();
  CHECK(error(policy.enqueue(a, false, input), p::Error::Limit)); CHECK(input.choicesConsumed() == 0);
  CHECK(policy.snapshot().publications == before.publications); CHECK(policy.snapshot().dispatches == before.dispatches);
  auto done = take(h.complete(input)); CHECK(done.timer); CHECK(!policy.snapshot().writeInProgress);
  auto fired = take(done.timer->fire()); CHECK(fired.events[1].id == before.writes.back().runnable);
  CHECK(policy.snapshot().dispatches.size() == 2);
  CHECK(error(policy.enqueue(b, true, input), p::Error::Limit));
  auto next = startLast(policy, input); CHECK(next.write); CHECK(std::string(next.events[3].label.data()) == "C");
  // Removal and close remain usable at full capacity and never need provider input.
  take(policy.enqueue(b, true, input));
  ok(policy.seedCacheMembership(a)); ok(policy.setRemoved(a, true));
  const auto removed = take(policy.enqueue(a, false, input)); CHECK(removed.size == 1); CHECK(removed.events[0].flag);
  CHECK(!policy.snapshot().downloads[0].cachedRating); CHECK(!policy.snapshot().downloads[0].cachedLookup);
  const auto report = policy.close(); CHECK(report.writes == 1); CHECK(report.dispatches == 1); CHECK(policy.snapshot().downloads.empty());
  CHECK(error(next.write->complete(input), p::Error::Closed));
}
void boundedHistoryAndTimerProgress() {
  auto policy = take(p::Policy::create({4, 4, 3, 1, 1})); p::Inputs input;
  auto a = take(policy.registerDownload("A")), b = take(policy.registerDownload("B")), c = take(policy.registerDownload("C"));
  take(policy.enqueue(a, true, input)); take(policy.enqueue(b, true, input));
  const auto first = *startLast(policy, input).write; const auto timer = *take(first.complete(input)).timer;
  take(policy.enqueue(c, true, input));
  const auto before = policy.snapshot(); CHECK(error(policy.dispatchOne(input), p::Error::Limit));
  CHECK(policy.snapshot().dispatches == before.dispatches); CHECK(policy.snapshot().publications == before.publications);
  take(timer.fire()); auto second = *startLast(policy, input).write;
  CHECK(error(first.complete(input), p::Error::Stale)); CHECK(policy.snapshot().writeInProgress);
  const std::array<std::int64_t, 1> time{2000}; ok(input.setTimes(time));
  auto done = take(second.complete(input)); CHECK(done.timer); CHECK(done.timer->id() == 2);
  CHECK(policy.snapshot().writes.size() == 1); CHECK(policy.snapshot().timers.size() == 1);
  CHECK(error(timer.fire(), p::Error::Stale)); CHECK(!policy.snapshot().timers[0].fired);
  take(done.timer->fire()); auto third = *startLast(policy, input).write;
  CHECK(error(second.complete(input), p::Error::Stale)); take(third.complete(input));
  CHECK(policy.snapshot().publications.empty()); CHECK(!policy.snapshot().writeInProgress);
}
void removalStopAndInline() {
  auto policy = take(p::Policy::create()); p::Inputs input;
  auto a = take(policy.registerDownload("A")), b = take(policy.registerDownload("B"));
  ok(policy.seedCacheMembership(a)); take(policy.enqueue(a, true, input)); take(policy.enqueue(b, true, input));
  auto started = startLast(policy, input); CHECK(started.write);
  ok(policy.setSourceDestroyed(true)); const auto completed = take(started.write->complete(input)); CHECK(completed.timer);
  take(completed.timer->fire()); const auto skipped = take(policy.dispatchOne(input));
  CHECK(kinds(skipped, {p::EventKind::Run})); CHECK(policy.snapshot().publications == std::vector<std::uint64_t>{a.id()});
  const auto removed = take(policy.remove(a)); CHECK(removed.size == 0);
  CHECK(!policy.snapshot().downloads[0].cachedRating); CHECK(!policy.snapshot().downloads[0].cachedLookup);
  CHECK(!policy.snapshot().downloads[0].removed); // removal notification does not invent a host flag
  ok(policy.setSourceDestroyed(false)); take(policy.enqueue(a, true, input));
  auto inlineDone = take(policy.dispatchOne(input, p::CompletionMode::Inline));
  CHECK(kinds(inlineDone, {p::EventKind::Run, p::EventKind::QueueSize, p::EventKind::Log, p::EventKind::Write, p::EventKind::Complete}));
  CHECK(inlineDone.write); CHECK(!policy.snapshot().writeInProgress);
  CHECK(error(inlineDone.write->complete(input), p::Error::AlreadyConsumed));
  CHECK(take(policy.dispatchOne(input)).emptyDispatch);
  auto other = take(p::Policy::create()); p::Inputs otherInput;
  const auto x = take(other.registerDownload("X")), y = take(other.registerDownload("Y"));
  take(other.enqueue(x, true, otherInput)); take(other.enqueue(y, true, otherInput));
  auto inlineTimer = startLast(other, otherInput); // deferred first, explicitly complete after source removal
  take(other.remove(x)); take(inlineTimer.write->complete(otherInput)); CHECK(other.snapshot().timers.empty());
}
void scalarAndConfigurationBounds() {
  for (std::size_t bad : {std::size_t{0}, std::size_t{8193}}) {
    CHECK(error(p::Policy::create({bad, 1, 1, 1, 1}), p::Error::Limit));
    CHECK(error(p::Policy::create({1, bad, 1, 1, 1}), p::Error::Limit));
    CHECK(error(p::Policy::create({1, 1, bad, 1, 1}), p::Error::Limit));
    CHECK(error(p::Policy::create({1, 1, 1, bad, 1}), p::Error::Limit));
    CHECK(error(p::Policy::create({1, 1, 1, 1, bad}), p::Error::Limit));
  }
  auto policy = take(p::Policy::create({1, 1, 1, 1, 1})); p::Inputs input;
  CHECK(error(policy.registerDownload(""), p::Error::Input)); CHECK(error(policy.registerDownload("lower"), p::Error::Input));
  CHECK(error(policy.registerDownload(std::string(65, 'A')), p::Error::Input));
  auto a = take(policy.registerDownload("A")); CHECK(error(policy.registerDownload("A"), p::Error::Input));
  CHECK(error(policy.registerDownload("B"), p::Error::Limit));
  const std::array<std::int32_t, 1> original{0}; ok(input.setChoices(original));
  CHECK(error(input.setChoices({}), p::Error::Limit));
  const std::vector<std::int32_t> excessive(p::Inputs::maximumScalars + 1, 0);
  CHECK(error(input.setChoices(excessive), p::Error::Limit));
  std::array<std::int64_t, 1> time{500}; ok(input.setTimes(time)); time[0] = 900;
  CHECK(error(input.setTimes({}), p::Error::Limit));
  take(policy.enqueue(a, true, input)); CHECK(error(policy.enqueue(a, true, input), p::Error::Limit));
  auto start = take(policy.dispatchOne(input, p::CompletionMode::Inline)); CHECK(start.write); CHECK(start.size == 5);
  CHECK(!policy.snapshot().writeInProgress);
  CHECK(error(policy.dispatchOne(input, static_cast<p::CompletionMode>(999)), p::Error::Input));
  for (const auto now : {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1}, std::int64_t{0}, std::numeric_limits<std::int64_t>::max()}) {
    auto clocks = take(p::Policy::create()); p::Inputs script;
    auto x = take(clocks.registerDownload("X")), y = take(clocks.registerDownload("Y"));
    std::array<std::int64_t, 1> supplied{now}; ok(script.setTimes(supplied)); supplied[0] = 123;
    take(clocks.enqueue(x, true, script)); take(clocks.enqueue(y, true, script));
    auto done = take(startLast(clocks, script).write->complete(script)); CHECK(done.timer);
    const auto expected = now == std::numeric_limits<std::int64_t>::max() ? std::numeric_limits<std::int64_t>::min() + 9999 : now + 10000;
    CHECK(clocks.snapshot().timers[0].when == expected); CHECK(script.timesConsumed() == 1);
  }
}
void pendingCloseAndInlinePreflight() {
  {
    auto policy = take(p::Policy::create()); p::Inputs input;
    const auto a = take(policy.registerDownload("A")), b = take(policy.registerDownload("B")), c = take(policy.registerDownload("C"));
    take(policy.enqueue(a, true, input)); take(policy.enqueue(b, true, input));
    const auto old = *startLast(policy, input).write;
    const auto timer = *take(old.complete(input)).timer;
    take(policy.enqueue(c, true, input)); const auto active = *startLast(policy, input).write;
    const auto closed = policy.close();
    CHECK(closed.publications == 1); CHECK(closed.dispatches == 0); CHECK(closed.writes == 1); CHECK(closed.timers == 1);
    CHECK(error(timer.fire(), p::Error::Closed)); CHECK(error(active.complete(input), p::Error::Closed));
    CHECK(policy.snapshot().timers.empty()); CHECK(policy.snapshot().writes.empty());
  }
  {
    auto policy = take(p::Policy::create()); p::Inputs input;
    const auto a = take(policy.registerDownload("A")), b = take(policy.registerDownload("B")), c = take(policy.registerDownload("C"));
    take(policy.enqueue(a, true, input)); take(policy.enqueue(b, true, input)); take(policy.enqueue(c, true, input));
    auto done = take(startLast(policy, input).write->complete(input)); CHECK(done.timer); take(done.timer->fire());
    const auto before = policy.snapshot();
    CHECK(error(policy.dispatchOne(input, p::CompletionMode::Inline), p::Error::Input));
    CHECK(policy.snapshot().dispatches == before.dispatches); CHECK(policy.snapshot().publications == before.publications);
    CHECK(!policy.snapshot().writeInProgress); CHECK(input.timesConsumed() == 1);
    const std::array<std::int64_t, 1> clock{0}; ok(input.setTimes(clock));
    const auto inlineDone = take(policy.dispatchOne(input, p::CompletionMode::Inline));
    CHECK(inlineDone.size == 7); CHECK(inlineDone.size <= p::Receipt::capacity);
    CHECK(inlineDone.timer); CHECK(inlineDone.events[6].when == 10000);
    CHECK(!policy.snapshot().writeInProgress); CHECK(policy.snapshot().publications == std::vector<std::uint64_t>{a.id()});
  }
}
}
int main() {
  ownershipAndMoves(); foreignAndOnceOnly(); orderingAndRandomBounds(); reservedControlCapacity();
  boundedHistoryAndTimerProgress(); removalStopAndInline(); scalarAndConfigurationBounds(); pendingCloseAndInlinePreflight();
  std::cout << "PASS rating publication ownership, handles, control capacity and source policy bounds; " << assertions << " assertions\n";
}
