#pragma once

#include "biglybtc/bencode.hpp"

#include <optional>
#include <span>

namespace biglybtc::compat {

struct FilePolicy {
  bool skipped = false;
  std::int32_t priority = 0;
  bool operator==(const FilePolicy&) const = default;
};
struct PolicyError { std::string message; };

// Pure plan for the actual DiskManagerUtil.loadFilePriorities call. No
// replacement means existing constructor/runtime values must remain intact.
// This is distinct from the raw saved projection below. Per-item nullopt models
// a caught absent/null/non-Long slot; absence of the entire list is separate.
// The caller owns source validation, actual file census and applying the plan.
struct OriginalFilePriorityLoad {
  std::optional<std::vector<FilePolicy>> replacement;
  // Original in-memory user data only; never removal of persisted fields.
  bool clearCachedPriorities = false;
};
std::expected<OriginalFilePriorityLoad, PolicyError> planOriginalFilePriorityLoad(
    std::size_t files,
    std::optional<std::span<const std::optional<std::int64_t>>> saved,
    std::size_t maximumFiles = 100000);

// Reads an individual original downloads.config entry. No file or global state
// is changed. Missing/sparse raw elements project as normal; runtime defaults
// require the separate priority-load plan and the actual file-set owner.
std::expected<std::vector<FilePolicy>, PolicyError> originalFilePolicy(
    const core::Node& entry, std::size_t files, std::size_t maximumFiles = 100000);

// Original compressed policy encoding; validates signed round-trip and the seven
// representable selected priority ranks. Padding is validated by the caller.
std::expected<std::string, PolicyError> encodeOriginalFilePolicy(std::span<const FilePolicy>);

// Preserve every distinct non-skipped priority ordering. More than seven ranks
// cannot be represented by libtorrent without collapsing original distinctions.
std::expected<std::vector<std::uint8_t>, PolicyError> linearPriorityRanks(std::span<const FilePolicy> policy);

} // namespace biglybtc::compat
