#include "biglybtc/saved_history_month.hpp"

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
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
// Independent integer Gregorian ordinal, inherited from the qualified week-test
// fixture. No production date iterator or chrono predicts the differences.
std::int64_t ordinal(HistoryDate date) {
  constexpr std::array<int, 12> BeforeMonth{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  const std::int64_t previous = date.year - 1;
  return previous * 365 + previous / 4 - previous / 100 + previous / 400
      + BeforeMonth[date.month - 1] + (date.month > 2 && leap(date.year) ? 1 : 0) + date.day - 1;
}
void contiguous(HistoryDate end, HistoryDate first) {
  const auto result = savedHistoryMonthDates(end); check(result.has_value(), "valid30-day interval rejected");
  check(result->size() == 30 && result->front() == first && result->back() == end, "fixed inclusive range endpoints differ");
  for (std::size_t i = 0; i < result->size(); ++i) {
    check(validHistoryDate((*result)[i]) && ordinal((*result)[i]) == ordinal(end) - 29 + static_cast<std::int64_t>(i),
        "not exactly30 distinct oldest-first UTC dates");
  }
}
void invalid(HistoryDate end) {
  const auto result = savedHistoryMonthDates(end);
  check(!result && result.error() == SavedHistoryError::InvalidDate, "invalid/short interval accepted or clamped");
}
} // namespace

static_assert(biglybtc::core::SavedHistoryMonthDays == 30);
static_assert(noexcept(biglybtc::core::savedHistoryMonthDates({1970, 1, 30})));

int main() {
  try {
    run("first-complete-thirty-days-exact-epoch-array", [] {
      const auto result = savedHistoryMonthDates({1970, 1, 30}); check(result.has_value(), "epoch interval rejected");
      for (unsigned i = 0; i < 30; ++i) { check((*result)[i] == HistoryDate{1970, 1, i + 1}, "epoch range normalized/truncated"); }
      contiguous({1970, 1, 30}, {1970, 1, 1});
    });
    run("every-short-epoch-prefix-rejected-without-truncation", [] {
      for (unsigned day = 1; day < 30; ++day) { invalid({1970, 1, day}); }
    });
    run("upper-domain-exact-array-does-not-wrap", [] {
      const auto result = savedHistoryMonthDates({9999, 12, 31}); check(result.has_value(), "upper interval rejected");
      for (unsigned i = 0; i < 30; ++i) { check((*result)[i] == HistoryDate{9999, 12, i + 2}, "upper range wrapped"); }
      invalid({10000, 1, 1});
    });
    run("leap-February-has-twenty-nine-members-before-March", [] {
      const auto result = savedHistoryMonthDates({2024, 3, 1}); check(result.has_value(), "leap range rejected");
      for (unsigned i = 0; i < 29; ++i) { check((*result)[i] == HistoryDate{2024, 2, i + 1}, "leap-February member differs"); }
      check(result->back() == HistoryDate{2024, 3, 1}, "inclusive March endpoint missing");
      contiguous({2024, 3, 2}, {2024, 2, 2});
    });
    run("century-rules-2000-leap-2100-common", [] {
      contiguous({2000, 3, 1}, {2000, 2, 1}); contiguous({2100, 3, 1}, {2100, 1, 31});
      const auto common = savedHistoryMonthDates({2100, 3, 1}); check(common.has_value(), "common century rejected");
      check((*common)[1] == HistoryDate{2100, 2, 1} && (*common)[28] == HistoryDate{2100, 2, 28}, "common century invented February29");
    });
    run("fixed-thirty-never-a-calendar-month", [] {
      contiguous({2024, 1, 31}, {2024, 1, 2}); contiguous({2023, 2, 28}, {2023, 1, 30});
      contiguous({2024, 4, 30}, {2024, 4, 1}); contiguous({2024, 5, 31}, {2024, 5, 2});
    });
    run("cross-year-keeps-oldest-first-ownership", [] {
      contiguous({2024, 1, 10}, {2023, 12, 12}); contiguous({2000, 1, 1}, {1999, 12, 3});
    });
    run("DST-dates-are-ordinary-UTC-calendar-members", [] {
      contiguous({2024, 3, 31}, {2024, 3, 2}); contiguous({2024, 10, 31}, {2024, 10, 2});
    });
    run("invalid-fields-never-normalized", [] {
      for (const auto end : {HistoryDate{1969, 12, 31}, HistoryDate{2024, 2, 30}, HistoryDate{2100, 2, 29},
          HistoryDate{2024, 0, 30}, HistoryDate{2024, 13, 30}, HistoryDate{2024, 1, 0}, HistoryDate{2024, 1, 32},
          HistoryDate{std::numeric_limits<int>::min(), 1, 30}, HistoryDate{std::numeric_limits<int>::max(), 1, 30},
          HistoryDate{2024, std::numeric_limits<unsigned>::max(), 30}, HistoryDate{2024, 1, std::numeric_limits<unsigned>::max()}}) { invalid(end); }
    });
    run("date-results-copy-move-and-next-call-are-independent", [] {
      HistoryDate input{2024, 1, 10}; auto first = savedHistoryMonthDates(input); check(first.has_value(), "owned fixture rejected");
      const auto copied = *first; auto moved = std::move(*first); input = {9999, 12, 31}; const auto later = savedHistoryMonthDates(input);
      check(later && copied == moved && copied.front() == HistoryDate{2023, 12, 12} && copied.back() == HistoryDate{2024, 1, 10}, "result borrowed input/call state");
    });
    run("all-domain-month-ends-agree-with-independent-Gregorian-ordinals", [] {
      constexpr std::array<unsigned, 12> Lengths{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for (int year = 1970; year <= 9999; ++year) { for (unsigned month = 1; month <= 12; ++month) {
        const HistoryDate end{year, month, Lengths[month - 1] + (month == 2 && leap(year) ? 1U : 0U)};
        const auto actual = savedHistoryMonthDates(end); check(actual.has_value(), "valid month end rejected");
        for (std::size_t i = 0; i < 30; ++i) { check(validHistoryDate((*actual)[i]) && ordinal((*actual)[i]) == ordinal(end) - 29 + static_cast<std::int64_t>(i), "domain ordinal disagreement"); }
      } }
    });
    std::cout << "PASS " << cases << " saved-history-month date cases; " << assertions
              << " assertions; fixed30 UTC dates; no Java/runtime equality credit\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL saved-history-month dates: " << e.what() << '\n'; return 1; }
}
