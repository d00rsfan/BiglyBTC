#include "history_month_view.hpp"

#include "history_view.hpp"

#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
std::unexpected<Error> cancelled() {
  return std::unexpected(Error{ErrorCode::Cancelled, "Saved 30-day read cancelled"});
}
} // namespace

std::expected<core::SavedHistoryMonth, Error> readSavedHistoryMonthView(
    const ProfileLease& lease, core::HistoryDate endingDate, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return cancelled(); }
    if (!lease.valid()) {
      return std::unexpected(Error{ErrorCode::UnsafePath, "A held profile lease is required for saved history"});
    }
    auto dates = core::savedHistoryMonthDates(endingDate);
    if (!dates) {
      return std::unexpected(Error{ErrorCode::InvalidFormat, "The complete 30-day UTC range is invalid"});
    }
    core::SavedHistoryMonth result{.endingDate = endingDate, .days = {}};
    for (std::size_t i = 0; i < dates->size(); ++i) {
      if (stop.stop_requested()) { return cancelled(); }
      auto day = readSavedHistoryDayView(lease, (*dates)[i], stop);
      if (stop.stop_requested()) { return cancelled(); }
      if (!day) { return std::unexpected(std::move(day.error())); }
      result.days[i] = {(*dates)[i], std::move(day->snapshot)};
    }
    if (stop.stop_requested()) { return cancelled(); }
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{stop.stop_requested() ? ErrorCode::Cancelled : ErrorCode::Io,
        stop.stop_requested() ? "Cancelled" : "No memory"});
  }
}

} // namespace biglybtc::storage
