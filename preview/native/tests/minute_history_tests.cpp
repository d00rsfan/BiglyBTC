#include "biglybtc/minute_history.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace biglybtc::core;
constexpr std::int64_t Jan1 = 1704067200000LL;
constexpr MinuteHistoryIdentity Identity{1, 2, 3};
constexpr HistoryCounters Base{100, 200, 300, 400, 500, 600};
constexpr HistoryCounters Raw{10, 20, 30, 40, 50, 60};
std::size_t assertions = 0;
void check(bool value, const char* why) { ++assertions; if (!value) { throw std::runtime_error(why); } }
MinuteHistoryInput input(HistoryCounters value) {
  return {std::array{value[0], value[1], value[2], value[3]}, std::array{value[4], value[5]}};
}
MinuteHistoryRequest request(MinuteHistoryEvent event, std::int64_t now, HistoryCounters value = Raw) {
  return {event, Identity, now, input(value), event == MinuteHistoryEvent::Start ? std::optional{Base} : std::nullopt};
}
MinuteHistoryPlan plan(MinuteHistoryRecorder& recorder, const MinuteHistoryRequest& value) {
  auto result = recorder.prepare(value); check(result.has_value(), "prepare rejected"); return *result;
}
void commit(MinuteHistoryRecorder& recorder, const MinuteHistoryPlan& value) {
  check(recorder.finish(value, MinuteHistoryIoResult::Complete).has_value(), "commit rejected");
}
void start(MinuteHistoryRecorder& recorder, std::int64_t now = Jan1, HistoryCounters value = Raw) {
  auto first = plan(recorder, request(MinuteHistoryEvent::Start, now, value)); commit(recorder, first);
}
void bytes(const MinuteHistoryPlan& value, std::size_t index, std::string_view expected) {
  check(value.actions().size() > index, "missing action");
  check(value.actions()[index].kind() == MinuteHistoryActionKind::Append, "not an append");
  check(value.actions()[index].bytes() == expected, "exact line mismatch");
}
void family(const char* name) { std::cout << "CASE " << name << '\n'; }
}

int main() {
  try {
    {
      family("explicit-unknown-start-and-observed-zero"); MinuteHistoryRecorder recorder;
      auto missing = request(MinuteHistoryEvent::Start, Jan1); missing.input.dht.reset();
      const auto result = recorder.prepare(missing);
      check(!result && result.error() == MinuteHistoryError::UnknownProvider, "DHT unknown fabricated");
      check(recorder.state() == MinuteHistoryState{}, "failed start mutated state");
      auto zero = request(MinuteHistoryEvent::Start, Jan1, {}); zero.absoluteBaseline = HistoryCounters{};
      auto first = plan(recorder, zero); bytes(first, 0, "s,1,28401120,0,0,0,0,0,0\n");
      check(!recorder.state().active, "prepare advanced state"); commit(recorder, first);
      auto tick = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 60000, {}));
      bytes(tick, 0, "0,0,0,0,0,0\n");
      auto projected = readSavedHistoryDay("s,1,28401120,0,0,0,0,0,0\n0,0,0,0,0,0\n", {2024, 1, 1});
      check(projected && projected->observations == 1 && projected->totals == HistoryCounters{}, "known zero lost");
    }
    {
      family("start-format-pending-plan-and-owned-bytes"); MinuteHistoryRecorder recorder;
      auto first = plan(recorder, request(MinuteHistoryEvent::Start, Jan1));
      bytes(first, 0, "s,1,28401120,100,200,300,400,500,600\n");
      check(first.actions()[0].date() == HistoryDate{2024, 1, 1}, "wrong UTC date");
      const auto pending = recorder.prepare(request(MinuteHistoryEvent::Start, Jan1));
      check(!pending && pending.error() == MinuteHistoryError::Pending, "parallel plans admitted");
      const auto owned = first; commit(recorder, first);
      bytes(owned, 0, "s,1,28401120,100,200,300,400,500,600\n");
      check(recorder.state().active && recorder.state().sourceOrigin == Raw, "start state wrong");
    }
    {
      family("independent-six-field-delta-and-end"); MinuteHistoryRecorder recorder; start(recorder);
      auto tick = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 60000, {13, 25, 37, 49, 61, 73}));
      bytes(tick, 0, "3,5,7,9,11,13\n"); commit(recorder, tick);
      auto end = plan(recorder, request(MinuteHistoryEvent::End, Jan1 + 120000, {14, 27, 40, 53, 66, 79}));
      bytes(end, 0, "e,1,28401122,1,2,3,4,5,6\n");
      check(end.actions().size() == 2 && end.actions()[1].kind() == MinuteHistoryActionKind::Close, "end close missing");
      commit(recorder, end); check(!recorder.state().active && !recorder.state().fileDate, "end still active");
    }
    {
      family("midnight-old-delta-new-baseline-no-double-numeric"); MinuteHistoryRecorder recorder; start(recorder, Jan1 - 60000);
      auto rollover = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1, {11, 22, 33, 44, 55, 66}));
      check(rollover.actions().size() == 3, "rollover action count");
      bytes(rollover, 0, "1,2,3,4,5,6\n");
      check(rollover.actions()[0].date() == HistoryDate{2023, 12, 31}, "spill not in old file");
      check(rollover.actions()[1].kind() == MinuteHistoryActionKind::Close, "old file not closed first");
      bytes(rollover, 2, "s,1,28401120,101,202,303,404,505,606\n");
      check(rollover.actions()[2].date() == HistoryDate{2024, 1, 1}, "new file date wrong");
      commit(recorder, rollover);
      auto next = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 60000, {12, 24, 36, 48, 60, 72}));
      bytes(next, 0, "1,2,3,4,5,6\n");
      auto old = readSavedHistoryDay("s,1,28401119,100,200,300,400,500,600\n1,2,3,4,5,6\n", {2023, 12, 31});
      check(old && old->observations == 0 && old->outsideDayRecords == 1 && !old->totals, "old spill semantics wrong");
      auto current = readSavedHistoryDay("s,1,28401120,101,202,303,404,505,606\n1,2,3,4,5,6\n", {2024, 1, 1});
      check(current && current->observations == 1 && current->totals == HistoryCounters{1, 2, 3, 4, 5, 6}, "new baseline counted as traffic");
    }
    {
      family("end-after-midnight-never-opens-new-file"); MinuteHistoryRecorder recorder; start(recorder, Jan1 - 60000);
      auto end = plan(recorder, request(MinuteHistoryEvent::End, Jan1, {11, 22, 33, 44, 55, 66}));
      check(end.actions().size() == 2, "end created new file");
      bytes(end, 0, "e,1,28401120,1,2,3,4,5,6\n");
      check(end.actions()[0].date() == HistoryDate{2023, 12, 31}, "end redirected to new date");
    }
    {
      family("late-tick-has-no-elapsed-minute-fill"); MinuteHistoryRecorder recorder; start(recorder);
      auto tick = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 13 * 3600000, {11, 22, 33, 44, 55, 66}));
      check(tick.actions().size() == 1, "late tick invented records"); bytes(tick, 0, "1,2,3,4,5,6\n");
      auto day = readSavedHistoryDay("s,1,28401120,100,200,300,400,500,600\n1,2,3,4,5,6\n", {2024, 1, 1});
      check(day && day->observations == 1 && day->hours[0] && !day->hours[13], "reconstructed minute replaced by wall time");
    }
    {
      family("backward-wall-date-is-not-monotonic-clock"); MinuteHistoryRecorder recorder; start(recorder);
      auto tick = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 - 60000, {11, 22, 33, 44, 55, 66}));
      check(tick.actions().size() == 3 && tick.actions()[2].date() == HistoryDate{2023, 12, 31}, "wall-clock date regression lost");
      bytes(tick, 2, "s,1,28401119,101,202,303,404,505,606\n");
    }
    {
      family("unknown-provider-suspends-without-zero-or-terminal-line"); MinuteHistoryRecorder recorder; start(recorder);
      auto value = request(MinuteHistoryEvent::Tick, Jan1 + 60000); value.input.dht.reset();
      auto gap = plan(recorder, value);
      check(gap.providerGap() && gap.actions().size() == 1 && gap.actions()[0].bytes().empty()
            && gap.actions()[0].kind() == MinuteHistoryActionKind::Close, "gap fabricated bytes");
      commit(recorder, gap);
      auto tick = recorder.prepare(request(MinuteHistoryEvent::Tick, Jan1 + 120000));
      check(!tick && tick.error() == MinuteHistoryError::Inactive, "gap silently bridged");
      auto restart = plan(recorder, request(MinuteHistoryEvent::Start, Jan1 + 120000));
      bytes(restart, 0, "s,1,28401122,100,200,300,400,500,600\n");
    }
    {
      family("unknown-end-cannot-claim-final-six-fields"); MinuteHistoryRecorder recorder; start(recorder);
      auto value = request(MinuteHistoryEvent::End, Jan1 + 60000); value.input.peer.reset();
      auto gap = plan(recorder, value);
      check(gap.providerGap() && gap.actions().size() == 1 && gap.actions()[0].bytes().empty(), "unknown end fabricated e");
    }
    {
      family("cancel-before-io-invalidates-plan-without-advancing"); MinuteHistoryRecorder recorder; start(recorder);
      const auto before = recorder.state();
      auto tick = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 60000, {11, 22, 33, 44, 55, 66}));
      check(recorder.finish(tick, MinuteHistoryIoResult::CancelledBeforeIo).has_value(), "cancel rejected");
      check(recorder.state() == before, "cancel advanced state");
      check(!recorder.finish(tick, MinuteHistoryIoResult::Complete), "cancelled plan reused");
      auto retry = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 60000, {11, 22, 33, 44, 55, 66}));
      check(retry.actions()[0].bytes() == tick.actions()[0].bytes(), "safe retry changed bytes");
    }
    {
      family("ambiguous-partial-append-poisons-no-blind-retry"); MinuteHistoryRecorder recorder; start(recorder);
      auto tick = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 60000));
      check(recorder.finish(tick, MinuteHistoryIoResult::Indeterminate).has_value(), "failure transition rejected");
      check(recorder.state().poisoned && !recorder.state().active, "ambiguous writer not poisoned");
      const auto next = recorder.prepare(request(MinuteHistoryEvent::Start, Jan1));
      check(!next && next.error() == MinuteHistoryError::Poisoned, "uncertain append retried");
    }
    {
      family("identity-foreign-plan-and-consumed-plan-fences"); MinuteHistoryRecorder a, b; start(a); start(b);
      auto wrong = request(MinuteHistoryEvent::Tick, Jan1); ++wrong.identity.engineEpoch;
      const auto rejected = a.prepare(wrong); check(!rejected && rejected.error() == MinuteHistoryError::WrongIdentity, "cross epoch admitted");
      auto ap = plan(a, request(MinuteHistoryEvent::Tick, Jan1)); auto bp = plan(b, request(MinuteHistoryEvent::Tick, Jan1));
      check(!a.finish(bp, MinuteHistoryIoResult::Complete), "foreign plan admitted");
      commit(a, ap); check(!a.finish(ap, MinuteHistoryIoResult::Complete), "plan replayed"); commit(b, bp);
    }
    {
      family("signed-adjustment-is-preserved-not-clamped"); MinuteHistoryRecorder recorder; start(recorder);
      auto tick = plan(recorder, request(MinuteHistoryEvent::Tick, Jan1 + 60000, {8, 17, 26, 35, 44, 53}));
      bytes(tick, 0, "-2,-3,-4,-5,-6,-7\n");
    }
    {
      family("checked-relative-overflow-does-not-consume-plan"); MinuteHistoryRecorder recorder;
      HistoryCounters origin{}; origin[0] = std::numeric_limits<std::int64_t>::min(); start(recorder, Jan1, origin);
      const auto before = recorder.state(); HistoryCounters current{}; current[0] = std::numeric_limits<std::int64_t>::max();
      const auto result = recorder.prepare(request(MinuteHistoryEvent::Tick, Jan1 + 60000, current));
      check(!result && result.error() == MinuteHistoryError::Overflow && recorder.state() == before, "overflow mutated state");
      auto valid = plan(recorder, request(MinuteHistoryEvent::End, Jan1 + 60000, origin)); bytes(valid, 0, "e,1,28401121,0,0,0,0,0,0\n");
    }
    {
      family("rollover-baseline-overflow-does-not-publish-prefix"); MinuteHistoryRecorder recorder;
      auto value = request(MinuteHistoryEvent::Start, Jan1 - 60000); value.absoluteBaseline = HistoryCounters{};
      (*value.absoluteBaseline)[0] = std::numeric_limits<std::int64_t>::max();
      auto first = plan(recorder, value); commit(recorder, first); const auto before = recorder.state();
      const auto result = recorder.prepare(request(MinuteHistoryEvent::Tick, Jan1, {11, 20, 30, 40, 50, 60}));
      check(!result && result.error() == MinuteHistoryError::Overflow && recorder.state() == before, "rollover leaked prefix state");
    }
    {
      family("full-signed-width-header-remains-owned-and-parseable"); MinuteHistoryRecorder recorder;
      auto value = request(MinuteHistoryEvent::Start, Jan1);
      value.absoluteBaseline = HistoryCounters{std::numeric_limits<std::int64_t>::min(),
          std::numeric_limits<std::int64_t>::max(), 0, -1, 1, std::numeric_limits<std::int64_t>::max()};
      auto first = plan(recorder, value);
      bytes(first, 0, "s,1,28401120,-9223372036854775808,9223372036854775807,0,-1,1,9223372036854775807\n");
      auto projected = readSavedHistoryDay(first.actions()[0].bytes(), {2024, 1, 1});
      check(projected && !projected->totals && projected->observations == 0, "absolute signed baseline was traffic");
    }
    {
      family("explicit-suspension-and-invalid-io-result-keep-fences"); MinuteHistoryRecorder recorder; start(recorder);
      auto value = request(MinuteHistoryEvent::Suspend, -1); // Closing the open writer needs no wall clock.
      auto stop = plan(recorder, value);
      check(stop.actions().size() == 1 && stop.actions()[0].kind() == MinuteHistoryActionKind::Close
            && stop.actions()[0].bytes().empty(), "suspend invented final data");
      check(!recorder.finish(stop, static_cast<MinuteHistoryIoResult>(999)), "unknown IO disposition accepted");
      check(recorder.state().active, "invalid acknowledgement consumed state"); commit(recorder, stop);
      check(!recorder.state().active && !recorder.state().poisoned, "clean suspension poisoned recorder");
    }
    {
      family("epoch-zero-and-date-domain-admission"); MinuteHistoryRecorder recorder;
      auto value = request(MinuteHistoryEvent::Start, Jan1); value.identity.engineEpoch = 0;
      check(!recorder.prepare(value), "zero identity admitted");
      value = request(MinuteHistoryEvent::Start, -1); check(!recorder.prepare(value), "negative time admitted");
      value.utcMilliseconds = 253402300800000LL; check(!recorder.prepare(value), "year10000 admitted");
      value.utcMilliseconds = 0; auto epoch = plan(recorder, value);
      bytes(epoch, 0, "s,1,0,100,200,300,400,500,600\n");
      auto day = readSavedHistoryDay("s,1,0,100,200,300,400,500,600\n1,2,3,4,5,6\n", {1970, 1, 1});
      check(day && !day->totals && day->observations == 0, "positive anchor gate changed");
    }
    std::cout << "minute recorder source-derived domain assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
