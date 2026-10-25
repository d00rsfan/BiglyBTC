#pragma once

#include "biglybtc/bencode.hpp"

#include <span>

namespace biglybtc::compat {

struct FilePolicy {
  bool skipped = false;
  std::int32_t priority = 0;
  bool operator==(const FilePolicy&) const = default;
};
struct PolicyError { std::string message; };

// Reads an individual original downloads.config entry. No file or global state
// is changed. Missing/sparse saved elements have Java's normal defaults.
std::expected<std::vector<FilePolicy>, PolicyError> originalFilePolicy(
    const core::Node& entry, std::size_t files, std::size_t maximumFiles = 100000);

// Original compressed policy encoding; validates signed round-trip and the seven
// representable selected priority ranks. Padding is validated by the caller.
std::expected<std::string, PolicyError> encodeOriginalFilePolicy(std::span<const FilePolicy>);

// Preserve every distinct non-skipped priority ordering. More than seven ranks
// cannot be represented by libtorrent without collapsing original distinctions.
std::expected<std::vector<std::uint8_t>, PolicyError> linearPriorityRanks(std::span<const FilePolicy> policy);

} // namespace biglybtc::compat
