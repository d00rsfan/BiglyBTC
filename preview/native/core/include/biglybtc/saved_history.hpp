#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <string_view>

namespace biglybtc::core {

struct HistoryDate {
  int year = 1970;
  unsigned month = 1;
  unsigned day = 1;
  bool operator==(const HistoryDate&) const = default;
};
// Original LongTermStats order: protocol upload, payload upload, protocol
// download, payload download, DHT upload, DHT download; signed byte adjustments.
using HistoryCounters = std::array<std::int64_t, 6>;
struct SavedHistoryDay {
  HistoryDate date;
  std::array<std::optional<HistoryCounters>, 24> hours{};
  std::optional<HistoryCounters> totals{};
  std::size_t observations = 0;
  std::size_t outsideDayRecords = 0;
};
enum class SavedHistoryError {
  InvalidDate, InvalidRecord, UnsupportedVersion, Limit, Overflow, Cancelled, Memory
};
inline constexpr std::size_t SavedHistoryMaximumBytes = 1024 * 1024;
inline constexpr std::size_t SavedHistoryMaximumRecords = 8192;
inline constexpr std::size_t SavedHistoryMaximumLineBytes = 512;

bool validHistoryDate(HistoryDate) noexcept;
// Pure owned primitive projection, no retained borrowed bytes or allocations.
// VERSION1 s/e have nine fields; ordinary deltas have six. LF/CRLF and an
// unterminated complete final line are accepted. Empty lines are ignored; line
// byte limits exclude LF/CRLF. Every nonblank record is counted and validated,
// including unanchored/outside-day records. Signed decimal accepts +/- and zeros.
// s minute<=0 disables the Java positive anchor; deltas/e without one are ignored.
// Anchored delta/e advances one minute; e timestamp never replaces this clock or
// clears the anchor. Session baselines are parsed, never added. Outside-day
// anchored observations are counted separately, not included in any sum.
// All s/e timestamp*60000, reconstructed timestamps and selected sums are checked.
// Limits/version/malformed/overflow/cancellation fail the entire result; these
// strict native policies do not claim Java malformed-record/overflow equality.
std::expected<SavedHistoryDay, SavedHistoryError> readSavedHistoryDay(
    std::string_view bytes, HistoryDate date, std::stop_token stop = {});

} // namespace biglybtc::core
