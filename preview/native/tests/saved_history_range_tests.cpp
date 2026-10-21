#include "biglybtc/saved_history_range.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace biglybtc::core;
using I = std::int64_t;
constexpr I Minute = 60000, Hour = 60 * Minute, Day = 24 * Hour, Week = 7 * Day;
std::size_t cases = 0, assertions = 0;
void check(bool condition, std::string_view message) { ++assertions; if (!condition) { throw std::runtime_error(std::string(message)); } }
template<class F> void run(std::string_view name, F body) { try { body(); ++cases; } catch (const std::exception& e) { throw std::runtime_error(std::string(name) + ": " + e.what()); } }
I utc(HistoryDate date, int hour = 0, int minute = 0, int second = 0) {
  using namespace std::chrono;
  return duration_cast<milliseconds>(sys_days{year{date.year}/month{date.month}/day{date.day}}.time_since_epoch()).count() + hour * Hour + minute * Minute + second * 1000;
}
HistoryDate date(I value) {
  const std::chrono::year_month_day d{std::chrono::sys_days{std::chrono::days{value / Day}}};
  return {static_cast<int>(d.year()), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day())};
}
HistoryCounters values(I multiplier = 1) { return {multiplier, multiplier * 2, multiplier * 3, multiplier * 4, multiplier * 5, multiplier * 6}; }
std::string row(const HistoryCounters& counters = values()) {
  std::string result; for (const auto value : counters) { if (!result.empty()) { result += ','; } result += std::to_string(value); } return result;
}
std::string start(I millis) { return "s,1," + std::to_string(millis / Minute) + ",100,200,300,400,500,600\n"; }
std::vector<SavedHistoryRecord> parsed(const std::string& bytes) { auto result = readSavedHistoryRecords(bytes); check(bool(result), "valid strict records rejected"); return std::move(*result); }
SavedHistoryRangePlan plan(HistoryPeriod period = HistoryPeriod::Day, I now = utc({2024, 3, 2}, 12), I offset = 0) {
  auto result = planSavedHistoryRange({period, offset, now}); check(bool(result), "valid request rejected"); return *result;
}
using Files = std::map<I, std::vector<SavedHistoryRecord>>;
SavedHistoryRange finish(SavedHistoryRangePlan p, Files files = {}) {
  auto accumulator = SavedHistoryRangeAccumulator::create(p); check(bool(accumulator), "valid plan create rejected");
  for (I file = utc(p.firstFile); file <= utc(p.lastFile); file += Day) {
    auto found = files.find(file); std::optional<std::vector<SavedHistoryRecord>> records;
    if (found != files.end()) { records = std::move(found->second); }
    auto result = accumulator->append(date(file), std::move(records)); check(bool(result), "valid file append rejected");
  }
  auto result = std::move(*accumulator).finish(); check(bool(result), "complete range finish rejected"); return std::move(*result);
}
void unknown(const SavedHistoryRange& range) {
  check(!range.totals && range.observations == 0, "unknown whole totals fabricated");
  for (const auto& bucket : range.buckets) { check(!bucket.totals && bucket.observations == 0, "unknown bucket fabricated"); }
}
void planError(HistoryRangeRequest request, SavedHistoryError wanted, std::optional<I> first = {}) {
  const auto result = planSavedHistoryRange(request, first); check(!result && result.error() == wanted, "planner wrong error");
}
} // namespace

int main() {
  try {
    run("all-five-fixed-resolutions-and-literal-Month720", [] {
      const I now = utc({2024, 3, 2}, 9, 31, 30);
      const std::array<HistoryPeriod, 5> periods{HistoryPeriod::Hour, HistoryPeriod::Day, HistoryPeriod::Week, HistoryPeriod::Month, HistoryPeriod::Year};
      const std::array<I, 5> resolutions{Minute, Minute, Hour, Hour, Day}; const std::array<std::size_t, 5> counts{60, 1440, 168, 720, 365};
      for (std::size_t i = 0; i < periods.size(); ++i) { const auto p = plan(periods[i], now); check(p.request.nowMillis == now && p.bucketMillis == resolutions[i] && p.bucketCount == counts[i], "fixed resolution/count wrong"); check(p.endMillis % resolutions[i] == 0 && p.endMillis >= now && p.endMillis - now < resolutions[i], "ceil boundary wrong"); }
      const auto month = plan(HistoryPeriod::Month, now); check(month.startMillis == utc({2024, 2, 1}, 10) && month.endMillis == utc({2024, 3, 2}, 10), "rolling Month is not fixed30days/hour720");
      check(month.firstFile == HistoryDate{2024, 2, 1} && month.lastFile == HistoryDate{2024, 3, 2}, "clamped inclusive whole files must span31 dates");
    });
    run("offsets-are-whole-spans-without-calendar-drift", [] {
      const I now = utc({2024, 3, 2}, 9, 31, 30); const auto month = plan(HistoryPeriod::Month, now, -1);
      check(month.startMillis == utc({2024, 1, 2}, 10) && month.endMillis == utc({2024, 2, 1}, 10), "previous fixed30 offset wrong");
      const auto hour = plan(HistoryPeriod::Hour, now, -2); check(hour.endMillis == utc({2024, 3, 2}, 7, 32), "minute ceil after offset wrong");
      check(hour.lastFile == HistoryDate{2024, 3, 2}, "past inclusive end file wrong");
    });
    run("domain-enums-offsets-and-arithmetic-fail-before-allocation", [] {
      const auto now = utc({2024, 3, 2}); planError({HistoryPeriod::Month, 1, now}, SavedHistoryError::InvalidDate);
      planError({static_cast<HistoryPeriod>(88), 0, now}, SavedHistoryError::InvalidDate); planError({HistoryPeriod::Hour, 0, -1}, SavedHistoryError::InvalidDate);
      planError({HistoryPeriod::Hour, 0, utc({10000, 1, 1})}, SavedHistoryError::InvalidDate); planError({HistoryPeriod::Hour, 0, 0}, SavedHistoryError::InvalidDate);
      planError({HistoryPeriod::Year, -1, Day}, SavedHistoryError::InvalidDate); planError({HistoryPeriod::Year, std::numeric_limits<I>::min(), now}, SavedHistoryError::Overflow);
      planError({HistoryPeriod::Hour, 0, utc({9999, 12, 31}, 23, 59, 59) + 999}, SavedHistoryError::InvalidDate);
      const auto edge = plan(HistoryPeriod::Hour, utc({9999, 12, 31}, 22, 59, 30)); check(edge.endMillis == utc({9999, 12, 31}, 23), "upper complete valid range lost");
      check(plan(HistoryPeriod::Hour, Hour).startMillis == 0, "earliest complete hour rejected");
    });
    run("All-explicit-earliest-fallback-and-minimum-one-bucket", [] {
      const HistoryRangeRequest request{HistoryPeriod::All, 0, 10 * Day + 1};
      const auto value = planSavedHistoryRange(request, 2 * Day); check(bool(value), "All valid earliest rejected"); check(value->startMillis == 0 && value->endMillis == 2 * Week && value->bucketCount == 2, "All floor/ceil wrong");
      const auto none = planSavedHistoryRange(request); check(bool(none) && none->startMillis == Week && none->bucketCount == 1, "empty All fallback wrong");
      for (I now : {I{0}, Week}) { const auto exact = planSavedHistoryRange({HistoryPeriod::All, 0, now}); check(bool(exact) && exact->startMillis == now && exact->endMillis == now + Week && exact->bucketCount == 1, "All equal boundary native minimum case wrong"); check(exact->firstFile == date(now) && exact->lastFile == date(now), "All must not enumerate nominal future bins"); unknown(finish(*exact)); }
      planError({HistoryPeriod::All, -1, Week}, SavedHistoryError::InvalidDate); planError({HistoryPeriod::All, 0, Week}, SavedHistoryError::InvalidDate, Week + 1);
    });
    run("All-file-cap-and-structurally-bounded-bucket-cap", [] {
      const auto edge = planSavedHistoryRange({HistoryPeriod::All, 0, 65535 * Day}, 0); check(bool(edge), "exact65536file cap rejected");
      check(edge->bucketCount <= SavedHistoryRangeMaximumBuckets, "file-bounded All exceeds bucket cap"); planError({HistoryPeriod::All, 0, 65536 * Day}, SavedHistoryError::Limit, 0);
      check(SavedHistoryRangeMaximumFiles == 65536 && SavedHistoryRangeMaximumBuckets == 16384, "public limits changed");
      // Finite modes have <=1440 buckets; file cap dominates All long before
      //16384 independent bucket exhaustion. Do not fabricate a reachable max.
      auto malformed = *edge; malformed.bucketCount = SavedHistoryRangeMaximumBuckets + 1; check(!SavedHistoryRangeAccumulator::create(malformed), "tampered bucket count accepted");
    });
    run("shared-record-grammar-baselines-positive-anchor-and-end-time", [] {
      auto records = parsed(start(utc({2024, 3, 2})) + row() + "\ne,1,99999,1,2,3,4,5,6\n" + row(values(-1)) + '\n');
      check(records.size() == 3 && records[0].timeMillis == utc({2024, 3, 2}, 0, 1) && records[2].timeMillis == utc({2024, 3, 2}, 0, 3), "reconstructed minute/e anchor wrong");
      check(records[0].counters == values() && records[2].counters == values(-1), "counter order/sign changed");
      check(parsed("s,1,0,0,0,0,0,0,0\n1,2,3,4,5,6\n").empty(), "zero anchor must be inactive");
      check(parsed("s,1,-1,0,0,0,0,0,0\n1,2,3,4,5,6\n").empty(), "negative anchor must be inactive");
    });
    run("shared-parser-EOF-CRLF-ownership-and-old-day-shape", [] {
      const auto raw = start(utc({2024, 3, 2}, 0, 58)) + row() + '\n' + row(values(-1)); auto records = parsed(raw); const auto old = readSavedHistoryDay(raw, {2024, 3, 2}); check(bool(old), "old daily projection failed");
      check(records.size() == 2 && old->hours[0] == values() && old->hours[1] == values(-1) && old->totals == HistoryCounters{}, "shared traversal altered old hour/day semantics");
      std::string input = "s,+01,+01440,0,0,0,0,0,0\r\n+1,-2,+3,-4,+5,-6\r\n"; auto owned = parsed(input); input.assign(4096, 'X'); check(owned.size() == 1 && owned[0].counters == HistoryCounters{1, -2, 3, -4, 5, -6}, "record result borrowed input");
    });
    run("strict-parser-errors-and-quotas-never-return-a-prefix", [] {
      const auto prefix = start(Day) + row() + '\n';
      for (const auto& suffix : {std::string("1,2,3,4,5,bad"), std::string("e,1,0,0,0,0,0,0"), std::string("1,2,3,4,5,6\r")}) { const auto result = readSavedHistoryRecords(prefix + suffix); check(!result && result.error() == SavedHistoryError::InvalidRecord, "strict malformed suffix must fail whole vector"); }
      const auto unsupported = readSavedHistoryRecords(prefix + "s,2,0,0,0,0,0,0,0"); check(!unsupported && unsupported.error() == SavedHistoryError::UnsupportedVersion, "version error wrong");
      check(!readSavedHistoryRecords(std::string(SavedHistoryMaximumBytes + 1, '\n')), "input cap lost");
      std::string many; for (std::size_t i = 0; i < SavedHistoryMaximumRecords; ++i) { many += "0,0,0,0,0,0\n"; } check(parsed(many).empty(), "valid inactive maximum records wrong"); check(!readSavedHistoryRecords(many + row()), "record cap lost");
      const auto upper = std::numeric_limits<I>::max() / Minute; const auto overflow = readSavedHistoryRecords(start(upper * Minute) + row()); check(!overflow && overflow.error() == SavedHistoryError::Overflow, "reconstructed timestamp overflow lost");
      std::stop_source stop; stop.request_stop(); const auto cancelled = readSavedHistoryRecords("bad", stop.get_token()); check(!cancelled && cancelled.error() == SavedHistoryError::Cancelled, "prestop must precede invalid input");
    });
    run("missing-present-empty-and-observed-zero-are-not-interchangeable", [] {
      const auto p = plan(); auto missing = finish(p); unknown(missing); check(missing.presentFiles == 0 && missing.missingFiles == 2, "missing coverage wrong");
      auto empty = finish(p, {{utc(p.firstFile), {}}}); unknown(empty); check(empty.presentFiles == 1 && empty.missingFiles == 1, "present-empty coverage lost");
      auto zero = finish(p, {{utc(p.firstFile), {{p.startMillis + Minute, {}}}}}); check(zero.totals == HistoryCounters{} && zero.observations == 1 && zero.buckets[1].totals == HistoryCounters{}, "observed zero became unknown");
    });
    run("inclusive-whole-end-is-not-an-exclusive-bucket-end", [] {
      const auto p = plan(HistoryPeriod::Day, utc({2024, 3, 2})); const auto range = finish(p, {{utc(p.lastFile), {{p.endMillis, values()}}}});
      check(range.totals == values() && range.observations == 1, "whole inclusive endpoint lost");
      for (const auto& bucket : range.buckets) { check(!bucket.totals, "whole endpoint incorrectly assigned to graph bucket"); }
    });
    run("midnight-spill-counts-in-whole-but-not-unrelated-bucket-file", [] {
      const auto p = plan(HistoryPeriod::Day, utc({2024, 3, 2}, 12, 30)); const auto midnight = utc({2024, 3, 2});
      auto range = finish(p, {{utc(p.firstFile), {{midnight, values(5)}}}, {utc(p.lastFile), {{midnight, values(2)}}}});
      check(range.totals == values(7) && range.observations == 2, "whole wider file range excluded valid spill");
      const auto index = static_cast<std::size_t>((midnight - p.startMillis) / Minute); check(index == 690 && range.buckets[index].totals == values(2) && range.buckets[index].observations == 1, "per-bucket filename membership omitted");
    });
    run("clamp-now-before-record-filter-and-filename-enumeration", [] {
      const auto p = plan(HistoryPeriod::Hour, utc({2024, 3, 2}, 9, 31, 30)); check(p.endMillis == utc({2024, 3, 2}, 9, 32), "bad partial bucket fixture");
      const auto range = finish(p, {{utc(p.firstFile), {{utc({2024, 3, 2}, 9, 31), values()}, {p.endMillis, values(10)}}}});
      check(range.totals == values() && range.observations == 1 && range.buckets.back().totals == values(), "future observation counted after now clamp");
      const auto all = planSavedHistoryRange({HistoryPeriod::All, 0, utc({2024, 3, 2}, 12)}); check(bool(all), "All fallback rejected"); check(all->lastFile == HistoryDate{2024, 3, 2}, "future bucket date enumerated");
    });
    run("six-independent-signed-fields-overlap-and-copy-ownership", [] {
      const auto p = plan(); auto range = finish(p, {{utc(p.firstFile), {{p.startMillis + Minute, {11, -22, 33, -44, 55, -66}}, {p.startMillis + Minute, {-1, 2, -3, 4, -5, 6}}}}});
      check(range.totals == HistoryCounters{10, -20, 30, -40, 50, -60} && range.observations == 2 && range.buckets[1].observations == 2, "overlap must add all6 signed fields");
      auto copy = range; range.buckets.clear(); range.totals.reset(); check(copy.totals == HistoryCounters{10, -20, 30, -40, 50, -60} && copy.buckets.size() == 1440, "result copy retained mutable owner alias");
    });
    run("all-planned-files-missing-completes-without-synthesizing-zero", [] {
      const auto p = plan(HistoryPeriod::Month); auto value = finish(p); unknown(value); check(value.buckets.size() == 720 && value.missingFiles == 31 && value.presentFiles == 0, "whole query needs31 files at a partial day");
    });
    run("plan-tampering-is-rejected-before-accumulator-allocation", [] {
      for (int kind = 0; kind < 6; ++kind) { auto p = plan(); switch (kind) { case 0: ++p.startMillis; break; case 1: ++p.endMillis; break; case 2: p.bucketMillis = 0; break; case 3: ++p.bucketCount; break; case 4: p.firstFile = {2024, 2, 29}; break; default: p.lastFile = {10000, 1, 1}; break; } check(!SavedHistoryRangeAccumulator::create(p), "tampered plan accepted"); }
    });
    run("missing-prefix-extra-duplicate-and-out-of-order-files-poison", [] {
      const auto p = plan(); auto shortRead = SavedHistoryRangeAccumulator::create(p); check(bool(shortRead), "create failed"); check(bool(shortRead->append(p.firstFile, std::nullopt)), "first append failed"); check(!std::move(*shortRead).finish(), "partial range published");
      auto wrong = SavedHistoryRangeAccumulator::create(p); check(bool(wrong), "create failed"); const auto failure = wrong->append(p.lastFile, std::nullopt); check(!failure && failure.error() == SavedHistoryError::InvalidDate, "out-of-order file accepted"); check(!wrong->append(p.firstFile, std::nullopt), "poisoned owner resumed"); check(!std::move(*wrong).finish(), "poisoned prefix published");
      auto duplicate = SavedHistoryRangeAccumulator::create(p); check(bool(duplicate), "create failed"); check(bool(duplicate->append(p.firstFile, std::nullopt)), "first append failed"); check(!duplicate->append(p.firstFile, std::nullopt), "duplicate append accepted");
    });
    run("external-record-shape-cap-and-cancellation-poison", [] {
      const auto p = plan();
      for (I time : {I{-1}, I{0}, p.startMillis + 1}) { auto value = SavedHistoryRangeAccumulator::create(p); check(bool(value), "create failed"); const auto result = value->append(p.firstFile, std::vector<SavedHistoryRecord>{{time, {}}}); check(!result && result.error() == SavedHistoryError::InvalidRecord, "impossible emitted timestamp accepted"); check(!std::move(*value).finish(), "invalid record published"); }
      auto oversized = SavedHistoryRangeAccumulator::create(p); check(bool(oversized), "create failed"); const auto tooMany = oversized->append(p.firstFile, std::vector<SavedHistoryRecord>(SavedHistoryMaximumRecords + 1, {p.startMillis + Minute, {}})); check(!tooMany && tooMany.error() == SavedHistoryError::Limit, "vector quota lost");
      auto cancel = SavedHistoryRangeAccumulator::create(p); check(bool(cancel), "create failed"); std::stop_source stop; stop.request_stop(); const auto result = cancel->append({0, 0, 0}, std::nullopt, stop.get_token()); check(!result && result.error() == SavedHistoryError::Cancelled, "prestop priority wrong"); check(!std::move(*cancel).finish(), "cancelled range published");
    });
    run("whole-overflow-and-bucket-overflow-have-no-prefix-success", [] {
      const auto p = plan(); const I low = std::numeric_limits<I>::min(), high = std::numeric_limits<I>::max();
      for (std::size_t component = 0; component < 6; ++component) {
        for (const auto& [large, one] : {std::pair{high, I{1}}, std::pair{low, I{-1}}}) { HistoryCounters a{}, b{}; a[component] = large; b[component] = one; auto owner = SavedHistoryRangeAccumulator::create(p); check(bool(owner), "create failed"); const auto failure = owner->append(p.firstFile, std::vector<SavedHistoryRecord>{{p.startMillis + Minute, a}, {p.startMillis + 2 * Minute, b}}); check(!failure && failure.error() == SavedHistoryError::Overflow, "whole sum overflow lost"); check(!std::move(*owner).finish(), "overflow prefix published"); }
      }
      auto owner = SavedHistoryRangeAccumulator::create(p); check(bool(owner), "create failed"); const auto time = utc(p.lastFile, 11); check(bool(owner->append(p.firstFile, std::vector<SavedHistoryRecord>{{time, {-1, 0, 0, 0, 0, 0}}})), "spill compensation failed");
      const auto failure = owner->append(p.lastFile, std::vector<SavedHistoryRecord>{{time, {high, 0, 0, 0, 0, 0}}, {time, {1, 0, 0, 0, 0, 0}}}); check(!failure && failure.error() == SavedHistoryError::Overflow, "independent bucket overflow hidden by whole compensation"); check(!std::move(*owner).finish(), "bucket overflow published");
    });
    run("moved-and-consumed-owners-never-publish-a-second-result", [] {
      const auto p = plan(); auto created = SavedHistoryRangeAccumulator::create(p); check(bool(created), "create failed"); auto moved = std::move(*created); check(!std::move(*created).finish(), "moved-from owner published");
      check(bool(moved.append(p.firstFile, std::nullopt)) && bool(moved.append(p.lastFile, std::nullopt)), "moved owner lost state"); auto output = std::move(moved).finish(); check(bool(output), "moved complete owner rejected"); check(!std::move(moved).finish(), "second finish published"); check(!moved.append(p.lastFile, std::nullopt), "consumed append accepted");
    });
    std::cout << "PASS " << cases << " saved-history-range cases; " << assertions << " assertions; strict native period queries; no Java/runtime equality credit\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "FAIL saved-history-range: " << error.what() << '\n'; return 1; }
}
