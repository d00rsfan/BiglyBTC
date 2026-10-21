#pragma once

#include "biglybtc/saved_history.hpp"

#include <vector>

namespace biglybtc::core {

enum class HistoryPeriod { Hour, Day, Week, Month, Year, All };
struct HistoryRangeRequest {
  HistoryPeriod period = HistoryPeriod::Week;
  std::int64_t offset = 0;
  std::int64_t nowMillis = 0;
  bool operator==(const HistoryRangeRequest&) const = default;
};
struct SavedHistoryRangePlan {
  HistoryRangeRequest request;
  std::int64_t startMillis = 0, endMillis = 0, bucketMillis = 0;
  std::size_t bucketCount = 0;
  HistoryDate firstFile, lastFile;
  bool operator==(const SavedHistoryRangePlan&) const = default;
};
inline constexpr std::size_t SavedHistoryRangeMaximumFiles = 65536;
inline constexpr std::size_t SavedHistoryRangeMaximumBuckets = 16384;

// Explicit UTC epoch milliseconds within1970..9999; no clock read. Non-All
// offsets are <=0. All requires0 and firstMillis<=now. End is nominal/exclusive
// for buckets; the independent whole query includes end and clamps to now.
// All equality extends the nominal end one week, retaining its floored start;
// this bounded minimum-one-bucket case is an explicit native policy.
std::expected<SavedHistoryRangePlan, SavedHistoryError> planSavedHistoryRange(
    HistoryRangeRequest, std::optional<std::int64_t> firstMillis = {});

struct SavedHistoryRecord {
  std::int64_t timeMillis;
  HistoryCounters counters;
};
// Same strict grammar/limits/error priority as readSavedHistoryDay. Only
// positive-anchor observations are returned, with checked minute timestamps;
// s baselines are ignored and e retains/increments the source anchor.
// No input alias, clock, range filter or sum is retained here.
std::expected<std::vector<SavedHistoryRecord>, SavedHistoryError> readSavedHistoryRecords(
    std::string_view, std::stop_token = {});
struct SavedHistoryRangeBucket {
  std::int64_t startMillis = 0, endMillis = 0; // Exclusive nominal end.
  std::optional<HistoryCounters> totals;
  std::size_t observations = 0;
};
struct SavedHistoryRange {
  SavedHistoryRangePlan plan;
  std::vector<SavedHistoryRangeBucket> buckets;
  std::optional<HistoryCounters> totals; // Independent inclusive whole query.
  std::size_t presentFiles = 0, missingFiles = 0, observations = 0;
};

// Files must arrive exactly once, oldest-first, through the entire plan's
// inclusive whole-query filename range. Missing differs from present empty.
// Each input vector is bounded8192 and must contain positive minute-aligned
// source timestamps. File membership for each bucket is checked independently
// after its query end is clamped to now. Whole totals never sum bucket totals.
// First error poisons this owner; finish cannot publish a partial result.
class SavedHistoryRangeAccumulator final {
public:
  SavedHistoryRangeAccumulator(SavedHistoryRangeAccumulator&&) noexcept;
  SavedHistoryRangeAccumulator& operator=(SavedHistoryRangeAccumulator&&) noexcept;
  SavedHistoryRangeAccumulator(const SavedHistoryRangeAccumulator&) = delete;
  SavedHistoryRangeAccumulator& operator=(const SavedHistoryRangeAccumulator&) = delete;
  static std::expected<SavedHistoryRangeAccumulator, SavedHistoryError> create(SavedHistoryRangePlan);
  std::expected<void, SavedHistoryError> append(HistoryDate,
      std::optional<std::vector<SavedHistoryRecord>>, std::stop_token = {});
  std::expected<SavedHistoryRange, SavedHistoryError> finish() &&;
private:
  SavedHistoryRangeAccumulator() = default;
  std::expected<void, SavedHistoryError> fail(SavedHistoryError) noexcept;
  SavedHistoryRange result_{};
  std::size_t files_ = 0, appended_ = 0;
  bool usable_ = true;
  std::optional<SavedHistoryError> error_;
};

} // namespace biglybtc::core
