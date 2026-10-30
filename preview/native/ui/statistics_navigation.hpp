#pragma once

#include <biglybtc/saved_history_week.hpp>
#include <biglybtc/saved_history_month.hpp>
#include <biglybtc/saved_history_range.hpp>

#include "saved_history_traffic.hpp"

#include <type_traits>

namespace biglybtc::ui {

// Per-window navigation choices only. No widget, packet, session or saved result
// is retained; a new Page starts with empty data and its existing lazy reads.
struct StatisticsNavigation {
  int section = 0, activity = 2, transfers = 0, history = 0;
  core::HistoryDate dailyDate{};
  core::HistoryDate weekEnding{1970, 1, 7};
  bool hourly = false;
  SavedHistoryTraffic dailyTraffic = SavedHistoryTraffic::Payload;
  SavedHistoryTraffic weekTraffic = SavedHistoryTraffic::Payload;
  core::HistoryDate monthEnding{1970, 1, 30};
  SavedHistoryTraffic monthTraffic = SavedHistoryTraffic::Payload;
  core::HistoryPeriod rangePeriod = core::HistoryPeriod::Week;
  std::int64_t rangeOffset = 0;
  SavedHistoryTraffic rangeTraffic = SavedHistoryTraffic::Payload;
  bool operator==(const StatisticsNavigation&) const = default;
};
static_assert(std::is_trivially_copyable_v<StatisticsNavigation>);

inline bool validStatisticsNavigation(const StatisticsNavigation& value) noexcept {
  return value.section >= 0 && value.section <= 10
      && value.activity >= 0 && value.activity <= 2
      && value.transfers >= 0 && value.transfers <= 3
      && value.history >= 0 && value.history <= 3
      && core::validHistoryDate(value.dailyDate)
      && core::savedHistoryWeekDates(value.weekEnding).has_value()
      && validSavedHistoryTraffic(value.dailyTraffic)
      && validSavedHistoryTraffic(value.weekTraffic)
      && core::savedHistoryMonthDates(value.monthEnding).has_value()
      && validSavedHistoryTraffic(value.monthTraffic)
      && value.rangePeriod >= core::HistoryPeriod::Hour && value.rangePeriod <= core::HistoryPeriod::All
      && value.rangeOffset <= 0 && (value.rangePeriod != core::HistoryPeriod::All || value.rangeOffset == 0)
      && validSavedHistoryTraffic(value.rangeTraffic);
}

} // namespace biglybtc::ui
