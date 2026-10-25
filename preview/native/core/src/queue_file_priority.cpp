#include "biglybtc/queue_file_priority.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace biglybtc::core {
std::expected<QueueFilePriorityStats, QueueFilePriorityError> queueFilePriorityStatistics(
    const compat::OriginalLayout& layout, std::span<const compat::FilePolicy> policies,
    std::span<const std::uint8_t> pieces) noexcept {
  using Error = QueueFilePriorityError;
  if (layout.files.size() > 100000 || pieces.size() > 1000000) { return std::unexpected(Error::Limit); }
  if (layout.bytes < 0 || layout.pieceLength <= 0
      || layout.pieceLength > compat::OriginalMaximumPieceLength) { return std::unexpected(Error::Geometry); }
  if (policies.size() != layout.files.size()) { return std::unexpected(Error::Policy); }
  const auto count = layout.bytes / layout.pieceLength + (layout.bytes % layout.pieceLength != 0);
  if (count > 1000000) { return std::unexpected(Error::Limit); }
  if (pieces.size() != static_cast<std::size_t>(count)
      || std::ranges::any_of(pieces, [](auto value) { return value > 1; })) { return std::unexpected(Error::Pieces); }
  std::int64_t end = 0;
  for (const auto& file : layout.files) {
    if (file.offset != end || file.bytes < 0 || file.bytes > layout.bytes - end) {
      return std::unexpected(Error::Geometry);
    }
    end += file.bytes;
  }
  if (end != layout.bytes) { return std::unexpected(Error::Geometry); }
  QueueFilePriorityStats result{std::numeric_limits<std::int32_t>::min(),
      std::numeric_limits<std::int32_t>::min(), 0, 0};
  std::uint32_t all = 0, incomplete = 0;
  for (std::size_t i = 0; i < layout.files.size(); ++i) {
    const auto& policy = policies[i];
    if (policy.skipped) { continue; }
    const auto& file = layout.files[i];
    bool complete = true;
    if (file.bytes != 0) {
      const auto last = (file.offset + file.bytes - 1) / layout.pieceLength;
      for (auto piece = file.offset / layout.pieceLength; piece <= last; ++piece) {
        if (!pieces[static_cast<std::size_t>(piece)]) { complete = false; break; }
      }
    }
    result[0] = std::max(result[0], policy.priority);
    all += std::bit_cast<std::uint32_t>(policy.priority);
    if (!complete) {
      result[1] = std::max(result[1], policy.priority);
      incomplete += std::bit_cast<std::uint32_t>(policy.priority);
    }
  }
  result[2] = std::bit_cast<std::int32_t>(all);
  result[3] = std::bit_cast<std::int32_t>(incomplete);
  return result;
}
} // namespace biglybtc::core
