#pragma once

#include <biglybtc/saved_history_week.hpp>
#include <biglybtc/saved_history_month.hpp>

#include "saved_history_traffic.hpp"

#include <type_traits>

namespace biglybtc::ui {

// Per-window navigation choices only. No widget, packet, session or saved result
// is retained; a new Page starts with empty data and its existing lazy reads.
struct StatisticsNavigation {
  int section = 0, activity = 0, transfers = 0, history = 0;
  core::HistoryDate dailyDate{};
  core::HistoryDate weekEnding{1970, 1, 7};
  bool hourly = false;
  SavedHistoryTraffic dailyTraffic = SavedHistoryTraffic::Payload;
  SavedHistoryTraffic weekTraffic = SavedHistoryTraffic::Payload;
  core::HistoryDate monthEnding{1970, 1, 30};
  SavedHistoryTraffic monthTraffic = SavedHistoryTraffic::Payload;
  bool operator==(const StatisticsNavigation&) const = default;
};
static_assert(std::is_trivially_copyable_v<StatisticsNavigation>);

inline bool validStatisticsNavigation(const StatisticsNavigation& value) noexcept {
  return value.section >= 0 && value.section <= 1
      && value.activity >= 0 && value.activity <= 1
      && value.transfers >= 0 && value.transfers <= 1
      && value.history >= 0 && value.history <= 2
      && core::validHistoryDate(value.dailyDate)
      && core::savedHistoryWeekDates(value.weekEnding).has_value()
      && validSavedHistoryTraffic(value.dailyTraffic)
      && validSavedHistoryTraffic(value.weekTraffic)
      && core::savedHistoryMonthDates(value.monthEnding).has_value()
      && validSavedHistoryTraffic(value.monthTraffic);
}

} // namespace biglybtc::ui
