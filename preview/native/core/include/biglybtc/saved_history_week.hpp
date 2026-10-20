#pragma once

#include "biglybtc/saved_history.hpp"

namespace biglybtc::core {

inline constexpr std::size_t SavedHistoryWeekDays = 7;
struct SavedHistoryWeekDay {
  HistoryDate date;
  std::optional<SavedHistoryDay> snapshot;
};
struct SavedHistoryWeek {
  HistoryDate endingDate;
  std::array<SavedHistoryWeekDay, SavedHistoryWeekDays> days;
};
// Exactly seven UTC calendar dates, oldest first, ending at endingDate. The
// complete interval must remain within1970..9999; never truncate or clamp.
// These identify independent daily file projections, not a whole-period sum.
std::expected<std::array<HistoryDate, SavedHistoryWeekDays>, SavedHistoryError>
savedHistoryWeekDates(HistoryDate endingDate) noexcept;

} // namespace biglybtc::core
