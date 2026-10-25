#pragma once

#include "biglybtc/bencode.hpp"

#include <expected>
#include <optional>
#include <span>

namespace biglybtc::compat {

inline constexpr std::size_t MaximumOriginalLinkFiles = 100000;
inline constexpr std::size_t MaximumOriginalLinkRows = 400000;
inline constexpr std::size_t MaximumOriginalLinkRowBytes = 16384;
inline constexpr std::size_t MaximumOriginalLinkBytes = 64 * 1024 * 1024;

enum class OriginalFileLinksErrorCode { Type, Limit, InvalidBase, Encoding, AmbiguousBase, Memory };
struct OriginalFileLinksError {
  OriginalFileLinksErrorCode code;
  std::size_t row = 0;
  std::string_view message;
};
enum class OriginalFileLinkDiagnosticCode {
  NonStringRow, MalformedRow, InvalidIndex, IndexedOutsideMetadata,
  UnresolvedNegative, ExtraFieldsIgnored
};
struct OriginalFileLinkDiagnostic {
  OriginalFileLinkDiagnosticCode code;
  std::size_t row;
};
struct OriginalFileLink {
  std::string from;
  std::string target;
  std::size_t sourceRow;
  bool migrated;
};
struct OriginalFileLinks {
  // No entry means the original unlinked base path. A present empty target is
  // intentionally distinct: Java retains it when a later field exists. Callers
  // must reject it using their ordinary target-path authority checks.
  std::vector<std::optional<OriginalFileLink>> byIndex;
  std::vector<OriginalFileLinkDiagnostic> diagnostics;
};

// Mirrors LinkFileMap's ordered put/remove projection. Negative indices match
// the exact ORIGINAL absolute path, never an earlier linked destination. An
// indexed row's obsolete 'from' is informational, as in the original reader.
// Bases come from the qualified save-root/layout resolver; no filesystem access,
// path normalization, document mutation, or borrowed views escape this function.
// This is path selection only: the caller MUST retain all target authentication,
// no-follow, identity, alias, profile exclusion and transaction/CAS checks.
std::expected<OriginalFileLinks, OriginalFileLinksError> resolveOriginalFileLinks(
    const core::Node* links, std::span<const std::string> originalAbsoluteBasePaths);

} // namespace biglybtc::compat
