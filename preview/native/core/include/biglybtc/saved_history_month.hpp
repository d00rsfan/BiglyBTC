#pragma once

#include "biglybtc/saved_history.hpp"

namespace biglybtc::core {

inline constexpr std::size_t SavedHistoryMonthDays = 30;
struct SavedHistoryMonthDay {
  HistoryDate date;
  std::optional<SavedHistoryDay> snapshot;
};
struct SavedHistoryMonth {
  HistoryDate endingDate;
  std::array<SavedHistoryMonthDay, SavedHistoryMonthDays> days;
};
// Exactly 30 consecutive UTC dates, oldest first and ending inclusive. The
// complete interval must stay within 1970..9999; never truncate or clamp.
// This fixed 30-day span is not a varying calendar month or a period total.
std::expected<std::array<HistoryDate, SavedHistoryMonthDays>, SavedHistoryError>
savedHistoryMonthDates(HistoryDate endingDate) noexcept;

} // namespace biglybtc::core
