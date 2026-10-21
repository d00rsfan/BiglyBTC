#include "biglybtc/saved_history_range.hpp"

#include <chrono>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::core {
namespace {
using I = std::int64_t;
constexpr I Minute = 60000, Hour = 60 * Minute, Day = 24 * Hour, Week = 7 * Day;
constexpr I MaximumMillis = 253402300799999; //9999-12-31T23:59:59.999Z.
HistoryDate date(I millis) noexcept {
  const auto value = std::chrono::year_month_day{std::chrono::sys_days{std::chrono::days{millis / Day}}};
  return {static_cast<int>(value.year()), static_cast<unsigned>(value.month()), static_cast<unsigned>(value.day())};
}
I midnight(HistoryDate value) noexcept {
  return std::chrono::sys_days{std::chrono::year{value.year} / std::chrono::month{value.month}
      / std::chrono::day{value.day}}.time_since_epoch().count() * Day;
}
bool add(HistoryCounters& target, const HistoryCounters& values) noexcept {
  for (std::size_t i = 0; i < target.size(); ++i) {
    if ((values[i] > 0 && target[i] > std::numeric_limits<I>::max() - values[i])
        || (values[i] < 0 && target[i] < std::numeric_limits<I>::min() - values[i])) { return false; }
    target[i] += values[i];
  }
  return true;
}
} // namespace

std::expected<SavedHistoryRangePlan, SavedHistoryError> planSavedHistoryRange(
    HistoryRangeRequest request, std::optional<I> firstMillis) {
  if (request.nowMillis < 0 || request.nowMillis > MaximumMillis || request.offset > 0) {
    return std::unexpected(SavedHistoryError::InvalidDate);
  }
  I span = 0, resolution = 0;
  switch (request.period) {
    case HistoryPeriod::Hour: span = Hour; resolution = Minute; break;
    case HistoryPeriod::Day: span = Day; resolution = Minute; break;
    case HistoryPeriod::Week: span = Week; resolution = Hour; break;
    case HistoryPeriod::Month: span = 30 * Day; resolution = Hour; break;
    case HistoryPeriod::Year: span = 365 * Day; resolution = Day; break;
    case HistoryPeriod::All: resolution = Week; break;
    default: return std::unexpected(SavedHistoryError::InvalidDate);
  }
  I start = 0, end = 0;
  if (request.period == HistoryPeriod::All) {
    if (request.offset != 0) { return std::unexpected(SavedHistoryError::InvalidDate); }
    const auto first = firstMillis.value_or(request.nowMillis);
    if (first < 0 || first > request.nowMillis) { return std::unexpected(SavedHistoryError::InvalidDate); }
    start = (first / Week) * Week;
    end = (request.nowMillis / Week + (request.nowMillis % Week != 0)) * Week;
    if (end == start) { end += Week; }
  } else {
    if (request.offset < std::numeric_limits<I>::min() / span) {
      return std::unexpected(SavedHistoryError::Overflow);
    }
    const I shifted = request.nowMillis + request.offset * span;
    if (shifted < 0) { return std::unexpected(SavedHistoryError::InvalidDate); }
    end = (shifted / resolution + (shifted % resolution != 0)) * resolution;
    start = end - span;
  }
  if (start < 0 || end > MaximumMillis || end <= start) {
    return std::unexpected(SavedHistoryError::InvalidDate);
  }
  const I count = (end - start) / resolution;
  if (count <= 0 || count > static_cast<I>(SavedHistoryRangeMaximumBuckets)) {
    return std::unexpected(SavedHistoryError::Limit);
  }
  const I final = end < request.nowMillis ? end : request.nowMillis;
  if (final < start) { return std::unexpected(SavedHistoryError::InvalidDate); }
  const I files = final / Day - start / Day + 1;
  if (files > static_cast<I>(SavedHistoryRangeMaximumFiles)) {
    return std::unexpected(SavedHistoryError::Limit);
  }
  return SavedHistoryRangePlan{request, start, end, resolution, static_cast<std::size_t>(count), date(start), date(final)};
}

SavedHistoryRangeAccumulator::SavedHistoryRangeAccumulator(SavedHistoryRangeAccumulator&& other) noexcept
    : result_(std::move(other.result_)), files_(other.files_), appended_(other.appended_),
      usable_(std::exchange(other.usable_, false)), error_(other.error_) {}
SavedHistoryRangeAccumulator& SavedHistoryRangeAccumulator::operator=(SavedHistoryRangeAccumulator&& other) noexcept {
  if (this != &other) {
    result_ = std::move(other.result_); files_ = other.files_; appended_ = other.appended_;
    usable_ = std::exchange(other.usable_, false); error_ = other.error_;
  }
  return *this;
}
std::expected<SavedHistoryRangeAccumulator, SavedHistoryError> SavedHistoryRangeAccumulator::create(
    SavedHistoryRangePlan plan) {
  const auto checked = planSavedHistoryRange(plan.request,
      plan.request.period == HistoryPeriod::All ? std::optional<I>{plan.startMillis} : std::nullopt);
  if (!checked) { return std::unexpected(checked.error()); }
  if (*checked != plan) { return std::unexpected(SavedHistoryError::InvalidDate); }
  try {
    SavedHistoryRangeAccumulator result;
    result.result_.plan = plan;
    result.files_ = static_cast<std::size_t>((midnight(plan.lastFile) - midnight(plan.firstFile)) / Day + 1);
    result.result_.buckets.reserve(plan.bucketCount);
    for (std::size_t i = 0; i < plan.bucketCount; ++i) {
      const I start = plan.startMillis + static_cast<I>(i) * plan.bucketMillis;
      result.result_.buckets.push_back({start, start + plan.bucketMillis, std::nullopt, 0});
    }
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(SavedHistoryError::Memory); }
}
std::expected<void, SavedHistoryError> SavedHistoryRangeAccumulator::fail(SavedHistoryError error) noexcept {
  if (!error_) { error_ = error; }
  return std::unexpected(*error_);
}
std::expected<void, SavedHistoryError> SavedHistoryRangeAccumulator::append(HistoryDate file,
    std::optional<std::vector<SavedHistoryRecord>> records, std::stop_token stop) {
  if (error_) { return std::unexpected(*error_); }
  if (!usable_) { return fail(SavedHistoryError::InvalidRecord); }
  if (stop.stop_requested()) { return fail(SavedHistoryError::Cancelled); }
  const auto& plan = result_.plan;
  if (appended_ >= files_ || !validHistoryDate(file)
      || midnight(file) != midnight(plan.firstFile) + static_cast<I>(appended_) * Day) {
    return fail(SavedHistoryError::InvalidDate);
  }
  if (records && records->size() > SavedHistoryMaximumRecords) { return fail(SavedHistoryError::Limit); }
  const I fileStart = midnight(file), fileEnd = fileStart + Day - 1;
  const I wholeEnd = plan.endMillis < plan.request.nowMillis ? plan.endMillis : plan.request.nowMillis;
  if (records) {
    for (const auto& record : *records) {
      if (stop.stop_requested()) { return fail(SavedHistoryError::Cancelled); }
      if (record.timeMillis <= 0 || record.timeMillis % Minute != 0) { return fail(SavedHistoryError::InvalidRecord); }
      if (record.timeMillis >= plan.startMillis && record.timeMillis <= wholeEnd) {
        auto total = result_.totals.value_or(HistoryCounters{});
        if (!add(total, record.counters)) { return fail(SavedHistoryError::Overflow); }
        result_.totals = total; ++result_.observations;
      }
      if (record.timeMillis < plan.startMillis || record.timeMillis >= plan.endMillis
          || record.timeMillis > plan.request.nowMillis) { continue; }
      const auto at = static_cast<std::size_t>((record.timeMillis - plan.startMillis) / plan.bucketMillis);
      auto& bucket = result_.buckets[at];
      const I end = bucket.endMillis - 1 < plan.request.nowMillis ? bucket.endMillis - 1 : plan.request.nowMillis;
      // Original each-bucket Date query enumerates only its own clamped UTC
      // filename dates. A timestamp-valid spill from a different day still
      // contributes to the independently wider whole query, never this bucket.
      if (bucket.startMillis > end || fileEnd < (bucket.startMillis / Day) * Day
          || fileStart > (end / Day) * Day) { continue; }
      auto total = bucket.totals.value_or(HistoryCounters{});
      if (!add(total, record.counters)) { return fail(SavedHistoryError::Overflow); }
      bucket.totals = total; ++bucket.observations;
    }
    ++result_.presentFiles;
  } else { ++result_.missingFiles; }
  ++appended_;
  if (stop.stop_requested()) { return fail(SavedHistoryError::Cancelled); }
  return {};
}
std::expected<SavedHistoryRange, SavedHistoryError> SavedHistoryRangeAccumulator::finish() && {
  if (error_) { return std::unexpected(*error_); }
  if (!usable_ || appended_ != files_) { usable_ = false; return std::unexpected(SavedHistoryError::InvalidRecord); }
  usable_ = false;
  return std::move(result_);
}

} // namespace biglybtc::core
