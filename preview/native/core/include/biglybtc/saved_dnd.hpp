#pragma once

#include <cstdint>
#include <optional>

namespace biglybtc::core {

struct Node;

enum class SavedDndIssue : std::uint32_t {
  AttributesMissing = 1U << 0,
  AttributesType = 1U << 1,
  ParametersType = 1U << 2,
  SkippedBytesType = 1U << 3,
  SkippedDownloadedBytesType = 1U << 4,
  DndFlagsType = 1U << 5,
  TotalUnresolved = 1U << 6,
  CompletedMissing = 1U << 7,
  CompletedRange = 1U << 8,
  SkippedUnresolved = 1U << 9,
  SkippedRange = 1U << 10,
  SkippedDownloadedRange = 1U << 11,
  CountersInconsistent = 1U << 12,
  ArithmeticOverflow = 1U << 13,
  FlagsMissing = 1U << 14
};

enum class SavedDndDefault : std::uint32_t {
  SkippedBytes = 1U << 0,            // Original missing skipfss defaults to -1.
  SkippedDownloadedBytes = 1U << 1  // Original missing skipbdl defaults to 0.
};

constexpr bool hasSavedDndIssue(std::uint32_t bits, SavedDndIssue issue) noexcept {
  return (bits & static_cast<std::uint32_t>(issue)) != 0;
}
constexpr bool hasSavedDndDefault(std::uint32_t bits, SavedDndDefault value) noexcept {
  return (bits & static_cast<std::uint32_t>(value)) != 0;
}

// Fixed owned facts. Integer optionals contain stored values only; defaults do
// not masquerade as raw saved fields. Presence distinguishes bad types/missing.
struct SavedDndFacts {
  bool attributesPresent = false;
  bool attributesDictionary = false;
  bool parametersPresent = false;
  bool parametersDictionary = false;
  bool skippedBytesPresent = false;
  bool skippedDownloadedBytesPresent = false;
  bool dndFlagsPresent = false;
  std::optional<std::int64_t> skippedBytes{};
  std::optional<std::int64_t> skippedDownloadedBytes{};
  std::optional<std::int64_t> dndFlags{};
  std::uint32_t defaults = 0;
  std::uint32_t diagnostics = 0;
};

// Saved/no-DiskManager candidates, not public assumedComplete or live progress.
// Independent fields stay available when another field is unknown/invalid.
struct SavedDndProgress {
  std::optional<std::int64_t> selectedBytes{};
  std::optional<std::int64_t> selectedRemaining{};
  std::optional<std::int32_t> percentThousandths{};
  std::optional<bool> cachedHasDnd{};
  std::optional<bool> cachedCompleteNoDnd{};
  std::optional<bool> controllerNoDiskCompleteExcludingDnd{};
  std::optional<bool> controllerNoDiskCompleteIncludingDnd{};
  std::uint32_t defaults = 0;
  std::uint32_t diagnostics = 0;
};

// Borrow a decoded attributes dictionary for this call only. No allocation,
// mutation, callbacks or IO. Unknown keys remain with the source document.
SavedDndFacts readSavedDndFacts(const Node* attributes) noexcept;

// Positive cached size only. Missing skipfss is unresolved; missing skipbdl in
// valid attributes uses the tagged original zero default. Invalid ranges and
// cross-counter inconsistencies affect only dependent metrics. Zero selected
// size preserves the original Done=1000 early return without C/B dependence.
SavedDndProgress projectSavedDndProgress(std::optional<std::int64_t> cachedBytes,
    std::optional<std::int64_t> completedBytes, const SavedDndFacts& facts) noexcept;

} // namespace biglybtc::core
