#pragma once

#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <span>

namespace biglybtc::core {
using QueueFilePriorityStats = std::array<std::int32_t, 4>;
enum class QueueFilePriorityError { Geometry, Policy, Pieces, Limit };
// Original calcFilePriorityStats: max all/incomplete, signed32 wrapped sums
// all/incomplete. Only explicit skipped policies are excluded. Empty real files
// are complete; padding retains its ORIGINAL slot/policy. No saved-progress guess.
// Actual original file downloaded counts are verified-piece intersections.
// O(files + pieces), no allocation, I/O, clock or mutable observation state.
std::expected<QueueFilePriorityStats, QueueFilePriorityError> queueFilePriorityStatistics(
    const compat::OriginalLayout&, std::span<const compat::FilePolicy>,
    std::span<const std::uint8_t> verifiedPieces) noexcept;
} // namespace biglybtc::core
