#include "biglybtc/saved_history.hpp"

#include <charconv>
#include <chrono>
#include <limits>

namespace biglybtc::core {
namespace {
using I = std::int64_t;
constexpr I MinuteMillis = 60000;
constexpr I MaximumMinute = std::numeric_limits<I>::max() / MinuteMillis;
constexpr I MinimumMinute = std::numeric_limits<I>::min() / MinuteMillis;

std::expected<I, SavedHistoryError> integer(std::string_view field) {
  if (field.empty()) { return std::unexpected(SavedHistoryError::InvalidRecord); }
  std::size_t firstDigit = 0;
  if (field.front() == '+' || field.front() == '-') { firstDigit = 1; }
  if (firstDigit == field.size()) { return std::unexpected(SavedHistoryError::InvalidRecord); }
  for (std::size_t i = firstDigit; i < field.size(); ++i) {
    if (field[i] < '0' || field[i] > '9') { return std::unexpected(SavedHistoryError::InvalidRecord); }
  }
  if (field.front() == '+') { field.remove_prefix(1); }
  I result = 0;
  const auto parsed = std::from_chars(field.data(), field.data() + field.size(), result);
  if (parsed.ec == std::errc::result_out_of_range) { return std::unexpected(SavedHistoryError::Overflow); }
  if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size()) {
    return std::unexpected(SavedHistoryError::InvalidRecord);
  }
  return result;
}
bool add(I& target, I amount) noexcept {
  if ((amount > 0 && target > std::numeric_limits<I>::max() - amount)
      || (amount < 0 && target < std::numeric_limits<I>::min() - amount)) { return false; }
  target += amount;
  return true;
}
} // namespace

bool validHistoryDate(HistoryDate date) noexcept {
  if (date.year < 1970 || date.year > 9999 || date.month < 1 || date.month > 12
      || date.day < 1 || date.day > 31) { return false; }
  return std::chrono::year_month_day{std::chrono::year{date.year},
      std::chrono::month{date.month}, std::chrono::day{date.day}}.ok();
}

std::expected<SavedHistoryDay, SavedHistoryError> readSavedHistoryDay(
    std::string_view bytes, HistoryDate date, std::stop_token stop) {
  if (stop.stop_requested()) { return std::unexpected(SavedHistoryError::Cancelled); }
  if (!validHistoryDate(date)) { return std::unexpected(SavedHistoryError::InvalidDate); }
  if (bytes.size() > SavedHistoryMaximumBytes) { return std::unexpected(SavedHistoryError::Limit); }
  const auto day = std::chrono::sys_days{std::chrono::year{date.year}
      / std::chrono::month{date.month} / std::chrono::day{date.day}};
  const I dayBegin = static_cast<I>(day.time_since_epoch().count()) * 1440;
  const I dayEnd = dayBegin + 1440;
  SavedHistoryDay result{.date = date};
  std::optional<I> minute;
  std::size_t records = 0;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (stop.stop_requested()) { return std::unexpected(SavedHistoryError::Cancelled); }
    const auto newline = bytes.find('\n', offset);
    const auto end = newline == std::string_view::npos ? bytes.size() : newline;
    auto line = bytes.substr(offset, end - offset);
    offset = newline == std::string_view::npos ? bytes.size() : newline + 1;
    if (newline != std::string_view::npos && !line.empty() && line.back() == '\r') { line.remove_suffix(1); }
    if (line.size() > SavedHistoryMaximumLineBytes) { return std::unexpected(SavedHistoryError::Limit); }
    if (line.empty()) { continue; }
    if (++records > SavedHistoryMaximumRecords) { return std::unexpected(SavedHistoryError::Limit); }
    std::array<std::string_view, 9> fields{};
    std::size_t count = 0;
    for (;;) {
      if (count == fields.size()) { return std::unexpected(SavedHistoryError::InvalidRecord); }
      const auto comma = line.find(',');
      fields[count++] = line.substr(0, comma);
      if (comma == std::string_view::npos) { break; }
      line.remove_prefix(comma + 1);
    }
    const bool start = fields[0] == "s";
    const bool finish = fields[0] == "e";
    if ((start || finish) ? count != 9 : count != 6) {
      return std::unexpected(SavedHistoryError::InvalidRecord);
    }
    I timestamp = 0;
    std::size_t firstCounter = 0;
    if (start || finish) {
      auto version = integer(fields[1]);
      if (!version) { return std::unexpected(version.error()); }
      if (*version != 1) { return std::unexpected(SavedHistoryError::UnsupportedVersion); }
      auto parsedTime = integer(fields[2]);
      if (!parsedTime) { return std::unexpected(parsedTime.error()); }
      timestamp = *parsedTime;
      if (timestamp < MinimumMinute || timestamp > MaximumMinute) {
        return std::unexpected(SavedHistoryError::Overflow);
      }
      firstCounter = 3;
    }
    HistoryCounters counters{};
    for (std::size_t i = 0; i < counters.size(); ++i) {
      auto value = integer(fields[firstCounter + i]);
      if (!value) { return std::unexpected(value.error()); }
      counters[i] = *value;
    }
    if (start) {
      if (timestamp > 0) { minute = timestamp; }
      else { minute.reset(); }
      continue;
    }
    if (!minute) { continue; }
    if (*minute == MaximumMinute) { return std::unexpected(SavedHistoryError::Overflow); }
    ++*minute;
    if (*minute < dayBegin || *minute >= dayEnd) { ++result.outsideDayRecords; continue; }
    const auto hour = static_cast<std::size_t>((*minute - dayBegin) / 60);
    auto hourly = result.hours[hour].value_or(HistoryCounters{});
    auto totals = result.totals.value_or(HistoryCounters{});
    for (std::size_t i = 0; i < counters.size(); ++i) {
      if (!add(hourly[i], counters[i]) || !add(totals[i], counters[i])) {
        return std::unexpected(SavedHistoryError::Overflow);
      }
    }
    result.hours[hour] = hourly;
    result.totals = totals;
    ++result.observations;
  }
  if (stop.stop_requested()) { return std::unexpected(SavedHistoryError::Cancelled); }
  return result;
}

} // namespace biglybtc::core
