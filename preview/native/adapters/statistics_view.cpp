#include "statistics_view.hpp"

#include "file_io.hpp"

#include <array>
#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
Error cancelled() { return {ErrorCode::Cancelled, "Saved statistics read cancelled"}; }
struct Candidate {
  const char* name;
  RecoverySource source;
};
constexpr Candidate Primary{"BiglyBT.statistics", RecoverySource::Primary};
constexpr Candidate Backup{"BiglyBT.statistics.bak", RecoverySource::Backup};
constexpr Candidate Saving{"BiglyBT.statistics.saving", RecoverySource::Saving};
} // namespace

std::expected<SavedStatisticsView, Error> readSavedStatisticsView(
    const ProfileLease& lease, std::stop_token stop) {
  if (stop.stop_requested()) { return std::unexpected(cancelled()); }
  if (!lease.valid()) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "A held profile lease is required for saved statistics"});
  }
  try {
    bool anyPresent = false;
    std::array<Candidate, 3> candidates{Primary, Saving, Backup};
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      const auto& candidate = candidates[index];
      auto bytes = detail::readFile(lease.directoryHandle(), candidate.name, core::Limits{}.inputBytes);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!bytes) { return std::unexpected(std::move(bytes.error())); }
      if (!bytes->has_value()) { continue; }
      anyPresent = true;
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      auto snapshot = core::readSavedStatistics(**bytes);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (snapshot) {
        SavedStatisticsView view;
        view.snapshot.emplace(std::move(*snapshot));
        view.recoverySource = candidate.source;
        return view;
      }
      switch (snapshot.error()) {
        case core::SavedStatisticsError::InvalidDocument:
        case core::SavedStatisticsError::Limit:
          // Parse/whole-document failure permits the source recovery order.
          break;
        case core::SavedStatisticsError::InvalidAll:
          return std::unexpected(Error{ErrorCode::InvalidFormat, "Saved statistics 'all' must be a dictionary"});
        case core::SavedStatisticsError::Memory:
          return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for saved statistics"});
        default:
          return std::unexpected(Error{ErrorCode::InvalidFormat, "Unsupported saved statistics projection result"});
      }
      if (index == 0) { candidates[1] = Backup; candidates[2] = Saving; }
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (anyPresent) {
      return std::unexpected(Error{ErrorCode::InvalidFormat, "Saved statistics candidates exist but no complete bounded dictionary was read"});
    }
    return SavedStatisticsView{};
  } catch (const std::bad_alloc&) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for saved statistics"});
  }
}

} // namespace biglybtc::storage
