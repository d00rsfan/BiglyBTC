#include "biglybtc/saved_history_week.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace biglybtc::core;
std::size_t cases = 0, assertions = 0;
void check(bool condition, std::string_view message) {
  ++assertions; if (!condition) { throw std::runtime_error(std::string(message)); }
}
template<class F> void run(std::string_view name, F body) {
  try { body(); ++cases; } catch (const std::exception& e) { throw std::runtime_error(std::string(name) + ": " + e.what()); }
}
void exact(HistoryDate end, const std::array<HistoryDate, SavedHistoryWeekDays>& expected) {
  const auto actual = savedHistoryWeekDates(end);
  check(actual.has_value(), "valid complete week rejected");
  check(*actual == expected && actual->back() == end, "seven UTC dates/order differ");
}
void invalid(HistoryDate end) {
  const auto result = savedHistoryWeekDates(end);
  check(!result && result.error() == SavedHistoryError::InvalidDate, "invalid complete week accepted/clamped");
}
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
// Independent integer Gregorian ordinal; no chrono or production helper is
// used to predict date differences in the domain-wide month-end cohort.
std::int64_t ordinal(HistoryDate date) {
  constexpr std::array<int, 12> BeforeMonth{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  const std::int64_t previous = date.year - 1;
  return previous * 365 + previous / 4 - previous / 100 + previous / 400
      + BeforeMonth[date.month - 1] + (date.month > 2 && leap(date.year) ? 1 : 0) + date.day - 1;
}
} // namespace

static_assert(noexcept(biglybtc::core::savedHistoryWeekDates({1970, 1, 7})));

int main() {
  try {
    run("earliest-complete-week-includes-epoch", [] {
      exact({1970, 1, 7}, {{{1970, 1, 1}, {1970, 1, 2}, {1970, 1, 3}, {1970, 1, 4},
          {1970, 1, 5}, {1970, 1, 6}, {1970, 1, 7}}});
      for (unsigned day = 1; day < 7; ++day) { invalid({1970, 1, day}); }
    });
    run("latest-complete-week-no-domain-wrap", [] {
      exact({9999, 12, 31}, {{{9999, 12, 25}, {9999, 12, 26}, {9999, 12, 27}, {9999, 12, 28},
          {9999, 12, 29}, {9999, 12, 30}, {9999, 12, 31}}});
      invalid({10000, 1, 1});
    });
    run("leap-day-and-four-hundred-year-century", [] {
      exact({2024, 3, 2}, {{{2024, 2, 25}, {2024, 2, 26}, {2024, 2, 27}, {2024, 2, 28},
          {2024, 2, 29}, {2024, 3, 1}, {2024, 3, 2}}});
      exact({2000, 3, 2}, {{{2000, 2, 25}, {2000, 2, 26}, {2000, 2, 27}, {2000, 2, 28},
          {2000, 2, 29}, {2000, 3, 1}, {2000, 3, 2}}});
    });
    run("nonleap-century-and-month-transition", [] {
      exact({2100, 3, 2}, {{{2100, 2, 24}, {2100, 2, 25}, {2100, 2, 26}, {2100, 2, 27},
          {2100, 2, 28}, {2100, 3, 1}, {2100, 3, 2}}});
      exact({2024, 5, 3}, {{{2024, 4, 27}, {2024, 4, 28}, {2024, 4, 29}, {2024, 4, 30},
          {2024, 5, 1}, {2024, 5, 2}, {2024, 5, 3}}});
    });
    run("year-transition-preserves-seven-distinct-dates", [] {
      exact({2024, 1, 3}, {{{2023, 12, 28}, {2023, 12, 29}, {2023, 12, 30}, {2023, 12, 31},
          {2024, 1, 1}, {2024, 1, 2}, {2024, 1, 3}}});
    });
    run("UTC-calendar-has-no-DST-short-or-long-day", [] {
      exact({2024, 3, 31}, {{{2024, 3, 25}, {2024, 3, 26}, {2024, 3, 27}, {2024, 3, 28},
          {2024, 3, 29}, {2024, 3, 30}, {2024, 3, 31}}});
      exact({2024, 10, 29}, {{{2024, 10, 23}, {2024, 10, 24}, {2024, 10, 25}, {2024, 10, 26},
          {2024, 10, 27}, {2024, 10, 28}, {2024, 10, 29}}});
    });
    run("invalid-fields-never-normalize", [] {
      for (const auto date : {HistoryDate{1969, 12, 31}, HistoryDate{2100, 2, 29}, HistoryDate{2024, 2, 30},
          HistoryDate{2024, 0, 7}, HistoryDate{2024, 13, 7}, HistoryDate{2024, 1, 0}, HistoryDate{2024, 1, 32},
          HistoryDate{std::numeric_limits<int>::min(), 1, 7}, HistoryDate{std::numeric_limits<int>::max(), 1, 7},
          HistoryDate{2024, std::numeric_limits<unsigned>::max(), 7}, HistoryDate{2024, 1, std::numeric_limits<unsigned>::max()}}) {
        invalid(date);
      }
    });
    run("owned-date-array-has-no-input-or-call-state", [] {
      HistoryDate input{2024, 1, 3}; auto original = savedHistoryWeekDates(input);
      check(original.has_value(), "owned fixture rejected"); auto copy = *original; auto moved = std::move(*original);
      input = {9999, 12, 31}; const auto later = savedHistoryWeekDates(input);
      check(later && copy == moved && copy.front() == HistoryDate{2023, 12, 28}
          && copy.back() == HistoryDate{2024, 1, 3}, "date results borrowed/shared later call state");
    });
    run("all-domain-month-ends-independent-integer-ordinals", [] {
      constexpr std::array<unsigned, 12> Lengths{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for (int year = 1970; year <= 9999; ++year) {
        for (unsigned month = 1; month <= 12; ++month) {
          const HistoryDate end{year, month, Lengths[month - 1] + (month == 2 && leap(year) ? 1U : 0U)};
          const auto dates = savedHistoryWeekDates(end); check(dates.has_value(), "valid month end rejected");
          for (std::size_t i = 0; i < SavedHistoryWeekDays; ++i) {
            check(validHistoryDate((*dates)[i]) && ordinal((*dates)[i]) == ordinal(end) - 6 + static_cast<std::int64_t>(i),
                "month-end cohort disagrees with independent Gregorian arithmetic");
          }
        }
      }
    });
    std::cout << "PASS " << cases << " saved-history-week date cases; " << assertions
              << " assertions; pure UTC dates, no clock/IO or whole-period equality claim\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL saved-history-week dates: " << e.what() << '\n'; return 1; }
}
