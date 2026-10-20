#include "biglybtc/saved_history_month.hpp"

#include <chrono>

namespace biglybtc::core {

std::expected<std::array<HistoryDate, SavedHistoryMonthDays>, SavedHistoryError>
savedHistoryMonthDates(HistoryDate endingDate) noexcept {
  if (!validHistoryDate(endingDate)) { return std::unexpected(SavedHistoryError::InvalidDate); }
  const auto end = std::chrono::sys_days{std::chrono::year{endingDate.year}
      / std::chrono::month{endingDate.month} / std::chrono::day{endingDate.day}};
  const auto first = end - std::chrono::days{29};
  if (first < std::chrono::sys_days{std::chrono::year{1970} / 1 / 1}) {
    return std::unexpected(SavedHistoryError::InvalidDate);
  }
  std::array<HistoryDate, SavedHistoryMonthDays> result{};
  for (std::size_t i = 0; i < result.size(); ++i) {
    const std::chrono::year_month_day date{first + std::chrono::days{static_cast<int>(i)}};
    result[i] = {static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day())};
  }
  return result;
}

} // namespace biglybtc::core
