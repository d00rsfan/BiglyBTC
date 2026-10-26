#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>
namespace biglybtc::core {
enum class QueuePositionsError { Limit, Identity, Position, Ambiguous, Group, Memory };
struct QueuePositionInput {
  std::uint64_t token=0;
  std::optional<std::int64_t> savedPosition{};
  bool complete=false,persistent=true;
};
// Explicit accepted group transitions, in observation order. All inputs carry
// their CURRENT group; the source moves each changed item through global N+1.
struct QueuePositionTransition {std::uint64_t token=0;bool complete=false,top=false;};
// Results parallel the original inventory; this does not reorder original rows.
// Missing/-1 positions append while loading; source Long.intValue is retained.
// Same-pool duplicate positions and subtraction-overflow comparator domains are
// deliberately unsupported: the original persistent tie comparator is not strict.
std::expected<std::vector<std::uint32_t>,QueuePositionsError> normalizeQueuePositions(
    std::span<const QueuePositionInput>,std::span<const QueuePositionTransition> = {});
} // namespace biglybtc::core
