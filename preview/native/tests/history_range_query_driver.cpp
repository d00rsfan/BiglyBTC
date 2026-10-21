#include "history_range_view.hpp"

#include <array>
#include <charconv>
#include <exception>
#include <iostream>
#include <string_view>

namespace {
using namespace biglybtc;
bool integer(std::string_view text, std::int64_t& value) {
  const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
  return code == std::errc{} && end == text.data() + text.size();
}
void fields(const std::optional<core::HistoryCounters>& value) {
  for (std::size_t i = 0; i < 6; ++i) {
    std::cout << '\t'; if (value) { std::cout << (*value)[i]; } else { std::cout << '-'; }
  }
  std::cout << '\n';
}
}
// Explicit generated profile only; no discovery, default profile or hidden now.
int main(int argc, char** argv) {
  try {
    if (argc != 5) {
      std::cerr << "usage: history_range_query_driver GENERATED_PROFILE Hour|Day|Week|Month|Year|All OFFSET NOW_MILLIS\n";
      return 2;
    }
    constexpr std::array<std::string_view, 6> names{"Hour", "Day", "Week", "Month", "Year", "All"};
    std::optional<core::HistoryPeriod> period;
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (names[i] == argv[2]) { period = static_cast<core::HistoryPeriod>(i); }
    }
    core::HistoryRangeRequest request;
    if (!period || !integer(argv[3], request.offset) || !integer(argv[4], request.nowMillis)) { return 2; }
    request.period = *period;
    auto lease = storage::ProfileLease::acquire(argv[1]);
    if (!lease) { std::cerr << "lease-error\t" << static_cast<int>(lease.error().code) << '\t' << lease.error().message << '\n'; return 1; }
    auto value = storage::readSavedHistoryRangeView(*lease, request);
    if (!value) { std::cerr << "range-error\t" << static_cast<int>(value.error().code) << '\t' << value.error().message << '\n'; return 1; }
    std::cout << "plan\t" << value->plan.startMillis << '\t' << value->plan.endMillis << '\t'
        << value->plan.bucketMillis << '\t' << value->plan.bucketCount << '\n';
    std::cout << "whole\t" << (value->totals ? 1 : 0) << '\t' << value->observations; fields(value->totals);
    for (std::size_t i = 0; i < value->buckets.size(); ++i) {
      const auto& bucket = value->buckets[i];
      std::cout << "bucket\t" << i << '\t' << bucket.startMillis << '\t' << bucket.endMillis << '\t'
          << (bucket.totals ? 1 : 0) << '\t' << bucket.observations; fields(bucket.totals);
    }
    std::cout << "coverage\t" << value->presentFiles << '\t' << value->missingFiles << '\n';
    return std::cout ? 0 : 1;
  } catch (const std::exception& exception) { std::cerr << "error\t" << exception.what() << '\n'; return 1; }
}
